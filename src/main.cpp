#include "MainWindow.h"
#include <QApplication>
#include <QCoreApplication>

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    QCoreApplication::setApplicationName("AO Baker Pro Standalone");
    QCoreApplication::setApplicationVersion(APP_VERSION);
    MainWindow window;
    window.show();
    return app.exec();
}
