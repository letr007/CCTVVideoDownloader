#include "../head/logger.h"
#include <QDebug>
#include <QDir>

Logger* Logger::m_instance = nullptr;

Logger::Logger(QObject* parent) : QObject(parent), m_currentLogLevel(1) // 默认INFO级别
{
}

Logger::~Logger()
{
    if (m_logFile.isOpen()) {
        m_logFile.close();
    }
}

Logger* Logger::instance()
{
    static QMutex mutex;
    if (!m_instance) {
        QMutexLocker locker(&mutex);
        if (!m_instance) {
            m_instance = new Logger();
        }
    }
    return m_instance;
}

void Logger::init(const QString& logFilePath)
{
    // 设置日志文件
    m_logFile.setFileName(logFilePath);
    if (!m_logFile.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        qWarning() << "无法打开日志文件:" << logFilePath;
        return;
    }

    m_textStream.setDevice(&m_logFile);

    // 安装消息处理器
    qInstallMessageHandler(messageHandler);

    qInfo() << "日志类初始化完成";
}

void Logger::cleanup()
{
    qInstallMessageHandler(nullptr);
    if (m_instance) {
        delete m_instance;
        m_instance = nullptr;
    }
}

void Logger::messageHandler(QtMsgType type, const QMessageLogContext& context, const QString& msg)
{
    QMutexLocker locker(&m_instance->m_mutex);

    // 检查是否应该记录此级别的日志
    if (!m_instance->shouldLog(type)) {
        return;
    }

    QString level;
    switch (type) {
    case QtDebugMsg:
        level = "DEBUG";
        break;
    case QtInfoMsg:
        level = "INFO";
        break;
    case QtWarningMsg:
        level = "WARNING";
        break;
    case QtCriticalMsg:
        level = "CRITICAL";
        break;
    case QtFatalMsg:
        level = "FATAL";
        break;
    }

    QString timestamp = QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss.zzz");
    QString logMessage = QString("[%1] [%2] %3").arg(timestamp, level, msg);

    // 输出到控制台
    m_instance->outputToConsole(logMessage);

    // 写入文件
    m_instance->writeToFile(logMessage);

    // 如果是致命错误，终止程序
    if (type == QtFatalMsg) {
        abort();
    }
}

void Logger::writeToFile(const QString& message)
{
    if (m_logFile.isOpen()) {
        m_textStream << message << "\n";
        m_textStream.flush();
    }
}

void Logger::outputToConsole(const QString& message)
{
    // 使用标准输出
    fprintf(stdout, "%s\n", message.toLocal8Bit().constData());
    fflush(stdout);
}

void Logger::setLogLevel(const int& level)
{
    QMutexLocker locker(&m_mutex);
    m_currentLogLevel = level;
    // 这里不能使用qInfo()，会导致死锁
}

bool Logger::shouldLog(QtMsgType type) const
{
    int messageLevel;
    switch (type) {
    case QtDebugMsg:
        messageLevel = 0;
        break;
    case QtInfoMsg:
        messageLevel = 1;
        break;
    case QtWarningMsg:
        messageLevel = 2;
        break;
    case QtCriticalMsg:
        messageLevel = 3;
        break;
    case QtFatalMsg:
        messageLevel = 4;
        break;
    default:
        messageLevel = 1; // 默认为INFO级别
        break;
    }

    // 如果配置的日志级别小于等于消息级别，则记录
    // DEBUG(0): 记录所有级别
    // INFO(1): 记录INFO(1)、WARNING(2)、CRITICAL(3)、FATAL(4)
    // WARNING(2): 记录WARNING(2)、CRITICAL(3)、FATAL(4)
    // CRITICAL(3): 记录CRITICAL(3)、FATAL(4)
    // FATAL(4): 只记录FATAL(4)
    return messageLevel >= m_currentLogLevel;
}

int Logger::logLevelToInt(const QString& level) const
{
    if (level.toUpper() == "DEBUG") {
        return 0;
    } else if (level.toUpper() == "INFO") {
        return 1;
    } else if (level.toUpper() == "WARNING") {
        return 2;
    } else if (level.toUpper() == "CRITICAL") {
        return 3;
    } else if (level.toUpper() == "FATAL") {
        return 4;
    } else {
        // 默认使用INFO级别
        return 1;
    }
}