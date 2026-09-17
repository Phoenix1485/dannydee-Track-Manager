#include "MainWindow.h"
#include "AudioProbe.h"
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

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent), m_converter(this), m_downloader(this), m_updater(this)
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
    auto *addPlaylistAction = libraryMenu->addAction("Auswahl zur Playlist");
    auto *viewPlaylistAction = libraryMenu->addAction("Playlist anzeigen");
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
    auto *libraryLayout = new QVBoxLayout(libraryCard);
    libraryLayout->setContentsMargins(0, 0, 0, 0);
    libraryLayout->setSpacing(0);
    auto *libraryHeader = new QHBoxLayout;
    libraryHeader->setContentsMargins(16, 13, 16, 11);
    auto *libraryTitle = new QLabel("Bibliothek");
    libraryTitle->setObjectName("sectionTitle");
    m_libraryCaption = new QLabel;
    m_libraryCaption->setObjectName("sectionCaption");
    libraryHeader->addWidget(libraryTitle);
    libraryHeader->addStretch();
    libraryHeader->addWidget(m_libraryCaption);
    libraryLayout->addLayout(libraryHeader);

    m_model = new QSqlTableModel(this, m_database.connection());
    m_model->setTable("tracks");
    m_model->setEditStrategy(QSqlTableModel::OnFieldChange);
    m_model->select();
    const QStringList headers{"ID", "Titel", "Artist", "Genre", "BPM", "Key", "Energy", "Label",
                              "Release", "Quelle", "Datei", "Lokal", "Erstellt"};
    for (int i = 0; i < headers.size(); ++i) m_model->setHeaderData(i, Qt::Horizontal, headers[i]);
    m_table = new QTableView;
    m_table->setObjectName("trackTable");
    m_table->setModel(m_model);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setSortingEnabled(true);
    m_table->setAlternatingRowColors(true);
    m_table->setShowGrid(false);
    m_table->setWordWrap(false);
    m_table->setCornerButtonEnabled(false);
    m_table->verticalHeader()->setVisible(false);
    m_table->verticalHeader()->setDefaultSectionSize(42);
    m_table->hideColumn(0);
    for (int column : {9, 10, 11, 12}) m_table->hideColumn(column);
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
    libraryLayout->addWidget(m_table, 1);

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
    makeActionButton(removeAction, "danger");
    libraryLayout->addLayout(actions);
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
    connect(addPlaylistAction, &QAction::triggered, this, &MainWindow::addSelectedToPlaylist);
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
        QFrame#statCard, QFrame#filterCard, QFrame#libraryCard {
            background: #131722; border: 1px solid #242A38; border-radius: 12px;
        }
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
    m_libraryCaption->setText(QString("%1 Track%2").arg(total).arg(total == 1 ? "" : "s"));
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
    if (m_libraryCaption)
        m_libraryCaption->setText(QString("%1 sichtbar").arg(m_model->rowCount()));
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

    QSet<QString> existing;
    QSqlQuery query(m_database.connection());
    query.exec("SELECT source_url FROM tracks WHERE source_url IS NOT NULL AND source_url <> ''");
    while (query.next()) existing.insert(query.value(0).toString());

    const QRegularExpression urlPattern(R"(https?://[^\s<>"']+)",
                                        QRegularExpression::CaseInsensitiveOption);
    auto matches = urlPattern.globalMatch(input);
    int imported = 0;
    int duplicates = 0;
    while (matches.hasNext()) {
        QString text = matches.next().captured(0);
        while (text.endsWith('.') || text.endsWith(',') || text.endsWith(';')) text.chop(1);
        const auto result = LinkResolver::resolve(text);
        const QString normalized = result.url.toString();
        if (!result.url.isValid() || normalized.isEmpty()) continue;
        if (existing.contains(normalized)) {
            ++duplicates;
            continue;
        }
        m_database.addTrack("Unbekannter Track", result.provider, "Other", 0, "", 5, "", "",
                            normalized, "", false);
        existing.insert(normalized);
        ++imported;
    }
    m_model->select();
    updateLibrarySummary();
    setActivity(QString("%1 Link(s) importiert, %2 Duplikat(e) übersprungen.")
                    .arg(imported).arg(duplicates));
    if (imported == 0)
        QMessageBox::information(this, "Links importieren", "Es wurden keine neuen HTTP- oder HTTPS-Links gefunden.");
}

int MainWindow::selectedTrackId() const
{
    const auto rows = m_table->selectionModel()->selectedRows();
    return rows.isEmpty() ? -1 : m_model->data(m_model->index(rows.first().row(), 0)).toInt();
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
    if (!m_downloader.mediaDownloaderAvailable(source)) {
        const bool spotify = source.host().contains("spotify.com", Qt::CaseInsensitive)
                             || source.host().compare("spotify.link", Qt::CaseInsensitive) == 0;
        QMessageBox box(this);
        box.setWindowTitle(spotify ? "spotDL fehlt" : "yt-dlp fehlt");
        box.setText(QString("Für diesen Link werden %1 und FFmpeg benötigt.")
                        .arg(spotify ? "spotDL" : "yt-dlp"));
        auto *open = box.addButton("Download-Seite öffnen", QMessageBox::ActionRole);
        box.addButton(QMessageBox::Ok);
        box.exec();
        if (box.clickedButton() == open)
            QDesktopServices::openUrl(QUrl(spotify
                ? "https://github.com/spotDL/spotify-downloader/releases/latest"
                : "https://github.com/yt-dlp/yt-dlp/releases/latest"));
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
    if (ok && !name.isEmpty() && !m_database.addPlaylist(name))
        QMessageBox::warning(this, "Playlist", m_database.lastError());
}

void MainWindow::addSelectedToPlaylist()
{
    const int trackId = selectedTrackId();
    if (trackId < 0) { QMessageBox::information(this, "Playlist", "Bitte einen Track auswählen."); return; }
    QSqlQuery q(m_database.connection());
    q.exec("SELECT id,name FROM playlists ORDER BY name");
    QStringList names; QList<int> ids;
    while (q.next()) { ids << q.value(0).toInt(); names << q.value(1).toString(); }
    if (names.isEmpty()) { QMessageBox::information(this, "Playlist", "Bitte zuerst eine Playlist erstellen."); return; }
    bool ok = false;
    const QString name = QInputDialog::getItem(this, "Playlist", "Ziel:", names, 0, false, &ok);
    if (ok) m_database.addToPlaylist(ids[names.indexOf(name)], trackId);
}

void MainWindow::showPlaylist()
{
    QSqlQuery q(m_database.connection());
    q.exec("SELECT id,name FROM playlists ORDER BY name");
    QStringList names{"Alle Tracks"};
    QList<int> ids{-1};
    while (q.next()) { ids << q.value(0).toInt(); names << q.value(1).toString(); }
    bool ok = false;
    const QString name = QInputDialog::getItem(this, "Playlist anzeigen", "Playlist:", names, 0, false, &ok);
    if (!ok) return;
    const int index = names.indexOf(name);
    m_playlistFilterId = ids.value(index, -1);
    refreshFilter();
    m_status->setText(m_playlistFilterId < 0 ? "Alle Tracks" : "Playlist: " + name);
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
