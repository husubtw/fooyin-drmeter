#pragma once

#include <core/track.h>

#include <QString>

#include <vector>

namespace Fooyin::DrMeter {
struct AlbumGroup
{
    QString groupKey;
    QString albumArtist;
    QString album;
    QString directory;
    TrackList tracks;
};

class DrAlbumGrouper
{
public:
    [[nodiscard]] static std::vector<AlbumGroup> groupTracks(const TrackList& tracks, const QString& groupingPattern);
};
} // namespace Fooyin::DrMeter
