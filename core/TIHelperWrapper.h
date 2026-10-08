#ifndef TIHELPERWRAPPER_H
#define TIHELPERWRAPPER_H

#include <QString>

#include <windows.h>

// 复用公开接口的签名与调用约定声明，避免在封装层重复书写而出现偏差。
// tihelper.h 用的是 Q_DECL_IMPORT，这里只取声明、不产生链接依赖。
#include "tihelper.h"

/**
 * @brief TIHelper.dll 的薄封装。
 *
 * 修正点：
 *   - 旧版没有映射 ti_delete_path，删除受保护文件的能力等于没接上；
 *   - load()/unload() 受 QMutex 保护，可从任意线程调用
 *     （InstallWorker 在后台线程里执行安装）；
 *   - 找不到新版 ti_execute_file_args 时自动回退到旧版 DLL 的
 *     ti_execute_file，保证新程序能搭配旧 DLL 运行。
 */
class TIHelperWrapper
{
public:
    static TIHelperWrapper &instance();

    /// 加载 DLL 并解析导出函数；可重复调用，失败后不会反复重试。
    bool load();
    /// 卸载 DLL（进程退出前调用一次即可）。
    void unload();

    bool isLoaded() const { return m_module != nullptr; }

    /// 最近一次失败原因（人类可读）。
    QString lastError() const { return m_lastError; }

    /**
     * @brief 以 TrustedInstaller 权限执行文件。
     * @param filePath 本地文件路径（自动转 UTF-8 + 原生分隔符）
     * @param args     附加命令行参数（不带前导空格），可为空
     * @return 0 成功；>0 子进程退出码；<0 见 tihelper.h 的约定
     */
    int executeFile(const QString &filePath, const QString &args = QString());

    /// 以 TrustedInstaller 权限删除文件或目录（递归）。
    int deletePath(const QString &path);

private:
    TIHelperWrapper() = default;
    ~TIHelperWrapper();

    TIHelperWrapper(const TIHelperWrapper &) = delete;
    TIHelperWrapper &operator=(const TIHelperWrapper &) = delete;

    void reset();

    /// 旧版 DLL 只有 ti_execute_file：用它适配成带参数版本（忽略参数）。
    using legacy_execute_fn = int (TIHELPER_CALL *)(const char *);
    static int TIHELPER_CALL legacyExecuteAdapter(const char *path,
                                                  const char *args);

    HMODULE m_module        = nullptr;
    bool    m_loadAttempted = false;

    using execute_args_fn = int (TIHELPER_CALL *)(const char *, const char *);
    using delete_path_fn  = int (TIHELPER_CALL *)(const char *);
    using abi_version_fn  = int (TIHELPER_CALL *)(void);

    execute_args_fn m_executeFileArgs = nullptr;
    delete_path_fn  m_deletePath      = nullptr;
    abi_version_fn  m_abiVersion      = nullptr;

    QString m_lastError;
};

#endif // TIHELPERWRAPPER_H
