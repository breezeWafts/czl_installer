#include "ConfigManager.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QLoggingCategory>

#include <functional>

namespace {

/// 去掉字符串首尾空白（含 \r）。
QString trimmed(const QString &value)
{
    return value.trimmed();
}

} // namespace

ConfigManager::ConfigManager(QObject *parent)
    : QObject(parent)
{
}

// ---------------------------------------------------------------------------
// 路径
// ---------------------------------------------------------------------------
QString ConfigManager::resolvePath(const QString &path)
{
    if (path.isEmpty())
        return path;

    QFileInfo info(path);
    if (info.isAbsolute())
        return QDir::cleanPath(path);

    // 相对路径按「可执行文件所在目录」解析。
    // 旧版直接用 "config/config_ftp.json"，在从其它工作目录启动时必然失败。
    return QDir::cleanPath(
        QCoreApplication::applicationDirPath() + QLatin1Char('/') + path);
}

// ---------------------------------------------------------------------------
// 加载
// ---------------------------------------------------------------------------
bool ConfigManager::loadConfig(const QString &filePath)
{
    m_lastError.clear();

    const QString resolved = resolvePath(filePath);
    QFile file(resolved);
    if (!file.open(QIODevice::ReadOnly)) {
        m_lastError = QStringLiteral("无法打开配置文件: %1 (%2)")
                          .arg(resolved, file.errorString());
        qWarning() << m_lastError;
        return false;
    }

    const QByteArray data = file.readAll();
    file.close();

    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        m_lastError = QStringLiteral("JSON 解析错误 (%1, 偏移 %2): %3")
                          .arg(resolved)
                          .arg(parseError.offset)
                          .arg(parseError.errorString());
        qWarning() << m_lastError;
        return false;
    }
    if (!doc.isObject()) {
        m_lastError = QStringLiteral("配置文件根节点必须是 JSON 对象: %1").arg(resolved);
        qWarning() << m_lastError;
        return false;
    }

    const QJsonObject root = doc.object();

    // ---- 服务器（支持 servers 数组与旧的单 server 对象） ----
    QList<ServerConfig> servers;
    if (root.contains(QStringLiteral("servers")) && root.value("servers").isArray()) {
        const QJsonArray array = root.value("servers").toArray();
        servers.reserve(array.size());
        for (const QJsonValue &value : array) {
            ServerConfig server;
            parseServer(value.toObject(), server);
            if (!server.host.isEmpty() || !server.baseUrl.isEmpty())
                servers.append(server);
        }
    } else if (root.contains(QStringLiteral("server"))
               && root.value("server").isObject()) {
        ServerConfig server;
        parseServer(root.value("server").toObject(), server);
        servers.append(server);
    }

    if (servers.isEmpty()) {
        m_lastError = QStringLiteral("配置文件中没有可用的服务器: %1").arg(resolved);
        qWarning() << m_lastError;
        return false;
    }

    // ---- 类别树 ----
    QList<Category> categories;
    const QJsonArray categoryArray = root.value("categories").toArray();
    categories.reserve(categoryArray.size());
    for (const QJsonValue &value : categoryArray) {
        Category category;
        parseCategory(value.toObject(), category);
        categories.append(category);
    }

    // ---- 书签（URL 为空的条目直接丢弃） ----
    QList<BookmarkItem> bookmarks;
    const QJsonArray bookmarkArray = root.value("bookmarks").toArray();
    bookmarks.reserve(bookmarkArray.size());
    for (const QJsonValue &value : bookmarkArray) {
        BookmarkItem bookmark;
        parseBookmark(value.toObject(), bookmark);
        if (!bookmark.url.isEmpty())
            bookmarks.append(bookmark);
    }

    // ---- 全部解析成功后才提交，保证失败时保留上一份可用配置 ----
    m_servers     = std::move(servers);
    m_categories  = std::move(categories);
    m_bookmarks   = std::move(bookmarks);
    m_configFilePath = resolved;

    const QString baseDir = root.value("localBaseDir").toString().trimmed();
    if (!baseDir.isEmpty())
        m_localBaseDir = baseDir;

    qInfo() << "配置加载完成:" << resolved
            << "| localBaseDir=" << m_localBaseDir
            << "servers=" << m_servers.size()
            << "categories=" << m_categories.size()
            << "bookmarks=" << m_bookmarks.size();

    emit configChanged();
    return true;
}

