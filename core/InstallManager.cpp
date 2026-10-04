#include "InstallManager.h"
#include "DownloadManager.h"
#include "ProcessManager.h"
#include "FileOperationManager.h"
#include "TIHelperWrapper.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>
#include <QThread>
#include <QCoreApplication>
#include <QDebug>

InstallManager::InstallManager(QObject *parent) : QObject(parent) {}

void InstallManager::setDependencies(ConfigManager *cfg,
                                      DownloadManager *dl,
                                      ProcessManager *pm,
                                      FileOperationManager *fm)
{
    m_config = cfg;
    m_downloadManager = dl;
    m_processManager = pm;
    m_fileManager = fm;
}

QString InstallManager::activeServerName() const
{
    return m_downloadManager ? m_downloadManager->activeServerName() : QString();
}

void InstallManager::setServerConfigFromQml(const QString &type)
{
    QString configFile = (type == "http")
        ? "config/config_http.json"
        : "config/config_ftp.json";

    if (m_config->loadConfig(configFile)) {
        m_downloadManager->setServers(m_config->servers());
        m_downloadManager->setLocalBaseDir(m_config->localBaseDir());
        emit activeServerChanged();
    }
}

// ============================================================
// 主流程
// ============================================================
void InstallManager::installItem(const QString &itemId)
{
    InstallItem item;
    if (!m_config->findItem(itemId, item)) {
        emit installFinished(itemId, false, "未找到安装项: " + itemId);
        return;
    }

    emit installStarted(itemId);
    emit installProgress(itemId, 3, "准备中...");
    QCoreApplication::processEvents();

    // ---------- ① 探测服务器 ----------
    emit installProgress(itemId, 5, "检测服务器可用性...");
    QCoreApplication::processEvents();
    if (!m_downloadManager->probeServers(/*force=*/true)) {
        emit installFinished(itemId, false,
            "所有备用服务器均无法连接，安装已中止。\n"
            "请检查网络连接、服务器状态或 config 配置。");
        return;
    }
    emit installProgress(itemId, 10,
        QString("当前服务器: %1").arg(m_downloadManager->activeServerName()));
    emit activeServerChanged();
    QCoreApplication::processEvents();

    // ---------- ② 前置清理 ----------
    const int svcCount = item.killServices.size();
    const int procCount = item.killProcesses.size();

    if (svcCount > 0 || procCount > 0) {
        auto conn = connect(m_processManager, &ProcessManager::cleanupProgress,
                            this, [this, itemId, svcCount, procCount]
                            (const QString &action, const QString &name,
                             int cur, int total) {
            Q_UNUSED(total);
            QString stage;
            int pct = 12;
            if (action == "stop_service") {
                pct = 12 + (svcCount > 0 ? cur * 6 / svcCount : 0);
                stage = QString("正在停止服务 (%1/%2): %3")
                            .arg(cur).arg(svcCount).arg(name);
            } else if (action == "wait_service") {
                stage = QString("等待服务停止: %1").arg(name);
            } else if (action == "kill_process") {
                pct = 20 + (procCount > 0 ? cur * 8 / procCount : 0);
                stage = QString("正在终止进程 (%1/%2): %3")
                            .arg(cur).arg(procCount).arg(name);
            } else if (action == "wait_process") {
                stage = QString("等待进程退出: %1").arg(name);
            }
            emit installProgress(itemId, pct, stage);
            QCoreApplication::processEvents();
        });

        if (svcCount > 0) {
            emit installProgress(itemId, 12,
                QString("准备停止 %1 个服务...").arg(svcCount));
            QCoreApplication::processEvents();
            m_processManager->stopServices(item.killServices, 20000);
        }
        if (procCount > 0) {
            emit installProgress(itemId, 20,
                QString("准备终止 %1 个进程...").arg(procCount));
            QCoreApplication::processEvents();
            m_processManager->killProcesses(item.killProcesses, 15000);
        }
        disconnect(conn);
        QThread::msleep(500);
    }

    // ---------- ③ 按类型分发 ----------
    if (item.type == "copy_only" || item.type == "copy_and_register") {
        if (!processAllSources(item, 30, 90)) {
            emit installFinished(itemId, false, "拷贝/解压失败");
            return;
        }

        if (item.type == "copy_and_register" && !item.registerCommands.isEmpty()) {
            emit installProgress(itemId, 92, "执行注册命令...");
            QCoreApplication::processEvents();

            QString batPath = m_downloadManager->resolveLocalPath(
                QString("__reg_%1.bat").arg(item.id));
            QFile bat(batPath);
            if (!bat.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                emit installFinished(itemId, false, "创建注册脚本失败");
                return;
            }
            QTextStream ts(&bat);
            ts.setCodec("UTF-8");
            ts << "@echo off\r\n";
            for (const QString &cmd : item.registerCommands)
                ts << cmd << "\r\n";
            bat.close();

            bool ok = runViaTI(batPath, item);
            QFile::remove(batPath);
            emit installFinished(itemId, ok,
                ok ? "复制并注册成功" : "注册命令执行失败");
        } else {
            emit installFinished(itemId, true, "复制/解压完成");
        }
        return;
    }

    // ---------- exe / msi / bat ----------
    emit installProgress(itemId, 30, "下载到本地...");
    QCoreApplication::processEvents();

    QString localPath;
    if (!downloadItemToLocal(item, localPath)) {
        emit installFinished(itemId, false,
            QString("下载失败（已尝试全部 %1 个服务器）: %2")
                .arg(m_config->servers().size())
                .arg(item.remotePath));
        return;
    }
    emit installProgress(itemId, 60, "下载完成，开始执行安装...");
    QCoreApplication::processEvents();

    if (item.type == "msi")      executeMsi(item, localPath);
    else if (item.type == "exe") executeExe(item, localPath);
    else if (item.type == "bat") executeBat(item, localPath);
    else emit installFinished(itemId, false, "未知安装类型: " + item.type);
}

