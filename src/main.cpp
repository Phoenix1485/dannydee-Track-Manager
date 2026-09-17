#include "MainWindow.h"
#include "AppConfig.h"

#include <QApplication>
#include <QCoreApplication>
#include <QIcon>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName("DannyDee");
    QCoreApplication::setApplicationName("TrackManager");
    QCoreApplication::setApplicationVersion(DANNYDEE_APP_VERSION);
    app.setWindowIcon(QIcon(":/icons/app-icon.svg"));

    MainWindow window;
    window.show();
    return app.exec();
}
