#include "BeatSyncMath.h"

#include <algorithm>
#include <cmath>

double BeatSyncMath::tempoRatio(double targetBpm, double masterBpm, double masterRate)
{
    if (targetBpm <= 0.0 || masterBpm <= 0.0 || masterRate <= 0.0) return 1.0;
    return std::clamp(masterBpm * masterRate / targetBpm, 0.5, 2.0);
}

double BeatSyncMath::phase(double positionMs, double bpm, double firstBeatMs)
{
    if (bpm <= 0.0) return 0.0;
    const double period = 60000.0 / bpm;
    double value = (positionMs - firstBeatMs) / period;
    value -= std::floor(value);
    return value < 0.0 ? value + 1.0 : value;
}

double BeatSyncMath::wrappedPhaseDelta(double targetPhase, double masterPhase)
{
    double delta = targetPhase - masterPhase;
    if (delta > 0.5) delta -= 1.0;
    if (delta < -0.5) delta += 1.0;
    return delta;
}

double BeatSyncMath::driftMilliseconds(double targetPositionMs, double targetBpm,
                                       double targetFirstBeatMs, double targetRate,
                                       double masterPositionMs, double masterBpm,
                                       double masterFirstBeatMs)
{
    if (targetBpm <= 0.0 || masterBpm <= 0.0 || targetRate <= 0.0) return 0.0;
    const double delta = wrappedPhaseDelta(
        phase(targetPositionMs, targetBpm, targetFirstBeatMs),
        phase(masterPositionMs, masterBpm, masterFirstBeatMs));
    return delta * (60000.0 / targetBpm) / targetRate;
}

qint64 BeatSyncMath::alignedPosition(double targetPositionMs, qint64 targetDurationMs,
                                     double targetBpm, double targetFirstBeatMs,
                                     double masterPositionMs, double masterBpm,
                                     double masterFirstBeatMs)
{
    if (targetBpm <= 0.0 || masterBpm <= 0.0) return qRound64(targetPositionMs);
    const double targetPeriod = 60000.0 / targetBpm;
    const double delta = wrappedPhaseDelta(
        phase(targetPositionMs, targetBpm, targetFirstBeatMs),
        phase(masterPositionMs, masterBpm, masterFirstBeatMs));
    qint64 corrected = qRound64(targetPositionMs - delta * targetPeriod);
    if (targetDurationMs > 0)
        corrected = std::clamp<qint64>(corrected, 0, targetDurationMs - 1);
    else
        corrected = std::max<qint64>(0, corrected);
    return corrected;
}

double BeatSyncMath::correctedPlaybackRate(double nominalRate, double driftMs)
{
    if (nominalRate <= 0.0) return 1.0;
    const double correction = std::clamp(-driftMs * 0.00008, -0.004, 0.004);
    return nominalRate * (1.0 + correction);
}
