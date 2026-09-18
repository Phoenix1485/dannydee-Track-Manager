#pragma once

#include <QtGlobal>

class BeatSyncMath {
public:
    static double tempoRatio(double targetBpm, double masterBpm, double masterRate);
    static double phase(double positionMs, double bpm, double firstBeatMs);
    static double driftMilliseconds(double targetPositionMs, double targetBpm,
                                    double targetFirstBeatMs, double targetRate,
                                    double masterPositionMs, double masterBpm,
                                    double masterFirstBeatMs);
    static qint64 alignedPosition(double targetPositionMs, qint64 targetDurationMs,
                                  double targetBpm, double targetFirstBeatMs,
                                  double masterPositionMs, double masterBpm,
                                  double masterFirstBeatMs);
    static double correctedPlaybackRate(double nominalRate, double driftMs);

private:
    static double wrappedPhaseDelta(double targetPhase, double masterPhase);
};
