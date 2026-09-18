#include "PlaylistImporter.h"

#include "ToolLocator.h"

#include <QDir>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcessEnvironment>
#include <QSet>

namespace {
QString firstText(const QJsonObject &object, std::initializer_list<const char *> keys)
{
    for (const char *key : keys) {
        const QString value = object.value(QLatin1String(key)).toString().trimmed();
        if (!value.isEmpty()) return value;
    }
    return {};
}

QString joinedStrings(const QJsonValue &value)
{
    if (value.isString()) return value.toString().trimmed();
    QStringList strings;
    for (const QJsonValue &item : value.toArray()) {
        const QString text = item.toString().trimmed();
        if (!text.isEmpty()) strings << text;
    }
    return strings.join(QStringLiteral(", "));
}

QString normalizedDate(QString value)
{
    value = value.trimmed();
    if (value.size() == 8 && value.indexOf('-') < 0)
        return value.mid(0, 4) + '-' + value.mid(4, 2) + '-' + value.mid(6, 2);
    return value;
}

QUrl canonicalSpotifyUrl(const QUrl &url)
{
    if (!url.host().contains(QStringLiteral("spotify.com"), Qt::CaseInsensitive)) return url;
    const QStringList segments = url.path().split('/', Qt::SkipEmptyParts);
    static const QSet<QString> types{"track", "album", "playlist", "artist", "episode", "show"};
    for (int index = 0; index + 1 < segments.size(); ++index) {
        if (!types.contains(segments.at(index).toLower())) continue;
        QUrl canonical(QStringLiteral("https://open.spotify.com"));
        canonical.setPath('/' + segments.at(index).toLower() + '/' + segments.at(index + 1));
        return canonical;
    }
    return url;
}
}

PlaylistImporter::PlaylistImporter(QObject *parent) : QObject(parent)
{
    qRegisterMetaType<PlaylistImportResult>();
    m_process.setProcessChannelMode(QProcess::SeparateChannels);
    m_timeout.setSingleShot(true);
    m_timeout.setInterval(180000);
    connect(&m_timeout, &QTimer::timeout, this, [this] {
        if (!m_active) return;
        m_process.kill();
        fail(QStringLiteral("Zeitüberschreitung beim Lesen der Playlist."));
    });
    connect(&m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart && m_active)
            fail(QStringLiteral("Das Metadaten-Werkzeug konnte nicht gestartet werden: %1")
                     .arg(m_process.errorString()));
    });
    connect(&m_process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
            this, &PlaylistImporter::finish);
}

bool PlaylistImporter::isBusy() const
{
    return m_active;
}

void PlaylistImporter::resolve(const QUrl &url, const QString &provider)
{
    if (m_active) return;
    m_requestedUrl = url;
    m_provider = provider;
    m_spotify = url.host().contains(QStringLiteral("spotify.com"), Qt::CaseInsensitive)
                || url.host().compare(QStringLiteral("spotify.link"), Qt::CaseInsensitive) == 0;

    const QString tool = ToolLocator::find(m_spotify ? QStringLiteral("spotdl")
                                                      : QStringLiteral("yt-dlp"));
    if (tool.isEmpty()) {
        emit failed(url, provider,
                    QStringLiteral("%1 wurde nicht gefunden.").arg(m_spotify ? "spotDL" : "yt-dlp"));
        return;
    }

    QStringList arguments;
    if (m_spotify) {
        m_spotDlFile = m_temporaryDirectory.filePath(QStringLiteral("playlist-%1.spotdl")
                                                          .arg(QDateTime::currentMSecsSinceEpoch()));
        arguments << QStringLiteral("save")
                  << canonicalSpotifyUrl(url).toString(QUrl::FullyEncoded)
                  << QStringLiteral("--save-file") << m_spotDlFile
                  << QStringLiteral("--headless") << QStringLiteral("--simple-tui")
                  << QStringLiteral("--log-level") << QStringLiteral("ERROR");
    } else {
        arguments << QStringLiteral("--ignore-config") << QStringLiteral("--flat-playlist")
                  << QStringLiteral("--dump-single-json") << QStringLiteral("--no-warnings")
                  << QStringLiteral("--skip-download") << QStringLiteral("--")
                  << url.toString(QUrl::FullyEncoded);
    }

    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("PATH"), QFileInfo(tool).absolutePath()
                       + QDir::listSeparator() + environment.value(QStringLiteral("PATH")));
    environment.insert(QStringLiteral("PYTHONIOENCODING"), QStringLiteral("utf-8"));
    m_process.setProcessEnvironment(environment);
    m_active = true;
    emit progress(QStringLiteral("Playlist wird gelesen: %1").arg(url.toString()));
    m_timeout.start();
    m_process.start(tool, arguments);
}

