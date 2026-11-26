#include "core/drlogformatter.h"
#include "plugin/dralbumgrouper.h"
#include "plugin/drscanner.h"
#include "plugin/drsettings.h"
#include "plugin/drtagwriter.h"

#include <core/constants.h>
#include <core/engine/audioinput.h>
#include <core/engine/audioloader.h>
#include <core/library/musiclibrary.h>
#include <core/track.h>
#include <utils/settings/settingsmanager.h>

#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QPromise>
#include <QTemporaryDir>
#include <QTimer>

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <memory>
#include <thread>
#include <vector>

using namespace Qt::StringLiterals;

namespace Fooyin::DrMeter {
namespace {
class StubAudioReader final : public AudioReader
{
public:
    StubAudioReader(QStringList extensions, bool writable)
        : m_extensions{std::move(extensions)}
        , m_writable{writable}
    { }

    [[nodiscard]] QStringList extensions() const override
    {
        return m_extensions;
    }

    [[nodiscard]] bool canReadCover() const override
    {
        return false;
    }

    [[nodiscard]] bool canWriteMetaData() const override
    {
        return m_writable;
    }

    [[nodiscard]] bool readTrack(const AudioSource& /*source*/, Track& /*track*/) override
    {
        return true;
    }

private:
    QStringList m_extensions;
    bool m_writable{false};
};

class FakeMusicLibrary final : public MusicLibrary
{
public:
    explicit FakeMusicLibrary(QObject* parent = nullptr)
        : MusicLibrary{parent}
    { }

    [[nodiscard]] bool hasLibrary() const override
    {
        return false;
    }
    [[nodiscard]] std::optional<LibraryInfo> libraryInfo(int /*id*/) const override
    {
        return {};
    }
    [[nodiscard]] std::optional<LibraryInfo> libraryForPath(const QString& /*path*/) const override
    {
        return {};
    }
    void loadAllTracks() override { }
    [[nodiscard]] bool isEmpty() const override
    {
        return true;
    }
    void refreshAll() override { }
    void rescanAll() override { }
    ScanRequest refresh(const LibraryInfo& /*library*/) override
    {
        return {};
    }
    ScanRequest rescan(const LibraryInfo& /*library*/) override
    {
        return {};
    }
    void cancelScan(int /*id*/) override { }
    ScanRequest scanTracks(const TrackList& /*tracks*/) override
    {
        return {};
    }
    ScanRequest scanModifiedTracks(const TrackList& /*tracks*/) override
    {
        return {};
    }
    ScanRequest scanFiles(const QList<QUrl>& /*files*/) override
    {
        return {};
    }
    ScanRequest loadPlaylist(const QList<QUrl>& /*files*/) override
    {
        return {};
    }
    [[nodiscard]] TrackList tracks() const override
    {
        return {};
    }
    [[nodiscard]] TrackList libraryTracks() const override
    {
        return {};
    }
    [[nodiscard]] Track trackForId(int /*id*/) const override
    {
        return {};
    }
    [[nodiscard]] TrackList tracksForIds(const TrackIds& /*ids*/) const override
    {
        return {};
    }
    [[nodiscard]] std::shared_ptr<TrackMetadataStore> metadataStore() const override
    {
        return {};
    }
    void updateTrack(const Track& /*track*/) override { }
    void updateTracks(const TrackList& /*tracks*/) override { }
    void updateTrackMetadata(const TrackList& /*tracks*/) override { }

    WriteRequest writeTrackMetadata(const TrackList& tracks) override
    {
        ++writeCallCount;
        lastWrittenTracks = tracks;
        cancelCalled      = false;

        if(returnInvalidRequest) {
            return {};
        }

        auto promise = std::make_shared<QPromise<WriteResult>>();
        promise->start();
        auto future = promise->future();

        if(deferCompletion) {
            pendingPromise = promise;
        }
        else {
            const int failed    = std::min(simulatedFailedCount, static_cast<int>(tracks.size()));
            const int succeeded = static_cast<int>(tracks.size()) - failed;
            QTimer::singleShot(5, this, [promise, succeeded, failed]() {
                promise->addResult(WriteResult{
                    .state     = WriteState::Completed,
                    .succeeded = succeeded,
                    .failed    = failed,
                });
                promise->finish();
            });
        }

        WriteRequest req;
        req.finished = future;
        if(!omitCancelCallback) {
            req.cancel = [this, promise]() {
                cancelCalled = true;
                if(!promise->future().isFinished()) {
                    promise->addResult(WriteResult{
                        .state     = WriteState::Cancelled,
                        .succeeded = 0,
                        .failed    = 0,
                    });
                    promise->finish();
                }
            };
        }
        return req;
    }

    WriteRequest writeTrackCovers(const TrackCoverData& /*coverData*/) override
    {
        return {};
    }
    [[nodiscard]] PendingTrackCoverProvider* pendingTrackCoverProvider() const override
    {
        return nullptr;
    }
    void updateTrackStats(const TrackList& /*tracks*/, Track::Stats /*stats*/) override { }
    void updateTrackStats(const Track& /*track*/, Track::Stats /*stats*/) override { }
    WriteRequest removeUnavailbleTracks() override
    {
        return {};
    }
    WriteRequest deleteTracks(const TrackList& /*tracks*/) override
    {
        return {};
    }

    int writeCallCount{0};
    int simulatedFailedCount{0};
    bool deferCompletion{false};
    bool returnInvalidRequest{false};
    bool omitCancelCallback{false};
    bool cancelCalled{false};
    TrackList lastWrittenTracks;
    std::shared_ptr<QPromise<WriteResult>> pendingPromise;
};

class SlowSyntheticDecoder final : public AudioDecoder
{
public:
    explicit SlowSyntheticDecoder(std::shared_ptr<std::atomic<int>> readsCompleted)
        : m_readsCompleted{std::move(readsCompleted)}
    { }

    [[nodiscard]] QStringList extensions() const override
    {
        return {u"slowtest"_s};
    }
    [[nodiscard]] bool isSeekable() const override
    {
        return false;
    }

    std::optional<AudioFormat> init(const AudioSource& /*source*/, const Track& /*track*/,
                                    DecoderOptions /*options*/) override
    {
        m_format = AudioFormat{SampleFormat::F64, 48000, 2};
        m_chunks = 0;
        return m_format;
    }

    void stop() override
    {
        m_chunks = 0;
    }
    void seek(uint64_t /*pos*/) override { }

    ReadResult readAudio(size_t /*bytes*/) override
    {
        if(abortToken().stop_requested()) {
            return ReadResult::errorResult(u"Aborted"_s);
        }
        if(m_chunks >= 500) {
            return ReadResult::endOfStream();
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{5});
        if(abortToken().stop_requested()) {
            return ReadResult::errorResult(u"Aborted"_s);
        }

        constexpr int frames = 4800;
        std::vector<double> data(static_cast<size_t>(frames) * 2U, 0.25);
        AudioBuffer buf{reinterpret_cast<const uint8_t*>(data.data()), data.size() * sizeof(double), m_format,
                        static_cast<uint64_t>(m_chunks) * 100ULL};
        ++m_chunks;
        if(m_readsCompleted) {
            m_readsCompleted->fetch_add(1, std::memory_order_relaxed);
        }
        return ReadResult::data(std::move(buf));
    }

