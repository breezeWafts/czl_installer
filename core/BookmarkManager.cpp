#include "BookmarkManager.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QSet>
#include <QThread>
#include <QUuid>
#include <QDebug>

BookmarkManager::BookmarkManager(QObject *parent) : QObject(parent) {}

// ============================================================
// 检测安装的浏览器
// ============================================================
QStringList BookmarkManager::detectBrowsers()
{
    QString lad = QString::fromLocal8Bit(qgetenv("LOCALAPPDATA"));
    if (lad.isEmpty()) return {};

    struct Candidate {
        const char *name;
        const char *relative;   // 相对 LOCALAPPDATA 的路径
    };

    static const Candidate kCandidates[] = {
        { "Chrome",    "/Google/Chrome/User Data/Default/Bookmarks" },
        { "Edge",      "/Microsoft/Edge/User Data/Default/Bookmarks" },
        { "Brave",     "/BraveSoftware/Brave-Browser/User Data/Default/Bookmarks" },
        { "Vivaldi",   "/Vivaldi/User Data/Default/Bookmarks" },
        { "Opera",     "/Programs/Opera/User Data/Default/Bookmarks" },
        { "360Chrome", "/360Chrome/Chrome/User Data/Default/Bookmarks" },
        { "QQBrowser", "/Tencent/QQBrowser/User Data/Default/Bookmarks" },
    };

    QStringList found;
    for (const auto &c : kCandidates) {
        QString path = lad + QString::fromLatin1(c.relative);
        if (QFile::exists(path))
            found << QString::fromLatin1(c.name);
    }
    return found;
}

// ============================================================
// 对所有浏览器应用书签
// ============================================================
BookmarkManager::Result BookmarkManager::applyToAll(const QList<BookmarkItem> &items)
{
    Result total;

    if (items.isEmpty()) {
        total.message = "配置文件中没有书签，跳过";
        return total;
    }

    QString lad = QString::fromLocal8Bit(qgetenv("LOCALAPPDATA"));
    if (lad.isEmpty()) {
        total.message = "无法获取 LOCALAPPDATA 路径";
        return total;
    }

    // 构造浏览器候选（key、显示名、进程名、书签文件路径）
    QList<BrowserTarget> targets = {
        { "chrome",  "Chrome",  "chrome.exe",
          lad + "/Google/Chrome/User Data/Default/Bookmarks" },
        { "edge",    "Edge",    "msedge.exe",
          lad + "/Microsoft/Edge/User Data/Default/Bookmarks" },
        { "brave",   "Brave",   "brave.exe",
          lad + "/BraveSoftware/Brave-Browser/User Data/Default/Bookmarks" },
        { "vivaldi", "Vivaldi", "vivaldi.exe",
          lad + "/Vivaldi/User Data/Default/Bookmarks" },
        { "opera",   "Opera",   "opera.exe",
          lad + "/Programs/Opera/User Data/Default/Bookmarks" },
        { "360",     "360Chrome", "360chrome.exe",
          lad + "/360Chrome/Chrome/User Data/Default/Bookmarks" },
        { "qq",      "QQBrowser", "QQBrowser.exe",
          lad + "/Tencent/QQBrowser/User Data/Default/Bookmarks" },
    };

    int foundCount = 0;
    for (const auto &t : targets) {
        if (!QFile::exists(t.bookmarksPath)) continue;
        foundCount++;

        emit logMessage(QString("========== 处理 %1 ==========").arg(t.displayName));
        Result r = processOne(t, items);

        if (r.ok) {
            total.browsers << t.displayName;
            total.added   += r.added;
            total.skipped += r.skipped;
        } else {
            total.failed << t.displayName;
            emit logMessage(QString("  %1 处理失败: %2").arg(t.displayName, r.message));
        }
    }

    total.ok = (foundCount > 0 && total.failed.isEmpty());
    if (foundCount == 0) {
        total.message = "未检测到任何受支持的浏览器";
    } else {
        total.message = QString("共处理 %1 个浏览器：新增 %2 条，跳过 %3 条")
                            .arg(total.browsers.size())
                            .arg(total.added)
                            .arg(total.skipped);
        if (!total.failed.isEmpty())
            total.message += "；失败: " + total.failed.join(", ");
    }
    return total;
}

