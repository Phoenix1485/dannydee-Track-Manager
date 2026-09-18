#include "Database.h"

#include <QDir>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QStringList>
#include <QVariant>

#include <algorithm>

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
        "local_path TEXT, licensed INTEGER NOT NULL DEFAULT 0, created_at TEXT DEFAULT CURRENT_TIMESTAMP, "
        "beatgrid_offset_ms REAL DEFAULT 0, beatgrid_confidence REAL DEFAULT 0, "
        "beatgrid_analyzed INTEGER NOT NULL DEFAULT 0)",
        "CREATE TABLE IF NOT EXISTS playlists (id INTEGER PRIMARY KEY AUTOINCREMENT, "
        "name TEXT NOT NULL UNIQUE, source_url TEXT, provider TEXT, "
        "created_at TEXT DEFAULT CURRENT_TIMESTAMP)",
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

    auto ensureColumn = [this](const QString &table, const QString &column,
                               const QString &definition) {
        QSqlQuery columns(m_db);
        if (!columns.exec("PRAGMA table_info(" + table + ")")) return false;
        while (columns.next()) {
            if (columns.value(1).toString().compare(column, Qt::CaseInsensitive) == 0) return true;
        }
        QSqlQuery alter(m_db);
        if (!alter.exec(QString("ALTER TABLE %1 ADD COLUMN %2 %3")
                            .arg(table, column, definition))) {
            m_lastError = alter.lastError().text();
            return false;
        }
        return true;
    };
    if (!ensureColumn("tracks", "beatgrid_offset_ms", "REAL DEFAULT 0") ||
        !ensureColumn("tracks", "beatgrid_confidence", "REAL DEFAULT 0") ||
        !ensureColumn("tracks", "beatgrid_analyzed", "INTEGER NOT NULL DEFAULT 0") ||
        !ensureColumn("playlists", "source_url", "TEXT") ||
        !ensureColumn("playlists", "provider", "TEXT")) return false;

    if (!q.exec("CREATE UNIQUE INDEX IF NOT EXISTS idx_playlists_source_url "
                "ON playlists(source_url) WHERE source_url IS NOT NULL AND source_url <> ''")) {
        m_lastError = q.lastError().text();
        return false;
    }
    return true;
}

bool Database::addTrack(const QString &title, const QString &artist, const QString &genre,
                        double bpm, const QString &key, int energy, const QString &label,
                        const QString &releaseDate, const QString &sourceUrl,
                        const QString &localPath, bool licensed)
{
    return addTrackReturningId(title, artist, genre, bpm, key, energy, label,
                               releaseDate, sourceUrl, localPath, licensed) >= 0;
}

int Database::addTrackReturningId(const QString &title, const QString &artist, const QString &genre,
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
    if (!q.exec()) {
        m_lastError = q.lastError().text();
        return -1;
    }
    return q.lastInsertId().toInt();
}

int Database::findTrackBySourceUrl(const QString &sourceUrl) const
{
    if (sourceUrl.trimmed().isEmpty()) return -1;
    QSqlQuery q(m_db);
    q.prepare("SELECT id FROM tracks WHERE source_url=? ORDER BY id LIMIT 1");
    q.addBindValue(sourceUrl);
    return q.exec() && q.next() ? q.value(0).toInt() : -1;
}

bool Database::addPlaylist(const QString &name)
{
    return ensurePlaylist(name) >= 0;
}

int Database::ensurePlaylist(const QString &name, const QString &sourceUrl,
                             const QString &provider)
{
    if (!sourceUrl.trimmed().isEmpty()) {
        QSqlQuery existing(m_db);
        existing.prepare("SELECT id FROM playlists WHERE source_url=? LIMIT 1");
        existing.addBindValue(sourceUrl);
        if (existing.exec() && existing.next()) return existing.value(0).toInt();
    }

    QString candidate = name.trimmed();
    if (candidate.isEmpty()) candidate = provider.isEmpty() ? "Importierte Playlist" : provider + " Playlist";
    const QString baseName = candidate;
    int suffix = 2;
    while (true) {
        QSqlQuery existing(m_db);
        existing.prepare("SELECT id,source_url FROM playlists WHERE name=? LIMIT 1");
        existing.addBindValue(candidate);
        if (!existing.exec() || !existing.next()) break;
        if (sourceUrl.isEmpty() && existing.value(1).toString().isEmpty())
            return existing.value(0).toInt();
        candidate = QString("%1 (%2)").arg(baseName).arg(suffix++);
    }

    QSqlQuery insert(m_db);
    insert.prepare("INSERT INTO playlists(name,source_url,provider) VALUES(?,?,?)");
    insert.addBindValue(candidate);
    insert.addBindValue(sourceUrl);
    insert.addBindValue(provider);
    if (!insert.exec()) {
        m_lastError = insert.lastError().text();
        return -1;
    }
    return insert.lastInsertId().toInt();
}

bool Database::addToPlaylist(int playlistId, int trackId)
{
    QSqlQuery q(m_db);
    q.prepare("INSERT OR IGNORE INTO playlist_tracks(playlist_id,track_id,position) "
              "VALUES(?,?,COALESCE((SELECT MAX(position)+1 FROM playlist_tracks WHERE playlist_id=?),0))");
    q.addBindValue(playlistId);
    q.addBindValue(trackId);
    q.addBindValue(playlistId);
    if (!q.exec()) {
        m_lastError = q.lastError().text();
        return false;
    }
    return true;
}