// ---------------------------------------------------------------------------
// 解析：服务器
// ---------------------------------------------------------------------------
void ConfigManager::parseServer(const QJsonObject &o, ServerConfig &s)
{
    s.name           = trimmed(o.value("name").toString());
    s.type           = o.value("type").toString().trimmed().toLower();
    s.host           = o.value("host").toString().trimmed();
    s.port           = o.value("port").toInt(21);
    s.username       = o.value("username").toString();
    s.password       = o.value("password").toString();
    s.basePath       = o.value("basePath").toString().trimmed();
    s.baseUrl        = o.value("baseUrl").toString().trimmed();
    s.connectTimeout = qMax(1000, o.value("connectTimeout").toInt(5000));

    if (s.type.isEmpty())
        s.type = s.baseUrl.isEmpty() ? QStringLiteral("ftp") : QStringLiteral("http");

    if (s.name.isEmpty()) {
        s.name = (s.type == QLatin1String("http"))
                     ? s.baseUrl
                     : QStringLiteral("%1:%2").arg(s.host).arg(s.port);
    }

    if (s.type == QLatin1String("ftp") && !s.basePath.isEmpty()
        && !s.basePath.endsWith(QLatin1Char('/')))
        s.basePath += QLatin1Char('/');

    if (s.type == QLatin1String("http") && !s.baseUrl.isEmpty()
        && !s.baseUrl.endsWith(QLatin1Char('/')))
        s.baseUrl += QLatin1Char('/');
}

// ---------------------------------------------------------------------------
// 解析：拷贝源
// ---------------------------------------------------------------------------
void ConfigManager::parseCopySource(const QJsonObject &o, CopySource &src)
{
    src.kind       = o.value("kind").toString().trimmed().toLower();
    src.remotePath = o.value("remotePath").toString().trimmed();
    src.localPath  = o.value("localPath").toString().trimmed();
    src.targetPath = o.value("targetPath").toString().trimmed();
    src.files      = readStringList(o, QStringLiteral("files"));

    if (src.kind.isEmpty())
        src.kind = (src.remotePath.endsWith(QLatin1Char('/'))
                    || src.remotePath.endsWith(QLatin1Char('\\')))
                       ? QStringLiteral("dir")
                       : (src.remotePath.endsWith(QLatin1String(".zip"),
                                                  Qt::CaseInsensitive)
                              ? QStringLiteral("zip")
                              : QStringLiteral("file"));
}

// ---------------------------------------------------------------------------
// 解析：字符串 / 字符串数组
// ---------------------------------------------------------------------------
QStringList ConfigManager::readStringList(const QJsonObject &o, const QString &key)
{
    QStringList result;
    const QJsonValue value = o.value(key);

    if (value.isArray()) {
        const QJsonArray array = value.toArray();
        result.reserve(array.size());
        for (const QJsonValue &entry : array) {
            if (entry.isString()) {
                // QJsonValue::toString() 返回的是临时值，必须先存入具名
                // QString 再取引用，否则下面的 isEmpty() 是悬垂引用。
                const QString text = entry.toString();
                if (!text.isEmpty())
                    result.append(text);
            }
        }
    } else if (value.isString()) {
        const QString text = value.toString();
        if (!text.isEmpty())
            result.append(text);
    }
    return result;
}

// ---------------------------------------------------------------------------
// 解析：installArgs
// ---------------------------------------------------------------------------
QStringList ConfigManager::parseArgs(const QJsonObject &o)
{
    const QJsonValue value = o.value("installArgs");

    // 数组形式：直接采用，不做任何拆分（最安全的写法）
    if (value.isArray())
        return readStringList(o, QStringLiteral("installArgs"));

    const QString raw = value.toString();
    if (raw.isEmpty())
        return {};

    // 字符串形式：按「引号外空白」切分。
    // 旧实现用 QString::split(' ')，"/configure config.xml" 会被切成两项，
    // 后面再用引号包住 exe 路径，参数语义就完全错了。
    QStringList args;
    QString current;
    QChar quote;                       // 当前所在的引号类型

    for (const QChar ch : raw) {
        if (!quote.isNull()) {
            if (ch == quote)
                quote = QChar();
            else
                current.append(ch);
            continue;
        }
        if (ch == QLatin1Char('"') || ch == QLatin1Char('\'')) {
            quote = ch;
            continue;
        }
        if (ch.isSpace()) {
            if (!current.isEmpty()) {
                args.append(current);
                current.clear();
            }
            continue;
        }
        current.append(ch);
    }
    if (!current.isEmpty())
        args.append(current);

    return args;
}