void PlaylistImporter::finish(int exitCode, QProcess::ExitStatus exitStatus)
{
    if (!m_active) return;
    m_timeout.stop();
    const QByteArray standardOutput = m_process.readAllStandardOutput();
    const QString diagnostics = QString::fromUtf8(m_process.readAllStandardError()).trimmed();
    if (exitStatus != QProcess::NormalExit || exitCode != 0) {
        fail(diagnostics.isEmpty()
                 ? QStringLiteral("Die Playlist konnte nicht gelesen werden (Code %1).").arg(exitCode)
                 : diagnostics.right(1200));
        return;
    }

    QByteArray data = standardOutput;
    if (m_spotify) {
        QFile file(m_spotDlFile);
        if (!file.open(QIODevice::ReadOnly)) {
            fail(QStringLiteral("spotDL hat keine lesbare Playlist-Datei erzeugt."));
            return;
        }
        data = file.readAll();
        file.remove();
    }

    PlaylistImportResult result;
    QString parseError;
    const bool parsed = m_spotify
        ? parseSpotDlData(data, m_requestedUrl, m_provider, &result, &parseError)
        : parseYtDlpData(data, m_requestedUrl, m_provider, &result, &parseError);
    if (!parsed) {
        fail(parseError);
        return;
    }

    m_active = false;
    emit resolved(result);
}

void PlaylistImporter::fail(const QString &message)
{
    if (!m_active) return;
    m_timeout.stop();
    m_active = false;
    emit failed(m_requestedUrl, m_provider, message);
}

bool PlaylistImporter::looksLikeCollection(const QUrl &url)
{
    const QString path = url.path().toLower();
    const QString host = url.host().toLower();
    return path.contains(QStringLiteral("/playlist/")) || path.contains(QStringLiteral("/album/"))
           || path.contains(QStringLiteral("/sets/")) || path.contains(QStringLiteral("/collection/"))
           || url.hasQuery() && url.query().contains(QStringLiteral("list="))
           || host.contains(QStringLiteral("bandcamp.com")) && path.contains(QStringLiteral("/album/"));
}

bool PlaylistImporter::parseSpotDlData(const QByteArray &data, const QUrl &requestedUrl,
                                       const QString &provider, PlaylistImportResult *result,
                                       QString *error)
{
    QJsonParseError jsonError;
    const QJsonDocument document = QJsonDocument::fromJson(data, &jsonError);
    if (jsonError.error != QJsonParseError::NoError || !document.isArray()) {
        if (error) *error = QStringLiteral("Ungültige Spotify-Playlistdaten: %1").arg(jsonError.errorString());
        return false;
    }

    PlaylistImportResult parsed;
    parsed.requestedUrl = requestedUrl;
    parsed.provider = provider;
    for (const QJsonValue &value : document.array()) {
        const QJsonObject object = value.toObject();
        ImportedPlaylistTrack track;
        track.title = firstText(object, {"name", "title"});
        track.artist = joinedStrings(object.value(QStringLiteral("artists")));
        if (track.artist.isEmpty()) track.artist = firstText(object, {"artist", "album_artist"});
        track.genre = joinedStrings(object.value(QStringLiteral("genres")));
        track.label = firstText(object, {"publisher", "label"});
        track.releaseDate = normalizedDate(firstText(object, {"date", "release_date"}));
        track.sourceUrl = firstText(object, {"url"});
        track.position = object.value(QStringLiteral("list_position")).toInt(parsed.tracks.size() + 1);
        const QUrl source(track.sourceUrl);
        if (track.title.isEmpty() || !source.isValid() || source.scheme() != QStringLiteral("https")) continue;
        if (track.artist.isEmpty()) track.artist = provider;
        if (track.genre.isEmpty()) track.genre = QStringLiteral("Other");
        if (parsed.name.isEmpty()) parsed.name = firstText(object, {"list_name", "album_name"});
        parsed.tracks << track;
    }
    if (parsed.tracks.isEmpty()) {
        if (error) *error = QStringLiteral("Spotify hat keine importierbaren Tracks zurückgegeben.");
        return false;
    }
    parsed.isCollection = parsed.tracks.size() > 1 || looksLikeCollection(requestedUrl);
    if (parsed.name.isEmpty()) parsed.name = parsed.isCollection ? QStringLiteral("Spotify Playlist")
                                                                 : parsed.tracks.first().title;
    *result = parsed;
    return true;
}

