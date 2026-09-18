#include "BeatSyncMath.h"

#include <QCoreApplication>
#include <QDebug>

#include <cmath>

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);

    const double ratio = BeatSyncMath::tempoRatio(120.0, 128.0, 1.0);
    if (std::abs(ratio - 1.0666666667) > 0.0001) {
        qCritical() << "Wrong tempo ratio" << ratio;
        return 1;
    }

    const qint64 aligned = BeatSyncMath::alignedPosition(
        1300.0, 240000, 120.0, 250.0,
        1100.0, 128.0, 100.0);
    const double drift = BeatSyncMath::driftMilliseconds(
        aligned, 120.0, 250.0, ratio,
        1100.0, 128.0, 100.0);
    if (std::abs(drift) > 1.0) {
        qCritical() << "Aligned decks still drift by" << drift << "ms";
        return 2;
    }

    const double wrapDrift = BeatSyncMath::driftMilliseconds(
        740.0, 120.0, 250.0, 1.0,
        255.0, 120.0, 250.0);
    if (std::abs(wrapDrift + 15.0) > 1.0) {
        qCritical() << "Phase wrap is wrong" << wrapDrift;
        return 3;
    }

    if (BeatSyncMath::correctedPlaybackRate(1.0, 30.0) >= 1.0
        || BeatSyncMath::correctedPlaybackRate(1.0, -30.0) <= 1.0) {
        qCritical() << "Drift correction moves in the wrong direction";
        return 4;
    }

    return 0;
}
