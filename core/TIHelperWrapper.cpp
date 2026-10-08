#include "TIHelperWrapper.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QMutex>
#include <QMutexLocker>

namespace {

QMutex g_mutex;

/// 旧版 DLL 的 ti_execute_file 地址；仅在被解析出来后才使用。
int (TIHELPER_CALL *g_legacyExecute)(const char *) = nullptr;

/// 统一把 QString 路径转成 DLL 需要的 UTF-8 + 原生分隔符形式。
QByteArray toUtf8Native(const QString &path)
{
    return QDir::toNativeSeparators(path).toUtf8();
}

/**
 * @brief 从模块里取函数地址并转成目标函数指针类型。
 *
 * 直接 reinterpret_cast<Fn>(FARPROC) 会触发
 * -Wcast-function-type（FARPROC 与目标签名不兼容），
 * 因此先经由 void* 中转。
 */
template <typename Fn>
Fn resolveSymbol(HMODULE module, const char *name)
{
    return reinterpret_cast<Fn>(
        reinterpret_cast<void *>(::GetProcAddress(module, name)));
}

} // namespace

TIHelperWrapper &TIHelperWrapper::instance()
{
    static TIHelperWrapper wrapper;
    return wrapper;
}

TIHelperWrapper::~TIHelperWrapper()
{
    unload();
}

void TIHelperWrapper::reset()
{
    if (m_module != nullptr) {
        ::FreeLibrary(m_module);
        m_module = nullptr;
    }
    m_executeFileArgs = nullptr;
    m_deletePath      = nullptr;
    m_abiVersion      = nullptr;
    g_legacyExecute   = nullptr;
}

int TIHELPER_CALL TIHelperWrapper::legacyExecuteAdapter(const char *path,
                                                        const char *args)
{
    Q_UNUSED(args)
    if (g_legacyExecute == nullptr)
        return -1;
    return g_legacyExecute(path);
}

bool TIHelperWrapper::load()
{
    QMutexLocker locker(&g_mutex);

    if (m_module != nullptr)
        return true;

    // 失败后不再反复尝试 LoadLibrary，避免每次安装都重复一次失败路径。
    if (m_loadAttempted)
        return false;
    m_loadAttempted = true;

    m_lastError.clear();

    const QString exeDir = QCoreApplication::applicationDirPath();
    const QString dllPath =
        QDir::toNativeSeparators(exeDir + QStringLiteral("/TIHelper.dll"));

    // 优先从 exe 同目录加载，避免被当前工作目录或 PATH 中的同名 DLL 劫持。
    m_module = ::LoadLibraryExW(reinterpret_cast<LPCWSTR>(dllPath.utf16()),
                                nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (m_module == nullptr) {
        const DWORD directError = ::GetLastError();
        m_module = ::LoadLibraryW(L"TIHelper.dll");
        if (m_module == nullptr) {
            m_lastError = QStringLiteral("加载 TIHelper.dll 失败: %1 (err=%2)，"
                                         "系统搜索路径回退也失败 (err=%3)")
                              .arg(dllPath)
                              .arg(directError)
                              .arg(::GetLastError());
            qWarning() << m_lastError;
            return false;
        }
    }

    m_abiVersion = resolveSymbol<abi_version_fn>(m_module,
                                                  "ti_helper_abi_version");

    m_executeFileArgs = resolveSymbol<execute_args_fn>(m_module,
                                                       "ti_execute_file_args");

    if (m_executeFileArgs == nullptr) {
        // 回退到旧版接口
        g_legacyExecute = resolveSymbol<legacy_execute_fn>(m_module,
                                                           "ti_execute_file");
        if (g_legacyExecute != nullptr) {
            m_executeFileArgs = &TIHelperWrapper::legacyExecuteAdapter;
            qWarning() << "TIHelper.dll 为旧版（缺少 ti_execute_file_args），"
                          "安装参数将被忽略，建议替换为新版 DLL";
        }
    }

    m_deletePath = resolveSymbol<delete_path_fn>(m_module, "ti_delete_path");

    if (m_executeFileArgs == nullptr) {
        m_lastError = QStringLiteral("GetProcAddress(ti_execute_file) 失败, err=%1")
                          .arg(::GetLastError());
        qWarning() << m_lastError;
        reset();
        return false;
    }

    const int abi = (m_abiVersion != nullptr) ? m_abiVersion() : -1;
    qInfo() << "TIHelper.dll 加载成功:" << dllPath
            << "ABI=" << abi
            << "hasDeletePath=" << (m_deletePath != nullptr);

    return true;
}

void TIHelperWrapper::unload()
{
    QMutexLocker locker(&g_mutex);
    reset();
    m_loadAttempted = false;
    m_lastError.clear();
}

int TIHelperWrapper::executeFile(const QString &filePath, const QString &args)
{
    if (!load()) {
        qWarning() << "TIHelper.dll 不可用:" << m_lastError;
        return -2;   // 与 tihelper.h 的「路径/环境不可用」语义对齐
    }

    const QFileInfo info(filePath);
    if (!info.exists() || !info.isFile()) {
        qWarning() << "待执行文件不存在:" << filePath;
        return -2;
    }

    const QByteArray nativePath = toUtf8Native(info.absoluteFilePath());
    const QByteArray nativeArgs = args.toUtf8();

    qInfo() << "ti_execute_file:" << info.absoluteFilePath()
            << (args.isEmpty() ? QString() : QStringLiteral("| args=") + args);

    const int result = m_executeFileArgs(
        nativePath.constData(), args.isEmpty() ? nullptr : nativeArgs.constData());

    qInfo() << "ti_execute_file 返回:" << result;
    return result;
}

int TIHelperWrapper::deletePath(const QString &path)
{
    if (!load()) {
        qWarning() << "TIHelper.dll 不可用:" << m_lastError;
        return -2;
    }

    if (m_deletePath == nullptr) {
        qWarning() << "TIHelper.dll 未导出 ti_delete_path，无法以 TI 权限删除";
        return -2;
    }

    const QByteArray nativePath = toUtf8Native(path);
    qInfo() << "ti_delete_path:" << path;

    const int result = m_deletePath(nativePath.constData());
    qInfo() << "ti_delete_path 返回:" << result;
    return result;
}
