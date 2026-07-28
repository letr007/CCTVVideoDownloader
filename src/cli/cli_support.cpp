#include "cli_support.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>

namespace Cli {

bool parseSelection(const QString& value, int itemCount, QList<int>* indexes, QString* error)
{
    indexes->clear();
    const QString normalized = value.trimmed().toLower();
    if (normalized == QStringLiteral("latest")) {
        if (itemCount > 0) {
            indexes->append(0);
        }
        return true;
    }
    if (normalized == QStringLiteral("all")) {
        for (int index = 0; index < itemCount; ++index) {
            indexes->append(index);
        }
        return true;
    }
    if (normalized.isEmpty()) {
        *error = QStringLiteral("--select must be latest, all, or comma-separated 1-based indexes");
        return false;
    }

    QSet<int> seen;
    for (const QString& token : value.split(',', Qt::KeepEmptyParts)) {
        bool ok = false;
        const int oneBased = token.trimmed().toInt(&ok);
        if (!ok || oneBased < 1 || oneBased > itemCount) {
            *error = QStringLiteral("invalid selection index: %1").arg(token.trimmed());
            return false;
        }
        const int zeroBased = oneBased - 1;
        if (!seen.contains(zeroBased)) {
            seen.insert(zeroBased);
            indexes->append(zeroBased);
        }
    }
    return true;
}

int exitCodeForBatch(int failedJobs, int cancelledJobs)
{
    if (cancelledJobs > 0) {
        return static_cast<int>(ExitCode::Cancelled);
    }
    return failedJobs > 0 ? static_cast<int>(ExitCode::DownloadFailure)
                          : static_cast<int>(ExitCode::Success);
}

QString videoItemJson(int index, const QString& guid, const QString& title, const QString& time,
    const QString& channel, qint64 length, bool isHighlight, const QString& listType)
{
    QJsonObject object{
        {QStringLiteral("event"), QStringLiteral("video")},
        {QStringLiteral("index"), index + 1},
        {QStringLiteral("guid"), guid},
        {QStringLiteral("title"), title},
        {QStringLiteral("time"), time},
        {QStringLiteral("channel"), channel},
        {QStringLiteral("length"), length},
        {QStringLiteral("highlight"), isHighlight},
        {QStringLiteral("listType"), listType},
    };
    return QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact));
}

} // namespace Cli
