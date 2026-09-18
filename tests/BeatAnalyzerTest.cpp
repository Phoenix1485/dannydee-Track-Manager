#include "BeatAnalyzer.h"

#include <QCoreApplication>
#include <QDebug>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

namespace {
QByteArray clickTrack(double bpm, double offsetSeconds, int seconds = 24, int sampleRate = 11025)
{
    std::vector<float> samples(static_cast<size_t>(seconds * sampleRate), 0.0f);
    const double beatSeconds = 60.0 / bpm;
    for (double beat = offsetSeconds; beat < seconds; beat += beatSeconds) {
        const int start = qRound(beat * sampleRate);
        const int length = qRound(0.035 * sampleRate);
        for (int index = 0; index < length && start + index < static_cast<int>(samples.size()); ++index) {
            const double decay = std::exp(-index / (sampleRate * 0.009));
            const double wave = std::sin(2.0 * 3.141592653589793 * 880.0 * index / sampleRate);
            samples[static_cast<size_t>(start + index)] += static_cast<float>(0.85 * decay * wave);
        }
    }
    QByteArray pcm(static_cast<qsizetype>(samples.size() * sizeof(float)), Qt::Uninitialized);
    std::memcpy(pcm.data(), samples.data(), static_cast<size_t>(pcm.size()));
    return pcm;
}

double circularDistance(double left, double right, double period)
{
    const double distance = std::abs(left - right);
    return std::min(distance, period - distance);
}

bool verifyTempo(double expectedBpm, double offsetSeconds)
{
    const BeatAnalysisResult result = BeatAnalyzer::analyzePcm(
        clickTrack(expectedBpm, offsetSeconds), 11025, 42, QStringLiteral("synthetic.raw"));
    const double period = 60000.0 / expectedBpm;
    const double phaseError = circularDistance(result.firstBeatMs, offsetSeconds * 1000.0, period);
    if (!result.isValid() || std::abs(result.bpm - expectedBpm) > 1.5
        || phaseError > 55.0 || result.confidence < 0.15 || result.trackId != 42) {
        qCritical() << "Unexpected beat analysis" << "expected" << expectedBpm
                    << "actual" << result.bpm << "offset" << result.firstBeatMs
                    << "phase error" << phaseError << "confidence" << result.confidence;
        return false;
    }
    return true;
}
}

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    if (!verifyTempo(120.0, 0.20)) return 1;
    if (!verifyTempo(100.0, 0.17)) return 2;

    const QByteArray silence(11025 * 8 * static_cast<int>(sizeof(float)), '\0');
    if (BeatAnalyzer::analyzePcm(silence).isValid()) {
        qCritical() << "Silence must not produce a valid beatgrid";
        return 3;
    }
    return 0;
}
