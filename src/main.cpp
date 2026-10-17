#include "MainWindow.h"
#include <QApplication>
#include <QSurfaceFormat>
#include <QCoreApplication>
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
#include <QtGlobal>
#endif

int main(int argc, char *argv[]) {
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QCoreApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
#endif
    // Prefer a desktop OpenGL 4.3 context for the Compute Shader viewport path.
    // ViewportWidget detects unsupported drivers and falls back to its CPU painter path.
    QSurfaceFormat glFormat;
    glFormat.setRenderableType(QSurfaceFormat::OpenGL);
    glFormat.setVersion(4, 3);
    glFormat.setProfile(QSurfaceFormat::CoreProfile);
    glFormat.setDepthBufferSize(24);
    glFormat.setStencilBufferSize(8);
    QSurfaceFormat::setDefaultFormat(glFormat);
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName("AO_Baker_Pro");
    QCoreApplication::setApplicationName("AO_Baker_Pro_V2");
    QCoreApplication::setApplicationVersion(APP_VERSION);
    MainWindow window;
    window.show();
    return app.exec();
}
