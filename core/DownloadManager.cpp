#include "DownloadManager.h"

#include <QDir>
#include <QEventLoop>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QTimer>
#include <QUrl>

namespace {

/// 探测结果。
struct ProbeOutcome {
    bool    reachable = false;
    QString message;
};

} // namespace

// ---------------------------------------------------------------------------
// 构造 / 服务器列表
// ---------------------------------------------------------------------------
DownloadManager::DownloadManager(QObject *parent)
    : QObject(parent)
    , m_nam(new QNetworkAccessManager(this))
{
}

void DownloadManager::setServers(const QList<ServerConfig> &servers)
{
    m_servers = servers;
    m_activeIndex = -1;
}

void DownloadManager::setActiveServerIndex(int index)
{
    m_activeIndex = (index >= 0 && index < m_servers.size()) ? index : -1;
}

QString DownloadManager::activeServerName() const
{
    if (m_activeIndex < 0 || m_activeIndex >= m_servers.size())
        return {};
    return m_servers.at(m_activeIndex).name;
}

ServerConfig DownloadManager::activeServer() const
{
    if (m_activeIndex < 0 || m_activeIndex >= m_servers.size())
        return {};
    return m_servers.at(m_activeIndex);
}

// ---------------------------------------------------------------------------
// 路径 / URL
// ---------------------------------------------------------------------------
QString DownloadManager::resolveLocalPath(const QString &localBaseDir,
                                          const QString &relativePath)
{
    QString base = localBaseDir.trimmed();
    if (base.isEmpty())
        base = QStringLiteral("D:/c_temp");

    const QDir dir(base);
    const QString relative = QDir::fromNativeSeparators(relativePath);
    return QDir::toNativeSeparators(dir.absoluteFilePath(relative));
}

QUrl DownloadManager::buildUrl(const QString &remotePath, const ServerConfig &server)
{
    // QUrl::setPath 默认按已解码文本处理，会自动做百分号编码，
    // 因此中文/空格文件名不会再像旧版那样拼出非法 URL。
    const QString relative = remotePath.startsWith(QLatin1Char('/'))
                                 ? remotePath.mid(1)
                                 : remotePath;

    QUrl url;
    if (server.type == QLatin1String("http")) {
        url = QUrl(server.baseUrl + relative);
        if (!server.username.isEmpty()) {
            url.setUserName(server.username);
            url.setPassword(server.password);
        }
    } else {
        url.setScheme(QStringLiteral("ftp"));
        url.setHost(server.host);
        url.setPort(server.port);
        if (!server.username.isEmpty()) {
            url.setUserName(server.username);
            url.setPassword(server.password);
        }
        url.setPath(server.basePath + relative);
    }
    return url;
}

void DownloadManager::applyServerToRequest(QNetworkRequest &request,
                                           const ServerConfig &server,
                                           int timeoutMs)
{
    if (server.type == QLatin1String("http") && !server.username.isEmpty()) {
        const QByteArray credentials =
            (server.username + QLatin1Char(':') + server.password).toUtf8();
        request.setRawHeader(QByteArrayLiteral("Authorization"),
                             QByteArrayLiteral("Basic ") + credentials.toBase64());
    }

    // Qt 6 自带的传输超时，取代旧版手写的 QTimer + QEventLoop 组合。
    request.setTransferTimeout(qMax(1000, timeoutMs));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("czl_installer/2.0 (Qt6)"));
}

bool DownloadManager::isServerAliveError(int networkError)
{
    // 显式白名单：只有「收到了协议层响应」才算服务器活着。
    switch (static_cast<QNetworkReply::NetworkError>(networkError)) {
    case QNetworkReply::NoError:
    case QNetworkReply::ContentNotFoundError:           // 404：服务在，只是路径不对
    case QNetworkReply::AuthenticationRequiredError:    // 401：服务在，只是要认证
    case QNetworkReply::ContentAccessDenied:            // 403
    case QNetworkReply::ProtocolUnknownError:
    case QNetworkReply::ProtocolInvalidOperationError:
    case QNetworkReply::UnknownContentError:
    case QNetworkReply::ContentConflictError:
    case QNetworkReply::InternalServerError:
    case QNetworkReply::ServiceUnavailableError:
        return true;
    default:
        return false;
    }
}