    AudioBuffer readBuffer(size_t bytes) override
    {
        auto res = readAudio(bytes);
        return res.status == ReadStatus::DecodedAudio ? std::move(res.buffer) : AudioBuffer{};
    }

private:
    AudioFormat m_format;
    int m_chunks{0};
    std::shared_ptr<std::atomic<int>> m_readsCompleted;
};

class FastSyntheticDecoder final : public AudioDecoder
{
public:
    FastSyntheticDecoder(std::shared_ptr<std::atomic<int>> stopsCompleted,
                         std::shared_ptr<std::atomic<int>> abortAfterStop, int stopDelayMs = 0)
        : m_stopsCompleted{std::move(stopsCompleted)}
        , m_abortAfterStop{std::move(abortAfterStop)}
        , m_stopDelayMs{stopDelayMs}
    { }

    [[nodiscard]] QStringList extensions() const override
    {
        return {u"fasttest"_s};
    }
    [[nodiscard]] bool isSeekable() const override
    {
        return false;
    }

    std::optional<AudioFormat> init(const AudioSource& /*source*/, const Track& /*track*/,
                                    DecoderOptions /*options*/) override
    {
        m_format  = AudioFormat{SampleFormat::F64, 48000, 2};
        m_emitted = false;
        m_stopStarted.store(false, std::memory_order_release);
        return m_format;
    }

    void stop() override
    {
        m_stopStarted.store(true, std::memory_order_release);
        if(m_stopDelayMs > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds{m_stopDelayMs});
        }
        if(m_stopsCompleted) {
            m_stopsCompleted->fetch_add(1, std::memory_order_acq_rel);
        }
    }

    void seek(uint64_t /*pos*/) override { }

    ReadResult readAudio(size_t /*bytes*/) override
    {
        if(abortToken().stop_requested()) {
            return ReadResult::errorResult(u"Aborted"_s);
        }
        if(m_emitted) {
            return ReadResult::endOfStream();
        }
        m_emitted            = true;
        constexpr int frames = 4800;
        std::vector<double> data(static_cast<size_t>(frames) * 2U, 0.25);
        AudioBuffer buf{reinterpret_cast<const uint8_t*>(data.data()), data.size() * sizeof(double), m_format, 0ULL};
        return ReadResult::data(std::move(buf));
    }

