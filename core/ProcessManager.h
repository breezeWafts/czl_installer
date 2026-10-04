#ifndef PROCESSMANAGER_H
#define PROCESSMANAGER_H

#include <QObject>
#include <QStringList>

class ProcessManager : public QObject
{
    Q_OBJECT
public:
    explicit ProcessManager(QObject *parent = nullptr);

    // 批量停止服务、批量终止进程（按数组顺序执行）
    bool stopServices(const QStringList &names, int waitMsPerService = 20000);
    bool killProcesses(const QStringList &names, int waitMsPerProcess = 15000);

    bool stopServiceByName(const QString &name, int waitMs = 20000);
    bool killProcessByName(const QString &name, int waitMs = 15000);

signals:
    void logMessage(const QString &msg);
    // action: stop_service | wait_service | kill_process | wait_process
    void cleanupProgress(const QString &action, const QString &name,
                         int current, int total);

private:
    bool isProcessRunning(const QString &name);
    bool serviceExists(const QString &name);
    bool isServiceStopped(const QString &name);
    bool isServicePending(const QString &name);
    bool waitProcessExit(const QString &name, int timeoutMs);
    bool waitServiceStopped(const QString &name, int timeoutMs);
};

#endif
