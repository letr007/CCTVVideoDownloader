#pragma once

#include <QString>
#include <QList>

namespace Cli {

struct Options {
    QString command;
    QString url;
    QString guid;
    QString title;
    QString from;
    QString to;
    QString output;
    QString quality;
    QString select = QStringLiteral("latest");
    int threads = 0;
    bool json = false;
    bool debug = false;
    bool includeHighlights = false;
    bool mp4 = true;
    bool mp4Set = false;
};

enum class ExitCode {
    Success = 0,
    Usage = 2,
    ResolutionFailure = 3,
    DownloadFailure = 4,
    Cancelled = 130
};

bool parseSelection(const QString& value, int itemCount, QList<int>* indexes, QString* error);
int exitCodeForBatch(int failedJobs, int cancelledJobs);
QString videoItemJson(int index, const QString& guid, const QString& title, const QString& time,
    const QString& channel, const QString& image, const QString& brief, qint64 length, bool isHighlight,
    const QString& listType);

} // namespace Cli
