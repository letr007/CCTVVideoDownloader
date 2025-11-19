#undef slots
#define slots Q_SLOTS

#include "../head/cctvvideodownloader.h"
#include "../head/logger.h"
#include "../head/config.h"
#include <QtWidgets/QApplication>
#include <QGuiApplication>
#include <openssl/ssl.h>

int main(int argc, char *argv[])
{
    SSL_library_init();
    OpenSSL_add_all_algorithms();
    QGuiApplication::setAttribute(Qt::AA_EnableHighDpiScaling);

    QApplication a(argc, argv);
    CCTVVideoDownloader w;
    w.show();
    QObject::connect(&a, &QApplication::aboutToQuit, []() {
        Logger::instance()->cleanup();
    });
    return a.exec();
}
