#pragma once

#include <QMap>
#include <QObject>
#include <QPointer>
#include <QSharedPointer>
#include <QUrl>
#include <QVector>

class QJsonObject;

#include "apiservice.h"
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
    struct MediaVariant {
        qint64 bandwidth = 0;
        QString resolution;
        QString url;
    };

    QVector<MediaVariant> parseVariants(const QByteArray& playlistData) const;
    int selectVariantIndex(const QString& requestedQuality, const QVector<MediaVariant>& variants) const;
    bool isHighQualityVariant(const MediaVariant& variant) const;
    QStringList buildTsUrls(const QByteArray& playlistData, const QString& playlistUrl) const;
    QString normalizeEncryptedPlaylistUrl(QString url) const;
    QString clearVariantUrl(const QString& hlsUrl, const QString& quality) const;
    QStringList qualityFallbacks(const QString& quality) const;
    bool isCctv16Channel(const QString& playChannel) const;
    QString encryptedPlaylistUrlFrom(const QJsonObject& root) const;
    bool fallbackToEncryptedPlaylist(quint64 requestId);

    APIService& m_apiService;
    QPointer<QNetworkReply> m_pendingMediaReply;
    quint64 m_activeMediaResolveId = 0;
    quint64 m_nextMediaResolveId = 0;
    MediaResolveStage m_mediaResolveStage = MediaResolveStage::None;
    QString m_pendingQuality;
    QString m_pendingMasterPlaylistUrl;
    QString m_pendingEncryptedPlaylistUrl;
    QStringList m_pendingClearQualities;
    bool m_pendingSelectedVariantIsHighQuality = false;
};

} // namespace ContentParse
