#include "plugin/drtagwriter.h"

using namespace Qt::StringLiterals;

namespace Fooyin::DrMeter {
QString DrTagWriteSummary::formatSummary() const
{
    QString summary = u"Updated %1 %2"_s.arg(updated).arg(updated == 1 ? u"file"_s : u"files"_s);

    if(failed > 0) {
        summary = u"%1, %2 failed"_s.arg(summary).arg(failed);
    }
    if(skipped > 0) {
        summary = u"%1, skipped %2 (CUE/read-only)"_s.arg(summary).arg(skipped);
    }
    if(cancelled) {
        summary = u"%1 (cancelled)"_s.arg(summary);
    }

    return summary;
}

bool isTrackTagWritable(const Track& track, const AudioLoader& audioLoader)
{
    return !track.hasCue() && !track.isInArchive() && !track.isRemote() && audioLoader.canWriteMetadata(track);
}

DrTagPrepareResult prepareTagUpdates(const std::vector<ScannedAlbum>& albums, const AudioLoader& audioLoader,
                                     const DrSettings& settings)
{
    DrTagPrepareResult result;

    const QString trackDrKey = QString::fromLatin1(TagNames::DynamicRange);
    const QString albumDrKey = QString::fromLatin1(TagNames::AlbumDynamicRange);

    for(const auto& album : albums) {
        const bool writeAlbumDr  = album.album.tracks.size() > 1 || settings.writeAlbumDrForSingleTrack();
        const QString albumDrVal = QString::number(album.album.officialDr);

        for(const auto& scanned : album.tracks) {
            if(!scanned.ok) {
                continue;
            }

            if(!isTrackTagWritable(scanned.track, audioLoader)) {
                ++result.skippedCount;
                continue;
            }

            Track updatedTrack{scanned.track};
            updatedTrack.replaceExtraTag(trackDrKey, QString::number(scanned.dr.trackDrInt));
            if(writeAlbumDr) {
                updatedTrack.replaceExtraTag(albumDrKey, albumDrVal);
            }
            result.tracksToWrite.push_back(std::move(updatedTrack));
        }
    }

    return result;
}

DrTagWriter::DrTagWriter(MusicLibrary* library, std::shared_ptr<AudioLoader> audioLoader, QObject* parent)
    : QObject{parent}
    , m_library{library}
    , m_audioLoader{std::move(audioLoader)}
{
    qRegisterMetaType<DrTagWriteSummary>("Fooyin::DrMeter::DrTagWriteSummary");
}

DrTagWriter::~DrTagWriter()
{
    cancel();
    m_watcher.reset();
}

void DrTagWriter::writeTags(const std::vector<ScannedAlbum>& albums, const DrSettings& settings)
{
    if(m_running) {
        cancel();
    }

    if(!m_audioLoader) {
        DrTagWriteSummary summary;
        for(const auto& album : albums) {
            for(const auto& scanned : album.tracks) {
                if(scanned.ok) {
                    ++summary.skipped;
                }
            }
        }
        Q_EMIT finished(summary);
        return;
    }

    const DrTagPrepareResult prepared = prepareTagUpdates(albums, *m_audioLoader, settings);
    m_pendingWriteCount               = static_cast<int>(prepared.tracksToWrite.size());
    m_pendingSkipped                  = prepared.skippedCount;

    if(prepared.tracksToWrite.empty() || !m_library) {
        DrTagWriteSummary summary;
        summary.skipped = prepared.skippedCount;
        if(!m_library) {
            summary.failed = m_pendingWriteCount;
        }
        Q_EMIT finished(summary);
        return;
    }

    m_running       = true;
    m_activeRequest = m_library->writeTrackMetadata(prepared.tracksToWrite);

    if(!m_activeRequest.finished.isValid()) {
        m_running       = false;
        m_activeRequest = {};
        DrTagWriteSummary summary;
        summary.failed  = m_pendingWriteCount;
        summary.skipped = m_pendingSkipped;
        Q_EMIT finished(summary);
        return;
    }

    if(m_watcher) {
        m_watcher->disconnect(this);
    }
    m_watcher = std::make_unique<QFutureWatcher<WriteResult>>();
    QObject::connect(m_watcher.get(), &QFutureWatcher<WriteResult>::finished, this, [this] {
        m_running = false;
        DrTagWriteSummary summary;
        summary.skipped = m_pendingSkipped;

        if(m_watcher && !m_watcher->isCanceled()) {
            if(m_watcher->future().resultCount() > 0) {
                const WriteResult res = m_watcher->result();
                summary.updated       = res.succeeded;
                summary.failed        = res.failed;
                summary.cancelled     = (res.state == WriteState::Cancelled);
            }
            else {
                summary.failed = m_pendingWriteCount;
            }
        }
        else {
            summary.cancelled = true;
        }

        m_activeRequest = {};
        Q_EMIT finished(summary);
    });

    m_watcher->setFuture(m_activeRequest.finished);
}

void DrTagWriter::cancel()
{
    if(m_watcher) {
        m_watcher->disconnect(this);
    }
    if(m_running && m_activeRequest.cancel) {
        m_activeRequest.cancel();
    }
    m_running       = false;
    m_activeRequest = {};
}

bool DrTagWriter::isRunning() const noexcept
{
    return m_running;
}
} // namespace Fooyin::DrMeter
