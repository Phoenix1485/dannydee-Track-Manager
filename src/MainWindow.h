#pragma once

#include <QMainWindow>
#include <QQueue>
#include <QStringList>
#include <QUrl>

#include <array>

#include "AudioConverter.h"
#include "BeatAnalyzer.h"
#include "Database.h"
#include "DownloadManager.h"
#include "PlaylistImporter.h"
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
class QListWidget;
class QSlider;
class QTimer;
class BeatGridWidget;

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

    struct PendingLinkImport {
        QUrl url;
        QString provider;
    };

    struct DeckState {
        QMediaPlayer *player = nullptr;
        QAudioOutput *output = nullptr;
        BeatGridWidget *grid = nullptr;
        QLabel *titleLabel = nullptr;
        QLabel *bpmLabel = nullptr;
        QLabel *stateLabel = nullptr;
        QLabel *timeLabel = nullptr;
        QPushButton *playButton = nullptr;
        QPushButton *syncButton = nullptr;
        QSlider *positionSlider = nullptr;
        int trackId = -1;
        QString filePath;
        double bpm = 0.0;
        double firstBeatMs = 0.0;
        double confidence = 0.0;
        double nominalSyncRate = 1.0;
        bool beatgridAnalyzed = false;
        bool syncEnabled = false;
        bool sliderPressed = false;
    };

    void buildUi();
    QWidget *createDeckPanel(int deckIndex);
    void applyPlatformStyle();
    void updateLibrarySummary();
    void importFilePaths(const QStringList &paths);
    void setActivity(const QString &message, int progress = -2);
    void refreshFilter();
    void refreshPlaylistNavigation();
    void activatePlaylist(int playlistId);
    QString activePlaylistName() const;
    void importFiles();
    void addReferences();
    void startNextLinkImport();
    void storeResolvedLink(const PlaylistImportResult &result);
    void convertSelected(const QString &format);
    void downloadSelected();
    void exportAllLinksAsFlac();
    void startNextBatchDownload();
    void storeDownloadedMedia(const QStringList &paths, int trackId, const QUrl &sourceUrl);
    void createPlaylist();
    void addSelectedToPlaylist();
    void moveSelectedToPlaylist();
    void showPlaylist();
    void playSelected();
    void loadSelectedIntoDeck(int deckIndex);
    void toggleDeckPlayback(int deckIndex);
    void syncDeck(int deckIndex);
    void updateDeckDisplays();
    void applySyncCorrection(int deckIndex);
    void removeSelected();
    void checkForUpdates(bool manual);
    void downloadAvailableUpdate();
    void openDownloadedUpdate(const QString &path);
    int selectedTrackId() const;
    QList<int> selectedTrackIds() const;

    Database m_database;
    AudioConverter m_converter;
    DownloadManager m_downloader;
    UpdateManager m_updater;
    PlaylistImporter m_playlistImporter;
    BeatAnalyzer m_beatAnalyzer;
    int m_downloadTrackId = -1;
    QUrl m_downloadSourceUrl;
    QQueue<BatchDownloadItem> m_batchQueue;
    QQueue<PendingLinkImport> m_linkImportQueue;
    BatchDownloadItem m_batchCurrent;
    QString m_batchTargetDirectory;
    QStringList m_batchFailures;
    int m_batchTotal = 0;
    int m_batchProcessed = 0;
    int m_batchFilesCreated = 0;
    bool m_batchExportActive = false;
    bool m_linkImportActive = false;
    int m_linkImportTotal = 0;
    int m_linkImportProcessed = 0;
    int m_linkImportTracksAdded = 0;
    int m_linkImportTracksReused = 0;
    int m_linkImportPlaylists = 0;
    QStringList m_linkImportErrors;
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
    QLabel *m_activePlaylistTitle = nullptr;
    QLabel *m_activePlaylistContext = nullptr;
    QListWidget *m_playlistList = nullptr;
    QFrame *m_updateBanner = nullptr;
    QLabel *m_updateLabel = nullptr;
    QPushButton *m_updateButton = nullptr;
    QProgressBar *m_progress = nullptr;
    QMediaPlayer *m_player = nullptr;
    QAudioOutput *m_audioOutput = nullptr;
    std::array<DeckState, 2> m_decks;
    QTimer *m_deckTimer = nullptr;
    int m_playlistFilterId = -1;
    int m_lastImportedPlaylistId = -1;
    bool m_manualUpdateCheck = false;
};
