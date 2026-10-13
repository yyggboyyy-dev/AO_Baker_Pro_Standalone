#include "ViewportWidget.h"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPen>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>

namespace {
constexpr float kPi = 3.14159265358979323846f;
float degToRad(float degrees) { return degrees * kPi / 180.0f; }
float clamp01(float v) { return std::max(0.0f, std::min(1.0f, v)); }
}

ViewportWidget::ViewportWidget(QWidget* parent) : QOpenGLWidget(parent) {
    setMinimumHeight(420);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
}

void ViewportWidget::setScene(const SceneData* scene) {
    m_scene = scene;
    m_selectedMesh = -1;
    updateBounds();
    resetCamera();
    update();
}

void ViewportWidget::setBakeSettings(const BakeSettings& settings) {
    m_settings = settings;
    update();
}

void ViewportWidget::setRealtimePreviewEnabled(bool enabled) {
    m_realtimePreview = enabled;
    update();
}

void ViewportWidget::setWireframeVisible(bool visible) {
    m_wireframeVisible = visible;
    update();
}

void ViewportWidget::setDefaultMaterialColor(const QColor& color) {
    m_defaultMaterialColor = color;
    update();
}

void ViewportWidget::setUseVertexColor(bool enabled) {
    m_useVertexColor = enabled;
    update();
}

void ViewportWidget::setSelectedMesh(int meshIndex) {
    m_selectedMesh = meshIndex;
    update();
}

void ViewportWidget::initializeGL() {}
void ViewportWidget::resizeGL(int, int) {}

void ViewportWidget::updateBounds() {
    if (!m_scene || m_scene->empty()) {
        m_center = QVector3D(0, 0, 0);
        m_radius = 1.0f;
        return;
    }

    QVector3D minV(1e9f, 1e9f, 1e9f), maxV(-1e9f, -1e9f, -1e9f);
    bool hasVertex = false;
    for (const auto& mesh : m_scene->meshes) {
        for (const auto& v : mesh.vertices) {
            hasVertex = true;
            minV.setX(std::min(minV.x(), v.position.x()));
            minV.setY(std::min(minV.y(), v.position.y()));
            minV.setZ(std::min(minV.z(), v.position.z()));
            maxV.setX(std::max(maxV.x(), v.position.x()));
            maxV.setY(std::max(maxV.y(), v.position.y()));
            maxV.setZ(std::max(maxV.z(), v.position.z()));
        }
    }

    if (!hasVertex) {
        m_center = QVector3D(0, 0, 0);
        m_radius = 1.0f;
        return;
    }

    m_center = (minV + maxV) * 0.5f;
    m_radius = std::max((maxV - minV).length() * 0.5f, 0.001f);
}

void ViewportWidget::resetCamera() {
    m_yaw = -35.0f;
    m_pitch = 22.0f;
    m_distance = m_radius * 3.0f;
    m_zoom = 1.0f;
    m_pan = QPointF(0.0, 0.0);
}

QVector3D ViewportWidget::rotateToCamera(const QVector3D& world) const {
    QVector3D q = world - m_center;

    const float yaw = degToRad(m_yaw);
    const float cy = std::cos(yaw);
    const float sy = std::sin(yaw);
    QVector3D yRot(q.x() * cy + q.z() * sy, q.y(), -q.x() * sy + q.z() * cy);

    const float pitch = degToRad(m_pitch);
    const float cp = std::cos(pitch);
    const float sp = std::sin(pitch);
    QVector3D cam(yRot.x(), yRot.y() * cp - yRot.z() * sp, yRot.y() * sp + yRot.z() * cp);
    cam.setZ(cam.z() - m_distance);
    return cam;
}

ViewportWidget::ProjectedVertex ViewportWidget::projectVertex(const QVector3D& world) const {
    const QVector3D cam = rotateToCamera(world);
    const float focal = std::min(width(), height()) * 0.95f * m_zoom;
    const float z = std::max(0.05f, -cam.z());
    const QPointF screen(
        width() * 0.5 + m_pan.x() + cam.x() * focal / z,
        height() * 0.5 + m_pan.y() - cam.y() * focal / z
    );
    return {screen, z, cam};
}

