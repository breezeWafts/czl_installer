#include "ConfigManager.h"
#include <QFile>
#include <QDir>
#include <QJsonDocument>
#include <QDebug>

ConfigManager::ConfigManager(QObject *parent) : QObject(parent) {}

bool ConfigManager::loadConfig(const QString &filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "无法打开配置文件:" << filePath;
        return false;
    }
    QByteArray data = file.readAll();
    file.close();

    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(data, &err);
    if (err.error != QJsonParseError::NoError) {
        qWarning() << "JSON 解析错误:" << err.errorString();
        return false;
    }
    QJsonObject root = doc.object();

    // ---- servers ----
    m_servers.clear();
    if (root.contains("servers") && root["servers"].isArray()) {
        for (const auto &v : root["servers"].toArray()) {
            ServerConfig s;
            parseServer(v.toObject(), s);
            m_servers.append(s);
        }
    } else if (root.contains("server") && root["server"].isObject()) {
        // 兼容旧格式
        ServerConfig s;
        parseServer(root["server"].toObject(), s);
        m_servers.append(s);
    }
    if (m_servers.isEmpty()) {
        qWarning() << "配置文件中没有可用的服务器";
        return false;
    }

    // ---- 本地缓存目录 ----
    if (root.contains("localBaseDir"))
        m_localBaseDir = root["localBaseDir"].toString();

    // ---- 类别树 ----
    m_categories.clear();
    for (const auto &cv : root["categories"].toArray()) {
        Category cat;
        parseCategory(cv.toObject(), cat);
        m_categories.append(cat);
    }
    // ---- ★ 书签列表 ----
    m_bookmarks.clear();
    if (root.contains("bookmarks")) {
        for (const auto &bv : root["bookmarks"].toArray()) {
            BookmarkItem b;
            parseBookmark(bv.toObject(), b);
            if (!b.url.isEmpty())
                m_bookmarks.append(b);
        }
    }

    qDebug() << "配置加载完成:"
             << "localBaseDir=" << m_localBaseDir
             << "servers=" << m_servers.size()
             << "categories=" << m_categories.size()
             << "bookmarks=" << m_bookmarks.size();

    emit configChanged();
    return true;
}

void ConfigManager::parseServer(const QJsonObject &o, ServerConfig &s)
{
    s.name           = o.value("name").toString();
    s.type           = o.value("type").toString().toLower();
    s.host           = o.value("host").toString();
    s.port           = o.value("port").toInt(21);
    s.username       = o.value("username").toString();
    s.password       = o.value("password").toString();
    s.basePath       = o.value("basePath").toString();
    s.baseUrl        = o.value("baseUrl").toString();
    s.connectTimeout = o.value("connectTimeout").toInt(5000);

    if (s.name.isEmpty())
        s.name = s.type + "://" + (s.type == "http" ? s.baseUrl : s.host);

    if (s.type == "ftp" && !s.basePath.endsWith('/'))
        s.basePath += '/';
    if (s.type == "http" && !s.baseUrl.endsWith('/'))
        s.baseUrl += '/';
}

void ConfigManager::parseCopySource(const QJsonObject &o, CopySource &src)
{
    src.kind       = o.value("kind").toString().toLower();
    src.remotePath = o.value("remotePath").toString();
    src.localPath  = o.value("localPath").toString();
    src.targetPath = o.value("targetPath").toString();

    for (const auto &f : o.value("files").toArray())
        src.files.append(f.toString());

    if (src.kind.isEmpty()) {
        if (src.remotePath.endsWith('/') || src.remotePath.endsWith('\\'))
            src.kind = "dir";
        else if (src.remotePath.endsWith(".zip", Qt::CaseInsensitive))
            src.kind = "zip";
        else
            src.kind = "file";
    }
}

void ConfigManager::parseInstallItem(const QJsonObject &o, InstallItem &item)
{
    item.id            = o.value("id").toString();
    item.name          = o.value("name").toString();
    item.type          = o.value("type").toString().toLower();
    item.description   = o.value("description").toString();
    item.version       = o.value("version").toString();
    item.publisher     = o.value("publisher").toString();
    item.remotePath    = o.value("remotePath").toString();
    item.localTempPath = o.value("localTempPath").toString();
    item.targetPath    = o.value("targetPath").toString();
    item.overwrite     = o.value("overwrite").toBool(true);

    if (o.contains("installArgs")) {
        QString a = o.value("installArgs").toString();
        if (!a.isEmpty()) {
            QStringList raw = a.split(' ');
            for (const QString &arg : raw) {
                if (!arg.trimmed().isEmpty())
                    item.installArgs.append(arg);
            }
        }
    }
    for (const auto &f : o.value("files").toArray())
        item.files.append(f.toString());
    for (const auto &c : o.value("registerCommands").toArray())
        item.registerCommands.append(c.toString());
    for (const auto &p : o.value("killProcesses").toArray())
        item.killProcesses.append(p.toString());
    for (const auto &s : o.value("killServices").toArray())
        item.killServices.append(s.toString());

    // ---- sources ----
    item.sources.clear();
    if (o.contains("sources") && o["sources"].isArray()) {
        for (const auto &sv : o["sources"].toArray()) {
            CopySource s;
            parseCopySource(sv.toObject(), s);
            item.sources.append(s);
        }
    }

    // 兼容旧的单源写法
    if (item.sources.isEmpty() && !item.remotePath.isEmpty()
        && (item.type == "copy_only" || item.type == "copy_and_register")) {
        CopySource s;
        s.remotePath = item.remotePath;
        s.localPath  = item.localTempPath;
        s.files      = item.files;
        s.targetPath = item.targetPath;
        if (item.remotePath.endsWith('/') || item.remotePath.endsWith('\\'))
            s.kind = "dir";
        else if (item.remotePath.endsWith(".zip", Qt::CaseInsensitive))
            s.kind = "zip";
        else
            s.kind = "file";
        item.sources.append(s);
    }
}

void ConfigManager::parseCategory(const QJsonObject &o, Category &cat)
{
    cat.id   = o.value("id").toString();
    cat.name = o.value("name").toString();

    if (o.contains("children")) {
        for (const auto &cv : o["children"].toArray()) {
            Category child;
            parseCategory(cv.toObject(), child);
            cat.children.append(child);
        }
    }
    if (o.contains("items")) {
        for (const auto &iv : o["items"].toArray()) {
            InstallItem it;
            parseInstallItem(iv.toObject(), it);
            cat.items.append(it);
        }
    }
}

bool ConfigManager::findItem(const QString &id, InstallItem &out) const
{
    std::function<bool(const QList<Category>&)> walk =
        [&](const QList<Category> &cats) -> bool {
        for (const auto &c : cats) {
            for (const auto &i : c.items) {
                if (i.id == id) { out = i; return true; }
            }
            if (walk(c.children)) return true;
        }
        return false;
    };
    return walk(m_categories);
}

void ConfigManager::parseBookmark(const QJsonObject &o, BookmarkItem &b)
{
    b.name = o.value("name").toString().trimmed();
    b.url  = o.value("url").toString().trimmed();

    // 名字为空时用 URL 兜底
    if (b.name.isEmpty() && !b.url.isEmpty())
        b.name = b.url;
}
