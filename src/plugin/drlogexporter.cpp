#include "plugin/drlogexporter.h"

#include "core/drversion.h"
#include "plugin/drsettings.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QLocale>
#include <QSaveFile>

#include <unordered_map>

using namespace Qt::StringLiterals;

namespace Fooyin::DrMeter {
LogFormatOptions DrLogExporter::buildLogFormatOptions(const QDateTime& timestamp)
{
    QString fooyinVer = QCoreApplication::applicationVersion().trimmed();
    if(fooyinVer.isEmpty()) {
#ifdef FOOYIN_SDK_VERSION
        fooyinVer = QString::fromLatin1(FOOYIN_SDK_VERSION).trimmed();
#endif
        if(fooyinVer.isEmpty()) {
            fooyinVer = QString::fromUtf8(MinFooyinVersion.data(), static_cast<qsizetype>(MinFooyinVersion.size()));
        }
    }

    const QDateTime effectiveTime = timestamp.isValid() ? timestamp : QDateTime::currentDateTime();
    const QString formattedDate   = QLocale::c().toString(effectiveTime, u"yyyy-MM-dd HH:mm:ss"_s);

    return LogFormatOptions{
        .fooyinVersion = fooyinVer.toStdString(),
        .pluginVersion = std::string{PluginVersion},
        .logDate       = formattedDate.toStdString(),
    };
}

std::vector<AlbumDrResult> DrLogExporter::collectValidAlbums(const std::vector<ScannedAlbum>& albums)
{
    std::vector<AlbumDrResult> valid;
    valid.reserve(albums.size());

    for(const auto& scanned : albums) {
        if(!scanned.album.tracks.empty()) {
            valid.push_back(scanned.album);
        }
    }

    return valid;
}

std::vector<LogWriteTarget> DrLogExporter::planDirectoryLogWrites(const std::vector<ScannedAlbum>& albums,
                                                                  const QString& logFileName,
                                                                  const LogFormatOptions& options)
{
    const QString effectiveName = DrSettings::normalizeLogFileName(logFileName);

    std::vector<LogWriteTarget> targets;
    std::unordered_map<QString, size_t> targetIndexByDir;

    for(const auto& scanned : albums) {
        if(scanned.album.tracks.empty()) {
            continue;
        }

        const QString trimmedDir = scanned.directory.trimmed();
        if(trimmedDir.isEmpty() || !QFileInfo{trimmedDir}.isAbsolute()) {
            continue;
        }
        const QString cleanDir = QDir::cleanPath(trimmedDir);

        const auto it = targetIndexByDir.find(cleanDir);
        if(it == targetIndexByDir.end()) {
            const size_t idx = targets.size();
            targetIndexByDir.emplace(cleanDir, idx);

            LogWriteTarget target;
            target.directory     = cleanDir;
            target.filePath      = QDir{cleanDir}.filePath(effectiveName);
            target.alreadyExists = QFileInfo::exists(target.filePath);
            target.albums.push_back(scanned.album);
            targets.push_back(std::move(target));
        }
        else {
            targets[it->second].albums.push_back(scanned.album);
        }
    }

    for(auto& target : targets) {
        target.content = DrLogFormatter::formatMultiAlbumLog(target.albums, options);
    }

    return targets;
}

bool DrLogExporter::writeSingleLogFile(const QString& filePath, const std::string& content, QString* errorOut)
{
    const QFileInfo info{filePath};
    if(info.isDir()) {
        if(errorOut) {
            *errorOut = u"%1: Target path is a directory"_s.arg(filePath);
        }
        return false;
    }

    const QDir parentDir = info.absoluteDir();
    if(!parentDir.exists() && !parentDir.mkpath(u"."_s)) {
        if(errorOut) {
            *errorOut = u"Could not create directory: %1"_s.arg(parentDir.absolutePath());
        }
        return false;
    }

    QSaveFile file{filePath};
    if(!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if(errorOut) {
            *errorOut = u"%1: %2"_s.arg(filePath, file.errorString());
        }
        return false;
    }

    const qint64 expected = static_cast<qint64>(content.size());
    const qint64 written  = file.write(content.data(), expected);
    if(written != expected) {
        file.cancelWriting();
        if(errorOut) {
            *errorOut = u"%1: %2"_s.arg(filePath, file.errorString());
        }
        return false;
    }

    if(!file.commit()) {
        if(errorOut) {
            *errorOut = u"%1: %2"_s.arg(filePath, file.errorString());
        }
        return false;
    }

    return true;
}

LogWriteOutcome DrLogExporter::writePlannedLogs(const std::vector<LogWriteTarget>& targets)
{
    LogWriteOutcome outcome;
    for(const auto& target : targets) {
        QString err;
        if(writeSingleLogFile(target.filePath, target.content, &err)) {
            ++outcome.writtenFiles;
            outcome.writtenPaths.push_back(target.filePath);
        }
        else {
            ++outcome.failedFiles;
            outcome.errors.push_back(err);
        }
    }
    return outcome;
}
} // namespace Fooyin::DrMeter
