#include "core/drlogformatter.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <string_view>
#include <vector>

namespace Fooyin::DrMeter {
namespace {
constexpr std::string_view DashSeparator
    = "--------------------------------------------------------------------------------";
constexpr std::string_view EqualsSeparator
    = "================================================================================";

template <typename T>
void appendUnique(std::vector<T>& values, const T& value)
{
    if(std::ranges::find(values, value) == values.end()) {
        values.push_back(value);
    }
}

std::string joinStrings(const std::vector<std::string>& items, std::string_view separator)
{
    std::string out;
    for(size_t i = 0; i < items.size(); ++i) {
        if(i > 0U) {
            out += separator;
        }
        out += items[i];
    }
    return out;
}

std::string formatNumericDb(double db)
{
    if(std::isnan(db)) {
        return "nan";
    }
    if(std::isinf(db)) {
        return std::signbit(db) ? "-inf" : "+inf";
    }
    return std::format("{:.2f}", clampNegativeZero(db));
}

std::string sanitizeMetadataText(std::string_view input)
{
    std::string out;
    out.reserve(input.size());
    for(size_t i = 0; i < input.size(); ++i) {
        const char c  = input[i];
        const auto uc = static_cast<unsigned char>(c);
        if(c == '\r' || c == '\n' || c == '\t') {
            if(c == '\r' && i + 1U < input.size() && input[i + 1U] == '\n') {
                ++i;
            }
            out.push_back(' ');
        }
        else if(uc < 0x20U || uc == 0x7FU) {
            continue;
        }
        else {
            out.push_back(c);
        }
    }

    const size_t first = out.find_first_not_of(' ');
    if(first == std::string::npos) {
        return {};
    }
    const size_t last = out.find_last_not_of(' ');
    return out.substr(first, last - first + 1U);
}

std::string formatAnalyzedLine(const AlbumDrResult& album)
{
    const std::string artist = sanitizeMetadataText(album.albumArtist);
    const std::string title  = sanitizeMetadataText(album.album);
    if(!artist.empty() && !title.empty()) {
        return std::format("Analyzed: {} / {}", artist, title);
    }
    if(!artist.empty()) {
        return std::format("Analyzed: {}", artist);
    }
    if(!title.empty()) {
        return std::format("Analyzed: {}", title);
    }
    return "Analyzed: Unknown Artist / Unknown Album";
}
} // namespace

std::string DrLogFormatter::formatDuration(uint64_t totalFrames, uint32_t sampleRate)
{
    const int64_t totalSeconds = roundDurationSeconds(totalFrames, sampleRate);
    const int64_t minutes      = totalSeconds / 60;
    const int64_t seconds      = totalSeconds % 60;
    return std::format("{}:{:02d}", minutes, seconds);
}

std::string DrLogFormatter::formatDbValue(double db, bool withUnit)
{
    std::string num = formatNumericDb(db);
    if(!withUnit) {
        return num;
    }
    return std::format("{} dBFS", num);
}

std::string DrLogFormatter::formatTrackLabel(const TrackMetadata& metadata)
{
    std::string title = sanitizeMetadataText(metadata.title);
    if(metadata.trackNumber > 0) {
        return std::format("{:02d}-{}", metadata.trackNumber, title);
    }
    return title;
}

std::string DrLogFormatter::formatAlbumLog(const AlbumDrResult& album, const LogFormatOptions& options)
{
    std::string out;
    out.reserve(1024);

    out += std::format("fooyin {} / DR Meter v{}\n", options.fooyinVersion, options.pluginVersion);
    out += std::format("log date: {}\n\n", options.logDate);

    out += DashSeparator;
    out += '\n';
    out += formatAnalyzedLine(album);
    out += '\n';
    out += DashSeparator;
    out += "\n\n";

    out += "DR         Peak           RMS       Duration Track\n";
    out += DashSeparator;
    out += '\n';

    std::vector<std::string> sampleRates;
    std::vector<std::string> channelCounts;
    std::vector<std::string> bitDepths;
    std::vector<std::string> codecs;
    int64_t bitrateSum   = 0;
    int64_t bitrateCount = 0;

    for(const auto& track : album.tracks) {
        const std::string drCol    = std::format("DR{}", track.trackDrInt);
        const std::string peakCol  = formatNumericDb(track.peakDb);
        const std::string rmsCol   = formatNumericDb(track.rmsDb);
        const std::string durCol   = formatDuration(track.totalFrames, track.metadata.sampleRate);
        const std::string trackCol = formatTrackLabel(track.metadata);

        out += std::format("{:<5}{:>10} dBFS{:>9} dBFS{:>10} {}\n", drCol, peakCol, rmsCol, durCol, trackCol);

        if(track.metadata.sampleRate > 0U) {
            appendUnique(sampleRates, std::format("{} Hz", track.metadata.sampleRate));
        }
        if(track.metadata.channels > 0U) {
            appendUnique(channelCounts, std::format("{}", track.metadata.channels));
        }
        if(track.metadata.bitsPerSample > 0) {
            appendUnique(bitDepths, std::format("{}", track.metadata.bitsPerSample));
        }
        if(track.metadata.bitrateKbps > 0) {
            bitrateSum += track.metadata.bitrateKbps;
            ++bitrateCount;
        }
        if(!track.metadata.codec.empty()) {
            const std::string codec = sanitizeMetadataText(track.metadata.codec);
            if(!codec.empty()) {
                appendUnique(codecs, codec);
            }
        }
    }

    out += DashSeparator;
    out += "\n\n";

    out += std::format("{:<19}{}\n", "Number of tracks:", album.tracks.size());
    out += std::format("{:<19}DR{}\n\n", "Official DR value:", album.officialDr);

    if(!sampleRates.empty()) {
        out += std::format("{:<19}{}\n", "Samplerate:", joinStrings(sampleRates, ", "));
    }
    if(!channelCounts.empty()) {
        out += std::format("{:<19}{}\n", "Channels:", joinStrings(channelCounts, ", "));
    }
    if(!bitDepths.empty()) {
        out += std::format("{:<19}{}\n", "Bits per sample:", joinStrings(bitDepths, ", "));
    }
    if(bitrateCount > 0) {
        const auto avgBitrate
            = static_cast<int64_t>(std::llround(static_cast<double>(bitrateSum) / static_cast<double>(bitrateCount)));
        out += std::format("{:<19}{} kbps\n", "Bitrate:", avgBitrate);
    }
    if(!codecs.empty()) {
        out += std::format("{:<19}{}\n", "Codec:", joinStrings(codecs, ", "));
    }

    out += EqualsSeparator;
    out += '\n';

    return out;
}

std::string DrLogFormatter::formatMultiAlbumLog(std::span<const AlbumDrResult> albums, const LogFormatOptions& options)
{
    std::string out;
    for(size_t i = 0; i < albums.size(); ++i) {
        if(i > 0U) {
            out += '\n';
        }
        out += formatAlbumLog(albums[i], options);
    }
    return out;
}

std::string DrLogFormatter::formatResultsTable(std::span<const AlbumDrResult> albums)
{
    std::vector<std::string> channelLabels;
    for(const auto& album : albums) {
        for(const auto& track : album.tracks) {
            for(const auto& ch : track.channels) {
                appendUnique(channelLabels, sanitizeMetadataText(ch.channelLabel));
            }
        }
    }

    std::string out = "Name\tTrack DR\tAlbum DR";
    for(const auto& label : channelLabels) {
        out += std::format("\tDR ({})", label);
    }
    for(const auto& label : channelLabels) {
        out += std::format("\tRMS ({})", label);
    }
    for(const auto& label : channelLabels) {
        out += std::format("\tPeak ({})", label);
    }
    out += '\n';

    for(const auto& album : albums) {
        for(const auto& track : album.tracks) {
            out += std::format("{}\t{}\t{}", formatTrackLabel(track.metadata), track.trackDrInt, album.officialDr);

            const auto findChannel = [&track](const std::string& label) -> const ChannelDrResult* {
                const auto it = std::ranges::find_if(track.channels, [&label](const ChannelDrResult& ch) {
                    return sanitizeMetadataText(ch.channelLabel) == label;
                });
                return it != track.channels.end() ? &(*it) : nullptr;
            };

            for(const auto& label : channelLabels) {
                out += '\t';
                if(const auto* ch = findChannel(label)) {
                    out += std::format("{} dB", formatNumericDb(ch->drDb));
                }
            }
            for(const auto& label : channelLabels) {
                out += '\t';
                if(const auto* ch = findChannel(label)) {
                    out += formatDbValue(ch->rmsDb, true);
                }
            }
            for(const auto& label : channelLabels) {
                out += '\t';
                if(const auto* ch = findChannel(label)) {
                    out += formatDbValue(ch->peakDb, true);
                }
            }
            out += '\n';
        }
    }

    return out;
}
} // namespace Fooyin::DrMeter
