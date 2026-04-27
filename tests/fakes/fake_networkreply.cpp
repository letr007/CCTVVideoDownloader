#include "fake_networkreply.h"

#include <algorithm>
#include <cstring>
#include <QTimer>

FakeNetworkReply::FakeNetworkReply(const QNetworkRequest& request,
                                   QNetworkAccessManager::Operation operation,
                                   const ResponseSpec& spec,
                                   QObject* parent)
    : QNetworkReply(parent)
    , m_body(spec.body)
    , m_configuredError(spec.error)
    , m_configuredErrorString(spec.errorString)
    , m_finishDelayMs(std::max(0, spec.finishDelayMs))
    , m_bytesTotal(spec.bytesTotal)
{
    setRequest(request);
    setUrl(request.url());
    setOperation(operation);
    open(QIODevice::ReadOnly | QIODevice::Unbuffered);
    setHeader(QNetworkRequest::ContentLengthHeader, totalBytes());
}

void FakeNetworkReply::abort()
{
    m_aborted = true;

    if (m_finished) {
        return;
    }

    m_body.clear();
    m_readOffset = 0;
    finalizeWithError(QNetworkReply::OperationCanceledError, QStringLiteral("Operation canceled"));
}

bool FakeNetworkReply::isSequential() const
{
    return true;
}

qint64 FakeNetworkReply::bytesAvailable() const
{
    return (m_body.size() - m_readOffset) + QIODevice::bytesAvailable();
}

bool FakeNetworkReply::wasAborted() const
{
    return m_aborted;
}

bool FakeNetworkReply::hasStarted() const
{
    return m_started;
}

void FakeNetworkReply::start()
{
    if (m_started || m_finished) {
        return;
    }

    m_started = true;

    if (m_configuredError == QNetworkReply::NoError) {
        emitReadyReadAndProgress();
    }

    if (m_finishDelayMs > 0) {
        QTimer::singleShot(m_finishDelayMs, this, &FakeNetworkReply::finishReply);
        return;
    }

    finishReply();
}

qint64 FakeNetworkReply::readData(char* data, qint64 maxSize)
{
    if (m_readOffset >= m_body.size()) {
        return -1;
    }

    const qint64 bytesToCopy = std::min(maxSize, static_cast<qint64>(m_body.size() - m_readOffset));
    memcpy(data, m_body.constData() + m_readOffset, static_cast<size_t>(bytesToCopy));
    m_readOffset += bytesToCopy;
    return bytesToCopy;
}

void FakeNetworkReply::finishReply()
{
    if (m_finished) {
        return;
    }

    if (m_aborted) {
        finalizeWithError(QNetworkReply::OperationCanceledError, QStringLiteral("Operation canceled"));
        return;
    }

    if (m_configuredError != QNetworkReply::NoError) {
        finalizeWithError(m_configuredError, m_configuredErrorString);
        return;
    }

    finalizeAsFinished();
}

qint64 FakeNetworkReply::totalBytes() const
{
    return m_bytesTotal >= 0 ? m_bytesTotal : m_body.size();
}

void FakeNetworkReply::emitReadyReadAndProgress()
{
    if (!m_body.isEmpty()) {
        emit readyRead();
    }

    emit downloadProgress(m_body.size(), totalBytes());
}

void FakeNetworkReply::finalizeAsFinished()
{
    if (m_finished) {
        return;
    }

    m_finished = true;
    setFinished(true);
    emit finished();
}

void FakeNetworkReply::finalizeWithError(QNetworkReply::NetworkError error, const QString& errorString)
{
    if (m_finished) {
        return;
    }

    m_finished = true;
    setError(error, errorString.isEmpty() ? QStringLiteral("Fake network reply error") : errorString);
    setFinished(true);
    emit errorOccurred(error);
    emit finished();
}
