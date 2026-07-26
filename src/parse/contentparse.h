#pragma once

#include <QMetaType>
#include <QString>
#include <QStringList>
#include <QVector>

namespace ContentParse {

enum class Kind {
    Unknown,
    Episode,
    Album,
    Column,
    News,
    FourK
};

enum class EncryptionMode {
    None,
    H5E
};

enum class PageProfile {
    Standard,
    LegacySportsEpisode
};

struct ResolvedMedia {
    QStringList segmentUrls;
    EncryptionMode encryptionMode = EncryptionMode::H5E;
    bool is4K = false;
};

struct Features {
    QString title;
    QString itemId;
    QString columnId;
    QString guid;
    QString albumId;
    QString urlToken;
    bool hasGuid = false;
    bool hasAlbumCode = false;
    bool hasJsonEpisodeList = false;
    bool isFourK = false;
    PageProfile profile = PageProfile::Standard;
    Kind kind = Kind::Unknown;
};

struct ImportResult {
    QString title;
    QString rawItemId;
    QString rawColumnId;
    QString catalogId;
    PageProfile profile = PageProfile::Standard;

    bool isValid() const
    {
        return !title.isEmpty() && !rawItemId.isEmpty() && !rawColumnId.isEmpty()
            && !catalogId.isEmpty();
    }
};

struct ProgrammeRecord {
    QString storageKey;
    QString title;
    QString itemId;
    QString columnId;
    QString catalogId;
    PageProfile profile = PageProfile::Standard;

    bool isValid() const
    {
        return !title.isEmpty() && !itemId.isEmpty() && !columnId.isEmpty()
            && !catalogId.isEmpty();
    }
};

Features parsePage(const QString& html, const QString& url);
Features fromStoredIds(const QString& columnId, const QString& itemId);
Kind classify(const Features& features);
QString pageProfileName(PageProfile profile);
PageProfile pageProfileFromName(const QString& value);
ImportResult makeImportResult(const Features& features);
ProgrammeRecord makeProgrammeRecord(const ImportResult& result);
ProgrammeRecord programmeRecordFromStoredIds(const QString& columnId, const QString& itemId);

enum class CatalogStrategy {
    None,
    SingleVideo,
    ColumnByMonth,
    AlbumByModes,
    ColumnThenAlbumByModes,
    ResolveAlbumThenByModes
};

struct Plan {
    Features features;
    CatalogStrategy catalogStrategy = CatalogStrategy::None;
    QString serviceId;
    QString catalogId;
    QVector<int> albumModes;
};

Plan makePlan(const Features& features);
Plan makePlan(const ProgrammeRecord& record);
Plan makePlan(const ImportResult& result);

inline bool isVidA(const QString& value)
{
    return value.startsWith(QStringLiteral("VIDA"), Qt::CaseInsensitive);
}

inline bool isVidE(const QString& value)
{
    return value.startsWith(QStringLiteral("VIDE"), Qt::CaseInsensitive);
}

inline bool isTopc(const QString& value)
{
    return value.startsWith(QStringLiteral("TOPC"), Qt::CaseInsensitive);
}

inline bool isHexGuid(const QString& value)
{
    if (value.size() != 32) {
        return false;
    }
    for (const QChar ch : value) {
        if (!ch.isDigit() && (ch.toLower() < QLatin1Char('a') || ch.toLower() > QLatin1Char('f'))) {
            return false;
        }
    }
    return true;
}

} // namespace ContentParse

Q_DECLARE_METATYPE(ContentParse::EncryptionMode)
Q_DECLARE_METATYPE(ContentParse::ResolvedMedia)
Q_DECLARE_METATYPE(ContentParse::PageProfile)
Q_DECLARE_METATYPE(ContentParse::ImportResult)
Q_DECLARE_METATYPE(ContentParse::ProgrammeRecord)
