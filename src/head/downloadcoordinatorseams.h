#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariant>

#include "downloadjob.h"
#include "downloadmodel.h"

class CoordinatorResolveService : public QObject
{
    Q_OBJECT

public:
    using QObject::QObject;
    ~CoordinatorResolveService() override = default;

    virtual void startResolve(const QString& guid, const QString& quality) = 0;
    virtual void cancelResolve() = 0;

signals:
    void resolved(const QStringList& segmentUrls, bool is4K);
    void failed(DownloadErrorCategory category, const QString& message);
    void cancelled();
};

class CoordinatorDownloadStage : public QObject
{
    Q_OBJECT

public:
    using QObject::QObject;
    ~CoordinatorDownloadStage() override = default;

    virtual void startDownload(const QStringList& segmentUrls, const QString& saveDir, const QVariant& userData) = 0;
    virtual void cancelDownload(const QVariant& userData) = 0;
    virtual void cancelAllDownloads() = 0;

signals:
    void downloadProgress(qint64 bytesReceived, qint64 bytesTotal, const QVariant& userData);
    void shardInfoChanged(const DownloadInfo& info, const QVariant& userData);
    void downloadFinished(bool success, const QString& errorString, const QVariant& userData);
    void allDownloadFinished();
};

class CoordinatorConcatStage : public QObject
{
    Q_OBJECT

public:
    using QObject::QObject;
    ~CoordinatorConcatStage() override = default;

    virtual void setFilePath(const QString& path) = 0;
    virtual void startConcat() = 0;
    virtual void cancelConcat() = 0;

signals:
    void concatFinished(bool ok, const QString& message);
};

class CoordinatorDecryptStage : public QObject
{
    Q_OBJECT

public:
    using QObject::QObject;
    ~CoordinatorDecryptStage() override = default;

    virtual void setParams(const QString& name, const QString& savePath) = 0;
    virtual void setTranscodeToMp4(bool transcodeToMp4) = 0;
    virtual void startDecrypt() = 0;
    virtual void cancelDecrypt() = 0;

signals:
    void decryptFinished(bool ok, const QString& message);
};

class CoordinatorDirectFinalizeStage : public QObject
{
    Q_OBJECT

public:
    using QObject::QObject;
    ~CoordinatorDirectFinalizeStage() override = default;

    virtual void startFinalize(const QString& title, const QString& savePath, bool transcodeToMp4) = 0;
    virtual void cancelFinalize() = 0;

signals:
    void finished(bool ok, const QString& code, const QString& message, const QString& finalPath);
};
