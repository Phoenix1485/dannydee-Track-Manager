#pragma once

#include <QString>
#include <QUrl>

struct ResolvedLink {
    QString provider;
    QString kind;
    QUrl url;
    bool downloadable = false;
    QString guidance;
};

class LinkResolver {
public:
    static ResolvedLink resolve(const QString &input);
};

