#include "core/dranalyzer.h"

#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <numbers>
#include <vector>

namespace Fooyin::DrMeter {
namespace {
std::vector<double> generateStereoSine(uint32_t sampleRate, size_t totalFrames, double freqHz, double amplitude)
{
    std::vector<double> samples(totalFrames * 2U);
    const double twoPiFreq = 2.0 * std::numbers::pi * freqHz / static_cast<double>(sampleRate);
    for(size_t i = 0; i < totalFrames; ++i) {
        const double val     = amplitude * std::sin(twoPiFreq * static_cast<double>(i));
        samples[i * 2U]      = val;
        samples[i * 2U + 1U] = val;
    }
    return samples;
}

std::vector<double> generateStereoSquare(uint32_t sampleRate, size_t totalFrames, double freqHz, double amplitude)
{
    std::vector<double> samples(totalFrames * 2U);
    const double period = static_cast<double>(sampleRate) / freqHz;
    for(size_t i = 0; i < totalFrames; ++i) {
        const double phase   = std::fmod(static_cast<double>(i), period);
        const double val     = (phase < period * 0.5) ? amplitude : -amplitude;
        samples[i * 2U]      = val;
        samples[i * 2U + 1U] = val;
    }
    return samples;
}

TEST(DrAnalyzerTest, FullScaleSineWave48kAndStreamingInvariance)
{
    constexpr uint32_t sampleRate = 48000;
    constexpr size_t totalFrames  = sampleRate * 12U;
    const auto pcm                = generateStereoSine(sampleRate, totalFrames, 1000.0, 1.0);

    DrAnalyzer singleShot{sampleRate, 2};
    singleShot.process(pcm, totalFrames);
    const TrackDrResult res = singleShot.finish();

    EXPECT_EQ(res.totalFrames, totalFrames);
    EXPECT_NEAR(res.durationSec, 12.0, 1e-9);
    EXPECT_EQ(res.trackDrInt, 0);
    EXPECT_NEAR(res.trackDrFloat, 0.0, 0.01);
    EXPECT_NEAR(res.peakDb, 0.0, 0.01);
    EXPECT_NEAR(res.rmsDb, 0.0, 0.01); // AES17 RMS of full-scale sine is 0.00 dBFS
    ASSERT_EQ(res.channels.size(), 2U);
    EXPECT_EQ(res.channels[0].channelLabel, "FL");
    EXPECT_EQ(res.channels[1].channelLabel, "FR");

    for(const size_t chunkSize : {size_t{1}, size_t{513}, size_t{4096}}) {
        DrAnalyzer streamed{sampleRate, 2};
        size_t offset = 0;
        while(offset < totalFrames) {
            const size_t n = std::min(chunkSize, totalFrames - offset);
            streamed.process(std::span<const double>{pcm}.subspan(offset * 2U, n * 2U), n);
            offset += n;
        }
        const TrackDrResult sRes = streamed.finish();
        EXPECT_EQ(sRes.trackDrInt, res.trackDrInt);
        EXPECT_NEAR(sRes.trackDrFloat, res.trackDrFloat, 1e-12);
        EXPECT_NEAR(sRes.peakDb, res.peakDb, 1e-12);
        EXPECT_NEAR(sRes.rmsDb, res.rmsDb, 1e-12);
    }
}

TEST(DrAnalyzerTest, FullScaleSineWave44100HzExactFourBlocks)
{
    constexpr uint32_t sampleRate = 44100;
    constexpr size_t totalFrames  = 132480U * 4U; // Exact 4 blocks at 44.1 kHz
    const auto pcm                = generateStereoSine(sampleRate, totalFrames, 1000.0, 1.0);

    DrAnalyzer analyzer{sampleRate, 2};
    analyzer.process(pcm, totalFrames);
    const TrackDrResult res = analyzer.finish();

    EXPECT_EQ(res.trackDrInt, 0);
    EXPECT_NEAR(res.peakDb, 0.0, 0.01);
    EXPECT_NEAR(res.rmsDb, 0.0, 0.01);
}

TEST(DrAnalyzerTest, FullScaleSquareWave)
{
    constexpr uint32_t sampleRate = 48000;
    constexpr size_t totalFrames  = sampleRate * 12U;
    const auto pcm                = generateStereoSquare(sampleRate, totalFrames, 1000.0, 1.0);

    DrAnalyzer analyzer{sampleRate, 2};
    analyzer.process(pcm, totalFrames);
    const TrackDrResult res = analyzer.finish();

    EXPECT_EQ(res.trackDrInt, 0);
    EXPECT_NEAR(res.trackDrFloat, 0.0, 1e-9);
    EXPECT_NEAR(res.peakDb, 0.0, 0.01);
    EXPECT_NEAR(res.rmsDb, 20.0 * std::log10(std::numbers::sqrt2), 0.01); // +3.01 dBFS under AES17
}

TEST(DrAnalyzerTest, KnownCrestFactorPulseSignalAndSecondHighestPeak)
{
    constexpr uint32_t sampleRate = 48000;
    DrAnalyzer analyzer{sampleRate, 2};
    const size_t blockFrames     = analyzer.blockFrames();
    constexpr size_t numBlocks   = 10U;
    constexpr size_t pulseFrames = 7210U;
    const size_t totalFrames     = blockFrames * numBlocks;
    const double expectedDr
        = 10.0 * std::log10(static_cast<double>(blockFrames) / (2.0 * static_cast<double>(pulseFrames)));
    std::vector<double> pcm(totalFrames * 2U, 0.0);

    for(size_t b = 0; b < numBlocks; ++b) {
        // Block 0 has an isolated loud transient (peak = 0.9); blocks 1..9 have peak = 0.5.
        // Second-highest block peak must be 0.5, ignoring the isolated transient in block 0.
        // Loudest 20% = 2 blocks; make blocks 1..3 have amplitude 0.5 with pulseFrames,
        // and blocks 4..9 quieter (0.1) so top 20% RMS comes strictly from blocks 1..3.
        const double amp   = (b == 0) ? 0.9 : (b <= 3 ? 0.5 : 0.1);
        const size_t count = (b == 0) ? 100U : pulseFrames; // Block 0 has low RMS despite high peak
        const size_t start = b * blockFrames;
        for(size_t f = 0; f < count; ++f) {
            pcm[(start + f) * 2U]      = amp;
            pcm[(start + f) * 2U + 1U] = amp;
        }
    }

    analyzer.process(pcm, totalFrames);
    const TrackDrResult res = analyzer.finish();

    EXPECT_NEAR(res.peakDb, 20.0 * std::log10(0.9), 0.01);
    EXPECT_NEAR(res.trackDrFloat, expectedDr, 0.01);
    EXPECT_EQ(res.trackDrInt, 10);
    ASSERT_EQ(res.channels.size(), 2U);
    EXPECT_NEAR(res.channels[0].drDb, expectedDr, 0.01);
    EXPECT_NEAR(res.channels[1].drDb, expectedDr, 0.01);
}

TEST(DrAnalyzerTest, ShortClipUnderThreeSeconds)
{
    constexpr uint32_t sampleRate = 48000;
    constexpr size_t totalFrames  = 72000U; // 1.5 s
    std::vector<double> pcm(totalFrames * 2U, 0.0);

    // Pulse of 3600 samples out of 72000 -> N / (2 * M) = 10 -> exact 10.00 dB DR
    for(size_t i = 0; i < 3600U; ++i) {
        pcm[i * 2U]      = 0.5;
        pcm[i * 2U + 1U] = 0.5;
    }

    DrAnalyzer analyzer{sampleRate, 2};
    analyzer.process(pcm, totalFrames);
    const TrackDrResult res = analyzer.finish();

    EXPECT_FALSE(std::isnan(res.trackDrFloat));
    EXPECT_FALSE(std::isnan(res.peakDb));
    EXPECT_FALSE(std::isnan(res.rmsDb));
    EXPECT_NEAR(res.trackDrFloat, 10.0, 0.01);
    EXPECT_EQ(res.trackDrInt, 10);
    EXPECT_NEAR(res.peakDb, 20.0 * std::log10(0.5), 0.01);
}

TEST(DrAnalyzerTest, EmptyAndFullSilenceProduceNoNaN)
{
    DrAnalyzer emptyAnalyzer{48000, 2};
    const TrackDrResult emptyRes = emptyAnalyzer.finish();
    EXPECT_EQ(emptyRes.trackDrInt, 0);
    EXPECT_DOUBLE_EQ(emptyRes.trackDrFloat, 0.0);
    EXPECT_TRUE(std::isinf(emptyRes.peakDb) && emptyRes.peakDb < 0.0);
    EXPECT_TRUE(std::isinf(emptyRes.rmsDb) && emptyRes.rmsDb < 0.0);

    constexpr size_t silentFrames = 48000U * 10U;
    std::vector<double> zeros(silentFrames * 2U, 0.0);
    DrAnalyzer silentAnalyzer{48000, 2};
    silentAnalyzer.process(zeros, silentFrames);
    const TrackDrResult silentRes = silentAnalyzer.finish();

    EXPECT_EQ(silentRes.trackDrInt, 0);
    EXPECT_DOUBLE_EQ(silentRes.trackDrFloat, 0.0);
    EXPECT_TRUE(std::isinf(silentRes.peakDb) && silentRes.peakDb < 0.0);
    EXPECT_TRUE(std::isinf(silentRes.rmsDb) && silentRes.rmsDb < 0.0);
    for(const auto& ch : silentRes.channels) {
        EXPECT_DOUBLE_EQ(ch.drDb, 0.0);
        EXPECT_TRUE(std::isinf(ch.peakDb) && ch.peakDb < 0.0);
        EXPECT_TRUE(std::isinf(ch.rmsDb) && ch.rmsDb < 0.0);
    }
}

TEST(DrAnalyzerTest, SingleSilentChannelHandledSafely)
{
    constexpr uint32_t sampleRate = 48000;
    DrAnalyzer analyzer{sampleRate, 2};
    const size_t blockFrames     = analyzer.blockFrames();
    const size_t totalFrames     = blockFrames * 4U;
    constexpr size_t pulseFrames = 7210U;
    const double expectedChDr
        = 10.0 * std::log10(static_cast<double>(blockFrames) / (2.0 * static_cast<double>(pulseFrames)));
    std::vector<double> pcm(totalFrames * 2U, 0.0);

    for(size_t b = 0; b < 4U; ++b) {
        for(size_t f = 0; f < pulseFrames; ++f) {
            pcm[(b * blockFrames + f) * 2U] = 1.0; // FL active (DR ~10.00 dB), FR stays 0.0
        }
    }

    analyzer.process(pcm, totalFrames);
    const TrackDrResult res = analyzer.finish();

    ASSERT_EQ(res.channels.size(), 2U);
    EXPECT_NEAR(res.channels[0].drDb, expectedChDr, 0.01);
    EXPECT_NEAR(res.channels[0].peakDb, 0.0, 0.01);
    EXPECT_DOUBLE_EQ(res.channels[1].drDb, 0.0);
    EXPECT_TRUE(std::isinf(res.channels[1].peakDb) && res.channels[1].peakDb < 0.0);
    EXPECT_TRUE(std::isinf(res.channels[1].rmsDb) && res.channels[1].rmsDb < 0.0);
    EXPECT_FALSE(std::isnan(res.trackDrFloat));
    EXPECT_NEAR(res.trackDrFloat, expectedChDr * 0.5, 0.01);
    EXPECT_EQ(res.trackDrInt, 5);
}

TEST(DrAnalyzerTest, FloatingPointPeaksAboveFullScaleAreNotClipped)
{
    constexpr uint32_t sampleRate = 48000;
    constexpr size_t totalFrames  = sampleRate * 12U;
    const auto pcm                = generateStereoSine(sampleRate, totalFrames, 1000.0, 1.25);

    DrAnalyzer analyzer{sampleRate, 2};
    analyzer.process(pcm, totalFrames);
    const TrackDrResult res = analyzer.finish();

    const double expectedDb = 20.0 * std::log10(1.25); // +1.9382 dBFS
    EXPECT_NEAR(res.peakDb, expectedDb, 0.01);
    EXPECT_NEAR(res.rmsDb, expectedDb, 0.01);
    EXPECT_EQ(res.trackDrInt, 0);
}

TEST(DrAnalyzerTest, MultichannelExcludesLfeFromTrackDr)
{
    constexpr uint32_t sampleRate = 48000;
    const std::vector<ChannelInfo> layout{
        {"FL", false}, {"FR", false}, {"FC", false}, {"LFE", true}, {"BL", false}, {"BR", false},
    };
    DrAnalyzer analyzer{sampleRate, layout};
    const size_t blockFrames     = analyzer.blockFrames();
    const size_t totalFrames     = blockFrames * 4U;
    constexpr size_t pulseFrames = 7210U;
    const double expectedDr
        = 10.0 * std::log10(static_cast<double>(blockFrames) / (2.0 * static_cast<double>(pulseFrames)));
    const size_t chCount = layout.size();
    std::vector<double> pcm(totalFrames * chCount, 0.0);

    const double twoPiFreq = 2.0 * std::numbers::pi * 60.0 / static_cast<double>(sampleRate);
    for(size_t b = 0; b < 4U; ++b) {
        for(size_t f = 0; f < blockFrames; ++f) {
            const size_t frameIdx        = b * blockFrames + f;
            const double lfeVal          = std::sin(twoPiFreq * static_cast<double>(frameIdx)); // DR = 0 on LFE
            pcm[frameIdx * chCount + 3U] = lfeVal;

            if(f < pulseFrames) {
                for(size_t ch : {size_t{0}, size_t{1}, size_t{2}, size_t{4}, size_t{5}}) {
                    pcm[frameIdx * chCount + ch] = 0.8; // DR ~10.00 dB on non-LFE channels
                }
            }
        }
    }

    analyzer.process(pcm, totalFrames);
    const TrackDrResult res = analyzer.finish();

    ASSERT_EQ(res.channels.size(), 6U);
    EXPECT_TRUE(res.channels[3].isLfe);
    EXPECT_NEAR(res.channels[3].drDb, 0.0, 0.01);
    EXPECT_NEAR(res.trackDrFloat, expectedDr, 0.01);
    EXPECT_EQ(res.trackDrInt, 10);

    // Fallback when all channels are marked LFE
    DrAnalyzer lfeOnly{sampleRate, std::vector<ChannelInfo>{{"LFE", true}}};
    std::vector<double> mono(totalFrames, 0.0);
    for(size_t b = 0; b < 4U; ++b) {
        for(size_t f = 0; f < pulseFrames; ++f) {
            mono[b * blockFrames + f] = 0.5;
        }
    }
    lfeOnly.process(mono, totalFrames);
    const TrackDrResult lfeRes = lfeOnly.finish();
    EXPECT_NEAR(lfeRes.trackDrFloat, expectedDr, 0.01);
    EXPECT_EQ(lfeRes.trackDrInt, 10);
}

TEST(DrAnalyzerTest, OfficialAlbumDrAveragesUnroundedTrackDrFloats)
{
    std::vector<TrackDrResult> tracks(4);
    tracks[0].trackDrFloat = 12.45;
    tracks[0].trackDrInt   = 12;
    tracks[1].trackDrFloat = 12.45;
    tracks[1].trackDrInt   = 12;
    tracks[2].trackDrFloat = 12.45;
    tracks[2].trackDrInt   = 12;
    tracks[3].trackDrFloat = 13.45;
    tracks[3].trackDrInt   = 13;

    // Rounded ints average to 12.25 -> 12, whereas unrounded floats average to 12.70 -> 13.
    EXPECT_EQ(AlbumDrResult::computeOfficialDr(tracks), 13);
}

TEST(DrAnalyzerTest, NonFiniteSamplesAreIgnoredWithoutNaNOrSortUB)
{
    constexpr uint32_t sampleRate = 48000;
    DrAnalyzer cleanAnalyzer{sampleRate, 2};
    const size_t blockFrames     = cleanAnalyzer.blockFrames();
    constexpr size_t numBlocks   = 24U; // >16 blocks so std::ranges::sort exercises introsort partitioning
    constexpr size_t pulseFrames = 7210U;
    const size_t totalFrames     = blockFrames * numBlocks;

    std::vector<double> cleanPcm(totalFrames * 2U, 0.0);
    for(size_t b = 0; b < numBlocks; ++b) {
        const double amp   = (b == 0) ? 0.9 : (b <= 5 ? 0.5 : 0.1);
        const size_t count = (b == 0) ? 100U : pulseFrames;
        const size_t start = b * blockFrames;
        for(size_t f = 0; f < count; ++f) {
            cleanPcm[(start + f) * 2U]      = amp;
            cleanPcm[(start + f) * 2U + 1U] = amp;
        }
    }

    cleanAnalyzer.process(cleanPcm, totalFrames);
    const TrackDrResult cleanRes = cleanAnalyzer.finish();

    std::vector<double> dirtyPcm = cleanPcm;
    const double nanVal          = std::numeric_limits<double>::quiet_NaN();
    const double posInf          = std::numeric_limits<double>::infinity();
    const double negInf          = -std::numeric_limits<double>::infinity();

    for(size_t b = 0; b < numBlocks; ++b) {
        const size_t tailFrame = b * blockFrames + blockFrames - 3U;
        if(b % 3U == 0U) {
            dirtyPcm[tailFrame * 2U]      = nanVal;
            dirtyPcm[tailFrame * 2U + 1U] = nanVal;
        }
        else if(b % 3U == 1U) {
            dirtyPcm[tailFrame * 2U]      = posInf;
            dirtyPcm[tailFrame * 2U + 1U] = negInf;
        }
        else {
            dirtyPcm[tailFrame * 2U]      = 1e200; // finite sample whose square overflows to +Inf
            dirtyPcm[tailFrame * 2U + 1U] = -1e200;
        }
    }

    DrAnalyzer dirtyAnalyzer{sampleRate, 2};
    dirtyAnalyzer.process(dirtyPcm, totalFrames);
    const TrackDrResult dirtyRes = dirtyAnalyzer.finish();

    EXPECT_EQ(dirtyRes.totalFrames, totalFrames);
    EXPECT_TRUE(std::isfinite(dirtyRes.peakDb));
    EXPECT_TRUE(std::isfinite(dirtyRes.rmsDb));
    EXPECT_TRUE(std::isfinite(dirtyRes.trackDrFloat));
    EXPECT_EQ(dirtyRes.trackDrInt, cleanRes.trackDrInt);
    EXPECT_NEAR(dirtyRes.trackDrFloat, cleanRes.trackDrFloat, 1e-12);
    EXPECT_NEAR(dirtyRes.peakDb, cleanRes.peakDb, 1e-12);
    EXPECT_NEAR(dirtyRes.rmsDb, cleanRes.rmsDb, 1e-12);

    std::vector<double> allNonFinite(blockFrames * 20U * 2U, nanVal);
    allNonFinite[0] = posInf;
    allNonFinite[1] = negInf;
    DrAnalyzer allBadAnalyzer{sampleRate, 2};
    allBadAnalyzer.process(allNonFinite, blockFrames * 20U);
    const TrackDrResult allBadRes = allBadAnalyzer.finish();

    EXPECT_EQ(allBadRes.trackDrInt, 0);
    EXPECT_DOUBLE_EQ(allBadRes.trackDrFloat, 0.0);
    EXPECT_TRUE(std::isinf(allBadRes.peakDb) && allBadRes.peakDb < 0.0);
    EXPECT_TRUE(std::isinf(allBadRes.rmsDb) && allBadRes.rmsDb < 0.0);
}
} // namespace
} // namespace Fooyin::DrMeter
