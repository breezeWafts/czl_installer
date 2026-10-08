#include "FileOperationManager.h"

#include "TIHelperWrapper.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QThread>

#include <atomic>
#include <vector>

#include <windows.h>

#ifdef Q_OS_WIN
#  include <objbase.h>
#  include <oleauto.h>
#  include <shldisp.h>
#endif

namespace {

constexpr DWORD kCopyBufferSize = 256 * 1024;   // 256 KiB
constexpr int   kMaxRetries     = 3;

/// 文件句柄 RAII。
class FileHandle
{
public:
    explicit FileHandle(HANDLE handle = INVALID_HANDLE_VALUE) : m_handle(handle) {}
    ~FileHandle()
    {
        if (m_handle != INVALID_HANDLE_VALUE)
            ::CloseHandle(m_handle);
    }

    FileHandle(const FileHandle &) = delete;
    FileHandle &operator=(const FileHandle &) = delete;

    bool isValid() const { return m_handle != INVALID_HANDLE_VALUE; }
    HANDLE get() const { return m_handle; }

private:
    HANDLE m_handle;
};

/// 清除只读/隐藏/系统属性，否则覆盖写入会失败。
void clearReadonlyAttr(const QString &path)
{
    const DWORD attributes =
        ::GetFileAttributesW(reinterpret_cast<LPCWSTR>(path.utf16()));
    if (attributes == INVALID_FILE_ATTRIBUTES)
        return;
    if (attributes & (FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_HIDDEN
                      | FILE_ATTRIBUTE_SYSTEM)) {
        ::SetFileAttributesW(reinterpret_cast<LPCWSTR>(path.utf16()),
                             attributes & ~(FILE_ATTRIBUTE_READONLY
                                            | FILE_ATTRIBUTE_HIDDEN
                                            | FILE_ATTRIBUTE_SYSTEM));
    }
}

/// 把 dir 与 name 拼成一个路径（补一个分隔符）。
QString joinPath(const QString &dir, const QString &name)
{
    QString base = dir;
    if (!base.endsWith(QLatin1Char('/')) && !base.endsWith(QLatin1Char('\\')))
        base += QLatin1Char('/');
    return base + name;
}

} // namespace

FileOperationManager::FileOperationManager(QObject *parent)
    : QObject(parent)
{
}

void FileOperationManager::reportError(const QString &message)
{
    emit logMessage(message);
    qWarning() << message;
}

// ===========================================================================
// 复制入口
// ===========================================================================
bool FileOperationManager::copyToTarget(const QString &source,
                                        const QString &target,
                                        bool overwrite)
{
    const QFileInfo info(source);
    if (!info.exists()) {
        reportError(QStringLiteral("源路径不存在: %1").arg(source));
        return false;
    }

    if (info.isFile())
        return copyFileNative(source, target, overwrite);
    if (info.isDir())
        return copyDirectory(source, target, overwrite);

    reportError(QStringLiteral("未知源类型: %1").arg(source));
    return false;
}

