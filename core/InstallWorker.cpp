#include "InstallWorker.h"

#include "DownloadManager.h"
#include "FileOperationManager.h"
#include "Logger.h"
#include "ProcessManager.h"
#include "TIHelperWrapper.h"

#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>

#include <algorithm>
#include <utility>

// ===========================================================================
// 构造
// ===========================================================================
InstallWorker::InstallWorker(InstallItem item,
                             QList<ServerConfig> servers,
                             int activeServerIndex,
                             QString localBaseDir,
                             bool overwriteDefault,
                             QObject *parent)
    : QObject(parent)
    , m_item(std::move(item))
    , m_servers(std::move(servers))
    , m_activeIndex(activeServerIndex)
    , m_localBaseDir(std::move(localBaseDir))
    , m_overwriteDefault(overwriteDefault)
{
    // 注意：QNetworkAccessManager 必须在 worker 已经位于目标线程之后创建，
    // 否则它的内部 socket 通知会绑在 GUI 线程上。因此这里只记录，不 new。
}

bool InstallWorker::aborted() const
{
    return m_abortRequested.load(std::memory_order_relaxed);
}

void InstallWorker::requestAbort()
{
    m_abortRequested.store(true, std::memory_order_relaxed);
}

void InstallWorker::emitProgress(const QString &stage, int percent)
{
    emit progress(stage, qBound(0, percent, 100));
}

QString InstallWorker::resolveLocal(const QString &relativePath) const
{
    return DownloadManager::resolveLocalPath(m_localBaseDir, relativePath);
}

// ===========================================================================
// 命令行参数引号
// ===========================================================================
QString InstallWorker::quoteArgument(const QString &argument)
{
    // 按 Windows 官方 CommandLineToArgvW 的反向规则加引号：
    //   - 空串 -> ""
    //   - 不含空格/制表符/引号 -> 原样
    //   - 否则用 " 包裹，并把内部的 \ 与 " 按规则转义
    if (argument.isEmpty())
        return QStringLiteral("\"\"");

    const bool needsQuotes = argument.contains(QLatin1Char(' '))
                             || argument.contains(QLatin1Char('\t'))
                             || argument.contains(QLatin1Char('"'));
    if (!needsQuotes)
        return argument;

    QString result;
    result.reserve(argument.size() + 8);
    result += QLatin1Char('"');

    int backslashes = 0;
    for (const QChar ch : argument) {
        if (ch == QLatin1Char('\\')) {
            ++backslashes;
            continue;
        }
        if (ch == QLatin1Char('"')) {
            // 引号前的反斜杠要翻倍，引号本身再加一个反斜杠
            result += QString(backslashes * 2 + 1, QLatin1Char('\\'));
            result += QLatin1Char('"');
            backslashes = 0;
            continue;
        }
        if (backslashes > 0) {
            result += QString(backslashes, QLatin1Char('\\'));
            backslashes = 0;
        }
        result += ch;
    }

    // 结尾的反斜杠必须翻倍，否则会转义掉收尾的引号
    if (backslashes > 0)
        result += QString(backslashes * 2, QLatin1Char('\\'));

    result += QLatin1Char('"');
    return result;
}

QString InstallWorker::joinArguments(const QStringList &arguments)
{
    QStringList parts;
    parts.reserve(arguments.size());
    for (const QString &argument : arguments)
        parts.append(quoteArgument(argument));
    return parts.join(QLatin1Char(' '));
}

