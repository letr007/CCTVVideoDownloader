#include "../../include/logger.h"
#include "../../include/config.h"
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>
#include <QMutexLocker>
#include <iostream>
#include <atomic>

namespace {
// Fast path for message handler after cleanup without needing the singleton mutex.
std::atomic_bool g_loggerActive{false};
}

Logger* Logger::m_instance = nullptr;

Logger::Logger(QObject* parent)
	: QObject(parent)
	, m_currentLogLevel(1) // 默认INFO级别
{
}

Logger::~Logger()
{
	// Never re-enter cleanup()/delete from the destructor.
	g_loggerActive.store(false, std::memory_order_release);
	qInstallMessageHandler(nullptr);
	detachStreamAndCloseFile();
}

Logger* Logger::instance()
{
	if (!m_instance) {
		m_instance = new Logger();
	}
	return m_instance;
}

void Logger::detachStreamAndCloseFile()
{
	// Order matters: QTextStream must not outlive / keep using a closed QFile.
	m_textStream.flush();
	m_textStream.setDevice(nullptr);
	if (m_logFile.isOpen()) {
		m_logFile.close();
	}
}

void Logger::init(const QString& logFilePath)
{
	QMutexLocker locker(&m_mutex);

	detachStreamAndCloseFile();

	QString filePath = logFilePath;
	if (filePath.isEmpty() || filePath == QStringLiteral("app.log")
		|| !QFileInfo(filePath).isAbsolute()) {
		const QString appDataPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
		QDir().mkpath(appDataPath);
		const QString fileName = (filePath.isEmpty() || filePath == QStringLiteral("app.log"))
			? QStringLiteral("app.log")
			: QFileInfo(filePath).fileName();
		filePath = QDir(appDataPath).filePath(fileName);
	}

	m_logFile.setFileName(filePath);
	if (!m_logFile.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
		g_loggerActive.store(false, std::memory_order_release);
		return;
	}

	m_textStream.setDevice(&m_logFile);

	const int level = readLogLevel();
	m_currentLogLevel = level;

	g_loggerActive.store(true, std::memory_order_release);
	qInstallMessageHandler(messageHandler);

	// Log after handler install (and outside nested setLogLevel lock).
	locker.unlock();
	qInfo() << "日志类初始化完成";
	qInfo() << "读取日志级别配置";
	qInfo() << "日志级别:" << level;
}

void Logger::cleanup()
{
	// Make the message handler a no-op before touching file state.
	g_loggerActive.store(false, std::memory_order_release);
	qInstallMessageHandler(nullptr);

	QMutexLocker locker(&m_mutex);
	detachStreamAndCloseFile();
	// Intentionally keep the singleton alive until process exit.
	// Deleting it during aboutToQuit races with other QObject teardown.
}

void Logger::setLogLevel(const int& level)
{
	QMutexLocker locker(&m_mutex);
	m_currentLogLevel = level;
}

bool Logger::shouldLog(QtMsgType type) const
{
	int messageLevel = 0;
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
	}
	return messageLevel >= m_currentLogLevel;
}

void Logger::messageHandler(QtMsgType type, const QMessageLogContext& context, const QString& msg)
{
	Q_UNUSED(context);

	if (!g_loggerActive.load(std::memory_order_acquire)) {
		// Fallback: still print fatal to stderr.
		if (type == QtFatalMsg) {
			std::cerr << msg.toStdString() << std::endl;
			abort();
		}
		return;
	}

	Logger* logger = m_instance;
	if (!logger || !logger->shouldLog(type)) {
		if (type == QtFatalMsg) {
			abort();
		}
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

	const QString timestamp = QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss.zzz");
	const QString logMessage = QString("[%1] [%2] %3").arg(timestamp, level, msg);

	logger->writeToFile(logMessage);
	logger->outputToConsole(logMessage);

	if (type == QtFatalMsg) {
		abort();
	}
}

void Logger::writeToFile(const QString& message)
{
	QMutexLocker locker(&m_mutex);
	if (!m_logFile.isOpen() || m_textStream.device() == nullptr) {
		return;
	}
	m_textStream << message << Qt::endl;
	m_textStream.flush();
}

void Logger::outputToConsole(const QString& message)
{
	std::cout << message.toStdString() << std::endl;
}