    AudioBuffer readBuffer(size_t bytes) override
    {
        auto res = readAudio(bytes);
        return res.status == ReadStatus::DecodedAudio ? std::move(res.buffer) : AudioBuffer{};
    }

protected:
    void interruptRead() override
    {
        if(m_stopStarted.load(std::memory_order_acquire) && m_abortAfterStop) {
            m_abortAfterStop->fetch_add(1, std::memory_order_acq_rel);
        }
    }

private:
    AudioFormat m_format;
    bool m_emitted{false};
    std::atomic<bool> m_stopStarted{false};
    std::shared_ptr<std::atomic<int>> m_stopsCompleted;
    std::shared_ptr<std::atomic<int>> m_abortAfterStop;
    int m_stopDelayMs{0};
};

TEST(DrAlbumGrouperTest, GroupsByScriptPatternDiscNumberAndFallsBackToDirectory)
{
    Track t1{u"/music/album_a/01.flac"_s};
    t1.setAlbumArtists({u"Example Artist"_s});
    t1.setDate(u"2020"_s);
    t1.setAlbum(u"Example Album"_s);
    t1.setDiscNumber(u"1"_s);

    Track t2{u"/music/album_a/02.flac"_s};
    t2.setAlbumArtists({u"Example Artist"_s});
    t2.setDate(u"2020"_s);
    t2.setAlbum(u"Example Album"_s);
    t2.setDiscNumber(u"1"_s);

    Track t3{u"/music/album_a/disc2/01.flac"_s};
    t3.setAlbumArtists({u"Example Artist"_s});
    t3.setDate(u"2020"_s);
    t3.setAlbum(u"Example Album"_s);
    t3.setDiscNumber(u"2"_s);

    // Untagged tracks in two different folders must fall back to parent directory path
    Track u1{u"/music/untagged_one/a.flac"_s};
    Track u2{u"/music/untagged_one/b.flac"_s};
    Track u3{u"/music/untagged_two/c.flac"_s};

    const auto groups
        = DrAlbumGrouper::groupTracks({t1, u1, t2, t3, u2, u3}, DrSettings::defaultAlbumGroupingPattern());

    ASSERT_EQ(groups.size(), 4U);
    EXPECT_EQ(groups[0].tracks.size(), 2U); // Album A Disc 1 (t1, t2)
    EXPECT_EQ(groups[0].album, u"Example Album"_s);
    EXPECT_EQ(groups[1].tracks.size(), 2U); // /music/untagged_one (u1, u2)
    EXPECT_EQ(groups[1].groupKey, u"/music/untagged_one"_s);
    EXPECT_EQ(groups[2].tracks.size(), 1U); // Album A Disc 2 (t3)
    EXPECT_EQ(groups[3].tracks.size(), 1U); // /music/untagged_two (u3)
    EXPECT_EQ(groups[3].groupKey, u"/music/untagged_two"_s);

    EXPECT_EQ(parseTrackNumber(u"01"_s), 1);
    EXPECT_EQ(parseTrackNumber(u"01/08"_s), 1);
    EXPECT_EQ(parseTrackNumber(u" 12 / 14 "_s), 12);
    EXPECT_EQ(parseTrackNumber(u""_s), 0);
    EXPECT_EQ(parseTrackNumber(u"abc"_s), 0);
}

TEST(DrTagWriterTest, EnforcesSafetyGuardsPreservesTagsAndReportsSummary)
{
    auto loader = std::make_shared<AudioLoader>();
    loader->addReader(u"WritableStub"_s,
                      []() { return std::make_unique<StubAudioReader>(QStringList{u"flac"_s, u"dsf"_s}, true); });
    loader->addReader(u"ReadOnlyStub"_s,
                      []() { return std::make_unique<StubAudioReader>(QStringList{u"ape"_s}, false); });

    Track writableFlac{u"/music/01.flac"_s};
    writableFlac.setTitle(u"Track One"_s);
    writableFlac.replaceExtraTag(u"CUSTOM_TAG"_s, u"keep-me"_s);

    Track writableDsf{u"/music/02.dsf"_s};
    writableDsf.setTitle(u"DSF Track"_s);

    Track externalCue{u"/music/image.flac"_s, 1};
    externalCue.setCuePath(u"/music/image.cue"_s);

    Track embeddedCue{u"/music/embedded.flac"_s, 1};
    embeddedCue.setCuePath(u"Embedded"_s);

    const QString archivePath = u"/music/album.zip"_s;
    const QString archiveUrl  = u"unpack://zip|%1|file://%2!01.flac"_s.arg(archivePath.size()).arg(archivePath);
    Track archived{archiveUrl};
    ASSERT_TRUE(archived.isInArchive());

    Track remote{u"https://example.com/stream.flac"_s};
    ASSERT_TRUE(remote.isRemote());

    Track readOnlyApe{u"/music/readonly.ape"_s};

    // Confirm AudioLoader::canWriteMetadata() alone returns true for CUE (the exact hazard we guard against)
    EXPECT_TRUE(loader->canWriteMetadata(externalCue));
    EXPECT_FALSE(isTrackTagWritable(externalCue, *loader));
    EXPECT_FALSE(isTrackTagWritable(embeddedCue, *loader));
    EXPECT_FALSE(isTrackTagWritable(archived, *loader));
    EXPECT_FALSE(isTrackTagWritable(remote, *loader));
    EXPECT_FALSE(isTrackTagWritable(readOnlyApe, *loader));
    EXPECT_TRUE(isTrackTagWritable(writableFlac, *loader));
    EXPECT_TRUE(isTrackTagWritable(writableDsf, *loader));

    ScannedAlbum album;
    album.album.officialDr = 13;
    const auto addTrack    = [&album](const Track& tr, int drVal, bool ok = true) {
        ScannedTrack st;
        st.track           = tr;
        st.dr.trackDrInt   = drVal;
        st.dr.trackDrFloat = static_cast<double>(drVal);
        st.ok              = ok;
        album.tracks.push_back(std::move(st));
    };

    addTrack(writableFlac, 12);
    addTrack(writableDsf, 14);
    addTrack(externalCue, 13);
    addTrack(embeddedCue, 13);
    addTrack(archived, 11);
    addTrack(remote, 10);
    addTrack(readOnlyApe, 12);

    const auto prepared = prepareTagUpdates({album}, *loader);
    ASSERT_EQ(prepared.tracksToWrite.size(), 2U);
    EXPECT_EQ(prepared.skippedCount, 5);

    const Track& outFlac = prepared.tracksToWrite[0];
    EXPECT_EQ(outFlac.filepath(), u"/music/01.flac"_s);
    EXPECT_EQ(outFlac.extraTag(u"DYNAMIC RANGE"_s), QStringList{u"12"_s});
    EXPECT_EQ(outFlac.extraTag(u"ALBUM DYNAMIC RANGE"_s), QStringList{u"13"_s});
    EXPECT_EQ(outFlac.extraTag(u"CUSTOM_TAG"_s), QStringList{u"keep-me"_s});
    EXPECT_EQ(outFlac.title(), u"Track One"_s);

    const Track& outDsf = prepared.tracksToWrite[1];
    EXPECT_EQ(outDsf.extraTag(u"DYNAMIC RANGE"_s), QStringList{u"14"_s});
    EXPECT_EQ(outDsf.extraTag(u"ALBUM DYNAMIC RANGE"_s), QStringList{u"13"_s});

    FakeMusicLibrary library;
    DrTagWriter writer{&library, loader};

    QEventLoop loop;
    DrTagWriteSummary captured;
    QObject::connect(&writer, &DrTagWriter::finished, &loop, [&](const DrTagWriteSummary& summary) {
        captured = summary;
        loop.quit();
    });

    writer.writeTags({album});
    loop.exec();

    EXPECT_EQ(library.writeCallCount, 1);
    EXPECT_EQ(library.lastWrittenTracks.size(), 2U);
    EXPECT_EQ(captured.updated, 2);
    EXPECT_EQ(captured.failed, 0);
    EXPECT_EQ(captured.skipped, 5);
    EXPECT_FALSE(captured.cancelled);
    EXPECT_EQ(captured.formatSummary(), u"Updated 2 files, skipped 5 (CUE/read-only)"_s);

    // Verify destructor safely cancels an in-flight WriteRequest without crashing
    library.deferCompletion = true;
    {
        DrTagWriter inflightWriter{&library, loader};
        inflightWriter.writeTags({album});
        EXPECT_TRUE(inflightWriter.isRunning());
    }
    EXPECT_TRUE(library.cancelCalled);
}

TEST(DrScannerCancellationTest, CooperativeStopTokenCancelsPromptlyAndJoinsCleanly)
{
    auto readsCompleted = std::make_shared<std::atomic<int>>(0);
    auto loader         = std::make_shared<AudioLoader>();
    loader->addDecoder(u"SlowTest"_s,
                       [readsCompleted]() { return std::make_unique<SlowSyntheticDecoder>(readsCompleted); });

    // Create a temporary dummy file so AudioLoader::openFileSource succeeds
    const QString dummyPath = QDir::temp().absoluteFilePath(u"fooyin_drmeter_cancel_test.slowtest"_s);
    {
        QFile f{dummyPath};
        ASSERT_TRUE(f.open(QIODevice::WriteOnly));
        f.write("dummy", 5);
    }

    TrackList tracks;
    for(int i = 0; i < 4; ++i) {
        Track t{dummyPath};
        t.setTitle(u"Slow Track %1"_s.arg(i + 1));
        tracks.push_back(std::move(t));
    }

    DrScanner scanner{loader};
    bool finishedEmitted  = false;
    bool cancelledEmitted = false;

    QEventLoop loop;
    QObject::connect(&scanner, &DrScanner::scanFinished, &loop, [&]() {
        finishedEmitted = true;
        loop.quit();
    });
    QObject::connect(&scanner, &DrScanner::scanCancelled, &loop, [&]() {
        cancelledEmitted = true;
        loop.quit();
    });

    const auto start = std::chrono::steady_clock::now();
    scanner.startScan(tracks, DrSettings::defaultAlbumGroupingPattern());

    QTimer::singleShot(30, &scanner, &DrScanner::cancel);
    QTimer::singleShot(5000, &loop, &QEventLoop::quit);
    loop.exec();
    scanner.waitForFinished();

    const auto elapsedMs
        = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();

    QFile::remove(dummyPath);

    EXPECT_TRUE(cancelledEmitted);
    EXPECT_FALSE(finishedEmitted);
    EXPECT_LT(elapsedMs, 2000);
}

TEST(DrTagWriterTest, InvalidWriteRequestReportsFailureAndCancelSuppressesLateFinished)
{
    auto loader = std::make_shared<AudioLoader>();
    loader->addReader(u"WritableStub"_s,
                      []() { return std::make_unique<StubAudioReader>(QStringList{u"flac"_s}, true); });

    Track writableFlac{u"/music/01.flac"_s};
    writableFlac.setTitle(u"Track One"_s);

    Track externalCue{u"/music/image.flac"_s, 1};
    externalCue.setCuePath(u"/music/image.cue"_s);

    ScannedAlbum album;
    album.album.officialDr = 12;
    {
        ScannedTrack st1;
        st1.track         = writableFlac;
        st1.dr.trackDrInt = 12;
        st1.ok            = true;
        album.tracks.push_back(st1);

        ScannedTrack st2;
        st2.track         = externalCue;
        st2.dr.trackDrInt = 12;
        st2.ok            = true;
        album.tracks.push_back(st2);
    }

    // Bug #1: invalid QFuture from writeTrackMetadata must report failure, never false success.
    FakeMusicLibrary library;
    library.returnInvalidRequest = true;

    DrTagWriter writer{&library, loader};
    int finishedCount{0};
    DrTagWriteSummary captured;
    QObject::connect(&writer, &DrTagWriter::finished, [&](const DrTagWriteSummary& summary) {
        ++finishedCount;
        captured = summary;
    });

    writer.writeTags({album});
    EXPECT_EQ(finishedCount, 1);
    EXPECT_EQ(captured.updated, 0);
    EXPECT_EQ(captured.failed, 1);
    EXPECT_EQ(captured.skipped, 1);
    EXPECT_FALSE(captured.cancelled);

    // Bug #2a: cancel() with empty WriteRequest::cancel callback (like Fooyin's completedWriteRequest)
    // must disconnect m_watcher so the queued completion cannot trigger finished() later.
    library.returnInvalidRequest = false;
    library.omitCancelCallback   = true;
    finishedCount                = 0;

    writer.writeTags({album});
    EXPECT_TRUE(writer.isRunning());
    writer.cancel();
    EXPECT_FALSE(writer.isRunning());

    QEventLoop waitLoop;
    QTimer::singleShot(40, &waitLoop, &QEventLoop::quit);
    waitLoop.exec();
    EXPECT_EQ(finishedCount, 0);

    // Bug #2b: promise finishes with WriteState::Completed while QFutureWatcher::finished is queued,
    // then cancel() is called before the event loop dispatches it -> finished() must NOT fire.
    library.omitCancelCallback = false;
    library.deferCompletion    = true;
    finishedCount              = 0;

    writer.writeTags({album});
    ASSERT_NE(library.pendingPromise, nullptr);
    library.pendingPromise->addResult(WriteResult{.state = WriteState::Completed, .succeeded = 1, .failed = 0});
    library.pendingPromise->finish();
    writer.cancel();

    QEventLoop raceLoop;
    QTimer::singleShot(20, &raceLoop, &QEventLoop::quit);
    raceLoop.exec();
    EXPECT_EQ(finishedCount, 0);
}

TEST(DrScannerMetadataTest, OmitsBitDepthForLossyAndUnprobedFloatTracks)
{
    const AudioFormat f32Format{SampleFormat::F32, 44100, 2};
    const AudioFormat s16Format{SampleFormat::S16, 44100, 2};
    const AudioFormat s24Format{SampleFormat::S24In32, 96000, 2};

    // MP3 / Opus / Ogg Vorbis from TagLibReader: bitDepth == -1, encoding == "Lossy", decoded as F32.
    Track mp3Track{u"/music/01.mp3"_s};
    mp3Track.setTitle(u"Lossy Track"_s);
    mp3Track.setTrackNumber(u"1"_s);
    mp3Track.setCodec(u"MP3"_s);
    mp3Track.setEncoding(u"Lossy"_s);
    mp3Track.setBitrate(320);

    const TrackMetadata mp3Meta = buildTrackMetadata(mp3Track, f32Format);
    EXPECT_EQ(mp3Meta.bitsPerSample, 0);

    AlbumDrResult lossyAlbum;
    lossyAlbum.albumArtist = "Artist";
    lossyAlbum.album       = "MP3 Album";
    lossyAlbum.officialDr  = 11;
    TrackDrResult mp3Dr;
    mp3Dr.trackDrInt  = 11;
    mp3Dr.peakDb      = -0.5;
    mp3Dr.rmsDb       = -14.0;
    mp3Dr.totalFrames = 44100ULL * 180ULL;
    mp3Dr.metadata    = mp3Meta;
    lossyAlbum.tracks.push_back(mp3Dr);

    const std::string log = DrLogFormatter::formatAlbumLog(lossyAlbum, {.logDate = "2026-11-26 12:00:00"});
    EXPECT_EQ(log.find("Bits per sample:"), std::string::npos);

    // Lossy track probed by FFmpegReader (bitDepth populated as 32, encoding == "Lossy") -> still 0.
    Track ffmpegLossy{u"/music/02.opus"_s};
    ffmpegLossy.setEncoding(u"Lossy"_s);
    ffmpegLossy.setBitDepth(32);
    EXPECT_EQ(buildTrackMetadata(ffmpegLossy, f32Format).bitsPerSample, 0);

    // Un-probed F32 track (bitDepth == -1, empty encoding) -> 0 (never fabricate 32-bit).
    Track unprobedFloat{u"/music/03.ogg"_s};
    EXPECT_EQ(buildTrackMetadata(unprobedFloat, f32Format).bitsPerSample, 0);

    // Lossless FLAC (16-bit), DSF (1-bit), 32-bit float WAV, and un-probed 24-bit integer PCM.
    Track flacTrack{u"/music/04.flac"_s};
    flacTrack.setEncoding(u"Lossless"_s);
    flacTrack.setBitDepth(16);
    EXPECT_EQ(buildTrackMetadata(flacTrack, s16Format).bitsPerSample, 16);

    Track dsfTrack{u"/music/05.dsf"_s};
    dsfTrack.setEncoding(u"Lossless"_s);
    dsfTrack.setBitDepth(1);
    EXPECT_EQ(buildTrackMetadata(dsfTrack, f32Format).bitsPerSample, 1);

    Track floatWavTrack{u"/music/06.wav"_s};
    floatWavTrack.setEncoding(u"Lossless"_s);
    floatWavTrack.setBitDepth(32);
    EXPECT_EQ(buildTrackMetadata(floatWavTrack, f32Format).bitsPerSample, 32);

    Track unprobedInt24{u"/music/07.flac"_s};
    EXPECT_EQ(buildTrackMetadata(unprobedInt24, s24Format).bitsPerSample, 24);
}

TEST(DrScannerCancellationTest, QueuedFinishAfterCancelEmitsCancelledNotFinished)
{
    auto stopsCompleted = std::make_shared<std::atomic<int>>(0);
    auto abortAfterStop = std::make_shared<std::atomic<int>>(0);
    auto loader         = std::make_shared<AudioLoader>();
    loader->addDecoder(u"FastTest"_s, [stopsCompleted, abortAfterStop]() {
        return std::make_unique<FastSyntheticDecoder>(stopsCompleted, abortAfterStop);
    });

    const QString dummyPath = QDir::temp().absoluteFilePath(u"fooyin_drmeter_race_test.fasttest"_s);
    {
        QFile f{dummyPath};
        ASSERT_TRUE(f.open(QIODevice::WriteOnly));
        f.write("dummy", 5);
    }

    TrackList tracks;
    for(int i = 0; i < 2; ++i) {
        Track t{dummyPath};
        t.setTitle(u"Fast Track %1"_s.arg(i + 1));
        tracks.push_back(std::move(t));
    }

    DrScanner scanner{loader};
    bool finishedEmitted  = false;
    bool cancelledEmitted = false;

    QEventLoop loop;
    QObject::connect(&scanner, &DrScanner::scanFinished, &loop, [&]() {
        finishedEmitted = true;
        loop.quit();
    });
    QObject::connect(&scanner, &DrScanner::scanCancelled, &loop, [&]() {
        cancelledEmitted = true;
        loop.quit();
    });

    scanner.startScan(tracks, DrSettings::defaultAlbumGroupingPattern());

    // Wait while the main event loop is NOT spinning so DrWorker::finishScan() completes and queues
    // DrWorker::scanFinished onto the main thread before we invoke scanner.cancel().
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{2};
    while(stopsCompleted->load(std::memory_order_acquire) < 2 && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
    ASSERT_EQ(stopsCompleted->load(std::memory_order_acquire), 2);
    std::this_thread::sleep_for(std::chrono::milliseconds{50});

    scanner.cancel();

    QTimer::singleShot(2000, &loop, &QEventLoop::quit);
    loop.exec();
    scanner.waitForFinished();

    QFile::remove(dummyPath);

    EXPECT_TRUE(cancelledEmitted);
    EXPECT_FALSE(finishedEmitted);
    EXPECT_EQ(abortAfterStop->load(std::memory_order_acquire), 0);
}

TEST(DrScannerCancellationTest, StopCallbackNeverInvokesRequestAbortAfterStopStarts)
{
    auto stopsCompleted = std::make_shared<std::atomic<int>>(0);
    auto abortAfterStop = std::make_shared<std::atomic<int>>(0);
    auto loader         = std::make_shared<AudioLoader>();
    loader->addDecoder(u"FastTest"_s, [stopsCompleted, abortAfterStop]() {
        return std::make_unique<FastSyntheticDecoder>(stopsCompleted, abortAfterStop, /*stopDelayMs=*/3);
    });

    const QString dummyPath = QDir::temp().absoluteFilePath(u"fooyin_drmeter_stop_order_test.fasttest"_s);
    {
        QFile f{dummyPath};
        ASSERT_TRUE(f.open(QIODevice::WriteOnly));
        f.write("dummy", 5);
    }

    TrackList tracks;
    for(int i = 0; i < 4; ++i) {
        Track t{dummyPath};
        t.setTitle(u"Stop Order Track %1"_s.arg(i + 1));
        tracks.push_back(std::move(t));
    }

    for(int iter = 0; iter < 15; ++iter) {
        DrScanner scanner{loader};
        scanner.startScan(tracks, DrSettings::defaultAlbumGroupingPattern());
        std::this_thread::sleep_for(std::chrono::milliseconds{iter % 5});
        scanner.cancel();
        scanner.waitForFinished();
    }

    QFile::remove(dummyPath);

    EXPECT_EQ(abortAfterStop->load(std::memory_order_acquire), 0);
}

class ZeroFrameEofDecoder final : public AudioDecoder
{
public:
    [[nodiscard]] QStringList extensions() const override
    {
        return {u"zerotest"_s};
    }
    [[nodiscard]] bool isSeekable() const override
    {
        return false;
    }

    std::optional<AudioFormat> init(const AudioSource& /*source*/, const Track& /*track*/,
                                    DecoderOptions /*options*/) override
    {
        return AudioFormat{SampleFormat::F64, 44100, 2};
    }

    void stop() override { }
    void seek(uint64_t /*pos*/) override { }

    ReadResult readAudio(size_t /*bytes*/) override
    {
        return ReadResult::endOfStream();
    }

    AudioBuffer readBuffer(size_t /*bytes*/) override
    {
        return {};
    }
};

class PulseSyntheticDecoder final : public AudioDecoder
{
public:
    explicit PulseSyntheticDecoder(int totalFrames, QString ext = u"pulsetest"_s)
        : m_totalFrames{totalFrames}
        , m_ext{std::move(ext)}
    { }

    [[nodiscard]] QStringList extensions() const override
    {
        return {m_ext};
    }
    [[nodiscard]] bool isSeekable() const override
    {
        return false;
    }

    std::optional<AudioFormat> init(const AudioSource& /*source*/, const Track& /*track*/,
                                    DecoderOptions /*options*/) override
    {
        m_format  = AudioFormat{SampleFormat::F64, 44100, 2};
        m_emitted = false;
        return m_format;
    }

    void stop() override { }
    void seek(uint64_t /*pos*/) override { }

    ReadResult readAudio(size_t /*bytes*/) override
    {
        if(m_emitted) {
            return ReadResult::endOfStream();
        }
        m_emitted = true;

        std::vector<double> data(static_cast<size_t>(m_totalFrames) * 2U, 0.0);
        if(m_totalFrames > 0) {
            data[0] = 1.0;
            data[1] = 1.0;
        }
        AudioBuffer buf{reinterpret_cast<const uint8_t*>(data.data()), data.size() * sizeof(double), m_format, 0ULL};
        return ReadResult::data(std::move(buf));
    }

    AudioBuffer readBuffer(size_t bytes) override
    {
        auto res = readAudio(bytes);
        return res.status == ReadStatus::DecodedAudio ? std::move(res.buffer) : AudioBuffer{};
    }

private:
    int m_totalFrames{32};
    QString m_ext;
    AudioFormat m_format;
    bool m_emitted{false};
};

class ErrorSyntheticDecoder final : public AudioDecoder
{
public:
    [[nodiscard]] QStringList extensions() const override
    {
        return {u"errtest"_s};
    }
    [[nodiscard]] bool isSeekable() const override
    {
        return false;
    }

    std::optional<AudioFormat> init(const AudioSource& /*source*/, const Track& /*track*/,
                                    DecoderOptions /*options*/) override
    {
        return AudioFormat{SampleFormat::F64, 44100, 2};
    }

    void stop() override { }
    void seek(uint64_t /*pos*/) override { }

    ReadResult readAudio(size_t /*bytes*/) override
    {
        return ReadResult::errorResult(u"Simulated corrupt frame"_s);
    }

    AudioBuffer readBuffer(size_t /*bytes*/) override
    {
        return {};
    }
};

class CueStreamDecoder final : public AudioDecoder
{
public:
    explicit CueStreamDecoder(std::shared_ptr<std::atomic<int>> seekCalls)
        : m_seekCalls{std::move(seekCalls)}
    { }

    [[nodiscard]] QStringList extensions() const override
    {
        return {u"cuetest"_s};
    }
    [[nodiscard]] bool isSeekable() const override
    {
        return true;
    }

    std::optional<AudioFormat> init(const AudioSource& /*source*/, const Track& /*track*/,
                                    DecoderOptions /*options*/) override
    {
        m_format = AudioFormat{SampleFormat::F64, 44100, 2};
        m_pos    = 0;
        return m_format;
    }

    void stop() override { }
    void seek(uint64_t /*pos*/) override
    {
        if(m_seekCalls) {
            m_seekCalls->fetch_add(1, std::memory_order_relaxed);
        }
    }

    ReadResult readAudio(size_t /*bytes*/) override
    {
        constexpr int FramesPerSector = 588; // 44100 / 75
        constexpr int TotalFrames     = 10 * FramesPerSector;
        constexpr int ChunkFrames     = 1000; // intentionally crosses sector boundaries

        if(m_pos >= TotalFrames) {
            return ReadResult::endOfStream();
        }

        const int count = std::min(ChunkFrames, TotalFrames - m_pos);
        std::vector<double> data(static_cast<size_t>(count) * 2U, 0.0);

        for(int i = 0; i < count; ++i) {
            const int frame = m_pos + i;
            double sample   = 0.0;
            if(frame < 2 * FramesPerSector || frame >= 6 * FramesPerSector) {
                sample = 1.0;
            }
            else if(frame == 2 * FramesPerSector) {
                sample = 0.5;
            }
            data[static_cast<size_t>(i) * 2U]      = sample;
            data[static_cast<size_t>(i) * 2U + 1U] = sample;
        }

        const auto startMs = static_cast<uint64_t>(m_pos) * 1000ULL / 44100ULL;
        m_pos += count;

        AudioBuffer buf{reinterpret_cast<const uint8_t*>(data.data()), data.size() * sizeof(double), m_format, startMs};
        return ReadResult::data(std::move(buf));
    }

    AudioBuffer readBuffer(size_t bytes) override
    {
        auto res = readAudio(bytes);
        return res.status == ReadStatus::DecodedAudio ? std::move(res.buffer) : AudioBuffer{};
    }

private:
    AudioFormat m_format;
    int m_pos{0};
    std::shared_ptr<std::atomic<int>> m_seekCalls;
};

class SeekableSegmentDecoder final : public AudioDecoder
{
public:
    SeekableSegmentDecoder(bool seekable, QString ext, std::shared_ptr<std::atomic<int>> seekCalls,
                           std::shared_ptr<std::atomic<uint64_t>> lastSeekMs)
        : m_seekable{seekable}
        , m_ext{std::move(ext)}
        , m_seekCalls{std::move(seekCalls)}
        , m_lastSeekMs{std::move(lastSeekMs)}
    { }

    [[nodiscard]] QStringList extensions() const override
    {
        return {m_ext};
    }
    [[nodiscard]] bool isSeekable() const override
    {
        return m_seekable;
    }

    std::optional<AudioFormat> init(const AudioSource& /*source*/, const Track& /*track*/,
                                    DecoderOptions /*options*/) override
    {
        m_format = AudioFormat{SampleFormat::F64, 48000, 2};
        m_pos    = 0;
        return m_format;
    }

    void stop() override { }
    void seek(uint64_t pos) override
    {
        if(m_seekCalls) {
            m_seekCalls->fetch_add(1, std::memory_order_relaxed);
        }
        if(m_lastSeekMs) {
            m_lastSeekMs->store(pos, std::memory_order_relaxed);
        }
        m_pos = 0;
    }

    ReadResult readAudio(size_t /*bytes*/) override
    {
        constexpr int TotalFrames = 48000; // 1000 ms offered
        constexpr int ChunkFrames = 5000;  // crosses 24000-frame (500 ms) duration boundary

        if(m_pos >= TotalFrames) {
            return ReadResult::endOfStream();
        }

        const int count = std::min(ChunkFrames, TotalFrames - m_pos);
        std::vector<double> data(static_cast<size_t>(count) * 2U, 0.0);

        for(int i = 0; i < count; ++i) {
            const int frame = m_pos + i;
            double sample   = 0.0;
            if(frame == 0) {
                sample = 0.25;
            }
            else if(frame >= 24000) {
                sample = 1.0;
            }
            data[static_cast<size_t>(i) * 2U]      = sample;
            data[static_cast<size_t>(i) * 2U + 1U] = sample;
        }

        const auto startMs = static_cast<uint64_t>(m_pos) * 1000ULL / 48000ULL;
        m_pos += count;

        AudioBuffer buf{reinterpret_cast<const uint8_t*>(data.data()), data.size() * sizeof(double), m_format, startMs};
        return ReadResult::data(std::move(buf));
    }

    AudioBuffer readBuffer(size_t bytes) override
    {
        auto res = readAudio(bytes);
        return res.status == ReadStatus::DecodedAudio ? std::move(res.buffer) : AudioBuffer{};
    }

private:
    bool m_seekable{true};
    QString m_ext;
    AudioFormat m_format;
    int m_pos{0};
    std::shared_ptr<std::atomic<int>> m_seekCalls;
    std::shared_ptr<std::atomic<uint64_t>> m_lastSeekMs;
};

DrScanSummary runScanSync(const std::shared_ptr<AudioLoader>& loader, const TrackList& tracks)
{
    DrScanner scanner{loader};
    DrScanSummary captured;
    bool gotFinished = false;

    QEventLoop loop;
    QObject::connect(&scanner, &DrScanner::scanFinished, &loop, [&](const DrScanSummary& summary) {
        captured    = summary;
        gotFinished = true;
        loop.quit();
    });
    QObject::connect(&scanner, &DrScanner::scanCancelled, &loop, &QEventLoop::quit);

    scanner.startScan(tracks, DrSettings::defaultAlbumGroupingPattern());
    QTimer::singleShot(5000, &loop, &QEventLoop::quit);
    loop.exec();

    EXPECT_TRUE(gotFinished);
    return captured;
}

TEST(DrScannerBehaviorTest, ZeroFrameEofFailsAndIsExcludedFromOfficialDr)
{
    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());

    const QString pulsePath = tempDir.filePath(u"01.pulsetest"_s);
    const QString zeroPath  = tempDir.filePath(u"02.zerotest"_s);
    for(const QString& path : {pulsePath, zeroPath}) {
        QFile f{path};
        ASSERT_TRUE(f.open(QIODevice::WriteOnly));
        f.write("dummy", 5);
    }

    auto loader = std::make_shared<AudioLoader>();
    // 32 frames with single full-scale pulse -> DR = 10*log10(32/2) = 12.04 dB -> DR12
    loader->addDecoder(u"PulseTest"_s, []() { return std::make_unique<PulseSyntheticDecoder>(32); });
    loader->addDecoder(u"ZeroTest"_s, []() { return std::make_unique<ZeroFrameEofDecoder>(); });

    Track t1{pulsePath};
    t1.setAlbumArtists({u"Test Artist"_s});
    t1.setAlbum(u"Test Album"_s);
    t1.setTitle(u"Valid Pulse"_s);
    t1.setTrackNumber(u"1"_s);

    Track t2{zeroPath};
    t2.setAlbumArtists({u"Test Artist"_s});
    t2.setAlbum(u"Test Album"_s);
    t2.setTitle(u"Empty EOF"_s);
    t2.setTrackNumber(u"2"_s);

    const DrScanSummary summary = runScanSync(loader, {t1, t2});
    ASSERT_EQ(summary.albums.size(), 1U);

    const auto& album = summary.albums.front();
    ASSERT_EQ(album.tracks.size(), 2U);
    EXPECT_TRUE(album.tracks[0].ok);
    EXPECT_EQ(album.tracks[0].dr.trackDrInt, 12);

    EXPECT_FALSE(album.tracks[1].ok);
    EXPECT_EQ(album.tracks[1].error, u"No audio frames decoded"_s);

    ASSERT_EQ(album.album.tracks.size(), 1U);
    EXPECT_EQ(album.album.officialDr, 12);
}

TEST(DrScannerBehaviorTest, SlicesCueSectorBoundariesSampleAccurately)
{
    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());

