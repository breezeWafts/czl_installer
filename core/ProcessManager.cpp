#include "ProcessManager.h"

#include <QElapsedTimer>
#include <QThread>

#include <algorithm>

#include <windows.h>
#include <tlhelp32.h>   // CreateToolhelp32Snapshot / Process32FirstW 等

namespace {

/// 打开 SCM 的 RAII 包装。
class ScmHandle
{
public:
    explicit ScmHandle(DWORD access = SC_MANAGER_CONNECT)
        : m_handle(::OpenSCManagerW(nullptr, nullptr, access))
    {
    }
    ~ScmHandle()
    {
        if (m_handle != nullptr)
            ::CloseServiceHandle(m_handle);
    }

    ScmHandle(const ScmHandle &) = delete;
    ScmHandle &operator=(const ScmHandle &) = delete;

    bool isValid() const { return m_handle != nullptr; }
    SC_HANDLE get() const { return m_handle; }

private:
    SC_HANDLE m_handle = nullptr;
};

/// 打开服务的 RAII 包装。
class ServiceHandle
{
public:
    ServiceHandle(SC_HANDLE scm, const std::wstring &name, DWORD access)
    {
        if (scm != nullptr)
            m_handle = ::OpenServiceW(scm, name.c_str(), access);
    }
    ~ServiceHandle()
    {
        if (m_handle != nullptr)
            ::CloseServiceHandle(m_handle);
    }

    ServiceHandle(const ServiceHandle &) = delete;
    ServiceHandle &operator=(const ServiceHandle &) = delete;

    bool isValid() const { return m_handle != nullptr; }
    SC_HANDLE get() const { return m_handle; }

private:
    SC_HANDLE m_handle = nullptr;
};

/// 进程句柄 RAII。
class ProcessHandle
{
public:
    explicit ProcessHandle(HANDLE handle = nullptr) : m_handle(handle) {}
    ~ProcessHandle()
    {
        if (m_handle != nullptr)
            ::CloseHandle(m_handle);
    }

    ProcessHandle(const ProcessHandle &) = delete;
    ProcessHandle &operator=(const ProcessHandle &) = delete;

    HANDLE get() const { return m_handle; }
    bool isValid() const { return m_handle != nullptr; }

private:
    HANDLE m_handle = nullptr;
};

std::wstring toWide(const QString &text)
{
    return text.toStdWString();
}

/// 不区分大小写比较两个进程名（自动补 .exe）。
bool processNameMatches(const QString &candidate, const QString &wanted)
{
    QString left  = candidate.trimmed();
    QString right = wanted.trimmed();
    if (left.isEmpty() || right.isEmpty())
        return false;

    if (!left.endsWith(QLatin1String(".exe"), Qt::CaseInsensitive))
        left += QLatin1String(".exe");
    if (!right.endsWith(QLatin1String(".exe"), Qt::CaseInsensitive))
        right += QLatin1String(".exe");

    return left.compare(right, Qt::CaseInsensitive) == 0;
}

/// 查询服务状态；服务不存在返回 false。
bool queryServiceStatus(SC_HANDLE service, SERVICE_STATUS_PROCESS &status)
{
    DWORD needed = 0;
    return ::QueryServiceStatusEx(service, SC_STATUS_PROCESS_INFO,
                                  reinterpret_cast<LPBYTE>(&status),
                                  sizeof(status), &needed) != FALSE;
}

} // namespace

ProcessManager::ProcessManager(QObject *parent)
    : QObject(parent)
{
}

// ===========================================================================
// 批量
// ===========================================================================
bool ProcessManager::stopServices(const QStringList &names, int waitMsPerService)
{
    if (names.isEmpty())
        return true;

    bool allOk = true;
    const int total = static_cast<int>(names.size());
    for (int index = 0; index < total; ++index) {
        emit cleanupProgress(QStringLiteral("stop_service"), names.at(index),
                             index + 1, total);
        if (!stopServiceByName(names.at(index), waitMsPerService))
            allOk = false;
    }
    return allOk;
}

bool ProcessManager::killProcesses(const QStringList &names, int waitMsPerProcess)
{
    if (names.isEmpty())
        return true;

    bool allOk = true;
    const int total = static_cast<int>(names.size());
    for (int index = 0; index < total; ++index) {
        emit cleanupProgress(QStringLiteral("kill_process"), names.at(index),
                             index + 1, total);
        if (!killProcessByName(names.at(index), waitMsPerProcess))
            allOk = false;
    }
    return allOk;
}

