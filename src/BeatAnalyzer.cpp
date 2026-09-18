#include "BeatAnalyzer.h"

#include "ToolLocator.h"

#include <QDir>
#include <QFileInfo>
#include <QProcessEnvironment>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <numeric>
#include <vector>

BeatAnalyzer::BeatAnalyzer(QObject *parent) : QObject(parent)
{
    qRegisterMetaType<BeatAnalysisResult>();
    m_process.setProcessChannelMode(QProcess::SeparateChannels);
    m_timeout.setSingleShot(true);
    m_timeout.setInterval(120000);

    connect(&m_process, &QProcess::readyReadStandardOutput, this, [this] {
        m_pcm += m_process.readAllStandardOutput();
    });
    connect(&m_process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
            this, &BeatAnalyzer::finish);
    connect(&m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (m_active && error == QProcess::FailedToStart)
            failCurrent(QStringLiteral("FFmpeg konnte für die Beat-Analyse nicht gestartet werden."));
    });
    connect(&m_timeout, &QTimer::timeout, this, [this] {
        if (!m_active) return;
        m_process.kill();
        failCurrent(QStringLiteral("Zeitüberschreitung bei der Beat-Analyse."));
    });
}

void BeatAnalyzer::enqueue(int trackId, const QString &filePath)
{
    if (trackId < 0 || filePath.isEmpty() || m_pendingTrackIds.contains(trackId)) return;
    m_pendingTrackIds.insert(trackId);
    m_queue.enqueue({trackId, filePath});
    startNext();
}

bool BeatAnalyzer::isBusy() const
{
    return m_active || !m_queue.isEmpty();
}

void BeatAnalyzer::startNext()
{
    if (m_active || m_queue.isEmpty()) return;
    m_current = m_queue.dequeue();
    const QString ffmpeg = ToolLocator::find(QStringLiteral("ffmpeg"));
    if (ffmpeg.isEmpty()) {
        m_active = true;
        failCurrent(QStringLiteral("FFmpeg wurde nicht gefunden."));
        return;
    }

    m_pcm.clear();
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("PATH"), QFileInfo(ffmpeg).absolutePath()
                       + QDir::listSeparator() + environment.value(QStringLiteral("PATH")));
    m_process.setProcessEnvironment(environment);
    m_active = true;
    emit analysisStarted(m_current.trackId, m_current.filePath);
    m_timeout.start();
    m_process.start(ffmpeg, {
        QStringLiteral("-v"), QStringLiteral("error"),
        QStringLiteral("-i"), m_current.filePath,
        QStringLiteral("-map"), QStringLiteral("0:a:0"),
        QStringLiteral("-vn"), QStringLiteral("-sn"), QStringLiteral("-dn"),
        QStringLiteral("-t"), QStringLiteral("900"),
        QStringLiteral("-ac"), QStringLiteral("1"),
        QStringLiteral("-ar"), QStringLiteral("11025"),
        QStringLiteral("-c:a"), QStringLiteral("pcm_f32le"),
        QStringLiteral("-f"), QStringLiteral("f32le"),
        QStringLiteral("pipe:1")
    });
}

void BeatAnalyzer::finish(int exitCode, QProcess::ExitStatus exitStatus)
{
    if (!m_active) return;
    m_timeout.stop();
    m_pcm += m_process.readAllStandardOutput();
    const QString diagnostics = QString::fromUtf8(m_process.readAllStandardError()).trimmed();
    if (exitStatus != QProcess::NormalExit || exitCode != 0) {
        failCurrent(diagnostics.isEmpty()
                        ? QStringLiteral("FFmpeg-Analyse fehlgeschlagen (Code %1).").arg(exitCode)
                        : diagnostics.right(1000));
        return;
    }

    BeatAnalysisResult result = analyzePcm(m_pcm, 11025, m_current.trackId, m_current.filePath);
    const int completedTrackId = m_current.trackId;
    m_pendingTrackIds.remove(completedTrackId);
    m_active = false;
    m_pcm.clear();
    if (result.isValid()) {
        emit analyzed(result);
    } else {
        emit failed(result.trackId, result.filePath,
                    QStringLiteral("Kein ausreichend stabiles Tempo erkannt."));
    }
    QTimer::singleShot(0, this, &BeatAnalyzer::startNext);
}

void BeatAnalyzer::failCurrent(const QString &message)
{
    if (!m_active) return;
    m_timeout.stop();
    const Request failed = m_current;
    m_pendingTrackIds.remove(failed.trackId);
    m_active = false;
    m_pcm.clear();
    emit this->failed(failed.trackId, failed.filePath, message);
    QTimer::singleShot(0, this, &BeatAnalyzer::startNext);
}

