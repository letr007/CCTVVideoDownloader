#include "../head/cctvvideodownloader.h"
#include "../head/logger.h"
#include "../head/config.h"
#include <QtWidgets/QApplication>
#include <QGuiApplication>

int main(int argc, char *argv[])
{
    QGuiApplication::setAttribute(Qt::AA_EnableHighDpiScaling);

    QApplication a(argc, argv);
    a.setApplicationVersion(QStringLiteral(CCTV_VIDEO_DOWNLOADER_BUILD_VERSION));
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