    const QString cueAudioPath = tempDir.filePath(u"image.cuetest"_s);
    {
        QFile f{cueAudioPath};
        ASSERT_TRUE(f.open(QIODevice::WriteOnly));
        f.write("dummy", 5);
    }

    auto seekCalls = std::make_shared<std::atomic<int>>(0);
    auto loader    = std::make_shared<AudioLoader>();
    loader->addDecoder(u"CueTest"_s, [seekCalls]() { return std::make_unique<CueStreamDecoder>(seekCalls); });

    Track validCue{cueAudioPath, 1};
    validCue.setCuePath(tempDir.filePath(u"image.cue"_s));
    validCue.setAlbumArtists({u"Cue Artist"_s});
    validCue.setAlbum(u"Cue Album"_s);
    validCue.setTitle(u"Subsong One"_s);
    validCue.setTrackNumber(u"1"_s);
    validCue.setOffset(2667);
    validCue.setExtraProperty(QString::fromLatin1(Constants::CueIndex01Sector), u"2"_s);
    validCue.setExtraProperty(QString::fromLatin1(Constants::CueEndSector), u"6"_s);

    Track invalidCue{cueAudioPath, 2};
    invalidCue.setCuePath(tempDir.filePath(u"image.cue"_s));
    invalidCue.setAlbumArtists({u"Cue Artist"_s});
    invalidCue.setAlbum(u"Cue Album"_s);
    invalidCue.setTitle(u"Invalid Bounds"_s);
    invalidCue.setTrackNumber(u"2"_s);
    invalidCue.setExtraProperty(QString::fromLatin1(Constants::CueIndex01Sector), u"6"_s);
    invalidCue.setExtraProperty(QString::fromLatin1(Constants::CueEndSector), u"6"_s);

