#ifndef CONFIGMANAGER_H
#define CONFIGMANAGER_H

#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

#include "PgConfig.h"

class QJsonObject;

// ---------------------------------------------------------------------------
// 配置数据结构
// ---------------------------------------------------------------------------

/// 一台可用的下载服务器（FTP / HTTP 多地址备选）。
struct ServerConfig {
    QString name;                   ///< 显示名，如 "ftp1-主用"
    QString type;                   ///< "ftp" | "http"
    QString host;                   ///< FTP 主机
    int     port = 21;              ///< FTP 端口
    QString username;
    QString password;
    QString basePath;               ///< FTP 远程根目录（保证以 / 结尾）
    QString baseUrl;                ///< HTTP 基础 URL（保证以 / 结尾）
    int     connectTimeout = 5000;  ///< 毫秒
};

/// copy_* 类型安装项的一个拷贝源。
struct CopySource {
    QString     kind;               ///< "file" | "dir" | "zip"
    QString     remotePath;
    QString     localPath;
    QStringList files;              ///< kind=dir 时的文件清单
    QString     targetPath;         ///< 空则回退到 InstallItem::targetPath
};

/// 一个待安装项。
struct InstallItem {
    QString id;
    QString name;
    QString type;                   ///< msi | exe | bat | copy_only | copy_and_register
    QString description;
    QString version;
    QString publisher;

    QString     remotePath;         ///< exe / msi / bat 用
    QString     localTempPath;
    QStringList installArgs;
    QStringList files;              ///< 兼容旧版「目录单源」写法

    QString            targetPath;  ///< copy 类型用
    bool               overwrite = true;
    QList<CopySource>  sources;
    QStringList        registerCommands;

    QStringList killProcesses;      ///< 安装前清理
    QStringList killServices;

    /// 软件包大小 / 校验和（来自数据库的 czl_items 或 czl_packages）。
    /// JSON 配置里没有这两项，保持 0 / 空即可。
    qint64  fileSize = 0;
    QString sha256;                 ///< 小写十六进制；为空表示不校验
};

/// 递归类别节点。
struct Category {
    QString          id;
    QString          name;
    QList<Category>  children;
    QList<InstallItem> items;
};

/// 浏览器书签项。
struct BookmarkItem {
    QString name;
    QString url;
};

// ---------------------------------------------------------------------------
// 配置管理器
// ---------------------------------------------------------------------------

/**
 * @brief 统一管理两种配置来源，并向 UI / 安装线程提供只读快照。
 *
 *   - ConfigSource::JsonFile（模式一）：config/config_ftp.json
 *       配置文件 + FTP 服务器存放软件包。下载服务器来自 JSON 的
 *       servers 数组，树/软件项来自 categories。
 *   - ConfigSource::PostgreSQL（模式二）：PostgreSQL 数据库（czl_* 表）
 *       配置文件 + HTTP 网络服务器存放软件包。由 Web 管理端维护，
 *       下载服务器、分类、软件项、书签全部来自数据库。
 *
 * 两种模式产出完全相同的结构体，因此 TreeModel / InstallItemModel /
 * InstallManager / InstallWorker / MainWindow 都不需要区分来源。
 *
 * 线程约定：只在 GUI 线程创建与调用。安装线程拿到的是值拷贝
 * （QList<ServerConfig> / InstallItem），不存在共享可变状态。
 */
class ConfigManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString localBaseDir READ localBaseDir NOTIFY configChanged)

