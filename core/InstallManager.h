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
    /// 切换 config_ftp.json / config_http.json（"ftp" | "http"）。
    void setServerType(const QString &type);

signals:
    void installStarted(const QString &itemId);
    void installProgress(const QString &itemId, int percent, const QString &stage);
    void installFinished(const QString &itemId, bool success, const QString &message);

    void activeServerChanged();
    void busyChanged(bool busy);

private:
    void cleanupWorker();
    static QString configFileForType(const QString &type);

    ConfigManager   *m_config = nullptr;
    DownloadManager *m_downloadManager = nullptr;

    QThread       *m_thread = nullptr;
    InstallWorker *m_worker = nullptr;
    QString        m_currentItemId;
    QString        m_currentServerType = QStringLiteral("ftp");
};

#endif // INSTALLMANAGER_H
