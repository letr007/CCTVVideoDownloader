#include "../head/cctvvideodownloader.h"
#include "../head/logger.h"
#include "../head/config.h"
#include <QtWidgets/QApplication>
#include <QGuiApplication>

int main(int argc, char *argv[])
{
    QGuiApplication::setAttribute(Qt::AA_EnableHighDpiScaling);

    QApplication a(argc, argv);
    CCTVVideoDownloader w;
    w.show();
    QObject::connect(&a, &QApplication::aboutToQuit, []() {
        Logger::instance()->cleanup();
    });
    return a.exec();
}
