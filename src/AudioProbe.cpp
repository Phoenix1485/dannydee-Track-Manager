#include "AudioProbe.h"
#include "ToolLocator.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QStandardPaths>

namespace {
QString tag(const QJsonObject &tags, const QStringList &names)
{
    for (auto it = tags.begin(); it != tags.end(); ++it) {
        for (const QString &name : names) {
            if (it.key().compare(name, Qt::CaseInsensitive) == 0) return it.value().toVariant().toString();
        }
    }
    return {};
}
}

ProbedTrack AudioProbe::read(const QString &filePath)
{
    ProbedTrack result;
    result.title = QFileInfo(filePath).completeBaseName();
    const QString probe = ToolLocator::find("ffprobe");
    if (probe.isEmpty()) return result;

    QProcess process;
    process.start(probe, {"-v", "error", "-print_format", "json", "-show_format", "-show_streams", filePath});
    if (!process.waitForFinished(15000) || process.exitCode() != 0) return result;
    const QJsonObject root = QJsonDocument::fromJson(process.readAllStandardOutput()).object();
    QJsonObject tags = root.value("format").toObject().value("tags").toObject();
    if (tags.isEmpty()) {
        const QJsonArray streams = root.value("streams").toArray();
        for (const auto &stream : streams) {
            const QJsonObject candidate = stream.toObject().value("tags").toObject();
            if (!candidate.isEmpty()) { tags = candidate; break; }
        }
    }
    const QString title = tag(tags, {"title"});
    if (!title.isEmpty()) result.title = title;
    result.artist = tag(tags, {"artist", "album_artist", "albumartist"});
    result.genre = tag(tags, {"genre"});
    result.musicalKey = tag(tags, {"initialkey", "key", "musical_key"});
    result.label = tag(tags, {"label", "publisher", "organization"});
    result.releaseDate = tag(tags, {"date", "year", "release_date"});
    bool ok = false;
    result.bpm = tag(tags, {"bpm", "tbpm", "tempo"}).toDouble(&ok);
    if (!ok) result.bpm = 0;
    return result;
}
