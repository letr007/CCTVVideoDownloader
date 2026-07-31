#include "cli_output.h"

#include "downloadjob.h"

#include <QJsonDocument>
#include <QJsonObject>

#ifdef Q_OS_WIN
#include <io.h>
#else
#include <unistd.h>
#endif

namespace {

bool isTerminal(FILE* stream)
{
#ifdef Q_OS_WIN
    return _isatty(_fileno(stream)) != 0;
#else
    return isatty(fileno(stream)) != 0;
#endif
}

} // namespace

namespace Cli {

Output::Output(bool json)
    : Output(json, stdout, stderr, isTerminal(stdout))
{
}

Output::Output(bool json, FILE* stdoutDevice, FILE* stderrDevice, bool stdoutIsTerminal)
    : m_json(json)
    , m_interactiveProgress(!json && stdoutIsTerminal)
    , m_stdout(stdoutDevice)
    , m_stderr(stderrDevice)
{
}

void Output::video(int index, const QString& guid, const QString& title, const QString& time,
    const QString& channel, const QString& image, const QString& brief, qint64 length, bool isHighlight,
    const QString& listType)
{
    if (m_json) {
        stdoutLine(videoItemJson(index, guid, title, time, channel, image, brief, length, isHighlight, listType));
    } else {
        stdoutLine(QStringLiteral("%1. %2 [%3]").arg(index + 1).arg(title, guid));
    }
}

void Output::listComplete(int count)
{
    if (m_json) {
        jsonLine({{QStringLiteral("event"), QStringLiteral("list_complete")}, {QStringLiteral("count"), count}});
    }
}

void Output::resolutionFailed(const QString& message)
{
    if (m_json) {
        jsonLine({{QStringLiteral("event"), QStringLiteral("resolution_failed")}, {QStringLiteral("error"), message}});
    } else {
        stderrLine(QStringLiteral("resolution failed: %1").arg(message));
    }
}

void Output::jobChanged(const DownloadJob& job)
{
    if (m_json) {
        jsonLine({{QStringLiteral("event"), QStringLiteral("job")}, {QStringLiteral("title"), job.request.videoTitle},
            {QStringLiteral("state"), stateName(job.state)}, {QStringLiteral("progress"), job.progressPercent}});
    } else {
        const QString line = QStringLiteral("%1: %2%").arg(job.request.videoTitle).arg(job.progressPercent);
        if (m_interactiveProgress) {
            m_stdout << "\r\x1b[2K" << line << Qt::flush;
            m_progressVisible = true;
        } else {
            stdoutLine(line);
        }
    }
}

void Output::jobFinished(const DownloadJob& job)
{
    if (m_json) {
        QJsonObject event{{QStringLiteral("event"), QStringLiteral("job_finished")},
            {QStringLiteral("title"), job.request.videoTitle}, {QStringLiteral("state"), stateName(job.state)},
            {QStringLiteral("progress"), job.progressPercent}};
        if (job.state == DownloadJobState::Failed) {
            event.insert(QStringLiteral("error"), job.errorMessage);
            event.insert(QStringLiteral("category"), errorCategoryName(job.errorCategory));
        }
        jsonLine(event);
    } else {
        finishProgressLine();
        if (job.state == DownloadJobState::Failed) {
            stderrLine(QStringLiteral("download failed: %1 (%2): %3").arg(job.request.videoTitle,
                errorCategoryName(job.errorCategory), job.errorMessage));
        }
    }
}

void Output::downloadComplete(int completed, int failed, int cancelled, int total)
{
    if (m_json) {
        jsonLine({{QStringLiteral("event"), QStringLiteral("download_complete")}, {QStringLiteral("completed"), completed},
            {QStringLiteral("failed"), failed}, {QStringLiteral("cancelled"), cancelled}, {QStringLiteral("total"), total}});
    } else {
        stdoutLine(QStringLiteral("completed: %1, failed: %2, cancelled: %3 / %4").arg(completed).arg(failed).arg(cancelled).arg(total));
    }
}

void Output::downloadStartFailed()
{
    if (m_json) {
        jsonLine({{QStringLiteral("event"), QStringLiteral("download_start_failed")},
            {QStringLiteral("error"), QStringLiteral("unable to start jobs")}});
    } else {
        stderrLine(QStringLiteral("download failed: unable to start jobs"));
    }
}

void Output::usageError(const QString& message)
{
    if (m_json) {
        jsonLine({{QStringLiteral("event"), QStringLiteral("usage_error")}, {QStringLiteral("error"), message}});
    } else {
        stderrLine(QStringLiteral("error: %1").arg(message));
    }
}

void Output::jsonLine(const QJsonObject& object)
{
    stdoutLine(QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact)));
}

void Output::stdoutLine(const QString& line)
{
    finishProgressLine();
    m_stdout << line << Qt::endl;
}

void Output::stderrLine(const QString& line)
{
    finishProgressLine();
    m_stderr << line << Qt::endl;
}

void Output::finishProgressLine()
{
    if (m_progressVisible) {
        m_stdout << Qt::endl;
        m_progressVisible = false;
    }
}

QString stateName(DownloadJobState state)
{
    switch (state) {
    case DownloadJobState::Created: return QStringLiteral("created");
    case DownloadJobState::Queued: return QStringLiteral("queued");
    case DownloadJobState::ResolvingM3u8: return QStringLiteral("resolving");
    case DownloadJobState::Downloading: return QStringLiteral("downloading");
    case DownloadJobState::Concatenating: return QStringLiteral("concatenating");
    case DownloadJobState::Decrypting: return QStringLiteral("decrypting");
    case DownloadJobState::DirectFinalizing: return QStringLiteral("finalizing");
    case DownloadJobState::Completed: return QStringLiteral("completed");
    case DownloadJobState::Failed: return QStringLiteral("failed");
    case DownloadJobState::Cancelled: return QStringLiteral("cancelled");
    }
    return QStringLiteral("unknown");
}

QString errorCategoryName(DownloadErrorCategory category)
{
    switch (category) {
    case DownloadErrorCategory::NetworkError: return QStringLiteral("network_error");
    case DownloadErrorCategory::Timeout: return QStringLiteral("timeout");
    case DownloadErrorCategory::ServerError: return QStringLiteral("server_error");
    case DownloadErrorCategory::DecryptError: return QStringLiteral("decrypt_error");
    case DownloadErrorCategory::FileSystemError: return QStringLiteral("filesystem_error");
    case DownloadErrorCategory::ValidationError: return QStringLiteral("validation_error");
    case DownloadErrorCategory::Cancelled: return QStringLiteral("cancelled");
    case DownloadErrorCategory::Unknown: return QStringLiteral("unknown");
    }
    return QStringLiteral("unknown");
}

} // namespace Cli
