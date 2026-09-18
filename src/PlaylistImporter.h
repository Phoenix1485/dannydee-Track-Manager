#pragma once

#include <QObject>
#include <QProcess>
#include <QStringList>
#include <QTemporaryDir>
#include <QTimer>
#include <QUrl>

struct ImportedPlaylistTrack {
    QString title;
    QString artist;
    QString genre;
    QString label;
    QString releaseDate;
    QString sourceUrl;
    int position = 0;
};

struct PlaylistImportResult {
    QUrl requestedUrl;
    QString provider;
    QString name;
    QList<ImportedPlaylistTrack> tracks;
    bool isCollection = false;
};

class PlaylistImporter : public QObject {
    Q_OBJECT

public:
    explicit PlaylistImporter(QObject *parent = nullptr);

    bool isBusy() const;
    void resolve(const QUrl &url, const QString &provider);

    static bool parseSpotDlData(const QByteArray &data, const QUrl &requestedUrl,
                                const QString &provider, PlaylistImportResult *result,
                                QString *error = nullptr);
    static bool parseYtDlpData(const QByteArray &data, const QUrl &requestedUrl,
                               const QString &provider, PlaylistImportResult *result,
                               QString *error = nullptr);

signals:
    void progress(const QString &message);
    void resolved(const PlaylistImportResult &result);
    void failed(const QUrl &url, const QString &provider, const QString &message);

private:
    void finish(int exitCode, QProcess::ExitStatus exitStatus);
    void fail(const QString &message);
    static bool looksLikeCollection(const QUrl &url);

    QProcess m_process;
    QTimer m_timeout;
    QTemporaryDir m_temporaryDirectory;
    QUrl m_requestedUrl;
    QString m_provider;
    QString m_spotDlFile;
    bool m_spotify = false;
    bool m_active = false;
};

Q_DECLARE_METATYPE(PlaylistImportResult)
