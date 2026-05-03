#pragma once

#include <QObject>
#include <QProcess>
#include <QStringList>
#include <atomic>
#include <functional>

struct DecryptProcessRequest
{
	QString program;
	QStringList arguments;
	QString workingDirectory;
	int timeoutMs = 30000;
	std::function<bool()> cancellationRequested;
};

struct DecryptProcessResult
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

class ProductionCoordinatorDecryptStage;

class DecryptWorker : public QObject
{
	Q_OBJECT

#ifdef CORE_REGRESSION_TESTS
	friend class DecryptWorkerTestAdapter;
	friend class ProductionCoordinatorDecryptStage;
#endif

public:
	explicit DecryptWorker(QObject* parent = nullptr);

	void setParams(const QString& name, const QString& savePath) { m_name = name; m_savePath = savePath; }
	void setTaskDirectory(const QString& taskDirectory) { m_taskDirectory = taskDirectory; }
	void setTranscodeToMp4(bool transcodeToMp4) { m_transcodeToMp4 = transcodeToMp4; }
	void setProcessTimeoutMs(int timeoutMs);
	void startDecrypt() { doDecrypt(); }
	void cancelDecrypt();

public slots:
	void doDecrypt();

signals:
	void decryptFinished(bool ok, const QString& msg);

private:
	QString decryptAssetsDir() const;

	#ifdef CORE_REGRESSION_TESTS
	void setTestProcessRunner(const std::function<DecryptProcessResult(const DecryptProcessRequest&)>& runner);
	void clearTestProcessRunner();
	void setTestDecryptAssetsDir(const QString& decryptAssetsDir);
	void clearTestDecryptAssetsDir();
	#endif

	QString m_name;
	QString m_savePath;
	QString m_taskDirectory;
	bool m_transcodeToMp4 = true;
	int m_processTimeoutMs{30000};
	std::atomic_bool m_cancelled{false};

	#ifdef CORE_REGRESSION_TESTS
	std::function<DecryptProcessResult(const DecryptProcessRequest&)> m_testProcessRunner;
	QString m_testDecryptAssetsDir;
	#endif
};
