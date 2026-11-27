#include "core/drversion.h"
#include "plugin/drlogexporter.h"
#include "plugin/drresultsmodel.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include <gtest/gtest.h>

#include <limits>
#include <locale>
#include <string>
#include <vector>

#ifdef __unix__
#include <unistd.h>
#endif

using namespace Qt::StringLiterals;

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

ScannedAlbum makeValidAlbum(const QString& dir, const std::string& artist, const std::string& albumName, int officialDr,
                            const std::string& title, int trackNum)
{
    ScannedAlbum sa;
    sa.directory         = dir;
    sa.groupKey          = dir;
    sa.album.albumArtist = artist;
    sa.album.album       = albumName;
    sa.album.officialDr  = officialDr;

    TrackDrResult dr;
    dr.trackDrInt             = officialDr;
    dr.trackDrFloat           = static_cast<double>(officialDr);
    dr.peakDb                 = -1.25;
    dr.rmsDb                  = -16.50;
    dr.totalFrames            = 200ULL * 44100ULL;
    dr.durationSec            = 200.0;
    dr.metadata.trackNumber   = trackNum;
    dr.metadata.title         = title;
    dr.metadata.codec         = "FLAC";
    dr.metadata.sampleRate    = 44100;
    dr.metadata.channels      = 2;
    dr.metadata.bitsPerSample = 16;
    dr.metadata.bitrateKbps   = 800;
    dr.channels               = {
        ChannelDrResult{"FL", false, 12.50, -16.40, -1.25},
        ChannelDrResult{"FR", false, 13.50, -16.60, -0.002},
    };

    ScannedTrack st;
    st.track = Track{QDir{dir}.filePath(u"track.flac"_s)};
    st.dr    = dr;
    st.ok    = true;

    sa.tracks.push_back(st);
    sa.album.tracks.push_back(dr);
    return sa;
}

