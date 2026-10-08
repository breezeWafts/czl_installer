#include "InstallManager.h"

#include "DownloadManager.h"
#include "InstallWorker.h"
#include "Logger.h"

#include <QCoreApplication>
#include <QThread>

// ===========================================================================
// 构造 / 析构
// ===========================================================================
InstallManager::InstallManager(QObject *parent)
    : QObject(parent)
{
}

InstallManager::~InstallManager()
{
    if (m_thread == nullptr)
        return;

    if (m_worker != nullptr)
        m_worker->requestAbort();

    m_thread->quit();
    if (!m_thread->wait(10000)) {
        m_thread->terminate();
        m_thread->wait(3000);
    }
}

void InstallManager::setDependencies(ConfigManager *config,
                                     DownloadManager *downloadManager)
{
    m_config = config;
    m_downloadManager = downloadManager;
}

QString InstallManager::activeServerName() const
{
    return m_downloadManager != nullptr ? m_downloadManager->activeServerName()
                                        : QString();
}

QString InstallManager::configFileForType(const QString &type)
{
    return (type == QLatin1String("http"))
               ? QStringLiteral("config/config_http.json")
               : QStringLiteral("config/config_ftp.json");
}

// ===========================================================================
// 配置切换
// ===========================================================================
void InstallManager::setServerType(const QString &type)
{
    const QString normalized = type.trimmed().toLower();
    if (normalized != QLatin1String("ftp") && normalized != QLatin1String("http")) {
        Logger::warn(QStringLiteral("未知的服务器类型: %1").arg(type));
        return;
    }

    Logger::info(QStringLiteral("切换服务器方式为: %1").arg(normalized));

    if (m_config == nullptr)
        return;

    if (!m_config->loadConfig(configFileForType(normalized)))
        return;

    m_currentServerType = normalized;

    if (m_downloadManager != nullptr) {
        // 切换配置后旧的活动服务器索引不再有效
        m_downloadManager->setServers(m_config->servers());
    }

    emit activeServerChanged();
}

// ===========================================================================
// 安装
// ===========================================================================
void InstallManager::installItem(const QString &itemId)
{
    if (isBusy()) {
        // 旧版没有这个保护：安装期间再次点击会并行跑两套流程
        emit installFinished(itemId, false,
                             QStringLiteral("已有安装任务正在进行，请等待其完成"));
        return;
    }

    if (m_config == nullptr || m_downloadManager == nullptr) {
        emit installFinished(itemId, false, QStringLiteral("内部错误：依赖未注入"));
        return;
    }

    InstallItem item;
    if (!m_config->findItem(itemId, item)) {
        emit installFinished(itemId, false,
                             QStringLiteral("未找到安装项: %1").arg(itemId));
        return;
    }

    emit installStarted(itemId);
    emit installProgress(itemId, 1, QStringLiteral("准备中..."));

    m_currentItemId = itemId;

    // 传值快照：worker 线程访问自己的副本，不存在共享可变状态。
    auto *worker = new InstallWorker(item,
                                     m_config->servers(),
                                     m_downloadManager->activeServerIndex(),
                                     m_config->localBaseDir(),
                                     /*overwriteDefault=*/true);
    auto *thread = new QThread(this);

    worker->moveToThread(thread);
    m_worker = worker;
    m_thread = thread;

    // worker -> GUI
    connect(worker, &InstallWorker::progress, this,
            [this, itemId](const QString &stage, int percent) {
        emit installProgress(itemId, percent, stage);
    });

    connect(worker, &InstallWorker::finished, this,
            [this, itemId](bool success, const QString &message) {
        emit installFinished(itemId, success, message);
    });

    connect(worker, &InstallWorker::serverChanged, this,
            [this](int index, const QString &name) {
        if (m_downloadManager != nullptr)
            m_downloadManager->setActiveServerIndex(index);
        Logger::info(QStringLiteral("安装使用服务器: %1").arg(name));
        emit activeServerChanged();
    });

    // 线程启动即开始干活
    connect(thread, &QThread::started, worker, &InstallWorker::process);

    // worker 完成 -> 让线程的事件循环退出
    connect(worker, &InstallWorker::finished, thread, &QThread::quit);

    // 线程真正结束后再做销毁。
    // 注意：不要在 worker 线程里用 deleteLater() —— 事件循环此时正在退出，
    // 延迟删除事件很可能永远得不到处理，对象就泄漏了。
    // 在 QThread::finished 里删除是安全的：此刻线程已停止运行，不会再访问该对象。
    connect(thread, &QThread::finished, this, &InstallManager::cleanupWorker);

    emit busyChanged(true);
    thread->start();
}

void InstallManager::cleanupWorker()
{
    // QThread::finished 在「工作线程已结束」的状态下发射，因此这里可以安全地
    // 直接从 GUI 线程销毁 worker，不需要依赖事件循环处理 deleteLater()。
    delete m_worker;
    m_worker = nullptr;

    if (m_thread != nullptr) {
        m_thread->wait(5000);
        m_thread->deleteLater();
        m_thread = nullptr;
    }

    Logger::info(QStringLiteral("安装任务结束: %1").arg(m_currentItemId));
    m_currentItemId.clear();

    emit busyChanged(false);
}

void InstallManager::cancelCurrent()
{
    if (m_worker == nullptr)
        return;

    Logger::warn(QStringLiteral("请求取消安装: %1").arg(m_currentItemId));
    m_worker->requestAbort();
}
