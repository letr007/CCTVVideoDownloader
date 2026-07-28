#include "cli_logging.h"

#include <QLoggingCategory>
#include <QMutex>
#include <QMutexLocker>
#include <QTextStream>

#include <cstdarg>
#include <cstdio>

extern "C" {
#include <libavutil/log.h>
}

namespace {

QMutex messageHandlerMutex;
bool debugLoggingEnabled = false;

const char* levelName(QtMsgType type)
{
    switch (type) {
    case QtDebugMsg: return "DEBUG";
    case QtInfoMsg: return "INFO";
    case QtWarningMsg: return "WARNING";
    case QtCriticalMsg: return "CRITICAL";
    case QtFatalMsg: return "FATAL";
    }
    return "UNKNOWN";
}

void cliMessageHandler(QtMsgType type, const QMessageLogContext&, const QString& message)
{
    QMutexLocker locker(&messageHandlerMutex);
    if (!debugLoggingEnabled && type != QtFatalMsg) {
        return;
    }

    QTextStream stream(stderr);
    stream << levelName(type) << ": " << message << Qt::endl;
}

void ffmpegLogCallback(void*, int level, const char* format, va_list arguments)
{
    if (format == nullptr) {
        return;
    }

    va_list argumentsCopy;
    va_copy(argumentsCopy, arguments);
    const int messageLength = std::vsnprintf(nullptr, 0, format, argumentsCopy);
    va_end(argumentsCopy);
    if (messageLength <= 0) {
        return;
    }

    QByteArray formattedMessage(messageLength + 1, Qt::Uninitialized);
    std::vsnprintf(formattedMessage.data(), static_cast<size_t>(formattedMessage.size()), format, arguments);
    const QString message = QString::fromUtf8(formattedMessage.constData(), messageLength).trimmed();
    if (message.isEmpty()) {
        return;
    }

    const int severity = level & 0xff;
    if (severity <= AV_LOG_ERROR) {
        qCritical().noquote() << "FFmpeg:" << message;
    } else if (severity == AV_LOG_WARNING) {
        qWarning().noquote() << "FFmpeg:" << message;
    } else if (severity == AV_LOG_INFO) {
        qInfo().noquote() << "FFmpeg:" << message;
    } else {
        qDebug().noquote() << "FFmpeg:" << message;
    }
}

} // namespace

namespace Cli {

void installQtMessageHandler()
{
    qInstallMessageHandler(cliMessageHandler);
}

void installFfmpegLogCallback()
{
    av_log_set_callback(ffmpegLogCallback);
}

void setQtDebugLoggingEnabled(bool enabled)
{
    QMutexLocker locker(&messageHandlerMutex);
    debugLoggingEnabled = enabled;
}

} // namespace Cli
