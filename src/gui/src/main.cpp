#include "../include/cctvvideodownloader.h"
#include "logger.h"
#include "config.h"
#include "../include/theme.h"
#include <QtWidgets/QApplication>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    a.setApplicationName(QStringLiteral("CCTVVideoDownloader"));
    a.setApplicationVersion(QStringLiteral(CCTV_VIDEO_DOWNLOADER_BUILD_VERSION));
    Theme::apply(a);
    // Ensure Cmd+Q / last-window-closed both leave a normal process exit code.
    QObject::connect(&a, &QApplication::lastWindowClosed, &a, &QApplication::quit);
    CCTVVideoDownloader w;
    w.show();
    QObject::connect(&a, &QApplication::aboutToQuit, []() {
        // Close log resources only; do not delete the singleton during teardown.
        Logger::instance()->cleanup();
    });
    const int code = a.exec();
    Logger::instance()->cleanup();
    return code;
}
