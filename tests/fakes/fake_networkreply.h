#pragma once

#include <QNetworkAccessManager>
#include <QNetworkReply>

class FakeNetworkReply : public QNetworkReply
{
    Q_OBJECT

public:
    struct ResponseSpec {
        QByteArray body;
        QNetworkReply::NetworkError error = QNetworkReply::NoError;
        QString errorString;
        int finishDelayMs = 0;
        qint64 bytesTotal = -2;
    };

    FakeNetworkReply(const QNetworkRequest& request,
                     QNetworkAccessManager::Operation operation,
                     const ResponseSpec& spec,
                     QObject* parent = nullptr);

    void abort() override;
    bool isSequential() const override;
    qint64 bytesAvailable() const override;

    bool wasAborted() const;
    bool hasStarted() const;

public slots:
    void start();

protected:
    qint64 readData(char* data, qint64 maxSize) override;

private slots:
    void finishReply();

private:
    qint64 totalBytes() const;
    void emitReadyReadAndProgress();
    void finalizeAsFinished();
    void finalizeWithError(QNetworkReply::NetworkError error, const QString& errorString);

    QByteArray m_body;
    qint64 m_readOffset = 0;
    QNetworkReply::NetworkError m_configuredError = QNetworkReply::NoError;
    QString m_configuredErrorString;
    int m_finishDelayMs = 0;
    qint64 m_bytesTotal = -2;
    bool m_aborted = false;
    bool m_started = false;
    bool m_finished = false;
};
