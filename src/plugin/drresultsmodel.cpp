#include "plugin/drresultsmodel.h"

#include "core/drlogformatter.h"

#include <algorithm>
#include <cmath>
#include <format>

using namespace Qt::StringLiterals;

namespace Fooyin::DrMeter {
namespace {
constexpr int BaseColumnCount = 3; // Name, Track DR, Album DR
} // namespace

QString formatMeasurementFooter(int64_t elapsedMs, double speedRatio)
{
    const int64_t safeMs = std::max<int64_t>(0, elapsedMs);
    const int64_t mins   = safeMs / 60000;
    const int64_t secs   = (safeMs % 60000) / 1000;
    const int64_t millis = safeMs % 1000;
    const double speed   = (std::isfinite(speedRatio) && speedRatio > 0.0) ? speedRatio : 0.0;

    const std::string formatted
        = std::format("Measured in: {}:{:02d}.{:03d}, speed: {:.2f}x", mins, secs, millis, speed);
    return QString::fromStdString(formatted);
}

DrResultsModel::DrResultsModel(const std::vector<ScannedAlbum>& albums, QObject* parent)
    : QAbstractTableModel{parent}
{
    for(const auto& album : albums) {
        for(const auto& scanned : album.tracks) {
            RowEntry row;
            row.ok      = scanned.ok;
            row.error   = scanned.error;
            row.albumDr = album.album.officialDr;

            if(scanned.ok) {
                row.name     = QString::fromStdString(DrLogFormatter::formatTrackLabel(scanned.dr.metadata));
                row.trackDr  = scanned.dr.trackDrInt;
                row.channels = scanned.dr.channels;

                for(auto& ch : row.channels) {
                    ch.channelLabel = DrLogFormatter::sanitizeText(ch.channelLabel);
                    if(std::ranges::find(m_channelLabels, ch.channelLabel) == m_channelLabels.end()) {
                        m_channelLabels.push_back(ch.channelLabel);
                    }
                }
            }
            else {
                row.name = QString::fromStdString(
                    DrLogFormatter::sanitizeText(scanned.track.effectiveTitle().toStdString()));
            }

            m_rows.push_back(std::move(row));
        }
    }
}

int DrResultsModel::rowCount(const QModelIndex& parent) const
{
    if(parent.isValid()) {
        return 0;
    }
    return static_cast<int>(m_rows.size());
}

int DrResultsModel::columnCount(const QModelIndex& parent) const
{
    if(parent.isValid()) {
        return 0;
    }
    return BaseColumnCount + static_cast<int>(m_channelLabels.size() * 3U);
}

QVariant DrResultsModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if(orientation != Qt::Horizontal) {
        return {};
    }

    if(role == Qt::TextAlignmentRole) {
        if(section == 0) {
            return static_cast<int>(Qt::AlignVCenter | Qt::AlignLeft);
        }
        return Qt::AlignCenter;
    }

    if(role != Qt::DisplayRole) {
        return {};
    }

    if(section == 0) {
        return tr("Name");
    }
    if(section == 1) {
        return tr("Track DR");
    }
    if(section == 2) {
        return tr("Album DR");
    }

    const size_t chCount = m_channelLabels.size();
    if(chCount == 0U || section < BaseColumnCount) {
        return {};
    }

    const auto offset = static_cast<size_t>(section - BaseColumnCount);
    const size_t band = offset / chCount;
    const size_t idx  = offset % chCount;
    const QString ch  = QString::fromStdString(m_channelLabels[idx]);

    switch(band) {
        case 0:
            return u"DR (%1)"_s.arg(ch);
        case 1:
            return u"RMS (%1)"_s.arg(ch);
        case 2:
            return u"Peak (%1)"_s.arg(ch);
        default:
            return {};
    }
}

QVariant DrResultsModel::data(const QModelIndex& index, int role) const
{
    if(!checkIndex(index, CheckIndexOption::IndexIsValid)) {
        return {};
    }

    const int rowIdx = index.row();
    if(rowIdx < 0 || std::cmp_greater_equal(rowIdx, m_rows.size())) {
        return {};
    }

    const RowEntry& row = m_rows[static_cast<size_t>(rowIdx)];
    const int col       = index.column();

    if(role == Qt::TextAlignmentRole) {
        if(col == 0) {
            return static_cast<int>(Qt::AlignVCenter | Qt::AlignLeft);
        }
        if(col == 1 || col == 2) {
            return Qt::AlignCenter;
        }
        return static_cast<int>(Qt::AlignVCenter | Qt::AlignRight);
    }

    if(role == Qt::ToolTipRole) {
        if(!row.ok && !row.error.isEmpty()) {
            return row.error;
        }
        return {};
    }

    if(role != Qt::DisplayRole) {
        return {};
    }

    if(col == 0) {
        return row.name;
    }

    if(!row.ok) {
        return col == 1 ? tr("Error") : QVariant{};
    }

    if(col == 1) {
        return QString::number(row.trackDr);
    }
    if(col == 2) {
        return QString::number(row.albumDr);
    }

    const size_t chCount = m_channelLabels.size();
    if(chCount == 0U || col < BaseColumnCount) {
        return {};
    }

    const auto offset = static_cast<size_t>(col - BaseColumnCount);
    const size_t band = offset / chCount;
    const size_t idx  = offset % chCount;

    const ChannelDrResult* ch = findChannel(row, idx);
    if(!ch) {
        return {};
    }

    switch(band) {
        case 0:
            return QString::fromStdString(std::format("{} dB", DrLogFormatter::formatDbValue(ch->drDb, false)));
        case 1:
            return QString::fromStdString(DrLogFormatter::formatDbValue(ch->rmsDb, true));
        case 2:
            return QString::fromStdString(DrLogFormatter::formatDbValue(ch->peakDb, true));
        default:
            return {};
    }
}

const std::vector<std::string>& DrResultsModel::channelLabels() const noexcept
{
    return m_channelLabels;
}

const ChannelDrResult* DrResultsModel::findChannel(const RowEntry& row, size_t channelColIdx) const
{
    if(channelColIdx >= m_channelLabels.size()) {
        return nullptr;
    }

    const std::string& label = m_channelLabels[channelColIdx];
    const auto it
        = std::ranges::find_if(row.channels, [&label](const ChannelDrResult& ch) { return ch.channelLabel == label; });
    return it != row.channels.end() ? &(*it) : nullptr;
}
} // namespace Fooyin::DrMeter