BeatAnalysisResult BeatAnalyzer::analyzePcm(const QByteArray &pcm, int sampleRate,
                                            int trackId, const QString &filePath)
{
    BeatAnalysisResult result;
    result.trackId = trackId;
    result.filePath = filePath;
    if (sampleRate <= 0 || pcm.size() < static_cast<int>(sizeof(float) * sampleRate * 4))
        return result;

    const qsizetype sampleCount = pcm.size() / static_cast<qsizetype>(sizeof(float));
    std::vector<float> samples(static_cast<size_t>(sampleCount));
    std::memcpy(samples.data(), pcm.constData(), static_cast<size_t>(sampleCount) * sizeof(float));
    result.durationMs = qRound64(static_cast<double>(sampleCount) * 1000.0 / sampleRate);

    const int hop = std::max(1, sampleRate / 100);
    const int frame = std::max(hop * 2, 256);
    if (sampleCount <= frame) return result;
    const int frameCount = static_cast<int>((sampleCount - frame) / hop) + 1;
    std::vector<double> energy(static_cast<size_t>(frameCount), 0.0);
    std::vector<double> onset(static_cast<size_t>(frameCount), 0.0);

    for (int index = 0; index < frameCount; ++index) {
        const qsizetype start = static_cast<qsizetype>(index) * hop;
        double sum = 0.0;
        for (int offset = 0; offset < frame; ++offset) {
            const double sample = samples[static_cast<size_t>(start + offset)];
            sum += sample * sample;
        }
        energy[static_cast<size_t>(index)] = std::sqrt(sum / frame);
    }

    double onsetPower = 0.0;
    for (int index = 1; index < frameCount; ++index) {
        const double localRise = energy[static_cast<size_t>(index)]
                                 - energy[static_cast<size_t>(index - 1)];
        onset[static_cast<size_t>(index)] = std::max(0.0, localRise);
        onsetPower += onset[static_cast<size_t>(index)] * onset[static_cast<size_t>(index)];
    }
    if (onsetPower < 1e-10) return result;

    const double frameRate = static_cast<double>(sampleRate) / hop;
    const int minLag = std::max(2, static_cast<int>(std::floor(frameRate * 60.0 / 180.0)));
    const int maxLag = std::min(frameCount / 3,
                                static_cast<int>(std::ceil(frameRate * 60.0 / 70.0)));
    if (maxLag <= minLag + 2) return result;

    std::vector<double> correlations(static_cast<size_t>(maxLag + 2), 0.0);
    for (int lag = minLag; lag <= maxLag; ++lag) {
        double numerator = 0.0;
        double leftPower = 0.0;
        double rightPower = 0.0;
        for (int index = lag; index < frameCount; ++index) {
            const double left = onset[static_cast<size_t>(index)];
            const double right = onset[static_cast<size_t>(index - lag)];
            numerator += left * right;
            leftPower += left * left;
            rightPower += right * right;
        }
        if (leftPower > 0.0 && rightPower > 0.0)
            correlations[static_cast<size_t>(lag)] = numerator / std::sqrt(leftPower * rightPower);
    }

    int bestLag = minLag;
    for (int lag = minLag + 1; lag <= maxLag; ++lag) {
        if (correlations[static_cast<size_t>(lag)]
            > correlations[static_cast<size_t>(bestLag)]) bestLag = lag;
    }

    const double rawBpm = frameRate * 60.0 / bestLag;
    const int halfTempoLag = bestLag * 2;
    if (rawBpm > 145.0 && halfTempoLag <= maxLag
        && correlations[static_cast<size_t>(halfTempoLag)]
               >= correlations[static_cast<size_t>(bestLag)] * 0.82) {
        bestLag = halfTempoLag;
    }

    double refinedLag = bestLag;
    if (bestLag > minLag && bestLag < maxLag) {
        const double left = correlations[static_cast<size_t>(bestLag - 1)];
        const double center = correlations[static_cast<size_t>(bestLag)];
        const double right = correlations[static_cast<size_t>(bestLag + 1)];
        const double denominator = left - 2.0 * center + right;
        if (std::abs(denominator) > 1e-9)
            refinedLag += std::clamp(0.5 * (left - right) / denominator, -0.5, 0.5);
    }

    const double bpm = frameRate * 60.0 / refinedLag;
    if (!std::isfinite(bpm) || bpm < 50.0 || bpm > 200.0) return result;

    int bestPhase = 0;
    double bestPhaseScore = -1.0;
    for (int phase = 0; phase < bestLag; ++phase) {
        double score = 0.0;
        int beat = 0;
        for (int index = phase; index < frameCount; index += bestLag, ++beat) {
            const double barWeight = beat % 4 == 0 ? 1.1 : 1.0;
            score += onset[static_cast<size_t>(index)] * barWeight;
        }
        if (score > bestPhaseScore) {
            bestPhaseScore = score;
            bestPhase = phase;
        }
    }

    std::vector<double> candidateScores;
    candidateScores.reserve(static_cast<size_t>(maxLag - minLag + 1));
    for (int lag = minLag; lag <= maxLag; ++lag)
        candidateScores.push_back(correlations[static_cast<size_t>(lag)]);
    std::nth_element(candidateScores.begin(),
                     candidateScores.begin() + candidateScores.size() / 2,
                     candidateScores.end());
    const double median = candidateScores[candidateScores.size() / 2];
    const double peak = correlations[static_cast<size_t>(bestLag)];
    const double prominence = std::max(0.0, peak - median);
    if (peak < 0.04 || prominence < 0.005) return result;

    result.bpm = std::round(bpm * 100.0) / 100.0;
    result.firstBeatMs = std::fmod(static_cast<double>(bestPhase * hop + frame / 2) * 1000.0 / sampleRate,
                                  60000.0 / result.bpm);
    result.confidence = std::clamp(0.65 * peak + 1.8 * prominence, 0.01, 1.0);
    return result;
}
