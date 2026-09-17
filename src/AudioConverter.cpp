#include "AudioConverter.h"
#include "ToolLocator.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>

AudioConverter::AudioConverter(QObject *parent) : QObject(parent)
{
    connect(&m_process, &QProcess::finished, this,
            [this](int exitCode, QProcess::ExitStatus status) {
        const bool ok = status == QProcess::NormalExit && exitCode == 0;
        const QString details = ok ? "Konvertierung abgeschlossen."
            : QString::fromUtf8(m_process.readAllStandardError()).right(1500);
        emit finished(ok, details);
    });
}

void AudioConverter::convert(const QString &source, const QString &target, const QString &format)
{
    if (m_process.state() != QProcess::NotRunning) {
        emit finished(false, "Es läuft bereits eine Konvertierung.");
        return;
    }
    const QString ffmpeg = ToolLocator::find("ffmpeg");
    if (ffmpeg.isEmpty()) {
        emit finished(false, "FFmpeg wurde nicht gefunden. Bitte installieren und zum PATH hinzufügen.");
        return;
    }
    QStringList args{"-hide_banner", "-y", "-i", source, "-map_metadata", "0"};
    if (format == "flac") args << "-c:a" << "flac" << "-compression_level" << "8";
    else args << "-c:a" << "pcm_s24le";
    args << target;
    m_process.start(ffmpeg, args);
}
