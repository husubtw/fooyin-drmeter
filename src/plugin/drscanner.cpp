#include "plugin/drscanner.h"

#include "core/dranalyzer.h"
#include "plugin/dralbumgrouper.h"

#include <core/constants.h>
#include <core/engine/audioconverter.h>

#include <QLoggingCategory>
#include <QtConcurrentMap>

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <utility>

Q_LOGGING_CATEGORY(DR_SCANNER, "fy.drmeter.scanner")

using namespace Qt::StringLiterals;

namespace Fooyin::DrMeter {
namespace {
constexpr size_t TargetReadBytes    = 256UL * 1024UL;
constexpr uint64_t SectorsPerSecond = 75ULL;

struct ScopeGuard
{
    std::function<void()> fn;
    ~ScopeGuard()
    {
        if(fn) {
            fn();
        }
    }
};

uint64_t framesForTime(uint64_t time, int sampleRate, uint64_t unitsPerSecond)
{
    if(sampleRate <= 0 || unitsPerSecond == 0) {
        return 0;
    }

    const auto rate = static_cast<uint64_t>(sampleRate);
    if(time > std::numeric_limits<uint64_t>::max() / rate) {
        return std::numeric_limits<uint64_t>::max();
    }

    return (time * rate) / unitsPerSecond;
}

std::optional<uint64_t> cueSectorProperty(const Track& track, const char* name)
{
    const auto properties   = track.extraProperties();
    const auto* const value = properties.find(QString::fromLatin1(name));
    if(!value) {
        return {};
    }

    bool ok{false};
    const uint64_t sector = value->toULongLong(&ok);
    return ok ? std::optional<uint64_t>{sector} : std::optional<uint64_t>{};
}

AudioBuffer removeLeadingFrames(const AudioBuffer& buffer, uint64_t frames)
{
    if(!buffer.isValid() || frames == 0) {
        return buffer;
    }

    if(std::cmp_greater_equal(frames, buffer.frameCount())) {
        return {};
    }

    const auto offset = static_cast<size_t>(buffer.format().bytesForFrames(static_cast<int>(frames)));
    return {buffer.constData().subspan(offset), buffer.format(), buffer.startTime()};
}

AudioBuffer trimBufferFrames(const AudioBuffer& buffer, uint64_t frames)
{
    if(!buffer.isValid() || std::cmp_greater_equal(frames, buffer.frameCount())) {
        return buffer;
    }

    const auto bytes = static_cast<size_t>(buffer.format().bytesForFrames(static_cast<int>(frames)));
    return {buffer.constData().first(bytes), buffer.format(), buffer.startTime()};
}

std::string channelPositionLabel(AudioFormat::ChannelPosition pos, size_t index)
{
    using P = AudioFormat::ChannelPosition;
    // clang-format off
    switch(pos) {
        case P::FrontLeft:           return "FL";
        case P::FrontRight:          return "FR";
        case P::FrontCenter:         return "FC";
        case P::LFE:                 return "LFE";
        case P::BackLeft:            return "BL";
        case P::BackRight:           return "BR";
        case P::FrontLeftOfCenter:   return "FLC";
        case P::FrontRightOfCenter:  return "FRC";
        case P::BackCenter:          return "BC";
        case P::SideLeft:            return "SL";
        case P::SideRight:           return "SR";
        case P::TopCenter:           return "TC";
        case P::TopFrontLeft:        return "TFL";
        case P::TopFrontCenter:      return "TFC";
        case P::TopFrontRight:       return "TFR";
        case P::TopBackLeft:         return "TBL";
        case P::TopBackCenter:       return "TBC";
        case P::TopBackRight:        return "TBR";
        case P::LFE2:                return "LFE2";
        case P::TopSideLeft:         return "TSL";
        case P::TopSideRight:        return "TSR";
        case P::BottomFrontCenter:   return "BFC";
        case P::BottomFrontLeft:     return "BFL";
        case P::BottomFrontRight:    return "BFR";
        case P::UnknownPosition:
        default:                     return "Ch" + std::to_string(index + 1U);
    }
    // clang-format on
}

bool isLfePosition(AudioFormat::ChannelPosition pos)
{
    return pos == AudioFormat::ChannelPosition::LFE || pos == AudioFormat::ChannelPosition::LFE2;
}

TrackMetadata buildTrackMetadata(const Track& track, const AudioFormat& decodedFormat)
{
    TrackMetadata meta;
    meta.trackNumber   = parseTrackNumber(track.trackNumber());
    meta.title         = track.effectiveTitle().toStdString();
    meta.codec         = track.codec().toStdString();
    meta.sampleRate    = static_cast<uint32_t>(std::max(0, decodedFormat.sampleRate()));
    meta.channels      = static_cast<uint32_t>(std::max(0, decodedFormat.channelCount()));
    meta.bitsPerSample = track.bitDepth() > 0 ? track.bitDepth() : decodedFormat.bitdepth();
    meta.bitrateKbps   = std::max(0, track.bitrate());
    return meta;
}
} // namespace

std::vector<ChannelInfo> channelLayoutFromAudioFormat(const AudioFormat& format)
{
    const int channelCount = format.channelCount();
    if(channelCount <= 0) {
        return {};
    }

    if(!format.hasChannelLayout()) {
        return DrAnalyzer::defaultChannelLayout(static_cast<uint32_t>(channelCount));
    }

    const auto positions = format.channelLayout();
    if(std::cmp_not_equal(positions.size(), channelCount)) {
        return DrAnalyzer::defaultChannelLayout(static_cast<uint32_t>(channelCount));
    }

    std::vector<ChannelInfo> layout;
    layout.reserve(positions.size());
    for(size_t i = 0; i < positions.size(); ++i) {
        layout.push_back(ChannelInfo{
            .label = channelPositionLabel(positions[i], i),
            .isLfe = isLfePosition(positions[i]),
        });
    }
    return layout;
}

int parseTrackNumber(const QString& raw)
{
    const QString trimmed = raw.trimmed();
    if(trimmed.isEmpty()) {
        return 0;
    }

    qsizetype digitEnd = 0;
    while(digitEnd < trimmed.size() && trimmed.at(digitEnd).isDigit()) {
        ++digitEnd;
    }

    if(digitEnd == 0) {
        return 0;
    }

    bool ok{false};
    const int value = trimmed.first(digitEnd).toInt(&ok);
    return (ok && value > 0) ? value : 0;
}

DrWorker::DrWorker(std::shared_ptr<AudioLoader> audioLoader, QObject* parent)
    : Worker{parent}
    , m_audioLoader{std::move(audioLoader)}
{ }

void DrWorker::closeThread()
{
    Worker::closeThread();

    QMetaObject::invokeMethod(this, [this] {
        if(m_watcher) {
            m_watcher->disconnect(this);
            m_watcher->cancel();
            m_watcher->waitForFinished();
        }
        if(!m_completed) {
            m_completed = true;
            Q_EMIT scanCancelled();
        }
        Q_EMIT closed();
    });
}

void DrWorker::scan(const TrackList& tracks, const QString& groupingPattern, const std::stop_token& token)
{
    if(!startWork(token)) {
        m_completed = true;
        Q_EMIT scanCancelled();
        Q_EMIT finished();
        return;
    }

    m_startTime = std::chrono::steady_clock::now();
    m_completed = false;
    m_completedTracks.store(0, std::memory_order_relaxed);
    m_albums.clear();
    m_items.clear();

    const auto groups = DrAlbumGrouper::groupTracks(tracks, groupingPattern);
    m_albums.resize(groups.size());

    for(size_t a = 0; a < groups.size(); ++a) {
        auto& dst             = m_albums[a];
        dst.groupKey          = groups[a].groupKey;
        dst.directory         = groups[a].directory;
        dst.album.albumArtist = groups[a].albumArtist.toStdString();
        dst.album.album       = groups[a].album.toStdString();
        dst.tracks.resize(groups[a].tracks.size());

        for(size_t t = 0; t < groups[a].tracks.size(); ++t) {
            WorkItem item;
            item.albumIndex = a;
            item.trackIndex = t;
            item.track      = groups[a].tracks[t];
            m_items.push_back(std::move(item));
        }
    }

    m_totalTracks = static_cast<int>(m_items.size());

    if(m_items.empty()) {
        finishScan(token);
        return;
    }

    Q_EMIT progressChanged(0, m_totalTracks, m_items.front().track.effectiveTitle());

    if(!m_watcher) {
        m_watcher = new QFutureWatcher<void>(this);
    }
    m_watcher->disconnect(this);

    QObject::connect(m_watcher, &QFutureWatcher<void>::finished, this, [this, token] { finishScan(token); });

    auto future = QtConcurrent::map(m_items, [this, token](WorkItem& item) { scanSingleItem(item, token); });
    m_watcher->setFuture(future);
}

void DrWorker::scanSingleItem(WorkItem& item, const std::stop_token& token)
{
    item.result.track = item.track;
    item.result.ok    = false;

    if(!mayRun(token)) {
        return;
    }

    if(!m_audioLoader) {
        item.result.error = u"No AudioLoader available"_s;
        return;
    }

    const bool bounded                         = item.track.isBoundedSegment() || item.track.offset() > 0;
    const AudioDecoder::DecoderOptions options = bounded ? (AudioDecoder::NoLooping | AudioDecoder::NoInfiniteLooping)
                                                         : (AudioDecoder::NoSeeking | AudioDecoder::NoInfiniteLooping);

    auto loaded = m_audioLoader->loadDecoderForTrack(item.track, options);
    if(!loaded.decoder || !loaded.format || !loaded.format->isValid()) {
        item.result.error = u"No decoder available for track"_s;
        qCWarning(DR_SCANNER) << "No decoder available for" << item.track.filepath();
        const int done = m_completedTracks.fetch_add(1, std::memory_order_acq_rel) + 1;
        Q_EMIT progressChanged(done, m_totalTracks, item.track.effectiveTitle());
        return;
    }

    AudioDecoder* decoder       = loaded.decoder.get();
    const AudioFormat srcFormat = *loaded.format;
    AudioFormat f64Format       = srcFormat;
    f64Format.setSampleFormat(SampleFormat::F64);

    const std::stop_callback stopCallback{token, [decoder] { decoder->requestAbort(); }};

    decoder->start();
    const ScopeGuard stopGuard{[decoder] { decoder->stop(); }};

    const auto cueStartSector = cueSectorProperty(item.track, Constants::CueIndex01Sector);
    const auto cueEndSector   = cueSectorProperty(item.track, Constants::CueEndSector);
    const int sampleRate      = srcFormat.sampleRate();

    if(item.track.offset() > 0 && !cueStartSector) {
        if(!decoder->isSeekable()) {
            item.result.error = u"Decoder cannot seek to track segment"_s;
            const int done    = m_completedTracks.fetch_add(1, std::memory_order_acq_rel) + 1;
            Q_EMIT progressChanged(done, m_totalTracks, item.track.effectiveTitle());
            return;
        }
        decoder->seek(item.track.offset());
    }

    uint64_t framesToSkip = framesForTime(cueStartSector.value_or(0), sampleRate, SectorsPerSecond);
    std::optional<uint64_t> framesRemaining;

    if(cueStartSector && cueEndSector) {
        if(*cueEndSector <= *cueStartSector) {
            item.result.error = u"Invalid CUE track boundaries"_s;
            const int done    = m_completedTracks.fetch_add(1, std::memory_order_acq_rel) + 1;
            Q_EMIT progressChanged(done, m_totalTracks, item.track.effectiveTitle());
            return;
        }
        framesRemaining = framesForTime(*cueEndSector, sampleRate, SectorsPerSecond) - framesToSkip;
    }
    else if(item.track.isBoundedSegment() && item.track.duration() > 0 && !cueStartSector) {
        framesRemaining = framesForTime(item.track.duration(), sampleRate, 1000ULL);
    }

    DrAnalyzer analyzer{static_cast<uint32_t>(sampleRate), channelLayoutFromAudioFormat(srcFormat)};
    bool decodeError{false};

    while(true) {
        if(!mayRun(token)) {
            return;
        }

        auto read = decoder->readAudio(TargetReadBytes);
        if(read.status == AudioDecoder::ReadStatus::NeedMoreInput) {
            continue;
        }
        if(read.status == AudioDecoder::ReadStatus::EndOfStream) {
            break;
        }
        if(read.status == AudioDecoder::ReadStatus::Error) {
            if(!mayRun(token)) {
                return;
            }
            decodeError       = true;
            item.result.error = read.error.isEmpty() ? u"Decoder error"_s : read.error;
            qCWarning(DR_SCANNER) << "Decode error for" << item.track.filepath() << ":" << item.result.error;
            break;
        }

        AudioBuffer buffer{std::move(read.buffer)};
        if(!buffer.isValid()) {
            continue;
        }

        if(framesToSkip > 0) {
            const auto skipped = std::min<uint64_t>(framesToSkip, static_cast<uint64_t>(buffer.frameCount()));
            buffer             = removeLeadingFrames(buffer, skipped);
            framesToSkip -= skipped;
            if(!buffer.isValid()) {
                continue;
            }
        }

        if(framesRemaining) {
            buffer = trimBufferFrames(buffer, *framesRemaining);
            *framesRemaining -= static_cast<uint64_t>(buffer.frameCount());
        }

        if(buffer.frameCount() > 0) {
            const AudioBuffer converted = (buffer.format() == f64Format) ? buffer : Audio::convert(buffer, f64Format);
            if(!converted.isValid()) {
                decodeError       = true;
                item.result.error = u"Audio format conversion to F64 failed"_s;
                break;
            }

            const auto sampleCount = static_cast<size_t>(converted.sampleCount());
            const auto frameCount  = static_cast<size_t>(converted.frameCount());
            const auto* doubles    = reinterpret_cast<const double*>(converted.data());
            analyzer.process(std::span<const double>{doubles, sampleCount}, frameCount);
        }

        if(framesRemaining && *framesRemaining == 0) {
            break;
        }
    }

    if(!mayRun(token)) {
        return;
    }

    if(!decodeError) {
        item.result.dr          = analyzer.finish();
        item.result.dr.metadata = buildTrackMetadata(item.track, srcFormat);
        item.result.ok          = true;
    }

    const int done = m_completedTracks.fetch_add(1, std::memory_order_acq_rel) + 1;
    Q_EMIT progressChanged(done, m_totalTracks, item.track.effectiveTitle());
}

void DrWorker::finishScan(std::stop_token token)
{
    if(m_completed) {
        return;
    }
    m_completed = true;

    if(!mayRun(std::move(token))) {
        setState(Idle);
        Q_EMIT scanCancelled();
        Q_EMIT finished();
        return;
    }

    const auto elapsed
        = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - m_startTime);

