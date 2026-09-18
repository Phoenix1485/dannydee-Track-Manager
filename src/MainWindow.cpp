#include "MainWindow.h"
#include "AudioProbe.h"
#include "BeatGridWidget.h"
#include "BeatSyncMath.h"
#include "LinkResolver.h"

#include <QAction>
#include <QAbstractItemView>
#include <QAudioOutput>
#include <QComboBox>
#include <QCoreApplication>
#include <QDate>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QFormLayout>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenuBar>
#include <QMessageBox>
#include <QMediaPlayer>
#include <QMimeData>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QProcess>
#include <QPushButton>
#include <QRegularExpression>
#include <QSet>
#include <QSignalBlocker>
#include <QSlider>
#include <QSqlQuery>
#include <QSqlError>
#include <QSqlTableModel>
#include <QStatusBar>
#include <QStandardPaths>
#include <QStyle>
#include <QTableView>
#include <QToolBar>
#include <QToolButton>
#include <QTimer>
#include <QUrl>
#include <QVariant>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace {
QString deckTime(qint64 milliseconds)
{
    const qint64 totalSeconds = std::max<qint64>(0, milliseconds) / 1000;
    return QStringLiteral("%1:%2")
        .arg(totalSeconds / 60)
        .arg(totalSeconds % 60, 2, 10, QLatin1Char('0'));
}

}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent), m_converter(this), m_downloader(this), m_updater(this),
      m_playlistImporter(this), m_beatAnalyzer(this)
{
    setWindowTitle("DannyDee Track Manager");
    resize(1280, 820);
    setMinimumSize(900, 620);
    setAcceptDrops(true);
    if (!m_database.open()) {
        QMessageBox::critical(this, "Datenbank", m_database.lastError());
    }
    buildUi();
    applyPlatformStyle();
    updateLibrarySummary();
    m_audioOutput = new QAudioOutput(this);
    m_audioOutput->setVolume(0.75f);
    m_player = new QMediaPlayer(this);
    m_player->setAudioOutput(m_audioOutput);
    m_deckTimer = new QTimer(this);
    m_deckTimer->setInterval(100);
    connect(m_deckTimer, &QTimer::timeout, this, &MainWindow::updateDeckDisplays);
    m_deckTimer->start();
    connect(&m_converter, &AudioConverter::finished, this, [this](bool ok, const QString &msg) {
        setActivity(msg);
        if (!ok) QMessageBox::warning(this, "Konvertierung", msg);
    });
    connect(&m_downloader, &DownloadManager::progress, this, [this](qint64 received, qint64 total) {
        if (total > 0) setActivity(QString("Download: %1 %").arg(received * 100 / total), received * 100 / total);
        else setActivity(QString("Download: %1 MB").arg(received / 1048576.0, 0, 'f', 1), -1);
    });
    connect(&m_downloader, &DownloadManager::mediaProgress, this,
            [this](int percent, const QString &message) {
        setActivity(percent >= 0 ? QString("%1 (%2 %)").arg(message).arg(percent) : message,
                    percent >= 0 ? percent : -1);
    });
    connect(&m_downloader, &DownloadManager::downloaded, this,
            [this](const QString &path, const OfficialTrackInfo &track) {
        QSqlQuery update(m_database.connection());
        update.prepare("UPDATE tracks SET local_path=?,licensed=1,title=COALESCE(NULLIF(?,''),title),"
                       "artist=COALESCE(NULLIF(?,''),artist) WHERE id=?");
        update.addBindValue(path);
        update.addBindValue(track.title);
        update.addBindValue(track.artist);
        update.addBindValue(m_downloadTrackId);
        update.exec();
        m_model->select();
        updateLibrarySummary();
        setActivity("Download abgeschlossen: " + path);
        QMessageBox::information(this, "Download abgeschlossen", "Datei gespeichert und zur Bibliothek hinzugefügt:\n" + path);
    });
    connect(&m_downloader, &DownloadManager::mediaDownloaded, this, [this](const QStringList &paths) {
        storeDownloadedMedia(paths, m_downloadTrackId, m_downloadSourceUrl);
        if (m_batchExportActive) {
            ++m_batchProcessed;
            m_batchFilesCreated += paths.size();
            QTimer::singleShot(0, this, &MainWindow::startNextBatchDownload);
            return;
        }
        setActivity(QString("%1 Datei(en) heruntergeladen und konvertiert.").arg(paths.size()));
        QMessageBox::information(this, "Download abgeschlossen",
                                 QString("%1 Audiodatei(en) gespeichert:\n%2")
                                     .arg(paths.size()).arg(paths.join('\n')));
    });
    connect(&m_downloader, &DownloadManager::failed, this,
            [this](const QString &message, const QString &fallback) {
        setActivity(message);
        if (m_batchExportActive) {
            ++m_batchProcessed;
            m_batchFailures << QString("%1: %2").arg(m_batchCurrent.url.toString(), message);
            QTimer::singleShot(0, this, &MainWindow::startNextBatchDownload);
            return;
        }
        QMessageBox box(this);
        box.setWindowTitle("Download");
        box.setText(message);
        QPushButton *open = nullptr;
        if (!fallback.isEmpty()) open = box.addButton("Beim Anbieter öffnen", QMessageBox::ActionRole);
        box.addButton(QMessageBox::Ok);
        box.exec();
        if (open && box.clickedButton() == open) QDesktopServices::openUrl(QUrl(fallback));
    });
    connect(&m_updater, &UpdateManager::updateAvailable, this,
            [this](const QString &version, const QString &notes) {
        m_updateLabel->setText(QStringLiteral("Version %1 ist verf\u00fcgbar.").arg(version));
        m_updateLabel->setToolTip(notes);
        m_updateButton->setText(QStringLiteral("Herunterladen & installieren"));
        m_updateButton->setEnabled(true);
        m_updateBanner->show();
        setActivity(QStringLiteral("Update %1 verf\u00fcgbar.").arg(version));
        m_manualUpdateCheck = false;
    });
    connect(&m_updater, &UpdateManager::upToDate, this, [this] {
        if (m_manualUpdateCheck) {
            QMessageBox::information(this, QStringLiteral("Updates"),
                                     QStringLiteral("Du verwendest bereits die aktuelle Version %1.")
                                         .arg(QCoreApplication::applicationVersion()));
        }
        m_manualUpdateCheck = false;
        setActivity(QStringLiteral("Die Anwendung ist aktuell."));
    });
    connect(&m_updater, &UpdateManager::checkFailed, this, [this](const QString &message) {
        if (m_manualUpdateCheck) QMessageBox::warning(this, QStringLiteral("Updates"), message);
        m_manualUpdateCheck = false;
        setActivity(message);
    });
    connect(&m_updater, &UpdateManager::downloadProgress, this,
            [this](qint64 received, qint64 total) {
        if (total > 0) {
            setActivity(QStringLiteral("Update wird geladen: %1 %").arg(received * 100 / total),
                        static_cast<int>(received * 100 / total));
        } else {
            setActivity(QStringLiteral("Update wird geladen \u2026"), -1);
        }
    });
    connect(&m_updater, &UpdateManager::downloadFailed, this, [this](const QString &message) {
        m_updateButton->setEnabled(true);
        m_updateButton->setText(QStringLiteral("Erneut versuchen"));
        setActivity(QStringLiteral("Update-Download fehlgeschlagen."));
        QMessageBox::warning(this, QStringLiteral("Update"), message);
    });
    connect(&m_updater, &UpdateManager::downloadReady,
            this, &MainWindow::openDownloadedUpdate);
    connect(&m_playlistImporter, &PlaylistImporter::progress, this,
            [this](const QString &message) { setActivity(message, -1); });
    connect(&m_playlistImporter, &PlaylistImporter::resolved, this,
            [this](const PlaylistImportResult &result) {
        storeResolvedLink(result);
        ++m_linkImportProcessed;
        QTimer::singleShot(0, this, &MainWindow::startNextLinkImport);
    });
    connect(&m_playlistImporter, &PlaylistImporter::failed, this,
            [this](const QUrl &url, const QString &provider, const QString &message) {
        const QString source = url.toString(QUrl::FullyEncoded);
        const int existingId = m_database.findTrackBySourceUrl(source);
        if (existingId >= 0) {
            ++m_linkImportTracksReused;
        } else if (m_database.addTrackReturningId(QStringLiteral("Unbekannter Track"), provider,
                                                  QStringLiteral("Other"), 0, {}, 5, {}, {},
                                                  source, {}, false) >= 0) {
            ++m_linkImportTracksAdded;
        }
        m_linkImportErrors << QStringLiteral("%1: %2").arg(source, message);
        ++m_linkImportProcessed;
        QTimer::singleShot(0, this, &MainWindow::startNextLinkImport);
    });
    connect(&m_beatAnalyzer, &BeatAnalyzer::analysisStarted, this,
            [this](int trackId, const QString &) {
        for (DeckState &deck : m_decks) {
            if (deck.trackId == trackId && deck.stateLabel)
                deck.stateLabel->setText(QStringLiteral("BPM & Beatgrid werden analysiert …"));
        }
        setActivity(QStringLiteral("Automatische BPM- und Beatgrid-Analyse läuft …"), -1);
    });
    connect(&m_beatAnalyzer, &BeatAnalyzer::analyzed, this,
            [this](const BeatAnalysisResult &result) {
        if (!m_database.updateBeatAnalysis(result.trackId, result.bpm,
                                           result.firstBeatMs, result.confidence)) {
            QMessageBox::warning(this, QStringLiteral("Beat-Analyse"), m_database.lastError());
            return;
        }
        for (DeckState &deck : m_decks) {
            if (deck.trackId != result.trackId) continue;
            deck.bpm = result.bpm;
            deck.firstBeatMs = result.firstBeatMs;
            deck.confidence = result.confidence;
            deck.beatgridAnalyzed = true;
            deck.grid->setGrid(deck.bpm, deck.firstBeatMs, deck.confidence);
            deck.stateLabel->setText(QStringLiteral("Beatgrid bereit · %1% Sicherheit")
                                         .arg(qRound(deck.confidence * 100.0)));
        }
        m_model->select();
        refreshFilter();
        updateDeckDisplays();
        setActivity(QStringLiteral("BPM %1 und Beatgrid gespeichert.").arg(result.bpm, 0, 'f', 2));
    });
    connect(&m_beatAnalyzer, &BeatAnalyzer::failed, this,
            [this](int trackId, const QString &, const QString &message) {
        for (DeckState &deck : m_decks) {
            if (deck.trackId == trackId && deck.stateLabel)
                deck.stateLabel->setText(QStringLiteral("Analyse fehlgeschlagen"));
        }
        updateDeckDisplays();
        setActivity(QStringLiteral("Beat-Analyse fehlgeschlagen: %1").arg(message));
    });

    if (m_updater.isConfigured()) {
        QTimer::singleShot(1800, &m_updater, &UpdateManager::checkForUpdates);
    }
}

