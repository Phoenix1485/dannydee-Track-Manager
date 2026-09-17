#pragma once

#include <QMainWindow>
#include <QQueue>
#include <QStringList>
#include <QUrl>

#include "AudioConverter.h"
#include "Database.h"
#include "DownloadManager.h"
#include "UpdateManager.h"

class QComboBox;
class QLineEdit;
class QSqlTableModel;
class QTableView;
class QDoubleSpinBox;
class QLabel;
class QProgressBar;
class QAudioOutput;
class QMediaPlayer;
class QDragEnterEvent;
class QDropEvent;
class QFrame;
class QPushButton;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);

protected:
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private:
    struct BatchDownloadItem {
        int trackId = -1;
        QUrl url;
    };

    void buildUi();
    void applyPlatformStyle();
    void updateLibrarySummary();
    void importFilePaths(const QStringList &paths);
    void setActivity(const QString &message, int progress = -2);
    void refreshFilter();
    void importFiles();
    void addReferences();
    void convertSelected(const QString &format);
    void downloadSelected();
    void exportAllLinksAsFlac();
    void startNextBatchDownload();
    void storeDownloadedMedia(const QStringList &paths, int trackId, const QUrl &sourceUrl);
    void createPlaylist();
    void addSelectedToPlaylist();
    void showPlaylist();
    void playSelected();
    void removeSelected();
    void checkForUpdates(bool manual);
    void downloadAvailableUpdate();
    void openDownloadedUpdate(const QString &path);
    int selectedTrackId() const;

    Database m_database;
    AudioConverter m_converter;
    DownloadManager m_downloader;
    UpdateManager m_updater;
    int m_downloadTrackId = -1;
    QUrl m_downloadSourceUrl;
    QQueue<BatchDownloadItem> m_batchQueue;
    BatchDownloadItem m_batchCurrent;
    QString m_batchTargetDirectory;
    QStringList m_batchFailures;
    int m_batchTotal = 0;
    int m_batchProcessed = 0;
    int m_batchFilesCreated = 0;
    bool m_batchExportActive = false;
    QSqlTableModel *m_model = nullptr;
    QTableView *m_table = nullptr;
    QLineEdit *m_search = nullptr;
    QComboBox *m_genre = nullptr;
    QDoubleSpinBox *m_bpmMin = nullptr;
    QDoubleSpinBox *m_bpmMax = nullptr;
    QLabel *m_status = nullptr;
    QLabel *m_trackCount = nullptr;
    QLabel *m_localCount = nullptr;
    QLabel *m_linkCount = nullptr;
    QLabel *m_libraryCaption = nullptr;
    QFrame *m_updateBanner = nullptr;
    QLabel *m_updateLabel = nullptr;
    QPushButton *m_updateButton = nullptr;
    QProgressBar *m_progress = nullptr;
    QMediaPlayer *m_player = nullptr;
    QAudioOutput *m_audioOutput = nullptr;
    int m_playlistFilterId = -1;
    bool m_manualUpdateCheck = false;
};
