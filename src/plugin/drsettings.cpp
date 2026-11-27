#include "plugin/drsettings.h"

#include <utils/settings/advancedsettingsregistry.h>
#include <utils/settings/settingsmanager.h>

#include <QCoreApplication>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace Fooyin::DrMeter {
namespace {
QString trimOrDefault(const QString& raw, const QString& fallback)
{
    QString trimmed = raw.trimmed();
    return trimmed.isEmpty() ? fallback : trimmed;
}

bool isSafeLogFileName(const QString& trimmed)
{
    if(trimmed.isEmpty() || trimmed == u"."_s || trimmed == u".."_s) {
        return false;
    }

    return std::ranges::all_of(trimmed, [](const QChar ch) {
        const ushort code = ch.unicode();
        return ch != u'/' && ch != u'\\' && ch != u':' && code >= 0x20U && code != 0x7FU;
    });
}
} // namespace

DrSettings::DrSettings(SettingsManager* settings)
    : m_settings{settings}
    , m_albumGroupingPattern{defaultAlbumGroupingPattern()}
    , m_logFileName{defaultLogFileName()}
    , m_writeAlbumDrForSingleTrack{defaultWriteAlbumDrForSingleTrack()}
    , m_verboseDialog{defaultVerboseDialog()}
{ }

QString DrSettings::albumGroupingPattern() const
{
    if(m_settings) {
        const QString val
            = m_settings->fileValue(QLatin1StringView{SettingKeys::AlbumGroupingPattern}, defaultAlbumGroupingPattern())
                  .toString()
                  .trimmed();
        if(!val.isEmpty()) {
            return val;
        }
    }
    return m_albumGroupingPattern;
}

QString DrSettings::logFileName() const
{
    if(m_settings) {
        return normalizeLogFileName(
            m_settings->fileValue(QLatin1StringView{SettingKeys::LogFileName}, defaultLogFileName()).toString());
    }
    return m_logFileName;
}

bool DrSettings::writeAlbumDrForSingleTrack() const
{
    if(m_settings) {
        return m_settings
            ->fileValue(QLatin1StringView{SettingKeys::WriteAlbumDrForSingleTrack}, defaultWriteAlbumDrForSingleTrack())
            .toBool();
    }
    return m_writeAlbumDrForSingleTrack;
}

bool DrSettings::verboseDialog() const
{
    if(m_settings) {
        return m_settings->fileValue(QLatin1StringView{SettingKeys::VerboseDialog}, defaultVerboseDialog()).toBool();
    }
    return m_verboseDialog;
}

void DrSettings::setAlbumGroupingPattern(const QString& pattern)
{
    m_albumGroupingPattern = trimOrDefault(pattern, defaultAlbumGroupingPattern());
    if(m_settings) {
        m_settings->fileSet(QLatin1StringView{SettingKeys::AlbumGroupingPattern}, m_albumGroupingPattern);
    }
}

void DrSettings::setLogFileName(const QString& fileName)
{
    m_logFileName = normalizeLogFileName(fileName);
    if(m_settings) {
        m_settings->fileSet(QLatin1StringView{SettingKeys::LogFileName}, m_logFileName);
    }
}

void DrSettings::setWriteAlbumDrForSingleTrack(bool enabled)
{
    m_writeAlbumDrForSingleTrack = enabled;
    if(m_settings) {
        m_settings->fileSet(QLatin1StringView{SettingKeys::WriteAlbumDrForSingleTrack}, enabled);
    }
}

void DrSettings::setVerboseDialog(bool enabled)
{
    m_verboseDialog = enabled;
    if(m_settings) {
        m_settings->fileSet(QLatin1StringView{SettingKeys::VerboseDialog}, enabled);
    }
}

QString DrSettings::defaultAlbumGroupingPattern()
{
    return u"%album artist% | %date% | %album% | %discnumber%"_s;
}

QString DrSettings::defaultLogFileName()
{
    return u"foo_dr.txt"_s;
}

QString DrSettings::normalizeLogFileName(const QString& fileName)
{
    QString trimmed = fileName.trimmed();
    if(!isSafeLogFileName(trimmed)) {
        return defaultLogFileName();
    }

    return trimmed;
}

QString DrSettings::validateLogFileName(const QString& fileName)
{
    const QString trimmed = fileName.trimmed();
    if(trimmed.isEmpty() || isSafeLogFileName(trimmed)) {
        return {};
    }

    return QCoreApplication::translate(
        "DrSettings",
        "Log file name cannot be '.' or '..' and must not contain path separators ('/', '\\'), ':', or control "
        "characters.");
}

