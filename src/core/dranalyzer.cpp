#include "core/dranalyzer.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <functional>
#include <limits>
#include <utility>

namespace Fooyin::DrMeter {
namespace {
constexpr size_t computeBlockFrames(uint32_t sampleRate) noexcept
{
    if(sampleRate == 0U) {
        return 0U;
    }
    constexpr double WindowSeconds = 132480.0 / 44100.0; // 3.0040816326530613 (foo_dr_meter v1.0.8)
    const auto frames              = static_cast<size_t>(std::floor(static_cast<double>(sampleRate) * WindowSeconds));
    return std::max<size_t>(1U, frames);
}
} // namespace

DrAnalyzer::DrAnalyzer(uint32_t sampleRate, uint32_t channels)
    : m_sampleRate{sampleRate}
    , m_channels{channels}
    , m_blockFrames{computeBlockFrames(sampleRate)}
    , m_channelLayout{defaultChannelLayout(channels)}
{
    initChannelBuffers();
}

DrAnalyzer::DrAnalyzer(uint32_t sampleRate, std::vector<ChannelInfo> channelLayout)
    : m_sampleRate{sampleRate}
    , m_channels{static_cast<uint32_t>(channelLayout.size())}
    , m_blockFrames{computeBlockFrames(sampleRate)}
    , m_channelLayout{std::move(channelLayout)}
{
    initChannelBuffers();
}

uint32_t DrAnalyzer::sampleRate() const noexcept
{
    return m_sampleRate;
}

uint32_t DrAnalyzer::channels() const noexcept
{
    return m_channels;
}

size_t DrAnalyzer::blockFrames() const noexcept
{
    return m_blockFrames;
}

uint64_t DrAnalyzer::totalFrames() const noexcept
{
    return m_totalFrames;
}

std::vector<ChannelInfo> DrAnalyzer::defaultChannelLayout(uint32_t channels)
{
    std::vector<ChannelInfo> layout;
    layout.reserve(channels);

    if(channels == 1U) {
        layout.push_back(ChannelInfo{"C", false});
    }
    else if(channels == 2U) {
        layout.push_back(ChannelInfo{"FL", false});
        layout.push_back(ChannelInfo{"FR", false});
    }
    else {
        for(uint32_t ch = 0; ch < channels; ++ch) {
            layout.push_back(ChannelInfo{std::format("Ch{}", ch + 1U), false});
        }
    }

    return layout;
}

void DrAnalyzer::initChannelBuffers()
{
    const auto chCount = static_cast<size_t>(m_channels);
    m_totalSumSq.assign(chCount, 0.0);
    m_totalPeak.assign(chCount, 0.0);
    m_blockSumSq.assign(chCount, 0.0);
    m_blockPeak.assign(chCount, 0.0);
    m_blockRmsValues.assign(chCount, {});
    m_blockPeakValues.assign(chCount, {});
}

void DrAnalyzer::finalizeFullBlock()
{
    const auto invBlockFrames = 2.0 / static_cast<double>(m_blockFrames);
    const auto chCount        = static_cast<size_t>(m_channels);

    for(size_t ch = 0; ch < chCount; ++ch) {
        m_blockRmsValues[ch].push_back(std::sqrt(m_blockSumSq[ch] * invBlockFrames));
        m_blockPeakValues[ch].push_back(m_blockPeak[ch]);
        m_blockSumSq[ch] = 0.0;
        m_blockPeak[ch]  = 0.0;
    }

    m_currentBlockFrames = 0;
}

void DrAnalyzer::process(std::span<const double> interleaved, size_t frameCount)
{
    if(frameCount == 0 || m_channels == 0 || m_blockFrames == 0) {
        return;
    }

    const auto chCount = static_cast<size_t>(m_channels);
    if(interleaved.size() < frameCount * chCount) {
        return;
    }

    size_t frameIdx = 0;
    while(frameIdx < frameCount) {
        const size_t remainingInBlock = m_blockFrames - m_currentBlockFrames;
        const size_t chunkFrames      = std::min(frameCount - frameIdx, remainingInBlock);

        for(size_t f = 0; f < chunkFrames; ++f) {
            const size_t base = (frameIdx + f) * chCount;
            for(size_t ch = 0; ch < chCount; ++ch) {
                const double sample = interleaved[base + ch];
                const double sqVal  = sample * sample;
                if(!std::isfinite(sqVal)) {
                    continue;
                }
                const double absVal = std::abs(sample);

                m_totalSumSq[ch] += sqVal;
                if(absVal > m_totalPeak[ch]) {
                    m_totalPeak[ch] = absVal;
                }

                m_blockSumSq[ch] += sqVal;
                if(absVal > m_blockPeak[ch]) {
                    m_blockPeak[ch] = absVal;
                }
            }
        }

        frameIdx += chunkFrames;
        m_totalFrames += chunkFrames;
        m_currentBlockFrames += chunkFrames;

        if(m_currentBlockFrames == m_blockFrames) {
            finalizeFullBlock();
        }
    }
}

TrackDrResult DrAnalyzer::finish() const
{
    TrackDrResult result;
    result.totalFrames = m_totalFrames;
    result.durationSec
        = m_sampleRate > 0U ? static_cast<double>(m_totalFrames) / static_cast<double>(m_sampleRate) : 0.0;
    result.metadata.sampleRate = m_sampleRate;
    result.metadata.channels   = m_channels;

    const auto chCount = static_cast<size_t>(m_channels);
    result.channels.resize(chCount);

    for(size_t ch = 0; ch < chCount; ++ch) {
        result.channels[ch].channelLabel = m_channelLayout[ch].label;
        result.channels[ch].isLfe        = m_channelLayout[ch].isLfe;
    }

    if(chCount == 0 || m_totalFrames == 0 || m_blockFrames == 0) {
        return result;
    }

    const size_t fullBlocks      = m_blockRmsValues[0].size();
    const bool hasPartialBlock   = m_currentBlockFrames > 0;
    const size_t totalBlocks     = fullBlocks + (hasPartialBlock ? 1U : 0U);
    const double totalRmsDivisor = (fullBlocks > 0)
                                     ? (static_cast<double>(totalBlocks) * static_cast<double>(m_blockFrames))
                                     : static_cast<double>(m_totalFrames);
    const double partialDivisor
        = (fullBlocks > 0) ? static_cast<double>(m_blockFrames) : static_cast<double>(m_currentBlockFrames);
    const size_t topK = std::max<size_t>(1U, static_cast<size_t>(std::floor(0.20 * static_cast<double>(totalBlocks))));

    double maxChannelPeakLinear = 0.0;
    double sumChannelRmsSq      = 0.0;
    double activeDrSum          = 0.0;
    size_t activeDrCount        = 0;

    bool hasNonLfe = false;
    for(const auto& info : m_channelLayout) {
        if(!info.isLfe) {
            hasNonLfe = true;
            break;
        }
    }

    std::vector<double> sortedRms;
    std::vector<double> sortedPeaks;
    sortedRms.reserve(totalBlocks);
    sortedPeaks.reserve(totalBlocks);

    for(size_t ch = 0; ch < chCount; ++ch) {
        auto& chResult = result.channels[ch];

        const double chPeakLinear = m_totalPeak[ch];
        if(chPeakLinear > maxChannelPeakLinear) {
            maxChannelPeakLinear = chPeakLinear;
        }
        chResult.peakDb = linearToDb(chPeakLinear);

        const double chRmsSq     = (2.0 * m_totalSumSq[ch]) / totalRmsDivisor;
        const double chRmsLinear = std::sqrt(chRmsSq);
        sumChannelRmsSq += chRmsSq;
        chResult.rmsDb = linearToDb(chRmsLinear);

        sortedRms.assign(m_blockRmsValues[ch].begin(), m_blockRmsValues[ch].end());
        sortedPeaks.assign(m_blockPeakValues[ch].begin(), m_blockPeakValues[ch].end());

        if(hasPartialBlock) {
            sortedRms.push_back(std::sqrt((2.0 * m_blockSumSq[ch]) / partialDivisor));
            sortedPeaks.push_back(m_blockPeak[ch]);
        }

        std::ranges::sort(sortedRms, std::greater<>{});
        std::ranges::sort(sortedPeaks, std::greater<>{});

        double topRmsSqSum = 0.0;
        for(size_t i = 0; i < topK; ++i) {
            topRmsSqSum += sortedRms[i] * sortedRms[i];
        }
        const double top20Rms = std::sqrt(topRmsSqSum / static_cast<double>(topK));

        const double refPeak = (totalBlocks >= 2U) ? sortedPeaks[1] : sortedPeaks[0];
        double chDr          = 0.0;

        if(top20Rms > 0.0 && refPeak > 0.0) {
            chDr = 20.0 * std::log10(refPeak / top20Rms);
            if(chDr < 0.0 && sortedPeaks[0] > 0.0) {
                chDr = 20.0 * std::log10(sortedPeaks[0] / top20Rms);
            }
            if(chDr < 0.0 || !std::isfinite(chDr)) {
                chDr = 0.0;
            }
        }

        chResult.drDb = chDr;

        if(!hasNonLfe || !chResult.isLfe) {
            activeDrSum += chDr;
            ++activeDrCount;
        }
    }

    result.peakDb = linearToDb(maxChannelPeakLinear);

    const double meanRmsSq = sumChannelRmsSq / static_cast<double>(chCount);
    result.rmsDb = (meanRmsSq > 0.0) ? (10.0 * std::log10(meanRmsSq)) : -std::numeric_limits<double>::infinity();

    if(activeDrCount > 0U) {
        result.trackDrFloat = activeDrSum / static_cast<double>(activeDrCount);
        result.trackDrInt   = static_cast<int>(std::lround(result.trackDrFloat));
    }

    return result;
}
} // namespace Fooyin::DrMeter
