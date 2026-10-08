#include "Logger.h"

#include <QDateTime>
#include <QDir>
#include <QMutexLocker>

#include <cstdio>

namespace {

/// 把 QtMsgType 映射成日志级别文本。
const char *levelOf(QtMsgType type)
{
    switch (type) {
    case QtDebugMsg:
        return "DEBUG";
    case QtInfoMsg:
        return "INFO";
    case QtWarningMsg:
        return "WARN";
    case QtCriticalMsg:
        return "ERROR";
    case QtFatalMsg:
        return "FATAL";
    }
    return "INFO";
}

} // namespace

Logger &Logger::instance()
{
    static Logger logger;
    return logger;
}

Logger::~Logger()
{
    shutdown();
}

// ---------------------------------------------------------------------------
// 初始化 / 关闭
// ---------------------------------------------------------------------------
void Logger::init(const QString &baseDir)
{
    QMutexLocker locker(&m_mutex);

    QString base = baseDir.trimmed();
    if (base.isEmpty())
        base = QStringLiteral("D:/c_temp");

    const QString logDir = QDir::toNativeSeparators(base + QStringLiteral("/log"));
    const QString fileName = QStringLiteral("czl_installer_%1.log")
                                 .arg(QDateTime::currentDateTime()
                                          .toString(QStringLiteral("yyyyMMdd")));
    const QString logFile =
        QDir::toNativeSeparators(logDir + QLatin1Char('/') + fileName);

    // 目录与文件名都没变，且文件已经打开：直接复用
    if (m_initialized && m_logFile == logFile && m_file.isOpen())
        return;

    // 切换目录：先关掉旧文件
    if (m_file.isOpen()) {
        m_file.flush();
        m_file.close();
    }
    m_initialized = false;

    if (!QDir().mkpath(logDir)) {
        std::fprintf(stderr, "[Logger] 无法创建日志目录: %s\n",
                     logDir.toUtf8().constData());
        return;
    }

    m_logDir  = logDir;
    m_logFile = logFile;

    m_file.setFileName(m_logFile);
    // 不加 QIODevice::Text：Windows 下会把 \n 变成 \r\n，
    // 而我们自己已显式写 \n；保持二进制+UTF-8 更可控。
    if (!m_file.open(QIODevice::WriteOnly | QIODevice::Append)) {
        std::fprintf(stderr, "[Logger] 无法打开日志文件: %s (%s)\n",
                     m_logFile.toUtf8().constData(),
                     m_file.errorString().toUtf8().constData());
        return;
    }

    m_initialized = true;

#ifdef QT_DEBUG
    m_consoleOutput = true;
#endif

    const QString banner = QStringLiteral("========== czl_installer 启动 %1 ==========")
                               .arg(QDateTime::currentDateTime()
                                        .toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")));
    writeLocked(QStringLiteral("INFO"), banner);
    writeLocked(QStringLiteral("INFO"),
                QStringLiteral("日志文件: %1").arg(m_logFile));

    // 必须在锁外安装处理器，避免消息处理器与 init() 相互等待
    locker.unlock();
    qInstallMessageHandler(&Logger::messageHandler);
}

void Logger::shutdown()
{
    bool wasInitialized = false;
    {
        QMutexLocker locker(&m_mutex);
        if (!m_file.isOpen()) {
            m_initialized = false;
            return;
        }

        writeLocked(QStringLiteral("INFO"),
                    QStringLiteral("========== czl_installer 退出 =========="));
        m_file.flush();
        m_file.close();
        m_initialized = false;
        wasInitialized = true;
    }

    if (wasInitialized)
        qInstallMessageHandler(nullptr);
}

// ---------------------------------------------------------------------------
// 写日志
// ---------------------------------------------------------------------------
void Logger::writeLocked(const QString &level, const QString &message)
{
    if (!m_file.isOpen())
        return;

    const QString timestamp = QDateTime::currentDateTime()
                                  .toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz"));
    const QString line = QStringLiteral("[%1] [%2] %3\n")
                             .arg(timestamp, level.leftJustified(5), message);

    m_file.write(line.toUtf8());
    m_file.flush();
}

void Logger::write(const QString &level, const QString &message)
{
    QMutexLocker locker(&m_mutex);
    writeLocked(level, message);
}

QString Logger::logDir() const
{
    QMutexLocker locker(&m_mutex);
    return m_logDir;
}

QString Logger::logFile() const
{
    QMutexLocker locker(&m_mutex);
    return m_logFile;
}

bool Logger::isInitialized() const
{
    QMutexLocker locker(&m_mutex);
    return m_initialized && m_file.isOpen();
}

void Logger::setConsoleOutputEnabled(bool enabled)
{
    QMutexLocker locker(&m_mutex);
    m_consoleOutput = enabled;
}

// ---------------------------------------------------------------------------
// 静态便捷方法
// ---------------------------------------------------------------------------
void Logger::info(const QString &message)  { instance().write(QStringLiteral("INFO"),  message); }
void Logger::warn(const QString &message)  { instance().write(QStringLiteral("WARN"),  message); }
void Logger::error(const QString &message) { instance().write(QStringLiteral("ERROR"), message); }
void Logger::debug(const QString &message) { instance().write(QStringLiteral("DEBUG"), message); }

// ---------------------------------------------------------------------------
// Qt 消息处理器
// ---------------------------------------------------------------------------
void Logger::messageHandler(QtMsgType type,
                            const QMessageLogContext &context,
                            const QString &message)
{
    Logger &self = instance();

    QString text = message;
    // 非 default 日志类别加上前缀，便于过滤
    if (context.category != nullptr
        && qstrcmp(context.category, "default") != 0) {
        text = QStringLiteral("[%1] %2")
                   .arg(QString::fromUtf8(context.category), message);
    }

    bool console = false;
    {
        // 使用递归锁：即使下面的 fprintf/写盘再次触发日志也不会死锁。
        QMutexLocker locker(&self.m_mutex);
        self.writeLocked(QString::fromLatin1(levelOf(type)), text);
        console = self.m_consoleOutput;
    }

    // 刻意使用 fprintf 而不是 qDebug()：在消息处理器里再调用 qDebug
    // 会造成无限递归（旧版在 QT_DEBUG 下正是如此）。
    if (console) {
        std::fprintf(stderr, "[%s] %s\n",
                     levelOf(type), text.toUtf8().constData());
    }
}
