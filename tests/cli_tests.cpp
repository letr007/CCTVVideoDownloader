#include "cli_logging.h"
#include "cli_output.h"
#include "cli_support.h"

#include "downloadjob.h"

#include <QJsonDocument>
#include <QProcess>
#include <QTemporaryFile>
#include <QtTest>

extern "C" {
#include <libavutil/log.h>
}

#ifdef Q_OS_WIN
#include <io.h>
#else
#include <unistd.h>
#endif

namespace {

int standardErrorDescriptor()
{
#ifdef Q_OS_WIN
    return _fileno(stderr);
#else
    return fileno(stderr);
#endif
}

int duplicateDescriptor(int descriptor)
{
#ifdef Q_OS_WIN
    return _dup(descriptor);
#else
    return dup(descriptor);
#endif
}

int duplicateToDescriptor(qint64 source, int destination)
{
#ifdef Q_OS_WIN
    return _dup2(static_cast<int>(source), destination);
#else
    return dup2(static_cast<int>(source), destination);
#endif
}

void closeDescriptor(int descriptor)
{
#ifdef Q_OS_WIN
    _close(descriptor);
#else
    close(descriptor);
#endif
}

class StderrCapture final
{
public:
    bool start()
    {
        if (!m_file.open()) {
            return false;
        }

        fflush(stderr);
        const int standardError = standardErrorDescriptor();
        m_savedDescriptor = duplicateDescriptor(standardError);
        if (m_savedDescriptor == -1) {
            return false;
        }
        if (duplicateToDescriptor(m_file.handle(), standardError) == -1) {
            closeDescriptor(m_savedDescriptor);
            m_savedDescriptor = -1;
            return false;
        }
        return true;
    }

    QByteArray finish()
    {
        restore();
        m_file.seek(0);
        return m_file.readAll();
    }

    ~StderrCapture()
    {
        restore();
    }

private:
    void restore()
    {
        if (m_savedDescriptor == -1) {
            return;
        }

        fflush(stderr);
        duplicateToDescriptor(m_savedDescriptor, standardErrorDescriptor());
        closeDescriptor(m_savedDescriptor);
        m_savedDescriptor = -1;
    }

    QTemporaryFile m_file;
    int m_savedDescriptor = -1;
};

class ScopedCliFfmpegLogging final
{
public:
    ScopedCliFfmpegLogging()
        : m_previousMessageHandler(qInstallMessageHandler(nullptr))
    {
        Cli::installQtMessageHandler();
        Cli::installFfmpegLogCallback();
    }

    ~ScopedCliFfmpegLogging()
    {
        av_log_set_callback(av_log_default_callback);
        qInstallMessageHandler(m_previousMessageHandler);
    }

private:
    QtMessageHandler m_previousMessageHandler;
};

} // namespace

class CliTests final : public QObject
{
    Q_OBJECT

private slots:
    void parsesSelections();
    void rejectsInvalidSelections();
    void serializesJsonLine();
    void keepsJsonProgressProtocolLineBased();
    void writesInteractiveProgressOnOneLine();
    void terminatesInteractiveProgressBeforeErrors();
    void keepsNonInteractiveProgressLineBased();
    void mapsExitCodes();
    void executableHelpSucceeds();
    void executableRejectsInvalidCommand();
    void executableRejectsInvalidGuid();
    void executableSilencesQtLogsByDefault();
    void executableWritesQtLogsToStderrWithDebug();
    void ffmpegLogsAreSilentByDefault();
    void ffmpegLogsAreWrittenToStderrWithDebug();

private:
    static void runCli(QProcess& process, const QStringList& arguments);
};

void CliTests::runCli(QProcess& process, const QStringList& arguments)
{
    process.start(QStringLiteral(CLI_EXECUTABLE), arguments);
    QVERIFY2(process.waitForStarted(), qPrintable(process.errorString()));
    QVERIFY2(process.waitForFinished(), qPrintable(process.errorString()));
}

void CliTests::parsesSelections()
{
    QList<int> indexes;
    QString error;
    QVERIFY(Cli::parseSelection(QStringLiteral("latest"), 3, &indexes, &error));
    QCOMPARE(indexes, QList<int>({0}));
    QVERIFY(Cli::parseSelection(QStringLiteral("all"), 3, &indexes, &error));
    QCOMPARE(indexes, QList<int>({0, 1, 2}));
    QVERIFY(Cli::parseSelection(QStringLiteral("3, 1, 3"), 3, &indexes, &error));
    QCOMPARE(indexes, QList<int>({2, 0}));
}

void CliTests::rejectsInvalidSelections()
{
    QList<int> indexes;
    QString error;
    QVERIFY(!Cli::parseSelection(QStringLiteral("0"), 2, &indexes, &error));
    QCOMPARE(error, QStringLiteral("invalid selection index: 0"));
    QVERIFY(!Cli::parseSelection(QStringLiteral("two"), 2, &indexes, &error));
    QCOMPARE(error, QStringLiteral("invalid selection index: two"));
}

