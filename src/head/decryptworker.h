#pragma once

#include <QObject>
#include <QProcess>
#include <QStringList>

#ifdef CORE_REGRESSION_TESTS
#include <functional>
#endif

struct DecryptProcessRequest
{
	QString program;
	QStringList arguments;
	QString workingDirectory;
	int timeoutMs = 30000;
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
};

class DecryptWorker : public QObject
{
	Q_OBJECT

#ifdef CORE_REGRESSION_TESTS
	friend class DecryptWorkerTestAdapter;
#endif

public:
	explicit DecryptWorker(QObject* parent = nullptr);

	void setParams(const QString& name, const QString& savePath) { m_name = name; m_savePath = savePath; }
	void setTranscodeToMp4(bool transcodeToMp4) { m_transcodeToMp4 = transcodeToMp4; }
	void setProcessTimeoutMs(int timeoutMs);

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
	bool m_transcodeToMp4 = true;
	int m_processTimeoutMs{30000};

	#ifdef CORE_REGRESSION_TESTS
	std::function<DecryptProcessResult(const DecryptProcessRequest&)> m_testProcessRunner;
	QString m_testDecryptAssetsDir;
	#endif
};
