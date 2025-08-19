#include "../head/downloadmodel.h"

DownloadModel::DownloadModel(QObject* parent)
    : QStandardItemModel(parent) {
    setColumnCount(4);
    setHorizontalHeaderLabels({ "序号", "状态", "URL", "进度" });
}

void DownloadModel::updateInfo(const DownloadInfo& info) {
    //qDebug() << "Updating row:" << info.index << "with url:" << info.url;
    if (rowCount() <= info.index - 1) {
        insertRows(rowCount(), info.index - rowCount());
    }

    const int row = info.index - 1;

    QStandardItem* indexItem = item(row, 0) ? item(row, 0) : new QStandardItem();
    indexItem->setText(QString::number(info.index));
    setItem(row, 0, indexItem);

    QStandardItem* statusItem = item(row, 1) ? item(row, 1) : new QStandardItem();
    statusItem->setText(info.statusText());
    statusItem->setForeground([&]() {
        switch (info.status) {
        case DownloadStatus::Downloading: return QBrush(Qt::blue);
        case DownloadStatus::Finished: return QBrush(Qt::darkGreen);
        default: return QBrush(Qt::white);
        }
        }());
    setItem(row, 1, statusItem);

    QStandardItem* urlItem = item(row, 2) ? item(row, 2) : new QStandardItem();
    urlItem->setText(info.url);
    setItem(row, 2, urlItem);

    QStandardItem* progressItem = item(row, 3) ? item(row, 3) : new QStandardItem();
    progressItem->setText(info.progressText());
    setItem(row, 3, progressItem);

    m_progressDict[info.index] = info.progress;
    emit dataChanged(index(row, 0), index(row, 3));
}

int DownloadModel::totalProgress() const {
    if (m_progressDict.isEmpty()) return 0;
    auto progress = static_cast<int>(std::accumulate(m_progressDict.begin(), m_progressDict.end(), 0) / m_progressDict.size());
    return progress;
}