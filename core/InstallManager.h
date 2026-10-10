#ifndef INSTALLMANAGER_H
#define INSTALLMANAGER_H

#include <QObject>
#include <QString>

#include "ConfigManager.h"

class QThread;
class InstallWorker;
class DownloadManager;

/**
 * @brief 安装流程的调度者：负责准备快照、启动后台线程、转发进度信号。
 *
 * 与旧版的关键差异：
 *   旧版 installItem() 在 GUI 线程里同步跑完「探测 -> 下载 -> 执行安装」，
 *   靠 processEvents() 刷新界面，导致界面卡死且槽函数可重入。
 *   现在 installItem() 会立刻返回：真正的活在 InstallWorker 里，
 *   跑在独立的 QThread 上，进度通过排队信号回到 GUI 线程。
 */
class InstallManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString activeServerName READ activeServerName
                   NOTIFY activeServerChanged)
    Q_PROPERTY(bool busy READ isBusy NOTIFY busyChanged)

public:
    explicit InstallManager(QObject *parent = nullptr);
    ~InstallManager() override;

    void setDependencies(ConfigManager *config,
                         DownloadManager *downloadManager);

    /// 是否正在安装（同一时刻只允许一个安装任务）。
    bool isBusy() const { return m_worker != nullptr; }

    QString activeServerName() const;

    /// 取消当前的安装任务。
    void cancelCurrent();

public slots:
    /// 开始安装（立即返回）。已在安装中时忽略并给出提示。
    void installItem(const QString &itemId);

    /**
     * @brief 切换配置来源。
     *
     * @param source ConfigManager::ConfigSource::JsonFile（模式一：JSON + FTP）
     *               或 ConfigManager::ConfigSource::PostgreSQL（模式二：PG + HTTP）
     * @return 切换并成功加载返回 true；失败返回 false 并保留原配置。
     */
    bool setConfigSource(ConfigManager::ConfigSource source);

    /// 兼容旧调用：按字符串切换（"json" / "ftp" / "pg" / "postgresql" / "pgsql"）。
    bool setServerType(const QString &type);

    /// 按当前来源重新加载（Web 端改完配置后点「刷新配置」用）。
    bool reloadConfig();

    ConfigManager::ConfigSource currentSource() const;

signals:
    void installStarted(const QString &itemId);
    void installProgress(const QString &itemId, int percent, const QString &stage);
    void installFinished(const QString &itemId, bool success, const QString &message);

    void activeServerChanged();
    void busyChanged(bool busy);

    /// 配置来源切换/重新加载后发出，供主窗口给出中文提示。
    void sourceChanged(const QString &description, bool ok, const QString &message);

private:
    void cleanupWorker();

    ConfigManager   *m_config = nullptr;
    DownloadManager *m_downloadManager = nullptr;

    QThread       *m_thread = nullptr;
    InstallWorker *m_worker = nullptr;
    QString        m_currentItemId;
};

#endif // INSTALLMANAGER_H
