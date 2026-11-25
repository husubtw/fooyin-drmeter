#pragma once

#include "core/drlogformatter.h"
#include "plugin/drscanner.h"

#include <QDateTime>
#include <QString>
#include <QStringList>

#include <string>
#include <vector>

namespace Fooyin::DrMeter {
struct LogWriteTarget
{
    QString filePath;
    QString directory;
    bool alreadyExists{false};
    std::vector<AlbumDrResult> albums;
    std::string content;
};

struct LogWriteOutcome
{
    int writtenFiles{0};
    int failedFiles{0};
    QStringList writtenPaths;
    QStringList errors;
};

class DrLogExporter
{
public:
    [[nodiscard]] static LogFormatOptions buildLogFormatOptions(const QDateTime& timestamp
                                                                = QDateTime::currentDateTime());
    [[nodiscard]] static std::vector<AlbumDrResult> collectValidAlbums(const std::vector<ScannedAlbum>& albums);
    [[nodiscard]] static std::vector<LogWriteTarget> planDirectoryLogWrites(const std::vector<ScannedAlbum>& albums,
                                                                            const QString& logFileName,
                                                                            const LogFormatOptions& options);
    [[nodiscard]] static bool writeSingleLogFile(const QString& filePath, const std::string& content,
                                                 QString* errorOut = nullptr);
    [[nodiscard]] static LogWriteOutcome writePlannedLogs(const std::vector<LogWriteTarget>& targets);
};
} // namespace Fooyin::DrMeter
