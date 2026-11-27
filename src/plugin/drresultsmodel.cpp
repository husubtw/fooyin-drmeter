#include "plugin/drresultsmodel.h"

#include "core/drlogformatter.h"

#include <QMetaType>
#include <QStringList>

#include <algorithm>
#include <cmath>
#include <format>
#include <utility>

using namespace Qt::StringLiterals;

namespace Fooyin::DrMeter {
namespace {
constexpr int NameColumn      = 0;
constexpr int TrackDrColumn   = 1;
constexpr int AlbumDrColumn   = 2;
constexpr int BaseColumnCount = 3; // Name, Track DR, Album DR

bool isValidNumericSortValue(const QVariant& value)
{
    if(!value.isValid() || value.isNull()) {
        return false;
    }

    const int typeId = value.metaType().id();
    if(typeId == QMetaType::Double || typeId == QMetaType::Float) {
        return !std::isnan(value.toDouble());
    }

    return true;
}

bool preserveSourceOrder(int leftRow, int rightRow, bool descending) noexcept
{
    return descending ? (leftRow > rightRow) : (leftRow < rightRow);
}
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

TriStateSort advanceTriStateSort(TriStateSort current, int clickedColumn) noexcept
{
    if(clickedColumn < 0) {
        return {};
    }
    if(current.column != clickedColumn) {
        return {.column = clickedColumn, .order = Qt::AscendingOrder};
    }
    if(current.order == Qt::AscendingOrder) {
        return {.column = clickedColumn, .order = Qt::DescendingOrder};
    }
    return {};
}

std::vector<int> collectValidRows(const QAbstractItemModel& model)
{
    std::vector<int> validRows;
    const int rows = model.rowCount();
    if(rows <= 0 || model.columnCount() < 2) {
        return validRows;
    }

    validRows.reserve(static_cast<size_t>(rows));
    for(int r = 0; r < rows; ++r) {
        if(model.data(model.index(r, TrackDrColumn), DrResultsModel::SortRole).isValid()) {
            validRows.push_back(r);
        }
    }

    return validRows;
}

QString formatRowsAsTsv(const QAbstractItemModel& model, const std::vector<int>& rows, bool includeHeader)
{
    const int cols = model.columnCount();
    if(cols <= 0) {
        return {};
    }

    QString out;
    if(includeHeader) {
        QStringList headers;
        headers.reserve(cols);
        for(int c = 0; c < cols; ++c) {
            headers.push_back(model.headerData(c, Qt::Horizontal, Qt::DisplayRole).toString());
        }
        out += headers.join(u'\t');
        out += u'\n';
    }

    const int rowCount = model.rowCount();
    for(const int r : rows) {
        if(r < 0 || r >= rowCount) {
            continue;
        }

        QStringList cells;
        cells.reserve(cols);
        for(int c = 0; c < cols; ++c) {
            cells.push_back(model.data(model.index(r, c), Qt::DisplayRole).toString());
        }
        out += cells.join(u'\t');
        out += u'\n';
    }

    return out;
}

DrResultsModel::DrResultsModel(const std::vector<ScannedAlbum>& albums, QObject* parent)
    : DrResultsModel{albums, true, parent}
{ }

DrResultsModel::DrResultsModel(const std::vector<ScannedAlbum>& albums, bool verbose, QObject* parent)
    : QAbstractTableModel{parent}
    , m_verbose{verbose}
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
    if(!m_verbose) {
        return BaseColumnCount;
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

    if(section == NameColumn) {
        return tr("Name");
    }
    if(section == TrackDrColumn) {
        return tr("Track DR");
    }
    if(section == AlbumDrColumn) {
        return tr("Album DR");
    }

    const auto channelCol = resolveChannelColumn(section);
    if(!channelCol) {
        return {};
    }

    const QString ch = QString::fromStdString(m_channelLabels[channelCol->channelIndex]);

    switch(channelCol->band) {
        case ChannelBand::Dr:
            return u"DR (%1)"_s.arg(ch);
        case ChannelBand::Rms:
            return u"RMS (%1)"_s.arg(ch);
        case ChannelBand::Peak:
            return u"Peak (%1)"_s.arg(ch);
    }
    return {};
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
        if(col == NameColumn) {
            return static_cast<int>(Qt::AlignVCenter | Qt::AlignLeft);
        }
        if(col == TrackDrColumn || col == AlbumDrColumn) {
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

    if(role == SortRole) {
        if(col == NameColumn) {
            return row.name;
        }
        if(!row.ok) {
            return {};
        }
        if(col == TrackDrColumn) {
            return row.trackDr;
        }
        if(col == AlbumDrColumn) {
            return row.albumDr;
        }

        const auto channelCol = resolveChannelColumn(col);
        if(!channelCol) {
            return {};
        }

        const ChannelDrResult* ch = findChannel(row, channelCol->channelIndex);
        if(!ch) {
            return {};
        }

        switch(channelCol->band) {
            case ChannelBand::Dr:
                return ch->drDb;
            case ChannelBand::Rms:
                return ch->rmsDb;
            case ChannelBand::Peak:
                return ch->peakDb;
        }
        return {};
    }

    if(role != Qt::DisplayRole) {
        return {};
    }

    if(col == NameColumn) {
        return row.name;
    }

    if(!row.ok) {
        return col == TrackDrColumn ? tr("Error") : QVariant{};
    }

    if(col == TrackDrColumn) {
        return QString::number(row.trackDr);
    }
    if(col == AlbumDrColumn) {
        return QString::number(row.albumDr);
    }

    const auto channelCol = resolveChannelColumn(col);
    if(!channelCol) {
        return {};
    }

    const ChannelDrResult* ch = findChannel(row, channelCol->channelIndex);
    if(!ch) {
        return {};
    }

    switch(channelCol->band) {
        case ChannelBand::Dr:
            return QString::fromStdString(std::format("{} dB", DrLogFormatter::formatDbValue(ch->drDb, false)));
        case ChannelBand::Rms:
            return QString::fromStdString(DrLogFormatter::formatDbValue(ch->rmsDb, true));
        case ChannelBand::Peak:
            return QString::fromStdString(DrLogFormatter::formatDbValue(ch->peakDb, true));
    }
    return {};
}

bool DrResultsModel::isVerbose() const noexcept
{
    return m_verbose;
}

const std::vector<std::string>& DrResultsModel::channelLabels() const noexcept
{
    return m_channelLabels;
}

std::optional<DrResultsModel::ChannelColumn> DrResultsModel::resolveChannelColumn(int col) const noexcept
{
    const size_t chCount = m_channelLabels.size();
    if(!m_verbose || chCount == 0U || col < BaseColumnCount) {
        return {};
    }

    const auto offset = static_cast<size_t>(col - BaseColumnCount);
    const size_t band = offset / chCount;
    if(band > static_cast<size_t>(ChannelBand::Peak)) {
        return {};
    }

    return ChannelColumn{
        .band         = static_cast<ChannelBand>(band),
        .channelIndex = offset % chCount,
    };
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

DrResultsSortFilterProxyModel::DrResultsSortFilterProxyModel(QObject* parent)
    : QSortFilterProxyModel{parent}
{
    setSortRole(DrResultsModel::SortRole);
}

bool DrResultsSortFilterProxyModel::lessThan(const QModelIndex& sourceLeft, const QModelIndex& sourceRight) const
{
    const int col         = sourceLeft.column();
    const bool descending = (sortOrder() == Qt::DescendingOrder);
    const QVariant left   = sourceLeft.data(DrResultsModel::SortRole);
    const QVariant right  = sourceRight.data(DrResultsModel::SortRole);

    if(col == NameColumn) {
        const int cmp = QString::compare(left.toString(), right.toString(), Qt::CaseInsensitive);
        if(cmp != 0) {
            return cmp < 0;
        }
        return preserveSourceOrder(sourceLeft.row(), sourceRight.row(), descending);
    }

    const bool leftValid  = isValidNumericSortValue(left);
    const bool rightValid = isValidNumericSortValue(right);

    if(!leftValid && !rightValid) {
        return preserveSourceOrder(sourceLeft.row(), sourceRight.row(), descending);
    }
    if(leftValid != rightValid) {
        return descending ? rightValid : leftValid;
    }

    if(col == TrackDrColumn || col == AlbumDrColumn) {
        const int l = left.toInt();
        const int r = right.toInt();
        if(l != r) {
            return l < r;
        }
        return preserveSourceOrder(sourceLeft.row(), sourceRight.row(), descending);
    }

    const double l = left.toDouble();
    const double r = right.toDouble();
    if(l != r) {
        return l < r;
    }
    return preserveSourceOrder(sourceLeft.row(), sourceRight.row(), descending);
}
} // namespace Fooyin::DrMeter