void CliTests::serializesJsonLine()
{
    const QJsonDocument document = QJsonDocument::fromJson(Cli::videoItemJson(
        0, QStringLiteral("guid"), QStringLiteral("title"), QStringLiteral("2026-01-01"),
        QStringLiteral("CCTV-1"), 12, true, QStringLiteral("highlight")).toUtf8());
    QVERIFY(document.isObject());
    const QJsonObject object = document.object();
    QCOMPARE(object.value(QStringLiteral("event")).toString(), QStringLiteral("video"));
    QCOMPARE(object.value(QStringLiteral("index")).toInt(), 1);
    QCOMPARE(object.value(QStringLiteral("guid")).toString(), QStringLiteral("guid"));
    QCOMPARE(object.value(QStringLiteral("highlight")).toBool(), true);
}

void CliTests::keepsJsonProgressProtocolLineBased()
{
    FILE* standardOutput = tmpfile();
    FILE* standardError = tmpfile();
    QVERIFY(standardOutput != nullptr);
    QVERIFY(standardError != nullptr);

    {
        Cli::Output output(true, standardOutput, standardError, true);
        DownloadJob job;
        job.request.videoTitle = QStringLiteral("video");
        job.progressPercent = 50;
        output.jobChanged(job);
    }

    fflush(standardOutput);
    rewind(standardOutput);
    char buffer[128] = {};
    const size_t bytesRead = fread(buffer, 1, sizeof(buffer), standardOutput);
    const QByteArray outputData(buffer, static_cast<qsizetype>(bytesRead));
    QVERIFY(outputData.endsWith('\n'));
    QVERIFY(!outputData.contains('\r'));
    const QJsonDocument document = QJsonDocument::fromJson(outputData);
    QVERIFY(document.isObject());
    const QJsonObject event = document.object();
    QCOMPARE(event.value(QStringLiteral("event")).toString(), QStringLiteral("job"));
    QCOMPARE(event.value(QStringLiteral("title")).toString(), QStringLiteral("video"));
    QCOMPARE(event.value(QStringLiteral("progress")).toInt(), 50);

    fclose(standardOutput);
    fclose(standardError);
}

void CliTests::writesInteractiveProgressOnOneLine()
{
    FILE* standardOutput = tmpfile();
    FILE* standardError = tmpfile();
    QVERIFY(standardOutput != nullptr);
    QVERIFY(standardError != nullptr);

    {
        Cli::Output output(false, standardOutput, standardError, true);
        DownloadJob job;
        job.request.videoTitle = QStringLiteral("video");
        job.progressPercent = 25;
        output.jobChanged(job);
        job.progressPercent = 50;
        output.jobChanged(job);
        output.downloadComplete(1, 0, 0, 1);
    }

    fflush(standardOutput);
    rewind(standardOutput);
    char buffer[128] = {};
    const size_t bytesRead = fread(buffer, 1, sizeof(buffer), standardOutput);
    const QByteArray outputData(buffer, static_cast<qsizetype>(bytesRead));
    QCOMPARE(outputData, QByteArray("\r\x1b[2Kvideo: 25%\r\x1b[2Kvideo: 50%\ncompleted: 1, failed: 0, cancelled: 0 / 1\n"));

    fclose(standardOutput);
    fclose(standardError);
}

void CliTests::terminatesInteractiveProgressBeforeErrors()
{
    FILE* standardOutput = tmpfile();
    FILE* standardError = tmpfile();
    QVERIFY(standardOutput != nullptr);
    QVERIFY(standardError != nullptr);

    {
        Cli::Output output(false, standardOutput, standardError, true);
        DownloadJob job;
        job.request.videoTitle = QStringLiteral("video");
        job.progressPercent = 50;
        output.jobChanged(job);
        output.usageError(QStringLiteral("invalid input"));
    }

    fflush(standardOutput);
    rewind(standardOutput);
    char outputBuffer[64] = {};
    const size_t outputBytesRead = fread(outputBuffer, 1, sizeof(outputBuffer), standardOutput);
    QCOMPARE(QByteArray(outputBuffer, static_cast<qsizetype>(outputBytesRead)), QByteArray("\r\x1b[2Kvideo: 50%\n"));

    fflush(standardError);
    rewind(standardError);
    char errorBuffer[64] = {};
    const size_t errorBytesRead = fread(errorBuffer, 1, sizeof(errorBuffer), standardError);
    QCOMPARE(QByteArray(errorBuffer, static_cast<qsizetype>(errorBytesRead)), QByteArray("error: invalid input\n"));

    fclose(standardOutput);
    fclose(standardError);
}

