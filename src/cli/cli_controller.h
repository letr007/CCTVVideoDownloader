#pragma once

#include "cli_support.h"
#include "cli_output.h"

#include "apiservice.h"

#include <QObject>
#include <QMap>
#include <functional>

class QCoreApplication;
class DownloadCoordinator;

namespace Cli {

class Controller final : public QObject
{
    Q_OBJECT

public:
    Controller(QCoreApplication& application, const Options& options);

    void start();
    void cancel();

private:
    void resolveUrl(const QString& url, const std::function<void()>& done);
    void startDownload();
    void applyConfiguredDefaults();

    QCoreApplication& m_application;
    Options m_options;
    Output m_output;
    class APIService* m_api = nullptr;
    class DownloadCoordinator* m_coordinator = nullptr;
    QMap<int, VideoItem> m_videos;
    QString m_from;
    QString m_to;
    bool m_cancelled = false;
};

} // namespace Cli
