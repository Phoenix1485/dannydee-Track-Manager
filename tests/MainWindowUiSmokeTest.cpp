#include "MainWindow.h"

#include <QApplication>
#include <QFrame>
#include <QLabel>
#include <QListWidget>
#include <QPixmap>
#include <QPushButton>
#include <QStandardPaths>
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
    const auto deckCards = window.findChildren<QFrame *>(QStringLiteral("deckCard"));
    const auto syncButtons = window.findChildren<QPushButton *>(QStringLiteral("deckSyncButton"));
    if (!playlists || playlists->count() < 1 || !playlists->currentItem()) {
        qCritical() << "Persistent playlist navigation was not initialized";
        return 1;
    }
    if (!activePlaylist || activePlaylist->text() != QStringLiteral("Alle Tracks")) {
        qCritical() << "Active playlist context is not visible";
        return 2;
    }
    if (deckCards.size() != 2 || syncButtons.size() != 2) {
        qCritical() << "Expected two complete performance decks" << deckCards.size()
                    << syncButtons.size();
        return 3;
    }
    const QPixmap snapshot = window.grab();
    if (snapshot.isNull() || snapshot.width() < 900 || snapshot.height() < 620) {
        qCritical() << "Main window could not be rendered at the supported desktop size";
        return 4;
    }
    snapshot.save(QCoreApplication::applicationDirPath() + QStringLiteral("/dannydee-ui-smoke.png"));
    return 0;
}
