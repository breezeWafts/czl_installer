#ifndef DOWNLOADMANAGER_H
#define DOWNLOADMANAGER_H

#include <QObject>
#include <QString>
#include <QStringList>
#include <QList>
#include "ConfigManager.h"

class DownloadManager : public QObject
{
    Q_OBJECT
public:
    explicit DownloadManager(QObject *parent = nullptr);

    void setServers(const QList<ServerConfig> &servers);
    void setLocalBaseDir(const QString &dir) { m_localBaseDir = dir; }
    QString localBaseDir() const { return m_localBaseDir; }

    // 相对路径 → 本地绝对路径
    QString resolveLocalPath(const QString &relativePath) const;

    // 探测服务器，第一个可用的被选中
    bool probeServers(bool force = false);
    bool hasActiveServer() const { return m_activeIndex >= 0; }
    QString activeServerName() const;

    // 同步下载（自动切换服务器）
    bool downloadFileSync(const QString &remotePath,
                          const QString &localPath,
                          int timeoutMs = 300000);

    // 目录下载
    bool downloadDirectorySync(const QString &remoteDir,
                               const QString &localDir,
                               const QStringList &files,
                               int timeoutMs = 600000);

signals:
    void downloadProgress(qint64 received, qint64 total);
    void downloadFinished(const QString &localPath);
    void downloadError(const QString &errorString);

    void serverProbeStarted(const QString &name, int index, int total);
    void serverProbeResult(const QString &name, bool ok, const QString &message);
    void serverSwitched(const QString &fromName, const QString &toName);
    void allServersUnavailable();

private:
    bool probeServer(const ServerConfig &s);
    QUrl buildUrl(const QString &remotePath, const ServerConfig &s) const;
    bool ensureDirExists(const QString &filePath);

    bool downloadOnce(const QString &remotePath,
                      const QString &localPath,
                      const ServerConfig &s,
                      int timeoutMs);

    QList<ServerConfig> m_servers;
    int     m_activeIndex = -1;
    QString m_localBaseDir = "D:/c_temp";
};

#endif