TEST(DrResultsModelTest, FormatsStereoMultichannelMixedAndFailedTracks)
{
    ScannedAlbum album;
    album.album.albumArtist = "Test Artist";
    album.album.album       = "Mixed Layout";
    album.album.officialDr  = 13;

    // Row 0: Stereo track with near-zero peak (-0.002 -> 0.00 dBFS)
    ScannedTrack stereoTrack;
    stereoTrack.ok                      = true;
    stereoTrack.dr.trackDrInt           = 13;
    stereoTrack.dr.metadata.trackNumber = 2;
    stereoTrack.dr.metadata.title       = "Track Two";
    stereoTrack.dr.channels             = {
        ChannelDrResult{"FL", false, 12.92, -20.16, -1.92},
        ChannelDrResult{"FR", false, 13.89, -19.93, -0.002},
    };
    album.tracks.push_back(stereoTrack);

    // Row 1: 5.1 surround track with silent LFE (-inf dBFS)
    ScannedTrack surroundTrack;
    surroundTrack.ok                      = true;
    surroundTrack.dr.trackDrInt           = 11;
    surroundTrack.dr.metadata.trackNumber = 3;
    surroundTrack.dr.metadata.title       = "Surround Mix";
    surroundTrack.dr.channels             = {
        ChannelDrResult{"FL", false, 11.10, -18.00, -1.00},
        ChannelDrResult{"FR", false, 11.20, -18.10, -1.10},
        ChannelDrResult{"FC", false, 10.50, -19.00, -2.00},
        ChannelDrResult{"LFE", true, 0.00, -std::numeric_limits<double>::infinity(),
                        -std::numeric_limits<double>::infinity()},
        ChannelDrResult{"BL", false, 12.00, -21.00, -3.00},
        ChannelDrResult{"BR", false, 12.10, -21.10, -3.10},
    };
    album.tracks.push_back(surroundTrack);

    // Row 2: Failed track
    ScannedTrack failedTrack;
    failedTrack.ok    = false;
    failedTrack.error = u"No decoder available"_s;
    failedTrack.track = Track{u"/music/broken.xyz"_s};
    failedTrack.track.setTitle(u"Broken Track"_s);
    album.tracks.push_back(failedTrack);

    const ScopedGlobalLocale commaLocale{std::locale{std::locale::classic(), new CommaDecimalFacet}};

    DrResultsModel model{{album}};

    ASSERT_EQ(model.rowCount(), 3);
    // 3 base cols + 6 unique channels * 3 metrics = 21 cols
    ASSERT_EQ(model.columnCount(), 21);

    EXPECT_EQ(model.headerData(0, Qt::Horizontal).toString(), u"Name"_s);
    EXPECT_EQ(model.headerData(1, Qt::Horizontal).toString(), u"Track DR"_s);
    EXPECT_EQ(model.headerData(2, Qt::Horizontal).toString(), u"Album DR"_s);
    EXPECT_EQ(model.headerData(3, Qt::Horizontal).toString(), u"DR (FL)"_s);
    EXPECT_EQ(model.headerData(6, Qt::Horizontal).toString(), u"DR (LFE)"_s);
    EXPECT_EQ(model.headerData(9, Qt::Horizontal).toString(), u"RMS (FL)"_s);
    EXPECT_EQ(model.headerData(15, Qt::Horizontal).toString(), u"Peak (FL)"_s);
    EXPECT_EQ(model.headerData(20, Qt::Horizontal).toString(), u"Peak (BR)"_s);

    // Row 0 (stereo): plain int DR, C-locale decimals, no -0.00 dBFS, empty 5.1 cells
    EXPECT_EQ(model.data(model.index(0, 0)).toString(), u"02-Track Two"_s);
    EXPECT_EQ(model.data(model.index(0, 1)).toString(), u"13"_s);
    EXPECT_EQ(model.data(model.index(0, 2)).toString(), u"13"_s);
    EXPECT_EQ(model.data(model.index(0, 3)).toString(), u"12.92 dB"_s);
    EXPECT_EQ(model.data(model.index(0, 4)).toString(), u"13.89 dB"_s);
    EXPECT_TRUE(model.data(model.index(0, 5)).toString().isEmpty()); // FC absent on stereo row
    EXPECT_EQ(model.data(model.index(0, 9)).toString(), u"-20.16 dBFS"_s);
    EXPECT_EQ(model.data(model.index(0, 10)).toString(), u"-19.93 dBFS"_s);
    EXPECT_EQ(model.data(model.index(0, 15)).toString(), u"-1.92 dBFS"_s);
    EXPECT_EQ(model.data(model.index(0, 16)).toString(), u"0.00 dBFS"_s);

    // Row 1 (5.1): LFE silent formatted as -inf dBFS
    EXPECT_EQ(model.data(model.index(1, 0)).toString(), u"03-Surround Mix"_s);
    EXPECT_EQ(model.data(model.index(1, 1)).toString(), u"11"_s);
    EXPECT_EQ(model.data(model.index(1, 12)).toString(), u"-inf dBFS"_s);
    EXPECT_EQ(model.data(model.index(1, 18)).toString(), u"-inf dBFS"_s);
    EXPECT_EQ(model.data(model.index(1, 20)).toString(), u"-3.10 dBFS"_s);

    // Row 2 (failed): shows Error + tooltip, empty numeric columns
    EXPECT_EQ(model.data(model.index(2, 0)).toString(), u"Broken Track"_s);
    EXPECT_EQ(model.data(model.index(2, 1)).toString(), u"Error"_s);
    EXPECT_TRUE(model.data(model.index(2, 2)).toString().isEmpty());
    EXPECT_TRUE(model.data(model.index(2, 3)).toString().isEmpty());
    EXPECT_EQ(model.data(model.index(2, 1), Qt::ToolTipRole).toString(), u"No decoder available"_s);
}

TEST(DrResultsModelTest, FormatsMeasurementFooterCLocale)
{
    const ScopedGlobalLocale commaLocale{std::locale{std::locale::classic(), new CommaDecimalFacet}};

    EXPECT_EQ(formatMeasurementFooter(1602, 1802.345), u"Measured in: 0:01.602, speed: 1802.35x"_s);
    EXPECT_EQ(formatMeasurementFooter(65004, 42.5), u"Measured in: 1:05.004, speed: 42.50x"_s);
    EXPECT_EQ(formatMeasurementFooter(-50, 0.0), u"Measured in: 0:00.000, speed: 0.00x"_s);
}

