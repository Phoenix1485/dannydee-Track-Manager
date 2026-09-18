#include "BeatGridWidget.h"

#include <QLinearGradient>
#include <QPainter>
#include <QPaintEvent>

#include <cmath>

BeatGridWidget::BeatGridWidget(QWidget *parent) : QWidget(parent)
{
    setMinimumHeight(62);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

void BeatGridWidget::setGrid(double bpm, double firstBeatMs, double confidence)
{
    m_bpm = bpm;
    m_firstBeatMs = firstBeatMs;
    m_confidence = confidence;
    update();
}

void BeatGridWidget::setTrackLoaded(bool loaded)
{
    m_trackLoaded = loaded;
    update();
}

void BeatGridWidget::setPlayback(qint64 positionMs, qint64 durationMs, double playbackRate)
{
    m_positionMs = positionMs;
    m_durationMs = durationMs;
    m_playbackRate = playbackRate;
    update();
}

void BeatGridWidget::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event)
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    QLinearGradient background(0, 0, width(), 0);
    background.setColorAt(0, QColor("#0B0F18"));
    background.setColorAt(0.5, QColor("#15112A"));
    background.setColorAt(1, QColor("#0B0F18"));
    painter.fillRect(rect(), background);

    const int center = width() / 2;
    painter.setPen(QPen(QColor("#FFFFFF"), 2));
    painter.drawLine(center, 4, center, height() - 4);

    if (m_bpm <= 0.0) {
        painter.setPen(QColor("#747E94"));
        painter.drawText(rect(), Qt::AlignCenter,
                         m_trackLoaded ? QStringLiteral("Beatgrid wird analysiert …")
                                       : QStringLiteral("Track aus der Bibliothek laden"));
        return;
    }

    const double interval = 60000.0 / m_bpm;
    const double pixelsPerBeat = std::clamp(width() / 9.0, 54.0, 110.0);
    const qint64 firstVisibleBeat = static_cast<qint64>(
        std::floor((m_positionMs - m_firstBeatMs) / interval)) - 6;
    for (qint64 beat = firstVisibleBeat; beat < firstVisibleBeat + 14; ++beat) {
        const double beatTime = m_firstBeatMs + beat * interval;
        const int x = qRound(center + (beatTime - m_positionMs) / interval * pixelsPerBeat);
        if (x < 0 || x >= width()) continue;
        const bool downbeat = ((beat % 4) + 4) % 4 == 0;
        painter.setPen(QPen(downbeat ? QColor("#2DD4BF") : QColor("#8B5CF6"),
                            downbeat ? 2.0 : 1.0));
        painter.drawLine(x, downbeat ? 13 : 25, x, height() - 7);
        if (downbeat) {
            painter.setPen(QColor("#75E8D5"));
            painter.drawText(x + 4, 18, QString::number(beat / 4 + 1));
        }
    }

    painter.setPen(QColor("#AAB2C2"));
    const QString tempo = qFuzzyCompare(m_playbackRate, 1.0)
        ? QStringLiteral("%1 BPM").arg(m_bpm, 0, 'f', 2)
        : QStringLiteral("%1 BPM  →  %2")
              .arg(m_bpm, 0, 'f', 2).arg(m_bpm * m_playbackRate, 0, 'f', 2);
    painter.drawText(8, height() - 8,
                     QStringLiteral("%1   Grid %2%")
                         .arg(tempo).arg(qRound(m_confidence * 100.0)));

    if (m_durationMs > 0) {
        const double progress = std::clamp(static_cast<double>(m_positionMs) / m_durationMs, 0.0, 1.0);
        painter.fillRect(QRectF(0, height() - 3, width() * progress, 3), QColor("#9B7BFF"));
    }
}