// ===========================================================================
// 服务
// ===========================================================================
bool ProcessManager::serviceExists(const QString &name)
{
    const ScmHandle scm;
    if (!scm.isValid())
        return false;

    const ServiceHandle service(scm.get(), toWide(name),
                                SERVICE_QUERY_STATUS);
    return service.isValid();
}

bool ProcessManager::isServiceStopped(const QString &name)
{
    const ScmHandle scm;
    if (!scm.isValid())
        return false;

    const ServiceHandle service(scm.get(), toWide(name), SERVICE_QUERY_STATUS);
    if (!service.isValid())
        return true;    // 服务不存在，视为「不需要停止」

    SERVICE_STATUS_PROCESS status{};
    if (!queryServiceStatus(service.get(), status))
        return false;

    return status.dwCurrentState == SERVICE_STOPPED;
}

bool ProcessManager::isServicePending(const QString &name)
{
    const ScmHandle scm;
    if (!scm.isValid())
        return false;

    const ServiceHandle service(scm.get(), toWide(name), SERVICE_QUERY_STATUS);
    if (!service.isValid())
        return false;

    SERVICE_STATUS_PROCESS status{};
    if (!queryServiceStatus(service.get(), status))
        return false;

    return status.dwCurrentState == SERVICE_START_PENDING
           || status.dwCurrentState == SERVICE_STOP_PENDING
           || status.dwCurrentState == SERVICE_PAUSE_PENDING
           || status.dwCurrentState == SERVICE_CONTINUE_PENDING;
}

bool ProcessManager::killServiceHostByPid(const QString &serviceName)
{
    const ScmHandle scm;
    if (!scm.isValid())
        return false;

    const ServiceHandle service(scm.get(), toWide(serviceName),
                                SERVICE_QUERY_STATUS);
    if (!service.isValid())
        return false;

    SERVICE_STATUS_PROCESS status{};
    if (!queryServiceStatus(service.get(), status))
        return false;

    // QueryServiceStatusEx 直接给出宿主 PID，不需要再解析 "sc queryex" 文本。
    const DWORD pid = status.dwProcessId;
    if (pid == 0)
        return false;

    const ProcessHandle process(::OpenProcess(
        PROCESS_TERMINATE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid));
    if (!process.isValid())
        return false;

    if (!::TerminateProcess(process.get(), static_cast<UINT>(-1)))
        return false;

    ::WaitForSingleObject(process.get(), 5000);
    return true;
}

bool ProcessManager::stopServiceByName(const QString &name, int waitMs)
{
    if (!serviceExists(name)) {
        emit logMessage(QStringLiteral("服务不存在，跳过: %1").arg(name));
        return true;
    }
    if (isServiceStopped(name)) {
        emit logMessage(QStringLiteral("服务已停止: %1").arg(name));
        return true;
    }

    emit logMessage(QStringLiteral("正在停止服务: %1").arg(name));

    {
        const ScmHandle scm;
        const ServiceHandle service(scm.get(), toWide(name),
                                    SERVICE_STOP | SERVICE_QUERY_STATUS);
        if (service.isValid()) {
            SERVICE_STATUS status{};
            if (!::ControlService(service.get(), SERVICE_CONTROL_STOP, &status)) {
                const DWORD error = ::GetLastError();
                // ERROR_SERVICE_NOT_ACTIVE 说明已经停了，不算失败
                if (error != ERROR_SERVICE_NOT_ACTIVE) {
                    emit logMessage(QStringLiteral("ControlService(STOP) 失败: %1 (err=%2)")
                                        .arg(name)
                                        .arg(error));
                }
            }
        }
    }

    emit cleanupProgress(QStringLiteral("wait_service"), name, 0, 0);
    if (waitServiceStopped(name, waitMs)) {
        emit logMessage(QStringLiteral("服务已停止: %1").arg(name));
        return true;
    }

    // 卡在 PENDING：直接按 PID 结束宿主进程
    if (isServicePending(name)) {
        emit logMessage(QStringLiteral("服务卡在 PENDING，尝试结束宿主进程: %1")
                            .arg(name));
        (void)killServiceHostByPid(name);

        if (waitServiceStopped(name, 5000)) {
            emit logMessage(QStringLiteral("服务已强制停止: %1").arg(name));
            return true;
        }
    }

    emit logMessage(QStringLiteral("警告：服务未能停止: %1").arg(name));
    return false;
}

