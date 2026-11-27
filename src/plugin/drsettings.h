#pragma once

#include <QString>

namespace Fooyin {
class AdvancedSettingsRegistry;
class SettingsManager;
} // namespace Fooyin

namespace Fooyin::DrMeter {
namespace SettingKeys {
inline constexpr auto AlbumGroupingPattern       = "DrMeter/AlbumGroupingPattern";
inline constexpr auto LogFileName                = "DrMeter/LogFileName";
inline constexpr auto WriteAlbumDrForSingleTrack = "DrMeter/WriteAlbumDrForSingleTrack";
inline constexpr auto VerboseDialog              = "DrMeter/VerboseDialog";
} // namespace SettingKeys

class DrSettings
{
public:
    explicit DrSettings(SettingsManager* settings = nullptr);

    [[nodiscard]] QString albumGroupingPattern() const;
    [[nodiscard]] QString logFileName() const;
    [[nodiscard]] bool writeAlbumDrForSingleTrack() const;
    [[nodiscard]] bool verboseDialog() const;

    void setAlbumGroupingPattern(const QString& pattern);
    void setLogFileName(const QString& fileName);
    void setWriteAlbumDrForSingleTrack(bool enabled);
    void setVerboseDialog(bool enabled);

    [[nodiscard]] static QString defaultAlbumGroupingPattern();
    [[nodiscard]] static QString defaultLogFileName();
    [[nodiscard]] static QString normalizeLogFileName(const QString& fileName);
    [[nodiscard]] static QString validateLogFileName(const QString& fileName);
    [[nodiscard]] static bool defaultWriteAlbumDrForSingleTrack() noexcept;
    [[nodiscard]] static bool defaultVerboseDialog() noexcept;

    static void registerAdvancedSettings(AdvancedSettingsRegistry* registry, SettingsManager* settings);

private:
    SettingsManager* m_settings{nullptr};
    QString m_albumGroupingPattern;
    QString m_logFileName;
    bool m_writeAlbumDrForSingleTrack{true};
    bool m_verboseDialog{true};
};
} // namespace Fooyin::DrMeter