    double totalAudioSec = 0.0;

    for(auto& item : m_items) {
        auto& album                   = m_albums[item.albumIndex];
        album.tracks[item.trackIndex] = std::move(item.result);
    }

    for(auto& album : m_albums) {
        album.album.tracks.clear();
        album.album.tracks.reserve(album.tracks.size());
        for(const auto& scanned : album.tracks) {
            if(scanned.ok) {
                album.album.tracks.push_back(scanned.dr);
                totalAudioSec += scanned.dr.durationSec;
            }
        }
        album.album.officialDr = AlbumDrResult::computeOfficialDr(album.album.tracks);
    }

    DrScanSummary summary;
    summary.albums        = std::move(m_albums);
    summary.elapsedMs     = std::max<int64_t>(0, elapsed.count());
    summary.totalAudioSec = totalAudioSec;
    summary.speedRatio
        = summary.elapsedMs > 0 ? (totalAudioSec / (static_cast<double>(summary.elapsedMs) / 1000.0)) : 0.0;
    summary.cancelled = false;

    m_items.clear();
    setState(Idle);

    Q_EMIT scanFinished(summary);
    Q_EMIT finished();
}

DrScanner::DrScanner(std::shared_ptr<AudioLoader> audioLoader, QObject* parent)
    : QObject{parent}
    , m_worker{std::make_unique<DrWorker>(std::move(audioLoader))}
{
    qRegisterMetaType<DrScanSummary>("Fooyin::DrMeter::DrScanSummary");

    m_worker->moveToThread(&m_scanThread);
    m_scanThread.start();

    QObject::connect(m_worker.get(), &DrWorker::progressChanged, this, &DrScanner::progressChanged);
    QObject::connect(m_worker.get(), &DrWorker::scanFinished, this, &DrScanner::scanFinished);
    QObject::connect(m_worker.get(), &DrWorker::scanCancelled, this, &DrScanner::scanCancelled);
    QObject::connect(m_worker.get(), &DrWorker::closed, &m_scanThread, &QThread::quit, Qt::DirectConnection);
}

DrScanner::~DrScanner()
{
    cancel();
    waitForFinished();
}

void DrScanner::startScan(const TrackList& tracks, const QString& groupingPattern)
{
    QMetaObject::invokeMethod(m_worker.get(), [this, tracks, groupingPattern, token = m_worker->prepareWork()] {
        m_worker->scan(tracks, groupingPattern, token);
    });
}

void DrScanner::cancel()
{
    if(m_worker && !m_worker->closing()) {
        m_worker->closeThread();
    }
}

void DrScanner::waitForFinished()
{
    if(m_scanThread.isRunning()) {
        m_scanThread.wait();
    }
}
} // namespace Fooyin::DrMeter