// ===========================================================================
// 脚本执行
// ===========================================================================
bool InstallWorker::runScript(const QString &scriptBody, const QString &tag)
{
    if (!TIHelperWrapper::instance().isLoaded()) {
        Logger::error(QStringLiteral("TIHelper.dll 未加载，无法执行脚本: %1").arg(tag));
        return false;
    }

    static std::atomic<quint32> s_sequence{0};
    const QString scriptPath = resolveLocal(
        QStringLiteral("__czl_%1_%2_%3.bat")
            .arg(tag)
            .arg(m_item.id)
            .arg(s_sequence.fetch_add(1)));

    const QString scriptDir = QFileInfo(scriptPath).absolutePath();
    if (!QDir().mkpath(scriptDir)) {
        Logger::error(QStringLiteral("无法创建脚本目录: %1").arg(scriptDir));
        return false;
    }

    QFile file(scriptPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        Logger::error(QStringLiteral("无法创建脚本: %1 (%2)")
                          .arg(scriptPath, file.errorString()));
        return false;
    }

    // ★ 关键修正：cmd.exe 按「系统 ANSI/OEM 代码页」读取 bat 文件内容。
    //   旧版用 QTextStream::setCodec("UTF-8") 写脚本，中文/含空格路径在
    //   中文 Windows 上必然变成乱码，安装静默失败。
    //   正确的做法是用本地 8 位编码写盘。
    QByteArray payload;
    payload += QByteArrayLiteral("@echo off\r\n");
    payload += QByteArrayLiteral("chcp >nul 2>&1\r\n");
    payload += scriptBody.toLocal8Bit();
    payload += QByteArrayLiteral("\r\nset CZL_RC=%errorlevel%\r\n");
    payload += QByteArrayLiteral("exit /b %CZL_RC%\r\n");

    if (file.write(payload) != payload.size()) {
        Logger::error(QStringLiteral("写入脚本失败: %1").arg(scriptPath));
        file.close();
        QFile::remove(scriptPath);
        return false;
    }
    file.close();

    Logger::info(QStringLiteral("执行脚本 [%1]: %2").arg(tag, scriptPath));

    const int result = TIHelperWrapper::instance().executeFile(scriptPath);
    QFile::remove(scriptPath);

    if (result != 0) {
        Logger::error(QStringLiteral("脚本执行失败 [%1] 返回码=%2 (%3)")
                          .arg(tag)
                          .arg(result)
                          .arg(scriptPath));
        return false;
    }
    return true;
}

bool InstallWorker::runViaTrustedInstaller(const QString &filePath, const QString &args)
{
    const QFileInfo info(filePath);
    if (!info.exists()) {
        Logger::error(QStringLiteral("待执行文件不存在: %1").arg(filePath));
        return false;
    }

    // 直接把 exe 与参数交给 TIHelper：TIHelper 内部使用
    // lpApplicationName + lpCommandLine，不再经过外壳解析，
    // 因此不再需要旧版那种「把 exe 包进 bat 再执行」的绕路。
    const int result = TIHelperWrapper::instance().executeFile(
        info.absoluteFilePath(), args);

    if (result != 0)
        Logger::error(QStringLiteral("以 TI 权限执行失败: %1 参数=[%2] 返回码=%3")
                          .arg(info.absoluteFilePath(), args)
                          .arg(result));

    return result == 0;
}

// ===========================================================================
// 服务器
// ===========================================================================
bool InstallWorker::ensureServer()
{
    if (m_servers.isEmpty())
        return false;
    if (m_activeIndex >= 0 && m_activeIndex < m_servers.size())
        return true;

    // 主服务器不可用：依次尝试其余地址
    emitProgress(QStringLiteral("正在选择备用服务器..."), 5);

    const int total = static_cast<int>(m_servers.size());
    for (int index = 0; index < total; ++index) {
        const QUrl url = DownloadManager::buildUrl(QString(), m_servers.at(index));

        QNetworkRequest request(url);
        DownloadManager::applyServerToRequest(request, m_servers.at(index),
                                              m_servers.at(index).connectTimeout);

        QNetworkReply *reply = m_nam->head(request);

        QEventLoop loop;
        QTimer timer;
        timer.setSingleShot(true);
        QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
        QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        timer.start(m_servers.at(index).connectTimeout);
        loop.exec();

        if (!timer.isActive()) {
            QObject::disconnect(reply, nullptr, &loop, nullptr);
            reply->abort();
        }

        // 与 GUI 线程的探测使用同一份判定逻辑
        const bool reachable = timer.isActive()
                               && DownloadManager::isServerAliveError(
                                      static_cast<int>(reply->error()));
        const QString errorText =
            DownloadManager::describeNetworkError(static_cast<int>(reply->error()));
        reply->deleteLater();

        if (reachable) {
            m_activeIndex = index;
            Logger::info(QStringLiteral("选定服务器: %1").arg(m_servers.at(index).name));
            emit serverChanged(index, m_servers.at(index).name);
            return true;
        }

        Logger::warn(QStringLiteral("服务器不可用: %1 (%2)")
                         .arg(m_servers.at(index).name, errorText));
    }

    return false;
}

