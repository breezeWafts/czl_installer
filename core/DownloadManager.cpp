#include "DownloadManager.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QEventLoop>
#include <QTimer>
#include <QDebug>

DownloadManager::DownloadManager(QObject *parent) : QObject(parent) {}

void DownloadManager::setServers(const QList<ServerConfig> &servers)
{
    m_servers = servers;
    m_activeIndex = -1;
}

QString DownloadManager::activeServerName() const
{
    if (m_activeIndex >= 0 && m_activeIndex < m_servers.size())
        return m_servers[m_activeIndex].name;
    return QString();
}

QString DownloadManager::resolveLocalPath(const QString &relativePath) const
{
    QString full = m_localBaseDir;
    if (!full.endsWith('/') && !full.endsWith('\\'))
        full += "/";
    full += relativePath;
    return QDir::toNativeSeparators(full);
}

bool DownloadManager::ensureDirExists(const QString &filePath)
{
    QFileInfo fi(filePath);
    QDir dir(fi.absolutePath());
    if (!dir.exists()) return dir.mkpath(".");
    return true;
}

QUrl DownloadManager::buildUrl(const QString &remotePath, const ServerConfig &s) const
{
    QUrl url;
    if (s.type == "ftp") {
        url.setScheme("ftp");
        url.setHost(s.host);
        url.setPort(s.port);
        url.setUserName(s.username);
        url.setPassword(s.password);
        url.setPath(s.basePath + remotePath);
    } else {
        url = QUrl(s.baseUrl + remotePath);
    }
    return url;
}

// ============================================================
// 探测
// ============================================================
bool DownloadManager::probeServers(bool force)
{
    if (!force && hasActiveServer()) return true;
    if (m_servers.isEmpty()) { emit allServersUnavailable(); return false; }

    QString prev = activeServerName();
    m_activeIndex = -1;

    const int total = m_servers.size();
    for (int i = 0; i < total; ++i) {
        const ServerConfig &s = m_servers[i];
        emit serverProbeStarted(s.name, i + 1, total);

        if (probeServer(s)) {
            m_activeIndex = i;
            qDebug() << "选定服务器:" << s.name;
            if (!prev.isEmpty() && prev != s.name)
                emit serverSwitched(prev, s.name);
            return true;
        }
    }

    qWarning() << "所有服务器均不可用";
    emit allServersUnavailable();
    return false;
}

bool DownloadManager::probeServer(const ServerConfig &s)
{
    QUrl url;
    if (s.type == "ftp") {
        url.setScheme("ftp");
        url.setHost(s.host);
        url.setPort(s.port);
        url.setUserName(s.username);
        url.setPassword(s.password);
        url.setPath(s.basePath);
    } else {
        url = QUrl(s.baseUrl);
    }

    QNetworkAccessManager mgr;
    QNetworkRequest req(url);
    if (s.type == "http" && !s.username.isEmpty()) {
        QString auth = s.username + ":" + s.password;
        req.setRawHeader("Authorization", "Basic " + auth.toUtf8().toBase64());
    }

    QNetworkReply *reply = mgr.get(req);
    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    timer.start(s.connectTimeout);
    loop.exec();

    bool reachable = false;
    QString message;

    if (timer.isActive()) {
        if (reply->error() == QNetworkReply::NoError) {
            reachable = true;
            message = "连接成功";
        } else {
            switch (reply->error()) {
            case QNetworkReply::HostNotFoundError:
            case QNetworkReply::TimeoutError:
            case QNetworkReply::ConnectionRefusedError:
            case QNetworkReply::RemoteHostClosedError:
            case QNetworkReply::NetworkSessionFailedError:
            case QNetworkReply::TemporaryNetworkFailureError:
            case QNetworkReply::ProxyConnectionRefusedError:
            case QNetworkReply::ProxyNotFoundError:
            case QNetworkReply::ProxyTimeoutError:
                reachable = false;
                message = "不可达: " + reply->errorString();
                break;
            default:
                reachable = true;   // 4xx/5xx/550 等说明服务器响应了
                message = "服务器响应: " + reply->errorString();
                break;
            }
        }
    } else {
        reply->abort();
        reachable = false;
        message = QString("连接超时（%1ms）").arg(s.connectTimeout);
    }

    emit serverProbeResult(s.name, reachable, message);
    qDebug() << "探测:" << s.name << "->" << (reachable ? "OK" : "FAIL") << message;

    reply->deleteLater();
    return reachable;
}

