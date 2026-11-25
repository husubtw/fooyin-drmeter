#pragma once

#include "plugin/drscanner.h"

#include <QAbstractTableModel>
#include <QString>

#include <cstdint>
#include <string>
#include <vector>

namespace Fooyin::DrMeter {
[[nodiscard]] QString formatMeasurementFooter(int64_t elapsedMs, double speedRatio);

class DrResultsModel : public QAbstractTableModel
{
    Q_OBJECT

public:
    explicit DrResultsModel(const std::vector<ScannedAlbum>& albums, QObject* parent = nullptr);

    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] int columnCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant headerData(int section, Qt::Orientation orientation,
                                      int role = Qt::DisplayRole) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;

    [[nodiscard]] const std::vector<std::string>& channelLabels() const noexcept;

private:
    struct RowEntry
    {
        QString name;
        int trackDr{0};
        int albumDr{0};
        bool ok{false};
        QString error;
        std::vector<ChannelDrResult> channels;
    };

    [[nodiscard]] const ChannelDrResult* findChannel(const RowEntry& row, size_t channelColIdx) const;

    std::vector<std::string> m_channelLabels;
    std::vector<RowEntry> m_rows;
};
} // namespace Fooyin::DrMeter
