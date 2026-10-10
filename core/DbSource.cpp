#include "DbSource.h"

#include <QDebug>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSet>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

#include <functional>
#include <utility>

namespace {

/// 逐个递增的连接名，保证多次构造 DbSource 不会互相覆盖。
QString nextConnectionName()
{
    static int counter = 0;
    return QStringLiteral("czl_pg_%1").arg(++counter);
}

/// QVariant -> QStringList（QPSQL 把 text[] 读成 QStringList，jsonb 读成字符串）。
QStringList toStringList(const QVariant &value)
{
    if (!value.isValid() || value.isNull())
        return {};

    if (value.typeId() == QMetaType::QStringList)
        return value.toStringList();

    const QString text = value.toString().trimmed();
    if (text.isEmpty())
        return {};

    // jsonb 里的数组：["a","b"]
    const QJsonDocument doc = QJsonDocument::fromJson(text.toUtf8());
    if (doc.isArray()) {
        QStringList result;
        const QJsonArray array = doc.array();
        result.reserve(array.size());
        for (const QJsonValue &entry : array) {
            if (entry.isString()) {
                const QString item = entry.toString();
                if (!item.isEmpty())
                    result.append(item);
            } else if (!entry.isNull() && !entry.isUndefined()) {
                result.append(QString::number(entry.toDouble()));
            }
        }
        if (!result.isEmpty())
            return result;
    }

    // 退化成「按行拆分」，方便运维直接在 jsonb 里写多行文本
    QStringList result;
    const QStringList lines = text.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (const QString &line : lines) {
        const QString item = line.trimmed();
        if (!item.isEmpty())
            result.append(item);
    }
    return result;
}

/// 把 JSONB 列的值转成 QJsonValue（QPSQL 返回的是文本形式的 JSON）。
QJsonValue toJsonValue(const QVariant &value)
{
    if (!value.isValid() || value.isNull())
        return {};

    const QString text = value.toString().trimmed();
    if (text.isEmpty())
        return {};

    QJsonParseError error{};
    const QJsonDocument doc = QJsonDocument::fromJson(text.toUtf8(), &error);
    if (error.error == QJsonParseError::NoError)
        return doc.isArray() ? QJsonValue(doc.array())
                             : (doc.isObject() ? QJsonValue(doc.object())
                                               : QJsonValue(text));

    return QJsonValue(text);
}

/// 统一把字段读成去空白字符串。
QString fieldText(const QSqlQuery &query, int index)
{
    return query.value(index).toString().trimmed();
}

/// 执行只读查询；失败时把驱动错误写进 errorMessage。
bool runQuery(QSqlQuery &query, const QString &sql, QString &errorMessage)
{
    if (!query.exec(sql)) {
        errorMessage = QStringLiteral("SQL 执行失败: %1\n%2")
                           .arg(query.lastError().databaseText().trimmed(),
                                query.lastError().driverText().trimmed());
        return false;
    }
    return true;
}

} // namespace

// ===========================================================================
// 构造 / 析构
// ===========================================================================
DbSource::DbSource(const PgConnectionInfo &connection)
    : m_connection(connection)
    , m_connectionName(nextConnectionName())
{
}

DbSource::~DbSource()
{
    close();
}

void DbSource::close()
{
    if (!m_open)
        return;

    // QSqlDatabase 的所有副本都必须先失效，否则 removeDatabase 会告警。
    {
        QSqlDatabase db = QSqlDatabase::database(m_connectionName, false);
        if (db.isValid() && db.isOpen())
            db.close();
    }
    QSqlDatabase::removeDatabase(m_connectionName);
    m_open = false;
}

