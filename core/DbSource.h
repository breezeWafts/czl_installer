#ifndef DBSOURCE_H
#define DBSOURCE_H

#include <QList>
#include <QString>
#include <QStringList>

#include "ConfigManager.h"
#include "PgConfig.h"

class QJsonValue;

/**
 * @brief 从 PostgreSQL（czl_* 表）读取安装器配置。
 *
 * 这是「模式二」的数据源：PG 存配置 + HTTP 网络服务器存放软件包。
 * 读取结果是 ConfigManager 已经使用的同一批结构体（ServerConfig /
 * Category / InstallItem / BookmarkItem），因此 UI、TreeModel、
 * InstallWorker 全部不需要区分数据来自 JSON 还是数据库。
 *
 * 表结构与 Web 管理项目（tornado/）的 sql/schema.sql 严格对应：
 *   czl_settings / czl_servers / czl_bookmarks / czl_categories /
 *   czl_items / czl_item_sources
 *
 * 线程约定：只在 GUI 线程使用。DbSource 自己持有一个具名 QSqlDatabase
 * 连接，析构时关闭并 removeDatabase，不会泄漏驱动连接。
 */
class DbSource
{
public:
    /// 一次加载的完整结果；success=false 时 errorMessage 给出中文原因。
    struct Response {
        bool                 success = false;
        QString              errorMessage;
        QString              baseDir;       ///< czl_settings.localBaseDir
        QString              httpBaseUrl;   ///< czl_settings.installerHttpBaseUrl
        QList<ServerConfig>  servers;
        QList<Category>      categories;
        QList<BookmarkItem>  bookmarks;
        QStringList          warnings;
    };

    explicit DbSource(const PgConnectionInfo &connection);
    ~DbSource();

    DbSource(const DbSource &) = delete;
    DbSource &operator=(const DbSource &) = delete;

    /// 连接数据库并读取整份配置（连接在析构时释放）。
    Response load();

    /// 解析 installArgs 这类「字符串或数组」的 JSON 值。
    /// - 数组：逐项取出
    /// - 字符串：按引号规则切分（与 ConfigManager 的 JSON 行为一致）
    /// 供 ConfigManager 复用，避免两套解析语义不一致。
    static QStringList parseArgsValue(const QJsonValue &value);

private:
    bool ensureOpen(QString &errorMessage);
    void close();

    bool loadSettings(Response &response);
    bool loadServers(Response &response);
    bool loadBookmarks(Response &response);
    bool loadCategories(Response &response);
    bool loadItems(Response &response);
    bool loadSources(Response &response);

    PgConnectionInfo m_connection;
    QString          m_connectionName;   ///< QSqlDatabase 连接名（唯一）
    bool             m_open = false;
};

#endif // DBSOURCE_H
