#include "plugin/dralbumgrouper.h"

#include "plugin/drsettings.h"

#include <core/scripting/scriptparser.h>

#include <QFileInfo>

#include <unordered_map>

namespace Fooyin::DrMeter {
namespace {
QString trackParentDirectory(const Track& track)
{
    if(track.isRemote() || track.isVirtual()) {
        return {};
    }

    const QString path = track.isInArchive() ? track.archivePath() : track.filepath();
    if(path.isEmpty()) {
        return {};
    }

    const QFileInfo info{path};
    return info.isAbsolute() ? info.absolutePath() : QString{};
}
} // namespace

std::vector<AlbumGroup> DrAlbumGrouper::groupTracks(const TrackList& tracks, const QString& groupingPattern)
{
    std::vector<AlbumGroup> groups;
    if(tracks.empty()) {
        return groups;
    }

    const QString effectivePattern
        = groupingPattern.trimmed().isEmpty() ? DrSettings::defaultAlbumGroupingPattern() : groupingPattern;

    ScriptParser parser;
    const ParsedScript parsed = parser.parse(effectivePattern);
    const QString emptyKey    = parser.evaluate(parsed, Track{});

    std::unordered_map<QString, size_t> groupIndexByKey;
    groupIndexByKey.reserve(tracks.size());

    for(const Track& track : tracks) {
        const QString evaluated = parser.evaluate(parsed, track);
        const QString dirPath   = trackParentDirectory(track);
        const bool isEmptyEval  = evaluated.trimmed().isEmpty() || evaluated == emptyKey;
        const QString key       = isEmptyEval ? dirPath : evaluated;

        const auto it = groupIndexByKey.find(key);
        if(it == groupIndexByKey.end()) {
            const size_t idx = groups.size();
            groupIndexByKey.emplace(key, idx);

            AlbumGroup group;
            group.groupKey    = key;
            group.albumArtist = track.effectiveAlbumArtist();
            group.album       = track.album();
            group.directory   = dirPath;
            group.tracks.push_back(track);
            groups.push_back(std::move(group));
        }
        else {
            auto& group = groups[it->second];
            if(group.albumArtist.isEmpty()) {
                group.albumArtist = track.effectiveAlbumArtist();
            }
            if(group.album.isEmpty()) {
                group.album = track.album();
            }
            if(group.directory.isEmpty()) {
                group.directory = dirPath;
            }
            group.tracks.push_back(track);
        }
    }

    return groups;
}
} // namespace Fooyin::DrMeter