    const DrScanSummary summary = runScanSync(loader, {validCue, invalidCue});
    ASSERT_EQ(summary.albums.size(), 1U);

    const auto& album = summary.albums.front();
    ASSERT_EQ(album.tracks.size(), 2U);

    EXPECT_TRUE(album.tracks[0].ok);
    EXPECT_EQ(album.tracks[0].dr.totalFrames, 2352ULL); // 4 sectors * 588 frames/sector
    EXPECT_NEAR(album.tracks[0].dr.peakDb, -6.0206, 0.01);
    EXPECT_EQ(album.tracks[0].dr.trackDrInt, 31);
    EXPECT_EQ(seekCalls->load(std::memory_order_relaxed), 0);

    EXPECT_FALSE(album.tracks[1].ok);
    EXPECT_EQ(album.tracks[1].error, u"Invalid CUE track boundaries"_s);
}

TEST(DrScannerBehaviorTest, SeeksAndTrimsBoundedOffsetDurationSegment)
{
    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());

    const QString seekPath    = tempDir.filePath(u"chapter.seektest"_s);
    const QString nonSeekPath = tempDir.filePath(u"chapter.nonseektest"_s);
    for(const QString& path : {seekPath, nonSeekPath}) {
        QFile f{path};
        ASSERT_TRUE(f.open(QIODevice::WriteOnly));
        f.write("dummy", 5);
    }

    auto seekCalls  = std::make_shared<std::atomic<int>>(0);
    auto lastSeekMs = std::make_shared<std::atomic<uint64_t>>(0);

    auto loader = std::make_shared<AudioLoader>();
    loader->addDecoder(u"SeekTest"_s, [seekCalls, lastSeekMs]() {
        return std::make_unique<SeekableSegmentDecoder>(true, u"seektest"_s, seekCalls, lastSeekMs);
    });
    loader->addDecoder(u"NonSeekTest"_s, []() {
        return std::make_unique<SeekableSegmentDecoder>(false, u"nonseektest"_s, nullptr, nullptr);
    });

    Track seekTrack{seekPath, 1};
    seekTrack.setCuePath(tempDir.filePath(u"chapters.cue"_s));
    seekTrack.setAlbumArtists({u"Chapter Artist"_s});
    seekTrack.setAlbum(u"Chapter Album"_s);
    seekTrack.setTitle(u"Bounded Chapter"_s);
    seekTrack.setOffset(2000);
    seekTrack.setDuration(500);
    ASSERT_TRUE(seekTrack.isBoundedSegment());

    Track nonSeekTrack{nonSeekPath, 2};
    nonSeekTrack.setCuePath(tempDir.filePath(u"chapters.cue"_s));
    nonSeekTrack.setAlbumArtists({u"Chapter Artist"_s});
    nonSeekTrack.setAlbum(u"Chapter Album"_s);
    nonSeekTrack.setTitle(u"Unseekable Chapter"_s);
    nonSeekTrack.setOffset(2000);
    nonSeekTrack.setDuration(500);

    const DrScanSummary summary = runScanSync(loader, {seekTrack, nonSeekTrack});
    ASSERT_EQ(summary.albums.size(), 1U);

    const auto& album = summary.albums.front();
    ASSERT_EQ(album.tracks.size(), 2U);

    EXPECT_TRUE(album.tracks[0].ok);
    EXPECT_EQ(seekCalls->load(std::memory_order_relaxed), 1);
    EXPECT_EQ(lastSeekMs->load(std::memory_order_relaxed), 2000ULL);
    EXPECT_EQ(album.tracks[0].dr.totalFrames, 24000ULL); // 500 ms @ 48000 Hz
    EXPECT_NEAR(album.tracks[0].dr.peakDb, -12.0412, 0.01);

    EXPECT_FALSE(album.tracks[1].ok);
    EXPECT_EQ(album.tracks[1].error, u"Decoder cannot seek to track segment"_s);
}

