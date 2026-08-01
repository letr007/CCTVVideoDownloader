#include "../../include/contentresolver.h"

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
            const QRegularExpression columnIdExpression(
                QStringLiteral(R"(\bvar\s+(?:topicID|lmtopId)\s*=\s*["'](TOPC[A-Za-z0-9_-]+)["']\s*;?)"));
            QString columnId = columnIdExpression.match(html).captured(1).trimmed();
            if (columnId.isEmpty()) {
                const QString videosetUrl = QStringLiteral("https://tv.cctv.com/lm/%1/videoset").arg(lmUrlMatch.captured(1));
                const QString videoset = QString::fromUtf8(m_apiService.fetchPageHtml(QUrl(videosetUrl)));
                columnId = columnIdExpression.match(videoset).captured(1).trimmed();
            }
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
    case CatalogStrategy::VcctvProgrammeByPage:
        return m_apiService.fetchVcctvProgrammeVideoList(plan.catalogId, plan.chid, startDate, endDate);
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
    m_pendingEncryptedPlaylistUrl.clear();
    m_pendingClearQualities.clear();
    m_pendingSelectedVariantIsHighQuality = false;
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
    m_pendingEncryptedPlaylistUrl.clear();
    m_pendingClearQualities.clear();
    m_pendingSelectedVariantIsHighQuality = false;
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
            m_pendingEncryptedPlaylistUrl.clear();
            m_pendingClearQualities.clear();
            m_pendingSelectedVariantIsHighQuality = false;
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
            if (fallbackToEncryptedPlaylist(requestId)) {
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
        if (stage == MediaResolveStage::FetchClearPlaylist && fallbackToEncryptedPlaylist(requestId)) {
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
            const QString playChannel = root.value(QStringLiteral("play_channel")).toString();
            if (playChannel.contains(QStringLiteral("CCTV-4K"), Qt::CaseInsensitive)) {
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

            if (isCctv16Channel(playChannel)) {
                const QString hlsUrl = root.value(QStringLiteral("hls_url")).toString();
                const QString normalizedEncryptedPlaylistUrl = encryptedPlaylistUrlFrom(root);
                if (!hlsUrl.isEmpty()) {
                    m_pendingMasterPlaylistUrl = hlsUrl;
                    m_pendingEncryptedPlaylistUrl = normalizedEncryptedPlaylistUrl;
                    m_pendingClearQualities = qualityFallbacks(m_pendingQuality);
                    if (!m_pendingClearQualities.isEmpty()) {
                        const QString playlistUrl = clearVariantUrl(hlsUrl, m_pendingClearQualities.takeFirst());
                        if (!playlistUrl.isEmpty()) {
                            m_mediaResolveStage = MediaResolveStage::FetchClearPlaylist;
                            startMediaRequest(requestId, QUrl(playlistUrl));
                            return;
                        }
                    }
                }
                if (!normalizedEncryptedPlaylistUrl.isEmpty()) {
                    m_pendingMasterPlaylistUrl = normalizedEncryptedPlaylistUrl;
                    m_mediaResolveStage = MediaResolveStage::FetchMasterPlaylist;
                    startMediaRequest(requestId, QUrl(m_pendingMasterPlaylistUrl));
                    return;
                }
            }
        }

        const QJsonObject root = infoDoc.object();
        const QString normalizedEncryptedPlaylistUrl = encryptedPlaylistUrlFrom(root);
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
        if (fallbackToEncryptedPlaylist(requestId)) {
            return;
        }
        finishMediaResolveFailure(requestId, QStringLiteral("未解析到明文TS切片"));
        return;
    }

    if (stage == MediaResolveStage::FetchMasterPlaylist) {
        const QVector<MediaVariant> variants = parseVariants(responseData);
        const int selectedIndex = selectVariantIndex(m_pendingQuality, variants);
        if (selectedIndex < 0) {
            finishMediaResolveFailure(requestId, variants.isEmpty()
                ? QStringLiteral("解析M3U8质量信息失败")
                : QStringLiteral("选择质量失败"));
            return;
        }
        const MediaVariant& selectedVariant = variants.at(selectedIndex);
        const QUrl masterUrl(m_pendingMasterPlaylistUrl);
        const QUrl variantUrl = masterUrl.resolved(QUrl(selectedVariant.url));
        m_pendingSelectedVariantIsHighQuality = isHighQualityVariant(selectedVariant);
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
        finishMediaResolveSuccess(requestId, {urls, EncryptionMode::H5E, m_pendingSelectedVariantIsHighQuality});
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
    m_pendingEncryptedPlaylistUrl.clear();
    m_pendingClearQualities.clear();
    m_pendingSelectedVariantIsHighQuality = false;
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
    m_pendingEncryptedPlaylistUrl.clear();
    m_pendingClearQualities.clear();
    m_pendingSelectedVariantIsHighQuality = false;
    emit mediaResolveFailed(errorMessage);
}

QVector<ContentResolver::MediaVariant> ContentResolver::parseVariants(const QByteArray& playlistData) const
{
    QVector<MediaVariant> variants;
    const QRegularExpression bandwidthExpression(QStringLiteral(R"((?:^|[,:])\s*BANDWIDTH=(\d+))"));
    const QRegularExpression resolutionExpression(QStringLiteral(R"((?:^|[,:])\s*RESOLUTION=(\d+x\d+))"));
    qint64 pendingBandwidth = -1;
    QString pendingResolution;

    for (const QString& line : QString::fromUtf8(playlistData).split(QLatin1Char('\n'))) {
        const QString trimmed = line.trimmed();
        if (trimmed.startsWith(QStringLiteral("#EXT-X-STREAM-INF"))) {
            const QRegularExpressionMatch bandwidthMatch = bandwidthExpression.match(trimmed);
            pendingBandwidth = bandwidthMatch.hasMatch() ? bandwidthMatch.captured(1).toLongLong() : -1;
            const QRegularExpressionMatch resolutionMatch = resolutionExpression.match(trimmed);
            pendingResolution = resolutionMatch.hasMatch() ? resolutionMatch.captured(1) : QString();
        } else if (!trimmed.isEmpty() && !trimmed.startsWith(QLatin1Char('#')) && pendingBandwidth >= 0) {
            variants.append({pendingBandwidth, pendingResolution, trimmed});
            pendingBandwidth = -1;
            pendingResolution.clear();
        }
    }
    return variants;
}

int ContentResolver::selectVariantIndex(const QString& requestedQuality, const QVector<MediaVariant>& variants) const
{
    if (variants.isEmpty()) {
        return -1;
    }

    if (requestedQuality == QStringLiteral("0") || requestedQuality == QStringLiteral("5")) {
        int selectedIndex = 0;
        for (int index = 1; index < variants.size(); ++index) {
            if (variants.at(index).bandwidth > variants.at(selectedIndex).bandwidth) {
                selectedIndex = index;
            }
        }
        return selectedIndex;
    }

    const QHash<QString, qint64> targetBandwidths = {
        {QStringLiteral("5"), 4000000}, {QStringLiteral("1"), 2048000},
        {QStringLiteral("2"), 1228800}, {QStringLiteral("3"), 870400},
        {QStringLiteral("4"), 460800}
    };
    const auto target = targetBandwidths.constFind(requestedQuality);
    if (target == targetBandwidths.cend()) {
        return -1;
    }

    int selectedIndex = -1;
    for (int index = 0; index < variants.size(); ++index) {
        if (variants.at(index).bandwidth <= target.value()
            && (selectedIndex < 0 || variants.at(index).bandwidth > variants.at(selectedIndex).bandwidth)) {
            selectedIndex = index;
        }
    }
    if (selectedIndex >= 0) {
        return selectedIndex;
    }

    selectedIndex = 0;
    for (int index = 1; index < variants.size(); ++index) {
        if (variants.at(index).bandwidth < variants.at(selectedIndex).bandwidth) {
            selectedIndex = index;
        }
    }
    return selectedIndex;
}

bool ContentResolver::isHighQualityVariant(const MediaVariant& variant) const
{
    if (variant.bandwidth >= 3000000) {
        return true;
    }

    const QRegularExpressionMatch resolutionMatch = QRegularExpression(QStringLiteral(R"(^\d+x(\d+)$)"))
                                                        .match(variant.resolution);
    return resolutionMatch.hasMatch() && resolutionMatch.captured(1).toInt() >= 1080;
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
    const QRegularExpression directoryPattern(QStringLiteral(R"(/asp/hls/(main|4000|3000|2000|1200|850|450)(?=/))"));
    QString result = hlsUrl;
    if (!result.contains(directoryPattern)) {
        return {};
    }
    result.replace(directoryPattern, QStringLiteral("/asp/hls/") + quality);
    result.replace(QRegularExpression(QStringLiteral(R"(/main\.m3u8(?=([?#]|$)))")),
        QStringLiteral("/") + quality + QStringLiteral(".m3u8"));
    return result;
}

QStringList ContentResolver::qualityFallbacks(const QString& quality) const
{
    const QStringList qualities = {QStringLiteral("4000"), QStringLiteral("3000"), QStringLiteral("2000"), QStringLiteral("1200"), QStringLiteral("850"), QStringLiteral("450")};
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

bool ContentResolver::isCctv16Channel(const QString& playChannel) const
{
    // 匹配 CCTV-16 且后面不紧跟数字，避免误伤类似 CCTV-160 的邻号（实际不存在，但保持保守）。
    const QRegularExpression expression(QStringLiteral(R"(CCTV-16(?![0-9]))"),
        QRegularExpression::CaseInsensitiveOption);
    return expression.match(playChannel).hasMatch();
}

QString ContentResolver::encryptedPlaylistUrlFrom(const QJsonObject& root) const
{
    const QJsonObject manifest = root.value(QStringLiteral("manifest")).toObject();
    QString encryptedPlaylistUrl = manifest.value(QStringLiteral("hls_h5e_url")).toString();
    if (encryptedPlaylistUrl.isEmpty()) {
        encryptedPlaylistUrl = manifest.value(QStringLiteral("hls_enc_url")).toString();
    }
    if (encryptedPlaylistUrl.isEmpty()) {
        encryptedPlaylistUrl = manifest.value(QStringLiteral("hls_enc2_url")).toString();
    }
    return normalizeEncryptedPlaylistUrl(encryptedPlaylistUrl);
}

bool ContentResolver::fallbackToEncryptedPlaylist(quint64 requestId)
{
    if (m_pendingEncryptedPlaylistUrl.isEmpty()) {
        return false;
    }
    m_pendingMasterPlaylistUrl = m_pendingEncryptedPlaylistUrl;
    m_pendingEncryptedPlaylistUrl.clear();
    m_mediaResolveStage = MediaResolveStage::FetchMasterPlaylist;
    startMediaRequest(requestId, QUrl(m_pendingMasterPlaylistUrl));
    return true;
}

} // namespace ContentParse
