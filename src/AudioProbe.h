#pragma once

#include <QString>

struct ProbedTrack {
    QString title;
    QString artist;
    QString genre;
    QString musicalKey;
    QString label;
    QString releaseDate;
    double bpm = 0;
};

class AudioProbe {
public:
    static ProbedTrack read(const QString &filePath);
};