TEST(DrScannerBehaviorTest, PartialFailureAlbumAveragesOnlyValidTracks)
{
    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());

    const QString track1Path = tempDir.filePath(u"01.pulse10"_s);
    const QString track2Path = tempDir.filePath(u"02.errtest"_s);
    const QString track3Path = tempDir.filePath(u"03.pulse14"_s);
    for(const QString& path : {track1Path, track2Path, track3Path}) {
        QFile f{path};
        ASSERT_TRUE(f.open(QIODevice::WriteOnly));
        f.write("dummy", 5);
    }

    auto loader = std::make_shared<AudioLoader>();
    // 20 frames -> DR = 10*log10(20/2) = 10.00 dB; 50 frames -> DR = 10*log10(50/2) = 13.98 dB
    loader->addDecoder(u"Pulse10"_s, []() { return std::make_unique<PulseSyntheticDecoder>(20, u"pulse10"_s); });
    loader->addDecoder(u"ErrTest"_s, []() { return std::make_unique<ErrorSyntheticDecoder>(); });
    loader->addDecoder(u"Pulse14"_s, []() { return std::make_unique<PulseSyntheticDecoder>(50, u"pulse14"_s); });

    Track t1{track1Path};
    t1.setAlbumArtists({u"Partial Artist"_s});
    t1.setAlbum(u"Partial Album"_s);
    t1.setTitle(u"First Good"_s);
    t1.setTrackNumber(u"1"_s);

    Track t2{track2Path};
    t2.setAlbumArtists({u"Partial Artist"_s});
    t2.setAlbum(u"Partial Album"_s);
    t2.setTitle(u"Broken Middle"_s);
    t2.setTrackNumber(u"2"_s);

    Track t3{track3Path};
    t3.setAlbumArtists({u"Partial Artist"_s});
    t3.setAlbum(u"Partial Album"_s);
    t3.setTitle(u"Last Good"_s);
    t3.setTrackNumber(u"3"_s);

    const DrScanSummary summary = runScanSync(loader, {t1, t2, t3});
    ASSERT_EQ(summary.albums.size(), 1U);

    const auto& album = summary.albums.front();
    ASSERT_EQ(album.tracks.size(), 3U);
    EXPECT_TRUE(album.tracks[0].ok);
    EXPECT_EQ(album.tracks[0].dr.trackDrInt, 10);
    EXPECT_FALSE(album.tracks[1].ok);
    EXPECT_EQ(album.tracks[1].error, u"Simulated corrupt frame"_s);
    EXPECT_TRUE(album.tracks[2].ok);
    EXPECT_EQ(album.tracks[2].dr.trackDrInt, 14);

    ASSERT_EQ(album.album.tracks.size(), 2U);
    EXPECT_EQ(album.album.officialDr, 12);
}

