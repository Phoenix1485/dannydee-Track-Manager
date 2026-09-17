#include "DownloadManager.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QDebug>
#include <QTemporaryDir>
#include <QTimer>

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    qputenv("DANNYDEE_YTDLP_EXTRA_ARGS",
            "--download-sections *00:00:00-00:00:02 --force-keyframes-at-cuts");
    QTemporaryDir output;
    if (!output.isValid()) return 2;

    DownloadManager manager;
    int result = 3;
    QObject::connect(&manager, &DownloadManager::mediaDownloaded, &app,
                     [&](const QStringList &paths) {
        result = !paths.isEmpty() && QFileInfo::exists(paths.first())
                 && QFileInfo(paths.first()).suffix().compare("flac", Qt::CaseInsensitive) == 0
                     ? 0 : 4;
        app.quit();
    });
    QObject::connect(&manager, &DownloadManager::failed, &app,
                     [&](const QString &message, const QString &) {
        qCritical().noquote() << message;
        result = 5;
        app.quit();
    });
    QTimer::singleShot(115000, &app, [&] {
        result = 6;
        app.quit();
    });

    manager.downloadMedia(QUrl("https://open.spotify.com/intl-de/track/test?si=abc"), output.path(), "flac");
    app.exec();
    return result;
}
