#pragma once

#include "core/drtypes.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace Fooyin::DrMeter {
class DrAnalyzer
{
public:
    DrAnalyzer(uint32_t sampleRate, uint32_t channels);
    DrAnalyzer(uint32_t sampleRate, std::vector<ChannelInfo> channelLayout);

    [[nodiscard]] uint32_t sampleRate() const noexcept;
    [[nodiscard]] uint32_t channels() const noexcept;
    [[nodiscard]] size_t blockFrames() const noexcept;
    [[nodiscard]] uint64_t totalFrames() const noexcept;

    void process(std::span<const double> interleaved, size_t frameCount);
    [[nodiscard]] TrackDrResult finish() const;

    [[nodiscard]] static std::vector<ChannelInfo> defaultChannelLayout(uint32_t channels);

private:
    void initChannelBuffers();
    void finalizeFullBlock();

    uint32_t m_sampleRate{0};
    uint32_t m_channels{0};
    size_t m_blockFrames{0};
    std::vector<ChannelInfo> m_channelLayout;

    uint64_t m_totalFrames{0};
    size_t m_currentBlockFrames{0};

    std::vector<double> m_totalSumSq;
    std::vector<double> m_totalPeak;

    std::vector<double> m_blockSumSq;
    std::vector<double> m_blockPeak;

    std::vector<std::vector<double>> m_blockRmsValues;
    std::vector<std::vector<double>> m_blockPeakValues;
};
} // namespace Fooyin::DrMeter
