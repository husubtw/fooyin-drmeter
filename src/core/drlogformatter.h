#pragma once

#include "core/drtypes.h"

#include <span>
#include <string>

namespace Fooyin::DrMeter {
struct LogFormatOptions
{
    std::string fooyinVersion{"0.15.1"};
    std::string pluginVersion{"0.1.2"};
    std::string logDate;
};

class DrLogFormatter
{
public:
    [[nodiscard]] static std::string formatDuration(uint64_t totalFrames, uint32_t sampleRate);
    [[nodiscard]] static std::string formatDbValue(double db, bool withUnit = true);
    [[nodiscard]] static std::string formatTrackLabel(const TrackMetadata& metadata);

    [[nodiscard]] static std::string formatAlbumLog(const AlbumDrResult& album, const LogFormatOptions& options = {});
    [[nodiscard]] static std::string formatMultiAlbumLog(std::span<const AlbumDrResult> albums,
                                                         const LogFormatOptions& options = {});

    [[nodiscard]] static std::string formatResultsTable(std::span<const AlbumDrResult> albums);
};
} // namespace Fooyin::DrMeter
