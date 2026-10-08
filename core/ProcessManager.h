#ifndef PROCESSMANAGER_H
#define PROCESSMANAGER_H

#include <QObject>
#include <QString>
#include <QStringList>

/**
 * @brief 安装前的环境清理：停止服务、结束进程。
 *
 * 这是对旧版改动最大的模块之一。旧版通过启动 sc / tasklist / wmic
 * 并解析它们的**本地化文本输出**来判断状态，问题包括：
 *   - 中文/日文 Windows 上 "STATE"、"PID"、"SERVICE_NAME" 之外的
 *     本地化字符串匹配不稳定，服务是否停止可能判断错误；
 *   - stopServiceByName() 每轮都新起 sc query 进程，等待服务停止
 *     时反复 fork，慢且噪音大；
 *   - wmic 已被微软标记为弃用，Win11 24H2 起默认不再预装；
 *   - 判断「进程未运行」依赖 tasklist 的英文 "No tasks" 文案。
 *
 * 现在全部改为原生 Win32 API：
 *   - 服务：OpenSCManager / OpenService / QueryServiceStatusEx / ControlService
 *   - 进程：CreateToolhelp32Snapshot / OpenProcess / TerminateProcess
 * 不再依赖任何外部程序，也不依赖系统语言。
 */
class ProcessManager : public QObject
{
    Q_OBJECT

public:
    explicit ProcessManager(QObject *parent = nullptr);

    /// 按数组顺序批量停止服务。
    bool stopServices(const QStringList &names, int waitMsPerService = 20000);
    /// 按数组顺序批量结束进程。
    bool killProcesses(const QStringList &names, int waitMsPerProcess = 15000);

    bool stopServiceByName(const QString &name, int waitMs = 20000);
    bool killProcessByName(const QString &name, int waitMs = 15000);

    /// 供 UI 使用：某进程当前是否在运行。
    static bool isProcessRunning(const QString &processName);
    /// 供 UI 使用：某服务当前状态描述（"未安装" / "已停止" / "运行中" ...）。
    static QString serviceStatusText(const QString &serviceName);

signals:
    void logMessage(const QString &message);
    /// action: stop_service | wait_service | kill_process | wait_process
    void cleanupProgress(const QString &action, const QString &name,
                         int current, int total);

private:
    bool serviceExists(const QString &name);
    bool isServiceStopped(const QString &name);
    bool isServicePending(const QString &name);
    bool waitProcessExit(const QString &name, int timeoutMs);
    bool waitServiceStopped(const QString &name, int timeoutMs);
    /// 服务卡在 PENDING 时，按 PID 强杀其宿主进程。
    bool killServiceHostByPid(const QString &serviceName);
};

#endif // PROCESSMANAGER_H
