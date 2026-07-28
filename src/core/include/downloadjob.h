#pragma once

#include <QString>

// ── Job lifecycle state ───────────────────────────────────────────
//
// Legal forward progression:
//   Created → Queued → ResolvingM3u8 → Downloading → Concatenating
//     → Decrypting ─→ Completed
//     → DirectFinalizing ─→ Completed
// From any non-terminal state: → Failed | Cancelled

enum class DownloadJobState {
    Created,
    Queued,
    ResolvingM3u8,
    Downloading,
    Concatenating,
    Decrypting,
    DirectFinalizing,
    Completed,
    Failed,
    Cancelled
};

// ── Granular stage within a state ────────────────────────────────

enum class DownloadJobStage {
    None,
    FetchingPlaylist,
    ParsingManifest,
    DownloadingShards,
    MergingShards,
    RunningDecrypt,
    ValidatingOutput,
    PublishingOutput,
    CleaningUp
};

// ── Error classification ─────────────────────────────────────────

enum class DownloadErrorCategory {
    NetworkError,
    Timeout,
    ServerError,
    DecryptError,
    FileSystemError,
    ValidationError,
    Cancelled,
    Unknown
};

// ── Batch failure policy ─────────────────────────────────────────
//   SkipVideo  – video-specific failure; continue the batch.
//   StopBatch  – shared-environment failure; abort the entire batch.

enum class BatchFailurePolicy { SkipVideo, StopBatch };

// ── Cancellation scope ───────────────────────────────────────────

enum class CancellationScope { SingleVideo, AllVideos };

// ── Request / Job data types ─────────────────────────────────────

struct DownloadRequest {
    QString url;
    QString videoTitle;
    QString quality;
    QString savePath;
    int threadCount = 2;
    bool transcodeToMp4 = false;
};

struct DownloadJob {
    QString id;
    DownloadRequest request;
    DownloadJobState state = DownloadJobState::Created;
    DownloadJobStage stage = DownloadJobStage::None;
    int progressPercent = 0;
    QString errorMessage;
    DownloadErrorCategory errorCategory = DownloadErrorCategory::Unknown;
};

// ── State-transition validation ───────────────────────────────────

inline bool isValidTransition(DownloadJobState from, DownloadJobState to)
{
    switch (from) {
    case DownloadJobState::Created:
        return to == DownloadJobState::Queued
            || to == DownloadJobState::Failed
            || to == DownloadJobState::Cancelled;
    case DownloadJobState::Queued:
        return to == DownloadJobState::ResolvingM3u8
            || to == DownloadJobState::Failed
            || to == DownloadJobState::Cancelled;
    case DownloadJobState::ResolvingM3u8:
        return to == DownloadJobState::Downloading
            || to == DownloadJobState::Failed
            || to == DownloadJobState::Cancelled;
    case DownloadJobState::Downloading:
        return to == DownloadJobState::Concatenating
            || to == DownloadJobState::Failed
            || to == DownloadJobState::Cancelled;
    case DownloadJobState::Concatenating:
        return to == DownloadJobState::Decrypting
            || to == DownloadJobState::DirectFinalizing
            || to == DownloadJobState::Failed
            || to == DownloadJobState::Cancelled;
    case DownloadJobState::Decrypting:
    case DownloadJobState::DirectFinalizing:
        return to == DownloadJobState::Completed
            || to == DownloadJobState::Failed
            || to == DownloadJobState::Cancelled;
    case DownloadJobState::Completed:
    case DownloadJobState::Failed:
    case DownloadJobState::Cancelled:
        return false;
    }
    return false;
}

// ── Error classification → batch policy ──────────────────────────

inline BatchFailurePolicy classifyFailurePolicy(DownloadErrorCategory category)
{
    switch (category) {
    case DownloadErrorCategory::NetworkError:
    case DownloadErrorCategory::Timeout:
    case DownloadErrorCategory::ServerError:
    case DownloadErrorCategory::DecryptError:
    case DownloadErrorCategory::ValidationError:
    case DownloadErrorCategory::Cancelled:
        return BatchFailurePolicy::SkipVideo;
    case DownloadErrorCategory::FileSystemError:
    case DownloadErrorCategory::Unknown:
        return BatchFailurePolicy::StopBatch;
    }
    return BatchFailurePolicy::StopBatch;
}
