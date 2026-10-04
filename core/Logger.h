#ifndef LOGGER_H
#define LOGGER_H

#include <QString>
#include <QFile>
#include <QMutex>

class Logger
{
public:
    static Logger &instance();

    // 初始化日志系统：日志目录 = <baseDir>/log/
    // baseDir 通常是 DownloadManager 里的 localBaseDir
    void init(const QString &baseDir);

    // 关闭日志（程序退出时调用）
    void shutdown();

    // 手动写入
    void write(const QString &level, const QString &message);

    // 便捷静态方法
    static void info (const QString &msg);
    static void warn (const QString &msg);
    static void error(const QString &msg);
    static void debug(const QString &msg);

    QString logDir()  const { return m_logDir; }
    QString logFile() const { return m_logFile; }

    // 消息重定向钩子（给 qInstallMessageHandler 用）
    static void messageHandler(QtMsgType type,
                               const QMessageLogContext &ctx,
                               const QString &msg);

private:
    Logger() = default;
    ~Logger();
    Logger(const Logger &) = delete;
    Logger &operator=(const Logger &) = delete;

    QFile   m_file;
    QMutex  m_mutex;      // 保护多线程写入
    QString m_logDir;
    QString m_logFile;
    bool    m_initialized = false;
};

#endif // LOGGER_H
