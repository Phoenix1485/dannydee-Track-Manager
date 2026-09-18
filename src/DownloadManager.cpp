#include "DownloadManager.h"
#include "ToolLocator.h"

#include "AppConfig.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QMimeDatabase>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>

namespace {
QNetworkRequest requestFor(const QUrl &url)
{
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("DannyDeeTrackManager/%1").arg(DANNYDEE_APP_VERSION));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    return request;
}
}

DownloadManager::DownloadManager(QObject *parent)
    : QObject(parent), m_network(new QNetworkAccessManager(this))
{
    qRegisterMetaType<OfficialTrackInfo>();
    m_mediaProcess.setProcessChannelMode(QProcess::MergedChannels);
    connect(&m_mediaProcess, &QProcess::readyReadStandardOutput,
            this, &DownloadManager::consumeMediaOutput);
    connect(&m_mediaProcess, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart) return;
        m_mediaStartFailed = true;
        const QString tool = m_mediaUsesSpotDl ? "spotDL" : "yt-dlp";
        emit failed(tool + " konnte nicht gestartet werden: " + m_mediaProcess.errorString(),
                    m_mediaSourceUrl.toString());
    });
    connect(&m_mediaProcess, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [this](int exitCode, QProcess::ExitStatus exitStatus) {
        consumeMediaOutput();
        if (!m_mediaOutputBuffer.isEmpty()) {
            handleMediaLine(QString::fromUtf8(m_mediaOutputBuffer));
            m_mediaOutputBuffer.clear();
        }
        if (m_mediaStartFailed) {
            m_mediaStartFailed = false;
            return;
        }
        QStringList outputPaths;
        if (m_mediaUsesSpotDl) {
            const QFileInfoList files = QDir(m_mediaTargetDirectory).entryInfoList(
                {"*." + m_mediaFormat}, QDir::Files, QDir::Time);
            for (const QFileInfo &file : files) {
                const QString path = file.absoluteFilePath();
                const qint64 previous = m_mediaFilesBefore.value(path, -1);
                if (previous < 0 || file.lastModified().toMSecsSinceEpoch() != previous)
                    outputPaths << path;
            }
        } else {
            for (const QString &path : m_mediaOutputPaths) {
                if (QFileInfo::exists(path)) outputPaths << path;
            }
        }
        outputPaths << m_mediaPartialPaths;
        outputPaths.removeDuplicates();
        const bool ok = exitStatus == QProcess::NormalExit && exitCode == 0 && !outputPaths.isEmpty();
        if (ok) {
            emit mediaProgress(100, "Download und Konvertierung abgeschlossen.");
            emit mediaDownloaded(outputPaths);
        } else {
            if (m_mediaUsesSpotDl && !m_spotifyFallbackActive) {
                const QStringList fallbackUrls = spotDlFallbackUrls();
                if (!fallbackUrls.isEmpty()) {
                    m_mediaPartialPaths = outputPaths;
                    m_spotifyFallbackActive = true;
                    emit mediaProgress(0, "spotDL-Treffer wird direkt über yt-dlp erneut versucht …");
                    startYtDlpDownload(fallbackUrls, fallbackUrls.size() > 1);
                    return;
                }
            }
            QString details = m_mediaLog.trimmed().right(2500);
            const QString tool = m_mediaUsesSpotDl ? "spotDL" : "yt-dlp";
            if (details.isEmpty())
                details = QString("%1 wurde mit Code %2 beendet.").arg(tool).arg(exitCode);
            emit failed("Medien-Download fehlgeschlagen:\n" + details, m_mediaSourceUrl.toString());
        }
    });
}

void DownloadManager::downloadDirect(const QUrl &url, const QString &targetDirectory)
{
    OfficialTrackInfo track;
    track.title = QFileInfo(url.path()).completeBaseName();
    track.artist = "Direkter Download";
    track.downloadable = true;
    track.downloadUrl = url.toString();
    startFileRequest(url, targetDirectory, track);
}

QString DownloadManager::findTool(const QString &baseName)
{
    return ToolLocator::find(baseName);
}

QUrl DownloadManager::canonicalSpotifyUrl(const QUrl &url)
{
    if (!url.host().contains("spotify.com", Qt::CaseInsensitive)) return url;
    const QStringList segments = url.path().split('/', Qt::SkipEmptyParts);
    static const QSet<QString> entityTypes{
        "track", "album", "playlist", "artist", "episode", "show"
    };
    for (int index = 0; index + 1 < segments.size(); ++index) {
        const QString type = segments.at(index).toLower();
        if (!entityTypes.contains(type)) continue;
        QUrl canonical("https://open.spotify.com");
        canonical.setPath('/' + type + '/' + segments.at(index + 1));
        return canonical;
    }
    return url;
}

