#pragma once

#include <QCryptographicHash>
#include <QObject>
#include <QUrl>

class QNetworkAccessManager;
class QNetworkReply;
class QSaveFile;

class UpdateManager : public QObject {
    Q_OBJECT

public:
    explicit UpdateManager(QObject *parent = nullptr);

    bool isConfigured() const;
    bool isBusy() const;
    QString manifestUrl() const;

    static bool isNewerVersion(const QString &candidate, const QString &installed);

public slots:
    void checkForUpdates();
    void downloadUpdate();

signals:
    void updateAvailable(const QString &version, const QString &notes);
    void upToDate();
    void checkFailed(const QString &message);
    void downloadProgress(qint64 received, qint64 total);
    void downloadReady(const QString &path);
    void downloadFailed(const QString &message);

private:
    struct AvailableUpdate {
        QString version;
        QString notes;
        QUrl url;
        QByteArray sha256;
        qint64 size = 0;
        QString fileName;
    };

    void finishManifestRequest(QNetworkReply *reply);
    void finishDownload(QNetworkReply *reply);
    void failDownload(const QString &message);
    static QString platformKey();

    QNetworkAccessManager *m_network = nullptr;
    QNetworkReply *m_checkReply = nullptr;
    QNetworkReply *m_downloadReply = nullptr;
    QSaveFile *m_downloadFile = nullptr;
    QCryptographicHash m_downloadHash{QCryptographicHash::Sha256};
    qint64 m_downloadedBytes = 0;
    AvailableUpdate m_available;
};
