#include "DownloadManager.h"

#include <QCoreApplication>
#include <QDebug>
#include <QTemporaryDir>

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QTemporaryDir output;
    if (!output.isValid()) return 2;

    DownloadManager manager;
    int result = 3;
    QObject::connect(&manager, &DownloadManager::mediaDownloaded, &app,
                     [&](const QStringList &) {
        result = 4;
        app.quit();
    });
    QObject::connect(&manager, &DownloadManager::failed, &app,
                     [&](const QString &message, const QString &) {
        result = message.contains("strikten Quellenmodus")
                     && message.contains("keine YouTube-") ? 0 : 5;
        app.quit();
    });

    manager.downloadMedia(QUrl("https://open.spotify.com/intl-de/track/test?si=abc"), output.path(), "flac");
    if (result == 3) app.exec();
    return result;
}