QColor ViewportWidget::previewColor(const MeshData& mesh, const Vertex& v, int meshIndex) const {
    QColor base = (m_useVertexColor && mesh.hasVertexColor) ? v.color : m_defaultMaterialColor;

    if (m_realtimePreview) {
        QVector3D n = v.normal.lengthSquared() > 0.00001f ? v.normal.normalized() : QVector3D(0, 1, 0);
        const float upward = clamp01((n.y() + 1.0f) * 0.5f);
        float cavity = std::pow(1.0f - upward, std::max(0.2f, m_settings.transitionContrast));
        cavity = cavity * m_settings.aoStrength;
        float ao = 1.0f - cavity;
        ao = m_settings.darkest + (m_settings.brightest - m_settings.darkest) * clamp01(ao);
        const QColor bounce = m_settings.groundBounceColor;
        const float bounceAmount = clamp01(m_settings.groundBounceStrength * upward * 0.35f);
        int r = int(base.red() * ao * (1.0f - bounceAmount) + bounce.red() * bounceAmount);
        int g = int(base.green() * ao * (1.0f - bounceAmount) + bounce.green() * bounceAmount);
        int b = int(base.blue() * ao * (1.0f - bounceAmount) + bounce.blue() * bounceAmount);
        base = QColor(std::clamp(r, 0, 255), std::clamp(g, 0, 255), std::clamp(b, 0, 255), 255);
    }

    if (meshIndex == m_selectedMesh) {
        base = QColor(
            std::min(255, int(base.red() * 0.75f + 255 * 0.25f)),
            std::min(255, int(base.green() * 0.72f + 176 * 0.28f)),
            std::min(255, int(base.blue() * 0.55f + 40 * 0.45f)),
            255
        );
    }
    return base;
}

void ViewportWidget::drawGrid(QPainter& p) const {
    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    const float gridSize = m_radius * 2.0f;
    const int steps = 10;
    p.setPen(QPen(QColor(62, 72, 84), 1));
    for (int i = -steps; i <= steps; ++i) {
        const float t = gridSize * float(i) / float(steps);
        const QPointF a = projectVertex(m_center + QVector3D(t, -m_radius, -gridSize)).screen;
        const QPointF b = projectVertex(m_center + QVector3D(t, -m_radius, gridSize)).screen;
        const QPointF c = projectVertex(m_center + QVector3D(-gridSize, -m_radius, t)).screen;
        const QPointF d = projectVertex(m_center + QVector3D(gridSize, -m_radius, t)).screen;
        p.drawLine(a, b);
        p.drawLine(c, d);
    }
    p.setPen(QPen(QColor(80, 98, 120), 2));
    p.drawLine(projectVertex(m_center + QVector3D(-gridSize, -m_radius, 0)).screen,
               projectVertex(m_center + QVector3D(gridSize, -m_radius, 0)).screen);
    p.drawLine(projectVertex(m_center + QVector3D(0, -m_radius, -gridSize)).screen,
               projectVertex(m_center + QVector3D(0, -m_radius, gridSize)).screen);
    p.restore();
}

