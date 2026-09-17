#include "ToolLocator.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>
#include <QStringList>

QString ToolLocator::find(const QString &baseName)
{
#ifdef Q_OS_WIN
    const QString fileName = baseName + ".exe";
#else
    const QString fileName = baseName;
#endif

    const QString appDirectory = QCoreApplication::applicationDirPath();
    QStringList candidates;
    const QString overrideDirectory = qEnvironmentVariable("DANNYDEE_TOOLS_DIR");
    if (!overrideDirectory.isEmpty())
        candidates << QDir(overrideDirectory).filePath(fileName);
    candidates << QDir(appDirectory).filePath("tools/" + fileName);
#ifdef Q_OS_MACOS
    candidates.prepend(QDir(appDirectory).filePath("../Resources/tools/" + fileName));
#endif

    for (const QString &candidate : candidates) {
        const QFileInfo info(candidate);
        if (info.exists() && info.isFile()) return info.absoluteFilePath();
    }
    return QStandardPaths::findExecutable(baseName);
}