TEST(DrScannerMetadataTest, ChannelLayoutFromAudioFormatMapsPositionsAndLfeFlags)
{
    using P = AudioFormat::ChannelPosition;

    EXPECT_TRUE(channelLayoutFromAudioFormat(AudioFormat{}).empty());

    // Fallback default layout when AudioFormat has no explicit channel layout
    const AudioFormat stereoDefault{SampleFormat::S16, 44100, 2};
    const auto stereoLayout = channelLayoutFromAudioFormat(stereoDefault);
    ASSERT_EQ(stereoLayout.size(), 2U);
    EXPECT_EQ(stereoLayout[0].label, "FL");
    EXPECT_FALSE(stereoLayout[0].isLfe);
    EXPECT_EQ(stereoLayout[1].label, "FR");
    EXPECT_FALSE(stereoLayout[1].isLfe);

    // Explicit layout containing both LFE and LFE2 plus UnknownPosition
    AudioFormat customFmt{SampleFormat::F64, 48000, 8};
    ASSERT_TRUE(customFmt.setChannelLayoutAndCount({
        P::FrontLeft,
        P::FrontRight,
        P::FrontCenter,
        P::LFE,
        P::LFE2,
        P::SideLeft,
        P::SideRight,
        P::UnknownPosition,
    }));

    const auto layout = channelLayoutFromAudioFormat(customFmt);
    ASSERT_EQ(layout.size(), 8U);

    EXPECT_EQ(layout[0].label, "FL");
    EXPECT_FALSE(layout[0].isLfe);
    EXPECT_EQ(layout[1].label, "FR");
    EXPECT_FALSE(layout[1].isLfe);
    EXPECT_EQ(layout[2].label, "FC");
    EXPECT_FALSE(layout[2].isLfe);
    EXPECT_EQ(layout[3].label, "LFE");
    EXPECT_TRUE(layout[3].isLfe);
    EXPECT_EQ(layout[4].label, "LFE2");
    EXPECT_TRUE(layout[4].isLfe);
    EXPECT_EQ(layout[5].label, "SL");
    EXPECT_FALSE(layout[5].isLfe);
    EXPECT_EQ(layout[6].label, "SR");
    EXPECT_FALSE(layout[6].isLfe);
    EXPECT_EQ(layout[7].label, "Ch8");
    EXPECT_FALSE(layout[7].isLfe);
}

