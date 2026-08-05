#include "cli_controller.h"

#include "config.h"
#include "downloadcoordinator.h"

#include <QCoreApplication>
#include <QTimer>

#include <tuple>
#include <utility>

namespace Cli {

Controller::Controller(QCoreApplication& application, const Options& options)
    : QObject(&application)
    , m_application(application)
    , m_options(options)
    , m_output(options.json)
    , m_api(&APIService::instance())
{
    connect(m_api, &APIService::videoInfoResolved, this, &Controller::handleVideoInfoResolved);
    connect(m_api, &APIService::videoInfoFailed, this, &Controller::handleVideoInfoFailed);
}

void Controller::start()
{
    initGlobalSettings();
    applyConfiguredDefaults();
    if (m_options.command == QStringLiteral("list")) {
        resolveUrl(m_options.url, [this] {
            for (auto it = m_videos.cbegin(); it != m_videos.cend(); ++it) {
                m_output.video(it.key(), it.value().guid, it.value().title, it.value().time, it.value().channel,
                    it.value().image, it.value().brief, it.value().length, it.value().isHighlight, it.value().listType);
            }
            m_output.listComplete(m_videos.size());
            m_application.exit(static_cast<int>(ExitCode::Success));
        });
        return;
    }

    if (!m_options.guid.isEmpty()) {
        QTimer::singleShot(0, this, &Controller::startDownload);
    } else {
        resolveUrl(m_options.url, [this] { startDownload(); });
    }
}

void Controller::cancel()
{
    if (m_cancelled) {
        return;
    }
    m_cancelled = true;
    if (m_coordinator != nullptr) {
        m_coordinator->cancelAll();
    } else {
        m_application.exit(static_cast<int>(ExitCode::Cancelled));
    }
}

void Controller::resolveUrl(const QString& url, const std::function<void()>& done)
{
    const quint64 importId = m_api->startGetPlayColumnInfo(url);
    connect(m_api, &APIService::playColumnInfoResolved, this,
        [this, importId, done](quint64 requestId, const ContentParse::ImportResult& result) {
            if (requestId != importId || m_cancelled) {
                return;
            }
            const quint64 listId = m_api->startGetBrowseVideoList(result, m_from, m_to, m_options.includeHighlights);
            connect(m_api, &APIService::browseVideoListResolved, this,
                [this, listId, done](quint64 completedId, const QMap<int, VideoItem>& videos) {
                    if (completedId != listId || m_cancelled) {
                        return;
                    }
                    m_videos = videos;
                    if (m_options.command == QStringLiteral("list") && m_options.json) {
                        enrichJsonListChannels(done);
                    } else {
                        done();
                    }
                }, Qt::SingleShotConnection);
        }, Qt::SingleShotConnection);
    connect(m_api, &APIService::playColumnInfoFailed, this,
        [this, importId](quint64 requestId, const QString& message) {
            if (requestId != importId || m_cancelled) {
                return;
            }
            m_output.resolutionFailed(message);
            m_application.exit(static_cast<int>(ExitCode::ResolutionFailure));
        }, Qt::SingleShotConnection);
}

void Controller::enrichJsonListChannels(const std::function<void()>& done)
{
    m_pendingJsonChannelIndexes.clear();
    m_pendingJsonChannelPosition = 0;
    m_pendingJsonChannelIndex = -1;
    m_pendingJsonChannelRequestId = 0;
    m_pendingJsonChannelGuid.clear();
    m_pendingJsonListCompletion = done;

    for (auto it = m_videos.cbegin(); it != m_videos.cend(); ++it) {
        if (it.value().channel.trimmed().isEmpty() && !it.value().guid.trimmed().isEmpty()) {
            m_pendingJsonChannelIndexes.append(it.key());
        }
    }

    resolveNextJsonListChannel();
}

void Controller::resolveNextJsonListChannel()
{
    if (m_cancelled) {
        return;
    }

    if (m_pendingJsonChannelPosition >= m_pendingJsonChannelIndexes.size()) {
        const auto done = std::exchange(m_pendingJsonListCompletion, {});
        m_pendingJsonChannelIndexes.clear();
        m_pendingJsonChannelPosition = 0;
        m_pendingJsonChannelIndex = -1;
        m_pendingJsonChannelRequestId = 0;
        m_pendingJsonChannelGuid.clear();
        if (done) {
            done();
        }
        return;
    }

    m_pendingJsonChannelIndex = m_pendingJsonChannelIndexes.at(m_pendingJsonChannelPosition);
    const auto video = m_videos.constFind(m_pendingJsonChannelIndex);
    if (video == m_videos.cend() || !video.value().channel.trimmed().isEmpty()
        || video.value().guid.trimmed().isEmpty()) {
        ++m_pendingJsonChannelPosition;
        QTimer::singleShot(0, this, &Controller::resolveNextJsonListChannel);
        return;
    }

    m_pendingJsonChannelGuid = video.value().guid;
    m_pendingJsonChannelRequestId = m_api->startGetVideoInfo(m_pendingJsonChannelGuid);
}

void Controller::handleVideoInfoResolved(quint64 requestId,
    const QString& guid,
    const QString& channel,
    qint64)
{
    if (requestId != m_pendingJsonChannelRequestId || guid != m_pendingJsonChannelGuid
        || m_pendingJsonChannelIndex < 0) {
        return;
    }

    auto video = m_videos.find(m_pendingJsonChannelIndex);
    if (video != m_videos.end() && video.value().guid == guid) {
        video.value().channel = channel.trimmed();
    }

    ++m_pendingJsonChannelPosition;
    QTimer::singleShot(0, this, &Controller::resolveNextJsonListChannel);
}

void Controller::handleVideoInfoFailed(quint64 requestId,
    const QString& guid,
    const QString& errorMessage)
{
    if (requestId != m_pendingJsonChannelRequestId || guid != m_pendingJsonChannelGuid
        || m_pendingJsonChannelIndex < 0) {
        return;
    }

    m_output.warning(QStringLiteral("获取视频频道失败 [%1]: %2").arg(guid, errorMessage));
    ++m_pendingJsonChannelPosition;
    QTimer::singleShot(0, this, &Controller::resolveNextJsonListChannel);
}

void Controller::startDownload()
{
    QList<DownloadJob> jobs;
    if (!m_options.guid.isEmpty()) {
        DownloadJob job;
        job.request.url = m_options.guid;
        job.request.videoTitle = m_options.title;
        job.request.quality = m_options.quality;
        job.request.savePath = m_options.output;
        job.request.threadCount = m_options.threads;
        job.request.transcodeToMp4 = m_options.mp4;
        jobs.append(job);
    } else {
        QList<int> selected;
        QString selectionError;
        if (!parseSelection(m_options.select, m_videos.size(), &selected, &selectionError)) {
            m_output.usageError(selectionError);
            m_application.exit(static_cast<int>(ExitCode::Usage));
            return;
        }
        for (const int selectedIndex : selected) {
            const VideoItem item = m_videos.value(selectedIndex);
            DownloadJob job;
            job.request.url = item.guid;
            job.request.videoTitle = item.title;
            job.request.quality = m_options.quality;
            job.request.savePath = m_options.output;
            job.request.threadCount = m_options.threads;
            job.request.transcodeToMp4 = m_options.mp4;
            jobs.append(job);
        }
    }

    if (jobs.isEmpty()) {
        m_output.resolutionFailed(QStringLiteral("no videos matched the selection"));
        m_application.exit(static_cast<int>(ExitCode::ResolutionFailure));
        return;
    }

    m_coordinator = new DownloadCoordinator(m_api, nullptr, nullptr, nullptr, this);
    connect(m_coordinator, &DownloadCoordinator::jobChanged, this, [this](const DownloadJob& job) {
        m_output.jobChanged(job);
    });
    connect(m_coordinator, &DownloadCoordinator::jobFinished, this, [this](const DownloadJob& job) {
        m_output.jobFinished(job);
    });
    connect(m_coordinator, &DownloadCoordinator::batchFinished, this,
        [this](int completed, int failed, int cancelled, int total, bool) {
            m_output.downloadComplete(completed, failed, cancelled, total);
            m_application.exit(m_cancelled ? static_cast<int>(ExitCode::Cancelled) : exitCodeForBatch(failed, cancelled));
        });
    if (!m_coordinator->startBatch(jobs)) {
        m_output.downloadStartFailed();
        m_application.exit(static_cast<int>(ExitCode::DownloadFailure));
    }
}

void Controller::applyConfiguredDefaults()
{
    const auto configuredMonths = readDisplayMinAndMax();
    const auto months = normalizeDisplayMonths(m_options.from.isEmpty() ? std::get<0>(configuredMonths) : m_options.from,
        m_options.to.isEmpty() ? std::get<1>(configuredMonths) : m_options.to);
    m_from = std::get<0>(months);
    m_to = std::get<1>(months);
    if (m_options.output.isEmpty()) {
        m_options.output = readSavePath();
    }
    if (m_options.quality.isEmpty()) {
        m_options.quality = readQuality();
    }
    if (m_options.threads == 0) {
        m_options.threads = qMax(1, readThreadNum());
    }
    if (!m_options.mp4Set) {
        m_options.mp4 = readTranscode();
    }
}

} // namespace Cli
