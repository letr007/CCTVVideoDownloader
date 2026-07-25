#include "contentparse.h"

#include <QRegularExpression>

namespace ContentParse {
namespace {

QString matchOne(const QString& text, const QString& pattern)
{
    const QRegularExpression re(pattern);
    const auto m = re.match(text);
    return m.hasMatch() ? m.captured(1).trimmed() : QString();
}

QString extractUrlToken(const QString& url)
{
    return matchOne(url, QStringLiteral(R"((VID[A-Z][A-Za-z0-9]+))"));
}

void finalize(Features& features)
{
    features.hasGuid = !features.guid.isEmpty();
    features.hasAlbumCode = isVidA(features.albumId);
    if (features.albumId.isEmpty() && isVidA(features.itemId)) {
        features.albumId = features.itemId;
        features.hasAlbumCode = true;
    }
    if (features.urlToken.isEmpty()) {
        if (isVidA(features.itemId) || isVidE(features.itemId)) {
            features.urlToken = features.itemId;
        }
    }
    features.kind = classify(features);
}

} // namespace

Kind classify(const Features& features)
{
    const bool vida = isVidA(features.itemId) || isVidA(features.urlToken) || isVidA(features.albumId);
    const bool vide = isVidE(features.itemId) || isVidE(features.urlToken);

    if (vida && !vide) {
        return Kind::Album;
    }
    if (vide) {
        return Kind::Episode;
    }
    if (isTopc(features.columnId) || isTopc(features.itemId)) {
        return Kind::Column;
    }
    if (features.hasGuid || isHexGuid(features.columnId)
        || (!features.columnId.isEmpty() && !isTopc(features.columnId)
            && !isVidA(features.columnId) && !isVidE(features.columnId))) {
        return Kind::FourK;
    }
    return Kind::Unknown;
}

Features parsePage(const QString& html, const QString& url)
{
    Features features;
    features.urlToken = extractUrlToken(url);
    features.title = matchOne(html, QStringLiteral(R"(var commentTitle\s*=\s*["'](.*?)["'];)")).split(QLatin1Char(' ')).value(0);
    features.itemId = matchOne(html, QStringLiteral(R"(var itemid1\s*=\s*["'](.*?)["'];)"));
    features.columnId = matchOne(html, QStringLiteral(R"(var column_id\s*=\s*["'](.*?)["'];)"));
    features.guid = matchOne(html, QStringLiteral(R"(var guid\s*=\s*["'](.*?)["'];)"));
    features.albumId = matchOne(html, QStringLiteral(R"(var videotvCodes\s*=\s*["'](.*?)["'];)"));
    features.hasJsonEpisodeList = html.contains(QStringLiteral("var jsonData2="))
        || html.contains(QStringLiteral("var jsonData2 ="));

    const QString videoCenterId = matchOne(html, QStringLiteral(R"(\bvideoCenterId\s*:\s*["']([0-9a-fA-F]{32})["'])"));
    if (url.contains(QStringLiteral("news.cctv.cn"), Qt::CaseInsensitive) && !videoCenterId.isEmpty()) {
        const QString videoId = matchOne(html, QStringLiteral(R"(\bvideoId\s*:\s*["']([^"']+)["'])"));
        features.itemId = videoId.isEmpty() ? videoCenterId : videoId;
        features.columnId = videoCenterId;
        features.kind = Kind::News;
        features.hasGuid = isHexGuid(videoCenterId);
        return features;
    }

    if (features.itemId.isEmpty() && !features.urlToken.isEmpty()) {
        features.itemId = features.urlToken;
    }

    if (features.title.isEmpty() || features.itemId.isEmpty() || features.columnId.isEmpty()) {
        const QRegularExpression lmUrlRegex(QStringLiteral(R"(tv\.cctv\.com/lm/([^/?#]+))"));
        const auto lmUrlMatch = lmUrlRegex.match(url);
        if (lmUrlMatch.hasMatch()) {
            QString lmTitle = matchOne(html, QStringLiteral(R"(<meta\s+property=["']og:title["']\s+content=["'](.*?)["'])"));
            if (lmTitle.isEmpty()) {
                lmTitle = matchOne(html, QStringLiteral(R"(<title>\s*(.*?)\s*(?:_CCTV|</title>))"));
            }
            const QString lmItemId = matchOne(html, QStringLiteral(R"(play\(\s*["']([0-9a-fA-F]{32})["'])"));
            features.title = lmTitle.isEmpty() ? features.title : lmTitle;
            if (!lmItemId.isEmpty()) {
                features.itemId = lmItemId;
            }
        }
    }

    if (features.columnId.isEmpty() && !features.guid.isEmpty()) {
        features.title = features.title.isEmpty() ? QStringLiteral("CCTV-4K") : features.title;
        features.itemId = features.itemId.isEmpty() ? features.guid : features.itemId;
        features.columnId = features.guid;
    }

    finalize(features);
    return features;
}

Features fromStoredIds(const QString& columnId, const QString& itemId)
{
    Features features;
    features.columnId = columnId;
    features.itemId = itemId;
    features.urlToken = itemId;

    if (isVidA(itemId)) {
        features.albumId = itemId;
    }
    if (isHexGuid(columnId)) {
        features.guid = columnId;
    } else if (!columnId.isEmpty() && !isTopc(columnId) && !isVidA(columnId) && !isVidE(columnId)) {
        features.guid = columnId;
    }

    finalize(features);
    return features;
}

} // namespace ContentParse