bool DownloadManager::mediaDownloaderAvailable(const QUrl &url) const
{
    const bool spotify = url.host().contains("spotify.com", Qt::CaseInsensitive)
                         || url.host().compare("spotify.link", Qt::CaseInsensitive) == 0;
    const QString downloader = spotify ? findTool("spotdl") : findTool("yt-dlp");
    return !downloader.isEmpty() && !findTool("ffmpeg").isEmpty();
}

bool DownloadManager::isBusy() const
{
    return m_fileReply || m_mediaProcess.state() != QProcess::NotRunning;
}

void DownloadManager::downloadMedia(const QUrl &url, const QString &targetDirectory,
                                    const QString &audioFormat)
{
    if (isBusy()) {
        emit failed("Es läuft bereits ein Download.", {});
        return;
    }
    if (!url.isValid() || (url.scheme() != "http" && url.scheme() != "https")) {
        emit failed("Der Medien-Link ist ungültig. Es werden nur HTTP- und HTTPS-Links akzeptiert.", {});
        return;
    }
    const bool spotify = url.host().contains("spotify.com", Qt::CaseInsensitive)
                         || url.host().compare("spotify.link", Qt::CaseInsensitive) == 0;
    const QString downloader = findTool(spotify ? "spotdl" : "yt-dlp");
    const QString ffmpeg = findTool("ffmpeg");
    if (downloader.isEmpty() || ffmpeg.isEmpty()) {
        const QString missingTool = spotify ? "spotDL" : "yt-dlp";
        emit failed(QString("Für diesen Link werden %1 und FFmpeg benötigt. Ein API-Schlüssel ist nicht erforderlich.")
                        .arg(missingTool),
                    spotify ? "https://github.com/spotDL/spotify-downloader/releases/latest"
                            : "https://github.com/yt-dlp/yt-dlp/releases/latest");
        return;
    }
    const QString normalizedFormat = audioFormat.toLower();
    if (normalizedFormat != "mp3" && normalizedFormat != "flac") {
        emit failed("Nicht unterstütztes Ausgabeformat: " + audioFormat, {});
        return;
    }
    QDir directory(targetDirectory);
    if (!directory.exists() && !directory.mkpath(".")) {
        emit failed("Der Zielordner kann nicht erstellt werden.", {});
        return;
    }

    m_mediaOutputBuffer.clear();
    m_mediaLog.clear();
    m_mediaOutputPaths.clear();
    m_mediaPartialPaths.clear();
    m_mediaFilesBefore.clear();
    m_mediaTargetDirectory = directory.absolutePath();
    m_mediaFormat = normalizedFormat;
    m_mediaSourceUrl = url;
    m_mediaUsesSpotDl = spotify;
    m_spotifyFallbackActive = false;
    m_mediaStartFailed = false;

    if (!spotify) {
        startYtDlpDownload({url.toString(QUrl::FullyEncoded)}, false);
        return;
    }

    const QFileInfoList existingFiles = directory.entryInfoList(
        {"*." + normalizedFormat}, QDir::Files);
    for (const QFileInfo &file : existingFiles)
        m_mediaFilesBefore.insert(file.absoluteFilePath(), file.lastModified().toMSecsSinceEpoch());

    QStringList args{
        "download", canonicalSpotifyUrl(url).toString(QUrl::FullyEncoded),
        "--format", normalizedFormat,
        "--output", directory.filePath("{artists} - {title} [{track-id}].{output-ext}"),
        "--ffmpeg", ffmpeg,
        "--threads", "1",
        "--overwrite", "force",
        "--headless",
        "--simple-tui",
        "--log-level", "INFO",
        "--print-errors",
        "--audio", "youtube-music", "youtube", "soundcloud", "bandcamp"
    };
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    QStringList toolDirectories{QFileInfo(downloader).absolutePath(), QFileInfo(ffmpeg).absolutePath()};
    toolDirectories.removeDuplicates();
    environment.insert("PATH", toolDirectories.join(QDir::listSeparator())
                                   + QDir::listSeparator() + environment.value("PATH"));
    environment.insert("PYTHONIOENCODING", "utf-8");
    m_mediaProcess.setProcessEnvironment(environment);
    m_mediaProcess.setWorkingDirectory(directory.absolutePath());
    emit mediaProgress(0, "Spotify-Metadaten werden aufgelöst und eine Audioquelle wird gesucht …");
    m_mediaProcess.start(downloader, args);
}