// ===========================================================================
// 单文件复制（原生）
// ===========================================================================
bool FileOperationManager::copyFileNative(const QString &src,
                                          const QString &dst,
                                          bool overwrite)
{
    // 目标可能是目录：此时沿用源文件名
    QString targetPath = dst;
    const QFileInfo dstInfo(dst);
    if (dstInfo.isDir() || dst.endsWith(QLatin1Char('/'))
        || dst.endsWith(QLatin1Char('\\'))) {
        targetPath = joinPath(dst, QFileInfo(src).fileName());
    }

    const QString nativeSrc = QDir::toNativeSeparators(src);
    const QString nativeDst = QDir::toNativeSeparators(targetPath);

    if (!QDir().mkpath(QFileInfo(nativeDst).absolutePath())) {
        reportError(QStringLiteral("无法创建目标目录: %1")
                        .arg(QFileInfo(nativeDst).absolutePath()));
        return false;
    }

    if (!overwrite && QFile::exists(nativeDst)) {
        emit logMessage(QStringLiteral("目标已存在，跳过: %1").arg(nativeDst));
        return true;
    }

    for (int attempt = 0; attempt < kMaxRetries; ++attempt) {
        clearReadonlyAttr(nativeDst);

        const FileHandle source(::CreateFileW(
            reinterpret_cast<LPCWSTR>(nativeSrc.utf16()), GENERIC_READ,
            FILE_SHARE_READ, nullptr, OPEN_EXISTING,
            FILE_FLAG_SEQUENTIAL_SCAN, nullptr));

        if (!source.isValid()) {
            reportError(QStringLiteral("无法打开源文件: %1 (err=%2)")
                            .arg(nativeSrc)
                            .arg(::GetLastError()));
            return false;
        }

        const FileHandle target(::CreateFileW(
            reinterpret_cast<LPCWSTR>(nativeDst.utf16()), GENERIC_WRITE, 0,
            nullptr, CREATE_ALWAYS,
            FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr));

        if (!target.isValid()) {
            const DWORD error = ::GetLastError();

            // 受保护目录：回退到 TrustedInstaller 身份复制
            if (m_tiFallback
                && (error == ERROR_ACCESS_DENIED
                    || error == ERROR_SHARING_VIOLATION)
                && copyFileViaTrustedInstaller(nativeSrc, nativeDst)) {
                return true;
            }

            if (attempt + 1 == kMaxRetries) {
                reportError(QStringLiteral("无法创建目标文件: %1 (err=%2)")
                                .arg(nativeDst)
                                .arg(error));
                return false;
            }
            QThread::msleep(200);
            continue;
        }

        std::vector<char> buffer(kCopyBufferSize);
        bool copyOk = true;

        for (;;) {
            DWORD read = 0;
            if (!::ReadFile(source.get(), buffer.data(), kCopyBufferSize,
                            &read, nullptr)) {
                copyOk = false;
                break;
            }
            if (read == 0)
                break;      // EOF

            DWORD offset = 0;
            while (offset < read) {
                DWORD written = 0;
                if (!::WriteFile(target.get(), buffer.data() + offset,
                                 read - offset, &written, nullptr)) {
                    copyOk = false;
                    break;
                }
                offset += written;
            }
            if (!copyOk)
                break;
        }

        if (copyOk) {
            // 保留修改时间，便于增量部署判断新旧
            FILETIME writeTime{};
            if (::GetFileTime(source.get(), nullptr, nullptr, &writeTime))
                ::SetFileTime(target.get(), nullptr, nullptr, &writeTime);

            emit logMessage(QStringLiteral("已复制: %1 -> %2")
                                .arg(nativeSrc, nativeDst));
            return true;
        }

        const DWORD error = ::GetLastError();
        if (attempt + 1 == kMaxRetries) {
            reportError(QStringLiteral("复制失败: %1 -> %2 (err=%3)")
                            .arg(nativeSrc, nativeDst)
                            .arg(error));
            return false;
        }
        QThread::msleep(200);
    }

    return false;
}

bool FileOperationManager::copyFileViaTrustedInstaller(const QString &src,
                                                       const QString &dst)
{
    // TIHelper 只能「执行文件」，因此借 cmd.exe 做一次 copy。
    // 路径中的引号会导致命令被截断，这种情况直接放弃。
    if (src.contains(QLatin1Char('"')) || dst.contains(QLatin1Char('"')))
        return false;

    static std::atomic<quint32> s_sequence{0};
    const QString batPath = QDir::toNativeSeparators(
        QDir::tempPath() + QStringLiteral("/czl_copy_%1_%2.bat")
                               .arg(::GetCurrentProcessId())
                               .arg(s_sequence.fetch_add(1)));

    QFile bat(batPath);
    if (!bat.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        reportError(QStringLiteral("无法创建临时脚本: %1").arg(batPath));
        return false;
    }

    // cmd.exe 按系统 ANSI 代码页读取 bat 内容，绝不能用 UTF-8 写，
    // 否则中文路径会变成乱码。toLocal8Bit() 才能被 cmd 正确解析。
    bat.write(QByteArrayLiteral("@echo off\r\n"));
    bat.write(QStringLiteral("copy /Y \"%1\" \"%2\"\r\n")
                  .arg(src, dst)
                  .toLocal8Bit());
    bat.close();

    const int result = TIHelperWrapper::instance().executeFile(batPath);
    QFile::remove(batPath);

    const bool ok = (result == 0) && QFile::exists(dst);
    if (ok) {
        emit logMessage(QStringLiteral("已通过 TrustedInstaller 复制: %1 -> %2")
                            .arg(src, dst));
    } else {
        reportError(QStringLiteral("TrustedInstaller 复制失败(返回 %1): %2 -> %3")
                        .arg(result)
                        .arg(src, dst));
    }
    return ok;
}

