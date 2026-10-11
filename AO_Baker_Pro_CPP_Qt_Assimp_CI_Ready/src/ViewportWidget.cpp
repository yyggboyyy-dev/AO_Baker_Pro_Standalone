#include "ViewportWidget.h"
#include <QPainter>
#include <QPen>
#include <algorithm>

ViewportWidget::ViewportWidget(QWidget* parent) : QOpenGLWidget(parent) {
    setMinimumHeight(420);
}

void ViewportWidget::setScene(const SceneData* scene) {
    m_scene = scene;
    update();
}

void ViewportWidget::initializeGL() {}
void ViewportWidget::resizeGL(int, int) {}

void ViewportWidget::paintGL() {
    QPainter p(this);
    p.fillRect(rect(), QColor(34, 40, 48));
    p.setRenderHint(QPainter::Antialiasing, true);

    if (!m_scene || m_scene->empty()) {
        p.setPen(QColor(215, 221, 229));
        p.drawText(rect(), Qt::AlignCenter, "导入 OBJ / FBX / GLB 模型后在此显示线框预览");
        return;
    }

    QVector3D minV(1e9f, 1e9f, 1e9f), maxV(-1e9f, -1e9f, -1e9f);
    for (const auto& mesh : m_scene->meshes) {
        for (const auto& v : mesh.vertices) {
            minV.setX(std::min(minV.x(), v.position.x())); minV.setY(std::min(minV.y(), v.position.y())); minV.setZ(std::min(minV.z(), v.position.z()));
            maxV.setX(std::max(maxV.x(), v.position.x())); maxV.setY(std::max(maxV.y(), v.position.y())); maxV.setZ(std::max(maxV.z(), v.position.z()));
        }
    }
    const QVector3D center = (minV + maxV) * 0.5f;
    const QVector3D span = maxV - minV;
    const float scale = 0.78f * std::min(width() / std::max(span.x() + span.z() * 0.4f, 1e-3f), height() / std::max(span.y() + span.z() * 0.35f, 1e-3f));

    auto project = [&](const QVector3D& v) {
        QVector3D q = v - center;
        const float x = q.x() + q.z() * 0.35f;
        const float y = -q.y() + q.z() * 0.22f;
        return QPointF(width() * 0.5f + x * scale, height() * 0.55f + y * scale);
    };

    p.setPen(QPen(QColor(210, 218, 226), 1));
    for (const auto& mesh : m_scene->meshes) {
        for (const auto& f : mesh.faces) {
            const QPointF a = project(mesh.vertices[f.i0].position);
            const QPointF b = project(mesh.vertices[f.i1].position);
            const QPointF c = project(mesh.vertices[f.i2].position);
            p.drawLine(a, b); p.drawLine(b, c); p.drawLine(c, a);
        }
    }

    p.setPen(QColor(150, 160, 175));
    p.drawText(14, 24, QString("Mesh: %1    Vertices: %2    Triangles: %3").arg(m_scene->meshes.size()).arg(m_scene->vertexCount()).arg(m_scene->faceCount()));
}