// ===========================================================================
// 下载
// ===========================================================================
bool InstallWorker::downloadOneFile(const QString &remotePath,
                                    const QString &localPath)
{
    // 先尝试当前服务器，再轮询其余服务器
    const int total = static_cast<int>(m_servers.size());
    for (int attempt = 0; attempt < total; ++attempt) {
        if (aborted())
            return false;

        const int index = (m_activeIndex + attempt) % total;
        const ServerConfig &server = m_servers.at(index);

        const QUrl url = DownloadManager::buildUrl(remotePath, server);
        QNetworkRequest request(url);
        DownloadManager::applyServerToRequest(request, server, 300000);

        const QString nativeLocal = QDir::toNativeSeparators(localPath);
        if (!QDir().mkpath(QFileInfo(nativeLocal).absolutePath())) {
            Logger::error(QStringLiteral("无法创建下载目录: %1")
                              .arg(QFileInfo(nativeLocal).absolutePath()));
            return false;
        }

        // 先写 .part，成功后再改名，避免半截文件被当成成品
        const QString partPath = nativeLocal + QStringLiteral(".part");
        QFile::remove(partPath);

        QFile file(partPath);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            Logger::error(QStringLiteral("无法创建本地文件: %1 (%2)")
                              .arg(partPath, file.errorString()));
            return false;
        }

        QNetworkReply *reply = m_nam->get(request);

        QEventLoop loop;
        QTimer timer;
        timer.setSingleShot(true);
        QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
        QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        QObject::connect(reply, &QNetworkReply::readyRead, this,
                         [&file, reply]() { file.write(reply->readAll()); });

        timer.start(300000);
        loop.exec();

        const bool timedOut = !timer.isActive();
        if (timedOut) {
            QObject::disconnect(reply, nullptr, &loop, nullptr);
            reply->abort();
        }

        const auto error = reply->error();
        if (!timedOut && error == QNetworkReply::NoError) {
            file.write(reply->readAll());
            file.close();
            reply->deleteLater();

            QFile::remove(nativeLocal);
            if (!QFile::rename(partPath, nativeLocal)) {
                Logger::error(QStringLiteral("无法重命名下载文件: %1 -> %2")
                                  .arg(partPath, nativeLocal));
                QFile::remove(partPath);
                return false;
            }

            Logger::info(QStringLiteral("下载成功 [%1]: %2")
                             .arg(server.name, remotePath));
            if (index != m_activeIndex) {
                m_activeIndex = index;
                emit serverChanged(index, server.name);
            }
            return true;
        }

        const QString errorText = timedOut
                                      ? QStringLiteral("超时")
                                      : reply->errorString();
        file.close();
        reply->deleteLater();
        QFile::remove(partPath);

        Logger::warn(QStringLiteral("下载失败 [%1]: %2 - %3")
                         .arg(server.name, remotePath, errorText));
    }

    return false;
}

bool InstallWorker::downloadToLocal(QString &localPathOut)
{
    const QString localPath = resolveLocal(m_item.localTempPath);
    const bool isDirectory = m_item.remotePath.endsWith(QLatin1Char('/'))
                             || m_item.remotePath.endsWith(QLatin1Char('\\'));

    if (isDirectory || !m_item.files.isEmpty()) {
        if (m_item.files.isEmpty()) {
            Logger::error(QStringLiteral("目录下载缺少 files 列表: %1")
                              .arg(m_item.remotePath));
            return false;
        }

        QString remoteDir = m_item.remotePath;
        if (!remoteDir.endsWith(QLatin1Char('/')))
            remoteDir += QLatin1Char('/');

        if (!QDir().mkpath(localPath)) {
            Logger::error(QStringLiteral("无法创建目录: %1").arg(localPath));
            return false;
        }

        const int total = static_cast<int>(m_item.files.size());
        for (int index = 0; index < total; ++index) {
            if (aborted())
                return false;

            const QString remoteFile = remoteDir + m_item.files.at(index);
            const QString localFile =
                QDir::toNativeSeparators(QDir(localPath).absoluteFilePath(
                    m_item.files.at(index)));

            emitProgress(QStringLiteral("下载 (%1/%2): %3")
                             .arg(index + 1)
                             .arg(total)
                             .arg(m_item.files.at(index)),
                         30 + (total > 0 ? index * 25 / total : 0));

            if (!downloadOneFile(remoteFile, localFile))
                return false;
        }

        localPathOut = localPath;
        return true;
    }

    emitProgress(QStringLiteral("下载: %1").arg(m_item.remotePath), 30);
    if (!downloadOneFile(m_item.remotePath, localPath))
        return false;

    localPathOut = localPath;
    return true;
}