void DownloadManager::startYtDlpDownload(const QStringList &urls, bool continueOnError)
{
    const QString downloader = findTool("yt-dlp");
    const QString ffmpeg = findTool("ffmpeg");
    if (downloader.isEmpty() || ffmpeg.isEmpty()) {
        emit failed("Für den Wiederholungsversuch werden yt-dlp und FFmpeg benötigt.",
                    "https://github.com/yt-dlp/yt-dlp/releases/latest");
        return;
    }

    m_mediaUsesSpotDl = false;
    m_mediaOutputBuffer.clear();
    m_mediaLog.clear();
    m_mediaOutputPaths.clear();

    QStringList args{
        "--ignore-config", "--no-playlist",
        continueOnError ? "--ignore-errors" : "--abort-on-error",
        "--newline", "--progress", "--color", "never",
        "--progress-template", "download:__DANNYDEE_PROGRESS__%(progress._percent_str)s",
        "--print", "after_move:__DANNYDEE_FILE__%(filepath)s",
        "--no-overwrites", "--windows-filenames", "--extract-audio",
        "--audio-format", m_mediaFormat, "--embed-metadata", "--embed-thumbnail",
        "--no-embed-info-json", "--ffmpeg-location", QFileInfo(ffmpeg).absolutePath(),
        "--paths", m_mediaTargetDirectory,
        "--output", "%(uploader)s - %(title)s [%(id)s].%(ext)s"
    };
    if (m_mediaFormat == "mp3") args << "--audio-quality" << "0";
    const QString extraArguments = qEnvironmentVariable("DANNYDEE_YTDLP_EXTRA_ARGS");
    if (!extraArguments.isEmpty()) args << QProcess::splitCommand(extraArguments);
    args << "--";
    args << urls;

    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    QStringList toolDirectories{QFileInfo(downloader).absolutePath(), QFileInfo(ffmpeg).absolutePath()};
    toolDirectories.removeDuplicates();
    environment.insert("PATH", toolDirectories.join(QDir::listSeparator())
                                   + QDir::listSeparator() + environment.value("PATH"));
    environment.insert("PYTHONIOENCODING", "utf-8");
    m_mediaProcess.setProcessEnvironment(environment);
    m_mediaProcess.setWorkingDirectory(m_mediaTargetDirectory);
    if (!m_spotifyFallbackActive) emit mediaProgress(0, "Link wird analysiert …");
    m_mediaProcess.start(downloader, args);
}

QStringList DownloadManager::spotDlFallbackUrls() const
{
    static const QRegularExpression providerUrl(R"(https?://[^\s]+)",
                                                 QRegularExpression::CaseInsensitiveOption);
    QSet<QString> unique;
    auto matches = providerUrl.globalMatch(m_mediaLog);
    while (matches.hasNext()) {
        QString value = matches.next().captured(0);
        while (value.endsWith('.') || value.endsWith(',') || value.endsWith(';')
               || value.endsWith(')')) {
            value.chop(1);
        }
        const QUrl candidate(value);
        const QString host = candidate.host().toLower();
        const bool supported = host == "youtu.be" || host.endsWith("youtube.com")
                               || host.endsWith("soundcloud.com")
                               || host.endsWith("bandcamp.com");
        if (candidate.isValid() && supported)
            unique.insert(candidate.toString(QUrl::FullyEncoded));
    }
    return unique.values();
}

void DownloadManager::consumeMediaOutput()
{
    m_mediaOutputBuffer += m_mediaProcess.readAllStandardOutput();
    qsizetype newline = -1;
    while ((newline = m_mediaOutputBuffer.indexOf('\n')) >= 0) {
        QByteArray line = m_mediaOutputBuffer.left(newline);
        m_mediaOutputBuffer.remove(0, newline + 1);
        if (line.endsWith('\r')) line.chop(1);
        handleMediaLine(QString::fromUtf8(line));
    }
}