bool DrSettings::defaultWriteAlbumDrForSingleTrack() noexcept
{
    return true;
}

bool DrSettings::defaultVerboseDialog() noexcept
{
    return true;
}

void DrSettings::registerAdvancedSettings(AdvancedSettingsRegistry* registry, SettingsManager* settings)
{
    if(!registry || !settings) {
        return;
    }

    const QStringList category{
        QCoreApplication::translate("DrSettings", "Tools"),
        QCoreApplication::translate("DrSettings", "DR Meter"),
    };

    registry->add(AdvancedSettingDescriptor{
        .id       = QString::fromLatin1(SettingKeys::AlbumGroupingPattern),
        .category = category,
        .label    = QCoreApplication::translate("DrSettings", "Album grouping pattern"),
        .description
        = QCoreApplication::translate("DrSettings", "Fooyin title formatting pattern used to group tracks into albums. "
                                                    "Empty values revert to the default pattern."),
        .defaultValue = defaultAlbumGroupingPattern(),
        .editor       = AdvancedSettingLineEdit{},
        .read         = [settings] { return DrSettings{settings}.albumGroupingPattern(); },
        .write =
            [settings](const QVariant& value) {
                DrSettings drSettings{settings};
                drSettings.setAlbumGroupingPattern(value.toString());
                return true;
            },
        .normalise =
            [](const QVariant& value) {
                return QVariant{trimOrDefault(value.toString(), defaultAlbumGroupingPattern())};
            },
        .validate = {},
    });

    registry->add(AdvancedSettingDescriptor{
        .id       = QString::fromLatin1(SettingKeys::LogFileName),
        .category = category,
        .label    = QCoreApplication::translate("DrSettings", "Log file name"),
        .description
        = QCoreApplication::translate("DrSettings", "Default file name for exported DR logs (e.g., foo_dr.txt). "
                                                    "Must be a plain file name without path separators."),
        .defaultValue = defaultLogFileName(),
        .editor       = AdvancedSettingLineEdit{},
        .read         = [settings] { return DrSettings{settings}.logFileName(); },
        .write =
            [settings](const QVariant& value) {
                DrSettings drSettings{settings};
                drSettings.setLogFileName(value.toString());
                return true;
            },
        .normalise
        = [](const QVariant& value) { return QVariant{trimOrDefault(value.toString(), defaultLogFileName())}; },
        .validate = [](const QVariant& value) { return validateLogFileName(value.toString()); },
    });

    registry->add(AdvancedSettingDescriptor{
        .id          = QString::fromLatin1(SettingKeys::WriteAlbumDrForSingleTrack),
        .category    = category,
        .label       = QCoreApplication::translate("DrSettings", "Write ALBUM DYNAMIC RANGE for single-track albums"),
        .description = QCoreApplication::translate(
            "DrSettings", "When enabled, Update File Tags writes ALBUM DYNAMIC RANGE even for 1-track album groups."),
        .defaultValue = defaultWriteAlbumDrForSingleTrack(),
        .editor       = AdvancedSettingCheckBox{},
        .read         = [settings] { return DrSettings{settings}.writeAlbumDrForSingleTrack(); },
        .write =
            [settings](const QVariant& value) {
                DrSettings drSettings{settings};
                drSettings.setWriteAlbumDrForSingleTrack(value.toBool());
                return true;
            },
        .normalise = {},
        .validate  = {},
    });

    registry->add(AdvancedSettingDescriptor{
        .id          = QString::fromLatin1(SettingKeys::VerboseDialog),
        .category    = category,
        .label       = QCoreApplication::translate("DrSettings", "Verbose results dialog"),
        .description = QCoreApplication::translate(
            "DrSettings", "Show per-channel DR, RMS, and Peak columns in the DR Meter Results dialog."),
        .defaultValue = defaultVerboseDialog(),
        .editor       = AdvancedSettingCheckBox{},
        .read         = [settings] { return DrSettings{settings}.verboseDialog(); },
        .write =
            [settings](const QVariant& value) {
                DrSettings drSettings{settings};
                drSettings.setVerboseDialog(value.toBool());
                return true;
            },
        .normalise = {},
        .validate  = {},
    });
}
} // namespace Fooyin::DrMeter