// ============================================================
// exe/msi/bat 单文件下载
// ============================================================
bool InstallManager::downloadItemToLocal(const InstallItem &item,
                                          QString &localPathOut)
{
    QString baseLocal = m_downloadManager->resolveLocalPath(item.localTempPath);
    bool isDir = item.remotePath.endsWith('/') || item.remotePath.endsWith('\\');
    if (isDir || !item.files.isEmpty()) {
        if (!m_downloadManager->downloadDirectorySync(
                item.remotePath, baseLocal, item.files))
            return false;
        localPathOut = baseLocal;
        return true;
    }
    if (!m_downloadManager->downloadFileSync(item.remotePath, baseLocal))
        return false;
    localPathOut = baseLocal;
    return true;
}

// ============================================================
// sources 数组处理
// ============================================================
bool InstallManager::processAllSources(const InstallItem &item,
                                        int pStart, int pEnd)
{
    const int total = item.sources.size();
    if (total == 0) {
        emit installProgress(item.id, pStart, "sources 为空，跳过");
        return true;
    }

    const int span = pEnd - pStart;
    for (int i = 0; i < total; ++i) {
        const CopySource &src = item.sources[i];
        int pct = pStart + (span * i / total);

        QString kindLabel;
        if (src.kind == "file")      kindLabel = "文件";
        else if (src.kind == "dir")  kindLabel = "目录";
        else if (src.kind == "zip")  kindLabel = "压缩包";
        else                         kindLabel = src.kind;

        emit installProgress(item.id, pct,
            QString("处理 %1 (%2/%3): %4")
                .arg(kindLabel).arg(i + 1).arg(total)
                .arg(QFileInfo(src.remotePath).fileName()));
        QCoreApplication::processEvents();

        if (!processOneSource(item, src)) {
            emit installProgress(item.id, pct,
                QString("第 %1 项失败: %2").arg(i + 1).arg(src.remotePath));
            return false;
        }
    }
    return true;
}