TEST(DrResultsModelTest, SanitizesChannelLabelsAndFailedTrackNamesMatchingFormatter)
{
    ScannedAlbum album;
    album.album.albumArtist = "Test Artist";
    album.album.album       = "Sanitized Album";
    album.album.officialDr  = 12;

    ScannedTrack track1;
    track1.ok                      = true;
    track1.dr.trackDrInt           = 12;
    track1.dr.metadata.trackNumber = 1;
    track1.dr.metadata.title       = "Track\tOne\n\x07";
    track1.dr.channels             = {
        ChannelDrResult{"F\tL\n\x01", false, 12.10, -16.00, -1.00},
        ChannelDrResult{"FR\rŞğüöç", false, 12.20, -16.10, -1.10},
    };
    album.tracks.push_back(track1);
    album.album.tracks.push_back(track1.dr);

    // Second track uses already-clean "F L" label; must deduplicate with track1's sanitized "F\tL\n\x01"
    ScannedTrack track2;
    track2.ok                      = true;
    track2.dr.trackDrInt           = 13;
    track2.dr.metadata.trackNumber = 2;
    track2.dr.metadata.title       = "Track Two";
    track2.dr.channels             = {
        ChannelDrResult{"F L", false, 13.00, -17.00, -2.00},
    };
    album.tracks.push_back(track2);
    album.album.tracks.push_back(track2.dr);

    ScannedTrack failedTrack;
    failedTrack.ok    = false;
    failedTrack.error = u"Decode error"_s;
    failedTrack.track = Track{u"/music/bad.flac"_s};
    failedTrack.track.setTitle(u"Broken\r\nTrack\tŞğüöç\x1F"_s);
    album.tracks.push_back(failedTrack);

    DrResultsModel model{{album}};

    // 3 base cols + 2 unique sanitized channels * 3 metrics = 9 cols
    ASSERT_EQ(model.columnCount(), 9);
    ASSERT_EQ(model.channelLabels().size(), 2U);
    EXPECT_EQ(model.channelLabels()[0], "F L");
    EXPECT_EQ(model.channelLabels()[1], "FR Şğüöç");

    EXPECT_EQ(model.headerData(3, Qt::Horizontal).toString(), u"DR (F L)"_s);
    EXPECT_EQ(model.headerData(4, Qt::Horizontal).toString(), u"DR (FR Şğüöç)"_s);
    EXPECT_EQ(model.headerData(5, Qt::Horizontal).toString(), u"RMS (F L)"_s);
    EXPECT_EQ(model.headerData(8, Qt::Horizontal).toString(), u"Peak (FR Şğüöç)"_s);

    // Row 0 & Row 1 both resolve values under deduplicated "F L" channel column
    EXPECT_EQ(model.data(model.index(0, 0)).toString(), u"01-Track One"_s);
    EXPECT_EQ(model.data(model.index(0, 3)).toString(), u"12.10 dB"_s);
    EXPECT_EQ(model.data(model.index(0, 4)).toString(), u"12.20 dB"_s);
    EXPECT_EQ(model.data(model.index(1, 3)).toString(), u"13.00 dB"_s);
    EXPECT_TRUE(model.data(model.index(1, 4)).toString().isEmpty());

    // Row 2 (failed): control chars stripped/collapsed while preserving UTF-8
    EXPECT_EQ(model.data(model.index(2, 0)).toString(), u"Broken Track Şğüöç"_s);
    EXPECT_EQ(model.data(model.index(2, 1)).toString(), u"Error"_s);

    // Verify parity with DrLogFormatter::formatResultsTable header
    const std::string tsv = DrLogFormatter::formatResultsTable(std::span<const AlbumDrResult>{&album.album, 1});
    const auto headerEnd  = tsv.find('\n');
    ASSERT_NE(headerEnd, std::string::npos);
    EXPECT_EQ(tsv.substr(0, headerEnd),
              "Name\tTrack DR\tAlbum DR\tDR (F L)\tDR (FR Şğüöç)\tRMS (F L)\tRMS (FR Şğüöç)\tPeak (F L)\tPeak (FR "
              "Şğüöç)");
}

