#pragma once

#include <QObject>
#include <QProcess>

class AudioConverter : public QObject {
    Q_OBJECT
public:
    explicit AudioConverter(QObject *parent = nullptr);
    void convert(const QString &source, const QString &target, const QString &format);
signals:
    void finished(bool ok, const QString &message);
private:
    QProcess m_process;
};

