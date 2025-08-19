#undef slots
#include <cpr/cpr.h>
#define slots Q_SLOTS

#include "cctvvideodownloader.h"
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
    return a.exec();
}
