#include "UpdateManager.h"

#include "AppConfig.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QVersionNumber>

namespace {
QNetworkRequest secureRequest(const QUrl &url)
{
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("DannyDeeTrackManager/%1").arg(DANNYDEE_APP_VERSION));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setMaximumRedirectsAllowed(4);
    request.setTransferTimeout(30000);
    return request;
}

QString cleanVersion(QString version)
{
    version = version.trimmed();
    if (version.startsWith(QLatin1Char('v'), Qt::CaseInsensitive)) version.removeFirst();
    return version;
}
}

UpdateManager::UpdateManager(QObject *parent)
    : QObject(parent), m_network(new QNetworkAccessManager(this))
{
}

bool UpdateManager::isConfigured() const
{
    const QUrl url(QString::fromUtf8(DANNYDEE_UPDATE_MANIFEST_URL));
    return url.isValid() && url.scheme() == QStringLiteral("https") && !url.host().isEmpty();
}

bool UpdateManager::isBusy() const
{
    return m_checkReply || m_downloadReply;
}

QString UpdateManager::manifestUrl() const
{
    return QString::fromUtf8(DANNYDEE_UPDATE_MANIFEST_URL);
}

bool UpdateManager::isNewerVersion(const QString &candidate, const QString &installed)
{
    const QVersionNumber candidateVersion = QVersionNumber::fromString(cleanVersion(candidate));
    const QVersionNumber installedVersion = QVersionNumber::fromString(cleanVersion(installed));
    if (candidateVersion.isNull() || installedVersion.isNull()) return false;
    return QVersionNumber::compare(candidateVersion, installedVersion) > 0;
}

QString UpdateManager::platformKey()
{
#if defined(Q_OS_WIN) && defined(Q_PROCESSOR_X86_64)
    return QStringLiteral("windows-x64");
#elif defined(Q_OS_MACOS) && defined(Q_PROCESSOR_ARM_64)
    return QStringLiteral("macos-arm64");
#elif defined(Q_OS_MACOS) && defined(Q_PROCESSOR_X86_64)
    return QStringLiteral("macos-x64");
#else
    return {};
#endif
}

void UpdateManager::checkForUpdates()
{
    if (m_checkReply || m_downloadReply) return;
    m_available = {};

    if (!isConfigured()) {
        emit checkFailed(QStringLiteral("F\u00fcr diesen Build ist noch keine sichere Update-Adresse konfiguriert."));
        return;
    }
    if (platformKey().isEmpty()) {
        emit checkFailed(QStringLiteral("F\u00fcr diese Plattform werden noch keine automatischen Updates angeboten."));
        return;
    }

    m_checkReply = m_network->get(secureRequest(QUrl(manifestUrl())));
    connect(m_checkReply, &QNetworkReply::finished, this, [this] {
        QNetworkReply *reply = m_checkReply;
        m_checkReply = nullptr;
        finishManifestRequest(reply);
        reply->deleteLater();
    });
}

void UpdateManager::finishManifestRequest(QNetworkReply *reply)
{
    if (reply->error() != QNetworkReply::NoError) {
        emit checkFailed(QStringLiteral("Update-Pr\u00fcfung fehlgeschlagen: %1").arg(reply->errorString()));
        return;
    }

    const QByteArray body = reply->readAll();
    if (body.size() > 1024 * 1024) {
        emit checkFailed(QStringLiteral("Die Update-Datei ist unerwartet gro\u00df."));
        return;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(body, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        emit checkFailed(QStringLiteral("Die Update-Datei ist ung\u00fcltig."));
        return;
    }

    const QJsonObject root = document.object();
    const QString version = root.value(QStringLiteral("version")).toString().trimmed();
    if (!isNewerVersion(version, QCoreApplication::applicationVersion())) {
        emit upToDate();
        return;
    }

    const QJsonObject artifact = root.value(QStringLiteral("platforms")).toObject()
                                     .value(platformKey()).toObject();
    const QUrl downloadUrl(artifact.value(QStringLiteral("url")).toString());
    const QByteArray checksum = artifact.value(QStringLiteral("sha256")).toString()
                                    .trimmed().toLatin1().toLower();
    const qint64 expectedSize = artifact.value(QStringLiteral("size")).toVariant().toLongLong();
    const QRegularExpression checksumPattern(QStringLiteral("^[0-9a-f]{64}$"));
    QString fileName = QFileInfo(downloadUrl.path()).fileName();
    fileName.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9._-]")), QStringLiteral("_"));

#if defined(Q_OS_WIN)
    const bool expectedExtension = fileName.endsWith(QStringLiteral(".exe"), Qt::CaseInsensitive);
#elif defined(Q_OS_MACOS)
    const bool expectedExtension = fileName.endsWith(QStringLiteral(".dmg"), Qt::CaseInsensitive);
#else
    const bool expectedExtension = false;
#endif

    if (!downloadUrl.isValid() || downloadUrl.scheme() != QStringLiteral("https") ||
        downloadUrl.host().isEmpty() || !checksumPattern.match(QString::fromLatin1(checksum)).hasMatch() ||
        expectedSize <= 0 || expectedSize > 1024LL * 1024LL * 1024LL ||
        fileName.isEmpty() || !expectedExtension) {
        emit checkFailed(QStringLiteral("Das Update enth\u00e4lt keine g\u00fcltigen Download- und Pr\u00fcfsummenangaben."));
        return;
    }

    m_available.version = version;
    m_available.notes = root.value(QStringLiteral("notes")).toString().trimmed();
    m_available.url = downloadUrl;
    m_available.sha256 = checksum;
    m_available.size = expectedSize;
    m_available.fileName = fileName;
    emit updateAvailable(m_available.version, m_available.notes);
}