void MainWindow::storeDownloadedMedia(const QStringList &paths, int trackId, const QUrl &sourceUrl)
{
    for (int index = 0; index < paths.size(); ++index) {
        const QString &path = paths.at(index);
        const ProbedTrack metadata = AudioProbe::read(path);
        const QString title = metadata.title.isEmpty() ? QFileInfo(path).completeBaseName() : metadata.title;
        const QString artist = metadata.artist.isEmpty() ? "Unbekannt" : metadata.artist;
        if (index == 0 && trackId >= 0) {
            QSqlQuery update(m_database.connection());
            update.prepare("UPDATE tracks SET local_path=?,licensed=1,"
                           "title=COALESCE(NULLIF(?,''),title),artist=COALESCE(NULLIF(?,''),artist),"
                           "genre=COALESCE(NULLIF(?,''),genre),"
                           "bpm=CASE WHEN ?>0 THEN ? ELSE bpm END,"
                           "musical_key=COALESCE(NULLIF(?,''),musical_key),"
                           "label=COALESCE(NULLIF(?,''),label),"
                           "release_date=COALESCE(NULLIF(?,''),release_date) WHERE id=?");
            update.addBindValue(path);
            update.addBindValue(title);
            update.addBindValue(artist);
            update.addBindValue(metadata.genre);
            update.addBindValue(metadata.bpm);
            update.addBindValue(metadata.bpm);
            update.addBindValue(metadata.musicalKey);
            update.addBindValue(metadata.label);
            update.addBindValue(metadata.releaseDate);
            update.addBindValue(trackId);
            update.exec();
        } else {
            m_database.addTrack(title, artist,
                                metadata.genre.isEmpty() ? "Other" : metadata.genre,
                                metadata.bpm, metadata.musicalKey, 5, metadata.label,
                                metadata.releaseDate, sourceUrl.toString(), path, true);
        }
    }
    m_model->select();
    updateLibrarySummary();
}

QWidget *MainWindow::createDeckPanel(int deckIndex)
{
    DeckState &deck = m_decks.at(static_cast<size_t>(deckIndex));
    auto *card = new QFrame;
    card->setObjectName("deckCard");
    card->setProperty("deck", deckIndex == 0 ? "A" : "B");
    auto *layout = new QVBoxLayout(card);
    layout->setContentsMargins(14, 11, 14, 12);
    layout->setSpacing(7);

    auto *header = new QHBoxLayout;
    auto *badge = new QLabel(deckIndex == 0 ? "DECK A" : "DECK B");
    badge->setObjectName("deckBadge");
    badge->setProperty("deck", deckIndex == 0 ? "A" : "B");
    deck.stateLabel = new QLabel("Kein Track geladen");
    deck.stateLabel->setObjectName("deckState");
    deck.stateLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    header->addWidget(badge);
    header->addStretch();
    header->addWidget(deck.stateLabel);
    layout->addLayout(header);

    deck.titleLabel = new QLabel("–");
    deck.titleLabel->setObjectName("deckTrackTitle");
    deck.titleLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(deck.titleLabel);

    deck.grid = new BeatGridWidget;
    layout->addWidget(deck.grid);

    deck.positionSlider = new QSlider(Qt::Horizontal);
    deck.positionSlider->setObjectName("deckPosition");
    deck.positionSlider->setRange(0, 1000);
    deck.positionSlider->setEnabled(false);
    layout->addWidget(deck.positionSlider);

    auto *controls = new QHBoxLayout;
    controls->setSpacing(7);
    auto *loadButton = new QPushButton("Auswahl laden");
    loadButton->setProperty("role", "quiet");
    loadButton->setToolTip(QStringLiteral("Ausgewählten lokalen Track in Deck %1 laden")
                               .arg(deckIndex == 0 ? "A" : "B"));
    deck.playButton = new QPushButton(QStringLiteral("▶"));
    deck.playButton->setObjectName("deckPlayButton");
    deck.playButton->setEnabled(false);
    deck.playButton->setToolTip("Play / Pause");
    deck.syncButton = new QPushButton(deckIndex == 0 ? "SYNC → B" : "SYNC → A");
    deck.syncButton->setObjectName("deckSyncButton");
    deck.syncButton->setCheckable(true);
    deck.syncButton->setEnabled(false);
    deck.syncButton->setToolTip("Tempo und Beatphase mit dem anderen Deck synchronisieren");
    deck.bpmLabel = new QLabel("— BPM");
    deck.bpmLabel->setObjectName("deckBpm");
    deck.timeLabel = new QLabel("0:00 / 0:00");
    deck.timeLabel->setObjectName("deckTime");
    controls->addWidget(loadButton);
    controls->addWidget(deck.playButton);
    controls->addWidget(deck.syncButton);
    controls->addWidget(deck.bpmLabel);
    controls->addStretch();
    controls->addWidget(deck.timeLabel);
    layout->addLayout(controls);

    deck.output = new QAudioOutput(this);
    deck.output->setVolume(0.75f);
    deck.player = new QMediaPlayer(this);
    deck.player->setAudioOutput(deck.output);

    connect(loadButton, &QPushButton::clicked, this,
            [this, deckIndex] { loadSelectedIntoDeck(deckIndex); });
    connect(deck.playButton, &QPushButton::clicked, this,
            [this, deckIndex] { toggleDeckPlayback(deckIndex); });
    connect(deck.syncButton, &QPushButton::clicked, this,
            [this, deckIndex] { syncDeck(deckIndex); });
    connect(deck.positionSlider, &QSlider::sliderPressed, this,
            [this, deckIndex] { m_decks.at(static_cast<size_t>(deckIndex)).sliderPressed = true; });
    connect(deck.positionSlider, &QSlider::sliderReleased, this, [this, deckIndex] {
        DeckState &current = m_decks.at(static_cast<size_t>(deckIndex));
        current.sliderPressed = false;
        if (current.player->duration() > 0) {
            current.player->setPosition(current.player->duration()
                                        * current.positionSlider->value() / 1000);
        }
    });
    connect(deck.player, &QMediaPlayer::errorOccurred, this,
            [this, deckIndex](QMediaPlayer::Error, const QString &message) {
        DeckState &current = m_decks.at(static_cast<size_t>(deckIndex));
        current.stateLabel->setText(QStringLiteral("Wiedergabefehler"));
        setActivity(QStringLiteral("Deck %1: %2")
                        .arg(deckIndex == 0 ? "A" : "B", message));
    });
    return card;
}