void CliTests::keepsNonInteractiveProgressLineBased()
{
    FILE* standardOutput = tmpfile();
    FILE* standardError = tmpfile();
    QVERIFY(standardOutput != nullptr);
    QVERIFY(standardError != nullptr);

    {
        Cli::Output output(false, standardOutput, standardError, false);
        DownloadJob job;
        job.request.videoTitle = QStringLiteral("video");
        job.progressPercent = 25;
        output.jobChanged(job);
        job.progressPercent = 50;
        output.jobChanged(job);
        output.downloadComplete(1, 0, 0, 1);
    }

    fflush(standardOutput);
    rewind(standardOutput);
    char buffer[128] = {};
    const size_t bytesRead = fread(buffer, 1, sizeof(buffer), standardOutput);
    const QByteArray outputData(buffer, static_cast<qsizetype>(bytesRead));
    QCOMPARE(outputData, QByteArray("video: 25%\nvideo: 50%\ncompleted: 1, failed: 0, cancelled: 0 / 1\n"));

    fclose(standardOutput);
    fclose(standardError);
}

void CliTests::mapsExitCodes()
{
    QCOMPARE(Cli::exitCodeForBatch(0, 0), static_cast<int>(Cli::ExitCode::Success));
    QCOMPARE(Cli::exitCodeForBatch(1, 0), static_cast<int>(Cli::ExitCode::DownloadFailure));
    QCOMPARE(Cli::exitCodeForBatch(0, 1), static_cast<int>(Cli::ExitCode::Cancelled));
}

void CliTests::executableHelpSucceeds()
{
    QProcess process;
    runCli(process, {QStringLiteral("--help")});
    QCOMPARE(process.exitStatus(), QProcess::NormalExit);
    QCOMPARE(process.exitCode(), 0);
    const QByteArray standardOutput = process.readAllStandardOutput();
    QVERIFY(standardOutput.contains("Usage:"));
    QVERIFY(standardOutput.contains("--debug"));
}

void CliTests::executableRejectsInvalidCommand()
{
    QProcess process;
    runCli(process, {QStringLiteral("invalid")});
    QCOMPARE(process.exitStatus(), QProcess::NormalExit);
    QCOMPARE(process.exitCode(), static_cast<int>(Cli::ExitCode::Usage));
    QVERIFY(process.readAllStandardError().contains("command must be list or download"));
}

void CliTests::executableRejectsInvalidGuid()
{
    QProcess process;
    runCli(process, {QStringLiteral("download"), QStringLiteral("--guid"), QStringLiteral("bad"),
        QStringLiteral("--title"), QStringLiteral("title")});
    QCOMPARE(process.exitStatus(), QProcess::NormalExit);
    QCOMPARE(process.exitCode(), static_cast<int>(Cli::ExitCode::Usage));
    QVERIFY(process.readAllStandardError().contains("32-character hexadecimal GUID"));
}

void CliTests::executableSilencesQtLogsByDefault()
{
    QProcess process;
    runCli(process, {QStringLiteral("--version")});
    QCOMPARE(process.exitStatus(), QProcess::NormalExit);
    QCOMPARE(process.exitCode(), 0);
    QVERIFY(process.readAllStandardOutput().startsWith("cctv-dl "));
    QCOMPARE(process.readAllStandardError(), QByteArray());
}

void CliTests::executableWritesQtLogsToStderrWithDebug()
{
    QProcess process;
    runCli(process, {QStringLiteral("--debug"), QStringLiteral("--json"), QStringLiteral("--version")});
    QCOMPARE(process.exitStatus(), QProcess::NormalExit);
    QCOMPARE(process.exitCode(), 0);
    const QByteArray standardOutput = process.readAllStandardOutput();
    QVERIFY(standardOutput.startsWith("cctv-dl "));
    QVERIFY(!standardOutput.contains("INFO:"));
    QVERIFY(process.readAllStandardError().contains("INFO: cctv-dl started"));
}

void CliTests::ffmpegLogsAreSilentByDefault()
{
    QByteArray standardError;
    {
        ScopedCliFfmpegLogging logging;
        StderrCapture capture;
        QVERIFY(capture.start());
        Cli::setQtDebugLoggingEnabled(false);
        av_log(nullptr, AV_LOG_WARNING, "test warning\n");
        av_log(nullptr, AV_LOG_ERROR, "test error\n");
        standardError = capture.finish();
    }

    QCOMPARE(standardError, QByteArray());
}

void CliTests::ffmpegLogsAreWrittenToStderrWithDebug()
{
    QByteArray standardError;
    {
        ScopedCliFfmpegLogging logging;
        StderrCapture capture;
        QVERIFY(capture.start());
        Cli::setQtDebugLoggingEnabled(true);
        av_log(nullptr, AV_LOG_WARNING, "test warning\n");
        av_log(nullptr, AV_LOG_ERROR, "test error\n");
        standardError = capture.finish();
    }

    QVERIFY(standardError.contains("WARNING: FFmpeg: test warning"));
    QVERIFY(standardError.contains("CRITICAL: FFmpeg: test error"));
}

QTEST_APPLESS_MAIN(CliTests)
#include "cli_tests.moc"