void UpdateManager::downloadUpdate()
{
    if (m_checkReply || m_downloadReply || !m_available.url.isValid()) return;

    const QString updateDirectory = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
                                    + QStringLiteral("/updates");
    if (!QDir().mkpath(updateDirectory)) {
        emit downloadFailed(QStringLiteral("Der lokale Update-Ordner konnte nicht erstellt werden."));
        return;
    }

    const QString targetPath = QDir(updateDirectory).filePath(m_available.fileName);
    m_downloadFile = new QSaveFile(targetPath, this);
    if (!m_downloadFile->open(QIODevice::WriteOnly)) {
        const QString error = m_downloadFile->errorString();
        m_downloadFile->deleteLater();
        m_downloadFile = nullptr;
        emit downloadFailed(QStringLiteral("Der Installer konnte nicht gespeichert werden: %1").arg(error));
        return;
    }

    m_downloadHash.reset();
    m_downloadedBytes = 0;
    m_downloadReply = m_network->get(secureRequest(m_available.url));
    connect(m_downloadReply, &QNetworkReply::readyRead, this, [this] {
        const QByteArray data = m_downloadReply->readAll();
        if (data.isEmpty()) return;
        if (m_downloadedBytes + data.size() > m_available.size) {
            m_downloadReply->abort();
            return;
        }
        if (m_downloadFile->write(data) != data.size()) {
            m_downloadReply->abort();
            return;
        }
        m_downloadHash.addData(data);
        m_downloadedBytes += data.size();
    });
    connect(m_downloadReply, &QNetworkReply::downloadProgress,
            this, &UpdateManager::downloadProgress);
    connect(m_downloadReply, &QNetworkReply::finished, this, [this] {
        QNetworkReply *reply = m_downloadReply;
        m_downloadReply = nullptr;
        finishDownload(reply);
        reply->deleteLater();
    });
}

void UpdateManager::finishDownload(QNetworkReply *reply)
{
    if (reply->error() != QNetworkReply::NoError) {
        failDownload(QStringLiteral("Update-Download fehlgeschlagen: %1").arg(reply->errorString()));
        return;
    }
    const QByteArray remaining = reply->readAll();
    if (!remaining.isEmpty()) {
        if (m_downloadedBytes + remaining.size() > m_available.size ||
            m_downloadFile->write(remaining) != remaining.size()) {
            failDownload(QStringLiteral("Der Update-Download konnte nicht vollst\u00e4ndig gespeichert werden."));
            return;
        }
        m_downloadHash.addData(remaining);
        m_downloadedBytes += remaining.size();
    }
    if (m_downloadedBytes != m_available.size) {
        failDownload(QStringLiteral("Die Gr\u00f6\u00dfe des Downloads stimmt nicht mit dem Update-Manifest \u00fcberein."));
        return;
    }
    if (m_downloadHash.result().toHex().toLower() != m_available.sha256) {
        failDownload(QStringLiteral("Die SHA-256-Pr\u00fcfsumme des Downloads stimmt nicht. Das Update wurde verworfen."));
        return;
    }

    const QString path = m_downloadFile->fileName();
    if (!m_downloadFile->commit()) {
        failDownload(QStringLiteral("Der verifizierte Installer konnte nicht fertig gespeichert werden."));
        return;
    }
    m_downloadFile->deleteLater();
    m_downloadFile = nullptr;
    emit downloadReady(path);
}

void UpdateManager::failDownload(const QString &message)
{
    if (m_downloadFile) {
        m_downloadFile->cancelWriting();
        m_downloadFile->deleteLater();
        m_downloadFile = nullptr;
    }
    emit downloadFailed(message);
}
