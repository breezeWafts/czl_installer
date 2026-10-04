#include "ProcessManager.h"
#include <QProcess>
#include <QThread>
#include <QElapsedTimer>
#include <QDebug>
#include <windows.h>

ProcessManager::ProcessManager(QObject *parent) : QObject(parent) {}

// ============================================================
// 批量
// ============================================================
bool ProcessManager::stopServices(const QStringList &names, int waitMsPerService)
{
    if (names.isEmpty()) return true;
    bool allOk = true;
    const int total = names.size();

    for (int i = 0; i < total; ++i) {
        emit cleanupProgress("stop_service", names[i], i + 1, total);
        if (!stopServiceByName(names[i], waitMsPerService))
            allOk = false;
    }
    return allOk;
}

bool ProcessManager::killProcesses(const QStringList &names, int waitMsPerProcess)
{
    if (names.isEmpty()) return true;
    bool allOk = true;
    const int total = names.size();

    for (int i = 0; i < total; ++i) {
        emit cleanupProgress("kill_process", names[i], i + 1, total);
        if (!killProcessByName(names[i], waitMsPerProcess))
            allOk = false;
    }
    return allOk;
}

// ============================================================
// 单个：停止服务
// ============================================================
bool ProcessManager::stopServiceByName(const QString &name, int waitMs)
{
    if (!serviceExists(name)) {
        emit logMessage(QString("服务不存在，跳过: %1").arg(name));
        return true;
    }
    if (isServiceStopped(name)) {
        emit logMessage(QString("服务已停止: %1").arg(name));
        return true;
    }

    emit logMessage(QString("正在停止服务: %1").arg(name));
    QProcess stop;
    stop.start("sc", QStringList() << "stop" << name);
    stop.waitForFinished(15000);

    emit cleanupProgress("wait_service", name, 0, 0);
    if (waitServiceStopped(name, waitMs)) {
        emit logMessage(QString("服务已停止: %1").arg(name));
        return true;
    }

    // 卡在 PENDING：查 PID 强杀
    if (isServicePending(name)) {
        emit logMessage(QString("服务卡在 PENDING，尝试强杀宿主进程: %1").arg(name));
        QProcess q;
        q.start("sc", QStringList() << "queryex" << name);
        q.waitForFinished(5000);
        QString qout = QString::fromLocal8Bit(q.readAllStandardOutput());
        int idx = qout.indexOf("PID");
        if (idx >= 0) {
            QString line = qout.mid(idx).section('\n', 0, 0);
            QString pidStr = line.section(':', 1).trimmed();
            bool okNum = false;
            DWORD pid = pidStr.toULong(&okNum);
            if (okNum && pid > 0) {
                QProcess kill;
                kill.start("taskkill",
                           QStringList() << "/F" << "/PID" << QString::number(pid));
                kill.waitForFinished(8000);
                QThread::msleep(800);
            }
        }
        if (isServiceStopped(name)) {
            emit logMessage(QString("服务已强制停止: %1").arg(name));
            return true;
        }
    }

    emit logMessage(QString("警告：服务未能停止: %1").arg(name));
    return false;
}

// ============================================================
// 单个：终止进程
// ============================================================
bool ProcessManager::killProcessByName(const QString &name, int waitMs)
{
    if (!isProcessRunning(name)) {
        emit logMessage(QString("进程未运行，跳过: %1").arg(name));
        return true;
    }

    emit logMessage(QString("正在终止进程: %1").arg(name));
    QProcess kill;
    kill.start("taskkill", QStringList() << "/F" << "/T" << "/IM" << name);
    kill.waitForFinished(15000);

    emit cleanupProgress("wait_process", name, 0, 0);
    if (waitProcessExit(name, waitMs)) {
        emit logMessage(QString("进程已终止: %1").arg(name));
        return true;
    }

    // 兜底：wmic
    emit logMessage(QString("常规方式未终止，兜底处理: %1").arg(name));
    QProcess wmic;
    wmic.start("wmic",
               QStringList() << "process" << "where"
                             << QString("name='%1'").arg(name)
                             << "call" << "terminate");
    wmic.waitForFinished(8000);

    if (waitProcessExit(name, 5000)) {
        emit logMessage(QString("进程已终止(兜底): %1").arg(name));
        return true;
    }

    emit logMessage(QString("警告：进程未能终止: %1").arg(name));
    return false;
}

// ============================================================
// 状态检查
// ============================================================
bool ProcessManager::isProcessRunning(const QString &name)
{
    QProcess p;
    p.start("tasklist",
            QStringList() << "/FI" << "IMAGENAME eq " + name
                          << "/NH" << "/FO" << "CSV");
    if (!p.waitForFinished(5000)) return false;
    QString out = QString::fromLocal8Bit(p.readAllStandardOutput());

    if (out.contains("No tasks", Qt::CaseInsensitive) ||
        out.contains("没有运行的任务", Qt::CaseInsensitive))
        return false;

    return out.contains("\"" + name + "\"", Qt::CaseInsensitive);
}

bool ProcessManager::serviceExists(const QString &name)
{
    QProcess p;
    p.start("sc", QStringList() << "query" << name);
    p.waitForFinished(5000);
    QString err = QString::fromLocal8Bit(p.readAllStandardError());
    QString out = QString::fromLocal8Bit(p.readAllStandardOutput());

    if (err.contains("1060") ||
        err.contains("does not exist", Qt::CaseInsensitive) ||
        err.contains("指定的服务未安装", Qt::CaseInsensitive))
        return false;

    return p.exitCode() == 0 && out.contains("SERVICE_NAME", Qt::CaseInsensitive);
}

bool ProcessManager::isServiceStopped(const QString &name)
{
    QProcess p;
    p.start("sc", QStringList() << "query" << name);
    p.waitForFinished(5000);
    QString out = QString::fromLocal8Bit(p.readAllStandardOutput());

    int idx = out.indexOf("STATE", 0, Qt::CaseInsensitive);
    if (idx < 0) return false;
    QString line = out.mid(idx).section('\n', 0, 0).toUpper();
    return line.contains("STOPPED") && !line.contains("PENDING");
}

bool ProcessManager::isServicePending(const QString &name)
{
    QProcess p;
    p.start("sc", QStringList() << "query" << name);
    p.waitForFinished(5000);
    QString out = QString::fromLocal8Bit(p.readAllStandardOutput());
    int idx = out.indexOf("STATE", 0, Qt::CaseInsensitive);
    if (idx < 0) return false;
    return out.mid(idx).section('\n', 0, 0).toUpper().contains("PENDING");
}

bool ProcessManager::waitProcessExit(const QString &name, int timeoutMs)
{
    QElapsedTimer t; t.start();
    while (t.elapsed() < timeoutMs) {
        if (!isProcessRunning(name)) return true;
        QThread::msleep(300);
    }
    return !isProcessRunning(name);
}

bool ProcessManager::waitServiceStopped(const QString &name, int timeoutMs)
{
    QElapsedTimer t; t.start();
    while (t.elapsed() < timeoutMs) {
        if (isServiceStopped(name)) return true;
        QThread::msleep(500);
    }
    return isServiceStopped(name);
}
