#pragma once

#include <QString>

namespace Fooyin {
class SettingsManager;
} // namespace Fooyin

namespace Fooyin::DrMeter {
namespace SettingKeys {
inline constexpr auto AlbumGroupingPattern       = "DrMeter/AlbumGroupingPattern";
inline constexpr auto LogFileName                = "DrMeter/LogFileName";
inline constexpr auto WriteAlbumDrForSingleTrack = "DrMeter/WriteAlbumDrForSingleTrack";
} // namespace SettingKeys

class DrSettings
{
public:
    explicit DrSettings(SettingsManager* settings = nullptr);

    [[nodiscard]] QString albumGroupingPattern() const;
    [[nodiscard]] QString logFileName() const;
    [[nodiscard]] bool writeAlbumDrForSingleTrack() const;

    void setAlbumGroupingPattern(const QString& pattern);
    void setLogFileName(const QString& fileName);
    void setWriteAlbumDrForSingleTrack(bool enabled);

    [[nodiscard]] static QString defaultAlbumGroupingPattern();
    [[nodiscard]] static QString defaultLogFileName();
    [[nodiscard]] static bool defaultWriteAlbumDrForSingleTrack() noexcept;

private:
    SettingsManager* m_settings{nullptr};
    QString m_albumGroupingPattern;
    QString m_logFileName;
    bool m_writeAlbumDrForSingleTrack{true};
};
} // namespace Fooyin::DrMeter
