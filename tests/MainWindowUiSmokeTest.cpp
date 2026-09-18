#include "MainWindow.h"

#include <QApplication>
#include <QAbstractItemView>
#include <QFrame>
#include <QLabel>
#include <QListWidget>
#include <QPixmap>
#include <QPushButton>
#include <QStandardPaths>
#include <QTableView>
#include <QThread>
#include <QDebug>

int main(int argc, char **argv)
{
    QApplication application(argc, argv);
    QStandardPaths::setTestModeEnabled(true);
    QCoreApplication::setOrganizationName(QStringLiteral("DannyDeeTests"));
    QCoreApplication::setApplicationName(QStringLiteral("MainWindowUiSmoke"));

    MainWindow window;
    window.resize(1280, 820);
    window.show();
    for (int iteration = 0; iteration < 5; ++iteration) {
        application.processEvents();
        QThread::msleep(20);
    }

    QListWidget *playlists = window.findChild<QListWidget *>(QStringLiteral("playlistList"));
    QLabel *activePlaylist = window.findChild<QLabel *>(QStringLiteral("activePlaylistTitle"));
    QFrame *analysisCard = window.findChild<QFrame *>(QStringLiteral("analysisCard"));
    QPushButton *analyzeSelected = window.findChild<QPushButton *>(
        QStringLiteral("analyzeSelectedButton"));
    QPushButton *analyzeAll = window.findChild<QPushButton *>(QStringLiteral("analyzeAllButton"));
    QTableView *trackTable = window.findChild<QTableView *>(QStringLiteral("trackTable"));
    if (!playlists || playlists->count() < 1 || !playlists->currentItem()) {
        qCritical() << "Persistent playlist navigation was not initialized";
        return 1;
    }
    if (!activePlaylist || activePlaylist->text() != QStringLiteral("Alle Tracks")) {
        qCritical() << "Active playlist context is not visible";
        return 2;
    }
    if (!analysisCard || !analyzeSelected || !analyzeAll
        || !window.findChildren<QFrame *>(QStringLiteral("deckCard")).isEmpty()) {
        qCritical() << "Track analysis panel did not replace the performance decks";
        return 3;
    }
    if (!trackTable || trackTable->selectionMode() != QAbstractItemView::ExtendedSelection
        || trackTable->contextMenuPolicy() != Qt::CustomContextMenu) {
        qCritical() << "Track table does not expose multi-selection and its context menu";
        return 4;
    }
    const QPixmap snapshot = window.grab();
    if (snapshot.isNull() || snapshot.width() < 900 || snapshot.height() < 620) {
        qCritical() << "Main window could not be rendered at the supported desktop size";
        return 5;
    }
    snapshot.save(QCoreApplication::applicationDirPath() + QStringLiteral("/dannydee-ui-smoke.png"));
    return 0;
}
