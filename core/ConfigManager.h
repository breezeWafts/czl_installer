#ifndef CONFIGMANAGER_H
#define CONFIGMANAGER_H

#include <QObject>
#include <QList>
#include <QString>
#include <QStringList>

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
 * @brief 负责加载 config_*.json，并向 UI / 安装线程提供只读快照。
 *
 * 线程约定：只在 GUI 线程创建与调用。安装线程拿到的是值拷贝
 * （QList<ServerConfig> / InstallItem），不存在共享可变状态。
 */
class ConfigManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString localBaseDir READ localBaseDir NOTIFY configChanged)

public:
    explicit ConfigManager(QObject *parent = nullptr);

    /// 加载配置文件；失败时保留上一次成功加载的数据。
    bool loadConfig(const QString &filePath);

    /// 解析相对路径：相对 applicationDirPath()，绝对路径原样返回。
    static QString resolvePath(const QString &path);

    const QList<ServerConfig> &servers() const { return m_servers; }
    const QList<Category>     &categories() const { return m_categories; }
    const QList<BookmarkItem> &bookmarks() const { return m_bookmarks; }

    QString localBaseDir() const { return m_localBaseDir; }
    QString configFilePath() const { return m_configFilePath; }
    QString lastError() const { return m_lastError; }

    /// 按 id 递归查找安装项，找到返回 true 并写入 out。
    bool findItem(const QString &id, InstallItem &out) const;

    /// 按 id 递归查找类别。
    bool findCategory(const QString &id, Category &out) const;

signals:
    void configChanged();

private:
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

    QList<ServerConfig> m_servers;
    QList<Category>     m_categories;
    QList<BookmarkItem> m_bookmarks;

    QString m_localBaseDir = QStringLiteral("D:/c_temp");
    QString m_configFilePath;
    QString m_lastError;
};

#endif // CONFIGMANAGER_H
