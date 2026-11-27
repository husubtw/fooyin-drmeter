#include "core/drlogformatter.h"
#include "core/drversion.h"

#include <gtest/gtest.h>

#include <limits>
#include <locale>
#include <sstream>
#include <string>
#include <vector>

namespace Fooyin::DrMeter {
namespace {
class CommaDecimalFacet : public std::numpunct<char>
{
protected:
    char do_decimal_point() const override
    {
        return ',';
    }
};

class ScopedGlobalLocale
{
public:
    explicit ScopedGlobalLocale(const std::locale& loc)
        : m_previous{std::locale::global(loc)}
    { }

    ~ScopedGlobalLocale()
    {
        std::locale::global(m_previous);
    }

private:
    std::locale m_previous;
};

std::vector<std::string> splitLinesStripCr(const std::string& text)
{
    std::vector<std::string> lines;
    std::istringstream iss{text};
    std::string line;
    while(std::getline(iss, line)) {
        if(!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        lines.push_back(line);
    }
    return lines;
}

AlbumDrResult makeExampleAlbum()
{
    struct Row
    {
        int num;
        const char* title;
        int dr;
        double peak;
        double rms;
        uint64_t durationSec;
    };

    constexpr Row rows[] = {
        {1, "Track One", 11, -2.23, -16.35, 10 * 60 + 43},
        {2, "Track Two", 13, -1.17, -20.04, 3 * 60 + 42},
        {3, "Track Three", 12, -10.57, -26.08, 1 * 60 + 43},
        {4, "Track Four", 13, 0.00, -15.91, 4 * 60 + 22},
    };

    AlbumDrResult album;
    album.albumArtist = "Example Artist";
    album.album       = "Example Album";
    album.officialDr  = 12;

    for(const auto& r : rows) {
        TrackDrResult t;
        t.trackDrInt             = r.dr;
        t.trackDrFloat           = static_cast<double>(r.dr);
        t.peakDb                 = r.peak;
        t.rmsDb                  = r.rms;
        t.totalFrames            = r.durationSec * 44100ULL;
        t.durationSec            = static_cast<double>(r.durationSec);
        t.metadata.trackNumber   = r.num;
        t.metadata.title         = r.title;
        t.metadata.sampleRate    = 44100;
        t.metadata.channels      = 2;
        t.metadata.bitsPerSample = 16;
        t.metadata.bitrateKbps   = 750;
        t.metadata.codec         = "FLAC";
        album.tracks.push_back(std::move(t));
    }

    return album;
}

TEST(DrLogFormatterTest, MatchesExact80ColumnFooDrTxtLayoutSnapshot)
{
    const LogFormatOptions opts{
        .fooyinVersion = "0.15.1",
        .pluginVersion = "0.1.0",
        .logDate       = "2026-08-09 02:05:43",
    };

    const std::string expected = "fooyin 0.15.1 / DR Meter v0.1.0\n"
                                 "log date: 2026-08-09 02:05:43\n"
                                 "\n"
                                 "--------------------------------------------------------------------------------\n"
                                 "Analyzed: Example Artist / Example Album\n"
                                 "--------------------------------------------------------------------------------\n"
                                 "\n"
                                 "DR         Peak           RMS       Duration Track\n"
                                 "--------------------------------------------------------------------------------\n"
                                 "DR11      -2.23 dBFS   -16.35 dBFS     10:43 01-Track One\n"
                                 "DR13      -1.17 dBFS   -20.04 dBFS      3:42 02-Track Two\n"
                                 "DR12     -10.57 dBFS   -26.08 dBFS      1:43 03-Track Three\n"
                                 "DR13       0.00 dBFS   -15.91 dBFS      4:22 04-Track Four\n"
                                 "--------------------------------------------------------------------------------\n"
                                 "\n"
                                 "Number of tracks:  4\n"
                                 "Official DR value: DR12\n"
                                 "\n"
                                 "Samplerate:        44100 Hz\n"
                                 "Channels:          2\n"
                                 "Bits per sample:   16\n"
                                 "Bitrate:           750 kbps\n"
                                 "Codec:             FLAC\n"
                                 "================================================================================\n";

    const std::string generated = DrLogFormatter::formatAlbumLog(makeExampleAlbum(), opts);
    EXPECT_EQ(generated, expected);
}

TEST(DrLogFormatterTest, ClampsNegativeZeroAndFormatsInfinity)
{
    EXPECT_EQ(DrLogFormatter::formatDbValue(-0.0), "0.00 dBFS");
    EXPECT_EQ(DrLogFormatter::formatDbValue(-0.0049), "0.00 dBFS");
    EXPECT_EQ(DrLogFormatter::formatDbValue(-0.0051), "-0.01 dBFS");
    EXPECT_EQ(DrLogFormatter::formatDbValue(-std::numeric_limits<double>::infinity()), "-inf dBFS");

    AlbumDrResult album;
    album.albumArtist = "Example Artist";
    album.album       = "Full Scale Album";
    album.officialDr  = 13;

    TrackDrResult t;
    t.trackDrInt             = 13;
    t.peakDb                 = -0.0001;
    t.rmsDb                  = -15.91;
    t.totalFrames            = (4ULL * 60ULL + 22ULL) * 44100ULL;
    t.metadata.trackNumber   = 3;
    t.metadata.title         = "Track Three";
    t.metadata.sampleRate    = 44100;
    t.metadata.channels      = 2;
    t.metadata.bitsPerSample = 16;
    t.metadata.bitrateKbps   = 813;
    t.metadata.codec         = "FLAC";
    album.tracks.push_back(t);

    const std::string log = DrLogFormatter::formatAlbumLog(album, {.logDate = "2026-11-25 18:45:38"});
    EXPECT_NE(log.find("DR13       0.00 dBFS   -15.91 dBFS      4:22 03-Track Three\n"), std::string::npos);
    EXPECT_EQ(log.find("-0.00"), std::string::npos);
}

TEST(DrLogFormatterTest, FormatsTrackLabelsMixedMetadataAndMultiAlbumConcatenation)
{
    TrackMetadata missingNum;
    missingNum.trackNumber = 0;
    missingNum.title       = "Untitled";
    TrackMetadata singleDigit;
    singleDigit.trackNumber = 3;
    singleDigit.title       = "Track Three";
    TrackMetadata doubleDigit;
    doubleDigit.trackNumber = 12;
    doubleDigit.title       = "Finale";
    EXPECT_EQ(DrLogFormatter::formatTrackLabel(missingNum), "Untitled");
    EXPECT_EQ(DrLogFormatter::formatTrackLabel(singleDigit), "03-Track Three");
    EXPECT_EQ(DrLogFormatter::formatTrackLabel(doubleDigit), "12-Finale");

    AlbumDrResult mixed;
    mixed.albumArtist = "Various Artists";
    mixed.album       = "Mixed Compilation";
    mixed.officialDr  = 11;

    TrackDrResult t1;
    t1.trackDrInt  = 10;
    t1.peakDb      = -1.00;
    t1.rmsDb       = -12.50;
    t1.totalFrames = 180ULL * 44100ULL;
    t1.metadata    = {1, "First", "FLAC", 44100, 2, 16, 700};

    TrackDrResult t2;
    t2.trackDrInt  = 12;
    t2.peakDb      = -2.50;
    t2.rmsDb       = -16.25;
    t2.totalFrames = 240ULL * 96000ULL;
    t2.metadata    = {2, "Second", "ALAC", 96000, 2, 24, 1501};

    mixed.tracks = {t1, t2};

    const std::string singleLog = DrLogFormatter::formatAlbumLog(mixed, {.logDate = "2026-11-25 12:00:00"});
    EXPECT_NE(singleLog.find("Samplerate:        44100 Hz, 96000 Hz\n"), std::string::npos);
    EXPECT_NE(singleLog.find("Channels:          2\n"), std::string::npos);
    EXPECT_NE(singleLog.find("Bits per sample:   16, 24\n"), std::string::npos);
    EXPECT_NE(singleLog.find("Bitrate:           1101 kbps\n"), std::string::npos);
    EXPECT_NE(singleLog.find("Codec:             FLAC, ALAC\n"), std::string::npos);

    const std::vector<AlbumDrResult> twoAlbums{mixed, makeExampleAlbum()};
    const std::string multiLog = DrLogFormatter::formatMultiAlbumLog(twoAlbums, {.logDate = "2026-11-25 12:00:00"});
    const std::string sep
        = std::string{"================================================================================\n\nfooyin "}
        + std::string{MinFooyinVersion} + " / DR Meter v" + std::string{PluginVersion} + "\n";
    EXPECT_NE(multiLog.find(sep), std::string::npos);
}

TEST(DrLogFormatterTest, FormatsResultsTableWithPlainIntegerDrAndLocaleIndependence)
{
    AlbumDrResult album;
    album.albumArtist = "Example Artist";
    album.album       = "Example Album";
    album.officialDr  = 13;

    TrackDrResult t;
    t.trackDrInt           = 13;
    t.trackDrFloat         = 13.40;
    t.peakDb               = -1.17;
    t.rmsDb                = -20.04;
    t.totalFrames          = 222ULL * 44100ULL;
    t.metadata.trackNumber = 2;
    t.metadata.title       = "Track Two";
    t.metadata.sampleRate  = 44100;
    t.channels             = {
        ChannelDrResult{"FL", false, 12.92, -20.16, -1.92},
        ChannelDrResult{"FR", false, 13.89, -19.93, -0.001},
    };
    album.tracks.push_back(t);

    // Force a comma-decimal global C++ locale and verify outputs still use '.' strictly.
    const ScopedGlobalLocale commaLocale{std::locale{std::locale::classic(), new CommaDecimalFacet}};

    const std::string table = DrLogFormatter::formatResultsTable(std::span<const AlbumDrResult>{&album, 1});
    const auto lines        = splitLinesStripCr(table);

    ASSERT_EQ(lines.size(), 2U);
    EXPECT_EQ(lines[0], "Name\tTrack DR\tAlbum DR\tDR (FL)\tDR (FR)\tRMS (FL)\tRMS (FR)\tPeak (FL)\tPeak (FR)");
    EXPECT_EQ(lines[1], "02-Track Two\t13\t13\t12.92 dB\t13.89 dB\t-20.16 dBFS\t-19.93 dBFS\t-1.92 dBFS\t0.00 dBFS");

    const std::string log = DrLogFormatter::formatAlbumLog(album, {.logDate = "2026-11-25 12:00:00"});
    EXPECT_NE(log.find("-1.17 dBFS"), std::string::npos);
    EXPECT_NE(log.find("-20.04 dBFS"), std::string::npos);
    EXPECT_EQ(log.find("-1,17"), std::string::npos);
}

TEST(DrLogFormatterTest, DistinguishesPositiveNegativeInfinityAndNan)
{
    const double posInf = std::numeric_limits<double>::infinity();
    const double negInf = -std::numeric_limits<double>::infinity();
    const double qNan   = std::numeric_limits<double>::quiet_NaN();

    EXPECT_EQ(DrLogFormatter::formatDbValue(posInf, false), "+inf");
    EXPECT_EQ(DrLogFormatter::formatDbValue(posInf, true), "+inf dBFS");
    EXPECT_EQ(DrLogFormatter::formatDbValue(negInf, false), "-inf");
    EXPECT_EQ(DrLogFormatter::formatDbValue(negInf, true), "-inf dBFS");
    EXPECT_EQ(DrLogFormatter::formatDbValue(qNan, false), "nan");
    EXPECT_EQ(DrLogFormatter::formatDbValue(qNan, true), "nan dBFS");
}

TEST(DrLogFormatterTest, SanitizesControlCharsAndPreservesUtf8Metadata)
{
    AlbumDrResult album;
    album.albumArtist = "Art\rist\nŞğüöç\x01";
    album.album       = "Alb\r\num\té\x1F";
    album.officialDr  = 12;

    TrackDrResult t;
    t.trackDrInt             = 12;
    t.trackDrFloat           = 12.0;
    t.peakDb                 = -1.00;
    t.rmsDb                  = -14.00;
    t.totalFrames            = 60ULL * 44100ULL;
    t.metadata.trackNumber   = 1;
    t.metadata.title         = "日本語\tTitle\nLine\rEnd\x07";
    t.metadata.codec         = "FL\tAC\n\x02";
    t.metadata.sampleRate    = 44100;
    t.metadata.channels      = 2;
    t.metadata.bitsPerSample = 16;
    t.metadata.bitrateKbps   = 800;
    t.channels               = {
        ChannelDrResult{"F\tL\n\x01", false, 12.00, -14.00, -1.00},
        ChannelDrResult{"FR\rŞğüöç", false, 12.00, -14.00, -1.00},
    };
    album.tracks.push_back(t);

    EXPECT_EQ(DrLogFormatter::formatTrackLabel(t.metadata), "01-日本語 Title Line End");

    const std::string log = DrLogFormatter::formatAlbumLog(album, {.logDate = "2026-11-26 12:00:00"});
    EXPECT_NE(log.find("Analyzed: Art ist Şğüöç / Alb um é\n"), std::string::npos);
    EXPECT_NE(log.find(" 01-日本語 Title Line End\n"), std::string::npos);
    EXPECT_NE(log.find("Codec:             FL AC\n"), std::string::npos);
    EXPECT_EQ(log.find('\r'), std::string::npos);
    EXPECT_EQ(log.find('\t'), std::string::npos);
    EXPECT_EQ(log.find('\x01'), std::string::npos);
    EXPECT_EQ(log.find('\x07'), std::string::npos);
    EXPECT_EQ(log.find('\x1F'), std::string::npos);

    const std::string table = DrLogFormatter::formatResultsTable(std::span<const AlbumDrResult>{&album, 1});
    const auto lines        = splitLinesStripCr(table);
    ASSERT_EQ(lines.size(), 2U);
    EXPECT_EQ(lines[0],
              "Name\tTrack DR\tAlbum DR\tDR (F L)\tDR (FR Şğüöç)\tRMS (F L)\tRMS (FR Şğüöç)\tPeak (F L)\tPeak (FR "
              "Şğüöç)");
    EXPECT_EQ(lines[1],
              "01-日本語 Title Line End\t12\t12\t12.00 dB\t12.00 dB\t-14.00 dBFS\t-14.00 dBFS\t-1.00 dBFS\t-1.00 dBFS");
}

TEST(DrLogFormatterTest, ControlOnlyMetadataFallsBackCleanly)
{
    AlbumDrResult album;
    album.albumArtist = "\r\n\t\x01 ";
    album.album       = "\t\n\x1F";
    album.officialDr  = 11;

    TrackDrResult t;
    t.trackDrInt             = 11;
    t.trackDrFloat           = 11.0;
    t.peakDb                 = -1.50;
    t.rmsDb                  = -15.00;
    t.totalFrames            = 60ULL * 44100ULL;
    t.metadata.trackNumber   = 1;
    t.metadata.title         = "\tTrack One\r\n";
    t.metadata.codec         = "\n\x07\t";
    t.metadata.sampleRate    = 44100;
    t.metadata.channels      = 2;
    t.metadata.bitsPerSample = 16;
    album.tracks.push_back(t);

    const std::string log = DrLogFormatter::formatAlbumLog(album, {.logDate = "2026-11-26 12:00:00"});
    EXPECT_NE(log.find("Analyzed: Unknown Artist / Unknown Album\n"), std::string::npos);
    EXPECT_NE(log.find(" 01-Track One\n"), std::string::npos);
    EXPECT_EQ(log.find("Codec:"), std::string::npos);

    // Control-only albumArtist with a valid album must not emit a blank artist prefix.
    album.album                    = "  \tValid Album\n";
    const std::string albumOnlyLog = DrLogFormatter::formatAlbumLog(album, {.logDate = "2026-11-26 12:00:00"});
    EXPECT_NE(albumOnlyLog.find("Analyzed: Valid Album\n"), std::string::npos);
}
} // namespace
} // namespace Fooyin::DrMeter
