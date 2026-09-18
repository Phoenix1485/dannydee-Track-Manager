#include "LinkResolver.h"

#include <QRegularExpression>

ResolvedLink LinkResolver::resolve(const QString &input)
{
    const QUrl url = QUrl::fromUserInput(input.trimmed());
    const QString host = url.host().toLower();
    ResolvedLink result{"Unbekannt", "Link", url, false,
                        "Der Link wird über den generischen yt-dlp-Resolver versucht."};

    if (host.contains("beatport.com")) {
        result = {"Beatport", "Store/Track", url, false,
                  "Gekaufte Datei über Beatport laden und anschließend lokal importieren."};
    } else if (host.contains("beatsource.com")) {
        result = {"Beatsource", "Store/Streaming", url, false,
                  "Der Link wird über den generischen yt-dlp-Resolver versucht."};
    } else if (host.contains("spotify.com") || host == "spotify.link") {
        result = {"Spotify", "Streaming", url, false,
                  "Spotify-Metadaten werden über spotDL eingelesen; Audio wird über passende externe Quellen aufgelöst."};
    } else if (host.contains("soundcloud.com")) {
        result = {"SoundCloud", "Streaming", url, false,
                  "Öffentlich zugängliche Tracks werden ohne API-Schlüssel über yt-dlp verarbeitet."};
    } else if (host.contains("youtube.com") || host == "youtu.be" || host.contains("music.youtube.com")) {
        result = {"YouTube", "Video/Streaming", url, false,
                  "Öffentlich zugängliche Medien werden ohne API-Schlüssel über yt-dlp verarbeitet."};
    } else if (host.contains("tiktok.com")) {
        result = {"TikTok", "Video/Streaming", url, false,
                  "Öffentlich zugängliche Medien werden über yt-dlp verarbeitet."};
    } else if (host.contains("bandcamp.com")) {
        result = {"Bandcamp", "Musik/Store", url, false,
                  "Öffentlich zugängliche Medien werden über yt-dlp verarbeitet."};
    } else if (host.contains("soundbeaver")) {
        result = {"SoundBeaver", "Audio-Link", url, false,
                  "Der Link wird über den generischen yt-dlp-Resolver versucht."};
    } else if (host.contains("dj.studio")) {
        result = {"DJ.Studio", "DJ-Projekt", url, false,
                  "Projekt/Track-Referenz speichern und Audio aus der lokalen Bibliothek zuordnen."};
    } else if (host.contains("beatsaver.com") || host.contains("beatleader.xyz")) {
        result = {"BeatSaver", "Beatmap", url, false,
                  "Der Link wird als Beatmap-Referenz gespeichert."};
    } else if (url.isLocalFile()) {
        result = {"Lokale Datei", "Audio", url, true,
                  "Lokale Audiodateien können importiert und konvertiert werden."};
    } else if (url.path().contains(QRegularExpression("\\.(flac|wav|aiff?|mp3|m4a|ogg)$",
               QRegularExpression::CaseInsensitiveOption))) {
        result = {"Direkte Audiodatei", "Download", url, true,
                  "Direkter Download und Konvertierung sind möglich."};
    }
    return result;
}
