#include "ConfigManager.h"

#include "DbSource.h"

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
#include <utility>

namespace {

/// 去掉字符串首尾空白（含 \r）。
QString trimmed(const QString &value)
{
    return value.trimmed();
}

/// 模式一默认配置文件。
const char *const kJsonConfigFile = "config/config_ftp.json";
/// 模式二的连接参数文件（同一目录，与模式一互不影响）。
const char *const kPgConfigFile = "config/config_pg.json";

} // namespace

ConfigManager::ConfigManager(QObject *parent)
    : QObject(parent)
{
    // 默认连接串与 PgConnectionInfo 的默认值保持一致：
    // 192.168.1.1:5432 / alice / alice123 / czl_installer
    m_pgConnectionString = defaultPgConnectionString();
    m_pgConnection = PgConnectionInfo{};
}

QString ConfigManager::defaultJsonConfigPath()
{
    return QString::fromLatin1(kJsonConfigFile);
}

QString ConfigManager::defaultPgConnectionString()
{
    return ::defaultPgConnectionString();
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
    const QString baseDir = root.value("localBaseDir").toString().trimmed();

    commit(/*fromDatabase=*/false,
           std::move(servers),
           std::move(categories),
           std::move(bookmarks),
           baseDir.isEmpty() ? m_localBaseDir : baseDir,
           /*httpBaseUrl=*/QString(),
           resolved,
           /*warnings=*/{});

    return true;
}

// ---------------------------------------------------------------------------
// 模式二：载入连接参数文件 config/config_pg.json
// ---------------------------------------------------------------------------
bool ConfigManager::loadPgSettingsFromJson(const QString &filePath,
                                           QString &connectionOut)
{
    connectionOut.clear();

    const QString resolved = resolvePath(filePath);
    QFile file(resolved);
    if (!file.open(QIODevice::ReadOnly)) {
        // 连接参数文件不是必需的：没有就用内置默认值（192.168.1.1:5432）
        qInfo() << "未找到 PostgreSQL 连接参数文件，使用内置默认连接:"
                << resolved;
        return false;
    }

    const QByteArray data = file.readAll();
    file.close();

    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        qWarning() << "PostgreSQL 连接参数文件解析失败:" << resolved
                   << parseError.errorString();
        return false;
    }

    const QJsonObject root = doc.object();

    // 兼容两种写法，优先级：拆开的分字段 > connectionString
    // （分字段更不容易写错，且能被界面逐项展示）
    PgConnectionInfo info;
    bool hasSplitFields = false;

    if (root.contains("host")) {
        info.host = root.value("host").toString().trimmed();
        hasSplitFields = true;
    }
    if (root.contains("port")) {
        info.port = root.value("port").toInt(info.port);
        hasSplitFields = true;
    }
    if (root.contains("database")) {
        info.database = root.value("database").toString().trimmed();
        hasSplitFields = true;
    }
    if (root.contains("user")) {
        info.user = root.value("user").toString().trimmed();
        hasSplitFields = true;
    }
    if (root.contains("password")) {
        info.password = root.value("password").toString();
        hasSplitFields = true;
    }
    if (root.contains("connectTimeout")) {
        info.connectTimeout = root.value("connectTimeout").toInt(info.connectTimeout);
        hasSplitFields = true;
    }

    if (hasSplitFields && !info.host.isEmpty()) {
        connectionOut = info.toConnectionString();
        return true;
    }

    // 只给一个连接串：语法完全交给 parsePgConnectionString
    const QString connection = root.value("connectionString").toString().trimmed();
    if (!connection.isEmpty()) {
        connectionOut = connection;
        return true;
    }

    return false;
}

// ---------------------------------------------------------------------------
// 模式二：从 PostgreSQL 加载
// ---------------------------------------------------------------------------
bool ConfigManager::loadFromPostgres(const QString &connectionString)
{
    PgConnectionInfo info = m_pgConnection;   // 保留未在连接串里出现的字段
    if (!connectionString.trimmed().isEmpty()
        && !parsePgConnectionString(connectionString, info)) {
        m_lastError = QStringLiteral("无法解析 PostgreSQL 连接串: %1")
                          .arg(connectionString);
        qWarning() << m_lastError;
        return false;
    }
    return loadFromPostgres(info);
}

