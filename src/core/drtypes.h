#pragma once

#include <cmath>
#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <vector>

namespace Fooyin::DrMeter {
struct ChannelInfo
{
    std::string label;
    bool isLfe{false};
};

struct ChannelDrResult
{
    std::string channelLabel;
    bool isLfe{false};
    double drDb{0.0};
    double rmsDb{-std::numeric_limits<double>::infinity()};
    double peakDb{-std::numeric_limits<double>::infinity()};
};

struct TrackMetadata
{
    int trackNumber{0};
    std::string title;
    std::string codec;
    uint32_t sampleRate{0};
    uint32_t channels{0};
    int bitsPerSample{0};
    int bitrateKbps{0};
};

struct TrackDrResult
{
    std::vector<ChannelDrResult> channels;
    double trackDrFloat{0.0};
    int trackDrInt{0};
    double peakDb{-std::numeric_limits<double>::infinity()};
    double rmsDb{-std::numeric_limits<double>::infinity()};
    uint64_t totalFrames{0};
    double durationSec{0.0};
    TrackMetadata metadata;
};

struct AlbumDrResult
{
    std::string albumArtist;
    std::string album;
    std::vector<TrackDrResult> tracks;
    int officialDr{0};

    [[nodiscard]] static int computeOfficialDr(std::span<const TrackDrResult> tracks) noexcept
    {
        if(tracks.empty()) {
            return 0;
        }

        double sum = 0.0;
        for(const auto& track : tracks) {
            sum += track.trackDrFloat;
        }
        return static_cast<int>(std::lround(sum / static_cast<double>(tracks.size())));
    }
};

[[nodiscard]] inline double clampNegativeZero(double value) noexcept
{
    if(value > -0.005 && value <= 0.0) {
        return 0.0;
    }
    return value;
}

[[nodiscard]] inline double linearToDb(double linear) noexcept
{
    if(!(linear > 0.0)) {
        return -std::numeric_limits<double>::infinity();
    }
    return 20.0 * std::log10(linear);
}

[[nodiscard]] inline int64_t roundDurationSeconds(uint64_t totalFrames, uint32_t sampleRate) noexcept
{
    if(sampleRate == 0U || totalFrames == 0U) {
        return 0;
    }
    return static_cast<int64_t>(std::llround(static_cast<double>(totalFrames) / static_cast<double>(sampleRate)));
}
} // namespace Fooyin::DrMeter
