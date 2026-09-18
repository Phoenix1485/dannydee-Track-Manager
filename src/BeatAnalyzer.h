#pragma once

#include <QByteArray>
#include <QObject>
#include <QProcess>
#include <QQueue>
#include <QSet>
#include <QString>
#include <QTimer>

struct BeatAnalysisResult {
    int trackId = -1;
    QString filePath;
    double bpm = 0.0;
    double firstBeatMs = 0.0;
    double confidence = 0.0;
    QString musicalKey;
    QString label;
    int energy = 0;
    qint64 durationMs = 0;

    bool isValid() const { return bpm >= 50.0 && confidence > 0.0; }
};

class BeatAnalyzer : public QObject {
    Q_OBJECT

public:
    explicit BeatAnalyzer(QObject *parent = nullptr);

    bool enqueue(int trackId, const QString &filePath);
    bool isBusy() const;

    static BeatAnalysisResult analyzePcm(const QByteArray &pcm, int sampleRate = 11025,
                                         int trackId = -1, const QString &filePath = {});

signals:
    void analysisStarted(int trackId, const QString &filePath);
    void analyzed(const BeatAnalysisResult &result);
    void failed(int trackId, const QString &filePath, const QString &message);

private:
    struct Request {
        int trackId = -1;
        QString filePath;
    };

    void startNext();
    void finish(int exitCode, QProcess::ExitStatus exitStatus);
    void failCurrent(const QString &message);

    QProcess m_process;
    QTimer m_timeout;
    QQueue<Request> m_queue;
    QSet<int> m_pendingTrackIds;
    Request m_current;
    QByteArray m_pcm;
    bool m_active = false;
};

Q_DECLARE_METATYPE(BeatAnalysisResult)
