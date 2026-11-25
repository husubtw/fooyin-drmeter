#pragma once

#include "core/drtypes.h"

#include <core/engine/audioformat.h>
#include <core/engine/audioloader.h>
#include <core/track.h>
#include <utils/worker.h>

#include <QFutureWatcher>
#include <QMetaType>
#include <QObject>
#include <QString>
#include <QThread>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <stop_token>
#include <vector>

namespace Fooyin::DrMeter {
struct ScannedTrack
{
    Track track;
    TrackDrResult dr;
    bool ok{false};
    QString error;
};

struct ScannedAlbum
{
    QString groupKey;
    QString directory;
    AlbumDrResult album;
    std::vector<ScannedTrack> tracks;
};

struct DrScanSummary
{
    std::vector<ScannedAlbum> albums;
    int64_t elapsedMs{0};
    double totalAudioSec{0.0};
    double speedRatio{0.0};
    bool cancelled{false};
};

[[nodiscard]] std::vector<ChannelInfo> channelLayoutFromAudioFormat(const AudioFormat& format);
[[nodiscard]] int parseTrackNumber(const QString& raw);

class DrWorker : public Worker
{
    Q_OBJECT

public:
    explicit DrWorker(std::shared_ptr<AudioLoader> audioLoader, QObject* parent = nullptr);

    void closeThread() override;

    void scan(const TrackList& tracks, const QString& groupingPattern, const std::stop_token& token);

Q_SIGNALS:
    void progressChanged(int completed, int total, const QString& currentTrack);
    void scanFinished(const Fooyin::DrMeter::DrScanSummary& summary);
    void scanCancelled();
    void closed();

private:
    struct WorkItem
    {
        size_t albumIndex{0};
        size_t trackIndex{0};
        Track track;
        ScannedTrack result;
    };

    void scanSingleItem(WorkItem& item, const std::stop_token& token);
    void finishScan(std::stop_token token);

    std::shared_ptr<AudioLoader> m_audioLoader;
    std::vector<ScannedAlbum> m_albums;
    std::vector<WorkItem> m_items;
    QFutureWatcher<void>* m_watcher{nullptr};
    std::chrono::steady_clock::time_point m_startTime;
    std::atomic<int> m_completedTracks{0};
    int m_totalTracks{0};
    bool m_completed{false};
};

class DrScanner : public QObject
{
    Q_OBJECT

public:
    explicit DrScanner(std::shared_ptr<AudioLoader> audioLoader, QObject* parent = nullptr);
    ~DrScanner() override;

    void startScan(const TrackList& tracks, const QString& groupingPattern);
    void cancel();
    void waitForFinished();

Q_SIGNALS:
    void progressChanged(int completed, int total, const QString& currentTrack);
    void scanFinished(const Fooyin::DrMeter::DrScanSummary& summary);
    void scanCancelled();

private:
    QThread m_scanThread;
    std::unique_ptr<DrWorker> m_worker;
};
} // namespace Fooyin::DrMeter

Q_DECLARE_METATYPE(Fooyin::DrMeter::DrScanSummary)
