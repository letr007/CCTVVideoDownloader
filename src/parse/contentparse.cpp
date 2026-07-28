#include "contentparse.h"

#include <QRegularExpression>
#include <QUrl>

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

QString extractOgTitle(const QString& html)
{
    const QRegularExpression re(QStringLiteral(R"(<meta\b(?=[^>]*\bproperty\s*=\s*["']og:title["'])(?=[^>]*\bcontent\s*=\s*["']([^"']*)["'])[^>]*>)"),
        QRegularExpression::CaseInsensitiveOption);
    const auto m = re.match(html);
    return m.hasMatch() ? m.captured(1).trimmed() : QString();
}

void finalize(Features& features)
{
    features.hasGuid = isHexGuid(features.guid);
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

QString pageProfileName(PageProfile profile)
{
    switch (profile) {
    case PageProfile::LegacySportsEpisode:
        return QStringLiteral("LegacySportsEpisode");
    case PageProfile::Standard:
        return QStringLiteral("Standard");
    }
    return QStringLiteral("Standard");
}

PageProfile pageProfileFromName(const QString& value)
{
    return value.compare(QStringLiteral("LegacySportsEpisode"), Qt::CaseInsensitive) == 0
        ? PageProfile::LegacySportsEpisode : PageProfile::Standard;
}

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
    if (features.isFourK) {
        return Kind::FourK;
    }
    if (features.hasGuid || isHexGuid(features.columnId)) {
        return Kind::Episode;
    }
    return Kind::Unknown;
}

Features parsePage(const QString& html, const QString& url)
{
    Features features;
    features.urlToken = extractUrlToken(url);
    features.title = matchOne(html, QStringLiteral(R"(var commentTitle\s*=\s*["'](.*?)["'];)")).split(QLatin1Char(' ')).value(0);
    if (features.title.isEmpty()) {
        features.title = extractOgTitle(html);
    }
    features.itemId = matchOne(html, QStringLiteral(R"(var itemid1\s*=\s*["'](.*?)["'];)"));
    features.columnId = matchOne(html, QStringLiteral(R"(var column_id\s*=\s*["'](.*?)["'];)"));
    features.guid = matchOne(html, QStringLiteral(R"(\bvar\s+guid\s*=\s*["']([0-9a-fA-F]{32})["']\s*;?)"));
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

    const QUrl pageUrl(url);
    const QRegularExpression fourKTitleRegex(
        QStringLiteral(R"(<title\b[^>]*>[^<]*4K专区[^<]*</title>)"),
        QRegularExpression::CaseInsensitiveOption);
    const bool hasFourKPath = pageUrl.path().contains(QStringLiteral("/cctv4k/"), Qt::CaseInsensitive)
        || pageUrl.path().contains(QStringLiteral("/4K/"), Qt::CaseInsensitive);
    const bool hasFourKTitle = fourKTitleRegex.match(html).hasMatch();
    features.isFourK = hasFourKPath
        || hasFourKTitle
        || html.contains(QStringLiteral("CCTV-4K"), Qt::CaseInsensitive);
    if ((hasFourKPath || hasFourKTitle) && isHexGuid(features.guid)) {
        features.title = features.title.isEmpty() ? QStringLiteral("CCTV-4K") : features.title;
        features.itemId = features.guid;
        features.columnId = features.guid;
    } else if (pageUrl.host().compare(QStringLiteral("v.cctv.cn"), Qt::CaseInsensitive) == 0
        && isHexGuid(features.guid)) {
        features.columnId = features.guid;
    }
    if (pageUrl.host().compare(QStringLiteral("sports.cctv.com"), Qt::CaseInsensitive) == 0
        && isVidE(features.itemId)
        && isTopc(features.columnId)
        && isHexGuid(features.guid)) {
        features.profile = PageProfile::LegacySportsEpisode;
    }
    if (features.columnId.isEmpty() && isHexGuid(features.guid)) {
        if (features.isFourK) {
            features.title = features.title.isEmpty() ? QStringLiteral("CCTV-4K") : features.title;
        }
        features.itemId = features.itemId.isEmpty() ? features.guid : features.itemId;
        features.columnId = features.guid;
    }

    finalize(features);
    return features;
}

