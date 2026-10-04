#include "FileOperationManager.h"
#include <QFile>
#include <QDir>
#include <QFileInfo>
#include <QThread>
#include <QElapsedTimer>
#include <QDebug>

#ifdef Q_OS_WIN
#include <windows.h>
#include <shlobj.h>
#include <shldisp.h>
#include <exdisp.h>
#include <objbase.h>
#include <oleauto.h>
#endif

FileOperationManager::FileOperationManager(QObject *parent) : QObject(parent) {}

// ============================================================
// 复制
// ============================================================
bool FileOperationManager::copyToTarget(const QString &source,
                                         const QString &target,
                                         bool overwrite)
{
    QFileInfo fi(source);
    if (!fi.exists()) {
        emit logMessage("源路径不存在: " + source);
        return false;
    }
    if (fi.isFile()) return copyFile(source, target, overwrite);
    if (fi.isDir())  return copyDirectory(source, target, overwrite);

    emit logMessage("未知源类型: " + source);
    return false;
}

bool FileOperationManager::copyFile(const QString &src,
                                     const QString &dst,
                                     bool overwrite)
{
    QFileInfo di(dst);
    QDir().mkpath(di.absolutePath());

    if (QFile::exists(dst)) {
        if (!overwrite) return true;
        QFile::setPermissions(dst,
            QFileDevice::WriteOwner | QFileDevice::WriteUser |
            QFileDevice::ReadOwner | QFileDevice::ReadUser);
        if (!QFile::remove(dst)) {
            emit logMessage("无法删除目标文件: " + dst);
            return false;
        }
    }

    if (QFile::copy(src, dst)) {
        emit logMessage(QString("已复制: %1 -> %2").arg(src, dst));
        return true;
    }
    emit logMessage(QString("复制失败: %1 -> %2").arg(src, dst));
    return false;
}

bool FileOperationManager::copyDirectory(const QString &srcDir,
                                          const QString &dstDir,
                                          bool overwrite)
{
    QDir src(srcDir);
    if (!src.exists()) {
        emit logMessage("源目录不存在: " + srcDir);
        return false;
    }
    QDir().mkpath(dstDir);

    QFileInfoList entries = src.entryInfoList(
        QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot);

    for (const QFileInfo &e : entries) {
        QString dstPath = dstDir;
        if (!dstPath.endsWith('/') && !dstPath.endsWith('\\')) dstPath += "/";
        dstPath += e.fileName();

        if (e.isDir()) {
            if (!copyDirectory(e.absoluteFilePath(), dstPath, overwrite))
                return false;
        } else {
            if (!copyFile(e.absoluteFilePath(), dstPath, overwrite))
                return false;
        }
    }
    return true;
}

// ============================================================
// ZIP 解压（Windows Shell COM）
// ============================================================
bool FileOperationManager::extractZip(const QString &zipPath,
                                       const QString &destDir,
                                       bool overwrite)
{
    Q_UNUSED(overwrite);   // Shell COM 默认覆盖

#ifdef Q_OS_WIN
    QFileInfo zi(zipPath);
    if (!zi.exists()) {
        emit logMessage("ZIP 文件不存在: " + zipPath);
        return false;
    }
    QDir().mkpath(destDir);
    emit logMessage(QString("正在解压: %1 -> %2").arg(zipPath, destDir));

    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    bool needUninit = SUCCEEDED(hr);

    IShellDispatch *pShell = nullptr;
    hr = CoCreateInstance(CLSID_Shell, nullptr, CLSCTX_INPROC_SERVER,
                          IID_IShellDispatch, (void**)&pShell);
    if (FAILED(hr) || !pShell) {
        emit logMessage("创建 Shell.Application 失败");
        if (needUninit) CoUninitialize();
        return false;
    }

    bool ok = false;
    Folder *pZip = nullptr;
    Folder *pDest = nullptr;
    FolderItems *pItems = nullptr;

    VARIANT vZip, vDest;
    VariantInit(&vZip);
    VariantInit(&vDest);

    QString nativeZip  = QDir::toNativeSeparators(zi.absoluteFilePath());
    QString nativeDest = QDir::toNativeSeparators(QDir(destDir).absolutePath());

    vZip.vt = VT_BSTR;
    vZip.bstrVal = SysAllocString(
        reinterpret_cast<const OLECHAR*>(nativeZip.utf16()));
    vDest.vt = VT_BSTR;
    vDest.bstrVal = SysAllocString(
        reinterpret_cast<const OLECHAR*>(nativeDest.utf16()));

    do {
        hr = pShell->NameSpace(vZip, &pZip);
        if (FAILED(hr) || !pZip) { emit logMessage("打开 ZIP 失败"); break; }

        hr = pShell->NameSpace(vDest, &pDest);
        if (FAILED(hr) || !pDest) { emit logMessage("打开目标目录失败"); break; }

        hr = pZip->Items(&pItems);
        if (FAILED(hr) || !pItems) { emit logMessage("读取 ZIP 内容失败"); break; }

        VARIANT vItems, vOpts;
        VariantInit(&vItems);
        VariantInit(&vOpts);

        vItems.vt = VT_DISPATCH;
        vItems.pdispVal = pItems;

        // FOF_SILENT(4) | FOF_NOCONFIRMATION(16)
        // | FOF_NOCONFIRMMKDIR(512) | FOF_NOERRORUI(1024)
        vOpts.vt = VT_I4;
        vOpts.lVal = 4 | 16 | 512 | 1024;

        hr = pDest->CopyHere(vItems, vOpts);
        vItems.vt = VT_EMPTY;  // 防止二次 Release

        if (FAILED(hr)) { emit logMessage("CopyHere 失败"); break; }

        ok = waitForExtraction(destDir, 60000);
        emit logMessage(ok ? "解压完成" : "解压超时或目录为空");
    } while (false);

    if (pItems) pItems->Release();
    if (pDest)  pDest->Release();
    if (pZip)   pZip->Release();
    VariantClear(&vZip);
    VariantClear(&vDest);
    pShell->Release();
    if (needUninit) CoUninitialize();

    return ok;
#else
    Q_UNUSED(zipPath); Q_UNUSED(destDir);
    emit logMessage("仅支持 Windows 平台解压");
    return false;
#endif
}

int FileOperationManager::countEntriesRecursive(const QString &dir)
{
    QDir d(dir);
    if (!d.exists()) return -1;
    int n = 0;
    for (const QFileInfo &fi : d.entryInfoList(
             QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot)) {
        n++;
        if (fi.isDir()) n += countEntriesRecursive(fi.absoluteFilePath());
    }
    return n;
}

bool FileOperationManager::waitForExtraction(const QString &destDir, int timeoutMs)
{
    QElapsedTimer t; t.start();
    int last = -1, stable = 0;
    while (t.elapsed() < timeoutMs) {
        int c = countEntriesRecursive(destDir);
        if (c > 0 && c == last) {
            if (++stable >= 3) return true;
        } else stable = 0;
        last = c;
        QThread::msleep(300);
    }
    return last > 0;
}
