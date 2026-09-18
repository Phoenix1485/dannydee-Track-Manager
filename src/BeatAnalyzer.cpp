#include "BeatAnalyzer.h"

#include "AudioProbe.h"
#include "ToolLocator.h"

#include <QDir>
#include <QFileInfo>
#include <QProcessEnvironment>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <numeric>
#include <vector>

namespace {
constexpr double pi = 3.14159265358979323846;

int estimateEnergy(const std::vector<float> &samples)
{
    if (samples.empty()) return 0;
    long double squareSum = 0.0;
    double peak = 0.0;
    for (const float value : samples) {
        const double sample = std::clamp(static_cast<double>(value), -1.0, 1.0);
        squareSum += sample * sample;
        peak = std::max(peak, std::abs(sample));
    }
    const double rms = std::sqrt(static_cast<double>(squareSum / samples.size()));
    if (rms < 1e-5 || peak < 1e-4) return 0;
    const double rmsDb = 20.0 * std::log10(rms);
    const double crestDb = 20.0 * std::log10(std::max(peak / rms, 1.0));
    const double score = (rmsDb + 36.0) / 3.0 + std::clamp((10.0 - crestDb) / 8.0, 0.0, 1.0);
    return std::clamp(static_cast<int>(std::lround(score)), 1, 10);
}

QString estimateMusicalKey(const std::vector<float> &samples, int sampleRate)
{
    constexpr int windowSize = 4096;
    constexpr int maxWindows = 48;
    if (sampleRate <= 0 || samples.size() < windowSize) return {};

    std::array<double, 12> chroma{};
    const size_t availableStarts = samples.size() - windowSize;
    const int windowCount = std::min(
        maxWindows, std::max(1, static_cast<int>(samples.size() / (sampleRate * 2))));
    std::vector<double> windowed(windowSize);
    int analyzedWindows = 0;

    for (int windowIndex = 0; windowIndex < windowCount; ++windowIndex) {
        const size_t start = windowCount == 1 ? 0
            : availableStarts * static_cast<size_t>(windowIndex)
                  / static_cast<size_t>(windowCount - 1);
        double squareSum = 0.0;
        for (int index = 0; index < windowSize; ++index) {
            const double hann = 0.5 - 0.5 * std::cos(2.0 * pi * index / (windowSize - 1));
            const double value = samples[start + static_cast<size_t>(index)] * hann;
            windowed[static_cast<size_t>(index)] = value;
            squareSum += value * value;
        }
        if (squareSum / windowSize < 1e-7) continue;
        ++analyzedWindows;

        for (int midi = 36; midi <= 83; ++midi) {
            const double frequency = 440.0 * std::pow(2.0, (midi - 69) / 12.0);
            const double omega = 2.0 * pi * frequency / sampleRate;
            const double coefficient = 2.0 * std::cos(omega);
            double previous = 0.0;
            double previous2 = 0.0;
            for (const double sample : windowed) {
                const double current = sample + coefficient * previous - previous2;
                previous2 = previous;
                previous = current;
            }
            const double power = std::max(0.0, previous2 * previous2 + previous * previous
                                                   - coefficient * previous * previous2);
            chroma[static_cast<size_t>(midi % 12)] += std::sqrt(power) / std::sqrt(frequency);
        }
    }
    if (analyzedWindows == 0) return {};

    const double chromaMean = std::accumulate(chroma.begin(), chroma.end(), 0.0) / chroma.size();
    double chromaNorm = 0.0;
    for (const double value : chroma) chromaNorm += (value - chromaMean) * (value - chromaMean);
    if (chromaNorm < 1e-9) return {};

    constexpr std::array<double, 12> majorProfile{
        6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88};
    constexpr std::array<double, 12> minorProfile{
        6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17};
    auto correlation = [&](const auto &profile, int root) {
        const double profileMean = std::accumulate(profile.begin(), profile.end(), 0.0)
                                   / profile.size();
        double numerator = 0.0;
        double profileNorm = 0.0;
        for (int pitch = 0; pitch < 12; ++pitch) {
            const double left = chroma[static_cast<size_t>((root + pitch) % 12)] - chromaMean;
            const double right = profile[static_cast<size_t>(pitch)] - profileMean;
            numerator += left * right;
            profileNorm += right * right;
        }
        return numerator / std::sqrt(chromaNorm * profileNorm);
    };

    int bestRoot = 0;
    bool bestMinor = false;
    double bestScore = -2.0;
    for (int root = 0; root < 12; ++root) {
        const double majorScore = correlation(majorProfile, root);
        if (majorScore > bestScore) {
            bestScore = majorScore;
            bestRoot = root;
            bestMinor = false;
        }
        const double minorScore = correlation(minorProfile, root);
        if (minorScore > bestScore) {
            bestScore = minorScore;
            bestRoot = root;
            bestMinor = true;
        }
    }
    if (bestScore < 0.10) return {};
    static const std::array<const char *, 12> names{
        "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    return QString::fromLatin1(names[static_cast<size_t>(bestRoot)])
           + (bestMinor ? QStringLiteral("m") : QString());
}
}

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

bool BeatAnalyzer::enqueue(int trackId, const QString &filePath)
{
    if (trackId < 0 || filePath.isEmpty() || m_pendingTrackIds.contains(trackId)) return false;
    m_pendingTrackIds.insert(trackId);
    m_queue.enqueue({trackId, filePath});
    startNext();
    return true;
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
    const ProbedTrack metadata = AudioProbe::read(m_current.filePath);
    if (!metadata.musicalKey.trimmed().isEmpty()) result.musicalKey = metadata.musicalKey.trimmed();
    result.label = metadata.label.trimmed();
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
    result.energy = estimateEnergy(samples);
    result.musicalKey = estimateMusicalKey(samples, sampleRate);

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
