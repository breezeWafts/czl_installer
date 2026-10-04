#include "Logger.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QDebug>
#include <cstdio>

Logger &Logger::instance()
{
    static Logger s;
    return s;
}

Logger::~Logger()
{
    shutdown();
}

// ============================================================
// 初始化
// ============================================================
void Logger::init(const QString &baseDir)
{
    // ---------- 加锁的临界区：只做路径/文件初始化 ----------
    {
        QMutexLocker locker(&m_mutex);

        if (m_initialized) return;

        QString base = baseDir.trimmed();
        if (base.isEmpty()) base = "D:/c_temp";

        m_logDir  = QDir::toNativeSeparators(base + "/log");
        QDir().mkpath(m_logDir);

        QString date = QDateTime::currentDateTime().toString("yyyyMMdd");
        m_logFile = QDir::toNativeSeparators(
            m_logDir + "/czl_installer_" + date + ".log");

        m_file.setFileName(m_logFile);
        if (!m_file.open(QIODevice::WriteOnly |
                         QIODevice::Append   |
                         QIODevice::Text)) {
            fprintf(stderr, "无法打开日志文件: %s\n",
                    m_logFile.toUtf8().constData());
            return;
        }

        m_initialized = true;
    }   // ← 这里释放锁

    // ---------- 锁外：安装 handler + 写启动日志 ----------
    qInstallMessageHandler(&Logger::messageHandler);

    QString line = QString("========== czl_installer 启动 %1 ==========")
                       .arg(QDateTime::currentDateTime()
                                .toString("yyyy-MM-dd HH:mm:ss"));
    write("INFO", line);
    write("INFO", QString("日志文件: %1").arg(m_logFile));
}

// ============================================================
// 关闭
// ============================================================
void Logger::shutdown()
{
    {
        QMutexLocker locker(&m_mutex);

        if (m_file.isOpen()) {
            QString time = QDateTime::currentDateTime()
                               .toString("yyyy-MM-dd HH:mm:ss");
            QString line = QString("[%1] [INFO] ========== czl_installer 退出 ==========\n")
                               .arg(time);
            m_file.write(line.toUtf8());
            m_file.flush();
            m_file.close();
        }

        m_initialized = false;
    }   // ← 释放锁

    qInstallMessageHandler(nullptr);
}

// ============================================================
// 写入一条日志
// ============================================================
void Logger::write(const QString &level, const QString &message)
{
    QMutexLocker locker(&m_mutex);

    if (!m_file.isOpen()) return;

    QString time = QDateTime::currentDateTime()
                       .toString("yyyy-MM-dd HH:mm:ss.zzz");
    QString line = QString("[%1] [%2] %3\n")
                       .arg(time, level, message);

    m_file.write(line.toUtf8());
    m_file.flush();
}

// ============================================================
// 静态封装
// ============================================================
void Logger::info (const QString &msg) { instance().write("INFO",  msg); }
void Logger::warn (const QString &msg) { instance().write("WARN",  msg); }
void Logger::error(const QString &msg) { instance().write("ERROR", msg); }
void Logger::debug(const QString &msg) { instance().write("DEBUG", msg); }

// ============================================================
// 全局消息处理器：qDebug / qInfo / qWarning / qCritical 自动落盘
// ============================================================
void Logger::messageHandler(QtMsgType type,
                             const QMessageLogContext &ctx,
                             const QString &msg)
{
    Q_UNUSED(ctx);

    QString level;
    switch (type) {
    case QtDebugMsg:    level = "DEBUG"; break;
    case QtInfoMsg:     level = "INFO";  break;
    case QtWarningMsg:  level = "WARN";  break;
    case QtCriticalMsg: level = "ERROR"; break;
    case QtFatalMsg:    level = "FATAL"; break;
    }

    // 写文件
    Logger::instance().write(level, msg);

    // Debug 模式下同时在控制台输出，方便排查
#ifdef QT_DEBUG
    fprintf(stderr, "[%s] %s\n",
            level.toUtf8().constData(),
            msg.toUtf8().constData());
#endif
}
