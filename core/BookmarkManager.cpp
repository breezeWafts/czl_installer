#include "BookmarkManager.h"

#include "Logger.h"
#include "ProcessManager.h"

#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QProcess>
#include <QThread>
#include <QUuid>

// ===========================================================================
// 浏览器候选
// ===========================================================================
namespace {

/// 一个浏览器产品的静态描述。
struct BrowserProduct {
    const char *key;
    const char *displayName;
    const char *processName;
    const char *relativePath;   ///< 相对 LOCALAPPDATA 的 User Data 目录
};

/// 唯一一份候选列表（旧版在 detectBrowsers 与 applyToAll 里各写了一份）。
const BrowserProduct kProducts[] = {
    { "chrome",  "Chrome",    "chrome.exe",
      "/Google/Chrome/User Data" },
    { "edge",    "Edge",      "msedge.exe",
      "/Microsoft/Edge/User Data" },
    { "brave",   "Brave",     "brave.exe",
      "/BraveSoftware/Brave-Browser/User Data" },
    { "vivaldi", "Vivaldi",   "vivaldi.exe",
      "/Vivaldi/User Data" },
    { "opera",   "Opera",     "opera.exe",
      "/Programs/Opera/User Data" },
    { "360",     "360Chrome", "360chrome.exe",
      "/360Chrome/Chrome/User Data" },
    { "qq",      "QQBrowser", "QQBrowser.exe",
      "/Tencent/QQBrowser/User Data" },
};

/// 在 User Data 下找出所有含 Bookmarks 文件的配置目录。
QStringList bookmarkFilesIn(const QString &userDataDir)
{
    QStringList result;

    const QString defaultFile = userDataDir + QStringLiteral("/Default/Bookmarks");
    if (QFile::exists(defaultFile))
        result << defaultFile;

    const QDir dir(userDataDir);
    if (!dir.exists())
        return result;

    // Profile 1 / Profile 2 ...（Chromium 的多用户配置目录命名）
    const QStringList entries =
        dir.entryList(QStringList{QStringLiteral("Profile *")}, QDir::Dirs);
    for (const QString &entry : entries) {
        const QString candidate =
            userDataDir + QLatin1Char('/') + entry + QStringLiteral("/Bookmarks");
        if (QFile::exists(candidate))
            result << candidate;
    }

    result.removeDuplicates();
    return result;
}

} // namespace

BookmarkManager::BookmarkManager(QObject *parent)
    : QObject(parent)
{
}

QString BookmarkManager::localAppData()
{
    return qEnvironmentVariable("LOCALAPPDATA");
}

QList<BookmarkManager::BrowserTarget> BookmarkManager::buildTargets()
{
    QList<BrowserTarget> targets;

    const QString lad = localAppData();
    if (lad.isEmpty())
        return targets;

    for (const BrowserProduct &product : kProducts) {
        const QString userData =
            QDir::toNativeSeparators(lad + QString::fromLatin1(product.relativePath));

        const QStringList files = bookmarkFilesIn(userData);
        for (const QString &file : files) {
            BrowserTarget target;
            target.key           = QString::fromLatin1(product.key);
            target.displayName   = QString::fromLatin1(product.displayName);
            target.processName   = QString::fromLatin1(product.processName);
            target.bookmarksPath = file;
            targets.append(target);
        }
    }

    return targets;
}

QStringList BookmarkManager::detectBrowsers()
{
    QStringList found;
    const QList<BrowserTarget> targets = buildTargets();
    for (const BrowserTarget &target : targets) {
        if (!found.contains(target.displayName))
            found << target.displayName;
    }
    return found;
}

// ===========================================================================
// 批量应用
// ===========================================================================
BookmarkManager::Result BookmarkManager::applyToAll(const QList<BookmarkItem> &items)
{
    Result total;

    if (items.isEmpty()) {
        total.message = QStringLiteral("配置文件中没有书签，跳过");
        return total;
    }

    const QList<BrowserTarget> targets = buildTargets();
    if (targets.isEmpty()) {
        total.message = QStringLiteral("未检测到任何受支持的浏览器");
        return total;
    }

    // 浏览器数量按「去重后的显示名」统计，而不是配置文件个数
    QSet<QString> foundBrowsers;
    for (const BrowserTarget &target : targets)
        foundBrowsers.insert(target.displayName);
    total.found = static_cast<int>(foundBrowsers.size());

    for (const BrowserTarget &target : targets) {
        emit logMessage(QStringLiteral("========== 处理 %1 (%2) ==========")
                            .arg(target.displayName, QFileInfo(target.bookmarksPath).fileName()));

        const Result one = processOne(target, items);

        if (one.ok) {
            if (!total.browsers.contains(target.displayName))
                total.browsers << target.displayName;
            total.added   += one.added;
            total.skipped += one.skipped;
        } else {
            if (!total.failed.contains(target.displayName))
                total.failed << target.displayName;
            emit logMessage(QStringLiteral("  %1 处理失败: %2")
                                .arg(target.displayName, one.message));
        }
    }

    // ok 表示「至少有一个浏览器写入成功」，避免全部失败时仍报成功
    total.ok = !total.browsers.isEmpty();

    total.message = QStringLiteral("检测到 %1 个浏览器，成功 %2 个：新增 %3 条，跳过 %4 条")
                        .arg(total.found)
                        .arg(total.browsers.size())
                        .arg(total.added)
                        .arg(total.skipped);
    if (!total.failed.isEmpty())
        total.message += QStringLiteral("；失败: %1").arg(total.failed.join(QStringLiteral(", ")));

    return total;
}

