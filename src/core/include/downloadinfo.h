#pragma once

#include <QString>

// Download domain state shared by the core coordinator and GUI presentation.
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

    DownloadInfo() = default;

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