public:
    /// 配置来源。
    enum class ConfigSource {
        JsonFile,       ///< 模式一：JSON + FTP
        PostgreSQL      ///< 模式二：PG + HTTP
    };
    Q_ENUM(ConfigSource)

    /// 解析模式一的默认配置文件路径。
    static QString defaultJsonConfigPath();
    /// 解析模式二的默认连接串（与 PgConnectionInfo 默认值一致）。
    static QString defaultPgConnectionString();

    explicit ConfigManager(QObject *parent = nullptr);

    // ---------------- 模式一：JSON ----------------

    /// 加载 JSON 配置文件；失败时保留上一次成功加载的数据。
    bool loadConfig(const QString &filePath);
    /// 同 loadConfig，语义更明确的别名。
    bool loadFromJson(const QString &filePath) { return loadConfig(filePath); }

    // ---------------- 模式二：PostgreSQL ----------------

    /// 用连接串（URI / key=value / host:port,user,password[,dbname]）加载。
    bool loadFromPostgres(const QString &connectionString);
    /// 用已解析的连接参数加载。
    bool loadFromPostgres(const PgConnectionInfo &connection);

    // ---------------- 模式切换 ----------------

    /**
     * @brief 切换（或重新加载）配置来源。
     *
     * @param source       目标来源。
     * @param fromJsonFile 第二参数的含义随 source 变化：
     *                     - JsonFile    ：显式 JSON 配置文件路径；空则用
     *                                     defaultJsonConfigPath()
     *                     - PostgreSQL  ：连接串（URI / key=value / 逗号简写）；
     *                                     空则读 config/config_pg.json，
     *                                     再空则用 m_pgConnection 当前值
     *                                     （默认 192.168.1.1:5432）
     *                     参数名保留历史写法以兼容既有调用点。
     *
     * @return 加载成功返回 true；失败返回 false 时 lastError() 给出中文原因，
     *         且 currentSource() 与已加载数据都保持在调用前的状态，
     *         不会出现「来源标记已改、数据还是旧的」这种半切换状态。
     */
    bool setSource(ConfigSource source, const QString &fromJsonFile = QString());

    ConfigSource currentSource() const { return m_source; }
    /// 当前来源的中文描述，直接用于状态栏/日志。
    QString sourceDescription() const;

    /// 模式二下安装器读取的 HTTP 下载基础地址（czl_settings）。
    QString httpBaseUrl() const { return m_httpBaseUrl; }

    /// 模式二当前使用的连接参数。
    PgConnectionInfo pgConnection() const { return m_pgConnection; }
    /// 模式二当前使用的连接串。
    QString pgConnectionString() const { return m_pgConnectionString; }

    // ---------------- 通用访问 ----------------

    /// 解析相对路径：相对 applicationDirPath()，绝对路径原样返回。
    static QString resolvePath(const QString &path);

    const QList<ServerConfig> &servers() const { return m_servers; }
    const QList<Category>     &categories() const { return m_categories; }
    const QList<BookmarkItem> &bookmarks() const { return m_bookmarks; }

    QString localBaseDir() const { return m_localBaseDir; }
    QString configFilePath() const { return m_configFilePath; }
    QString lastError() const { return m_lastError; }

    /// 加载过程中的非致命提示（例如数据库里引用了不存在的分类）。
    QStringList warnings() const { return m_warnings; }

    /// 按 id 递归查找安装项，找到返回 true 并写入 out。
    bool findItem(const QString &id, InstallItem &out) const;

    /// 按 id 递归查找类别。
    bool findCategory(const QString &id, Category &out) const;

signals:
    void configChanged();

private:
    // ---- JSON 解析 ----
    void parseServer(const QJsonObject &o, ServerConfig &s);
    void parseCategory(const QJsonObject &o, Category &cat);
    void parseInstallItem(const QJsonObject &o, InstallItem &item);
    void parseCopySource(const QJsonObject &o, CopySource &src);
    void parseBookmark(const QJsonObject &o, BookmarkItem &b);

    /// 把 JSON 里的字符串或字符串数组统一读成 QStringList。
    static QStringList readStringList(const QJsonObject &o, const QString &key);

    /// 解析 installArgs：既支持 "a b c" 字符串，也支持 ["a","b c"] 数组，
    /// 并且尊重引号（旧版按空格硬切，"/configure config.xml" 会被切成两项）。
    static QStringList parseArgs(const QJsonObject &o);

    /// 读取模式一的连接参数（配置文件同目录的 config_pg.json）。
    bool loadPgSettingsFromJson(const QString &filePath, QString &connectionOut);

    /// 提交一次成功加载的结果，并发出 configChanged()。
    void commit(bool fromDatabase,
                QList<ServerConfig> servers,
                QList<Category> categories,
                QList<BookmarkItem> bookmarks,
                const QString &localBaseDir,
                const QString &httpBaseUrl,
                const QString &origin,
                const QStringList &warnings);

    QList<ServerConfig> m_servers;
    QList<Category>     m_categories;
    QList<BookmarkItem> m_bookmarks;

    QString m_localBaseDir = QStringLiteral("D:/c_temp");
    QString m_configFilePath;
    QString m_lastError;
    QStringList m_warnings;

    ConfigSource    m_source = ConfigSource::JsonFile;
    PgConnectionInfo m_pgConnection;
    QString         m_pgConnectionString;
    QString         m_httpBaseUrl;
};

#endif // CONFIGMANAGER_H