// ---------------------------------------------------------------------------
// 解析：安装项
// ---------------------------------------------------------------------------
void ConfigManager::parseInstallItem(const QJsonObject &o, InstallItem &item)
{
    item.id            = o.value("id").toString().trimmed();
    item.name          = o.value("name").toString().trimmed();
    item.type          = o.value("type").toString().trimmed().toLower();
    item.description   = o.value("description").toString();
    item.version       = o.value("version").toString();
    item.publisher     = o.value("publisher").toString();
    item.remotePath    = o.value("remotePath").toString().trimmed();
    item.localTempPath = o.value("localTempPath").toString().trimmed();
    item.targetPath    = o.value("targetPath").toString().trimmed();
    item.overwrite     = o.value("overwrite").toBool(true);

    item.installArgs      = parseArgs(o);
    item.files            = readStringList(o, QStringLiteral("files"));
    item.registerCommands = readStringList(o, QStringLiteral("registerCommands"));
    item.killProcesses    = readStringList(o, QStringLiteral("killProcesses"));
    item.killServices     = readStringList(o, QStringLiteral("killServices"));

    // ---- sources 数组 ----
    item.sources.clear();
    if (o.value("sources").isArray()) {
        const QJsonArray array = o.value("sources").toArray();
        item.sources.reserve(array.size());
        for (const QJsonValue &value : array) {
            CopySource source;
            parseCopySource(value.toObject(), source);
            if (!source.remotePath.isEmpty())
                item.sources.append(source);
        }
    }

    // ---- 兼容旧版「单源」写法 ----
    const bool isCopyType = (item.type == QLatin1String("copy_only")
                             || item.type == QLatin1String("copy_and_register"));
    if (item.sources.isEmpty() && isCopyType && !item.remotePath.isEmpty()) {
        CopySource source;
        source.remotePath = item.remotePath;
        source.localPath  = item.localTempPath;
        source.files      = item.files;
        source.targetPath = item.targetPath;
        source.kind = (item.remotePath.endsWith(QLatin1Char('/'))
                       || item.remotePath.endsWith(QLatin1Char('\\')))
                          ? QStringLiteral("dir")
                          : (item.remotePath.endsWith(QLatin1String(".zip"),
                                                      Qt::CaseInsensitive)
                                 ? QStringLiteral("zip")
                                 : QStringLiteral("file"));
        item.sources.append(source);
    }

    if (item.name.isEmpty())
        item.name = item.id;
}

// ---------------------------------------------------------------------------
// 解析：类别
// ---------------------------------------------------------------------------
void ConfigManager::parseCategory(const QJsonObject &o, Category &cat)
{
    cat.id   = o.value("id").toString().trimmed();
    cat.name = o.value("name").toString().trimmed();
    if (cat.name.isEmpty())
        cat.name = cat.id;

    const QJsonArray children = o.value("children").toArray();
    cat.children.reserve(children.size());
    for (const QJsonValue &value : children) {
        Category child;
        parseCategory(value.toObject(), child);
        cat.children.append(child);
    }

    const QJsonArray items = o.value("items").toArray();
    cat.items.reserve(items.size());
    for (const QJsonValue &value : items) {
        InstallItem item;
        parseInstallItem(value.toObject(), item);
        if (item.id.isEmpty())
            continue;

        // 子类别下的安装项补全为 "父类别/子类别/项"，
        // 这样选中父类别时也能列出全部后代安装项
        // （旧版只列直接 items，选父节点常常是空列表）。
        if (item.id.indexOf(QLatin1Char('/')) < 0 && !cat.id.isEmpty())
            item.id = cat.id + QLatin1Char('/') + item.id;

        cat.items.append(item);
    }
}

// ---------------------------------------------------------------------------
// 解析：书签
// ---------------------------------------------------------------------------
void ConfigManager::parseBookmark(const QJsonObject &o, BookmarkItem &b)
{
    b.name = o.value("name").toString().trimmed();
    b.url  = o.value("url").toString().trimmed();
    if (b.name.isEmpty())
        b.name = b.url;
}

// ---------------------------------------------------------------------------
// 查询
// ---------------------------------------------------------------------------
bool ConfigManager::findItem(const QString &id, InstallItem &out) const
{
    // 显式使用 std::function，避免旧版里 GCC 因缺少 <functional> 而报
    // "'function' is not a member of 'std'" 的编译错误。
    std::function<bool(const QList<Category> &)> walk =
        [&](const QList<Category> &categories) -> bool {
        for (const Category &category : categories) {
            for (const InstallItem &item : category.items) {
                if (item.id == id) {
                    out = item;
                    return true;
                }
            }
            if (walk(category.children))
                return true;
        }
        return false;
    };
    return walk(m_categories);
}

bool ConfigManager::findCategory(const QString &id, Category &out) const
{
    std::function<bool(const QList<Category> &)> walk =
        [&](const QList<Category> &categories) -> bool {
        for (const Category &category : categories) {
            if (category.id == id) {
                out = category;
                return true;
            }
            if (walk(category.children))
                return true;
        }
        return false;
    };
    return walk(m_categories);
}
