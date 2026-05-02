#pragma once

#include "ffmpegcliremuxer.h"
#include "mediacontainervalidator.h"

#include <QString>

struct MediaFinalizeResult
{
	bool ok = false;
	QString code;
	QString message;
	QString finalPath;
	MediaContainerType publishedType = MediaContainerType::Unknown;
};

class MediaFinalizer
{
#ifdef CORE_REGRESSION_TESTS
	friend class MediaFinalizerTestAdapter;
#endif

public:
	void setProcessTimeoutMs(int timeoutMs);
	MediaFinalizeResult finalize(const QString& stagingTsPath,
		const QString& title,
		const QString& saveDir,
		MediaContainerType desiredContainer);

private:
	QString sanitizedTitle(const QString& title) const;
	QString uniqueOutputPath(const QString& saveDir, const QString& baseName, const QString& suffix) const;

	FfmpegCliRemuxer m_remuxer;
};
