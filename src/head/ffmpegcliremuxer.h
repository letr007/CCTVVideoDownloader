#pragma once

#include <QProcess>
#include <QString>
#include <QStringList>
#include <functional>

struct FfmpegCliProcessRequest
{
	QString program;
	QStringList arguments;
	QString workingDirectory;
	int timeoutMs = 30000;
	std::function<bool()> cancellationRequested;
};

struct FfmpegCliProcessResult
{
	bool started = false;
	bool timedOut = false;
	int exitCode = -1;
	QProcess::ExitStatus exitStatus = QProcess::NormalExit;
	QString stdoutText;
	QString stderrText;
	QString errorString;
	bool cancelled = false;
};

struct FfmpegCliRemuxResult
{
	bool ok = false;
	QString code;
	QString message;
	QString outputPath;
	FfmpegCliProcessResult processResult;
};

class FfmpegCliRemuxer
{
#ifdef CORE_REGRESSION_TESTS
	friend class MediaFinalizerTestAdapter;
	friend class MediaFinalizer;
#endif

public:
	void setProcessTimeoutMs(int timeoutMs);
	FfmpegCliRemuxResult remuxTsToMp4(const QString& inputTsPath,
		const QString& outputMp4TempPath,
		const std::function<bool()>& cancellationRequested = {}) const;

private:
	QString decryptAssetsDir() const;

	#ifdef CORE_REGRESSION_TESTS
	void setTestProcessRunner(const std::function<FfmpegCliProcessResult(const FfmpegCliProcessRequest&)>& runner);
	void clearTestProcessRunner();
	void setTestDecryptAssetsDir(const QString& decryptAssetsDir);
	void clearTestDecryptAssetsDir();
	#endif

	int m_processTimeoutMs{30000};

	#ifdef CORE_REGRESSION_TESTS
	std::function<FfmpegCliProcessResult(const FfmpegCliProcessRequest&)> m_testProcessRunner;
	QString m_testDecryptAssetsDir;
	#endif
};