bool ConfigManager::loadFromPostgres(const PgConnectionInfo &connection)
{
    m_lastError.clear();
    m_warnings.clear();

    DbSource source(connection);
    const DbSource::Response response = source.load();

    if (!response.success) {
        m_lastError = response.errorMessage.isEmpty()
                          ? QStringLiteral("从 PostgreSQL 读取配置失败（未知原因）")
                          : response.errorMessage;
        qWarning() << "PG 配置加载失败:" << m_lastError;
        return false;
    }

    for (const QString &warning : response.warnings)
        qWarning() << "PG 配置提示:" << warning;

    const QString baseDir = response.baseDir.trimmed().isEmpty()
                                ? m_localBaseDir
                                : response.baseDir.trimmed();

    commit(/*fromDatabase=*/true,
           response.servers,
           response.categories,
           response.bookmarks,
           baseDir,
           response.httpBaseUrl,
           QStringLiteral("postgresql://%1").arg(connection.describe()),
           response.warnings);

    // 连接参数只在成功之后才更新，失败时保留上一次可用的
    m_pgConnection = connection;
    m_pgConnectionString = connection.toConnectionString();

    return true;
}

// ---------------------------------------------------------------------------
// 模式切换
// ---------------------------------------------------------------------------
bool ConfigManager::setSource(ConfigSource source, const QString &fromJsonFile)
{
    if (source == m_source) {
        // 同一种来源被再次选中：JSON 模式重新读盘，PG 模式重新查库，
        // 这样 Web 端改完配置后安装器「刷新」即可生效。
        // 注意 fromJsonFile 可能是「显式指定的 JSON 路径」或「PG 连接串」，
        // 两者的含义由 source 决定，不能在这里丢掉。
        if (source == ConfigSource::PostgreSQL) {
            return fromJsonFile.trimmed().isEmpty()
                       ? loadFromPostgres(m_pgConnection)
                       : loadFromPostgres(fromJsonFile);
        }
        return loadConfig(fromJsonFile.trimmed().isEmpty()
                              ? defaultJsonConfigPath()
                              : fromJsonFile);
    }

    // 切换失败时回滚到切换前的来源，绝不处于「半切换」状态
    // （失败时数据没有被 commit，界面仍是旧数据，来源标记也必须跟着回旧值）。
    const ConfigSource previous = m_source;

    if (source == ConfigSource::PostgreSQL) {
        // 连接参数优先级：调用方给的 > config/config_pg.json > 内置默认
        QString connection;
        const QString settingsFile = fromJsonFile.trimmed().isEmpty()
                                         ? QString::fromLatin1(kPgConfigFile)
                                         : fromJsonFile;
        loadPgSettingsFromJson(settingsFile, connection);
        if (connection.trimmed().isEmpty())
            connection = m_pgConnectionString;

        if (!loadFromPostgres(connection)) {
            m_source = previous;
            return false;
        }

        m_source = ConfigSource::PostgreSQL;
        return true;
    }

    // 切回模式一
    const QString jsonFile = fromJsonFile.trimmed().isEmpty()
                                 ? defaultJsonConfigPath()
                                 : fromJsonFile;
    if (!loadConfig(jsonFile)) {
        m_source = previous;
        return false;
    }

    m_source = ConfigSource::JsonFile;
    return true;
}

QString ConfigManager::sourceDescription() const
{
    if (m_source == ConfigSource::PostgreSQL)
        return QStringLiteral("PostgreSQL 数据库 · HTTP 下载")
               + QLatin1Char('\n') + m_pgConnection.describe();
    return QStringLiteral("JSON 配置文件 · FTP 下载")
           + QLatin1Char('\n') + m_configFilePath;
}

// ---------------------------------------------------------------------------
// 提交一次成功加载
// ---------------------------------------------------------------------------
void ConfigManager::commit(bool fromDatabase,
                           QList<ServerConfig> servers,
                           QList<Category> categories,
                           QList<BookmarkItem> bookmarks,
                           const QString &localBaseDir,
                           const QString &httpBaseUrl,
                           const QString &origin,
                           const QStringList &warnings)
{
    m_servers    = std::move(servers);
    m_categories = std::move(categories);
    m_bookmarks  = std::move(bookmarks);
    m_warnings   = warnings;
    m_configFilePath = origin;
    m_httpBaseUrl = fromDatabase ? httpBaseUrl : QString();

    if (!localBaseDir.trimmed().isEmpty())
        m_localBaseDir = localBaseDir.trimmed();

    qInfo() << "配置加载完成:" << (fromDatabase ? "[PG]" : "[JSON]") << origin
            << "| localBaseDir=" << m_localBaseDir
            << "servers=" << m_servers.size()
            << "categories=" << m_categories.size()
            << "bookmarks=" << m_bookmarks.size()
            << "warnings=" << m_warnings.size();

    emit configChanged();
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
