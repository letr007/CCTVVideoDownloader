#include "contentresolver.h"

#include <QDate>
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QRegularExpression>
#include <QUrlQuery>

namespace ContentParse {
namespace {

QStringList monthRange(const QString& startDate, const QString& endDate)
{
    QDate dateBegin = QDateTime::fromString(startDate, QStringLiteral("yyyyMM")).date();
    QDate dateEnd = QDateTime::fromString(endDate, QStringLiteral("yyyyMM")).date();
    if (dateBegin < dateEnd) {
        qSwap(dateBegin, dateEnd);
    }

    QStringList dates;
    for (QDate date = dateBegin; date >= dateEnd; date = date.addMonths(-1)) {
        dates.append(date.toString(QStringLiteral("yyyyMM")));
    }
    return dates;
}

QMap<int, VideoItem> fetchAlbumByModes(APIService& apiService, const QString& albumId, const QVector<int>& modes)
{
    for (const int mode : modes) {
        const QMap<int, VideoItem> result = apiService.fetchAlbumVideoList(albumId, mode);
        if (!result.isEmpty()) {
            return result;
        }
    }
    return {};
}

} // namespace

ContentResolver::ContentResolver(APIService& apiService, QObject* parent)
    : QObject(parent)
    , m_apiService(apiService)
{
}

QSharedPointer<ImportResult> ContentResolver::resolvePlayColumnInfo(const QString& url)
{
    const QByteArray responseData = m_apiService.fetchPageHtml(QUrl(url));
    if (responseData.isEmpty()) {
        return nullptr;
    }

    const QString html = QString::fromUtf8(responseData);
    Features features = parsePage(html, url);
    if (features.title.isEmpty() || features.itemId.isEmpty() || features.columnId.isEmpty()) {
        const QRegularExpression lmUrlRegex(QStringLiteral(R"(tv\.cctv\.com/lm/([^/?#]+))"));
        const auto lmUrlMatch = lmUrlRegex.match(url);
        if (lmUrlMatch.hasMatch()) {
            const QString videosetUrl = QStringLiteral("https://tv.cctv.com/lm/%1/videoset").arg(lmUrlMatch.captured(1));
            const QString videoset = QString::fromUtf8(m_apiService.fetchPageHtml(QUrl(videosetUrl)));
            const QString columnId = QRegularExpression(QStringLiteral(R"(var\s+lmtopId\s*=\s*["'](TOPC\d+)["'];)"))
                .match(videoset).captured(1).trimmed();
            if (!features.title.isEmpty() && !columnId.isEmpty()) {
                features.itemId = features.itemId.isEmpty() ? columnId : features.itemId;
                features.columnId = columnId;
                features.kind = classify(features);
            }
        }
    }

    if (features.title.isEmpty() || features.itemId.isEmpty() || features.columnId.isEmpty()) {
        return nullptr;
    }

    ImportResult importResult = makeImportResult(features);
    if (!importResult.isValid()) {
        return nullptr;
    }
    return QSharedPointer<ImportResult>::create(importResult);
}

QMap<int, VideoItem> ContentResolver::resolveVideoList(const ProgrammeRecord& record,
    const QString& startDate,
    const QString& endDate)
{
    const Plan plan = makePlan(record);
    const QStringList dates = monthRange(startDate, endDate);

    switch (plan.catalogStrategy) {
    case CatalogStrategy::SingleVideo:
        return m_apiService.fetchSingleVideoByGuid(plan.serviceId, plan.catalogId);
    case CatalogStrategy::ColumnByMonth:
        return m_apiService.fetchColumnVideoList(plan.catalogId, dates);
    case CatalogStrategy::AlbumByModes:
        return fetchAlbumByModes(m_apiService, plan.catalogId, plan.albumModes);
    case CatalogStrategy::ColumnThenAlbumByModes: {
        QMap<int, VideoItem> videos = m_apiService.fetchColumnVideoList(plan.catalogId, dates);
        if (!videos.isEmpty()) {
            return videos;
        }
        const QString albumId = plan.features.albumId.isEmpty()
            ? m_apiService.resolveAlbumId(record.itemId) : plan.features.albumId;
        return fetchAlbumByModes(m_apiService, albumId, plan.albumModes);
    }
    case CatalogStrategy::ResolveAlbumThenByModes:
        return fetchAlbumByModes(m_apiService, m_apiService.resolveAlbumId(plan.catalogId), plan.albumModes);
    case CatalogStrategy::None:
        return {};
    }
    return {};
}

QMap<int, VideoItem> ContentResolver::resolveVideoList(const ImportResult& result,
    const QString& startDate,
    const QString& endDate)
{
    return resolveVideoList(makeProgrammeRecord(result), startDate, endDate);
}

QMap<int, VideoItem> ContentResolver::resolveVideoList(const QString& columnId,
    const QString& itemId,
    const QString& startDate,
    const QString& endDate)
{
    return resolveVideoList(programmeRecordFromStoredIds(columnId, itemId), startDate, endDate);
}

void ContentResolver::startResolveMedia(const QString& guid, const QString& quality)
{
    if (m_activeMediaResolveId != 0) {
        cancelResolveMedia();
    }

    m_pendingQuality = quality;
    m_pendingMasterPlaylistUrl.clear();
    m_pendingClearQualities.clear();
    m_mediaResolveStage = MediaResolveStage::FetchInfo;
    m_activeMediaResolveId = ++m_nextMediaResolveId;

    QUrl infoUrl(QStringLiteral("https://vdn.apps.cntv.cn/api/getHttpVideoInfo.do"));
    QUrlQuery infoQuery;
    infoQuery.addQueryItem(QStringLiteral("pid"), guid);
    infoUrl.setQuery(infoQuery);
    startMediaRequest(m_activeMediaResolveId, infoUrl);
}

void ContentResolver::cancelResolveMedia()
{
    if (m_activeMediaResolveId == 0) {
        return;
    }

    QPointer<QNetworkReply> reply = m_pendingMediaReply;
    m_pendingMediaReply = nullptr;
    m_activeMediaResolveId = 0;
    m_mediaResolveStage = MediaResolveStage::None;
    m_pendingQuality.clear();
    m_pendingMasterPlaylistUrl.clear();
    m_pendingClearQualities.clear();
    emit mediaResolveCancelled();

    if (reply) {
        reply->abort();
    }
}

void ContentResolver::startMediaRequest(quint64 requestId, const QUrl& url)
{
    if (requestId != m_activeMediaResolveId || requestId == 0) {
        return;
    }

    QNetworkReply* reply = m_apiService.networkAccessManager()->get(m_apiService.buildNetworkRequest(url));
    m_pendingMediaReply = reply;
    connect(reply, &QNetworkReply::errorOccurred, this,
        [reply](QNetworkReply::NetworkError error) {
            if (error == QNetworkReply::SslHandshakeFailedError) {
                reply->ignoreSslErrors();
            }
        });
    connect(reply, &QNetworkReply::finished, this, [this, requestId, reply]() {
        handleMediaReplyFinished(requestId, reply);
    });
}

void ContentResolver::handleMediaReplyFinished(quint64 requestId, QNetworkReply* reply)
{
    if (requestId != m_activeMediaResolveId || requestId == 0 || reply != m_pendingMediaReply) {
        reply->deleteLater();
        return;
    }

    m_pendingMediaReply = nullptr;
    const MediaResolveStage stage = m_mediaResolveStage;
    if (reply->error() != QNetworkReply::NoError) {
        if (reply->error() == QNetworkReply::OperationCanceledError) {
            reply->deleteLater();
            m_activeMediaResolveId = 0;
            m_mediaResolveStage = MediaResolveStage::None;
            m_pendingQuality.clear();
            m_pendingMasterPlaylistUrl.clear();
            m_pendingClearQualities.clear();
            emit mediaResolveCancelled();
            return;
        }
        if (stage == MediaResolveStage::FetchClearPlaylist) {
            reply->deleteLater();
            if (!m_pendingClearQualities.isEmpty()) {
                const QString playlistUrl = clearVariantUrl(m_pendingMasterPlaylistUrl, m_pendingClearQualities.takeFirst());
                startMediaRequest(requestId, QUrl(playlistUrl));
                return;
            }
            finishMediaResolveFailure(requestId, QStringLiteral("网络请求失败: %1").arg(reply->errorString()));
            return;
        }
        const QString errorMessage = QStringLiteral("网络请求失败: %1").arg(reply->errorString());
        reply->deleteLater();
        finishMediaResolveFailure(requestId, errorMessage);
        return;
    }

    const QByteArray responseData = reply->readAll();
    const QString requestUrl = reply->url().toString();
    reply->deleteLater();
    if (responseData.isEmpty()) {
        if (stage == MediaResolveStage::FetchClearPlaylist && !m_pendingClearQualities.isEmpty()) {
            const QString playlistUrl = clearVariantUrl(m_pendingMasterPlaylistUrl, m_pendingClearQualities.takeFirst());
            startMediaRequest(requestId, QUrl(playlistUrl));
            return;
        }
        finishMediaResolveFailure(requestId, QStringLiteral("网络响应为空: %1").arg(requestUrl));
        return;
    }

    if (stage == MediaResolveStage::FetchInfo) {
        QJsonParseError parseError;
        const QJsonDocument infoDoc = QJsonDocument::fromJson(responseData, &parseError);
        if (parseError.error == QJsonParseError::NoError && infoDoc.isObject()) {
            const QJsonObject root = infoDoc.object();
            if (root.value(QStringLiteral("play_channel")).toString().contains(QStringLiteral("CCTV-4K"), Qt::CaseInsensitive)) {
                QString hlsUrl = root.value(QStringLiteral("hls_url")).toString();
                if (hlsUrl.isEmpty()) {
                    finishMediaResolveFailure(requestId, QStringLiteral("CCTV-4K视频hls_url为空"));
                    return;
                }
                QString fourKUrl = clearVariantUrl(hlsUrl, QStringLiteral("4000"));
                if (fourKUrl.isEmpty()) {
                    fourKUrl = hlsUrl;
                    fourKUrl.replace(QRegularExpression(QStringLiteral(R"((?<=/)main(?=/))")), QStringLiteral("4000"));
                }
                if (fourKUrl == hlsUrl) {
                    finishMediaResolveFailure(requestId, QStringLiteral("CCTV-4K视频hls_url格式无效"));
                    return;
                }
                hlsUrl = fourKUrl;
                m_pendingMasterPlaylistUrl = hlsUrl;
                m_mediaResolveStage = MediaResolveStage::Fetch4KPlaylist;
                startMediaRequest(requestId, QUrl(hlsUrl));
                return;
            }
        }

        const QJsonObject root = infoDoc.object();
        const QJsonObject manifest = root.value(QStringLiteral("manifest")).toObject();
        QString encryptedPlaylistUrl = manifest.value(QStringLiteral("hls_h5e_url")).toString();
        if (encryptedPlaylistUrl.isEmpty()) {
            encryptedPlaylistUrl = manifest.value(QStringLiteral("hls_enc_url")).toString();
        }
        if (encryptedPlaylistUrl.isEmpty()) {
            encryptedPlaylistUrl = manifest.value(QStringLiteral("hls_enc2_url")).toString();
        }
        const QString normalizedEncryptedPlaylistUrl = normalizeEncryptedPlaylistUrl(encryptedPlaylistUrl);
        if (!normalizedEncryptedPlaylistUrl.isEmpty()) {
            m_pendingMasterPlaylistUrl = normalizedEncryptedPlaylistUrl;
            m_mediaResolveStage = MediaResolveStage::FetchMasterPlaylist;
            startMediaRequest(requestId, QUrl(m_pendingMasterPlaylistUrl));
            return;
        }

        const QString clearHlsUrl = root.value(QStringLiteral("hls_url")).toString();
        if (!clearHlsUrl.isEmpty()) {
            m_pendingMasterPlaylistUrl = clearHlsUrl;
            m_pendingClearQualities = qualityFallbacks(m_pendingQuality);
            if (!m_pendingClearQualities.isEmpty()) {
                const QString playlistUrl = clearVariantUrl(clearHlsUrl, m_pendingClearQualities.takeFirst());
                if (!playlistUrl.isEmpty()) {
                    m_mediaResolveStage = MediaResolveStage::FetchClearPlaylist;
                    startMediaRequest(requestId, QUrl(playlistUrl));
                    return;
                }
            }
        }

        finishMediaResolveFailure(requestId, QStringLiteral("无法获取hls_h5e_url"));
        return;
    }

    if (stage == MediaResolveStage::Fetch4KPlaylist) {
        const QStringList urls = buildTsUrls(responseData, requestUrl);
        if (urls.isEmpty()) {
            finishMediaResolveFailure(requestId, QStringLiteral("未解析到CCTV-4K TS切片"));
            return;
        }
        finishMediaResolveSuccess(requestId, {urls, EncryptionMode::None, true});
        return;
    }

    if (stage == MediaResolveStage::FetchClearPlaylist) {
        const QStringList urls = buildTsUrls(responseData, requestUrl);
        if (!urls.isEmpty()) {
            finishMediaResolveSuccess(requestId, {urls, EncryptionMode::None, false});
            return;
        }
        if (!m_pendingClearQualities.isEmpty()) {
            const QString playlistUrl = clearVariantUrl(m_pendingMasterPlaylistUrl, m_pendingClearQualities.takeFirst());
            startMediaRequest(requestId, QUrl(playlistUrl));
            return;
        }
        finishMediaResolveFailure(requestId, QStringLiteral("未解析到明文TS切片"));
        return;
    }

    if (stage == MediaResolveStage::FetchMasterPlaylist) {
        const QHash<QString, QString> qualityUrls = parseQualityUrls(responseData);
        const QString selectedQuality = selectQuality(m_pendingQuality, qualityUrls);
        if (selectedQuality.isEmpty()) {
            finishMediaResolveFailure(requestId, qualityUrls.isEmpty()
                ? QStringLiteral("解析M3U8质量信息失败")
                : QStringLiteral("选择质量失败"));
            return;
        }
        const QUrl masterUrl(m_pendingMasterPlaylistUrl);
        const QUrl variantUrl = masterUrl.resolved(QUrl(qualityUrls.value(selectedQuality)));
        m_mediaResolveStage = MediaResolveStage::FetchVariantPlaylist;
        startMediaRequest(requestId, variantUrl);
        return;
    }

    if (stage == MediaResolveStage::FetchVariantPlaylist) {
        const QStringList urls = buildTsUrls(responseData, requestUrl);
        if (urls.isEmpty()) {
            finishMediaResolveFailure(requestId, QStringLiteral("未解析到TS切片"));
            return;
        }
        finishMediaResolveSuccess(requestId, {urls, EncryptionMode::H5E, false});
        return;
    }

    finishMediaResolveFailure(requestId, QStringLiteral("未知的M3U8解析阶段"));
}

void ContentResolver::finishMediaResolveSuccess(quint64 requestId, const ContentParse::ResolvedMedia& media)
{
    if (requestId != m_activeMediaResolveId || requestId == 0) {
        return;
    }
    m_activeMediaResolveId = 0;
    m_mediaResolveStage = MediaResolveStage::None;
    m_pendingQuality.clear();
    m_pendingMasterPlaylistUrl.clear();
    m_pendingClearQualities.clear();
    emit mediaResolved(media);
}

void ContentResolver::finishMediaResolveFailure(quint64 requestId, const QString& errorMessage)
{
    if (requestId != m_activeMediaResolveId || requestId == 0) {
        return;
    }
    m_activeMediaResolveId = 0;
    m_mediaResolveStage = MediaResolveStage::None;
    m_pendingQuality.clear();
    m_pendingMasterPlaylistUrl.clear();
    m_pendingClearQualities.clear();
    emit mediaResolveFailed(errorMessage);
}

QHash<QString, QString> ContentResolver::parseQualityUrls(const QByteArray& playlistData) const
{
    const QHash<QString, int> qualityBandwidths = {
        {QStringLiteral("5"), 4000000}, {QStringLiteral("4"), 460800},
        {QStringLiteral("3"), 870400}, {QStringLiteral("2"), 1228800},
        {QStringLiteral("1"), 2048000}
    };
    QHash<QString, QString> qualityUrls;
    QString currentQuality;
    for (const QString& line : QString::fromUtf8(playlistData).split(QLatin1Char('\n'))) {
        const QString trimmed = line.trimmed();
        if (trimmed.startsWith(QStringLiteral("#EXT-X-STREAM-INF"))) {
            const QRegularExpressionMatch match = QRegularExpression(QStringLiteral("BANDWIDTH=(\\d+)")).match(trimmed);
            if (!match.hasMatch()) {
                currentQuality.clear();
                continue;
            }
            const int bandwidth = match.captured(1).toInt();
            currentQuality.clear();
            for (auto it = qualityBandwidths.cbegin(); it != qualityBandwidths.cend(); ++it) {
                if (it.value() == bandwidth || (bandwidth >= 4000000 && it.key() == QStringLiteral("5"))) {
                    currentQuality = it.key();
                    break;
                }
            }
        } else if (!trimmed.isEmpty() && !trimmed.startsWith(QLatin1Char('#')) && !currentQuality.isEmpty()) {
            qualityUrls.insert(currentQuality, trimmed);
            currentQuality.clear();
        }
    }
    return qualityUrls;
}

QString ContentResolver::selectQuality(const QString& requestedQuality, const QHash<QString, QString>& availableQualities) const
{
    if (requestedQuality != QStringLiteral("0")) {
        return availableQualities.contains(requestedQuality) ? requestedQuality : QString();
    }
    const QHash<QString, int> qualityBandwidths = {
        {QStringLiteral("5"), 4000000}, {QStringLiteral("1"), 2048000},
        {QStringLiteral("2"), 1228800}, {QStringLiteral("3"), 870400}, {QStringLiteral("4"), 460800}
    };
    QString selected;
    int maxBandwidth = -1;
    for (auto it = availableQualities.cbegin(); it != availableQualities.cend(); ++it) {
        const int bandwidth = qualityBandwidths.value(it.key(), -1);
        if (bandwidth > maxBandwidth) {
            maxBandwidth = bandwidth;
            selected = it.key();
        }
    }
    return selected.isEmpty() && !availableQualities.isEmpty() ? availableQualities.constBegin().key() : selected;
}

QStringList ContentResolver::buildTsUrls(const QByteArray& playlistData, const QString& playlistUrl) const
{
    QStringList urls;
    const QUrl baseUrl(playlistUrl);
    for (const QString& line : QString::fromUtf8(playlistData).replace(QStringLiteral("\r\n"), QStringLiteral("\n")).replace(QLatin1Char('\r'), QLatin1Char('\n')).split(QLatin1Char('\n'))) {
        const QString trimmed = line.trimmed();
        if (trimmed.isEmpty() || trimmed.startsWith(QLatin1Char('#'))) {
            continue;
        }
        QUrl segmentUrl(trimmed);
        if (segmentUrl.scheme().isEmpty() && trimmed.startsWith(QStringLiteral("//"))) {
            segmentUrl = QUrl(baseUrl.scheme() + QLatin1Char(':') + trimmed);
        } else if (segmentUrl.isRelative()) {
            QUrl directoryUrl = baseUrl;
            directoryUrl.setQuery(QString());
            QString directoryPath = directoryUrl.path();
            directoryPath = directoryPath.left(directoryPath.lastIndexOf(QLatin1Char('/')) + 1);
            directoryUrl.setPath(directoryPath);
            segmentUrl = directoryUrl.resolved(segmentUrl);
        }
        if (segmentUrl.path().endsWith(QStringLiteral(".ts"), Qt::CaseInsensitive)) {
            urls.append(segmentUrl.toString());
        }
    }
    return urls;
}

QString ContentResolver::clearVariantUrl(const QString& hlsUrl, const QString& quality) const
{
    const QRegularExpression pattern(QStringLiteral(R"(/asp/hls/(main|4000|2000|1200|850|450)(?=/))"));
    QString result = hlsUrl;
    if (!result.contains(pattern)) {
        return {};
    }
    result.replace(pattern, QStringLiteral("/asp/hls/") + quality);
    return result;
}

QStringList ContentResolver::qualityFallbacks(const QString& quality) const
{
    const QStringList qualities = {QStringLiteral("4000"), QStringLiteral("2000"), QStringLiteral("1200"), QStringLiteral("850"), QStringLiteral("450")};
    if (quality == QStringLiteral("0")) {
        return qualities;
    }
    const QHash<QString, QString> mapping = {
        {QStringLiteral("5"), QStringLiteral("4000")}, {QStringLiteral("1"), QStringLiteral("2000")},
        {QStringLiteral("2"), QStringLiteral("1200")}, {QStringLiteral("3"), QStringLiteral("850")},
        {QStringLiteral("4"), QStringLiteral("450")}
    };
    const int index = qualities.indexOf(mapping.value(quality));
    return index < 0 ? QStringList() : qualities.mid(index);
}

QString ContentResolver::normalizeEncryptedPlaylistUrl(QString url) const
{
    const QRegularExpression expression(QStringLiteral("https://[^/]+/asp/enc2/"));
    const QRegularExpressionMatch match = expression.match(url);
    if (match.hasMatch()) {
        url.replace(match.captured(0), QStringLiteral("https://drm.cntv.vod.dnsv1.com/asp/enc2/"));
    }
    return url;
}

} // namespace ContentParse
