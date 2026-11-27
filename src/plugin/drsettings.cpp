#include "plugin/drsettings.h"

#include <utils/settings/settingsmanager.h>

using namespace Qt::StringLiterals;

namespace Fooyin::DrMeter {
DrSettings::DrSettings(SettingsManager* settings)
    : m_settings{settings}
    , m_albumGroupingPattern{defaultAlbumGroupingPattern()}
    , m_logFileName{defaultLogFileName()}
    , m_writeAlbumDrForSingleTrack{defaultWriteAlbumDrForSingleTrack()}
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

void DrSettings::setAlbumGroupingPattern(const QString& pattern)
{
    const QString trimmed  = pattern.trimmed();
    m_albumGroupingPattern = trimmed.isEmpty() ? defaultAlbumGroupingPattern() : trimmed;
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
    if(trimmed.isEmpty() || trimmed == u"."_s || trimmed == u".."_s) {
        return defaultLogFileName();
    }

    for(const QChar ch : trimmed) {
        const ushort code = ch.unicode();
        if(ch == u'/' || ch == u'\\' || ch == u':' || code < 0x20U || code == 0x7FU) {
            return defaultLogFileName();
        }
    }

    return trimmed;
}

bool DrSettings::defaultWriteAlbumDrForSingleTrack() noexcept
{
    return true;
}
} // namespace Fooyin::DrMeter
