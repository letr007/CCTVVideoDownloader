#include "networkcore.h"
#include <cpr/cpr.h>
#include <openssl/ssl.h>
#include <QDebug>
#include <QThread>

NetworkCore::NetworkCore(QObject* parent) : QObject(parent) {}

void NetworkCore::request(
    const QString url,
    const QByteArray& data,
    const QMap<QString, QString>& headers
) {
    std::string encodedUrl = url.toUtf8().constData();
    cpr::Response r;
    r = cpr::Get(cpr::Url{encodedUrl});
    if (r.status_code == 200)
    {
        qDebug() << r.text;
    }
    else {
        qDebug() << "Url:" << url;
        qDebug() << r.status_code;
    }
}