// ===========================================================================
// copy_* 类型：sources 处理
// ===========================================================================
bool InstallWorker::allSourcesOk()
{
    if (m_item.sources.isEmpty()) {
        Logger::warn(QStringLiteral("安装项 %1 的 sources 为空").arg(m_item.id));
        return true;
    }

    FileOperationManager fileManager;
    fileManager.setTiFallbackEnabled(true);

    const int total = static_cast<int>(m_item.sources.size());
    for (int index = 0; index < total; ++index) {
        if (aborted())
            return false;

        const CopySource &source = m_item.sources.at(index);

        emitProgress(QStringLiteral("处理 (%1/%2): %3")
                         .arg(index + 1)
                         .arg(total)
                         .arg(QFileInfo(source.remotePath).fileName()),
                     30 + (total > 0 ? index * 55 / total : 0));

        // ---- 目标路径 ----
        QString target = source.targetPath;
        if (target.isEmpty())
            target = m_item.targetPath;
        if (target.isEmpty()) {
            Logger::error(QStringLiteral("source 未指定 targetPath: %1")
                              .arg(source.remotePath));
            return false;
        }

        // ---- 下载 ----
        const QString localPath = resolveLocal(source.localPath);
        QString downloadedPath;

        if (source.kind == QLatin1String("dir")) {
            if (source.files.isEmpty()) {
                Logger::error(QStringLiteral("dir 类型需要 files 列表: %1")
                                  .arg(source.remotePath));
                return false;
            }

            QString remoteDir = source.remotePath;
            if (!remoteDir.endsWith(QLatin1Char('/')))
                remoteDir += QLatin1Char('/');

            if (!QDir().mkpath(localPath)) {
                Logger::error(QStringLiteral("无法创建目录: %1").arg(localPath));
                return false;
            }

            for (const QString &name : source.files) {
                if (aborted())
                    return false;
                const QString localFile = QDir::toNativeSeparators(
                    QDir(localPath).absoluteFilePath(name));
                if (!downloadOneFile(remoteDir + name, localFile))
                    return false;
            }
            downloadedPath = localPath;
        } else {
            if (!downloadOneFile(source.remotePath, localPath))
                return false;
            downloadedPath = localPath;
        }

        if (aborted())
            return false;

        // ---- 应用 ----
        const bool overwrite = m_item.overwrite && m_overwriteDefault;
        bool ok = false;

        if (source.kind == QLatin1String("zip")) {
            ok = fileManager.extractZip(downloadedPath, target, overwrite);
        } else if (source.kind == QLatin1String("dir")) {
            ok = fileManager.copyToTarget(downloadedPath, target, overwrite);
        } else {
            QString destination = target;
            if (destination.endsWith(QLatin1Char('/'))
                || destination.endsWith(QLatin1Char('\\'))) {
                destination += QFileInfo(downloadedPath).fileName();
            }
            ok = fileManager.copyToTarget(downloadedPath, destination, overwrite);
        }

        if (!ok) {
            Logger::error(QStringLiteral("处理失败 (%1/%2): %3")
                              .arg(index + 1)
                              .arg(total)
                              .arg(source.remotePath));
            return false;
        }
    }

    return true;
}

// ===========================================================================
// 执行安装
// ===========================================================================
void InstallWorker::executeMsi(const QString &localPath)
{
    QStringList args;
    args << QStringLiteral("/i") << QDir::toNativeSeparators(localPath);
    args << m_item.installArgs;

    // 只有配置里完全没有静默参数时才补 /qn，避免出现两个互斥的 UI 参数。
    const bool hasUiFlag = std::any_of(
        m_item.installArgs.cbegin(), m_item.installArgs.cend(),
        [](const QString &argument) {
            return argument.startsWith(QLatin1String("/q"), Qt::CaseInsensitive)
                   || argument.compare(QLatin1String("/quiet"),
                                       Qt::CaseInsensitive) == 0
                   || argument.compare(QLatin1String("/passive"),
                                       Qt::CaseInsensitive) == 0;
        });
    if (!hasUiFlag)
        args << QStringLiteral("/qn");

    args << QStringLiteral("/norestart");

    const QString script = QStringLiteral("msiexec.exe %1").arg(joinArguments(args));
    const bool ok = runScript(script, QStringLiteral("msi"));
    emit finished(ok, ok ? QStringLiteral("MSI 安装成功")
                         : QStringLiteral("MSI 安装失败，请查看日志"));
}

void InstallWorker::executeExe(const QString &localPath)
{
    const QString args = joinArguments(m_item.installArgs);
    const bool ok = runViaTrustedInstaller(localPath, args);
    emit finished(ok, ok ? QStringLiteral("EXE 安装成功")
                         : QStringLiteral("EXE 安装失败，请查看日志"));
}

void InstallWorker::executeBat(const QString &localPath)
{
    const bool ok = runViaTrustedInstaller(localPath,
                                           joinArguments(m_item.installArgs));
    emit finished(ok, ok ? QStringLiteral("批处理执行成功")
                         : QStringLiteral("批处理执行失败，请查看日志"));
}