// ===========================================================================
// 连接
// ===========================================================================
bool DbSource::ensureOpen(QString &errorMessage)
{
    if (m_open)
        return true;

    if (!QSqlDatabase::isDriverAvailable(QStringLiteral("QPSQL"))) {
        errorMessage = QStringLiteral(
            "Qt 未安装 QPSQL 数据库驱动，无法使用 PostgreSQL 配置模式。\n"
            "请确认程序目录下存在 sqldrivers/qsqlpsql.dll，"
            "并且其依赖的 libpq.dll 等 PostgreSQL 客户端库可被加载。");
        return false;
    }

    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QPSQL"),
                                                m_connectionName);
    db.setHostName(m_connection.host);
    db.setPort(m_connection.port);
    db.setDatabaseName(m_connection.database);
    db.setUserName(m_connection.user);
    db.setPassword(m_connection.password);
    db.setConnectOptions(QStringLiteral("connect_timeout=%1")
                             .arg(qMax(1, m_connection.connectTimeout)));

    if (!db.open()) {
        errorMessage = QStringLiteral("无法连接 PostgreSQL %1\n%2")
                           .arg(m_connection.describe(),
                                db.lastError().text().trimmed());
        // 打开失败也要把连接名清掉，否则下次 addDatabase 会告警「重复连接名」
        db = QSqlDatabase();
        QSqlDatabase::removeDatabase(m_connectionName);
        return false;
    }

    m_open = true;

    // ★ 安装器对数据库只有读需求：显式开一个只读事务，
    //   这样即使将来某段 SQL 写错了，也不可能污染 Web 管理端的数据。
    {
        QSqlQuery guard(QSqlDatabase::database(m_connectionName));
        if (!guard.exec(QStringLiteral("BEGIN READ ONLY"))) {
            // 只读事务开启失败不影响读取，仅记录（部分连接池/中间件不支持）
            qWarning() << "无法开启只读事务，继续以普通连接读取:"
                       << guard.lastError().text().trimmed();
        } else {
            qInfo() << "PostgreSQL 已连接（只读事务）:" << m_connection.describe();
        }
    }

    return true;
}

