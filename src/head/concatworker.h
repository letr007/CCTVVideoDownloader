#pragma once

#include <QObject>
#include <QStringList>
#include <atomic>

class ConcatWorker : public QObject
{
    Q_OBJECT
public:
    explicit ConcatWorker(QObject* parent = nullptr);

    void setConcatInputs(const QStringList& inputPaths, const QString& outputPath)
    {
        m_inputPaths = inputPaths;
        m_outputPath = outputPath;
    }
    void startConcat() { doConcat(); }
    void cancelConcat();

public slots:
    void doConcat();

signals:
    void concatFinished(bool ok, const QString& msg);

private:
    QStringList m_inputPaths;
    QString m_outputPath;
    std::atomic_bool m_cancelled{false};
};
