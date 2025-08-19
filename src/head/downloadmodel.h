#pragma once
#include <QStandardItemModel>

enum class DownloadStatus {
    Error = -1,
    Finished = 0,
    Waiting = 1,
    Downloading = 2
};

struct DownloadInfo {
    int index;
    DownloadStatus status;
    QString url;
    int progress;

    // 默认构造函数
    DownloadInfo() = default;

    // 带参构造函数
    DownloadInfo(int idx, DownloadStatus s, const QString& u, int p)
        : index(idx), status(s), url(u), progress(p) {
    }

    QString statusText() const {
        switch (status) {
        case DownloadStatus::Downloading: return QStringLiteral("下载中");
        case DownloadStatus::Finished: return QStringLiteral("完成");
        case DownloadStatus::Waiting: return QStringLiteral("等待");
        case DownloadStatus::Error: return QStringLiteral("错误");
        default: return QStringLiteral("未知");
        }
    }

    QString progressText() const {
        return QStringLiteral("%1%").arg(progress);
    }
};

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