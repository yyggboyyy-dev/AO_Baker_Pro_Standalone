#include "MainWindow.h"
#include <QApplication>
#include <QCoreApplication>
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
#include <QtGlobal>
#endif

int main(int argc, char *argv[]) {
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QCoreApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
#endif
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName("AO_Baker_Pro");
    QCoreApplication::setApplicationName("AO_Baker_Pro_V2");
    QCoreApplication::setApplicationVersion(APP_VERSION);
    MainWindow window;
    window.show();
    return app.exec();
}
