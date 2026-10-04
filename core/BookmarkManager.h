#ifndef BOOKMARKMANAGER_H
#define BOOKMARKMANAGER_H

#include <QObject>
#include <QStringList>
#include "ConfigManager.h"

class BookmarkManager : public QObject
{
    Q_OBJECT
public:
    explicit BookmarkManager(QObject *parent = nullptr);

    // 处理结果
    struct Result {
        bool        ok        = false;
        int         added     = 0;
        int         skipped   = 0;
        QStringList browsers;         // 成功处理的浏览器名
        QStringList failed;           // 处理失败的浏览器名
        QString     message;          // 汇总消息
    };

    // 对所有检测到的浏览器应用书签
    Result applyToAll(const QList<BookmarkItem> &items);

    // 检测系统里安装了哪些 Chromium 系浏览器
    static QStringList detectBrowsers();

signals:
    void logMessage(const QString &msg);

private:
    // 单个浏览器处理
    struct BrowserTarget {
        QString key;            // 内部标识
        QString displayName;    // "Chrome" / "Edge" / ...
        QString killProcess;    // "chrome.exe"
        QString bookmarksPath;  // 完整路径
    };

    Result processOne(const BrowserTarget &target,
                      const QList<BookmarkItem> &items);

    // 工具函数
    bool     urlExistsInAll(const QJsonObject &roots, const QString &url);
    void     collectUrls(const QJsonObject &node, QSet<QString> &urls);
    QJsonObject createBookmarkNode(const BookmarkItem &item);
    qint64   findMaxId(const QJsonObject &node, qint64 current) const;
    static QString generateGuid();
    static qint64  chromiumNow();
    static QString normalizeUrl(const QString &url);

    QStringList m_searchedPaths;   // 供日志输出
};

#endif