// ============================================================
// 单文件下载
// ============================================================
bool DownloadManager::downloadOnce(const QString &remotePath,
                                    const QString &localPath,
                                    const ServerConfig &s,
                                    int timeoutMs)
{
    ensureDirExists(localPath);
    if (QFile::exists(localPath)) {
        if (!QFile::remove(localPath)) {
            emit downloadError("无法删除旧文件: " + localPath);
            return false;
        }
    }

    QFile file(localPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        emit downloadError("无法创建本地文件: " + localPath);
        return false;
    }

    QNetworkAccessManager mgr;
    QUrl url = buildUrl(remotePath, s);
    QNetworkRequest req(url);
    if (s.type == "http" && !s.username.isEmpty()) {
        QString auth = s.username + ":" + s.password;
        req.setRawHeader("Authorization", "Basic " + auth.toUtf8().toBase64());
    }

    QNetworkReply *reply = mgr.get(req);
    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QObject::connect(reply, &QNetworkReply::readyRead, [&file, reply]() {
        file.write(reply->readAll());
    });
    QObject::connect(reply, &QNetworkReply::downloadProgress,
                     this, &DownloadManager::downloadProgress);

    timer.start(timeoutMs);
    loop.exec();

    bool ok = false;
    if (timer.isActive() && reply->error() == QNetworkReply::NoError) {
        file.write(reply->readAll());
        file.close();
        ok = true;
        qDebug() << "下载成功[" << s.name << "]:" << remotePath;
    } else {
        if (!timer.isActive()) reply->abort();
        file.close();
        file.remove();
        qWarning() << "下载失败[" << s.name << "]:" << reply->errorString();
    }
    reply->deleteLater();
    return ok;
}

bool DownloadManager::downloadFileSync(const QString &remotePath,
                                        const QString &localPath,
                                        int timeoutMs)
{
    if (m_servers.isEmpty()) {
        emit downloadError("没有配置任何服务器");
        return false;
    }
    if (!hasActiveServer() && !probeServers()) {
        emit downloadError("所有服务器均不可达，无法下载: " + remotePath);
        return false;
    }

    int startIdx = m_activeIndex;
    for (int k = 0; k < m_servers.size(); ++k) {
        int idx = (startIdx + k) % m_servers.size();
        const ServerConfig &s = m_servers[idx];

        if (downloadOnce(remotePath, localPath, s, timeoutMs)) {
            if (idx != m_activeIndex) {
                QString prev = activeServerName();
                m_activeIndex = idx;
                emit serverSwitched(prev, s.name);
            }
            emit downloadFinished(localPath);
            return true;
        }
    }

    emit downloadError("所有服务器均下载失败: " + remotePath);
    return false;
}

bool DownloadManager::downloadDirectorySync(const QString &remoteDir,
                                             const QString &localDir,
                                             const QStringList &files,
                                             int timeoutMs)
{
    if (files.isEmpty()) {
        qWarning() << "目录下载需要 files 列表:" << remoteDir;
        return false;
    }

    QString rd = remoteDir;
    if (!rd.endsWith('/')) rd += '/';
    QDir().mkpath(localDir);

    for (const QString &f : files) {
        QString remoteFile = rd + f;
        QString localFile = localDir;
        if (!localFile.endsWith('/') && !localFile.endsWith('\\'))
            localFile += "/";
        localFile += f;

        if (!downloadFileSync(remoteFile, localFile, timeoutMs))
            return false;
    }
    return true;
}
