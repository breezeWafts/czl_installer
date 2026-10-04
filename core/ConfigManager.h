#ifndef CONFIGMANAGER_H
#define CONFIGMANAGER_H

#include <QObject>
#include <QJsonObject>
#include <QJsonArray>
#include <QList>
#include <QStringList>

// ---------- 服务器 ----------
struct ServerConfig {
    QString name;               // 显示名，如 "ftp1-主用"
    QString type;               // "ftp" | "http"
    QString host;
    int     port = 21;
    QString username;
    QString password;
    QString basePath;           // FTP 远程根目录（以 / 结尾）
    QString baseUrl;            // HTTP 基础 URL（以 / 结尾）
    int     connectTimeout = 5000;
};

// ---------- 拷贝源 ----------
struct CopySource {
    QString kind;               // "file" | "dir" | "zip"
    QString remotePath;
    QString localPath;
    QStringList files;          // kind=dir 时使用
    QString targetPath;         // 可空，空则用 InstallItem.targetPath
};

// ---------- 待安装项 ----------
struct InstallItem {
    QString id;
    QString name;
    QString type;               // msi | exe | bat | copy_only | copy_and_register
    QString description;
    QString version;
    QString publisher;

    // exe / msi / bat 用
    QString remotePath;
    QString localTempPath;
    QStringList installArgs;
    QStringList files;          // 兼容旧的目录单源

    // copy 类型用
    QString targetPath;
    bool overwrite = true;
    QList<CopySource> sources;
    QStringList registerCommands;

    // 前置清理
    QStringList killProcesses;
    QStringList killServices;
};

// ---------- 类别（递归） ----------
struct Category {
    QString id;
    QString name;
    QList<Category> children;
    QList<InstallItem> items;
};

// ---------- ★ 新增：书签项 ----------
struct BookmarkItem {
    QString name;
    QString url;
};

// ---------- 管理器 ----------
class ConfigManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString localBaseDir READ localBaseDir NOTIFY configChanged)
public:
    explicit ConfigManager(QObject *parent = nullptr);

    bool loadConfig(const QString &filePath);

    QList<ServerConfig> servers() const { return m_servers; }
    QString localBaseDir() const { return m_localBaseDir; }
    QList<Category> categories() const { return m_categories; }
    // ★ 新增
    QList<BookmarkItem> bookmarks() const { return m_bookmarks; }
    bool findItem(const QString &id, InstallItem &out) const;

signals:
    void configChanged();

private:
    void parseServer(const QJsonObject &o, ServerConfig &s);
    void parseCategory(const QJsonObject &o, Category &cat);
    void parseInstallItem(const QJsonObject &o, InstallItem &item);
    void parseCopySource(const QJsonObject &o, CopySource &src);
    void parseBookmark(const QJsonObject &o, BookmarkItem &b);

    QList<ServerConfig> m_servers;
    QString m_localBaseDir = "D:/c_temp";
    QList<Category> m_categories;
    QList<BookmarkItem> m_bookmarks;   // ★ 新增
};

#endif
