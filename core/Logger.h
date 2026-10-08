#ifndef LOGGER_H
#define LOGGER_H

#include <QFile>
#include <QMutex>
#include <QString>

/**
 * @brief 简单的线程安全文件日志 + Qt 消息重定向。
 *
 * 相对旧版的修正：
 *   - init() 现在允许用新目录重新初始化（旧版第二次调用直接被忽略，
 *     切换配置文件后日志仍然写进旧目录）；
 *   - shutdown() 里写「退出」标记时同样持锁，不再有数据竞争（旧版在锁外
 *     拼接字符串后调用 write()，而 write() 又去取同一把锁，逻辑绕且易错）；
 *   - 使用 QRecursiveMutex：消息处理器在持锁写盘时若又触发 qWarning，
 *     不会自死锁。
 *
 * 线程约定：installWorker 在后台线程会调用 info/warn/error，因此所有
 * 公开方法都必须线程安全。
 */
class Logger
{
public:
    static Logger &instance();

    /// 初始化：日志目录 = <baseDir>/log；可重复调用以切换目录。
    void init(const QString &baseDir);
    /// 关闭日志（程序退出时调用一次）。
    void shutdown();

    void write(const QString &level, const QString &message);

    static void info(const QString &message);
    static void warn(const QString &message);
    static void error(const QString &message);
    static void debug(const QString &message);

    QString logDir() const;
    QString logFile() const;
    bool isInitialized() const;

    /// 供 qInstallMessageHandler 使用的全局处理器。
    static void messageHandler(QtMsgType type,
                               const QMessageLogContext &context,
                               const QString &message);

    /// 是否把日志同时输出到 stderr（默认仅在 debug 构建下开启）。
    void setConsoleOutputEnabled(bool enabled);

private:
    Logger() = default;
    ~Logger();
    Logger(const Logger &) = delete;
    Logger &operator=(const Logger &) = delete;

    /// 持锁版本，供内部与消息处理器复用。
    void writeLocked(const QString &level, const QString &message);

    mutable QRecursiveMutex m_mutex;
    QFile           m_file;
    QString         m_logDir;
    QString         m_logFile;
    bool            m_initialized = false;
    bool            m_consoleOutput = false;
};

#endif // LOGGER_H
