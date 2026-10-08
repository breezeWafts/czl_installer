#ifndef DOWNLOADMANAGER_H
#define DOWNLOADMANAGER_H

#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

#include "ConfigManager.h"

class QNetworkAccessManager;
class QNetworkReply;
class QNetworkRequest;

/**
 * @brief 服务器探测 / 主备切换（GUI 线程）。
 *
 * 职责划分（这是对旧版最重要的结构调整）：
 *   - DownloadManager 只负责「探测 + 记住当前可用服务器」，
 *     全部调用都发生在 GUI 线程；
 *   - 真正的文件传输由 InstallWorker 在后台线程里用它自己的
 *     QNetworkAccessManager 完成，因此不再需要旧版那种
 *     「主线程里嵌套 QEventLoop + QTimer」的阻塞式下载。
 *
 * 这样既消除了嵌套事件循环带来的重入风险（旧版会在 processEvents()
 * 里再次进入安装槽函数），也不会再卡死界面。
 */
class DownloadManager : public QObject
{
    Q_OBJECT

public:
    explicit DownloadManager(QObject *parent = nullptr);

    void setServers(const QList<ServerConfig> &servers);
    const QList<ServerConfig> &servers() const { return m_servers; }

    /// 探测并选中第一个可用服务器；force=false 且已有可用服务器时直接返回。
    bool probeServers(bool force = false);
    bool probeServer(const ServerConfig &server);

    bool hasActiveServer() const { return m_activeIndex >= 0; }
    int activeServerIndex() const { return m_activeIndex; }
    void setActiveServerIndex(int index);

    QString activeServerName() const;
    ServerConfig activeServer() const;

    /// 相对路径 -> 本地绝对路径（基于传入的 localBaseDir）。
    static QString resolveLocalPath(const QString &localBaseDir,
                                    const QString &relativePath);

    /// 由 ServerConfig + 相对路径构造完整 URL（正确做百分号编码）。
    static QUrl buildUrl(const QString &remotePath, const ServerConfig &server);

    /// 给请求统一加上认证头、超时与重定向策略。
    static void applyServerToRequest(QNetworkRequest &request,
                                     const ServerConfig &server,
                                     int timeoutMs);

    /**
     * @brief 判定某个网络错误是否代表「服务器是活着的」。
     *
     * 旧版把这段逻辑写反了：4xx/5xx 与认证失败被当成可达，
     * 而 UnknownNetworkError 之类真正的不可达反而被放过。
     * 这里用显式白名单，探测与 worker 共用同一份判定。
     */
    static bool isServerAliveError(int networkError);

    /// 把网络错误翻译成中文提示。
    static QString describeNetworkError(int networkError);

signals:
    void serverProbeStarted(const QString &name, int index, int total);
    void serverProbeResult(const QString &name, bool ok, const QString &message);
    void serverSwitched(const QString &fromName, const QString &toName);
    void allServersUnavailable();

private:
    QList<ServerConfig> m_servers;
    int                 m_activeIndex = -1;

    QNetworkAccessManager *m_nam = nullptr;   ///< 仅用于探测
};

#endif // DOWNLOADMANAGER_H
