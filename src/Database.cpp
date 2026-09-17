#include "Database.h"

#include <QDir>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QStringList>
#include <QVariant>

Database::Database()
    : m_db(QSqlDatabase::addDatabase("QSQLITE"))
{
}

bool Database::open()
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dir);
    m_db.setDatabaseName(dir + "/dannydee.sqlite");
    if (!m_db.open()) {
        m_lastError = m_db.lastError().text();
        return false;
    }
    return migrate();
}

bool Database::migrate()
{
    QSqlQuery q(m_db);
    const QStringList statements = {
        "PRAGMA foreign_keys = ON",
        "CREATE TABLE IF NOT EXISTS tracks ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT, title TEXT NOT NULL, artist TEXT NOT NULL, "
        "genre TEXT, bpm REAL DEFAULT 0, musical_key TEXT, energy INTEGER DEFAULT 5 "
        "CHECK(energy BETWEEN 1 AND 10), label TEXT, release_date TEXT, source_url TEXT, "
        "local_path TEXT, licensed INTEGER NOT NULL DEFAULT 0, created_at TEXT DEFAULT CURRENT_TIMESTAMP)",
        "CREATE TABLE IF NOT EXISTS playlists (id INTEGER PRIMARY KEY AUTOINCREMENT, "
        "name TEXT NOT NULL UNIQUE, created_at TEXT DEFAULT CURRENT_TIMESTAMP)",
        "CREATE TABLE IF NOT EXISTS playlist_tracks (playlist_id INTEGER NOT NULL, "
        "track_id INTEGER NOT NULL, position INTEGER NOT NULL DEFAULT 0, "
        "PRIMARY KEY(playlist_id, track_id), "
        "FOREIGN KEY(playlist_id) REFERENCES playlists(id) ON DELETE CASCADE, "
        "FOREIGN KEY(track_id) REFERENCES tracks(id) ON DELETE CASCADE)",
        "CREATE INDEX IF NOT EXISTS idx_tracks_genre_bpm ON tracks(genre, bpm)",
        "CREATE INDEX IF NOT EXISTS idx_tracks_artist_title ON tracks(artist, title)"
    };
    for (const auto &sql : statements) {
        if (!q.exec(sql)) {
            m_lastError = q.lastError().text();
            return false;
        }
    }
    return true;
}

bool Database::addTrack(const QString &title, const QString &artist, const QString &genre,
                        double bpm, const QString &key, int energy, const QString &label,
                        const QString &releaseDate, const QString &sourceUrl,
                        const QString &localPath, bool licensed)
{
    QSqlQuery q(m_db);
    q.prepare("INSERT INTO tracks(title,artist,genre,bpm,musical_key,energy,label,release_date,"
              "source_url,local_path,licensed) VALUES(?,?,?,?,?,?,?,?,?,?,?)");
    const QVariantList values{title, artist, genre, bpm, key, energy, label, releaseDate,
                              sourceUrl, localPath, licensed ? 1 : 0};
    for (const auto &value : values) q.addBindValue(value);
    if (!q.exec()) m_lastError = q.lastError().text();
    return q.isActive();
}

bool Database::addPlaylist(const QString &name)
{
    QSqlQuery q(m_db);
    q.prepare("INSERT INTO playlists(name) VALUES(?)");
    q.addBindValue(name);
    if (!q.exec()) m_lastError = q.lastError().text();
    return q.isActive();
}

bool Database::addToPlaylist(int playlistId, int trackId)
{
    QSqlQuery q(m_db);
    q.prepare("INSERT OR IGNORE INTO playlist_tracks(playlist_id,track_id,position) "
              "VALUES(?,?,COALESCE((SELECT MAX(position)+1 FROM playlist_tracks WHERE playlist_id=?),0))");
    q.addBindValue(playlistId);
    q.addBindValue(trackId);
    q.addBindValue(playlistId);
    if (!q.exec()) m_lastError = q.lastError().text();
    return q.isActive();
}