bool InstallManager::processOneSource(const InstallItem &item,
                                       const CopySource &src)
{
    // ---------- 下载 ----------
    QString localBase = m_downloadManager->resolveLocalPath(src.localPath);
    QString downloadedPath;

    if (src.kind == "dir") {
        if (src.files.isEmpty()) {
            qWarning() << "dir 需要 files 列表:" << src.remotePath;
            return false;
        }
        if (!m_downloadManager->downloadDirectorySync(
                src.remotePath, localBase, src.files))
            return false;
        downloadedPath = localBase;
    } else {
        if (!m_downloadManager->downloadFileSync(src.remotePath, localBase))
            return false;
        downloadedPath = localBase;
    }

    // ---------- 目标路径 ----------
    QString target = src.targetPath;
    if (target.isEmpty()) target = item.targetPath;
    if (target.isEmpty()) {
        qWarning() << "source 未指定 targetPath:" << src.remotePath;
        return false;
    }

    // ---------- 处理 ----------
    if (src.kind == "zip")
        return m_fileManager->extractZip(downloadedPath, target, item.overwrite);

    if (src.kind == "dir")
        return m_fileManager->copyToTarget(downloadedPath, target, item.overwrite);

    // file
    QString dst = target;
    if (dst.endsWith('/') || dst.endsWith('\\'))
        dst += QFileInfo(downloadedPath).fileName();

    return m_fileManager->copyToTarget(downloadedPath, dst, item.overwrite);
}

// ============================================================
// TI 执行封装
// ============================================================
bool InstallManager::runViaTI(const QString &filePath, const InstallItem &item)
{
    Q_UNUSED(item);
    if (!TIHelperWrapper::instance().isLoaded()) {
        qWarning() << "TIHelper.dll 未加载";
        return false;
    }
    QFileInfo fi(filePath);
    if (!fi.exists()) {
        qWarning() << "本地文件不存在:" << filePath;
        return false;
    }
    return TIHelperWrapper::instance().executeFile(filePath) == 0;
}

bool InstallManager::runWithArgsViaTI(const QString &exePath,
                                       const QStringList &args,
                                       const InstallItem &item)
{
    if (args.isEmpty()) return runViaTI(exePath, item);

    QString batPath = m_downloadManager->resolveLocalPath(
        QString("__wrapped_%1.bat").arg(item.id));

    QFile bat(batPath);
    if (!bat.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;

    QTextStream ts(&bat);
    ts.setCodec("UTF-8");
    ts << "@echo off\r\n";
    ts << "\"" << QDir::toNativeSeparators(exePath) << "\"";
    for (const QString &a : args) ts << " " << a;
    ts << "\r\n";
    bat.close();

    bool ok = runViaTI(batPath, item);
    QFile::remove(batPath);
    return ok;
}

void InstallManager::executeMsi(const InstallItem &item, const QString &localPath)
{
    QString batPath = m_downloadManager->resolveLocalPath(
        QString("__msi_%1.bat").arg(item.id));

    QFile bat(batPath);
    if (!bat.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        emit installFinished(item.id, false, "创建 MSI 脚本失败");
        return;
    }
    QTextStream ts(&bat);
    ts.setCodec("UTF-8");
    ts << "@echo off\r\n";
    ts << "msiexec.exe /i \"" << QDir::toNativeSeparators(localPath) << "\" ";
    if (!item.installArgs.isEmpty())
        ts << item.installArgs.join(' ') << " ";
    ts << "/qn\r\n";
    bat.close();

    bool ok = runViaTI(batPath, item);
    QFile::remove(batPath);
    emit installFinished(item.id, ok, ok ? "MSI 安装成功" : "MSI 安装失败");
}

void InstallManager::executeExe(const InstallItem &item, const QString &localPath)
{
    bool ok = runWithArgsViaTI(localPath, item.installArgs, item);
    emit installFinished(item.id, ok, ok ? "EXE 安装成功" : "EXE 安装失败");
}

void InstallManager::executeBat(const InstallItem &item, const QString &localPath)
{
    bool ok = runViaTI(localPath, item);
    emit installFinished(item.id, ok, ok ? "批处理执行成功" : "批处理执行失败");
}