void InstallWorker::executeRegisterCommands()
{
    if (m_item.registerCommands.isEmpty())
        return;

    QString body;
    for (const QString &command : m_item.registerCommands)
        body += command + QStringLiteral("\r\n");

    const bool ok = runScript(body, QStringLiteral("reg"));
    emit finished(ok, ok ? QStringLiteral("复制并注册成功")
                         : QStringLiteral("注册命令执行失败，请查看日志"));
}

// ===========================================================================
// 主流程
// ===========================================================================
void InstallWorker::process()
{
    m_nam = new QNetworkAccessManager(this);

    Logger::info(QStringLiteral("开始安装项: %1 (%2)").arg(m_item.name, m_item.id));

    if (!ensureServer()) {
        emit finished(false,
                      QStringLiteral("所有备用服务器均无法连接，安装已中止。\n"
                                     "请检查网络连接、服务器状态或 config 配置。"));
        return;
    }

    emitProgress(QStringLiteral("准备中..."), 3);

    // ---------- 前置清理 ----------
    if (!m_item.killServices.isEmpty() || !m_item.killProcesses.isEmpty()) {
        ProcessManager processManager;

        const int serviceCount = static_cast<int>(m_item.killServices.size());
        const int processCount = static_cast<int>(m_item.killProcesses.size());

        // 因为 worker 在自己的线程里，这里用直连（DirectConnection）即可，
        // 进度通过 InstallWorker::progress 排队回 GUI。
        QObject::connect(&processManager, &ProcessManager::cleanupProgress,
                         this, [this, serviceCount, processCount](
                                   const QString &action, const QString &name,
                                   int current, int total) {
            Q_UNUSED(total)
            int percent = 12;
            QString stage;

            if (action == QLatin1String("stop_service")) {
                percent = 12 + (serviceCount > 0 ? current * 6 / serviceCount : 0);
                stage = QStringLiteral("正在停止服务 (%1/%2): %3")
                            .arg(current).arg(serviceCount).arg(name);
            } else if (action == QLatin1String("wait_service")) {
                stage = QStringLiteral("等待服务停止: %1").arg(name);
            } else if (action == QLatin1String("kill_process")) {
                percent = 20 + (processCount > 0 ? current * 8 / processCount : 0);
                stage = QStringLiteral("正在结束进程 (%1/%2): %3")
                            .arg(current).arg(processCount).arg(name);
            } else {
                stage = QStringLiteral("等待进程退出: %1").arg(name);
            }
            emitProgress(stage, percent);
        });

        if (serviceCount > 0) {
            emitProgress(QStringLiteral("准备停止 %1 个服务...").arg(serviceCount), 12);
            processManager.stopServices(m_item.killServices, 20000);
        }
        if (processCount > 0) {
            emitProgress(QStringLiteral("准备结束 %1 个进程...").arg(processCount), 20);
            processManager.killProcesses(m_item.killProcesses, 15000);
        }
    }

    if (aborted()) {
        emit finished(false, QStringLiteral("安装已取消"));
        return;
    }

    // ---------- copy_only / copy_and_register ----------
    const bool isCopyType = (m_item.type == QLatin1String("copy_only")
                             || m_item.type == QLatin1String("copy_and_register"));

    if (isCopyType) {
        if (!allSourcesOk()) {
            emit finished(false, QStringLiteral("复制/解压失败，请查看日志"));
            return;
        }

        if (m_item.type == QLatin1String("copy_and_register")) {
            emitProgress(QStringLiteral("执行注册命令..."), 92);
            executeRegisterCommands();
        } else {
            emit finished(true, QStringLiteral("复制/解压完成"));
        }
        return;
    }

    // ---------- exe / msi / bat ----------
    emitProgress(QStringLiteral("下载到本地..."), 30);

    QString localPath;
    if (!downloadToLocal(localPath)) {
        emit finished(false,
                      QStringLiteral("下载失败（已尝试全部 %1 个服务器）: %2")
                          .arg(m_servers.size())
                          .arg(m_item.remotePath));
        return;
    }

    if (aborted()) {
        emit finished(false, QStringLiteral("安装已取消"));
        return;
    }

    emitProgress(QStringLiteral("下载完成，开始执行安装..."), 60);

    if (m_item.type == QLatin1String("msi")) {
        executeMsi(localPath);
    } else if (m_item.type == QLatin1String("exe")) {
        executeExe(localPath);
    } else if (m_item.type == QLatin1String("bat")) {
        executeBat(localPath);
    } else {
        emit finished(false, QStringLiteral("未知安装类型: %1").arg(m_item.type));
    }
}