bool Database::addTracksToPlaylist(int playlistId, const QList<int> &trackIds)
{
    if (playlistId < 0 || trackIds.isEmpty()) {
        m_lastError = QStringLiteral("Ungültige Playlist oder leere Track-Auswahl.");
        return false;
    }
    if (!m_db.transaction()) {
        m_lastError = m_db.lastError().text();
        return false;
    }
    for (const int trackId : trackIds) {
        if (!addToPlaylist(playlistId, trackId)) {
            m_db.rollback();
            return false;
        }
    }
    if (!m_db.commit()) {
        m_lastError = m_db.lastError().text();
        m_db.rollback();
        return false;
    }
    return true;
}

bool Database::removeFromPlaylist(int playlistId, int trackId)
{
    QSqlQuery q(m_db);
    q.prepare("DELETE FROM playlist_tracks WHERE playlist_id=? AND track_id=?");
    q.addBindValue(playlistId);
    q.addBindValue(trackId);
    if (!q.exec()) {
        m_lastError = q.lastError().text();
        return false;
    }
    return true;
}

bool Database::moveTracksBetweenPlaylists(int sourcePlaylistId, int targetPlaylistId,
                                          const QList<int> &trackIds)
{
    if (sourcePlaylistId < 0 || targetPlaylistId < 0 || sourcePlaylistId == targetPlaylistId) {
        m_lastError = QStringLiteral("Quell- und Ziel-Playlist müssen verschieden und gültig sein.");
        return false;
    }
    if (trackIds.isEmpty()) {
        m_lastError = QStringLiteral("Es wurden keine Tracks zum Verschieben ausgewählt.");
        return false;
    }
    if (!m_db.transaction()) {
        m_lastError = m_db.lastError().text();
        return false;
    }
    QSqlQuery membership(m_db);
    membership.prepare("SELECT COUNT(*) FROM playlist_tracks WHERE playlist_id=? AND track_id=?");
    for (const int trackId : trackIds) {
        membership.bindValue(0, sourcePlaylistId);
        membership.bindValue(1, trackId);
        if (!membership.exec() || !membership.next() || membership.value(0).toInt() != 1) {
            m_lastError = QStringLiteral("Mindestens ein ausgewählter Track ist nicht mehr in der Quell-Playlist.");
            m_db.rollback();
            return false;
        }
        membership.finish();
    }
    for (const int trackId : trackIds) {
        if (!addToPlaylist(targetPlaylistId, trackId) ||
            !removeFromPlaylist(sourcePlaylistId, trackId)) {
            m_db.rollback();
            return false;
        }
    }
    if (!m_db.commit()) {
        m_lastError = m_db.lastError().text();
        m_db.rollback();
        return false;
    }
    return true;
}

bool Database::renameTrack(int trackId, const QString &title)
{
    const QString cleanTitle = title.trimmed();
    if (trackId < 0 || cleanTitle.isEmpty()) {
        m_lastError = QStringLiteral("Track und neuer Titel müssen gültig sein.");
        return false;
    }
    QSqlQuery query(m_db);
    query.prepare("UPDATE tracks SET title=? WHERE id=?");
    query.addBindValue(cleanTitle);
    query.addBindValue(trackId);
    if (!query.exec()) {
        m_lastError = query.lastError().text();
        return false;
    }
    return query.numRowsAffected() == 1;
}

bool Database::deleteTracks(const QList<int> &trackIds)
{
    if (trackIds.isEmpty()) {
        m_lastError = QStringLiteral("Es wurden keine Tracks zum Entfernen ausgewählt.");
        return false;
    }
    if (!m_db.transaction()) {
        m_lastError = m_db.lastError().text();
        return false;
    }
    QSqlQuery query(m_db);
    query.prepare("DELETE FROM tracks WHERE id=?");
    for (const int trackId : trackIds) {
        query.bindValue(0, trackId);
        if (!query.exec()) {
            m_lastError = query.lastError().text();
            m_db.rollback();
            return false;
        }
        query.finish();
    }
    if (!m_db.commit()) {
        m_lastError = m_db.lastError().text();
        m_db.rollback();
        return false;
    }
    return true;
}

bool Database::updateBeatAnalysis(int trackId, double bpm, double firstBeatMs,
                                  double confidence)
{
    QSqlQuery query(m_db);
    query.prepare("UPDATE tracks SET bpm=?,beatgrid_offset_ms=?,beatgrid_confidence=?,"
                  "beatgrid_analyzed=1 WHERE id=?");
    query.addBindValue(bpm);
    query.addBindValue(firstBeatMs);
    query.addBindValue(confidence);
    query.addBindValue(trackId);
    if (!query.exec()) {
        m_lastError = query.lastError().text();
        return false;
    }
    return query.numRowsAffected() == 1;
}

bool Database::updateTrackAnalysis(int trackId, double bpm, const QString &musicalKey,
                                   int energy, const QString &label, double firstBeatMs,
                                   double confidence)
{
    QSqlQuery query(m_db);
    query.prepare("UPDATE tracks SET bpm=?,"
                  "musical_key=COALESCE(NULLIF(?,''),musical_key),"
                  "energy=?,label=COALESCE(NULLIF(?,''),label),"
                  "beatgrid_offset_ms=?,beatgrid_confidence=?,beatgrid_analyzed=1 WHERE id=?");
    query.addBindValue(bpm);
    query.addBindValue(musicalKey.trimmed());
    query.addBindValue(std::clamp(energy, 1, 10));
    query.addBindValue(label.trimmed());
    query.addBindValue(firstBeatMs);
    query.addBindValue(confidence);
    query.addBindValue(trackId);
    if (!query.exec()) {
        m_lastError = query.lastError().text();
        return false;
    }
    return query.numRowsAffected() == 1;
}
