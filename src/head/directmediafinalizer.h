#pragma once

#include "ffmpegcliremuxer.h"

#include <QObject>
#include <QString>
#include <atomic>
#include <functional>

struct DirectMediaFinalizeResult
{
	bool ok = false;
	QString code;
	QString message;
	QString finalPath;
};

DirectMediaFinalizeResult finalizeDirectTsTask(const QString& title,
	const QString& savePath,
	bool transcodeToMp4,
	const QString& taskDirectory = QString(),
	const std::function<bool()>& cancellationRequested = {}
#ifdef CORE_REGRESSION_TESTS
	,
	const std::function<FfmpegCliProcessResult(const FfmpegCliProcessRequest&)>& testProcessRunner = {},
	const QString& testDecryptAssetsDir = QString()
#endif
	);

class ProductionCoordinatorDirectFinalizeStage;

class DirectFinalizeWorker : public QObject
{
	Q_OBJECT

#ifdef CORE_REGRESSION_TESTS
	friend class DirectFinalizeWorkerTestAdapter;
	friend class ProductionCoordinatorDirectFinalizeStage;
#endif

public:
	explicit DirectFinalizeWorker(QObject* parent = nullptr) : QObject(parent) {}
	void startFinalize(const QString& title, const QString& savePath, bool transcodeToMp4) { doWork(title, savePath, transcodeToMp4); }
	void setTaskDirectory(const QString& taskDirectory) { m_taskDirectory = taskDirectory; }
	void cancelFinalize();

public slots:
	void doWork(const QString& title, const QString& savePath, bool transcodeToMp4);

signals:
	void finished(bool ok, const QString& code, const QString& message, const QString& finalPath);

private:
	std::atomic_bool m_cancelled{false};
	QString m_taskDirectory;

#ifdef CORE_REGRESSION_TESTS
	void setTestProcessRunner(const std::function<FfmpegCliProcessResult(const FfmpegCliProcessRequest&)>& runner);
	void clearTestProcessRunner();
	void setTestDecryptAssetsDir(const QString& decryptAssetsDir);
	void clearTestDecryptAssetsDir();

	std::function<FfmpegCliProcessResult(const FfmpegCliProcessRequest&)> m_testProcessRunner;
	QString m_testDecryptAssetsDir;
#endif
};
