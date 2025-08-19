#pragma once

#include <QObject>
#include <QString>
#include <QMap>
#include <QJsonDocument>

class NetworkCore : public QObject
{
    Q_OBJECT
public:
    explicit NetworkCore(QObject* parent = nullptr);
    void request(
        const QString url,
        const QByteArray& data = {},
        const QMap<QString, QString>& headers = {}
    );

signals:
    void responseReceived(const QJsonDocument& json, int status);
    void errorOccurred(const QString& error);

private:
    void performRequest(
        const QString& url,
        const QByteArray& data,
        const QMap<QString, QString>& headers
    );
};