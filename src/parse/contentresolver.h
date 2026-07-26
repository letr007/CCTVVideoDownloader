#pragma once

#include <QMap>
#include <QObject>
#include <QPointer>
#include <QSharedPointer>
#include <QUrl>
#include <QVector>

#include "../head/apiservice.h"
#include "contentparse.h"

namespace ContentParse {

class ContentResolver : public QObject {
    Q_OBJECT

public:
    explicit ContentResolver(APIService& apiService, QObject* parent = nullptr);

    QSharedPointer<ImportResult> resolvePlayColumnInfo(const QString& url);
    QMap<int, VideoItem> resolveVideoList(const ProgrammeRecord& record,
        const QString& startDate,
        const QString& endDate);
    QMap<int, VideoItem> resolveVideoList(const ImportResult& result,
        const QString& startDate,
        const QString& endDate);
    QMap<int, VideoItem> resolveVideoList(const QString& columnId,
        const QString& itemId,
        const QString& startDate,
        const QString& endDate);

    void startResolveMedia(const QString& guid, const QString& quality);
    void cancelResolveMedia();

signals:
    void mediaResolved(const ContentParse::ResolvedMedia& media);
    void mediaResolveFailed(const QString& errorMessage);
    void mediaResolveCancelled();

private:
    enum class MediaResolveStage {
        None,
        FetchInfo,
        Fetch4KPlaylist,
        FetchClearPlaylist,
        FetchMasterPlaylist,
        FetchVariantPlaylist
    };

    void startMediaRequest(quint64 requestId, const QUrl& url);
    void handleMediaReplyFinished(quint64 requestId, QNetworkReply* reply);
    void finishMediaResolveSuccess(quint64 requestId, const ContentParse::ResolvedMedia& media);
    void finishMediaResolveFailure(quint64 requestId, const QString& errorMessage);
    QHash<QString, QString> parseQualityUrls(const QByteArray& playlistData) const;
    QString selectQuality(const QString& requestedQuality, const QHash<QString, QString>& availableQualities) const;
    QStringList buildTsUrls(const QByteArray& playlistData, const QString& playlistUrl) const;
    QString normalizeEncryptedPlaylistUrl(QString url) const;
    QString clearVariantUrl(const QString& hlsUrl, const QString& quality) const;
    QStringList qualityFallbacks(const QString& quality) const;

    APIService& m_apiService;
    QPointer<QNetworkReply> m_pendingMediaReply;
    quint64 m_activeMediaResolveId = 0;
    quint64 m_nextMediaResolveId = 0;
    MediaResolveStage m_mediaResolveStage = MediaResolveStage::None;
    QString m_pendingQuality;
    QString m_pendingMasterPlaylistUrl;
    QStringList m_pendingClearQualities;
};

} // namespace ContentParse