TEST(DrSettingsTest, DefaultsEmptyFallbacksPersistenceAndSingleTrackAlbumDrToggle)
{
    // Standalone (no SettingsManager) defaults and empty/whitespace fallbacks
    DrSettings standalone;
    EXPECT_EQ(standalone.albumGroupingPattern(), DrSettings::defaultAlbumGroupingPattern());
    EXPECT_EQ(standalone.logFileName(), DrSettings::defaultLogFileName());
    EXPECT_TRUE(standalone.writeAlbumDrForSingleTrack());

    standalone.setAlbumGroupingPattern(u"  %album%  "_s);
    EXPECT_EQ(standalone.albumGroupingPattern(), u"%album%"_s);
    standalone.setAlbumGroupingPattern(u"   "_s);
    EXPECT_EQ(standalone.albumGroupingPattern(), DrSettings::defaultAlbumGroupingPattern());

    standalone.setLogFileName(u" custom_dr.txt "_s);
    EXPECT_EQ(standalone.logFileName(), u"custom_dr.txt"_s);
    standalone.setLogFileName(u""_s);
    EXPECT_EQ(standalone.logFileName(), DrSettings::defaultLogFileName());

    standalone.setWriteAlbumDrForSingleTrack(false);
    EXPECT_FALSE(standalone.writeAlbumDrForSingleTrack());

    // Persistence and empty-value fallback through Fooyin's SettingsManager
    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());
    SettingsManager manager{tempDir.filePath(u"settings.conf"_s)};

    DrSettings persisted{&manager};
    EXPECT_EQ(persisted.albumGroupingPattern(), DrSettings::defaultAlbumGroupingPattern());
    EXPECT_EQ(persisted.logFileName(), DrSettings::defaultLogFileName());
    EXPECT_TRUE(persisted.writeAlbumDrForSingleTrack());

    persisted.setAlbumGroupingPattern(u"%artist% - %album%"_s);
    persisted.setLogFileName(u"dr_report.txt"_s);
    persisted.setWriteAlbumDrForSingleTrack(false);

    const DrSettings reloaded{&manager};
    EXPECT_EQ(reloaded.albumGroupingPattern(), u"%artist% - %album%"_s);
    EXPECT_EQ(reloaded.logFileName(), u"dr_report.txt"_s);
    EXPECT_FALSE(reloaded.writeAlbumDrForSingleTrack());

    // Empty/whitespace values stored directly in config must fall back to defaults
    manager.fileSet(QLatin1StringView{SettingKeys::AlbumGroupingPattern}, u"   "_s);
    manager.fileSet(QLatin1StringView{SettingKeys::LogFileName}, u""_s);
    const DrSettings fallback{&manager};
    EXPECT_EQ(fallback.albumGroupingPattern(), DrSettings::defaultAlbumGroupingPattern());
    EXPECT_EQ(fallback.logFileName(), DrSettings::defaultLogFileName());

    // Verify writeAlbumDrForSingleTrack = false suppresses ALBUM DYNAMIC RANGE only on 1-track albums
    auto loader = std::make_shared<AudioLoader>();
    loader->addReader(u"WritableStub"_s,
                      []() { return std::make_unique<StubAudioReader>(QStringList{u"flac"_s}, true); });

    ScannedAlbum singleTrackAlbum;
    singleTrackAlbum.album.officialDr = 13;
    {
        ScannedTrack st;
        st.track         = Track{u"/music/single.flac"_s};
        st.dr.trackDrInt = 13;
        st.ok            = true;
        singleTrackAlbum.tracks.push_back(st);
        singleTrackAlbum.album.tracks.push_back(st.dr);
    }

    const auto singlePrepared = prepareTagUpdates({singleTrackAlbum}, *loader, reloaded);
    ASSERT_EQ(singlePrepared.tracksToWrite.size(), 1U);
    EXPECT_EQ(singlePrepared.tracksToWrite[0].extraTag(u"DYNAMIC RANGE"_s), QStringList{u"13"_s});
    EXPECT_TRUE(singlePrepared.tracksToWrite[0].extraTag(u"ALBUM DYNAMIC RANGE"_s).isEmpty());

    ScannedAlbum multiTrackAlbum = singleTrackAlbum;
    {
        ScannedTrack st2;
        st2.track         = Track{u"/music/second.flac"_s};
        st2.dr.trackDrInt = 11;
        st2.ok            = true;
        multiTrackAlbum.tracks.push_back(st2);
        multiTrackAlbum.album.tracks.push_back(st2.dr);
        multiTrackAlbum.album.officialDr = 12;
    }

    const auto multiPrepared = prepareTagUpdates({multiTrackAlbum}, *loader, reloaded);
    ASSERT_EQ(multiPrepared.tracksToWrite.size(), 2U);
    EXPECT_EQ(multiPrepared.tracksToWrite[0].extraTag(u"ALBUM DYNAMIC RANGE"_s), QStringList{u"12"_s});
    EXPECT_EQ(multiPrepared.tracksToWrite[1].extraTag(u"ALBUM DYNAMIC RANGE"_s), QStringList{u"12"_s});
}
} // namespace
} // namespace Fooyin::DrMeter

int main(int argc, char** argv)
{
    QCoreApplication app{argc, argv};
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
