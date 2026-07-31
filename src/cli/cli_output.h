#pragma once

#include "cli_support.h"

#include <QTextStream>

#include <cstdio>

struct DownloadJob;

enum class DownloadJobState;
enum class DownloadErrorCategory;
class QJsonObject;

namespace Cli {

class Output final
{
public:
    explicit Output(bool json);
    Output(bool json, FILE* stdoutDevice, FILE* stderrDevice, bool stdoutIsTerminal);

    void video(int index, const QString& guid, const QString& title, const QString& time,
        const QString& channel, const QString& image, const QString& brief, qint64 length, bool isHighlight,
        const QString& listType);
    void listComplete(int count);
    void resolutionFailed(const QString& message);
    void jobChanged(const DownloadJob& job);
    void jobFinished(const DownloadJob& job);
    void downloadComplete(int completed, int failed, int cancelled, int total);
    void downloadStartFailed();
    void usageError(const QString& message);

private:
    void jsonLine(const QJsonObject& object);
    void stdoutLine(const QString& line);
    void stderrLine(const QString& line);
    void finishProgressLine();

    bool m_json = false;
    bool m_interactiveProgress = false;
    bool m_progressVisible = false;
    QTextStream m_stdout;
    QTextStream m_stderr;
};

QString stateName(DownloadJobState state);
QString errorCategoryName(DownloadErrorCategory category);

} // namespace Cli
