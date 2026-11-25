#pragma once

#include "plugin/drscanner.h"
#include "plugin/drsettings.h"

#include <core/engine/audioloader.h>
#include <core/library/musiclibrary.h>
#include <core/track.h>

#include <QFutureWatcher>
#include <QMetaType>
#include <QObject>
#include <QString>

#include <memory>
#include <vector>

namespace Fooyin::DrMeter {
namespace TagNames {
inline constexpr auto DynamicRange      = "DYNAMIC RANGE";
inline constexpr auto AlbumDynamicRange = "ALBUM DYNAMIC RANGE";
} // namespace TagNames

struct DrTagPrepareResult
{
    TrackList tracksToWrite;
    int skippedCount{0};
};

struct DrTagWriteSummary
{
    int updated{0};
    int failed{0};
    int skipped{0};
    bool cancelled{false};

    [[nodiscard]] QString formatSummary() const;
};

[[nodiscard]] bool isTrackTagWritable(const Track& track, const AudioLoader& audioLoader);

[[nodiscard]] DrTagPrepareResult prepareTagUpdates(const std::vector<ScannedAlbum>& albums,
                                                   const AudioLoader& audioLoader,
                                                   const DrSettings& settings = DrSettings{});

class DrTagWriter : public QObject
{
    Q_OBJECT

public:
    DrTagWriter(MusicLibrary* library, std::shared_ptr<AudioLoader> audioLoader, QObject* parent = nullptr);
    ~DrTagWriter() override;

    void writeTags(const std::vector<ScannedAlbum>& albums, const DrSettings& settings = DrSettings{});
    void cancel();
    [[nodiscard]] bool isRunning() const noexcept;

Q_SIGNALS:
    void finished(const Fooyin::DrMeter::DrTagWriteSummary& summary);

private:
    MusicLibrary* m_library{nullptr};
    std::shared_ptr<AudioLoader> m_audioLoader;
    WriteRequest m_activeRequest;
    std::unique_ptr<QFutureWatcher<WriteResult>> m_watcher;
    int m_pendingSkipped{0};
    bool m_running{false};
};
} // namespace Fooyin::DrMeter

Q_DECLARE_METATYPE(Fooyin::DrMeter::DrTagWriteSummary)