// ===========================================================================
// 解析：installArgs
// ===========================================================================
QStringList DbSource::parseArgsValue(const QJsonValue &value)
{
    if (value.isArray()) {
        QStringList result;
        const QJsonArray array = value.toArray();
        result.reserve(array.size());
        for (const QJsonValue &entry : array) {
            if (entry.isString()) {
                const QString text = entry.toString();
                if (!text.isEmpty())
                    result.append(text);
            }
        }
        return result;
    }

    const QString raw = value.toString();
    if (raw.isEmpty())
        return {};

    // 与 ConfigManager 的字符串形式保持一致：按「引号外空白」切分。
    QStringList args;
    QString current;
    QChar quote;

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

// ===========================================================================
// 加载
// ===========================================================================
DbSource::Response DbSource::load()
{
    Response response;

    QString error;
    if (!ensureOpen(error)) {
        response.errorMessage = error;
        return response;
    }

    if (!loadSettings(response) || !loadServers(response)
        || !loadBookmarks(response) || !loadCategories(response)
        || !loadItems(response) || !loadSources(response)) {
        // errorMessage 已由各步骤写入
        return response;
    }

    if (response.servers.isEmpty()) {
        response.errorMessage = QStringLiteral(
            "数据库表 czl_servers 中没有启用的下载服务器（enabled=true）。");
        return response;
    }

    response.success = true;
    return response;
}

bool DbSource::loadSettings(Response &response)
{
    QSqlQuery query(QSqlDatabase::database(m_connectionName));
    QString error;
    if (!runQuery(query, QStringLiteral("SELECT key, value FROM czl_settings"),
                  error)) {
        response.errorMessage = error;
        return false;
    }

    while (query.next()) {
        const QString key = query.value(0).toString().trimmed();
        const QString value = query.value(1).toString().trimmed();
        if (key == QLatin1String("localBaseDir") && !value.isEmpty())
            response.baseDir = value;
        else if (key == QLatin1String("installerHttpBaseUrl") && !value.isEmpty())
            response.httpBaseUrl = value;
    }
    return true;
}

bool DbSource::loadServers(Response &response)
{
    QSqlQuery query(QSqlDatabase::database(m_connectionName));
    QString error;
    // 注意：列名与 tornado/sql/schema.sql 严格一致
    if (!runQuery(query,
                  QStringLiteral(
                      "SELECT name, type, host, port, base_path, base_url, "
                      "       username, password, connect_timeout "
                      "FROM czl_servers "
                      "WHERE enabled = true "
                      "ORDER BY priority ASC, id ASC"),
                  error)) {
        response.errorMessage = error;
        return false;
    }

    while (query.next()) {
        ServerConfig server;
        server.name           = fieldText(query, 0);
        server.type           = fieldText(query, 1).toLower();
        server.host           = fieldText(query, 2);
        server.port           = query.value(3).toInt();
        server.basePath       = fieldText(query, 4);
        server.baseUrl        = fieldText(query, 5);
        server.username       = query.value(6).toString();
        server.password       = query.value(7).toString();
        server.connectTimeout = query.value(8).toInt();

        if (server.port <= 0)
            server.port = (server.type == QLatin1String("http")) ? 80 : 21;
        if (server.connectTimeout <= 0)
            server.connectTimeout = 5000;
        if (server.type.isEmpty())
            server.type = server.baseUrl.isEmpty() ? QStringLiteral("ftp")
                                                   : QStringLiteral("http");
        if (server.name.isEmpty()) {
            server.name = (server.type == QLatin1String("http"))
                              ? server.baseUrl
                              : QStringLiteral("%1:%2")
                                    .arg(server.host)
                                    .arg(server.port);
        }
        if (server.type == QLatin1String("http") && !server.baseUrl.isEmpty()
            && !server.baseUrl.endsWith(QLatin1Char('/')))
            server.baseUrl += QLatin1Char('/');
        if (server.type == QLatin1String("ftp") && !server.basePath.isEmpty()
            && !server.basePath.endsWith(QLatin1Char('/')))
            server.basePath += QLatin1Char('/');

        if (server.host.isEmpty() && server.baseUrl.isEmpty())
            continue;               // 既无主机也无 URL 的脏数据直接跳过

        response.servers.append(server);
    }

    return true;
}

bool DbSource::loadBookmarks(Response &response)
{
    QSqlQuery query(QSqlDatabase::database(m_connectionName));
    QString error;
    if (!runQuery(query,
                  QStringLiteral("SELECT name, url FROM czl_bookmarks "
                                 "WHERE enabled = true "
                                 "ORDER BY sort_order ASC, id ASC"),
                  error)) {
        response.errorMessage = error;
        return false;
    }

    while (query.next()) {
        BookmarkItem bookmark;
        bookmark.url = fieldText(query, 1);
        if (bookmark.url.isEmpty())
            continue;               // 与 JSON 模式一致：URL 为空的条目丢弃
        bookmark.name = fieldText(query, 0);
        if (bookmark.name.isEmpty())
            bookmark.name = bookmark.url;
        response.bookmarks.append(bookmark);
    }
    return true;
}

bool DbSource::loadCategories(Response &response)
{
    struct Row {
        qint64  id = 0;
        QString code;
        QString name;
        qint64  parentId = 0;
    };

    QSqlQuery query(QSqlDatabase::database(m_connectionName));
    QString error;
    if (!runQuery(query,
                  QStringLiteral("SELECT id, code, name, COALESCE(parent_id, 0) "
                                 "FROM czl_categories "
                                 "ORDER BY sort_order ASC, id ASC"),
                  error)) {
        response.errorMessage = error;
        return false;
    }

    QList<Row> rows;
    while (query.next()) {
        Row row;
        row.id       = query.value(0).toLongLong();
        row.code     = fieldText(query, 1);
        row.name     = fieldText(query, 2);
        row.parentId = query.value(3).toLongLong();
        if (row.code.isEmpty() && row.name.isEmpty())
            continue;
        if (row.name.isEmpty())
            row.name = row.code;
        rows.append(row);
    }

    // ---- 用显式栈建树，避免递归 lambda 依赖 <functional> 顺序问题 ----
    QHash<qint64, int> indexById;    // 数据库 id -> 顶层列表下标
    QList<Category>    topLevel;

    // 第一遍：只处理顶层（parent_id 为空或指向不存在的父节点）
    QSet<qint64> knownIds;
    for (const Row &row : rows)
        knownIds.insert(row.id);

    for (const Row &row : rows) {
        if (row.parentId != 0 && knownIds.contains(row.parentId))
            continue;
        Category category;
        category.id   = row.code;
        category.name = row.name;
        indexById.insert(row.id, static_cast<int>(topLevel.size()));
        topLevel.append(category);
    }

    // 第二遍：按深度反复挂载子节点（配置表最多几千行，层数有限）
    QSet<qint64> placed;
    for (const Row &row : rows)
        if (row.parentId == 0 || !knownIds.contains(row.parentId))
            placed.insert(row.id);

    // 父节点 id -> 已插入的 Category 指针（用 QList 下标定位，避免指针失效）
    // 路径：顶层下标 / 子下标 / ...
    QHash<qint64, QList<int>> pathById;
    for (auto it = indexById.constBegin(); it != indexById.constEnd(); ++it)
        pathById.insert(it.key(), QList<int>{it.value()});

    std::function<Category *(const QList<int> &)> resolve =
        [&](const QList<int> &path) -> Category * {
        if (path.isEmpty())
            return nullptr;
        Category *node = &topLevel[path.at(0)];
        for (int depth = 1; depth < path.size(); ++depth)
            node = &node->children[path.at(depth)];
        return node;
    };

    bool progressed = true;
    while (progressed) {
        progressed = false;
        for (const Row &row : rows) {
            if (placed.contains(row.id))
                continue;
            if (!pathById.contains(row.parentId))
                continue;

            Category *parent = resolve(pathById.value(row.parentId));
            if (parent == nullptr)
                continue;

            Category child;
            child.id   = row.code;
            child.name = row.name;
            parent->children.append(child);

            QList<int> path = pathById.value(row.parentId);
            path.append(parent->children.size() - 1);
            pathById.insert(row.id, path);
            placed.insert(row.id);
            progressed = true;
        }
    }

    // 剩下的行说明父节点被删/成环：作为顶层节点兜底，避免配置整体丢失
    for (const Row &row : rows) {
        if (placed.contains(row.id))
            continue;
        Category orphan;
        orphan.id   = row.code;
        orphan.name = row.name;
        topLevel.append(orphan);
        pathById.insert(row.id, QList<int>{static_cast<int>(topLevel.size()) - 1});
        placed.insert(row.id);
        response.warnings.append(
            QStringLiteral("分类 %1 的父节点缺失，已提升为顶层分类").arg(row.code));
    }

    response.categories = topLevel;
    return true;
}

bool DbSource::loadItems(Response &response)
{
    QSqlQuery query(QSqlDatabase::database(m_connectionName));
    QString error;
    if (!runQuery(query,
                  QStringLiteral(
                      "SELECT i.code, i.name, COALESCE(c.code, '') AS category_code, "
                      "       i.type, i.description, i.version, i.publisher, "
                      "       i.remote_path, i.local_temp_path, i.install_args, "
                      "       i.target_path, i.overwrite, i.files, "
                      "       i.register_commands, i.kill_processes, i.kill_services, "
                      // 软件项自身的 sha256/大小优先；没填就回退到已上传软件包
                      // 的登记信息（czl_packages 按相对路径匹配）。
                      "       COALESCE(i.file_size, "
                      "                (SELECT p.file_size FROM czl_packages p "
                      "                  WHERE p.rel_path = i.remote_path LIMIT 1), 0), "
                      "       COALESCE(NULLIF(i.sha256, ''), "
                      "                (SELECT p.sha256 FROM czl_packages p "
                      "                  WHERE p.rel_path = i.remote_path LIMIT 1), '') "
                      "FROM czl_items i "
                      "LEFT JOIN czl_categories c ON c.id = i.category_id "
                      "WHERE i.enabled = true "
                      "ORDER BY i.sort_order ASC, i.id ASC"),
                  error)) {
        response.errorMessage = error;
        return false;
    }

    // 分类 code -> 分类指针（用递归查找定位，避免保存易失效的指针）
    std::function<Category *(QList<Category> &, const QString &)> findCategory =
        [&](QList<Category> &categories, const QString &code) -> Category * {
        for (Category &category : categories) {
            if (category.id == code)
                return &category;
            if (Category *found = findCategory(category.children, code))
                return found;
        }
        return nullptr;
    };

    while (query.next()) {
        InstallItem item;
        const QString code = fieldText(query, 0);
        if (code.isEmpty())
            continue;

        item.id            = code;
        item.name          = fieldText(query, 1);
        const QString categoryCode = fieldText(query, 2);
        item.type          = fieldText(query, 3).toLower();
        item.description   = query.value(4).toString();
        item.version       = query.value(5).toString();
        item.publisher     = query.value(6).toString();
        item.remotePath    = fieldText(query, 7);
        item.localTempPath = fieldText(query, 8);
        item.installArgs   = parseArgsValue(toJsonValue(query.value(9)));
        item.targetPath    = fieldText(query, 10);
        item.overwrite     = query.value(11).toBool();
        item.files         = toStringList(query.value(12));
        item.registerCommands = toStringList(query.value(13));
        item.killProcesses = toStringList(query.value(14));
        item.killServices  = toStringList(query.value(15));
        item.fileSize      = query.value(16).toLongLong();
        item.sha256        = fieldText(query, 17).toLower();

        if (item.name.isEmpty())
            item.name = item.id;

        Category *category = findCategory(response.categories, categoryCode);
        if (category == nullptr) {
            // 没挂分类的软件项放进「未分类」，保证 UI 不丢数据
            Category *fallback = findCategory(response.categories,
                                              QStringLiteral("__uncategorized__"));
            if (fallback == nullptr) {
                Category uncategorized;
                uncategorized.id   = QStringLiteral("__uncategorized__");
                uncategorized.name = QStringLiteral("未分类");
                response.categories.append(uncategorized);
                fallback = &response.categories.last();
            }
            category = fallback;
            if (!categoryCode.isEmpty())
                response.warnings.append(
                    QStringLiteral("软件项 %1 引用的分类 %2 不存在，已归入「未分类」")
                        .arg(item.id, categoryCode));
        }

        // 与 ConfigManager 的 JSON 行为保持一致：类别下的软件项 id 补全为
        // "父类别/…/项"，这样选中父类别时也能列出全部后代安装项。
        if (!item.id.contains(QLatin1Char('/')) && !category->id.isEmpty()
            && category->id != QLatin1String("__uncategorized__"))
            item.id = category->id + QLatin1Char('/') + item.id;

        category->items.append(item);
    }

    return true;
}

bool DbSource::loadSources(Response &response)
{
    QSqlQuery query(QSqlDatabase::database(m_connectionName));
    QString error;
    if (!runQuery(query,
                  QStringLiteral(
                      "SELECT i.code, s.kind, s.remote_path, s.local_path, "
                      "       s.files, s.target_path "
                      "FROM czl_item_sources s "
                      "JOIN czl_items i ON i.id = s.item_id "
                      "ORDER BY s.item_id ASC, s.sort_order ASC, s.id ASC"),
                  error)) {
        response.errorMessage = error;
        return false;
    }

    // 收集：未补全的 code -> 源列表
    QHash<QString, QList<CopySource>> byCode;
    QHash<QString, QString> displayNameByCode;
    while (query.next()) {
        const QString code = fieldText(query, 0);
        if (code.isEmpty())
            continue;

        CopySource source;
        source.kind       = fieldText(query, 1).toLower();
        source.remotePath = fieldText(query, 2);
        source.localPath  = fieldText(query, 3);
        source.files      = toStringList(query.value(4));
        source.targetPath = fieldText(query, 5);

        if (source.remotePath.isEmpty())
            continue;
        if (source.kind.isEmpty()) {
            source.kind = (source.remotePath.endsWith(QLatin1Char('/'))
                           || source.remotePath.endsWith(QLatin1Char('\\')))
                              ? QStringLiteral("dir")
                              : (source.remotePath.endsWith(QLatin1String(".zip"),
                                                            Qt::CaseInsensitive)
                                     ? QStringLiteral("zip")
                                     : QStringLiteral("file"));
        }
        if (source.localPath.isEmpty())
            source.localPath = source.remotePath;

        byCode[code].append(source);
    }

    if (byCode.isEmpty())
        return true;

    std::function<void(Category &)> applyToCategory = [&](Category &category) {
        for (InstallItem &item : category.items) {
            // item.id 可能已被补全成 "分类/项"，两种写法都要能命中
            QList<CopySource> sources = byCode.value(item.id);
            if (sources.isEmpty()) {
                const int slash = item.id.lastIndexOf(QLatin1Char('/'));
                if (slash >= 0)
                    sources = byCode.value(item.id.mid(slash + 1));
            }
            if (!sources.isEmpty()) {
                item.sources = sources;
                byCode.remove(item.id);
            }
        }
        for (Category &child : category.children)
            applyToCategory(child);
    };

    for (Category &category : response.categories)
        applyToCategory(category);

    // 剩余的源：对应软件项没启用或分类被删，记为警告而不是丢弃
    for (auto it = byCode.constBegin(); it != byCode.constEnd(); ++it) {
        response.warnings.append(
            QStringLiteral("软件项 %1 的 %2 个拷贝源未匹配到启用中的软件项，已忽略")
                .arg(it.key())
                .arg(it.value().size()));
    }

    return true;
}