void MainWindow::buildUi()
{
    auto *central = new QWidget(this);
    central->setObjectName("appRoot");
    auto *layout = new QVBoxLayout(central);
    layout->setContentsMargins(26, 22, 26, 18);
    layout->setSpacing(16);

    auto *header = new QHBoxLayout;
    header->setSpacing(10);
    auto *brand = new QVBoxLayout;
    brand->setSpacing(2);
    auto *title = new QLabel("DannyDee");
    title->setObjectName("brandTitle");
    auto *subtitle = new QLabel("Music Library & FLAC Export");
    subtitle->setObjectName("brandSubtitle");
    brand->addWidget(title);
    brand->addWidget(subtitle);
    header->addLayout(brand);
    header->addStretch();

    auto *fileMenu = menuBar()->addMenu("&Datei");
    auto *libraryMenu = menuBar()->addMenu("&Bibliothek");
    auto *playbackMenu = menuBar()->addMenu("&Wiedergabe");
    auto *helpMenu = menuBar()->addMenu("&Hilfe");
    menuBar()->setNativeMenuBar(true);

    auto *importAction = fileMenu->addAction(style()->standardIcon(QStyle::SP_DialogOpenButton),
                                              "Audiodateien importieren");
    importAction->setShortcut(QKeySequence::Open);
    auto *linksAction = fileMenu->addAction(style()->standardIcon(QStyle::SP_FileDialogNewFolder),
                                             "Links importieren");
    linksAction->setShortcut(QKeySequence("Ctrl+L"));
    auto *exportAllAction = fileMenu->addAction(style()->standardIcon(QStyle::SP_DialogSaveButton),
                                                 "Alle Links als FLAC exportieren");
    exportAllAction->setShortcut(QKeySequence("Ctrl+Shift+E"));
    fileMenu->addSeparator();
    auto *quitAction = fileMenu->addAction("Beenden");
    quitAction->setMenuRole(QAction::QuitRole);
    quitAction->setShortcut(QKeySequence::Quit);

    auto *downloadAction = libraryMenu->addAction(style()->standardIcon(QStyle::SP_ArrowDown),
                                                   "Ausgewählten Link laden");
    auto *flacAction = libraryMenu->addAction("Lokale Datei als FLAC");
    auto *wavAction = libraryMenu->addAction("Lokale Datei als WAV");
    libraryMenu->addSeparator();
    auto *playlistAction = libraryMenu->addAction("Neue Playlist");
    auto *addPlaylistAction = libraryMenu->addAction("Auswahl in Playlist kopieren …");
    auto *movePlaylistAction = libraryMenu->addAction("Auswahl in Playlist verschieben …");
    auto *viewPlaylistAction = libraryMenu->addAction("Playlist-Navigation fokussieren");
    viewPlaylistAction->setShortcut(QKeySequence("Ctrl+P"));
    libraryMenu->addSeparator();
    auto *removeAction = libraryMenu->addAction(style()->standardIcon(QStyle::SP_TrashIcon),
                                                 "Eintrag entfernen");
    removeAction->setShortcut(QKeySequence::Delete);

    auto *playAction = playbackMenu->addAction(style()->standardIcon(QStyle::SP_MediaPlay),
                                                "Abspielen / Pause");
    playAction->setShortcut(QKeySequence(Qt::Key_Space));
    auto *updateAction = helpMenu->addAction(QStringLiteral("Nach Updates suchen \u2026"));
    auto *aboutAction = helpMenu->addAction(QStringLiteral("\u00dcber DannyDee"));

    auto makeHeaderButton = [this, header](QAction *action, const char *role) {
        auto *button = new QPushButton(action->icon(), action->text(), this);
        button->setProperty("role", role);
        button->setCursor(Qt::PointingHandCursor);
        connect(button, &QPushButton::clicked, action, &QAction::trigger);
        header->addWidget(button);
    };
    makeHeaderButton(importAction, "secondary");
    makeHeaderButton(linksAction, "secondary");
    makeHeaderButton(exportAllAction, "primary");
    layout->addLayout(header);

    m_updateBanner = new QFrame;
    m_updateBanner->setObjectName("updateBanner");
    auto *updateLayout = new QHBoxLayout(m_updateBanner);
    updateLayout->setContentsMargins(16, 10, 12, 10);
    updateLayout->setSpacing(12);
    m_updateLabel = new QLabel;
    m_updateLabel->setObjectName("updateLabel");
    m_updateButton = new QPushButton(QStringLiteral("Herunterladen & installieren"));
    m_updateButton->setProperty("role", "primary");
    updateLayout->addWidget(m_updateLabel, 1);
    updateLayout->addWidget(m_updateButton);
    m_updateBanner->hide();
    layout->addWidget(m_updateBanner);

    auto *mixerCard = new QFrame;
    mixerCard->setObjectName("mixerCard");
    auto *mixerLayout = new QVBoxLayout(mixerCard);
    mixerLayout->setContentsMargins(12, 10, 12, 12);
    mixerLayout->setSpacing(8);
    auto *mixerHeader = new QHBoxLayout;
    auto *mixerTitle = new QLabel("PERFORMANCE DECKS");
    mixerTitle->setObjectName("navigationTitle");
    auto *mixerHint = new QLabel("Lokalen Track auswählen → in Deck laden → beide Beatgrids analysieren → Sync");
    mixerHint->setObjectName("sectionCaption");
    mixerHeader->addWidget(mixerTitle);
    mixerHeader->addStretch();
    mixerHeader->addWidget(mixerHint);
    mixerLayout->addLayout(mixerHeader);
    auto *decks = new QHBoxLayout;
    decks->setSpacing(10);
    decks->addWidget(createDeckPanel(0), 1);
    decks->addWidget(createDeckPanel(1), 1);
    mixerLayout->addLayout(decks);
    layout->addWidget(mixerCard);

    auto *overview = new QHBoxLayout;
    overview->setSpacing(12);
    auto makeStatCard = [overview](const QString &caption, QLabel *&value, const QString &accent) {
        auto *card = new QFrame;
        card->setObjectName("statCard");
        card->setProperty("accent", accent);
        auto *cardLayout = new QVBoxLayout(card);
        cardLayout->setContentsMargins(18, 14, 18, 14);
        cardLayout->setSpacing(2);
        value = new QLabel("0");
        value->setObjectName("statValue");
        auto *label = new QLabel(caption);
        label->setObjectName("statLabel");
        cardLayout->addWidget(value);
        cardLayout->addWidget(label);
        overview->addWidget(card, 1);
    };
    makeStatCard("Tracks insgesamt", m_trackCount, "violet");
    makeStatCard("Lokale Dateien", m_localCount, "green");
    makeStatCard("Gespeicherte Links", m_linkCount, "blue");
    layout->addLayout(overview);

    auto *filterCard = new QFrame;
    filterCard->setObjectName("filterCard");
    auto *filters = new QHBoxLayout(filterCard);
    filters->setContentsMargins(14, 10, 14, 10);
    filters->setSpacing(10);
    m_search = new QLineEdit;
    m_search->setClearButtonEnabled(true);
    m_search->setPlaceholderText("Titel, Artist oder Label suchen …");
    m_genre = new QComboBox;
    m_genre->addItems({"Alle Genres", "Techno", "House", "Trance", "Hip-Hop", "Drum & Bass", "Other"});
    m_bpmMin = new QDoubleSpinBox;
    m_bpmMax = new QDoubleSpinBox;
    for (auto *spin : {m_bpmMin, m_bpmMax}) { spin->setRange(0, 300); spin->setSuffix(" BPM"); }
    m_bpmMax->setValue(300);
    auto *searchIcon = new QLabel;
    searchIcon->setPixmap(style()->standardIcon(QStyle::SP_FileDialogContentsView).pixmap(18, 18));
    searchIcon->setToolTip("Bibliothek durchsuchen");
    filters->addWidget(searchIcon);
    filters->addWidget(m_search, 1);
    filters->addWidget(m_genre);
    filters->addWidget(new QLabel("BPM"));
    filters->addWidget(m_bpmMin);
    filters->addWidget(new QLabel("–"));
    filters->addWidget(m_bpmMax);
    layout->addWidget(filterCard);

    auto *libraryCard = new QFrame;
    libraryCard->setObjectName("libraryCard");
    auto *libraryLayout = new QHBoxLayout(libraryCard);
    libraryLayout->setContentsMargins(0, 0, 0, 0);
    libraryLayout->setSpacing(0);

    auto *playlistSidebar = new QFrame;
    playlistSidebar->setObjectName("playlistSidebar");
    playlistSidebar->setMinimumWidth(210);
    playlistSidebar->setMaximumWidth(270);
    auto *playlistLayout = new QVBoxLayout(playlistSidebar);
    playlistLayout->setContentsMargins(12, 14, 12, 12);
    playlistLayout->setSpacing(10);
    auto *playlistHeader = new QHBoxLayout;
    auto *playlistTitle = new QLabel("PLAYLISTS");
    playlistTitle->setObjectName("navigationTitle");
    auto *newPlaylistButton = new QPushButton("+");
    newPlaylistButton->setObjectName("playlistAddButton");
    newPlaylistButton->setToolTip("Neue Playlist erstellen");
    newPlaylistButton->setAccessibleName("Neue Playlist erstellen");
    newPlaylistButton->setFixedSize(30, 30);
    newPlaylistButton->setCursor(Qt::PointingHandCursor);
    playlistHeader->addWidget(playlistTitle);
    playlistHeader->addStretch();
    playlistHeader->addWidget(newPlaylistButton);
    playlistLayout->addLayout(playlistHeader);

    m_playlistList = new QListWidget;
    m_playlistList->setObjectName("playlistList");
    m_playlistList->setFrameShape(QFrame::NoFrame);
    m_playlistList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_playlistList->setSelectionMode(QAbstractItemView::SingleSelection);
    m_playlistList->setUniformItemSizes(true);
    m_playlistList->setToolTip("Mit Klick oder Pfeiltasten zwischen Playlists wechseln");
    playlistLayout->addWidget(m_playlistList, 1);

    auto *navigationHint = new QLabel("Klicken zum Wechseln\nCtrl+P: Navigation");
    navigationHint->setObjectName("navigationHint");
    playlistLayout->addWidget(navigationHint);
    libraryLayout->addWidget(playlistSidebar);

    auto *trackPane = new QWidget;
    trackPane->setObjectName("trackPane");
    auto *trackLayout = new QVBoxLayout(trackPane);
    trackLayout->setContentsMargins(0, 0, 0, 0);
    trackLayout->setSpacing(0);
    auto *libraryHeader = new QHBoxLayout;
    libraryHeader->setContentsMargins(18, 12, 16, 11);
    auto *activeView = new QVBoxLayout;
    activeView->setSpacing(1);
    m_activePlaylistContext = new QLabel("BIBLIOTHEK / ALLE TRACKS");
    m_activePlaylistContext->setObjectName("activePlaylistContext");
    m_activePlaylistTitle = new QLabel("Alle Tracks");
    m_activePlaylistTitle->setObjectName("activePlaylistTitle");
    activeView->addWidget(m_activePlaylistContext);
    activeView->addWidget(m_activePlaylistTitle);
    m_libraryCaption = new QLabel;
    m_libraryCaption->setObjectName("sectionCaption");
    libraryHeader->addLayout(activeView);
    libraryHeader->addStretch();
    libraryHeader->addWidget(m_libraryCaption);
    trackLayout->addLayout(libraryHeader);

    m_model = new QSqlTableModel(this, m_database.connection());
    m_model->setTable("tracks");
    m_model->setEditStrategy(QSqlTableModel::OnFieldChange);
    m_model->select();
    const QStringList headers{"ID", "Titel", "Artist", "Genre", "BPM", "Key", "Energy", "Label",
                              "Release", "Quelle", "Datei", "Lokal", "Erstellt", "Beat-Offset",
                              "Grid-Sicherheit", "Grid analysiert"};
    for (int i = 0; i < headers.size(); ++i) m_model->setHeaderData(i, Qt::Horizontal, headers[i]);
    m_table = new QTableView;
    m_table->setObjectName("trackTable");
    m_table->setModel(m_model);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_table->setSortingEnabled(true);
    m_table->setAlternatingRowColors(true);
    m_table->setShowGrid(false);
    m_table->setWordWrap(false);
    m_table->setCornerButtonEnabled(false);
    m_table->verticalHeader()->setVisible(false);
    m_table->verticalHeader()->setDefaultSectionSize(42);
    m_table->hideColumn(0);
    for (int column : {9, 10, 11, 12, 13, 14, 15}) m_table->hideColumn(column);
    m_table->horizontalHeader()->setStretchLastSection(false);
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_table->setColumnWidth(3, 120);
    m_table->setColumnWidth(4, 76);
    m_table->setColumnWidth(5, 70);
    m_table->setColumnWidth(6, 76);
    m_table->setColumnWidth(7, 130);
    m_table->setColumnWidth(8, 100);
    trackLayout->addWidget(m_table, 1);

    auto *actions = new QHBoxLayout;
    actions->setContentsMargins(14, 10, 14, 12);
    actions->setSpacing(8);
    auto makeActionButton = [this, actions](QAction *action, const char *role = "quiet") {
        auto *button = new QPushButton(action->icon(), action->text(), this);
        button->setProperty("role", role);
        button->setCursor(Qt::PointingHandCursor);
        connect(button, &QPushButton::clicked, action, &QAction::trigger);
        actions->addWidget(button);
    };
    makeActionButton(playAction);
    makeActionButton(downloadAction, "accent");
    actions->addStretch();
    makeActionButton(addPlaylistAction);
    makeActionButton(movePlaylistAction);
    makeActionButton(removeAction, "danger");
    trackLayout->addLayout(actions);
    libraryLayout->addWidget(trackPane, 1);
    layout->addWidget(libraryCard, 1);
    setCentralWidget(central);

    connect(importAction, &QAction::triggered, this, &MainWindow::importFiles);
    connect(linksAction, &QAction::triggered, this, &MainWindow::addReferences);
    connect(exportAllAction, &QAction::triggered, this, &MainWindow::exportAllLinksAsFlac);
    connect(quitAction, &QAction::triggered, this, &QWidget::close);
    connect(downloadAction, &QAction::triggered, this, &MainWindow::downloadSelected);
    connect(playAction, &QAction::triggered, this, &MainWindow::playSelected);
    connect(flacAction, &QAction::triggered, this, [this]{ convertSelected("flac"); });
    connect(wavAction, &QAction::triggered, this, [this]{ convertSelected("wav"); });
    connect(playlistAction, &QAction::triggered, this, &MainWindow::createPlaylist);
    connect(newPlaylistButton, &QPushButton::clicked, this, &MainWindow::createPlaylist);
    connect(addPlaylistAction, &QAction::triggered, this, &MainWindow::addSelectedToPlaylist);
    connect(movePlaylistAction, &QAction::triggered, this, &MainWindow::moveSelectedToPlaylist);
    connect(viewPlaylistAction, &QAction::triggered, this, &MainWindow::showPlaylist);
    connect(removeAction, &QAction::triggered, this, &MainWindow::removeSelected);
    connect(updateAction, &QAction::triggered, this, [this] { checkForUpdates(true); });
    connect(aboutAction, &QAction::triggered, this, [this] {
        QMessageBox::about(this, QStringLiteral("DannyDee Track Manager"),
                           QStringLiteral("DannyDee Track Manager %1\n\n"
                                          "Music Library & FLAC Export")
                               .arg(QCoreApplication::applicationVersion()));
    });
    connect(m_updateButton, &QPushButton::clicked,
            this, &MainWindow::downloadAvailableUpdate);
    connect(m_search, &QLineEdit::textChanged, this, &MainWindow::refreshFilter);
    connect(m_genre, &QComboBox::currentTextChanged, this, &MainWindow::refreshFilter);
    connect(m_bpmMin, &QDoubleSpinBox::valueChanged, this, &MainWindow::refreshFilter);
    connect(m_bpmMax, &QDoubleSpinBox::valueChanged, this, &MainWindow::refreshFilter);
    connect(m_playlistList, &QListWidget::currentItemChanged, this,
            [this](QListWidgetItem *current) {
        if (!current) return;
        const int playlistId = current->data(Qt::UserRole).toInt();
        if (playlistId == m_playlistFilterId) return;
        m_playlistFilterId = playlistId;
        refreshFilter();
        setActivity(playlistId < 0 ? QStringLiteral("Alle Tracks")
                                   : QStringLiteral("Playlist: %1").arg(activePlaylistName()));
    });
    refreshPlaylistNavigation();
    m_status = new QLabel("Bereit – Links einfügen oder Audiodateien ablegen.");
    m_status->setObjectName("statusLabel");
    statusBar()->addWidget(m_status, 1);
    m_progress = new QProgressBar;
    m_progress->setObjectName("activityProgress");
    m_progress->setFixedWidth(170);
    m_progress->setFixedHeight(8);
    m_progress->setTextVisible(false);
    m_progress->hide();
    statusBar()->addPermanentWidget(m_progress);
}

