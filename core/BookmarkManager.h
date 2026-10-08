#ifndef BOOKMARKMANAGER_H
#define BOOKMARKMANAGER_H

#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>

#include "ConfigManager.h"

/**
 * @brief 把配置里的书签批量写入 Chromium 系浏览器的书签文件。
 *
 * 相对旧版的修正：
 *   - 只扫描 Default 配置目录，改为扫描全部配置文件（Default / Profile 1 ...），
 *     旧版在没有 Default 目录时会静默跳过整个浏览器；
 *   - 备份文件固定叫 xxx.czL_bak，第二次运行就被覆盖，改为带时间戳；
 *   - 用 taskkill 关浏览器后只 msleep(1200) 猜，改为轮询进程真正退出；
 *   - 检测浏览器的候选列表在 detectBrowsers() 与 applyToAll() 里各写了一份，
 *     容易不同步，现已合并为唯一一份；
 *   - 删除了从未被调用的 urlExistsInAll()（旧版对它内部节点的
 *     type 判断也是恒真的死逻辑）。
 */
class BookmarkManager : public QObject
{
    Q_OBJECT

public:
    explicit BookmarkManager(QObject *parent = nullptr);

    struct Result {
        bool        ok      = false;
        int         found   = 0;     ///< 检测到的浏览器数量
        int         added   = 0;
        int         skipped = 0;
        QStringList browsers;        ///< 成功写入的浏览器名
        QStringList failed;          ///< 处理失败的浏览器名
        QString     message;
    };

    /// 对所有检测到的浏览器应用书签。
    Result applyToAll(const QList<BookmarkItem> &items);

    /// 检测系统里装了哪些 Chromium 系浏览器。
    static QStringList detectBrowsers();

signals:
    void logMessage(const QString &message);

private:
    /// 一个待处理的「浏览器 + 配置文件」组合。
    struct BrowserTarget {
        QString key;            ///< 内部标识，如 "chrome"
        QString displayName;    ///< "Chrome" / "Edge" ...
        QString processName;    ///< "chrome.exe"
        QString bookmarksPath;  ///< 完整书签文件路径
    };

    /// 由 LOCALAPPDATA 展开出所有候选目标（含多配置文件）。
    static QList<BrowserTarget> buildTargets();

    Result processOne(const BrowserTarget &target,
                      const QList<BookmarkItem> &items);

    static void     collectUrls(const QJsonObject &node, QSet<QString> &urls);
    static QJsonObject createBookmarkNode(const BookmarkItem &item);
    static qint64   findMaxId(const QJsonObject &node, qint64 current);
    static QString  generateGuid();
    static qint64   chromiumNow();
    static QString  normalizeUrl(const QString &url);
    static QString  localAppData();
};

#endif // BOOKMARKMANAGER_H