void DownloadManager::handleMediaLine(const QString &rawLine)
{
    QString line = rawLine;
    line.remove(QRegularExpression("\\x1b\\[[0-9;]*m"));
    if (line.startsWith("__DANNYDEE_FILE__")) {
        m_mediaOutputPaths << line.mid(QString("__DANNYDEE_FILE__").size()).trimmed();
        return;
    }
    static const QRegularExpression progressExpression(
        "__DANNYDEE_PROGRESS__\\s*([0-9]+(?:\\.[0-9]+)?)%");
    const auto progressMatch = progressExpression.match(line);
    if (progressMatch.hasMatch()) {
        const int percent = qBound(0, qRound(progressMatch.captured(1).toDouble()), 100);
        emit mediaProgress(percent, "Medien-Download");
        return;
    }
    if (!line.trimmed().isEmpty()) {
        m_mediaLog += line.trimmed() + '\n';
        if (m_mediaLog.size() > 12000) m_mediaLog = m_mediaLog.right(12000);
        if (m_mediaUsesSpotDl || line.startsWith("[ExtractAudio]") || line.startsWith("[Metadata]")
            || line.startsWith("[EmbedThumbnail]") || line.startsWith("[Merger]")) {
            emit mediaProgress(-1, line.trimmed());
        }
    }
}

void DownloadManager::startFileRequest(const QUrl &url, const QString &targetDirectory,
                                       const OfficialTrackInfo &track)
{
    if (isBusy()) {
        emit failed("Es läuft bereits ein Download.", {});
        return;
    }
    m_targetDirectory = targetDirectory;
    m_currentTrack = track;
    m_output.reset();
    m_outputPath.clear();
    m_fileReply = m_network->get(requestFor(url));
    connect(m_fileReply, &QNetworkReply::downloadProgress, this, &DownloadManager::progress);
    connect(m_fileReply, &QIODevice::readyRead, this, [this] {
        if (ensureOutputFile(m_fileReply)) m_output->write(m_fileReply->readAll());
    });
    connect(m_fileReply, &QNetworkReply::finished, this, [this] {
        QNetworkReply *reply = m_fileReply;
        if (reply->error() != QNetworkReply::NoError) {
            if (m_output) m_output->cancelWriting();
            const QString error = reply->errorString();
            reply->deleteLater();
            m_fileReply = nullptr;
            m_output.reset();
            emit failed("Download fehlgeschlagen: " + error, m_currentTrack.purchaseUrl);
            return;
        }
        if (ensureOutputFile(reply)) m_output->write(reply->readAll());
        const bool committed = m_output && m_output->commit();
        const QString path = m_outputPath;
        reply->deleteLater();
        m_fileReply = nullptr;
        m_output.reset();
        if (!committed) emit failed("Die heruntergeladene Datei konnte nicht gespeichert werden.", {});
        else emit downloaded(path, m_currentTrack);
    });
}

bool DownloadManager::ensureOutputFile(QNetworkReply *reply)
{
    if (m_output) return true;
    QDir directory(m_targetDirectory);
    if (!directory.exists() && !directory.mkpath(".")) return false;
    QString name = chooseOutputName(reply);
    QString path = directory.filePath(name);
    const QFileInfo info(path);
    int suffix = 2;
    while (QFileInfo::exists(path)) {
        path = directory.filePath(QString("%1 (%2).%3").arg(info.completeBaseName()).arg(suffix++).arg(info.suffix()));
    }
    m_output = std::make_unique<QSaveFile>(path);
    if (!m_output->open(QIODevice::WriteOnly)) {
        m_output.reset();
        return false;
    }
    m_outputPath = path;
    return true;
}

QString DownloadManager::chooseOutputName(QNetworkReply *reply) const
{
    QString name;
    const QString disposition = QString::fromUtf8(reply->rawHeader("Content-Disposition"));
    const QRegularExpression expression("filename\\*?=(?:UTF-8''|\\\")?([^\\\";]+)",
                                        QRegularExpression::CaseInsensitiveOption);
    const auto match = expression.match(disposition);
    if (match.hasMatch()) name = QUrl::fromPercentEncoding(match.captured(1).trimmed().toUtf8());
    if (name.isEmpty()) name = QFileInfo(reply->url().path()).fileName();
    QString extension = QFileInfo(name).suffix();
    if (extension.isEmpty() || extension.size() > 5) {
        const QString mime = reply->header(QNetworkRequest::ContentTypeHeader).toString().section(';', 0, 0);
        const QString preferred = QMimeDatabase().mimeTypeForName(mime).preferredSuffix();
        extension = preferred.isEmpty() ? "audio" : preferred;
        name.clear();
    }
    if (name.isEmpty()) {
        const QString base = safeFileName(QString("%1 - %2").arg(m_currentTrack.artist, m_currentTrack.title));
        name = (base.isEmpty() ? "download" : base) + "." + extension;
    }
    return safeFileName(name);
}

QString DownloadManager::safeFileName(QString value)
{
    value.replace(QRegularExpression(R"([<>:"/\\|?*])"), "_");
    value = value.trimmed();
    while (value.endsWith('.') || value.endsWith(' ')) value.chop(1);
    return value.left(180);
}
