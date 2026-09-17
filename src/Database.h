#pragma once

#include <QSqlDatabase>
#include <QString>

class Database {
public:
    Database();
    bool open();
    bool addTrack(const QString &title, const QString &artist, const QString &genre,
                  double bpm, const QString &key, int energy, const QString &label,
                  const QString &releaseDate, const QString &sourceUrl,
                  const QString &localPath, bool licensed);
    bool addPlaylist(const QString &name);
    bool addToPlaylist(int playlistId, int trackId);
    QSqlDatabase connection() const { return m_db; }
    QString lastError() const { return m_lastError; }

private:
    bool migrate();
    QSqlDatabase m_db;
    QString m_lastError;
};

