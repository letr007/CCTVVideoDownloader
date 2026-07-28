#include "cli_parser.h"

#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDate>
#include <QUrl>

#include <algorithm>

namespace {

bool validMonth(const QString& value)
{
    return value.size() == 6 && std::all_of(value.cbegin(), value.cend(), [](QChar character) { return character.isDigit(); })
        && QDate::fromString(value + QStringLiteral("01"), QStringLiteral("yyyyMMdd")).isValid();
}

bool validUrl(const QString& value)
{
    const QUrl url(value);
    return url.isValid() && !url.host().isEmpty()
        && (url.scheme() == QStringLiteral("http") || url.scheme() == QStringLiteral("https"));
}

bool validGuid(const QString& value)
{
    if (value.size() != 32) {
        return false;
    }
    for (const QChar character : value) {
        const ushort code = character.unicode();
        if (!((code >= '0' && code <= '9')
                || (code >= 'a' && code <= 'f')
                || (code >= 'A' && code <= 'F'))) {
            return false;
        }
    }
    return true;
}

Cli::ParseResult usageError(const QString& message)
{
    Cli::ParseResult result;
    result.exitCode = static_cast<int>(Cli::ExitCode::Usage);
    result.error = QStringLiteral("error: %1\nRun cctv-video --help for usage.").arg(message);
    result.shouldExit = true;
    return result;
}

} // namespace

namespace Cli {

ParseResult parseArguments(const QStringList& arguments)
{
    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Headless CCTV video browser and downloader."));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument(QStringLiteral("command"), QStringLiteral("list or download"));
    parser.addPositionalArgument(QStringLiteral("url"), QStringLiteral("CCTV programme URL."), QStringLiteral("[url]"));

    const QCommandLineOption fromOption(QStringLiteral("from"), QStringLiteral("First month (yyyyMM)."), QStringLiteral("yyyyMM"));
    const QCommandLineOption toOption(QStringLiteral("to"), QStringLiteral("Last month (yyyyMM)."), QStringLiteral("yyyyMM"));
    const QCommandLineOption jsonOption(QStringLiteral("json"), QStringLiteral("Emit JSON Lines events."));
    const QCommandLineOption debugOption(QStringLiteral("debug"), QStringLiteral("Write Qt internal logs to standard error."));
    const QCommandLineOption highlightsOption(QStringLiteral("include-highlights"), QStringLiteral("Include highlights in list results."));
    const QCommandLineOption selectOption(QStringLiteral("select"), QStringLiteral("latest, all, or comma-separated 1-based indexes."), QStringLiteral("selection"), QStringLiteral("latest"));
    const QCommandLineOption guidOption(QStringLiteral("guid"), QStringLiteral("32-character hexadecimal video GUID for direct download."), QStringLiteral("guid"));
    const QCommandLineOption titleOption(QStringLiteral("title"), QStringLiteral("Required title with --guid."), QStringLiteral("title"));
    const QCommandLineOption outputOption(QStringLiteral("output"), QStringLiteral("Output directory."), QStringLiteral("directory"));
    const QCommandLineOption qualityOption(QStringLiteral("quality"), QStringLiteral("Playback quality."), QStringLiteral("quality"));
    const QCommandLineOption threadsOption(QStringLiteral("threads"), QStringLiteral("Download threads."), QStringLiteral("count"));
    const QCommandLineOption mp4Option(QStringLiteral("mp4"), QStringLiteral("Remux output to MP4."));
    const QCommandLineOption noMp4Option(QStringLiteral("no-mp4"), QStringLiteral("Keep transport-stream output."));
    parser.addOptions({fromOption, toOption, jsonOption, debugOption, highlightsOption, selectOption, guidOption, titleOption,
        outputOption, qualityOption, threadsOption, mp4Option, noMp4Option});

    if (!parser.parse(arguments)) {
        return usageError(parser.errorText());
    }

    ParseResult result;
    result.options.debug = parser.isSet(debugOption);
    if (parser.isSet(QStringLiteral("help"))) {
        result.output = parser.helpText();
        result.shouldExit = true;
        return result;
    }
    if (parser.isSet(QStringLiteral("version"))) {
        result.output = QStringLiteral("%1 %2").arg(QCoreApplication::applicationName(), QCoreApplication::applicationVersion());
        result.shouldExit = true;
        return result;
    }

    const QStringList positional = parser.positionalArguments();
    if (positional.isEmpty() || (positional.first() != QStringLiteral("list") && positional.first() != QStringLiteral("download"))) {
        return usageError(QStringLiteral("command must be list or download"));
    }
    if (positional.size() > 2) {
        return usageError(QStringLiteral("too many positional arguments"));
    }

    Options& options = result.options;
    options.command = positional.first();
    if (positional.size() == 2) {
        options.url = positional.at(1);
    }
    options.guid = parser.value(guidOption).trimmed();
    options.title = parser.value(titleOption).trimmed();
    options.from = parser.value(fromOption).trimmed();
    options.to = parser.value(toOption).trimmed();
    options.output = parser.value(outputOption).trimmed();
    options.quality = parser.value(qualityOption).trimmed();
    options.select = parser.value(selectOption);
    options.json = parser.isSet(jsonOption);
    options.includeHighlights = parser.isSet(highlightsOption);
    options.mp4 = !parser.isSet(noMp4Option);
    options.mp4Set = parser.isSet(mp4Option) || parser.isSet(noMp4Option);

    if (parser.isSet(mp4Option) && parser.isSet(noMp4Option)) {
        return usageError(QStringLiteral("--mp4 and --no-mp4 cannot be used together"));
    }
    if (parser.isSet(mp4Option)) {
        options.mp4 = true;
    }
    if (parser.isSet(threadsOption)) {
        bool ok = false;
        options.threads = parser.value(threadsOption).toInt(&ok);
        if (!ok || options.threads < 1) {
            return usageError(QStringLiteral("--threads must be a positive integer"));
        }
    }
    if ((!options.from.isEmpty() && !validMonth(options.from)) || (!options.to.isEmpty() && !validMonth(options.to))) {
        return usageError(QStringLiteral("--from and --to must use yyyyMM"));
    }

    if (options.command == QStringLiteral("list")) {
        if (options.url.isEmpty() || !validUrl(options.url) || !options.guid.isEmpty() || !options.title.isEmpty()
            || parser.isSet(selectOption) || parser.isSet(outputOption) || parser.isSet(qualityOption)
            || parser.isSet(threadsOption) || parser.isSet(mp4Option) || parser.isSet(noMp4Option)) {
            return usageError(QStringLiteral("list requires one http/https URL and only list options"));
        }
        return result;
    }

    if (!options.guid.isEmpty()) {
        if (!validGuid(options.guid) || options.title.isEmpty() || !options.url.isEmpty()
            || parser.isSet(fromOption) || parser.isSet(toOption) || parser.isSet(highlightsOption)
            || parser.isSet(selectOption)) {
            return usageError(QStringLiteral("download --guid requires a 32-character hexadecimal GUID, --title, and no URL/list options"));
        }
        return result;
    }

    if (options.url.isEmpty() || !validUrl(options.url) || !options.title.isEmpty()) {
        return usageError(QStringLiteral("download requires an http/https URL, or --guid together with --title"));
    }
    if (parser.isSet(fromOption) || parser.isSet(toOption) || parser.isSet(highlightsOption)) {
        return usageError(QStringLiteral("--from, --to, and --include-highlights are only valid with list"));
    }
    return result;
}

} // namespace Cli