void ViewportWidget::drawOverlay(QPainter& p) const {
    p.save();
    p.setPen(QColor(178, 188, 202));
    const QString selected = (m_scene && m_selectedMesh >= 0 && m_selectedMesh < m_scene->meshes.size())
        ? m_scene->meshes[m_selectedMesh].name
        : QString("无");
    p.drawText(14, 24, QString("Mesh: %1    Vertices: %2    Triangles: %3    Selected: %4")
        .arg(m_scene ? m_scene->meshes.size() : 0)
        .arg(m_scene ? m_scene->vertexCount() : 0)
        .arg(m_scene ? m_scene->faceCount() : 0)
        .arg(selected));
    p.drawText(14, height() - 42, "Maya 操作：Alt+左键旋转 / Alt+中键平移 / Alt+右键缩放 / 滚轮缩放 / F 重置视图 / 鼠标左键点选模型");
    p.drawText(14, height() - 20, QString("实时预览：%1    线框：%2    显示顶点色：%3    材质色：%4")
        .arg(m_realtimePreview ? "开启" : "关闭")
        .arg(m_wireframeVisible ? "开启" : "关闭")
        .arg(m_useVertexColor ? "开启" : "关闭")
        .arg(m_defaultMaterialColor.name()));
    p.restore();
}

void ViewportWidget::paintGL() {
    QPainter p(this);
    p.fillRect(rect(), QColor(30, 35, 42));
    p.setRenderHint(QPainter::Antialiasing, true);

    if (!m_scene || m_scene->empty()) {
        p.setPen(QColor(215, 221, 229));
        p.drawText(rect(), Qt::AlignCenter, "导入 OBJ / FBX / GLB 模型后在此显示 3D 视口");
        return;
    }

    drawGrid(p);

    QVector<DrawTriangle> triangles;
    triangles.reserve(m_scene->faceCount());

    for (int meshIndex = 0; meshIndex < m_scene->meshes.size(); ++meshIndex) {
        const auto& mesh = m_scene->meshes[meshIndex];
        for (const auto& f : mesh.faces) {
            if (f.i0 < 0 || f.i1 < 0 || f.i2 < 0 || f.i0 >= mesh.vertices.size() || f.i1 >= mesh.vertices.size() || f.i2 >= mesh.vertices.size()) {
                continue;
            }
            const auto p0 = projectVertex(mesh.vertices[f.i0].position);
            const auto p1 = projectVertex(mesh.vertices[f.i1].position);
            const auto p2 = projectVertex(mesh.vertices[f.i2].position);
            if (p0.camera.z() > -0.02f || p1.camera.z() > -0.02f || p2.camera.z() > -0.02f) {
                continue;
            }
            QColor color = previewColor(mesh, mesh.vertices[f.i0], meshIndex);
            color.setAlpha(meshIndex == m_selectedMesh ? 245 : 210);
            triangles.push_back({meshIndex, p0.screen, p1.screen, p2.screen, (p0.depth + p1.depth + p2.depth) / 3.0f, color});
        }
    }

    std::sort(triangles.begin(), triangles.end(), [](const DrawTriangle& lhs, const DrawTriangle& rhs){
        return lhs.depth > rhs.depth;
    });

    p.setPen(Qt::NoPen);
    for (const auto& t : triangles) {
        p.setBrush(t.color);
        p.drawPolygon(QPolygonF({t.a, t.b, t.c}));
    }

    if (m_wireframeVisible) {
        p.setBrush(Qt::NoBrush);
        for (const auto& t : triangles) {
            const bool selected = t.meshIndex == m_selectedMesh;
            p.setPen(QPen(selected ? QColor(255, 176, 40) : QColor(78, 88, 101), selected ? 2 : 1));
            p.drawLine(t.a, t.b);
            p.drawLine(t.b, t.c);
            p.drawLine(t.c, t.a);
        }
    }

    drawOverlay(p);
}

void ViewportWidget::mousePressEvent(QMouseEvent* event) {
    setFocus();
    m_lastMousePos = event->pos();
    m_pressMousePos = event->pos();
    m_activeButton = event->button();
    m_dragged = false;
    event->accept();
}

