#pragma once

#include <QObject>

class ConcatWorker : public QObject
{
    Q_OBJECT
public:
    explicit ConcatWorker(QObject* parent = nullptr);

    void setFilePath(const QString& path) { m_filePath = path; }

public slots:
    void doConcat();

signals:
    void concatFinished(bool ok, const QString& msg);

private:
    QString m_filePath;
};