// ===========================================================================
// 目录复制
// ===========================================================================
bool FileOperationManager::copyDirectory(const QString &srcDir,
                                         const QString &dstDir,
                                         bool overwrite)
{
    const QDir src(srcDir);
    if (!src.exists()) {
        reportError(QStringLiteral("源目录不存在: %1").arg(srcDir));
        return false;
    }

    if (!QDir().mkpath(dstDir)) {
        reportError(QStringLiteral("无法创建目标目录: %1").arg(dstDir));
        return false;
    }

    const QFileInfoList entries =
        src.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot
                          | QDir::Hidden | QDir::System);

    for (const QFileInfo &entry : entries) {
        const QString destination = joinPath(dstDir, entry.fileName());

        if (entry.isDir()) {
            if (!copyDirectory(entry.absoluteFilePath(), destination, overwrite))
                return false;
        } else {
            if (!entry.exists())     // 断开的符号链接等
                continue;
            if (!copyFileNative(entry.absoluteFilePath(), destination, overwrite))
                return false;
        }
    }
    return true;
}

// ===========================================================================
// ZIP 解压
// ===========================================================================
bool FileOperationManager::extractZip(const QString &zipPath,
                                      const QString &destDir,
                                      bool overwrite)
{
    const QFileInfo zipInfo(zipPath);
    if (!zipInfo.exists()) {
        reportError(QStringLiteral("ZIP 文件不存在: %1").arg(zipPath));
        return false;
    }
    if (!QDir().mkpath(destDir)) {
        reportError(QStringLiteral("无法创建解压目录: %1").arg(destDir));
        return false;
    }

    Q_UNUSED(overwrite)   // 两种实现都是覆盖语义

    emit logMessage(QStringLiteral("正在解压: %1 -> %2").arg(zipPath, destDir));

    if (extractZipWithTar(zipPath, destDir))
        return true;

    return extractZipWithShell(zipPath, destDir);
}

bool FileOperationManager::extractZipWithTar(const QString &zipPath,
                                             const QString &destDir)
{
    // Windows 10 1803+ 自带 bsdtar（tar.exe），完整支持 zip。
    const QString tar = QStandardPaths::findExecutable(QStringLiteral("tar"));
    if (tar.isEmpty())
        return false;

    QProcess process;
    process.setProgram(tar);
    process.setArguments({QStringLiteral("-xf"),
                          QDir::toNativeSeparators(zipPath),
                          QStringLiteral("-C"),
                          QDir::toNativeSeparators(destDir)});
    process.setWorkingDirectory(destDir);
    process.start();

    if (!process.waitForStarted(5000))
        return false;

    if (!process.waitForFinished(600000)) {
        process.kill();
        process.waitForFinished(5000);
        reportError(QStringLiteral("tar 解压超时: %1").arg(zipPath));
        return false;
    }

    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        emit logMessage(QStringLiteral("tar 解压失败(exit=%1)，回退到 Shell 解压")
                            .arg(process.exitCode()));
        return false;
    }

    emit logMessage(QStringLiteral("解压完成(tar): %1").arg(zipPath));
    return true;
}

