#pragma once

#include <QString>

struct DirectMediaFinalizeResult
{
	bool ok = false;
	QString code;
	QString message;
	QString finalPath;
};

DirectMediaFinalizeResult finalizeDirectTsTask(const QString& title,
	const QString& savePath,
	bool transcodeToMp4);