ImportResult makeImportResult(const Features& features)
{
    ImportResult result;
    result.title = features.title;
    result.rawItemId = features.itemId;
    result.rawColumnId = features.columnId;
    result.catalogId = makePlan(features).catalogId;
    result.profile = features.profile;
    return result;
}

ProgrammeRecord makeProgrammeRecord(const ImportResult& result)
{
    ProgrammeRecord record;
    record.title = result.title;
    record.itemId = result.rawItemId;
    record.columnId = result.rawColumnId;
    record.catalogId = result.catalogId;
    record.profile = result.profile;
    return record;
}

ProgrammeRecord programmeRecordFromStoredIds(const QString& columnId, const QString& itemId)
{
    ProgrammeRecord record;
    record.itemId = itemId;
    record.columnId = columnId;
    record.catalogId = columnId;
    const Features features = fromStoredIds(columnId, itemId);
    record.profile = features.profile;
    if (features.profile == PageProfile::LegacySportsEpisode && !features.guid.isEmpty()) {
        record.catalogId = features.guid;
    }
    return record;
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
        features.isFourK = itemId == columnId;
    }

    finalize(features);
    return features;
}

Plan makePlan(const Features& features)
{
    Plan plan;
    plan.features = features;

    if (features.profile == PageProfile::LegacySportsEpisode) {
        plan.catalogStrategy = CatalogStrategy::SingleVideo;
        plan.serviceId = QStringLiteral("tvcctv");
        plan.catalogId = features.guid;
        return plan;
    }

    switch (features.kind) {
    case Kind::Album:
        plan.catalogStrategy = CatalogStrategy::AlbumByModes;
        plan.catalogId = features.albumId.isEmpty() ? features.itemId : features.albumId;
        plan.albumModes = {1, 2, 0};
        break;
    case Kind::Episode:
        if (isHexGuid(features.columnId)) {
            plan.catalogStrategy = CatalogStrategy::SingleVideo;
            plan.serviceId = QStringLiteral("tvcctv");
            plan.catalogId = features.columnId;
        } else if (isTopc(features.columnId)) {
            plan.catalogStrategy = CatalogStrategy::ColumnThenAlbumByModes;
            plan.catalogId = features.columnId;
            plan.albumModes = {1, 2, 0};
        } else {
            plan.catalogStrategy = CatalogStrategy::ResolveAlbumThenByModes;
            plan.catalogId = features.itemId;
            plan.albumModes = {1, 2, 0};
        }
        break;
    case Kind::Column:
        plan.catalogStrategy = CatalogStrategy::ColumnByMonth;
        plan.catalogId = features.columnId;
        break;
    case Kind::News:
        plan.catalogStrategy = CatalogStrategy::SingleVideo;
        plan.serviceId = QStringLiteral("tvcctv");
        plan.catalogId = features.columnId;
        break;
    case Kind::FourK:
        plan.catalogStrategy = CatalogStrategy::SingleVideo;
        plan.serviceId = QStringLiteral("cctv4k");
        plan.catalogId = features.columnId;
        break;
    case Kind::Unknown:
        break;
    }
    return plan;
}

Plan makePlan(const ProgrammeRecord& record)
{
    Features features = fromStoredIds(record.columnId, record.itemId);
    features.title = record.title;
    features.profile = record.profile;
    if (record.profile == PageProfile::LegacySportsEpisode) {
        features.guid = record.catalogId;
    }
    finalize(features);

    Plan plan = makePlan(features);
    if (!record.catalogId.isEmpty()) {
        plan.catalogId = record.catalogId;
    }
    return plan;
}

Plan makePlan(const ImportResult& result)
{
    return makePlan(makeProgrammeRecord(result));
}

} // namespace ContentParse