QString DownloadManager::describeNetworkError(int networkError)
{
    switch (static_cast<QNetworkReply::NetworkError>(networkError)) {
    case QNetworkReply::HostNotFoundError:
        return QStringLiteral("主机无法解析");
    case QNetworkReply::TimeoutError:
        return QStringLiteral("连接超时");
    case QNetworkReply::ConnectionRefusedError:
        return QStringLiteral("连接被拒绝");
    case QNetworkReply::RemoteHostClosedError:
        return QStringLiteral("对端关闭连接");
    case QNetworkReply::NetworkSessionFailedError:
        return QStringLiteral("网络会话失败");
    case QNetworkReply::TemporaryNetworkFailureError:
        return QStringLiteral("临时网络故障");
    case QNetworkReply::ProxyConnectionRefusedError:
    case QNetworkReply::ProxyNotFoundError:
    case QNetworkReply::ProxyTimeoutError:
        return QStringLiteral("代理不可用");
    case QNetworkReply::SslHandshakeFailedError:
        return QStringLiteral("TLS 握手失败");
    case QNetworkReply::AuthenticationRequiredError:
        return QStringLiteral("认证失败（401）");
    case QNetworkReply::ContentAccessDenied:
        return QStringLiteral("访问被拒绝（403）");
    case QNetworkReply::ContentNotFoundError:
        return QStringLiteral("资源不存在（404）");
    default:
        return QStringLiteral("网络错误 %1").arg(networkError);
    }
}

// ---------------------------------------------------------------------------
// 探测
// ---------------------------------------------------------------------------
bool DownloadManager::probeServers(bool force)
{
    if (!force && hasActiveServer())
        return true;

    if (m_servers.isEmpty()) {
        emit allServersUnavailable();
        return false;
    }

    const QString previous = activeServerName();
    m_activeIndex = -1;

    const int total = static_cast<int>(m_servers.size());
    for (int index = 0; index < total; ++index) {
        const ServerConfig &server = m_servers.at(index);
        emit serverProbeStarted(server.name, index + 1, total);

        if (probeServer(server)) {
            m_activeIndex = index;
            if (!previous.isEmpty() && previous != server.name)
                emit serverSwitched(previous, server.name);
            return true;
        }
    }

    qWarning() << "所有备用服务器均不可达";
    emit allServersUnavailable();
    return false;
}

bool DownloadManager::probeServer(const ServerConfig &server)
{
    const QUrl url = buildUrl(QString(), server);

    QNetworkRequest request(url);
    applyServerToRequest(request, server, server.connectTimeout);

    // HEAD 只取响应头：旧版用 GET，探测阶段会把整个文件下载一遍。
    QNetworkReply *reply = m_nam->head(request);

    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);

    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);

    timer.start(server.connectTimeout);
    loop.exec();

    ProbeOutcome outcome;

    if (timer.isActive()) {
        const auto error = reply->error();
        if (error == QNetworkReply::NoError) {
            outcome.reachable = true;
            outcome.message = QStringLiteral("连接成功");
        } else if (isServerAliveError(static_cast<int>(error))) {
            outcome.reachable = true;
            outcome.message = QStringLiteral("服务器在线（%1）")
                                  .arg(describeNetworkError(static_cast<int>(error)));
        } else {
            outcome.reachable = false;
            outcome.message = describeNetworkError(static_cast<int>(error));
        }
    } else {
        // 超时：先断开信号再 abort，避免 abort 触发的 finished 再次进入 loop
        QObject::disconnect(reply, nullptr, &loop, nullptr);
        reply->abort();
        outcome.reachable = false;
        outcome.message = QStringLiteral("连接超时（%1 ms）").arg(server.connectTimeout);
    }

    emit serverProbeResult(server.name, outcome.reachable, outcome.message);
    qInfo() << "探测服务器:" << server.name
            << (outcome.reachable ? "OK" : "FAIL") << outcome.message;

    reply->deleteLater();
    return outcome.reachable;
}
