#pragma once

#include <QObject>
#include <atomic>

class ConcatWorker : public QObject
{
    Q_OBJECT
public:
    explicit ConcatWorker(QObject* parent = nullptr);

    void setFilePath(const QString& path) { m_filePath = path; }
    void startConcat() { doConcat(); }
    void cancelConcat();

public slots:
    void doConcat();

signals:
    void concatFinished(bool ok, const QString& msg);

private:
    QString m_filePath;
    std::atomic_bool m_cancelled{false};
};
