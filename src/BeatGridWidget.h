#pragma once

#include <QWidget>

class BeatGridWidget : public QWidget {
public:
    explicit BeatGridWidget(QWidget *parent = nullptr);

    void setGrid(double bpm, double firstBeatMs, double confidence);
    void setTrackLoaded(bool loaded);
    void setPlayback(qint64 positionMs, qint64 durationMs, double playbackRate);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    double m_bpm = 0.0;
    double m_firstBeatMs = 0.0;
    double m_confidence = 0.0;
    double m_playbackRate = 1.0;
    qint64 m_positionMs = 0;
    qint64 m_durationMs = 0;
    bool m_trackLoaded = false;
};
