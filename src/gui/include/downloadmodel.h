#pragma once
#include <QStandardItemModel>

#include "downloadinfo.h"

class DownloadModel : public QStandardItemModel {
    Q_OBJECT
public:
    explicit DownloadModel(QObject* parent = nullptr);
    void updateInfo(const DownloadInfo& info);
    int totalProgress() const;
signals:
    //void progressFinished() const;

private:
    QHash<int, int> m_progressDict;
};