TEST(DrLogExporterTest, GroupsMultiAlbumByDirectoryConcatenatesAndDetectsOverwrites)
{
    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());

    const QString dirA = tempDir.filePath(u"album_shared"_s);
    const QString dirB = tempDir.filePath(u"album_existing"_s);
    ASSERT_TRUE(QDir{}.mkpath(dirA));
    ASSERT_TRUE(QDir{}.mkpath(dirB));

    // Pre-create foo_dr.txt in dirB to verify overwrite detection
    const QString existingLogPath = QDir{dirB}.filePath(u"foo_dr.txt"_s);
    {
        QFile existing{existingLogPath};
        ASSERT_TRUE(existing.open(QIODevice::WriteOnly | QIODevice::Truncate));
        existing.write("old log");
    }

    // Two albums sharing dirA (e.g., Disc 1 and Disc 2), one album in dirB, and one all-failed album
    const ScannedAlbum disc1  = makeValidAlbum(dirA, "Artist A", "Double Album (Disc 1)", 13, "Opening", 1);
    const ScannedAlbum disc2  = makeValidAlbum(dirA, "Artist A", "Double Album (Disc 2)", 12, "Encore", 1);
    const ScannedAlbum albumB = makeValidAlbum(dirB, "Artist B", "Second Album", 14, "Finale", 1);

    ScannedAlbum emptyFailed;
    emptyFailed.directory = tempDir.filePath(u"failed_only"_s);
    ScannedTrack bad;
    bad.ok = false;
    emptyFailed.tracks.push_back(bad);

    const std::vector<ScannedAlbum> allAlbums{disc1, disc2, albumB, emptyFailed};
    EXPECT_EQ(DrLogExporter::collectValidAlbums(allAlbums).size(), 3U);

    const QDateTime fixedTime      = QDateTime::fromString(u"2026-11-25 18:45:38"_s, u"yyyy-MM-dd HH:mm:ss"_s);
    const LogFormatOptions options = DrLogExporter::buildLogFormatOptions(fixedTime);
    EXPECT_EQ(options.logDate, "2026-11-25 18:45:38");
    EXPECT_EQ(options.pluginVersion, PluginVersion);
    EXPECT_FALSE(options.fooyinVersion.empty());

    const auto targets = DrLogExporter::planDirectoryLogWrites(allAlbums, u"foo_dr.txt"_s, options);
    ASSERT_EQ(targets.size(), 2U);

    // Target 0: dirA combines Disc 1 + Disc 2 into one concatenated foo_dr.txt, not existing yet
    EXPECT_EQ(targets[0].filePath, QDir{dirA}.filePath(u"foo_dr.txt"_s));
    EXPECT_FALSE(targets[0].alreadyExists);
    EXPECT_EQ(targets[0].albums.size(), 2U);
    EXPECT_NE(targets[0].content.find("Analyzed: Artist A / Double Album (Disc 1)"), std::string::npos);
    EXPECT_NE(targets[0].content.find("Analyzed: Artist A / Double Album (Disc 2)"), std::string::npos);
    EXPECT_NE(targets[0].content.find(
                  "================================================================================\n\nfooyin "),
              std::string::npos);

    // Target 1: dirB flags existing foo_dr.txt for overwrite
    EXPECT_EQ(targets[1].filePath, existingLogPath);
    EXPECT_TRUE(targets[1].alreadyExists);
    EXPECT_EQ(targets[1].albums.size(), 1U);

    const LogWriteOutcome outcome = DrLogExporter::writePlannedLogs(targets);
    EXPECT_EQ(outcome.writtenFiles, 2);
    EXPECT_EQ(outcome.failedFiles, 0);
    EXPECT_TRUE(outcome.errors.isEmpty());

    for(const auto& target : targets) {
        QFile f{target.filePath};
        ASSERT_TRUE(f.open(QIODevice::ReadOnly));
        const QByteArray bytes = f.readAll();
        EXPECT_EQ(std::string(bytes.constData(), static_cast<size_t>(bytes.size())), target.content);
    }
}

TEST(DrLogExporterTest, SanitizesUnsafeLogFileNamesAndRejectsDirectoryWrite)
{
    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());

    const QString dirA = tempDir.filePath(u"album_a"_s);
    const QString dirB = tempDir.filePath(u"album_b"_s);
    ASSERT_TRUE(QDir{}.mkpath(dirA));
    ASSERT_TRUE(QDir{}.mkpath(dirB));

    const ScannedAlbum albumA      = makeValidAlbum(dirA, "Artist A", "Album A", 12, "Track A", 1);
    const ScannedAlbum albumB      = makeValidAlbum(dirB, "Artist B", "Album B", 13, "Track B", 1);
    const LogFormatOptions options = DrLogExporter::buildLogFormatOptions(
        QDateTime::fromString(u"2026-11-25 18:45:38"_s, u"yyyy-MM-dd HH:mm:ss"_s));

    const QStringList unsafeNames{
        u"/tmp/x.txt"_s, u"../escape.txt"_s,   u"sub/foo_dr.txt"_s, uR"(sub\foo_dr.txt)"_s, u"."_s,
        u".."_s,         u"bad\x01name.txt"_s,
    };

    for(const QString& unsafe : unsafeNames) {
        const auto targets = DrLogExporter::planDirectoryLogWrites({albumA, albumB}, unsafe, options);
        ASSERT_EQ(targets.size(), 2U) << unsafe.toStdString();
        EXPECT_EQ(targets[0].filePath, QDir{dirA}.filePath(u"foo_dr.txt"_s)) << unsafe.toStdString();
        EXPECT_EQ(targets[1].filePath, QDir{dirB}.filePath(u"foo_dr.txt"_s)) << unsafe.toStdString();

        const LogWriteOutcome outcome = DrLogExporter::writePlannedLogs(targets);
        EXPECT_EQ(outcome.writtenFiles, 2) << unsafe.toStdString();
        EXPECT_EQ(outcome.failedFiles, 0) << unsafe.toStdString();
    }

    EXPECT_FALSE(QFileInfo::exists(tempDir.filePath(u"escape.txt"_s)));
    EXPECT_FALSE(QFileInfo::exists(QDir{dirA}.filePath(u"sub"_s)));
    EXPECT_FALSE(QFileInfo::exists(QDir{dirB}.filePath(u"sub"_s)));

    // Direct write to an existing directory path must be rejected explicitly
    QString dirErr;
    EXPECT_FALSE(DrLogExporter::writeSingleLogFile(dirA, "test content", &dirErr));
    EXPECT_FALSE(dirErr.isEmpty());
}

