#include "core/dranalyzer.h"

#include <gtest/gtest.h>

namespace Fooyin::DrMeter {
namespace {
TEST(DrAnalyzerSmokeTest, PreservesFormatAndFooDrMeterBlockSizeAcrossRates)
{
    const DrAnalyzer cdAnalyzer{44100, 2};
    EXPECT_EQ(cdAnalyzer.sampleRate(), 44100U);
    EXPECT_EQ(cdAnalyzer.channels(), 2U);
    EXPECT_EQ(cdAnalyzer.blockFrames(), 132480U);

    EXPECT_EQ((DrAnalyzer{88200, 2}.blockFrames()), 264960U);
    EXPECT_EQ((DrAnalyzer{176400, 2}.blockFrames()), 529920U);
    EXPECT_EQ((DrAnalyzer{352800, 2}.blockFrames()), 1059840U);
    EXPECT_EQ((DrAnalyzer{48000, 2}.blockFrames()), 144195U);
    EXPECT_EQ((DrAnalyzer{96000, 2}.blockFrames()), 288391U);
}
} // namespace
} // namespace Fooyin::DrMeter
