#pragma once

#include "plugin/drscanner.h"

#include <QAbstractTableModel>
#include <QSortFilterProxyModel>
#include <QString>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace Fooyin::DrMeter {
struct TriStateSort
{
    int column{-1};
    Qt::SortOrder order{Qt::AscendingOrder};
};

[[nodiscard]] QString formatMeasurementFooter(int64_t elapsedMs, double speedRatio);
[[nodiscard]] TriStateSort advanceTriStateSort(TriStateSort current, int clickedColumn) noexcept;
[[nodiscard]] std::vector<int> collectValidRows(const QAbstractItemModel& model);
[[nodiscard]] QString formatRowsAsTsv(const QAbstractItemModel& model, const std::vector<int>& rows,
                                      bool includeHeader);

class DrResultsModel : public QAbstractTableModel
{
    Q_OBJECT

public:
    enum Roles : uint16_t
    {
        SortRole = Qt::UserRole + 1,
    };

    explicit DrResultsModel(const std::vector<ScannedAlbum>& albums, QObject* parent = nullptr);
    DrResultsModel(const std::vector<ScannedAlbum>& albums, bool verbose, QObject* parent = nullptr);

    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] int columnCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant headerData(int section, Qt::Orientation orientation,
                                      int role = Qt::DisplayRole) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;

    [[nodiscard]] bool isVerbose() const noexcept;
    [[nodiscard]] const std::vector<std::string>& channelLabels() const noexcept;

private:
    enum class ChannelBand : uint8_t
    {
        Dr   = 0,
        Rms  = 1,
        Peak = 2,
    };

    struct ChannelColumn
    {
        ChannelBand band{ChannelBand::Dr};
        size_t channelIndex{0};
    };

    struct RowEntry
    {
        QString name;
        int trackDr{0};
        int albumDr{0};
        bool ok{false};
        QString error;
        std::vector<ChannelDrResult> channels;
    };

    [[nodiscard]] std::optional<ChannelColumn> resolveChannelColumn(int col) const noexcept;
    [[nodiscard]] const ChannelDrResult* findChannel(const RowEntry& row, size_t channelColIdx) const;

    bool m_verbose{true};
    std::vector<std::string> m_channelLabels;
    std::vector<RowEntry> m_rows;
};

class DrResultsSortFilterProxyModel : public QSortFilterProxyModel
{
    Q_OBJECT

public:
    explicit DrResultsSortFilterProxyModel(QObject* parent = nullptr);

protected:
    [[nodiscard]] bool lessThan(const QModelIndex& sourceLeft, const QModelIndex& sourceRight) const override;
};
} // namespace Fooyin::DrMeter