bool FileOperationManager::extractZipWithShell(const QString &zipPath,
                                               const QString &destDir)
{
#ifdef Q_OS_WIN
    HRESULT hr = ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    bool needUninitialize = SUCCEEDED(hr);
    if (hr == RPC_E_CHANGED_MODE) {
        // 线程已经以 MTA 初始化，Shell API 仍然可用
        hr = S_OK;
        needUninitialize = false;
    } else if (FAILED(hr)) {
        reportError(QStringLiteral("CoInitializeEx 失败: %1").arg(hr));
        return false;
    }

    IShellDispatch *shell = nullptr;
    hr = ::CoCreateInstance(CLSID_Shell, nullptr, CLSCTX_INPROC_SERVER,
                            IID_IShellDispatch, reinterpret_cast<void **>(&shell));
    if (FAILED(hr) || shell == nullptr) {
        reportError(QStringLiteral("创建 Shell.Application 失败: %1").arg(hr));
        if (needUninitialize)
            ::CoUninitialize();
        return false;
    }

    bool ok = false;
    Folder      *zipFolder  = nullptr;
    Folder      *destFolder = nullptr;
    FolderItems *items      = nullptr;

    const QString nativeZip  = QDir::toNativeSeparators(zipPath);
    const QString nativeDest = QDir::toNativeSeparators(destDir);

    VARIANT vZip{};
    VARIANT vDest{};
    VariantInit(&vZip);
    VariantInit(&vDest);
    vZip.vt = VT_BSTR;
    vZip.bstrVal = ::SysAllocString(
        reinterpret_cast<const OLECHAR *>(nativeZip.utf16()));
    vDest.vt = VT_BSTR;
    vDest.bstrVal = ::SysAllocString(
        reinterpret_cast<const OLECHAR *>(nativeDest.utf16()));

    do {
        if (vZip.bstrVal == nullptr || vDest.bstrVal == nullptr) {
            reportError(QStringLiteral("SysAllocString 失败"));
            break;
        }
        if (FAILED(shell->NameSpace(vZip, &zipFolder)) || zipFolder == nullptr) {
            reportError(QStringLiteral("无法打开 ZIP: %1").arg(nativeZip));
            break;
        }
        if (FAILED(shell->NameSpace(vDest, &destFolder)) || destFolder == nullptr) {
            reportError(QStringLiteral("无法打开目标目录: %1").arg(nativeDest));
            break;
        }
        if (FAILED(zipFolder->Items(&items)) || items == nullptr) {
            reportError(QStringLiteral("无法读取 ZIP 内容: %1").arg(nativeZip));
            break;
        }

        VARIANT vItems{};
        VARIANT vOptions{};
        VariantInit(&vItems);
        VariantInit(&vOptions);
        vItems.vt = VT_DISPATCH;
        vItems.pdispVal = items;

        // FOF_SILENT | FOF_NOCONFIRMATION | FOF_NOCONFIRMMKDIR | FOF_NOERRORUI
        vOptions.vt = VT_I4;
        vOptions.lVal = 4 | 16 | 512 | 1024;

        hr = destFolder->CopyHere(vItems, vOptions);
        vItems.vt = VT_EMPTY;    // 防止 VariantClear 二次 Release

        if (FAILED(hr)) {
            reportError(QStringLiteral("CopyHere 失败: %1").arg(hr));
            break;
        }

        // CopyHere 是异步且没有完成回调的。
        // 旧版用「条目数连续 3 次不变」判断结束，解压大文件时会提前返回；
        // 这里改为观察解压出来的总字节数连续 5 次（约 1 秒）不变。
        int stableRounds = 0;
        qint64 lastSize = -1;
        constexpr int kMaxRounds = 300;     // 最多等待 60 秒

        for (int round = 0; round < kMaxRounds; ++round) {
            QThread::msleep(200);

            qint64 total = 0;
            QDirIterator it(destDir, QDir::Files | QDir::NoDotAndDotDot,
                            QDirIterator::Subdirectories);
            while (it.hasNext()) {
                it.next();
                total += it.fileInfo().size();
            }

            if (total > 0 && total == lastSize) {
                if (++stableRounds >= 5)
                    break;
            } else {
                stableRounds = 0;
            }
            lastSize = total;
        }

        ok = lastSize > 0;
        emit logMessage(ok ? QStringLiteral("解压完成(Shell): %1").arg(zipPath)
                           : QStringLiteral("解压后目标目录仍为空: %1").arg(destDir));
    } while (false);

    if (items != nullptr)
        items->Release();
    if (destFolder != nullptr)
        destFolder->Release();
    if (zipFolder != nullptr)
        zipFolder->Release();
    VariantClear(&vZip);
    VariantClear(&vDest);
    shell->Release();

    if (needUninitialize)
        ::CoUninitialize();

    return ok;
#else
    Q_UNUSED(zipPath)
    Q_UNUSED(destDir)
    reportError(QStringLiteral("仅支持 Windows 平台解压"));
    return false;
#endif
}

// ===========================================================================
// 递归删除（原生，不依赖 shell）
// ===========================================================================
bool FileOperationManager::removeRecursively(const QString &path)
{
    const QString native = QDir::toNativeSeparators(path);
    const DWORD attributes =
        ::GetFileAttributesW(reinterpret_cast<LPCWSTR>(native.utf16()));
    if (attributes == INVALID_FILE_ATTRIBUTES)
        return true;    // 已不存在即视为成功

    if (!(attributes & FILE_ATTRIBUTE_DIRECTORY)) {
        clearReadonlyAttr(native);
        return ::DeleteFileW(reinterpret_cast<LPCWSTR>(native.utf16())) != FALSE;
    }

    const QDir dir(path);
    const QFileInfoList entries =
        dir.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot
                          | QDir::Hidden | QDir::System);
    for (const QFileInfo &entry : entries) {
        if (!removeRecursively(entry.absoluteFilePath()))
            return false;
    }

    clearReadonlyAttr(native);
    return ::RemoveDirectoryW(reinterpret_cast<LPCWSTR>(native.utf16())) != FALSE;
}
