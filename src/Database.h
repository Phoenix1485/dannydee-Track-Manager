#pragma once

#include <QSqlDatabase>
#include <QList>
#include <QString>

class Database {
public:
    Database();
    bool open();
    bool addTrack(const QString &title, const QString &artist, const QString &genre,
                  double bpm, const QString &key, int energy, const QString &label,
                  const QString &releaseDate, const QString &sourceUrl,
                  const QString &localPath, bool licensed);
    int addTrackReturningId(const QString &title, const QString &artist, const QString &genre,
                            double bpm, const QString &key, int energy, const QString &label,
                            const QString &releaseDate, const QString &sourceUrl,
                            const QString &localPath, bool licensed);
    int findTrackBySourceUrl(const QString &sourceUrl) const;
    bool addPlaylist(const QString &name);
    int ensurePlaylist(const QString &name, const QString &sourceUrl = {},
                       const QString &provider = {});
    bool addToPlaylist(int playlistId, int trackId);
    bool addTracksToPlaylist(int playlistId, const QList<int> &trackIds);
    bool removeFromPlaylist(int playlistId, int trackId);
    bool moveTracksBetweenPlaylists(int sourcePlaylistId, int targetPlaylistId,
                                    const QList<int> &trackIds);
    bool renameTrack(int trackId, const QString &title);
    bool deleteTracks(const QList<int> &trackIds);
    bool updateBeatAnalysis(int trackId, double bpm, double firstBeatMs,
                            double confidence);
    bool updateTrackAnalysis(int trackId, double bpm, const QString &musicalKey,
                             int energy, const QString &label, double firstBeatMs,
                             double confidence);
    QSqlDatabase connection() const { return m_db; }
    QString lastError() const { return m_lastError; }

private:
    bool migrate();
    QSqlDatabase m_db;
    QString m_lastError;
};
