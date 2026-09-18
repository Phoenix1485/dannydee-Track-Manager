#include "Database.h"
#include "PlaylistImporter.h"

#include <QCoreApplication>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QUuid>

#include <iostream>

namespace {
bool require(bool condition, const char *message)
{
    if (condition) return true;
    std::cerr << message << '\n';
    return false;
}
}

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    QStandardPaths::setTestModeEnabled(true);
    QCoreApplication::setOrganizationName(QStringLiteral("DannyDeeTests"));
    QCoreApplication::setApplicationName(QUuid::createUuid().toString(QUuid::WithoutBraces));

    const QByteArray spotifyJson = R"json([
      {"name":"First","artists":["Artist A"],"genres":["House"],
       "publisher":"Label A","date":"2026-01-02",
       "url":"https://open.spotify.com/track/one","list_name":"My Spotify List",
       "list_position":1,"list_length":2},
      {"name":"Second","artist":"Artist B","genres":[],
       "url":"https://open.spotify.com/track/two","list_name":"My Spotify List",
       "list_position":2,"list_length":2}
    ])json";
    PlaylistImportResult spotify;
    QString error;
    if (!require(PlaylistImporter::parseSpotDlData(
                     spotifyJson, QUrl(QStringLiteral("https://open.spotify.com/playlist/list")),
                     QStringLiteral("Spotify"), &spotify, &error),
                 "Spotify playlist JSON did not parse") ||
        !require(spotify.isCollection && spotify.name == QStringLiteral("My Spotify List")
                     && spotify.tracks.size() == 2,
                 "Spotify playlist fields are incorrect")) return 1;

    const QByteArray soundCloudJson = R"json({
      "_type":"playlist","title":"My SoundCloud Set","entries":[
        {"title":"SC One","uploader":"DJ One","webpage_url":"https://soundcloud.com/dj/one","playlist_index":1},
        {"title":"SC Two","uploader":"DJ Two","webpage_url":"https://soundcloud.com/dj/two","playlist_index":2}
      ]
    })json";
    PlaylistImportResult soundCloud;
    if (!require(PlaylistImporter::parseYtDlpData(
                     soundCloudJson, QUrl(QStringLiteral("https://soundcloud.com/dj/sets/test")),
                     QStringLiteral("SoundCloud"), &soundCloud, &error),
                 "SoundCloud playlist JSON did not parse") ||
        !require(soundCloud.isCollection && soundCloud.name == QStringLiteral("My SoundCloud Set")
                     && soundCloud.tracks.size() == 2
                     && soundCloud.tracks.first().sourceUrl == QStringLiteral("https://soundcloud.com/dj/one"),
                 "SoundCloud playlist fields are incorrect")) return 1;

    Database database;
    if (!require(database.open(), "Test database could not be opened")) return 1;
    const int firstTrack = database.addTrackReturningId(
        "First", "Artist A", "House", 0, {}, 5, {}, {},
        "https://open.spotify.com/track/one", {}, false);
    const int secondTrack = database.addTrackReturningId(
        "Second", "Artist B", "Other", 0, {}, 5, {}, {},
        "https://open.spotify.com/track/two", {}, false);
    const int sourcePlaylist = database.ensurePlaylist(
        "Imported", "https://open.spotify.com/playlist/list", "Spotify");
    const int targetPlaylist = database.ensurePlaylist("Target");
    if (!require(firstTrack > 0 && secondTrack > 0 && sourcePlaylist > 0 && targetPlaylist > 0,
                 "Database fixtures could not be created") ||
        !require(database.updateBeatAnalysis(firstTrack, 124.5, 182.0, 0.91),
                 "Beat analysis could not be stored") ||
        !require(database.addToPlaylist(sourcePlaylist, firstTrack)
                     && database.addToPlaylist(sourcePlaylist, secondTrack),
                 "Tracks could not be copied into playlist") ||
        !require(database.moveTracksBetweenPlaylists(sourcePlaylist, targetPlaylist,
                                                     {firstTrack, secondTrack}),
                 "Tracks could not be moved between playlists")) return 1;

    QSqlQuery count(database.connection());
    count.prepare("SELECT COUNT(*) FROM playlist_tracks WHERE playlist_id=?");
    count.addBindValue(sourcePlaylist);
    if (!require(count.exec() && count.next() && count.value(0).toInt() == 0,
                 "Source playlist still contains moved tracks")) return 1;
    count.finish();
    count.prepare("SELECT COUNT(*) FROM playlist_tracks WHERE playlist_id=?");
    count.addBindValue(targetPlaylist);
    if (!require(count.exec() && count.next() && count.value(0).toInt() == 2,
                 "Target playlist does not contain moved tracks")) return 1;
    count.finish();
    count.prepare("SELECT bpm,beatgrid_offset_ms,beatgrid_confidence,beatgrid_analyzed "
                  "FROM tracks WHERE id=?");
    count.addBindValue(firstTrack);
    if (!require(count.exec() && count.next()
                     && qAbs(count.value(0).toDouble() - 124.5) < 0.001
                     && qAbs(count.value(1).toDouble() - 182.0) < 0.001
                     && qAbs(count.value(2).toDouble() - 0.91) < 0.001
                     && count.value(3).toBool(),
                 "Stored beatgrid data is incorrect")) return 1;

    return 0;
}