// ============================================================
// 单个浏览器处理
// ============================================================
BookmarkManager::Result BookmarkManager::processOne(const BrowserTarget &target,
                                                     const QList<BookmarkItem> &items)
{
    Result r;
    r.ok = false;

    // ---------- 1. 关闭浏览器 ----------
    emit logMessage(QString("  关闭 %1 ...").arg(target.killProcess));
    QProcess kill;
    kill.start("taskkill",
               QStringList() << "/F" << "/IM" << target.killProcess);
    kill.waitForFinished(8000);
    QThread::msleep(1200);   // 等待文件句柄释放

    // ---------- 2. 读取书签文件 ----------
    QFile in(target.bookmarksPath);
    if (!in.open(QIODevice::ReadOnly)) {
        r.message = "无法读取: " + target.bookmarksPath;
        return r;
    }
    QByteArray data = in.readAll();
    in.close();

    // ---------- 3. 备份 ----------
    QString backupPath = target.bookmarksPath + ".czl_bak";
    QFile::remove(backupPath);
    QFile::copy(target.bookmarksPath, backupPath);
    emit logMessage("  已备份原书签: " + backupPath);

    // ---------- 4. 解析 JSON ----------
    QJsonParseError perr;
    QJsonDocument doc = QJsonDocument::fromJson(data, &perr);
    if (perr.error != QJsonParseError::NoError) {
        r.message = "JSON 解析失败: " + perr.errorString();
        return r;
    }
    QJsonObject root = doc.object();

    if (!root.contains("roots")) {
        r.message = "书签文件缺少 roots 节点";
        return r;
    }
    QJsonObject roots = root["roots"].toObject();
    if (!roots.contains("bookmark_bar")) {
        r.message = "书签文件缺少 bookmark_bar 节点";
        return r;
    }

    // ---------- 5. 收集所有已存在的 URL ----------
    QSet<QString> existingUrls;
    for (const QString &rootKey : { QString("bookmark_bar"),
                                    QString("other"),
                                    QString("synced") }) {
        if (roots.contains(rootKey)) {
            collectUrls(roots[rootKey].toObject(), existingUrls);
        }
    }
    emit logMessage(QString("  已存在书签数量: %1").arg(existingUrls.size()));

    // ---------- 6. 计算当前最大 id ----------
    qint64 maxId = 0;
    for (const QString &rootKey : { QString("bookmark_bar"),
                                    QString("other"),
                                    QString("synced") }) {
        if (roots.contains(rootKey)) {
            maxId = findMaxId(roots[rootKey].toObject(), maxId);
        }
    }

    // ---------- 7. 逐项处理 ----------
    QJsonArray newItems;
    int added = 0, skipped = 0;

    for (const auto &item : items) {
        QString key = normalizeUrl(item.url);
        if (key.isEmpty()) continue;

        if (existingUrls.contains(key)) {
            skipped++;
            emit logMessage(QString("  跳过（已存在）: %1").arg(item.name));
            continue;
        }

        QJsonObject node = createBookmarkNode(item);
        node["id"] = QString::number(++maxId);
        newItems.append(node);
        existingUrls.insert(key);   // 防止同一次配置里重复
        added++;
        emit logMessage(QString("  新增: %1").arg(item.name));
    }

    // ---------- 8. 把新书签放在最前 ----------
    QJsonObject bar = roots["bookmark_bar"].toObject();
    QJsonArray oldChildren = bar["children"].toArray();

    QJsonArray finalChildren;
    // 先放新书签（保持 JSON 配置顺序）
    for (const auto &ni : newItems)
        finalChildren.append(ni);
    // 再放原书签
    for (const auto &oc : oldChildren)
        finalChildren.append(oc);

    bar["children"]  = finalChildren;
    bar["date_modified"] = QString::number(chromiumNow());
    roots["bookmark_bar"] = bar;
    root["roots"] = roots;

    // ---------- 9. 删除 checksum，让 Chromium 重新计算 ----------
    root.remove("checksum");

    // ---------- 10. 写回文件 ----------
    QFile out(target.bookmarksPath);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        r.message = "无法写入: " + target.bookmarksPath;
        return r;
    }
    QByteArray outData = QJsonDocument(root).toJson(QJsonDocument::Indented);
    out.write(outData);
    out.close();

    emit logMessage(QString("  完成: 新增 %1，跳过 %2")
                        .arg(added).arg(skipped));

    r.ok      = true;
    r.added   = added;
    r.skipped = skipped;
    r.browsers << target.displayName;
    r.message = QString("%1: 新增 %2，跳过 %3")
                    .arg(target.displayName).arg(added).arg(skipped);
    return r;
}

// ============================================================
// 工具函数
// ============================================================
void BookmarkManager::collectUrls(const QJsonObject &node, QSet<QString> &urls)
{
    if (node["type"].toString() == "url") {
        QString u = normalizeUrl(node["url"].toString());
        if (!u.isEmpty()) urls.insert(u);
    }
    if (node.contains("children")) {
        const QJsonArray arr = node["children"].toArray();
        for (const auto &c : arr) {
            collectUrls(c.toObject(), urls);
        }
    }
}

bool BookmarkManager::urlExistsInAll(const QJsonObject &roots, const QString &url)
{
    QSet<QString> urls;
    for (const QString &k : { QString("bookmark_bar"),
                              QString("other"),
                              QString("synced") }) {
        if (roots.contains(k))
            collectUrls(roots[k].toObject(), urls);
    }
    return urls.contains(normalizeUrl(url));
}

qint64 BookmarkManager::findMaxId(const QJsonObject &node, qint64 current) const
{
    bool ok = false;
    qint64 id = node["id"].toString().toLongLong(&ok);
    if (ok && id > current) current = id;

    if (node.contains("children")) {
        for (const auto &c : node["children"].toArray()) {
            current = findMaxId(c.toObject(), current);
        }
    }
    return current;
}

QJsonObject BookmarkManager::createBookmarkNode(const BookmarkItem &item)
{
    QJsonObject o;
    o["date_added"]     = QString::number(chromiumNow());
    o["date_last_used"] = "0";
    o["guid"]           = generateGuid();
    o["id"]             = "0";   // 由调用方赋值
    o["name"]           = item.name.isEmpty() ? item.url : item.name;
    o["type"]           = "url";
    o["url"]            = item.url;
    return o;
}

QString BookmarkManager::generateGuid()
{
    QUuid u = QUuid::createUuid();
    return u.toString(QUuid::WithoutBraces).toLower();
}

qint64 BookmarkManager::chromiumNow()
{
    // Chromium 时间戳：自 1601-01-01 起的微秒数
    const qint64 kEpochOffset = 11644473600000000LL;
    return QDateTime::currentMSecsSinceEpoch() * 1000LL + kEpochOffset;
}

QString BookmarkManager::normalizeUrl(const QString &url)
{
    QString u = url.trimmed();
    // 去掉末尾斜杠（忽略根目录差异）
    while (u.endsWith('/') && u.length() > 8) u.chop(1);
    return u.toLower();
}