void ViewportWidget::mouseMoveEvent(QMouseEvent* event) {
    const QPoint delta = event->pos() - m_lastMousePos;
    if (delta.manhattanLength() > 1) {
        m_dragged = true;
    }

    const bool mayaNav = event->modifiers() & Qt::AltModifier;
    if (mayaNav && (event->buttons() & Qt::LeftButton)) {
        m_yaw += delta.x() * 0.45f;
        m_pitch += delta.y() * 0.45f;
        m_pitch = std::clamp(m_pitch, -89.0f, 89.0f);
        update();
    } else if (mayaNav && (event->buttons() & Qt::MiddleButton)) {
        m_pan += QPointF(delta.x(), delta.y());
        update();
    } else if (mayaNav && (event->buttons() & Qt::RightButton)) {
        const float factor = std::exp(float(delta.x() - delta.y()) * 0.01f);
        m_zoom = std::clamp(m_zoom * factor, 0.05f, 50.0f);
        update();
    }

    m_lastMousePos = event->pos();
    event->accept();
}

void ViewportWidget::mouseReleaseEvent(QMouseEvent* event) {
    const bool clickLike = !m_dragged && (event->pos() - m_pressMousePos).manhattanLength() < 4;
    if (clickLike && event->button() == Qt::LeftButton && !(event->modifiers() & Qt::AltModifier)) {
        const int picked = pickMeshAt(event->pos());
        if (picked != m_selectedMesh) {
            m_selectedMesh = picked;
            const QString name = (m_scene && picked >= 0 && picked < m_scene->meshes.size()) ? m_scene->meshes[picked].name : QString();
            emit meshSelected(picked, name);
            update();
        }
    }
    m_activeButton = Qt::NoButton;
    event->accept();
}

void ViewportWidget::wheelEvent(QWheelEvent* event) {
    const float steps = float(event->angleDelta().y()) / 120.0f;
    m_zoom = std::clamp(m_zoom * std::pow(1.12f, steps), 0.05f, 50.0f);
    update();
    event->accept();
}

void ViewportWidget::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_F) {
        resetCamera();
        update();
        event->accept();
        return;
    }
    QOpenGLWidget::keyPressEvent(event);
}

bool ViewportWidget::pointInTriangle(const QPointF& p, const QPointF& a, const QPointF& b, const QPointF& c) {
    const double d1 = (p.x() - b.x()) * (a.y() - b.y()) - (a.x() - b.x()) * (p.y() - b.y());
    const double d2 = (p.x() - c.x()) * (b.y() - c.y()) - (b.x() - c.x()) * (p.y() - c.y());
    const double d3 = (p.x() - a.x()) * (c.y() - a.y()) - (c.x() - a.x()) * (p.y() - a.y());
    const bool hasNeg = (d1 < 0) || (d2 < 0) || (d3 < 0);
    const bool hasPos = (d1 > 0) || (d2 > 0) || (d3 > 0);
    return !(hasNeg && hasPos);
}

int ViewportWidget::pickMeshAt(const QPoint& pos) const {
    if (!m_scene || m_scene->empty()) return -1;

    int bestMesh = -1;
    float bestDepth = 1e9f;
    const QPointF p(pos);
    for (int meshIndex = 0; meshIndex < m_scene->meshes.size(); ++meshIndex) {
        const auto& mesh = m_scene->meshes[meshIndex];
        for (const auto& f : mesh.faces) {
            if (f.i0 < 0 || f.i1 < 0 || f.i2 < 0 || f.i0 >= mesh.vertices.size() || f.i1 >= mesh.vertices.size() || f.i2 >= mesh.vertices.size()) {
                continue;
            }
            const auto p0 = projectVertex(mesh.vertices[f.i0].position);
            const auto p1 = projectVertex(mesh.vertices[f.i1].position);
            const auto p2 = projectVertex(mesh.vertices[f.i2].position);
            if (p0.camera.z() > -0.02f || p1.camera.z() > -0.02f || p2.camera.z() > -0.02f) {
                continue;
            }
            if (pointInTriangle(p, p0.screen, p1.screen, p2.screen)) {
                const float depth = (p0.depth + p1.depth + p2.depth) / 3.0f;
                if (depth < bestDepth) {
                    bestDepth = depth;
                    bestMesh = meshIndex;
                }
            }
        }
    }
    return bestMesh;
}