bool PlaylistImporter::parseYtDlpData(const QByteArray &data, const QUrl &requestedUrl,
                                      const QString &provider, PlaylistImportResult *result,
                                      QString *error)
{
    QJsonParseError jsonError;
    const QJsonDocument document = QJsonDocument::fromJson(data, &jsonError);
    if (jsonError.error != QJsonParseError::NoError || !document.isObject()) {
        if (error) *error = QStringLiteral("Ungültige Playlistdaten: %1").arg(jsonError.errorString());
        return false;
    }

    const QJsonObject root = document.object();
    PlaylistImportResult parsed;
    parsed.requestedUrl = requestedUrl;
    parsed.provider = provider;
    parsed.name = firstText(root, {"title", "playlist_title", "album"});
    const QJsonArray entries = root.value(QStringLiteral("entries")).toArray();
    const bool hasEntries = !entries.isEmpty();

    auto appendTrack = [&](const QJsonObject &object, int fallbackPosition) {
        ImportedPlaylistTrack track;
        track.title = firstText(object, {"title", "track", "fulltitle"});
        track.artist = firstText(object, {"artist", "creator", "uploader", "channel"});
        track.genre = firstText(object, {"genre"});
        track.label = firstText(object, {"uploader", "channel"});
        track.releaseDate = normalizedDate(firstText(object, {"release_date", "upload_date"}));
        track.sourceUrl = firstText(object, {"webpage_url", "original_url", "url"});
        track.position = object.value(QStringLiteral("playlist_index")).toInt(fallbackPosition);
        QUrl source(track.sourceUrl);
        if (!source.isValid() || (source.scheme() != QStringLiteral("http")
                                  && source.scheme() != QStringLiteral("https"))) {
            if (!hasEntries) track.sourceUrl = requestedUrl.toString(QUrl::FullyEncoded);
            else return;
        }
        if (track.title.isEmpty()) {
            track.title = source.path().section('/', -1);
            track.title.replace('-', ' ');
            track.title.replace('_', ' ');
            if (track.title.isEmpty()) track.title = QStringLiteral("Unbekannter Track");
        }
        if (track.artist.isEmpty()) {
            track.artist = source.path().section('/', 1, 1);
            track.artist.replace('-', ' ');
            track.artist.replace('_', ' ');
            if (track.artist.isEmpty()) track.artist = provider;
        }
        if (track.genre.isEmpty()) track.genre = QStringLiteral("Other");
        parsed.tracks << track;
    };

    if (hasEntries) {
        int position = 1;
        for (const QJsonValue &entry : entries) appendTrack(entry.toObject(), position++);
    } else {
        appendTrack(root, 1);
    }
    if (parsed.tracks.isEmpty()) {
        if (error) *error = QStringLiteral("Die Adresse enthält keine importierbaren Tracks.");
        return false;
    }
    parsed.isCollection = hasEntries || parsed.tracks.size() > 1 || looksLikeCollection(requestedUrl);
    if (parsed.name.isEmpty()) parsed.name = parsed.isCollection
        ? provider + QStringLiteral(" Playlist") : parsed.tracks.first().title;
    *result = parsed;
    return true;
}