void MainWindow::applyPlatformStyle()
{
    setStyleSheet(R"(
        QMainWindow, QDialog, QWidget#appRoot { background: #0B0D13; color: #F4F6FA; }
        QLabel#brandTitle { font-size: 26px; font-weight: 750; color: #FFFFFF; }
        QLabel#brandSubtitle, QLabel#statLabel, QLabel#sectionCaption, QLabel#statusLabel { color: #8B93A7; }
        QFrame#statCard, QFrame#filterCard, QFrame#libraryCard, QFrame#mixerCard {
            background: #131722; border: 1px solid #242A38; border-radius: 12px;
        }
        QFrame#deckCard { background: #0E121B; border: 1px solid #282F3E; border-radius: 9px; }
        QFrame#deckCard[deck="A"] { border-top: 2px solid #8B5CF6; }
        QFrame#deckCard[deck="B"] { border-top: 2px solid #2DD4BF; }
        QLabel#deckBadge { font-size: 11px; font-weight: 800; padding: 3px 7px;
            border-radius: 5px; color: #FFFFFF; }
        QLabel#deckBadge[deck="A"] { background: #5B3DB2; }
        QLabel#deckBadge[deck="B"] { background: #167D72; }
        QLabel#deckTrackTitle { color: #FFFFFF; font-size: 14px; font-weight: 700; }
        QLabel#deckState, QLabel#deckTime { color: #747E94; font-size: 10px; }
        QLabel#deckBpm { color: #E7DCFF; font-weight: 750; min-width: 92px; }
        QPushButton#deckPlayButton { min-width: 40px; padding: 0; font-size: 15px; }
        QPushButton#deckSyncButton { color: #A98BFF; border-color: #51417D; font-weight: 750; }
        QPushButton#deckSyncButton:checked { color: #07110F; background: #2DD4BF;
            border-color: #55E6D3; }
        QSlider#deckPosition::groove:horizontal { height: 4px; background: #252C3A; border-radius: 2px; }
        QSlider#deckPosition::sub-page:horizontal { background: #8B5CF6; border-radius: 2px; }
        QSlider#deckPosition::handle:horizontal { width: 12px; margin: -5px 0;
            background: #E7DCFF; border-radius: 6px; }
        QFrame#playlistSidebar { background: #0F131C; border: 0; border-right: 1px solid #252C3A;
            border-top-left-radius: 12px; border-bottom-left-radius: 12px; }
        QLabel#navigationTitle, QLabel#activePlaylistContext { color: #747E94; font-size: 10px;
            font-weight: 750; letter-spacing: 1px; }
        QLabel#navigationHint { color: #626C80; font-size: 10px; padding: 4px 2px; }
        QLabel#activePlaylistTitle { color: #FFFFFF; font-size: 19px; font-weight: 750; }
        QPushButton#playlistAddButton { min-height: 28px; min-width: 28px; padding: 0;
            font-size: 20px; font-weight: 500; color: #A98BFF; background: #1D1830;
            border: 1px solid #44376C; border-radius: 8px; }
        QPushButton#playlistAddButton:hover { background: #2A2047; border-color: #7C4DFF; }
        QListWidget#playlistList { background: transparent; color: #AAB2C2; border: 0; outline: 0; }
        QListWidget#playlistList::item { min-height: 38px; padding: 0 10px; margin: 2px 0;
            border-radius: 8px; }
        QListWidget#playlistList::item:hover { background: #191F2B; color: #FFFFFF; }
        QListWidget#playlistList::item:selected { background: #302653; color: #FFFFFF;
            border-left: 3px solid #9B7BFF; font-weight: 650; }
        QFrame#updateBanner { background: #17251F; border: 1px solid #2E7D70; border-radius: 10px; }
        QLabel#updateLabel { color: #A7F3D0; font-weight: 650; }
        QFrame#statCard[accent="violet"] { border-top: 3px solid #8B5CF6; }
        QFrame#statCard[accent="green"] { border-top: 3px solid #22D3A7; }
        QFrame#statCard[accent="blue"] { border-top: 3px solid #4EA8FF; }
        QLabel#statValue { font-size: 24px; font-weight: 700; color: #FFFFFF; }
        QLabel#sectionTitle { font-size: 15px; font-weight: 700; color: #FFFFFF; }
        QLabel#providerHint { color: #7DE7CD; padding: 5px 0; }
        QPushButton { min-height: 34px; padding: 0 14px; border-radius: 8px;
            border: 1px solid #303748; background: #1A1F2B; color: #E9ECF2; }
        QPushButton:hover { background: #242B3A; border-color: #465064; }
        QPushButton:pressed { background: #11151E; }
        QPushButton[role="primary"] { background: #7C4DFF; border-color: #8B5CF6; color: white; font-weight: 650; }
        QPushButton[role="primary"]:hover { background: #8B5CF6; }
        QPushButton[role="accent"] { color: #7DE7CD; border-color: #2E7D70; }
        QPushButton[role="danger"] { color: #FF8E9A; border-color: #6A3640; }
        QLineEdit, QComboBox, QDoubleSpinBox, QPlainTextEdit {
            min-height: 34px; padding: 0 10px; color: #F4F6FA; background: #0D1119;
            border: 1px solid #2A3140; border-radius: 8px; selection-background-color: #7C4DFF;
        }
        QPlainTextEdit { padding: 10px; }
        QLineEdit:focus, QComboBox:focus, QDoubleSpinBox:focus, QPlainTextEdit:focus { border-color: #8B5CF6; }
        QComboBox QAbstractItemView { background: #171C27; color: #F4F6FA; selection-background-color: #7C4DFF; }
        QTableView#trackTable { background: #10141D; alternate-background-color: #121722;
            border: 0; color: #E8EBF2; selection-background-color: #342A59;
            selection-color: #FFFFFF; outline: 0; }
        QTableView#trackTable::item { padding: 6px; border-bottom: 1px solid #202634; }
        QHeaderView::section { background: #171C27; color: #99A2B7; border: 0;
            border-bottom: 1px solid #2B3241; padding: 10px 8px; font-weight: 650; }
        QStatusBar { background: #0B0D13; color: #8B93A7; border-top: 1px solid #202634; }
        QStatusBar::item { border: 0; }
        QProgressBar#activityProgress { background: #202634; border: 0; border-radius: 4px; }
        QProgressBar#activityProgress::chunk { background: #22D3A7; border-radius: 4px; }
        QScrollBar:vertical { width: 10px; background: #10141D; margin: 0; }
        QScrollBar::handle:vertical { background: #343C4D; border-radius: 5px; min-height: 28px; }
        QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }
        QToolTip { color: #FFFFFF; background: #202634; border: 1px solid #3B4559; padding: 5px; }
    )");
}

void MainWindow::setActivity(const QString &message, int progress)
{
    m_status->setText(message);
    if (progress == -2) {
        m_progress->hide();
    } else {
        m_progress->show();
        if (progress < 0) {
            m_progress->setRange(0, 0);
        } else {
            m_progress->setRange(0, 100);
            m_progress->setValue(progress);
            if (progress >= 100 && !m_batchExportActive)
                QTimer::singleShot(1200, m_progress, &QWidget::hide);
        }
    }
}

void MainWindow::updateLibrarySummary()
{
    QSqlQuery query(m_database.connection());
    if (!query.exec("SELECT COUNT(*), "
                    "SUM(CASE WHEN local_path IS NOT NULL AND local_path <> '' THEN 1 ELSE 0 END), "
                    "SUM(CASE WHEN source_url LIKE 'http://%' OR source_url LIKE 'https://%' THEN 1 ELSE 0 END) "
                    "FROM tracks") || !query.next()) return;
    const int total = query.value(0).toInt();
    m_trackCount->setText(QString::number(total));
    m_localCount->setText(QString::number(query.value(1).toInt()));
    m_linkCount->setText(QString::number(query.value(2).toInt()));
    refreshPlaylistNavigation();
    if (m_model) refreshFilter();
}

QString MainWindow::activePlaylistName() const
{
    if (m_playlistFilterId < 0) return QStringLiteral("Alle Tracks");
    if (m_playlistList && m_playlistList->currentItem()
        && m_playlistList->currentItem()->data(Qt::UserRole).toInt() == m_playlistFilterId) {
        return m_playlistList->currentItem()->data(Qt::UserRole + 1).toString();
    }
    QSqlQuery query(m_database.connection());
    query.prepare("SELECT name FROM playlists WHERE id=?");
    query.addBindValue(m_playlistFilterId);
    return query.exec() && query.next() ? query.value(0).toString()
                                        : QStringLiteral("Unbekannte Playlist");
}

void MainWindow::refreshPlaylistNavigation()
{
    if (!m_playlistList) return;
    const QSignalBlocker blocker(m_playlistList);
    m_playlistList->clear();

    int totalTracks = 0;
    QSqlQuery total(m_database.connection());
    if (total.exec("SELECT COUNT(*) FROM tracks") && total.next()) totalTracks = total.value(0).toInt();

    auto *allTracks = new QListWidgetItem(QStringLiteral("Alle Tracks  ·  %1").arg(totalTracks),
                                          m_playlistList);
    allTracks->setData(Qt::UserRole, -1);
    allTracks->setData(Qt::UserRole + 1, QStringLiteral("Alle Tracks"));
    allTracks->setToolTip(QStringLiteral("Gesamte Bibliothek anzeigen"));

    QSqlQuery playlists(m_database.connection());
    playlists.exec("SELECT p.id,p.name,p.provider,COUNT(pt.track_id) "
                   "FROM playlists p LEFT JOIN playlist_tracks pt ON pt.playlist_id=p.id "
                   "GROUP BY p.id,p.name,p.provider ORDER BY p.name COLLATE NOCASE");
    bool activeFound = m_playlistFilterId < 0;
    while (playlists.next()) {
        const int id = playlists.value(0).toInt();
        const QString name = playlists.value(1).toString();
        const QString provider = playlists.value(2).toString();
        const int count = playlists.value(3).toInt();
        auto *item = new QListWidgetItem(QStringLiteral("%1  ·  %2").arg(name).arg(count),
                                         m_playlistList);
        item->setData(Qt::UserRole, id);
        item->setData(Qt::UserRole + 1, name);
        item->setData(Qt::UserRole + 2, provider);
        item->setToolTip(provider.isEmpty()
                             ? QStringLiteral("Playlist '%1' anzeigen").arg(name)
                             : QStringLiteral("%1 · importiert von %2").arg(name, provider));
        if (id == m_playlistFilterId) activeFound = true;
    }

    if (!activeFound) m_playlistFilterId = -1;
    for (int row = 0; row < m_playlistList->count(); ++row) {
        if (m_playlistList->item(row)->data(Qt::UserRole).toInt() == m_playlistFilterId) {
            m_playlistList->setCurrentRow(row);
            break;
        }
    }
}

void MainWindow::activatePlaylist(int playlistId)
{
    if (!m_playlistList) return;
    for (int row = 0; row < m_playlistList->count(); ++row) {
        QListWidgetItem *item = m_playlistList->item(row);
        if (item->data(Qt::UserRole).toInt() != playlistId) continue;
        if (m_playlistFilterId == playlistId) {
            m_playlistList->setCurrentRow(row);
            refreshFilter();
        } else {
            m_playlistList->setCurrentRow(row);
        }
        m_playlistList->scrollToItem(item);
        return;
    }
}

void MainWindow::refreshFilter()
{
    QString term = m_search->text();
    term.replace("'", "''");
    QStringList clauses;
    if (!term.isEmpty()) clauses << QString("(title LIKE '%%1%' OR artist LIKE '%%1%' OR label LIKE '%%1%')").arg(term);
    if (m_genre->currentIndex() > 0) {
        QString genre = m_genre->currentText(); genre.replace("'", "''");
        clauses << QString("genre='%1'").arg(genre);
    }
    clauses << QString("bpm >= %1 AND bpm <= %2").arg(m_bpmMin->value()).arg(m_bpmMax->value());
    if (m_playlistFilterId >= 0)
        clauses << QString("id IN (SELECT track_id FROM playlist_tracks WHERE playlist_id=%1)").arg(m_playlistFilterId);
    m_model->setFilter(clauses.join(" AND "));
    m_model->select();
    const QString playlistName = activePlaylistName();
    if (m_activePlaylistTitle) m_activePlaylistTitle->setText(playlistName);
    if (m_activePlaylistContext) {
        m_activePlaylistContext->setText(m_playlistFilterId < 0
            ? QStringLiteral("BIBLIOTHEK / ALLE TRACKS")
            : QStringLiteral("BIBLIOTHEK / PLAYLIST"));
    }
    if (m_libraryCaption) {
        const int visible = m_model->rowCount();
        m_libraryCaption->setText(QStringLiteral("%1 Track%2 sichtbar")
                                      .arg(visible).arg(visible == 1 ? QString() : QStringLiteral("s")));
    }
}

void MainWindow::importFiles()
{
    const auto files = QFileDialog::getOpenFileNames(this, "Audiodateien importieren", {},
        "Audio (*.flac *.wav *.aiff *.aif *.mp3 *.m4a *.ogg);;Alle Dateien (*)");
    importFilePaths(files);
}

void MainWindow::importFilePaths(const QStringList &paths)
{
    static const QSet<QString> supported{"flac", "wav", "aiff", "aif", "mp3", "m4a", "ogg"};
    int imported = 0;
    for (const auto &path : paths) {
        QFileInfo info(path);
        if (!info.isFile() || !supported.contains(info.suffix().toLower())) continue;
        const ProbedTrack metadata = AudioProbe::read(path);
        m_database.addTrack(metadata.title, metadata.artist.isEmpty() ? "Unbekannt" : metadata.artist,
                            metadata.genre.isEmpty() ? "Other" : metadata.genre, metadata.bpm,
                            metadata.musicalKey, 5, metadata.label, metadata.releaseDate,
                            QUrl::fromLocalFile(path).toString(), path, true);
        ++imported;
    }
    m_model->select();
    updateLibrarySummary();
    setActivity(QString("%1 Datei(en) importiert; Metadaten können direkt editiert werden.").arg(imported));
}

void MainWindow::dragEnterEvent(QDragEnterEvent *event)
{
    if (!event->mimeData()->hasUrls()) return;
    for (const QUrl &url : event->mimeData()->urls()) {
        if (url.isLocalFile()) {
            event->acceptProposedAction();
            return;
        }
    }
}

void MainWindow::dropEvent(QDropEvent *event)
{
    QStringList paths;
    for (const QUrl &url : event->mimeData()->urls()) {
        if (url.isLocalFile()) paths << url.toLocalFile();
    }
    if (!paths.isEmpty()) {
        importFilePaths(paths);
        event->acceptProposedAction();
    }
}

void MainWindow::addReferences()
{
    if (m_linkImportActive || m_playlistImporter.isBusy()) {
        QMessageBox::information(this, "Links importieren", "Es wird bereits eine Link- oder Playlist-Liste eingelesen.");
        return;
    }
    QDialog dialog(this);
    dialog.setWindowTitle("Links importieren");
    dialog.resize(640, 430);
    auto *layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(22, 20, 22, 20);
    layout->setSpacing(12);
    auto *title = new QLabel("Mehrere Links auf einmal hinzufügen");
    title->setObjectName("sectionTitle");
    auto *description = new QLabel(
        "Füge einzelne Tracks, Playlists oder Alben ein. Ein Link pro Zeile ist am übersichtlichsten; "
        "Duplikate werden automatisch übersprungen.");
    description->setWordWrap(true);
    description->setObjectName("sectionCaption");
    auto *providers = new QLabel("Spotify   •   YouTube   •   TikTok   •   SoundCloud   •   Bandcamp   •   weitere");
    providers->setObjectName("providerHint");
    auto *editor = new QPlainTextEdit;
    editor->setPlaceholderText("https://open.spotify.com/…\nhttps://www.youtube.com/…\nhttps://soundcloud.com/…");
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel | QDialogButtonBox::Ok);
    buttons->button(QDialogButtonBox::Ok)->setText("Links importieren");
    buttons->button(QDialogButtonBox::Ok)->setProperty("role", "primary");
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(title);
    layout->addWidget(description);
    layout->addWidget(providers);
    layout->addWidget(editor, 1);
    layout->addWidget(buttons);
    editor->setFocus();
    if (dialog.exec() != QDialog::Accepted) return;
    const QString input = editor->toPlainText();
    if (input.trimmed().isEmpty()) return;

    const QRegularExpression urlPattern(R"(https?://[^\s<>"']+)",
                                        QRegularExpression::CaseInsensitiveOption);
    auto matches = urlPattern.globalMatch(input);
    QSet<QString> queued;
    m_linkImportQueue.clear();
    while (matches.hasNext()) {
        QString text = matches.next().captured(0);
        while (text.endsWith('.') || text.endsWith(',') || text.endsWith(';')) text.chop(1);
        const auto result = LinkResolver::resolve(text);
        const QString normalized = result.url.toString(QUrl::FullyEncoded);
        if (!result.url.isValid() || normalized.isEmpty()) continue;
        if (queued.contains(normalized)) continue;
        queued.insert(normalized);
        m_linkImportQueue.enqueue({result.url, result.provider});
    }
    if (m_linkImportQueue.isEmpty()) {
        QMessageBox::information(this, "Links importieren", "Es wurden keine neuen HTTP- oder HTTPS-Links gefunden.");
        return;
    }

    m_linkImportActive = true;
    m_linkImportTotal = m_linkImportQueue.size();
    m_linkImportProcessed = 0;
    m_linkImportTracksAdded = 0;
    m_linkImportTracksReused = 0;
    m_linkImportPlaylists = 0;
    m_lastImportedPlaylistId = -1;
    m_linkImportErrors.clear();
    startNextLinkImport();
}

void MainWindow::startNextLinkImport()
{
    if (!m_linkImportActive) return;
    if (m_linkImportQueue.isEmpty()) {
        m_linkImportActive = false;
        m_model->select();
        updateLibrarySummary();
        if (m_lastImportedPlaylistId >= 0) activatePlaylist(m_lastImportedPlaylistId);
        else refreshFilter();
        const QString summary = QStringLiteral(
            "%1 Link(s) verarbeitet: %2 neue Tracks, %3 vorhandene Tracks, %4 Playlist(s).")
                .arg(m_linkImportProcessed)
                .arg(m_linkImportTracksAdded)
                .arg(m_linkImportTracksReused)
                .arg(m_linkImportPlaylists);
        setActivity(summary);
        QString details = summary;
        if (!m_linkImportErrors.isEmpty()) {
            details += QStringLiteral("\n\nNicht vollständig aufgelöst; als einzelne Links gespeichert:\n")
                       + m_linkImportErrors.mid(0, 8).join(QStringLiteral("\n\n"));
        }
        QMessageBox::information(this, "Links importieren", details);
        return;
    }

    const PendingLinkImport item = m_linkImportQueue.dequeue();
    setActivity(QStringLiteral("Link %1/%2 wird analysiert: %3")
                    .arg(m_linkImportProcessed + 1)
                    .arg(m_linkImportTotal)
                    .arg(item.url.toString()), -1);
    m_playlistImporter.resolve(item.url, item.provider);
}

void MainWindow::storeResolvedLink(const PlaylistImportResult &result)
{
    QList<ImportedPlaylistTrack> tracks = result.tracks;
    std::stable_sort(tracks.begin(), tracks.end(), [](const auto &left, const auto &right) {
        return left.position < right.position;
    });

    int playlistId = -1;
    if (result.isCollection) {
        playlistId = m_database.ensurePlaylist(result.name,
                                               result.requestedUrl.toString(QUrl::FullyEncoded),
                                               result.provider);
        if (playlistId < 0) {
            m_linkImportErrors << QStringLiteral("%1: %2")
                                      .arg(result.requestedUrl.toString(), m_database.lastError());
            return;
        }
        ++m_linkImportPlaylists;
        m_lastImportedPlaylistId = playlistId;
    }

    for (const ImportedPlaylistTrack &track : tracks) {
        int trackId = m_database.findTrackBySourceUrl(track.sourceUrl);
        if (trackId < 0) {
            trackId = m_database.addTrackReturningId(
                track.title, track.artist, track.genre.isEmpty() ? QStringLiteral("Other") : track.genre,
                0, {}, 5, track.label, track.releaseDate, track.sourceUrl, {}, false);
            if (trackId < 0) {
                m_linkImportErrors << QStringLiteral("%1: %2").arg(track.sourceUrl, m_database.lastError());
                continue;
            }
            ++m_linkImportTracksAdded;
        } else {
            ++m_linkImportTracksReused;
            QSqlQuery update(m_database.connection());
            update.prepare("UPDATE tracks SET "
                           "title=CASE WHEN title='Unbekannter Track' THEN ? ELSE title END, "
                           "artist=CASE WHEN artist IN ('Unbekannt','Spotify','SoundCloud','YouTube','TikTok','Bandcamp') "
                           "THEN ? ELSE artist END, "
                           "label=COALESCE(NULLIF(label,''),?), release_date=COALESCE(NULLIF(release_date,''),?) "
                           "WHERE id=?");
            update.addBindValue(track.title);
            update.addBindValue(track.artist);
            update.addBindValue(track.label);
            update.addBindValue(track.releaseDate);
            update.addBindValue(trackId);
            update.exec();
        }
        if (playlistId >= 0 && !m_database.addToPlaylist(playlistId, trackId))
            m_linkImportErrors << QStringLiteral("%1: %2").arg(track.sourceUrl, m_database.lastError());
    }
}

int MainWindow::selectedTrackId() const
{
    const QList<int> ids = selectedTrackIds();
    return ids.isEmpty() ? -1 : ids.first();
}

QList<int> MainWindow::selectedTrackIds() const
{
    QList<int> ids;
    if (!m_table || !m_table->selectionModel()) return ids;
    for (const QModelIndex &row : m_table->selectionModel()->selectedRows()) {
        const int id = m_model->data(m_model->index(row.row(), 0)).toInt();
        if (id >= 0 && !ids.contains(id)) ids << id;
    }
    return ids;
}

void MainWindow::convertSelected(const QString &format)
{
    const auto rows = m_table->selectionModel()->selectedRows();
    if (rows.isEmpty()) { QMessageBox::information(this, "Export", "Bitte zuerst einen Track auswählen."); return; }
    const int row = rows.first().row();
    const QString source = m_model->data(m_model->index(row, 10)).toString();
    const bool licensed = m_model->data(m_model->index(row, 11)).toBool();
    if (source.isEmpty() || !licensed) {
        QMessageBox::warning(this, "Export", "Für diesen Eintrag ist noch keine lokale Audiodatei vorhanden.");
        return;
    }
    const QString target = QFileDialog::getSaveFileName(this, "Exportieren", QFileInfo(source).completeBaseName() + "." + format,
                                                         format == "flac" ? "FLAC (*.flac)" : "WAV (*.wav)");
    if (!target.isEmpty()) { setActivity("Konvertierung läuft …", -1); m_converter.convert(source, target, format); }
}

void MainWindow::downloadSelected()
{
    if (m_downloader.isBusy() || m_batchExportActive) {
        QMessageBox::information(this, "Download", "Es läuft bereits ein Download.");
        return;
    }
    const auto rows = m_table->selectionModel()->selectedRows();
    if (rows.isEmpty()) {
        QMessageBox::information(this, "Download", "Bitte zuerst einen Track auswählen.");
        return;
    }
    const int row = rows.first().row();
    m_downloadTrackId = m_model->data(m_model->index(row, 0)).toInt();
    const QUrl source(m_model->data(m_model->index(row, 9)).toString());
    m_downloadSourceUrl = source;
    if (!source.isValid() || source.isLocalFile()) {
        QMessageBox::information(this, "Download", "Dieser Eintrag besitzt keinen herunterladbaren Internet-Link.");
        return;
    }
    const bool spotify = source.host().contains("spotify.com", Qt::CaseInsensitive)
                         || source.host().compare("spotify.link", Qt::CaseInsensitive) == 0;
    if (spotify) {
        QMessageBox::information(
            this, "Spotify-Referenz",
            "Spotify-Tracks und -Playlists werden als Metadaten und Original-Links verwaltet. "
            "Im strikten Quellenmodus wird keine YouTube- oder andere Audio-Ersatzquelle verwendet. "
            "Importiere eine rechtmäßig vorhandene lokale Audiodatei, um sie zu konvertieren oder abzuspielen.");
        return;
    }
    if (!m_downloader.mediaDownloaderAvailable(source)) {
        QMessageBox box(this);
        box.setWindowTitle("yt-dlp fehlt");
        box.setText("Für diesen Link werden yt-dlp und FFmpeg benötigt.");
        auto *open = box.addButton("Download-Seite öffnen", QMessageBox::ActionRole);
        box.addButton(QMessageBox::Ok);
        box.exec();
        if (box.clickedButton() == open)
            QDesktopServices::openUrl(QUrl("https://github.com/yt-dlp/yt-dlp/releases/latest"));
        return;
    }

    bool formatAccepted = false;
    const QString formatLabel = QInputDialog::getItem(
        this, "Audioformat", "Ausgabeformat:",
        {"MP3 (beste VBR-Qualität)", "FLAC (verlustfrei kodiert; keine Qualitätssteigerung)"},
        0, false, &formatAccepted);
    if (!formatAccepted) return;
    const QString target = QFileDialog::getExistingDirectory(this, "Download-Ordner",
        QStandardPaths::writableLocation(QStandardPaths::DownloadLocation));
    if (target.isEmpty()) return;
    m_downloader.downloadMedia(source, target, formatLabel.startsWith("FLAC") ? "flac" : "mp3");
}

void MainWindow::exportAllLinksAsFlac()
{
    if (m_downloader.isBusy() || m_batchExportActive) {
        QMessageBox::information(this, "FLAC-Export", "Es läuft bereits ein Download.");
        return;
    }
    const QString target = QFileDialog::getExistingDirectory(
        this, "Zielordner für alle FLAC-Dateien",
        QStandardPaths::writableLocation(QStandardPaths::MusicLocation));
    if (target.isEmpty()) return;

    QSqlQuery query(m_database.connection());
    query.exec("SELECT id,source_url FROM tracks WHERE source_url LIKE 'http://%' OR source_url LIKE 'https://%' ORDER BY id");
    QSet<QString> queuedUrls;
    m_batchQueue.clear();
    while (query.next()) {
        const QUrl url(query.value(1).toString());
        const QString normalized = url.toString();
        if (!url.isValid() || queuedUrls.contains(normalized)) continue;
        queuedUrls.insert(normalized);
        m_batchQueue.enqueue({query.value(0).toInt(), url});
    }
    if (m_batchQueue.isEmpty()) {
        QMessageBox::information(this, "FLAC-Export", "Es sind keine Internet-Links in der Bibliothek vorhanden.");
        return;
    }

    m_batchTargetDirectory = target;
    m_batchFailures.clear();
    m_batchTotal = m_batchQueue.size();
    m_batchProcessed = 0;
    m_batchFilesCreated = 0;
    m_batchExportActive = true;
    startNextBatchDownload();
}

void MainWindow::startNextBatchDownload()
{
    if (!m_batchExportActive) return;
    if (m_batchQueue.isEmpty()) {
        m_batchExportActive = false;
        m_model->select();
        updateLibrarySummary();
        const QString summary = QString("FLAC-Export abgeschlossen: %1 von %2 Link(s), %3 Datei(en), %4 Fehler.")
            .arg(m_batchProcessed - m_batchFailures.size())
            .arg(m_batchTotal)
            .arg(m_batchFilesCreated)
            .arg(m_batchFailures.size());
        setActivity(summary);
        QString details = summary;
        if (!m_batchFailures.isEmpty())
            details += "\n\nNicht verarbeitet:\n" + m_batchFailures.mid(0, 10).join("\n\n");
        QMessageBox::information(this, "FLAC-Export", details);
        return;
    }

    m_batchCurrent = m_batchQueue.dequeue();
    m_downloadTrackId = m_batchCurrent.trackId;
    m_downloadSourceUrl = m_batchCurrent.url;
    setActivity(QString("FLAC-Export %1/%2: %3")
        .arg(m_batchProcessed + 1).arg(m_batchTotal).arg(m_batchCurrent.url.toString()),
        m_batchTotal > 0 ? m_batchProcessed * 100 / m_batchTotal : 0);
    m_downloader.downloadMedia(m_batchCurrent.url, m_batchTargetDirectory, "flac");
}

void MainWindow::createPlaylist()
{
    bool ok = false;
    const QString name = QInputDialog::getText(this, "Playlist", "Name:", QLineEdit::Normal, {}, &ok).trimmed();
    if (!ok || name.isEmpty()) return;
    const int playlistId = m_database.ensurePlaylist(name);
    if (playlistId < 0) {
        QMessageBox::warning(this, "Playlist", m_database.lastError());
        return;
    }
    refreshPlaylistNavigation();
    activatePlaylist(playlistId);
    setActivity(QStringLiteral("Playlist '%1' erstellt.").arg(name));
}

void MainWindow::addSelectedToPlaylist()
{
    const QList<int> trackIds = selectedTrackIds();
    if (trackIds.isEmpty()) { QMessageBox::information(this, "Playlist", "Bitte mindestens einen Track auswählen."); return; }
    QSqlQuery q(m_database.connection());
    q.exec("SELECT id,name FROM playlists ORDER BY name");
    QStringList names; QList<int> ids;
    while (q.next()) { ids << q.value(0).toInt(); names << q.value(1).toString(); }
    if (names.isEmpty()) { QMessageBox::information(this, "Playlist", "Bitte zuerst eine Playlist erstellen."); return; }
    bool ok = false;
    const QString name = QInputDialog::getItem(this, "In Playlist kopieren", "Ziel-Playlist:", names, 0, false, &ok);
    if (!ok) return;
    const int targetId = ids.value(names.indexOf(name), -1);
    int copied = 0;
    for (const int trackId : trackIds) {
        if (m_database.addToPlaylist(targetId, trackId)) ++copied;
    }
    refreshPlaylistNavigation();
    setActivity(QString("%1 Track(s) in '%2' kopiert.").arg(copied).arg(name));
}

void MainWindow::moveSelectedToPlaylist()
{
    const QList<int> selectedIds = selectedTrackIds();
    if (selectedIds.isEmpty()) {
        QMessageBox::information(this, "Playlist", "Bitte mindestens einen Track auswählen.");
        return;
    }

    QSqlQuery q(m_database.connection());
    q.exec("SELECT id,name FROM playlists ORDER BY name");
    QStringList names;
    QList<int> ids;
    while (q.next()) { ids << q.value(0).toInt(); names << q.value(1).toString(); }
    if (ids.size() < 2) {
        QMessageBox::information(this, "Playlist verschieben",
                                 "Zum Verschieben werden mindestens zwei Playlists benötigt.");
        return;
    }

    int sourceId = m_playlistFilterId;
    QString sourceName;
    if (sourceId >= 0) {
        sourceName = names.value(ids.indexOf(sourceId));
    } else {
        bool sourceAccepted = false;
        sourceName = QInputDialog::getItem(this, "Aus Playlist verschieben",
                                           "Quell-Playlist:", names, 0, false, &sourceAccepted);
        if (!sourceAccepted) return;
        sourceId = ids.value(names.indexOf(sourceName), -1);
    }

    QStringList targetNames;
    QList<int> targetIds;
    for (int index = 0; index < ids.size(); ++index) {
        if (ids.at(index) == sourceId) continue;
        targetIds << ids.at(index);
        targetNames << names.at(index);
    }
    bool targetAccepted = false;
    const QString targetName = QInputDialog::getItem(this, "In Playlist verschieben",
                                                     "Ziel-Playlist:", targetNames, 0, false,
                                                     &targetAccepted);
    if (!targetAccepted) return;
    const int targetId = targetIds.value(targetNames.indexOf(targetName), -1);

    QList<int> movableIds;
    QSqlQuery membership(m_database.connection());
    membership.prepare("SELECT 1 FROM playlist_tracks WHERE playlist_id=? AND track_id=?");
    for (const int trackId : selectedIds) {
        membership.bindValue(0, sourceId);
        membership.bindValue(1, trackId);
        if (membership.exec() && membership.next()) movableIds << trackId;
        membership.finish();
    }
    if (movableIds.isEmpty()) {
        QMessageBox::information(this, "Playlist verschieben",
                                 QString("Die Auswahl ist nicht in '%1' enthalten.").arg(sourceName));
        return;
    }
    if (!m_database.moveTracksBetweenPlaylists(sourceId, targetId, movableIds)) {
        QMessageBox::warning(this, "Playlist verschieben", m_database.lastError());
        return;
    }
    refreshPlaylistNavigation();
    activatePlaylist(targetId);
    setActivity(QString("%1 Track(s) von '%2' nach '%3' verschoben.")
                    .arg(movableIds.size()).arg(sourceName, targetName));
}

void MainWindow::showPlaylist()
{
    m_playlistList->setFocus(Qt::ShortcutFocusReason);
    if (m_playlistList->currentItem()) m_playlistList->scrollToItem(m_playlistList->currentItem());
    setActivity(QStringLiteral("Playlist-Navigation aktiv – mit Pfeiltasten wechseln."));
}

void MainWindow::loadSelectedIntoDeck(int deckIndex)
{
    const auto rows = m_table->selectionModel()->selectedRows();
    if (rows.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("Deck laden"),
                                 QStringLiteral("Bitte zuerst einen lokalen Track auswählen."));
        return;
    }
    const int row = rows.first().row();
    const QString filePath = m_model->data(m_model->index(row, 10)).toString();
    if (filePath.isEmpty() || !QFileInfo::exists(filePath)) {
        QMessageBox::information(this, QStringLiteral("Deck laden"),
                                 QStringLiteral("Dieser Eintrag besitzt keine verfügbare lokale Audiodatei."));
        return;
    }

    DeckState &deck = m_decks.at(static_cast<size_t>(deckIndex));
    DeckState &otherDeck = m_decks.at(static_cast<size_t>(1 - deckIndex));
    otherDeck.syncEnabled = false;
    otherDeck.syncButton->setChecked(false);
    deck.player->stop();
    deck.player->setPlaybackRate(1.0);
    deck.player->setSource(QUrl::fromLocalFile(filePath));
    deck.trackId = m_model->data(m_model->index(row, 0)).toInt();
    deck.filePath = filePath;
    deck.bpm = m_model->data(m_model->index(row, 4)).toDouble();
    deck.firstBeatMs = m_model->data(m_model->index(row, 13)).toDouble();
    deck.confidence = m_model->data(m_model->index(row, 14)).toDouble();
    deck.beatgridAnalyzed = m_model->data(m_model->index(row, 15)).toBool()
                            && deck.bpm > 0.0;
    deck.nominalSyncRate = 1.0;
    deck.syncEnabled = false;
    deck.syncButton->setChecked(false);
    deck.sliderPressed = false;
    deck.positionSlider->setValue(0);
    deck.positionSlider->setEnabled(true);

    const QString title = m_model->data(m_model->index(row, 1)).toString();
    const QString artist = m_model->data(m_model->index(row, 2)).toString();
    deck.titleLabel->setText(artist.isEmpty() ? title
                                              : QStringLiteral("%1 — %2").arg(artist, title));
    deck.titleLabel->setToolTip(filePath);
    deck.grid->setTrackLoaded(true);
    if (deck.beatgridAnalyzed) {
        deck.grid->setGrid(deck.bpm, deck.firstBeatMs, deck.confidence);
        deck.stateLabel->setText(QStringLiteral("Beatgrid bereit · %1% Sicherheit")
                                     .arg(qRound(deck.confidence * 100.0)));
    } else {
        deck.grid->setGrid(0.0, 0.0, 0.0);
        deck.stateLabel->setText(QStringLiteral("Analyse wird vorbereitet …"));
        m_beatAnalyzer.enqueue(deck.trackId, deck.filePath);
    }
    updateDeckDisplays();
    setActivity(QStringLiteral("%1 in Deck %2 geladen.")
                    .arg(deck.titleLabel->text(), deckIndex == 0 ? "A" : "B"));
}

void MainWindow::toggleDeckPlayback(int deckIndex)
{
    DeckState &deck = m_decks.at(static_cast<size_t>(deckIndex));
    if (deck.trackId < 0 || deck.filePath.isEmpty()) return;
    if (deck.player->playbackState() == QMediaPlayer::PlayingState) {
        deck.player->pause();
        deck.stateLabel->setText(QStringLiteral("Pausiert"));
    } else {
        if (deck.syncEnabled) applySyncCorrection(deckIndex);
        deck.player->play();
        deck.stateLabel->setText(deck.syncEnabled ? QStringLiteral("Sync aktiv")
                                                  : QStringLiteral("Wiedergabe"));
    }
    updateDeckDisplays();
}

void MainWindow::syncDeck(int deckIndex)
{
    DeckState &target = m_decks.at(static_cast<size_t>(deckIndex));
    DeckState &master = m_decks.at(static_cast<size_t>(1 - deckIndex));
    if (target.syncEnabled) {
        target.syncEnabled = false;
        target.syncButton->setChecked(false);
        target.nominalSyncRate = target.player->playbackRate();
        target.stateLabel->setText(QStringLiteral("Sync deaktiviert"));
        return;
    }
    if (!target.beatgridAnalyzed || !master.beatgridAnalyzed
        || target.bpm <= 0.0 || master.bpm <= 0.0) {
        target.syncButton->setChecked(false);
        QMessageBox::information(this, QStringLiteral("Sync"),
                                 QStringLiteral("Beide Decks benötigen ein analysiertes Beatgrid."));
        return;
    }

    master.syncEnabled = false;
    master.syncButton->setChecked(false);
    target.nominalSyncRate = BeatSyncMath::tempoRatio(
        target.bpm, master.bpm, master.player->playbackRate());
    target.player->setPlaybackRate(target.nominalSyncRate);
    target.syncEnabled = true;
    target.syncButton->setChecked(true);
    applySyncCorrection(deckIndex);
    target.stateLabel->setText(QStringLiteral("Sync mit Deck %1 aktiv")
                                   .arg(deckIndex == 0 ? "B" : "A"));
    setActivity(QStringLiteral("Deck %1 synchronisiert: %2 BPM, Beatphase ausgerichtet.")
                    .arg(deckIndex == 0 ? "A" : "B")
                    .arg(target.bpm * target.nominalSyncRate, 0, 'f', 2));
}

void MainWindow::applySyncCorrection(int deckIndex)
{
    DeckState &target = m_decks.at(static_cast<size_t>(deckIndex));
    DeckState &master = m_decks.at(static_cast<size_t>(1 - deckIndex));
    if (!target.syncEnabled || !target.beatgridAnalyzed || !master.beatgridAnalyzed
        || target.bpm <= 0.0 || master.bpm <= 0.0) return;

    const double driftMs = BeatSyncMath::driftMilliseconds(
        target.player->position(), target.bpm, target.firstBeatMs, target.nominalSyncRate,
        master.player->position(), master.bpm, master.firstBeatMs);

    if (master.player->playbackState() != QMediaPlayer::PlayingState) {
        target.player->setPlaybackRate(target.nominalSyncRate);
        return;
    }

    if (target.player->playbackState() != QMediaPlayer::PlayingState
        || std::abs(driftMs) > 55.0) {
        target.player->setPosition(BeatSyncMath::alignedPosition(
            target.player->position(), target.player->duration(), target.bpm, target.firstBeatMs,
            master.player->position(), master.bpm, master.firstBeatMs));
        target.player->setPlaybackRate(target.nominalSyncRate);
    } else {
        target.player->setPlaybackRate(
            BeatSyncMath::correctedPlaybackRate(target.nominalSyncRate, driftMs));
    }

    target.stateLabel->setText(QStringLiteral("Sync aktiv · Drift %1 ms")
                                   .arg(qRound(std::abs(driftMs))));
}

void MainWindow::updateDeckDisplays()
{
    for (int deckIndex = 0; deckIndex < static_cast<int>(m_decks.size()); ++deckIndex) {
        DeckState &deck = m_decks.at(static_cast<size_t>(deckIndex));
        if (!deck.player) continue;
        if (deck.syncEnabled) applySyncCorrection(deckIndex);

        const qint64 position = deck.player->position();
        const qint64 duration = deck.player->duration();
        if (!deck.sliderPressed && duration > 0)
            deck.positionSlider->setValue(qRound(position * 1000.0 / duration));
        deck.timeLabel->setText(QStringLiteral("%1 / %2").arg(deckTime(position), deckTime(duration)));
        deck.grid->setPlayback(position, duration, deck.player->playbackRate());
        deck.playButton->setEnabled(deck.trackId >= 0);
        deck.playButton->setText(deck.player->playbackState() == QMediaPlayer::PlayingState
                                     ? QStringLiteral("Ⅱ") : QStringLiteral("▶"));
        if (deck.beatgridAnalyzed) {
            const double effectiveBpm = deck.bpm * deck.player->playbackRate();
            deck.bpmLabel->setText(qFuzzyCompare(deck.player->playbackRate(), 1.0)
                ? QStringLiteral("%1 BPM").arg(deck.bpm, 0, 'f', 2)
                : QStringLiteral("%1 BPM").arg(effectiveBpm, 0, 'f', 2));
        } else if (deck.bpm > 0.0) {
            deck.bpmLabel->setText(QStringLiteral("~%1 BPM").arg(deck.bpm, 0, 'f', 2));
        } else {
            deck.bpmLabel->setText(QStringLiteral("— BPM"));
        }
    }
    const bool bothReady = m_decks[0].beatgridAnalyzed && m_decks[1].beatgridAnalyzed;
    m_decks[0].syncButton->setEnabled(bothReady);
    m_decks[1].syncButton->setEnabled(bothReady);
}

void MainWindow::playSelected()
{
    if (m_player->playbackState() == QMediaPlayer::PlayingState) {
        m_player->pause();
        m_status->setText("Wiedergabe pausiert.");
        return;
    }
    if (m_player->playbackState() == QMediaPlayer::PausedState) {
        m_player->play();
        m_status->setText("Wiedergabe fortgesetzt.");
        return;
    }
    const auto rows = m_table->selectionModel()->selectedRows();
    if (rows.isEmpty()) { QMessageBox::information(this, "Wiedergabe", "Bitte einen Track auswählen."); return; }
    const QString path = m_model->data(m_model->index(rows.first().row(), 10)).toString();
    if (path.isEmpty() || !QFileInfo::exists(path)) {
        QMessageBox::information(this, "Wiedergabe", "Für diesen Eintrag ist keine lokale Audiodatei vorhanden.");
        return;
    }
    m_player->setSource(QUrl::fromLocalFile(path));
    m_player->play();
    m_status->setText("Wiedergabe: " + QFileInfo(path).fileName());
}

void MainWindow::removeSelected()
{
    const int id = selectedTrackId();
    if (id < 0) { QMessageBox::information(this, "Entfernen", "Bitte einen Track auswählen."); return; }
    if (QMessageBox::question(this, "Eintrag entfernen",
            "Soll der Bibliothekseintrag entfernt werden? Die Audiodatei selbst bleibt erhalten.") != QMessageBox::Yes) return;
    QSqlQuery q(m_database.connection());
    q.prepare("DELETE FROM tracks WHERE id=?");
    q.addBindValue(id);
    if (!q.exec()) QMessageBox::warning(this, "Entfernen", q.lastError().text());
    m_model->select();
    updateLibrarySummary();
}

void MainWindow::checkForUpdates(bool manual)
{
    if (m_updater.isBusy()) {
        if (manual) {
            QMessageBox::information(this, QStringLiteral("Updates"),
                                     QStringLiteral("Eine Update-Pr\u00fcfung oder ein Download l\u00e4uft bereits."));
        }
        return;
    }
    m_manualUpdateCheck = manual;
    setActivity(QStringLiteral("Suche nach Updates \u2026"), -1);
    m_updater.checkForUpdates();
}

void MainWindow::downloadAvailableUpdate()
{
    if (m_updater.isBusy()) return;
    m_updateButton->setEnabled(false);
    m_updateButton->setText(QStringLiteral("Wird geladen \u2026"));
    m_updater.downloadUpdate();
}

void MainWindow::openDownloadedUpdate(const QString &path)
{
    m_updateButton->setEnabled(true);
    m_updateButton->setText(QStringLiteral("Installer starten"));
    setActivity(QStringLiteral("Update wurde gepr\u00fcft und ist bereit."));

#if defined(Q_OS_WIN)
    const auto answer = QMessageBox::question(
        this, QStringLiteral("Update bereit"),
        QStringLiteral("Der Installer wurde vollst\u00e4ndig geladen und per SHA-256 gepr\u00fcft. "
                       "Jetzt starten und DannyDee Track Manager schlie\u00dfen?"));
    if (answer != QMessageBox::Yes) return;
    if (!QProcess::startDetached(path, {})) {
        QMessageBox::warning(this, QStringLiteral("Update"),
                             QStringLiteral("Der Installer konnte nicht gestartet werden:\n%1").arg(path));
        return;
    }
    QCoreApplication::quit();
#elif defined(Q_OS_MACOS)
    if (!QDesktopServices::openUrl(QUrl::fromLocalFile(path))) {
        QMessageBox::warning(this, QStringLiteral("Update"),
                             QStringLiteral("Das geladene DMG konnte nicht ge\u00f6ffnet werden:\n%1").arg(path));
    }
#else
    QDesktopServices::openUrl(QUrl::fromLocalFile(path));
#endif
}
