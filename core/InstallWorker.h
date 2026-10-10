#ifndef INSTALLWORKER_H
#define INSTALLWORKER_H

#include <QList>
#include <QNetworkReply>
#include <QObject>
#include <QString>
#include <QStringList>

#include <atomic>

#include "ConfigManager.h"

class QNetworkAccessManager;

/**
 * @brief 在后台线程里执行「探测 + 下载 + 安装」的完整流程。
 *
 * 为什么要有这个类：旧版 InstallManager::installItem() 直接在 GUI 线程里
 * 同步完成下载（嵌套 QEventLoop）和外部进程等待，期间靠
 * QCoreApplication::processEvents() 来刷新进度。这带来两个严重问题：
 *   1. 界面在下载/安装期间完全卡死（processEvents 只处理一部分事件）；
 *   2. processEvents() 会重入任意槽函数 —— 用户可以在安装进行中再次点击
 *      安装按钮，或者在进度回调里触发列表重建，状态极易错乱。
 *
 * 现在唯一一次 processEvents() 调用被彻底移除：worker 活在
 * QThread 里，通过排队的信号把进度送回 GUI 线程。
 *
 * 这个类拥有自己的 QNetworkAccessManager（在其被 moveToThread 之后创建），
 * 因此不存在跨线程使用同一个 NAM 的问题。
 */
class InstallWorker : public QObject
{
    Q_OBJECT

public:
    InstallWorker(InstallItem item,
                  QList<ServerConfig> servers,
                  int activeServerIndex,
                  QString localBaseDir,
                  bool overwriteDefault,
                  QObject *parent = nullptr);

public slots:
    /// 安装流程入口（由 QThread::started 触发）。
    void process();

    /// 请求中止（线程安全：只置一个原子标志）。
    void requestAbort();

signals:
    void progress(const QString &stage, int percent);
    void finished(bool success, const QString &message);
    /// 服务器切换到备用地址时通知 GUI 更新标题栏。
    void serverChanged(int index, const QString &name);

private:
    // ---- 流程 ----
    bool ensureServer();
    bool downloadToLocal(QString &localPathOut);
    bool downloadOneFile(const QString &remotePath, const QString &localPath);
    bool allSourcesOk();

    void emitProgress(const QString &stage, int percent);
    bool aborted() const;

    // ---- 执行安装 ----
    void executeMsi(const QString &localPath);
    void executeExe(const QString &localPath);
    void executeBat(const QString &localPath);
    void executeRegisterCommands();
    bool runViaTrustedInstaller(const QString &filePath, const QString &args = QString());

    // ---- 辅助 ----
    /// 生成 bat 并交给 TIHelper 执行；返回子进程是否成功。
    bool runScript(const QString &scriptBody, const QString &tag);
    /**
     * @brief 校验刚下载的文件（仅在配置提供了 sha256 时生效）。
     *
     * 模式二（PostgreSQL + HTTP）下 czl_items.sha256 由 Web 管理端在
     * 上传软件包时自动写入；模式一的 JSON 配置没有该字段，直接跳过。
     * 校验失败视为下载失败，会自动切换到下一个备选服务器重下。
     */
    bool verifyDownloadedFile(const QString &localPath) const;
    /// 计算文件的 SHA-256（小写十六进制），读取失败返回空串。
    static QString computeSha256(const QString &localPath);
    /// 把单个参数按 Windows 命令行规则加引号。
    static QString quoteArgument(const QString &argument);
    static QString joinArguments(const QStringList &arguments);
    /// 相对路径 -> 本地绝对路径。
    QString resolveLocal(const QString &relativePath) const;
    /// 需要提权/受保护目标时用 TIHelper 清理旧目录。
    bool prepareTargetDirectory(const QString &target);

    InstallItem         m_item;
    QList<ServerConfig> m_servers;
    int                 m_activeIndex = -1;
    QString             m_localBaseDir;
    bool                m_overwriteDefault = true;

    QNetworkAccessManager *m_nam = nullptr;

    /// 中止标志。GUI 线程调用 requestAbort() 置位，worker 线程只读。
    std::atomic<bool> m_abortRequested{false};
};

#endif // INSTALLWORKER_H
