#include "PgConfig.h"

#include <QRegularExpression>
#include <QStringList>
#include <QUrl>
#include <QUrlQuery>

namespace {

/// 把 libpq 连接串的值做成单引号字面量（内部的 \ 与 ' 需要转义）。
QString quoteLibpqValue(const QString &value)
{
    QString escaped = value;
    escaped.replace(QLatin1Char('\\'), QLatin1String("\\\\"));
    escaped.replace(QLatin1Char('\''), QLatin1String("\\'"));
    return QLatin1Char('\'') + escaped + QLatin1Char('\'');
}

/// 解析 key=value 形式的连接串，支持单双引号包裹的值。
/// 例：host=1.2.3.4 port=5432 password='a b' dbname=czl
bool parseKeyValuePairs(const QString &text, PgConnectionInfo &info)
{
    if (!text.contains(QLatin1Char('=')))
        return false;

    static const QRegularExpression pairPattern(
        QStringLiteral("\\s*([A-Za-z_][A-Za-z0-9_]*)\\s*=\\s*"
                       "('(?:[^'\\\\]|\\\\.)*'|\"(?:[^\"\\\\]|\\\\.)*\"|[^\\s]*)"),
        QRegularExpression::MultilineOption);

    int matched = 0;
    auto it = pairPattern.globalMatch(text);
    while (it.hasNext()) {
        const QRegularExpressionMatch match = it.next();
        QString key = match.captured(1).toLower();
        QString value = match.captured(2);

        // 去掉外层引号并还原转义
        if (value.size() >= 2
            && ((value.startsWith(QLatin1Char('\'')) && value.endsWith(QLatin1Char('\'')))
                || (value.startsWith(QLatin1Char('"')) && value.endsWith(QLatin1Char('"'))))) {
            value = value.mid(1, value.size() - 2);
            value.replace(QLatin1String("\\'"), QStringLiteral("'"));
            value.replace(QLatin1String("\\\""), QStringLiteral("\""));
            value.replace(QLatin1String("\\\\"), QStringLiteral("\\"));
        }

        if (key == QLatin1String("host") || key == QLatin1String("hostaddr"))
            info.host = value;
        else if (key == QLatin1String("port"))
            info.port = value.toInt();
        else if (key == QLatin1String("dbname") || key == QLatin1String("database"))
            info.database = value;
        else if (key == QLatin1String("user"))
            info.user = value;
        else if (key == QLatin1String("password"))
            info.password = value;
        else if (key == QLatin1String("connect_timeout"))
            info.connectTimeout = value.toInt();
        else
            continue;               // 其它关键字忽略，但仍算作已匹配

        ++matched;
    }

    return matched > 0;
}

/// 解析 "host:port,user,password[,dbname]" 简写。
bool parseShorthand(const QString &text, PgConnectionInfo &info)
{
    const QStringList parts = text.split(QLatin1Char(','));
    if (parts.isEmpty())
        return false;

    const QString head = parts.at(0).trimmed();
    if (head.isEmpty())
        return false;

    // host[:port]
    const int colon = head.lastIndexOf(QLatin1Char(':'));
    if (colon > 0) {
        bool ok = false;
        const int port = head.mid(colon + 1).trimmed().toInt(&ok);
        if (ok && port > 0 && port <= 65535) {
            info.host = head.left(colon).trimmed();
            info.port = port;
        } else {
            info.host = head;
        }
    } else {
        info.host = head;
    }

    if (parts.size() > 1 && !parts.at(1).trimmed().isEmpty())
        info.user = parts.at(1).trimmed();
    if (parts.size() > 2 && !parts.at(2).trimmed().isEmpty())
        info.password = parts.at(2).trimmed();
    if (parts.size() > 3 && !parts.at(3).trimmed().isEmpty())
        info.database = parts.at(3).trimmed();

    return true;
}

} // namespace

// ---------------------------------------------------------------------------
// PgConnectionInfo
// ---------------------------------------------------------------------------
QString PgConnectionInfo::toConnectionString() const
{
    QStringList parts;
    parts << QStringLiteral("host=%1").arg(quoteLibpqValue(host));
    parts << QStringLiteral("port=%1").arg(port);
    parts << QStringLiteral("dbname=%1").arg(quoteLibpqValue(database));
    parts << QStringLiteral("user=%1").arg(quoteLibpqValue(user));
    if (!password.isEmpty())
        parts << QStringLiteral("password=%1").arg(quoteLibpqValue(password));
    parts << QStringLiteral("connect_timeout=%1").arg(qMax(1, connectTimeout));
    parts << QStringLiteral("application_name=%1")
                 .arg(quoteLibpqValue(QStringLiteral("czl_installer")));
    // 安装器只读配置，不允许任何会隐式写库的选项。
    parts << QStringLiteral("client_encoding=%1").arg(quoteLibpqValue(QStringLiteral("UTF8")));
    return parts.join(QLatin1Char(' '));
}

QString PgConnectionInfo::describe() const
{
    return QStringLiteral("%1:%2/%3 (user=%4)")
        .arg(host)
        .arg(port)
        .arg(database, user);
}

// ---------------------------------------------------------------------------
// 解析
// ---------------------------------------------------------------------------
bool parsePgConnectionString(const QString &text, PgConnectionInfo &info)
{
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty())
        return false;

    // ---- 1. URI ----
    if (trimmed.startsWith(QLatin1String("postgres://"), Qt::CaseInsensitive)
        || trimmed.startsWith(QLatin1String("postgresql://"), Qt::CaseInsensitive)) {
        const QUrl url(trimmed);
        if (!url.isValid() || url.host().isEmpty())
            return false;

        info.host = url.host();
        if (url.port() > 0)
            info.port = url.port();
        if (!url.userName().isEmpty())
            info.user = QUrl::fromPercentEncoding(url.userName().toUtf8());
        if (!url.password().isEmpty())
            info.password = QUrl::fromPercentEncoding(url.password().toUtf8());

        // path 形如 "/czl_installer"
        QString path = url.path();
        while (path.startsWith(QLatin1Char('/')))
            path.remove(0, 1);
        if (!path.isEmpty())
            info.database = path;

        const QUrlQuery query(url);
        if (query.hasQueryItem(QStringLiteral("connect_timeout"))) {
            const int timeout = query.queryItemValue(QStringLiteral("connect_timeout")).toInt();
            if (timeout > 0)
                info.connectTimeout = timeout;
        }
        return true;
    }

    // ---- 2. key=value ----
    if (parseKeyValuePairs(trimmed, info))
        return true;

    // ---- 3/4. 简写 / 纯主机 ----
    return parseShorthand(trimmed, info);
}

QString defaultPgConnectionString()
{
    const PgConnectionInfo defaults;
    return QStringLiteral("%1:%2,%3,%4,%5")
        .arg(defaults.host)
        .arg(defaults.port)
        .arg(defaults.user, defaults.password, defaults.database);
}