// ===========================================================================
// 单个目标处理
// ===========================================================================
BookmarkManager::Result BookmarkManager::processOne(const BrowserTarget &target,
                                                    const QList<BookmarkItem> &items)
{
    Result result;

    // ---------- 1. 关闭浏览器 ----------
    if (ProcessManager::isProcessRunning(target.processName)) {
        emit logMessage(QStringLiteral("  正在关闭 %1 ...").arg(target.processName));

        QProcess kill;
        kill.start(QStringLiteral("taskkill"),
                   {QStringLiteral("/F"), QStringLiteral("/IM"), target.processName});
        kill.waitForFinished(10000);

        // 旧版固定 msleep(1200) 靠猜；这里轮询确认进程真的退出，
        // 否则书签文件仍被占用，写入必然失败。
        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < 8000
               && ProcessManager::isProcessRunning(target.processName)) {
            QThread::msleep(250);
        }

        if (ProcessManager::isProcessRunning(target.processName)) {
            result.message = QStringLiteral("%1 无法关闭，书签文件可能被占用")
                                 .arg(target.processName);
            return result;
        }
    }

    // ---------- 2. 读取 ----------
    QFile input(target.bookmarksPath);
    if (!input.open(QIODevice::ReadOnly)) {
        result.message = QStringLiteral("无法读取: %1 (%2)")
                             .arg(target.bookmarksPath, input.errorString());
        return result;
    }
    const QByteArray data = input.readAll();
    input.close();

    // ---------- 3. 备份（带时间戳，避免覆盖上一次的备份） ----------
    const QString stamp =
        QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss"));
    const QString backupPath = QStringLiteral("%1.%2.czl_bak")
                                   .arg(target.bookmarksPath, stamp);
    if (!QFile::copy(target.bookmarksPath, backupPath)) {
        emit logMessage(QStringLiteral("  警告：备份失败（继续处理）: %1").arg(backupPath));
    } else {
        emit logMessage(QStringLiteral("  已备份原书签: %1").arg(backupPath));
    }

    // ---------- 4. 解析 ----------
    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        result.message = QStringLiteral("JSON 解析失败: %1").arg(parseError.errorString());
        return result;
    }
    if (!document.isObject()) {
        result.message = QStringLiteral("书签文件根节点不是 JSON 对象");
        return result;
    }

    QJsonObject root = document.object();
    if (!root.contains(QStringLiteral("roots"))) {
        result.message = QStringLiteral("书签文件缺少 roots 节点");
        return result;
    }

    QJsonObject roots = root.value(QStringLiteral("roots")).toObject();
    if (!roots.contains(QStringLiteral("bookmark_bar"))) {
        result.message = QStringLiteral("书签文件缺少 bookmark_bar 节点");
        return result;
    }

    const QStringList rootKeys{QStringLiteral("bookmark_bar"),
                               QStringLiteral("other"),
                               QStringLiteral("synced")};

    // ---------- 5. 已存在的 URL ----------
    QSet<QString> existingUrls;
    for (const QString &key : rootKeys) {
        if (roots.contains(key))
            collectUrls(roots.value(key).toObject(), existingUrls);
    }
    emit logMessage(QStringLiteral("  已存在书签数量: %1").arg(existingUrls.size()));

    // ---------- 6. 计算当前最大 id ----------
    qint64 maxId = 0;
    for (const QString &key : rootKeys) {
        if (roots.contains(key))
            maxId = findMaxId(roots.value(key).toObject(), maxId);
    }

    // ---------- 7. 生成新书签 ----------
    QJsonArray newItems;
    int added = 0;
    int skipped = 0;

    for (const BookmarkItem &item : items) {
        const QString key = normalizeUrl(item.url);
        if (key.isEmpty())
            continue;

        if (existingUrls.contains(key)) {
            ++skipped;
            emit logMessage(QStringLiteral("  跳过（已存在）: %1").arg(item.name));
            continue;
        }

        QJsonObject node = createBookmarkNode(item);
        node.insert(QStringLiteral("id"), QString::number(++maxId));
        newItems.append(node);
        existingUrls.insert(key);   // 同一次配置里的重复项也只加一次
        ++added;
        emit logMessage(QStringLiteral("  新增: %1").arg(item.name));
    }

    // ---------- 8. 新书签放最前 ----------
    QJsonObject bar = roots.value(QStringLiteral("bookmark_bar")).toObject();
    const QJsonArray oldChildren = bar.value(QStringLiteral("children")).toArray();

    QJsonArray finalChildren;
    for (const QJsonValue &value : newItems)
        finalChildren.append(value);
    for (const QJsonValue &value : oldChildren)
        finalChildren.append(value);

    bar.insert(QStringLiteral("children"), finalChildren);
    bar.insert(QStringLiteral("date_modified"), QString::number(chromiumNow()));
    roots.insert(QStringLiteral("bookmark_bar"), bar);
    root.insert(QStringLiteral("roots"), roots);

    // ---------- 9. 删除 checksum，让 Chromium 重新计算 ----------
    root.remove(QStringLiteral("checksum"));

    // ---------- 10. 写回 ----------
    QFile output(target.bookmarksPath);
    if (!output.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        result.message = QStringLiteral("无法写入: %1 (%2)")
                             .arg(target.bookmarksPath, output.errorString());
        return result;
    }

    const QByteArray payload =
        QJsonDocument(root).toJson(QJsonDocument::Indented);
    if (output.write(payload) != payload.size()) {
        result.message = QStringLiteral("写入不完整: %1").arg(target.bookmarksPath);
        output.close();
        return result;
    }
    output.close();

    emit logMessage(QStringLiteral("  完成: 新增 %1，跳过 %2").arg(added).arg(skipped));

    result.ok      = true;
    result.added   = added;
    result.skipped = skipped;
    result.browsers << target.displayName;
    result.message = QStringLiteral("%1: 新增 %2，跳过 %3")
                         .arg(target.displayName)
                         .arg(added)
                         .arg(skipped);
    return result;
}

