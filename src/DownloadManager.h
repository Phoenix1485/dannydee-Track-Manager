#pragma once

#include <QObject>
#include <QProcess>
#include <QSaveFile>
#include <QStringList>
#include <QUrl>

#include <memory>

class QNetworkAccessManager;
class QNetworkReply;

struct OfficialTrackInfo {
    QString title;
    QString artist;
    QString genre;
    QString musicalKey;
    QString label;
    QString releaseDate;
    QString purchaseUrl;
    QString downloadUrl;
    double bpm = 0;
    bool downloadable = false;
};

class DownloadManager : public QObject {
    Q_OBJECT
public:
    explicit DownloadManager(QObject *parent = nullptr);
    void downloadDirect(const QUrl &url, const QString &targetDirectory);
    void downloadMedia(const QUrl &url, const QString &targetDirectory, const QString &audioFormat);
    bool mediaDownloaderAvailable(const QUrl &url = {}) const;
    bool isBusy() const;

signals:
    void progress(qint64 received, qint64 total);
    void mediaProgress(int percent, const QString &message);
    void downloaded(const QString &localPath, const OfficialTrackInfo &track);
    void mediaDownloaded(const QStringList &localPaths);
    void failed(const QString &message, const QString &fallbackUrl);

private:
    void startFileRequest(const QUrl &url, const QString &targetDirectory,
                          const OfficialTrackInfo &track);
    void startYtDlpDownload(const QStringList &urls, bool continueOnError);
    void consumeMediaOutput();
    void handleMediaLine(const QString &line);
    static QString findTool(const QString &baseName);
    bool ensureOutputFile(QNetworkReply *reply);
    QString chooseOutputName(QNetworkReply *reply) const;
    static QString safeFileName(QString value);

    QNetworkAccessManager *m_network = nullptr;
    QNetworkReply *m_fileReply = nullptr;
    std::unique_ptr<QSaveFile> m_output;
    QString m_targetDirectory;
    QString m_outputPath;
    OfficialTrackInfo m_currentTrack;
    QProcess m_mediaProcess;
    QByteArray m_mediaOutputBuffer;
    QString m_mediaLog;
    QStringList m_mediaOutputPaths;
    QString m_mediaTargetDirectory;
    QString m_mediaFormat;
    QUrl m_mediaSourceUrl;
    bool m_mediaStartFailed = false;
};

Q_DECLARE_METATYPE(OfficialTrackInfo)