bool ProcessManager::waitServiceStopped(const QString &name, int timeoutMs)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeoutMs) {
        if (isServiceStopped(name))
            return true;
        QThread::msleep(400);
    }
    return isServiceStopped(name);
}

QString ProcessManager::serviceStatusText(const QString &serviceName)
{
    const ScmHandle scm;
    if (!scm.isValid())
        return QStringLiteral("无法访问服务管理器");

    const ServiceHandle service(scm.get(), toWide(serviceName),
                                SERVICE_QUERY_STATUS);
    if (!service.isValid())
        return QStringLiteral("未安装");

    SERVICE_STATUS_PROCESS status{};
    if (!queryServiceStatus(service.get(), status))
        return QStringLiteral("状态未知");

    switch (status.dwCurrentState) {
    case SERVICE_STOPPED:
        return QStringLiteral("已停止");
    case SERVICE_START_PENDING:
        return QStringLiteral("正在启动");
    case SERVICE_STOP_PENDING:
        return QStringLiteral("正在停止");
    case SERVICE_RUNNING:
        return QStringLiteral("运行中");
    case SERVICE_CONTINUE_PENDING:
        return QStringLiteral("正在恢复");
    case SERVICE_PAUSE_PENDING:
        return QStringLiteral("正在暂停");
    case SERVICE_PAUSED:
        return QStringLiteral("已暂停");
    default:
        return QStringLiteral("未知状态 %1").arg(status.dwCurrentState);
    }
}

// ===========================================================================
// 进程
// ===========================================================================
bool ProcessManager::isProcessRunning(const QString &processName)
{
    if (processName.trimmed().isEmpty())
        return false;

    HANDLE snapshot = ::CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE)
        return false;

    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);

    bool found = false;
    if (::Process32FirstW(snapshot, &entry)) {
        do {
            if (processNameMatches(QString::fromWCharArray(entry.szExeFile),
                                   processName)) {
                found = true;
                break;
            }
        } while (::Process32NextW(snapshot, &entry));
    }

    ::CloseHandle(snapshot);
    return found;
}

bool ProcessManager::killProcessByName(const QString &name, int waitMs)
{
    if (!isProcessRunning(name)) {
        emit logMessage(QStringLiteral("进程未运行，跳过: %1").arg(name));
        return true;
    }

    emit logMessage(QStringLiteral("正在结束进程: %1").arg(name));

    // 收集所有同名 PID 后逐个 TerminateProcess。
    // 旧版调用 taskkill（/T 结束整棵进程树），失败再退回 wmic；
    // wmic 已被微软弃用，这里改为原生 API，并且同样会处理
    // 「父进程被结束后子进程仍在」的常见情况（下一轮循环会再收一次）。
    int terminated = 0;
    for (int round = 0; round < 3; ++round) {
        HANDLE snapshot = ::CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snapshot == INVALID_HANDLE_VALUE)
            break;

        QList<DWORD> pids;
        PROCESSENTRY32W entry{};
        entry.dwSize = sizeof(entry);
        if (::Process32FirstW(snapshot, &entry)) {
            do {
                if (processNameMatches(QString::fromWCharArray(entry.szExeFile),
                                       name))
                    pids.append(entry.th32ProcessID);
            } while (::Process32NextW(snapshot, &entry));
        }
        ::CloseHandle(snapshot);

        if (pids.isEmpty())
            break;

        for (DWORD pid : pids) {
            if (pid == ::GetCurrentProcessId())
                continue;   // 绝不误杀自己
            const ProcessHandle process(::OpenProcess(
                PROCESS_TERMINATE | SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION,
                FALSE, pid));
            if (!process.isValid())
                continue;
            if (::TerminateProcess(process.get(), static_cast<UINT>(-1)))
                ++terminated;
        }

        if (!isProcessRunning(name))
            break;
        QThread::msleep(300);
    }

    emit cleanupProgress(QStringLiteral("wait_process"), name, 0, 0);
    if (waitProcessExit(name, waitMs)) {
        emit logMessage(QStringLiteral("进程已结束: %1（终止 %2 个实例）")
                            .arg(name)
                            .arg(terminated));
        return true;
    }

    emit logMessage(QStringLiteral("警告：进程未能结束: %1").arg(name));
    return false;
}

bool ProcessManager::waitProcessExit(const QString &name, int timeoutMs)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeoutMs) {
        if (!isProcessRunning(name))
            return true;
        QThread::msleep(300);
    }
    return !isProcessRunning(name);
}