// ===========================================================================
// 工具函数
// ===========================================================================
void BookmarkManager::collectUrls(const QJsonObject &node, QSet<QString> &urls)
{
    if (node.value(QStringLiteral("type")).toString() == QLatin1String("url")) {
        const QString url = normalizeUrl(node.value(QStringLiteral("url")).toString());
        if (!url.isEmpty())
            urls.insert(url);
    }

    const QJsonArray children = node.value(QStringLiteral("children")).toArray();
    for (const QJsonValue &child : children)
        collectUrls(child.toObject(), urls);
}

qint64 BookmarkManager::findMaxId(const QJsonObject &node, qint64 current)
{
    bool ok = false;
    const qint64 id = node.value(QStringLiteral("id")).toString().toLongLong(&ok);
    if (ok && id > current)
        current = id;

    const QJsonArray children = node.value(QStringLiteral("children")).toArray();
    for (const QJsonValue &child : children)
        current = findMaxId(child.toObject(), current);

    return current;
}

QJsonObject BookmarkManager::createBookmarkNode(const BookmarkItem &item)
{
    const qint64 now = chromiumNow();

    QJsonObject node;
    node.insert(QStringLiteral("date_added"), QString::number(now));
    node.insert(QStringLiteral("date_last_used"), QStringLiteral("0"));
    node.insert(QStringLiteral("guid"), generateGuid());
    node.insert(QStringLiteral("id"), QStringLiteral("0"));   // 由调用方赋值
    node.insert(QStringLiteral("name"),
                item.name.isEmpty() ? item.url : item.name);
    node.insert(QStringLiteral("type"), QStringLiteral("url"));
    node.insert(QStringLiteral("url"), item.url);
    return node;
}

QString BookmarkManager::generateGuid()
{
    // Chromium 的 guid 是不带花括号的小写 UUID
    return QUuid::createUuid().toString(QUuid::WithoutBraces).toLower();
}

qint64 BookmarkManager::chromiumNow()
{
    // Chromium 时间戳：自 1601-01-01 起的微秒数
    constexpr qint64 kEpochOffset = 11644473600000000LL;
    return QDateTime::currentMSecsSinceEpoch() * 1000LL + kEpochOffset;
}

QString BookmarkManager::normalizeUrl(const QString &url)
{
    QString normalized = url.trimmed();

    // 去掉末尾斜杠（"http://a.com" 与 "http://a.com/" 视为同一书签）
    while (normalized.endsWith(QLatin1Char('/')) && normalized.length() > 8)
        normalized.chop(1);

    return normalized.toLower();
}