TEST(DrLogExporterTest, SkipsEmptyOrRelativeDirectoriesAndContinuesAfterUnwritableTarget)
{
    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());

    const QString writableDir = tempDir.filePath(u"writable_album"_s);
    ASSERT_TRUE(QDir{}.mkpath(writableDir));

    // A regular file used as a fake parent directory reliably blocks mkpath even under root (e.g. Docker CI)
    const QString blockingFile = tempDir.filePath(u"not_a_dir"_s);
    {
        QFile f{blockingFile};
        ASSERT_TRUE(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        f.write("block");
    }
    const QString blockedDir = QDir{blockingFile}.filePath(u"nested"_s);

    const ScannedAlbum emptyDirAlbum    = makeValidAlbum(u""_s, "Artist E", "Empty Dir", 11, "T1", 1);
    const ScannedAlbum relativeDirAlbum = makeValidAlbum(u"relative/album"_s, "Artist R", "Relative Dir", 12, "T2", 1);
    const ScannedAlbum blockedAlbum     = makeValidAlbum(blockedDir, "Artist B", "Blocked Dir", 13, "T3", 1);
    const ScannedAlbum writableAlbum    = makeValidAlbum(writableDir, "Artist W", "Writable Dir", 14, "T4", 1);

    const LogFormatOptions options = DrLogExporter::buildLogFormatOptions(
        QDateTime::fromString(u"2026-11-25 18:45:38"_s, u"yyyy-MM-dd HH:mm:ss"_s));

    std::vector<ScannedAlbum> inputs{emptyDirAlbum, relativeDirAlbum, blockedAlbum, writableAlbum};
    size_t expectedTargets = 2U;
    int expectedFailed     = 1;

    QString readOnlyDir;
#ifdef __unix__
    const bool canTestReadOnlyDir = (::geteuid() != 0);
    if(canTestReadOnlyDir) {
        readOnlyDir = tempDir.filePath(u"readonly_album"_s);
        ASSERT_TRUE(QDir{}.mkpath(readOnlyDir));
        ASSERT_TRUE(QFile::setPermissions(readOnlyDir, QFileDevice::ReadOwner | QFileDevice::ExeOwner));
        inputs.push_back(makeValidAlbum(readOnlyDir, "Artist RO", "ReadOnly Dir", 10, "T5", 1));
        ++expectedTargets;
        ++expectedFailed;
    }
#endif

    const auto targets = DrLogExporter::planDirectoryLogWrites(inputs, u"foo_dr.txt"_s, options);
    ASSERT_EQ(targets.size(), expectedTargets);
    EXPECT_EQ(targets[0].directory, QDir::cleanPath(blockedDir));
    EXPECT_EQ(targets[1].directory, QDir::cleanPath(writableDir));

    const LogWriteOutcome outcome = DrLogExporter::writePlannedLogs(targets);
    EXPECT_EQ(outcome.writtenFiles, 1);
    EXPECT_EQ(outcome.failedFiles, expectedFailed);
    EXPECT_EQ(outcome.errors.size(), expectedFailed);
    ASSERT_EQ(outcome.writtenPaths.size(), 1);
    EXPECT_EQ(outcome.writtenPaths[0], QDir{writableDir}.filePath(u"foo_dr.txt"_s));
    EXPECT_TRUE(QFileInfo::exists(outcome.writtenPaths[0]));

#ifdef __unix__
    if(!readOnlyDir.isEmpty()) {
        QFile::setPermissions(readOnlyDir, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    }
#endif
}
} // namespace
} // namespace Fooyin::DrMeter
