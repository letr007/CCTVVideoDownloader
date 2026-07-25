#pragma once

#include <QString>

namespace ContentParse {

enum class Kind {
    Unknown,
    Episode,
    Album,
    Column,
    News,
    FourK
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
    Kind kind = Kind::Unknown;
};

Features parsePage(const QString& html, const QString& url);
Features fromStoredIds(const QString& columnId, const QString& itemId);
Kind classify(const Features& features);

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
