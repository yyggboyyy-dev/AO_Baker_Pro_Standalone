#include "ViewportWidget.h"

#include <QJsonObject>
#include <QJsonArray>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QLineF>
#include <QLinearGradient>
#include <QRadialGradient>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>
#include <limits>

namespace {
constexpr float kPi = 3.14159265358979323846f;
float degToRad(float degrees) { return degrees * kPi / 180.0f; }
float clamp01(float v) { return std::max(0.0f, std::min(1.0f, v)); }
int blendChannelValue(int base, int paint, const QString& mode) {
    if (mode == QStringLiteral("相加")) return std::min(255, base + paint);
    if (mode == QStringLiteral("相减")) return std::max(0, base - paint);
    if (mode == QStringLiteral("正片叠底") || mode == QStringLiteral("相乘")) return base * paint / 255;
    if (mode == QStringLiteral("滤色")) return 255 - (255 - base) * (255 - paint) / 255;
    if (mode == QStringLiteral("覆盖") || mode == QStringLiteral("叠加")) {
        return base < 128 ? (2 * base * paint / 255) : (255 - 2 * (255 - base) * (255 - paint) / 255);
    }
    if (mode == QStringLiteral("变亮") || mode == QStringLiteral("最大值") || mode == QStringLiteral("加亮")) return std::max(base, paint);
    if (mode == QStringLiteral("变暗") || mode == QStringLiteral("最小值")) return std::min(base, paint);
    return paint;
}
quint64 changeKey(int meshIndex, int vertexIndex) {
    return (quint64(quint32(meshIndex)) << 32) | quint32(vertexIndex);
}
}

ViewportWidget::ViewportWidget(QWidget* parent) : QOpenGLWidget(parent) {
    setMinimumHeight(420);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    m_frameTimer.start();
}

void ViewportWidget::setScene(SceneData* scene) {
    m_scene = scene;
    m_selectedMesh = -1;
    m_undoStack.clear();
    m_redoStack.clear();
    m_pendingStroke.clear();
    m_pendingStrokeIndex.clear();
    m_strokeActive = false;
    emit undoAvailabilityChanged(canUndo(), canRedo());
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

void ViewportWidget::toggleRealtimePreview() {
    m_realtimePreview = !m_realtimePreview;
    update();
}

void ViewportWidget::setDisplayMode(const QString& mode) {
    m_displayMode = mode;
    m_realtimePreview = (mode == QStringLiteral("AO 预览") || mode == QStringLiteral("AO 贴图预览") || mode == QStringLiteral("烘焙误差显示"));
    m_useVertexColor = (mode == QStringLiteral("顶点色模式") || mode == QStringLiteral("AO 预览") || mode == QStringLiteral("AO 贴图预览") || mode == QStringLiteral("过绘制显示") || mode == QStringLiteral("烘焙误差显示"));
    m_wireframeVisible = (mode == QStringLiteral("线框模式") || mode == QStringLiteral("实体 + 线框"));
    update();
}

void ViewportWidget::setOrthographicView(bool enabled) {
    m_orthographicView = enabled;
    update();
}

void ViewportWidget::saveCurrentCameraView() {
    m_savedYaw = m_yaw;
    m_savedPitch = m_pitch;
    m_savedDistance = m_distance;
    m_savedZoom = m_zoom;
    m_savedPan = m_pan;
}

void ViewportWidget::restoreSavedCameraView() {
    m_yaw = m_savedYaw;
    m_pitch = m_savedPitch;
    m_distance = m_savedDistance;
    m_zoom = m_savedZoom;
    m_pan = m_savedPan;
    update();
}

void ViewportWidget::restoreDefaultCameraView() {
    updateBounds();
    resetCamera();
    m_orthographicView = false;
    update();
}

void ViewportWidget::setCameraView(const QString& viewName) {
    if (viewName == QStringLiteral("透视图")) {
        m_orthographicView = false;
        m_yaw = -35.0f;
        m_pitch = 22.0f;
    } else if (viewName == QStringLiteral("前视图") || viewName == QStringLiteral("正面正交视图")) {
        m_orthographicView = true;
        m_yaw = 0.0f;
        m_pitch = 0.0f;
    } else if (viewName == QStringLiteral("左视图") || viewName == QStringLiteral("左侧视图")) {
        m_orthographicView = true;
        m_yaw = -90.0f;
        m_pitch = 0.0f;
    } else if (viewName == QStringLiteral("右视图") || viewName == QStringLiteral("右侧视图")) {
        m_orthographicView = true;
        m_yaw = 90.0f;
        m_pitch = 0.0f;
    } else if (viewName == QStringLiteral("后视图") || viewName == QStringLiteral("背后视图")) {
        m_orthographicView = true;
        m_yaw = 180.0f;
        m_pitch = 0.0f;
    } else if (viewName == QStringLiteral("顶视图") || viewName == QStringLiteral("顶部视图")) {
        m_orthographicView = true;
        m_yaw = 0.0f;
        m_pitch = 89.0f;
    } else if (viewName == QStringLiteral("底视图") || viewName == QStringLiteral("底部视图")) {
        m_orthographicView = true;
        m_yaw = 0.0f;
        m_pitch = -89.0f;
    }
    m_pan = QPointF(0.0, 0.0);
    update();
}

void ViewportWidget::focusModel() {
    updateBounds();
    resetCamera();
    update();
}

void ViewportWidget::setWireframeVisible(bool visible) {
    m_wireframeVisible = visible;
    update();
}

void ViewportWidget::toggleWireframeVisible() {
    m_wireframeVisible = !m_wireframeVisible;
    update();
}

void ViewportWidget::toggleGridVisible() {
    m_gridVisible = !m_gridVisible;
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

void ViewportWidget::setVertexPaintEnabled(bool enabled) {
    m_vertexPaintEnabled = enabled;
    m_useVertexColor = enabled ? true : m_useVertexColor;
    setCursor(enabled ? Qt::BlankCursor : Qt::ArrowCursor);
    update();
}

void ViewportWidget::toggleVertexPaintEnabled() {
    setVertexPaintEnabled(!m_vertexPaintEnabled);
    if (m_vertexPaintEnabled) {
        m_paintEraseMode = false;
        m_paintMode = 0;
    }
}

void ViewportWidget::togglePaintEraseMode() {
    setVertexPaintEnabled(true);
    m_paintEraseMode = !m_paintEraseMode;
    m_paintMode = 0;
    update();
}

void ViewportWidget::restoreSelectionMode() {
    m_vertexPaintEnabled = false;
    m_paintEraseMode = false;
    m_paintMode = 0;
    setCursor(Qt::ArrowCursor);
    update();
}

void ViewportWidget::setVisibleChannelPreset(int preset) {
    if (preset == 1) {
        setVisibleChannels(true, false, false);
    } else if (preset == 2) {
        setVisibleChannels(false, true, false);
    } else if (preset == 3) {
        setVisibleChannels(false, false, true);
    } else {
        setVisibleChannels(true, true, true);
    }
}

void ViewportWidget::setPaintEraseMode(bool enabled) {
    m_paintEraseMode = enabled;
}

void ViewportWidget::setPaintMode(int mode) {
    m_paintMode = mode;
}

void ViewportWidget::setPaintBlendMode(const QString& mode) {
    m_paintBlendMode = mode;
}

void ViewportWidget::setPaintColor(const QColor& color) {
    m_paintColor = color;
}

void ViewportWidget::setPaintOpacity(float opacity) {
    m_paintOpacity = std::clamp(opacity, 0.0f, 1.0f);
}

void ViewportWidget::setPaintRadius(float radius) {
    m_paintRadius = std::clamp(radius, 1.0f, 300.0f);
}

void ViewportWidget::setPaintHardness(float hardness) {
    m_paintHardness = std::clamp(hardness, 0.0f, 1.0f);
    update();
}

void ViewportWidget::setPaintSoftBrush(bool enabled) {
    m_paintSoftBrush = enabled;
}

void ViewportWidget::setPressureSimulationEnabled(bool enabled) {
    m_pressureSimulation = enabled;
    update();
}

void ViewportWidget::setMirrorPaintingEnabled(bool enabled) {
    m_mirrorPainting = enabled;
    update();
}

void ViewportWidget::setNormalFalloffEnabled(bool enabled) {
    m_normalFalloff = enabled;
    update();
}

void ViewportWidget::setLayerOpacity(float opacity) {
    m_layerOpacity = std::clamp(opacity, 0.0f, 1.0f);
    update();
}

void ViewportWidget::setVisibleChannels(bool red, bool green, bool blue) {
    m_showRedChannel = red;
    m_showGreenChannel = green;
    m_showBlueChannel = blue;
    update();
}

void ViewportWidget::setDisplayChannelMode(const QString& mode) {
    m_displayChannelMode = mode;
    if (mode == QStringLiteral("仅显示R")) setVisibleChannels(true, false, false);
    else if (mode == QStringLiteral("仅显示G")) setVisibleChannels(false, true, false);
    else if (mode == QStringLiteral("仅显示B")) setVisibleChannels(false, false, true);
    else setVisibleChannels(true, true, true);
}

void ViewportWidget::setIgnoreBackfaces(bool enabled) {
    m_ignoreBackfaces = enabled;
}

void ViewportWidget::setLockedChannels(bool lockRed, bool lockGreen, bool lockBlue) {
    m_lockRed = lockRed;
    m_lockGreen = lockGreen;
    m_lockBlue = lockBlue;
}

void ViewportWidget::setSelectedMesh(int meshIndex) {
    m_selectedMesh = meshIndex;
    update();
}

void ViewportWidget::undo() {
    if (!m_scene || m_undoStack.isEmpty()) return;
    const QVector<ColorChange> step = m_undoStack.takeLast();
    applyColorChanges(step, false);
    m_redoStack.push_back(step);
    emit undoAvailabilityChanged(canUndo(), canRedo());
    emit sceneModified();
}

void ViewportWidget::redo() {
    if (!m_scene || m_redoStack.isEmpty()) return;
    const QVector<ColorChange> step = m_redoStack.takeLast();
    applyColorChanges(step, true);
    m_undoStack.push_back(step);
    emit undoAvailabilityChanged(canUndo(), canRedo());
    emit sceneModified();
}

QVector<QVector<QColor>> ViewportWidget::captureVertexColors() const {
    QVector<QVector<QColor>> snapshot;
    if (!m_scene) return snapshot;
    snapshot.reserve(m_scene->meshes.size());
    for (const auto& mesh : m_scene->meshes) {
        QVector<QColor> colors;
        colors.reserve(mesh.vertices.size());
        for (const auto& vertex : mesh.vertices) colors.push_back(vertex.color);
        snapshot.push_back(colors);
    }
    return snapshot;
}

void ViewportWidget::pushVertexColorUndo(const QVector<QVector<QColor>>& beforeColors) {
    if (!m_scene) return;
    QVector<ColorChange> changes;
    for (int meshIndex = 0; meshIndex < m_scene->meshes.size() && meshIndex < beforeColors.size(); ++meshIndex) {
        const auto& mesh = m_scene->meshes[meshIndex];
        const auto& oldColors = beforeColors[meshIndex];
        const int count = std::min(static_cast<int>(mesh.vertices.size()), static_cast<int>(oldColors.size()));
        for (int vertexIndex = 0; vertexIndex < count; ++vertexIndex) {
            const QColor& oldColor = oldColors[vertexIndex];
            const QColor& newColor = mesh.vertices[vertexIndex].color;
            if (oldColor == newColor) continue;
            ColorChange change;
            change.meshIndex = meshIndex;
            change.vertexIndex = vertexIndex;
            change.oldColor = oldColor;
            change.newColor = newColor;
            changes.push_back(change);
        }
    }
    pushUndoStep(changes);
    emit sceneModified();
}

QJsonObject ViewportWidget::saveViewState() const {
    QJsonObject obj;
    obj["yaw"] = m_yaw;
    obj["pitch"] = m_pitch;
    obj["distance"] = m_distance;
    obj["zoom"] = m_zoom;
    obj["panX"] = m_pan.x();
    obj["panY"] = m_pan.y();
    obj["wireframe"] = m_wireframeVisible;
    obj["orthographicView"] = m_orthographicView;
    obj["displayMode"] = m_displayMode;
    obj["useVertexColor"] = m_useVertexColor;
    obj["realtimePreview"] = m_realtimePreview;
    obj["defaultMaterialColor"] = m_defaultMaterialColor.name(QColor::HexArgb);
    obj["selectedMesh"] = m_selectedMesh;

    QJsonObject brush;
    brush["vertexPaintEnabled"] = m_vertexPaintEnabled;
    brush["eraseMode"] = m_paintEraseMode;
    brush["paintMode"] = m_paintMode;
    brush["blendMode"] = m_paintBlendMode;
    brush["paintColor"] = m_paintColor.name(QColor::HexArgb);
    brush["opacity"] = m_paintOpacity;
    brush["radius"] = m_paintRadius;
    brush["hardness"] = m_paintHardness;
    brush["softBrush"] = m_paintSoftBrush;
    brush["pressureSimulation"] = m_pressureSimulation;
    brush["mirrorPainting"] = m_mirrorPainting;
    brush["normalFalloff"] = m_normalFalloff;
    brush["layerOpacity"] = m_layerOpacity;
    brush["ignoreBackfaces"] = m_ignoreBackfaces;
    QJsonArray visibleChannels;
    visibleChannels.append(m_showRedChannel);
    visibleChannels.append(m_showGreenChannel);
    visibleChannels.append(m_showBlueChannel);
    brush["visibleChannels"] = visibleChannels;
    QJsonArray lockedChannels;
    lockedChannels.append(m_lockRed);
    lockedChannels.append(m_lockGreen);
    lockedChannels.append(m_lockBlue);
    brush["lockedChannels"] = lockedChannels;
    obj["brushSettings"] = brush;
    return obj;
}

void ViewportWidget::restoreViewState(const QJsonObject& state) {
    m_yaw = float(state.value("yaw").toDouble(m_yaw));
    m_pitch = float(state.value("pitch").toDouble(m_pitch));
    m_distance = float(state.value("distance").toDouble(m_distance));
    m_zoom = float(state.value("zoom").toDouble(m_zoom));
    m_pan.setX(state.value("panX").toDouble(m_pan.x()));
    m_pan.setY(state.value("panY").toDouble(m_pan.y()));
    m_wireframeVisible = state.value("wireframe").toBool(m_wireframeVisible);
    m_orthographicView = state.value("orthographicView").toBool(m_orthographicView);
    m_displayMode = state.value("displayMode").toString(m_displayMode);
    m_useVertexColor = state.value("useVertexColor").toBool(m_useVertexColor);
    m_realtimePreview = state.value("realtimePreview").toBool(m_realtimePreview);
    m_defaultMaterialColor = QColor(state.value("defaultMaterialColor").toString(m_defaultMaterialColor.name(QColor::HexArgb)));
    m_selectedMesh = state.value("selectedMesh").toInt(m_selectedMesh);

    const QJsonObject brush = state.value("brushSettings").toObject();
    if (!brush.isEmpty()) {
        m_vertexPaintEnabled = brush.value("vertexPaintEnabled").toBool(m_vertexPaintEnabled);
        m_paintEraseMode = brush.value("eraseMode").toBool(m_paintEraseMode);
        m_paintMode = brush.value("paintMode").toInt(m_paintMode);
        m_paintBlendMode = brush.value("blendMode").toString(m_paintBlendMode);
        m_paintColor = QColor(brush.value("paintColor").toString(m_paintColor.name(QColor::HexArgb)));
        m_paintOpacity = std::clamp(float(brush.value("opacity").toDouble(m_paintOpacity)), 0.0f, 1.0f);
        m_paintRadius = std::clamp(float(brush.value("radius").toDouble(m_paintRadius)), 1.0f, 300.0f);
        m_paintHardness = std::clamp(float(brush.value("hardness").toDouble(m_paintHardness)), 0.0f, 1.0f);
        m_paintSoftBrush = brush.value("softBrush").toBool(m_paintSoftBrush);
        m_pressureSimulation = brush.value("pressureSimulation").toBool(m_pressureSimulation);
        m_mirrorPainting = brush.value("mirrorPainting").toBool(m_mirrorPainting);
        m_normalFalloff = brush.value("normalFalloff").toBool(m_normalFalloff);
        m_layerOpacity = std::clamp(float(brush.value("layerOpacity").toDouble(m_layerOpacity)), 0.0f, 1.0f);
        m_ignoreBackfaces = brush.value("ignoreBackfaces").toBool(m_ignoreBackfaces);
        const QJsonArray visibleChannels = brush.value("visibleChannels").toArray();
        if (visibleChannels.size() >= 3) {
            m_showRedChannel = visibleChannels.at(0).toBool(m_showRedChannel);
            m_showGreenChannel = visibleChannels.at(1).toBool(m_showGreenChannel);
            m_showBlueChannel = visibleChannels.at(2).toBool(m_showBlueChannel);
        }
        const QJsonArray lockedChannels = brush.value("lockedChannels").toArray();
        if (lockedChannels.size() >= 3) {
            m_lockRed = lockedChannels.at(0).toBool(m_lockRed);
            m_lockGreen = lockedChannels.at(1).toBool(m_lockGreen);
            m_lockBlue = lockedChannels.at(2).toBool(m_lockBlue);
        }
    }
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

QVector3D ViewportWidget::rotateDirectionToCamera(const QVector3D& dir) const {
    const float yaw = degToRad(m_yaw);
    const float cy = std::cos(yaw);
    const float sy = std::sin(yaw);
    QVector3D yRot(dir.x() * cy + dir.z() * sy, dir.y(), -dir.x() * sy + dir.z() * cy);

    const float pitch = degToRad(m_pitch);
    const float cp = std::cos(pitch);
    const float sp = std::sin(pitch);
    return QVector3D(yRot.x(), yRot.y() * cp - yRot.z() * sp, yRot.y() * sp + yRot.z() * cp);
}

QVector3D ViewportWidget::cameraRightWorld() const {
    const float yaw = degToRad(m_yaw);
    return QVector3D(std::cos(yaw), 0.0f, std::sin(yaw)).normalized();
}

ViewportWidget::ProjectedVertex ViewportWidget::projectVertex(const QVector3D& world) const {
    const QVector3D cam = rotateToCamera(world);
    const float focal = std::min(width(), height()) * 0.95f * m_zoom;
    const float z = std::max(0.05f, -cam.z());
    const QPointF screen(
        width() * 0.5 + m_pan.x() + cam.x() * focal / (m_orthographicView ? 1.0f : z),
        height() * 0.5 + m_pan.y() - cam.y() * focal / (m_orthographicView ? 1.0f : z)
    );
    return {screen, z, cam};
}

void ViewportWidget::updateBrushSurfacePreview(const QPoint& pos) {
    m_brushSurfaceHit = false;
    m_brushSurfaceCenter = QPointF(pos);
    m_brushSurfaceRadius = m_paintRadius;
    if (!m_scene || m_scene->empty()) return;

    float bestDepth = 1e9f;
    const QPointF cursor(pos);
    for (int meshIndex = 0; meshIndex < m_scene->meshes.size(); ++meshIndex) {
        const auto& mesh = m_scene->meshes[meshIndex];
        if (!mesh.visible) continue;
        if (m_selectedMesh >= 0 && meshIndex != m_selectedMesh) continue;
        for (const auto& f : mesh.faces) {
            if (f.i0 < 0 || f.i1 < 0 || f.i2 < 0 || f.i0 >= mesh.vertices.size() || f.i1 >= mesh.vertices.size() || f.i2 >= mesh.vertices.size()) continue;
            const auto p0 = projectVertex(mesh.vertices[f.i0].position);
            const auto p1 = projectVertex(mesh.vertices[f.i1].position);
            const auto p2 = projectVertex(mesh.vertices[f.i2].position);
            if (p0.camera.z() > -0.02f || p1.camera.z() > -0.02f || p2.camera.z() > -0.02f) continue;
            if (!pointInTriangle(cursor, p0.screen, p1.screen, p2.screen)) continue;
            const float depth = (p0.depth + p1.depth + p2.depth) / 3.0f;
            if (depth < bestDepth) {
                bestDepth = depth;
                m_brushSurfaceHit = true;
                m_brushSurfaceCenter = (p0.screen + p1.screen + p2.screen) / 3.0;
                m_brushSurfaceRadius = m_paintRadius;
                m_lastAffectedMesh = meshIndex;
            }
        }
    }
}

void ViewportWidget::drawViewportBackground(QPainter& p) const {
    QLinearGradient bg(rect().topLeft(), rect().bottomLeft());
    bg.setColorAt(0.0, QColor(22, 27, 34));
    bg.setColorAt(0.55, QColor(30, 35, 42));
    bg.setColorAt(1.0, QColor(18, 22, 28));
    p.fillRect(rect(), bg);
}

void ViewportWidget::drawGroundShadow(QPainter& p, const QVector<DrawTriangle>& triangles) const {
    if (triangles.isEmpty()) return;
    QRectF bounds;
    for (const auto& t : triangles) {
        bounds |= QRectF(t.a, QSizeF(1, 1));
        bounds |= QRectF(t.b, QSizeF(1, 1));
        bounds |= QRectF(t.c, QSizeF(1, 1));
    }
    if (!bounds.isValid()) return;
    const QPointF center(bounds.center().x(), std::min<double>(height() - 90, bounds.bottom() + 26));
    const float rx = std::clamp(float(bounds.width() * 0.36), 42.0f, float(width()) * 0.33f);
    const float ry = std::clamp(float(bounds.height() * 0.075), 12.0f, 52.0f);
    QRadialGradient shadow(center, rx);
    shadow.setColorAt(0.0, QColor(0, 0, 0, 92));
    shadow.setColorAt(0.65, QColor(0, 0, 0, 38));
    shadow.setColorAt(1.0, QColor(0, 0, 0, 0));
    p.save();
    p.setPen(Qt::NoPen);
    p.setBrush(shadow);
    p.drawEllipse(center, rx, ry);
    p.restore();
}

void ViewportWidget::drawAxisGizmo(QPainter& p) const {
    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    const QPointF origin(56, height() - 72);
    struct Axis { QVector3D dir; QColor color; QString label; };
    const QVector<Axis> axes = {
        {QVector3D(1, 0, 0), QColor(235, 92, 92), "X"},
        {QVector3D(0, 1, 0), QColor(94, 210, 118), "Y"},
        {QVector3D(0, 0, 1), QColor(94, 154, 255), "Z"}
    };
    for (const auto& axis : axes) {
        QVector3D cam = rotateDirectionToCamera(axis.dir).normalized();
        QPointF end = origin + QPointF(cam.x() * 34.0f, -cam.y() * 34.0f);
        p.setPen(QPen(axis.color, 3, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(origin, end);
        p.setBrush(axis.color);
        p.setPen(Qt::NoPen);
        p.drawEllipse(end, 4, 4);
        p.setPen(axis.color.lighter(130));
        p.drawText(end + QPointF(6, -4), axis.label);
    }
    p.setBrush(QColor(210, 216, 224));
    p.setPen(Qt::NoPen);
    p.drawEllipse(origin, 4, 4);
    p.restore();
}

QColor ViewportWidget::channelFilteredColor(const QColor& color) const {
    if (m_displayChannelMode == QStringLiteral("仅显示灰度")) {
        const int gray = qGray(color.rgb());
        return QColor(gray, gray, gray, color.alpha());
    }
    if (m_displayChannelMode == QStringLiteral("仅显示A")) {
        return QColor(color.alpha(), color.alpha(), color.alpha(), 255);
    }
    if (m_displayChannelMode == QStringLiteral("仅显示R")) return QColor(color.red(), color.red(), color.red(), color.alpha());
    if (m_displayChannelMode == QStringLiteral("仅显示G")) return QColor(color.green(), color.green(), color.green(), color.alpha());
    if (m_displayChannelMode == QStringLiteral("仅显示B")) return QColor(color.blue(), color.blue(), color.blue(), color.alpha());
    return QColor(
        m_showRedChannel ? color.red() : 0,
        m_showGreenChannel ? color.green() : 0,
        m_showBlueChannel ? color.blue() : 0,
        color.alpha()
    );
}

QColor ViewportWidget::previewColor(const MeshData& mesh, const Vertex& v, int meshIndex) const {
    QColor base = (m_useVertexColor && mesh.hasVertexColor) ? v.color : m_defaultMaterialColor;
    if (m_displayMode == QStringLiteral("实体模式")) base = QColor(176, 184, 194);
    else if (m_displayMode == QStringLiteral("材质模式")) base = m_defaultMaterialColor;
    else if (m_displayMode == QStringLiteral("法线显示")) {
        const QVector3D n = v.normal.lengthSquared() > 0.00001f ? v.normal.normalized() : QVector3D(0, 1, 0);
        base = QColor(int((n.x() * 0.5f + 0.5f) * 255), int((n.y() * 0.5f + 0.5f) * 255), int((n.z() * 0.5f + 0.5f) * 255));
    } else if (m_displayMode == QStringLiteral("UV 显示")) {
        base = QColor(std::clamp(int(v.uv.x() * 255), 0, 255), std::clamp(int(v.uv.y() * 255), 0, 255), 180);
    } else if (m_displayMode == QStringLiteral("曲率显示")) {
        const QVector3D n = v.normal.lengthSquared() > 0.00001f ? v.normal.normalized() : QVector3D(0, 1, 0);
        const int c = std::clamp(int((1.0f - std::abs(n.y())) * 255), 0, 255);
        base = QColor(c, c / 2, 255 - c);
    } else if (m_displayMode == QStringLiteral("过绘制显示")) {
        base = mesh.hasVertexColor ? QColor(std::min(255, v.color.red() + 60), std::max(0, v.color.green() - 30), std::max(0, v.color.blue() - 30)) : QColor(80, 80, 80);
    } else if (m_displayMode == QStringLiteral("烘焙误差显示")) {
        const int diff = std::abs(v.color.red() - v.color.green()) + std::abs(v.color.red() - v.color.blue());
        base = QColor(std::clamp(diff, 0, 255), 40, 255 - std::clamp(diff, 0, 255));
    }
    base = channelFilteredColor(base);
    if (m_useVertexColor && mesh.hasVertexColor) {
        base = QColor(
            int(base.red() * m_layerOpacity + m_defaultMaterialColor.red() * (1.0f - m_layerOpacity)),
            int(base.green() * m_layerOpacity + m_defaultMaterialColor.green() * (1.0f - m_layerOpacity)),
            int(base.blue() * m_layerOpacity + m_defaultMaterialColor.blue() * (1.0f - m_layerOpacity)),
            255
        );
    }

    if (m_realtimePreview && (m_displayMode == QStringLiteral("AO 预览") || m_displayMode == QStringLiteral("AO 贴图预览") || m_displayMode == QStringLiteral("烘焙误差显示"))) {
        QVector3D n = v.normal.lengthSquared() > 0.00001f ? v.normal.normalized() : QVector3D(0, 1, 0);
        const float upward = clamp01((n.y() + 1.0f) * 0.5f);
        const float facing = clamp01(-rotateDirectionToCamera(n).z());
        float cavity = std::pow(1.0f - upward, std::max(0.2f, m_settings.transitionContrast));
        cavity = cavity * m_settings.aoStrength;
        float ao = 1.0f - cavity;
        ao *= 0.72f + facing * 0.28f;
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
    p.drawText(14, 24, QString("%1 | 显示模式：%2    Mesh: %3    Vertices: %4    Triangles: %5    Selected: %6")
        .arg(m_orthographicView ? "正交" : "透视")
        .arg(m_displayMode)
        .arg(m_scene ? m_scene->meshes.size() : 0)
        .arg(m_scene ? m_scene->vertexCount() : 0)
        .arg(m_scene ? m_scene->faceCount() : 0)
        .arg(selected));
    p.drawText(14, height() - 42, "Maya 操作：Alt+左键旋转 / Alt+中键平移 / Alt+右键缩放 / 滚轮缩放 / F 重置视图 / 鼠标左键点选模型");
    p.drawText(14, height() - 20, QString("实时预览：%1    线框：%2    顶点色：%3    绘制：%4    半径：%5    硬度：%6    影响点：%7    FPS：%8")
        .arg(m_realtimePreview ? "开启" : "关闭")
        .arg(m_wireframeVisible ? "开启" : "关闭")
        .arg(m_useVertexColor ? "开启" : "关闭")
        .arg(m_vertexPaintEnabled ? (m_paintEraseMode ? "擦除" : (m_paintMode == 2 ? "填充" : "画笔")) : "关闭")
        .arg(int(m_paintRadius))
        .arg(QString::number(m_paintHardness, 'f', 2))
        .arg(m_lastAffectedVertices)
        .arg(QString::number(m_fps, 'f', 1)));

    drawAxisGizmo(p);

    if (m_vertexPaintEnabled) {
        p.setRenderHint(QPainter::Antialiasing, true);
        const QPoint c = mapFromGlobal(QCursor::pos());
        const QPointF center = m_brushSurfaceHit ? m_brushSurfaceCenter : QPointF(c);
        if (rect().contains(c) || rect().contains(center.toPoint())) {
            const QColor outer = m_paintEraseMode ? QColor(255, 112, 112) : QColor(92, 174, 255);
            p.setBrush(QColor(outer.red(), outer.green(), outer.blue(), m_activeButton == Qt::LeftButton ? 42 : 18));
            p.setPen(QPen(outer, m_brushSurfaceHit ? 2.6 : 1.6));
            p.drawEllipse(center, m_brushSurfaceRadius, m_brushSurfaceRadius * (m_brushSurfaceHit ? 0.42 : 1.0));
            p.setBrush(Qt::NoBrush);
            p.setPen(QPen(QColor(255, 255, 255, 165), 1, Qt::DashLine));
            const float hardRadius = std::max(1.0f, m_brushSurfaceRadius * m_paintHardness);
            p.drawEllipse(center, hardRadius, hardRadius * (m_brushSurfaceHit ? 0.42 : 1.0));
            p.setPen(QPen(QColor(255, 255, 255, 190), 1));
            p.drawLine(QPointF(center.x() - 5, center.y()), QPointF(center.x() + 5, center.y()));
            p.drawLine(QPointF(center.x(), center.y() - 5), QPointF(center.x(), center.y() + 5));
            if (m_activeButton == Qt::LeftButton) {
                p.setPen(QPen(QColor(255, 220, 88, 190), 1));
                p.drawEllipse(center, m_brushSurfaceRadius * 1.16f, m_brushSurfaceRadius * (m_brushSurfaceHit ? 0.50f : 1.16f));
            }
        }
    }
    p.restore();
}

void ViewportWidget::paintGL() {
    const qint64 elapsed = m_frameTimer.isValid() ? m_frameTimer.restart() : 0;
    if (elapsed > 0) {
        m_lastFrameMs = double(elapsed);
        const double instantFps = 1000.0 / std::max(1.0, m_lastFrameMs);
        m_fps = m_fps <= 0.1 ? instantFps : (m_fps * 0.88 + instantFps * 0.12);
    } else {
        m_frameTimer.start();
    }

    QPainter p(this);
    drawViewportBackground(p);
    p.setRenderHint(QPainter::Antialiasing, true);

    if (!m_scene || m_scene->empty()) {
        drawAxisGizmo(p);
        p.setPen(QColor(215, 221, 229));
        p.drawText(rect(), Qt::AlignCenter, "导入 OBJ / FBX / GLB 模型后在此显示 3D 视口");
        return;
    }

    if (m_gridVisible) {
        drawGrid(p);
    }

    QVector<DrawTriangle> triangles;
    triangles.reserve(m_scene->faceCount());

    for (int meshIndex = 0; meshIndex < m_scene->meshes.size(); ++meshIndex) {
        const auto& mesh = m_scene->meshes[meshIndex];
        if (!mesh.visible) continue;
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
            const QPointF brushCenter = (p0.screen + p1.screen + p2.screen) / 3.0;
            const float brushScreenRadius = std::max(2.0f, static_cast<float>((QLineF(p0.screen, p1.screen).length() + QLineF(p1.screen, p2.screen).length() + QLineF(p2.screen, p0.screen).length()) / 9.0));
            triangles.push_back({meshIndex, p0.screen, p1.screen, p2.screen, brushCenter, brushScreenRadius, false, (p0.depth + p1.depth + p2.depth) / 3.0f, color});
        }
    }

    std::sort(triangles.begin(), triangles.end(), [](const DrawTriangle& lhs, const DrawTriangle& rhs){
        return lhs.depth > rhs.depth;
    });

    p.setPen(Qt::NoPen);
    drawGroundShadow(p, triangles);
    for (const auto& t : triangles) {
        p.setBrush(t.color);
        p.drawPolygon(QPolygonF({t.a, t.b, t.c}));
    }

    if (m_vertexPaintEnabled && m_brushSurfaceHit) {
        p.save();
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setPen(QPen(QColor(92, 174, 255, 75), 1));
        p.setBrush(QColor(92, 174, 255, 28));
        for (const auto& t : triangles) {
            const float d = QLineF(t.brushCenter, m_brushSurfaceCenter).length();
            if (d <= m_paintRadius + t.brushRadius) {
                p.drawPolygon(QPolygonF({t.a, t.b, t.c}));
            }
        }
        p.restore();
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

QColor ViewportWidget::blendPaintColor(const QColor& base, const QColor& paint, float alpha) const {
    alpha = std::clamp(alpha * m_layerOpacity, 0.0f, 1.0f);
    const int r = blendChannelValue(base.red(), paint.red(), m_paintBlendMode);
    const int g = blendChannelValue(base.green(), paint.green(), m_paintBlendMode);
    const int b = blendChannelValue(base.blue(), paint.blue(), m_paintBlendMode);
    return QColor(
        std::clamp(int(base.red() * (1.0f - alpha) + r * alpha), 0, 255),
        std::clamp(int(base.green() * (1.0f - alpha) + g * alpha), 0, 255),
        std::clamp(int(base.blue() * (1.0f - alpha) + b * alpha), 0, 255),
        255
    );
}

QColor ViewportWidget::applyLockedChannels(const QColor& base, const QColor& painted) const {
    return QColor(
        m_lockRed ? base.red() : painted.red(),
        m_lockGreen ? base.green() : painted.green(),
        m_lockBlue ? base.blue() : painted.blue(),
        painted.alpha()
    );
}

bool ViewportWidget::isVertexPaintable(const Vertex& vertex) const {
    if (!m_ignoreBackfaces) return true;
    QVector3D n = vertex.normal.lengthSquared() > 0.00001f ? vertex.normal.normalized() : QVector3D(0, 0, 1);
    const QVector3D camN = rotateDirectionToCamera(n);
    return camN.z() < 0.0f;
}

void ViewportWidget::beginStroke() {
    if (m_strokeActive) return;
    m_pendingStroke.clear();
    m_pendingStrokeIndex.clear();
    m_strokeActive = true;
}

void ViewportWidget::recordColorChange(int meshIndex, int vertexIndex, const QColor& oldColor, const QColor& newColor) {
    if (oldColor == newColor) return;
    const quint64 key = changeKey(meshIndex, vertexIndex);
    const auto it = m_pendingStrokeIndex.constFind(key);
    if (it != m_pendingStrokeIndex.constEnd()) {
        m_pendingStroke[it.value()].newColor = newColor;
        return;
    }
    ColorChange change;
    change.meshIndex = meshIndex;
    change.vertexIndex = vertexIndex;
    change.oldColor = oldColor;
    change.newColor = newColor;
    m_pendingStrokeIndex.insert(key, m_pendingStroke.size());
    m_pendingStroke.push_back(change);
}

void ViewportWidget::commitPendingStroke() {
    if (!m_strokeActive) return;
    m_strokeActive = false;
    if (m_pendingStroke.isEmpty()) {
        m_pendingStrokeIndex.clear();
        return;
    }
    pushUndoStep(m_pendingStroke);
    m_pendingStroke.clear();
    m_pendingStrokeIndex.clear();
    emit sceneModified();
}

void ViewportWidget::pushUndoStep(const QVector<ColorChange>& changes) {
    if (changes.isEmpty()) return;
    m_undoStack.push_back(changes);
    m_redoStack.clear();
    emit undoAvailabilityChanged(canUndo(), canRedo());
}

void ViewportWidget::refreshMeshColorFlags(const QVector<int>& meshIndices) {
    if (!m_scene) return;
    for (const int meshIndex : meshIndices) {
        if (meshIndex < 0 || meshIndex >= m_scene->meshes.size()) continue;
        auto& mesh = m_scene->meshes[meshIndex];
        bool hasAnyNonWhite = false;
        for (const auto& vertex : mesh.vertices) {
            if (vertex.color != QColor(255, 255, 255, 255)) {
                hasAnyNonWhite = true;
                break;
            }
        }
        mesh.hasVertexColor = hasAnyNonWhite;
    }
    update();
}

void ViewportWidget::applyColorChanges(const QVector<ColorChange>& changes, bool useNewColor) {
    if (!m_scene) return;
    QVector<int> touchedMeshes;
    touchedMeshes.reserve(changes.size());
    for (const auto& change : changes) {
        if (change.meshIndex < 0 || change.meshIndex >= m_scene->meshes.size()) continue;
        auto& mesh = m_scene->meshes[change.meshIndex];
        if (change.vertexIndex < 0 || change.vertexIndex >= mesh.vertices.size()) continue;
        mesh.vertices[change.vertexIndex].color = useNewColor ? change.newColor : change.oldColor;
        if (!touchedMeshes.contains(change.meshIndex)) {
            touchedMeshes.push_back(change.meshIndex);
        }
    }
    refreshMeshColorFlags(touchedMeshes);
    m_useVertexColor = true;
}

void ViewportWidget::paintVertexAt(const QPoint& pos) {
    if (!m_vertexPaintEnabled || !m_scene || m_scene->empty()) return;

    if (m_paintMode == 2) {
        fillSelectedMesh();
        return;
    }

    beginStroke();
    const int pickedMesh = pickMeshAt(pos);
    if (pickedMesh < 0) return;
    const int targetMesh = m_selectedMesh >= 0 ? m_selectedMesh : pickedMesh;
    if (targetMesh != pickedMesh) return;
    bool changed = false;
    const QColor target = m_paintEraseMode ? QColor(255, 255, 255) : m_paintColor;
    const QVector<QPointF> strokePoints = m_mirrorPainting
        ? QVector<QPointF>{QPointF(pos), QPointF(width() - pos.x(), pos.y())}
        : QVector<QPointF>{QPointF(pos)};

    for (int meshIndex = 0; meshIndex < m_scene->meshes.size(); ++meshIndex) {
        if (targetMesh >= 0 && meshIndex != targetMesh) continue;
        auto& mesh = m_scene->meshes[meshIndex];
        if (!mesh.visible) continue;
        bool meshChanged = false;
        for (int vertexIndex = 0; vertexIndex < mesh.vertices.size(); ++vertexIndex) {
            auto& vertex = mesh.vertices[vertexIndex];
            const auto pv = projectVertex(vertex.position);
            if (pv.camera.z() > -0.02f) continue;
            if (!isVertexPaintable(vertex)) continue;

            float bestDist = std::numeric_limits<float>::max();
            for (const QPointF& strokePoint : strokePoints) {
                const float dist = float(std::hypot(pv.screen.x() - strokePoint.x(), pv.screen.y() - strokePoint.y()));
                bestDist = std::min(bestDist, dist);
            }
            if (bestDist > m_paintRadius) continue;

            float falloff = 1.0f;
            const float t = std::clamp(bestDist / std::max(1.0f, m_paintRadius), 0.0f, 1.0f);
            if (m_paintSoftBrush) {
                const float hard = std::clamp(m_paintHardness, 0.0f, 1.0f);
                if (t <= hard) {
                    falloff = 1.0f;
                } else {
                    const float edgeT = std::clamp((t - hard) / std::max(0.001f, 1.0f - hard), 0.0f, 1.0f);
                    falloff = 1.0f - edgeT * edgeT * (3.0f - 2.0f * edgeT);
                }
            }
            if (m_normalFalloff) {
                const QVector3D n = vertex.normal.lengthSquared() > 0.00001f ? vertex.normal.normalized() : QVector3D(0, 0, 1);
                const float normalFacing = std::clamp(-rotateDirectionToCamera(n).z(), 0.0f, 1.0f);
                falloff *= std::sqrt(normalFacing);
            }
            if (m_pressureSimulation) {
                const float centerPressure = 1.0f - std::clamp(t, 0.0f, 1.0f) * 0.35f;
                falloff *= centerPressure;
            }
            const float alpha = std::clamp(m_paintOpacity * falloff, 0.0f, 1.0f);
            if (alpha <= 0.001f) continue;
            const QColor src = vertex.color;
            const QColor blended = blendPaintColor(src, target, alpha);
            const QColor finalColor = applyLockedChannels(src, blended);
            if (finalColor == src) continue;
            recordColorChange(meshIndex, vertexIndex, src, finalColor);
            vertex.color = finalColor;
            meshChanged = true;
            changed = true;
        }
        if (meshChanged) mesh.hasVertexColor = true;
    }

    if (changed) {
        m_useVertexColor = true;
        m_lastAffectedVertices = m_pendingStroke.size();
        update();
    }
}

void ViewportWidget::fillSelectedMesh() {
    if (!m_scene || m_scene->empty()) return;
    beginStroke();
    const int targetMesh = m_selectedMesh;
    bool changed = false;
    for (int meshIndex = 0; meshIndex < m_scene->meshes.size(); ++meshIndex) {
        if (targetMesh >= 0 && meshIndex != targetMesh) continue;
        auto& mesh = m_scene->meshes[meshIndex];
        if (!mesh.visible) continue;
        bool meshChanged = false;
        for (int vertexIndex = 0; vertexIndex < mesh.vertices.size(); ++vertexIndex) {
            auto& vertex = mesh.vertices[vertexIndex];
            const QColor src = vertex.color;
            const QColor blended = blendPaintColor(src, m_paintColor, m_paintOpacity);
            const QColor finalColor = applyLockedChannels(src, blended);
            if (finalColor == src) continue;
            recordColorChange(meshIndex, vertexIndex, src, finalColor);
            vertex.color = finalColor;
            changed = true;
            meshChanged = true;
        }
        if (meshChanged) mesh.hasVertexColor = true;
    }
    if (changed) {
        m_useVertexColor = true;
        update();
        commitPendingStroke();
    } else {
        m_pendingStroke.clear();
        m_pendingStrokeIndex.clear();
        m_strokeActive = false;
    }
}

void ViewportWidget::clearVertexColors() {
    if (!m_scene || m_scene->empty()) return;
    beginStroke();
    bool changed = false;
    for (int meshIndex = 0; meshIndex < m_scene->meshes.size(); ++meshIndex) {
        auto& mesh = m_scene->meshes[meshIndex];
        for (int vertexIndex = 0; vertexIndex < mesh.vertices.size(); ++vertexIndex) {
            auto& vertex = mesh.vertices[vertexIndex];
            const QColor src = vertex.color;
            const QColor dst(255, 255, 255, 255);
            if (src == dst) continue;
            recordColorChange(meshIndex, vertexIndex, src, dst);
            vertex.color = dst;
            changed = true;
        }
        mesh.hasVertexColor = false;
    }
    if (changed) {
        update();
        commitPendingStroke();
    } else {
        m_pendingStroke.clear();
        m_pendingStrokeIndex.clear();
        m_strokeActive = false;
    }
}

void ViewportWidget::clearCurrentLayer() {
    clearVertexColors();
}

void ViewportWidget::clearSelectedMeshVertexColors() {
    if (!m_scene || m_scene->empty() || m_selectedMesh < 0 || m_selectedMesh >= m_scene->meshes.size()) return;
    beginStroke();
    bool changed = false;
    auto& mesh = m_scene->meshes[m_selectedMesh];
    for (int vertexIndex = 0; vertexIndex < mesh.vertices.size(); ++vertexIndex) {
        auto& vertex = mesh.vertices[vertexIndex];
        const QColor src = vertex.color;
        const QColor dst(255, 255, 255, 255);
        if (src == dst) continue;
        recordColorChange(m_selectedMesh, vertexIndex, src, dst);
        vertex.color = dst;
        changed = true;
    }
    mesh.hasVertexColor = false;
    if (changed) {
        update();
        commitPendingStroke();
    } else {
        m_pendingStroke.clear();
        m_pendingStrokeIndex.clear();
        m_strokeActive = false;
    }
}

void ViewportWidget::clearCurrentChannel() {
    if (!m_scene || m_scene->empty()) return;
    beginStroke();
    bool changed = false;
    const int targetMesh = m_selectedMesh;
    for (int meshIndex = 0; meshIndex < m_scene->meshes.size(); ++meshIndex) {
        if (targetMesh >= 0 && meshIndex != targetMesh) continue;
        auto& mesh = m_scene->meshes[meshIndex];
        for (int vertexIndex = 0; vertexIndex < mesh.vertices.size(); ++vertexIndex) {
            auto& vertex = mesh.vertices[vertexIndex];
            const QColor src = vertex.color;
            QColor dst = src;
            if (m_showRedChannel) dst.setRed(255);
            if (m_showGreenChannel) dst.setGreen(255);
            if (m_showBlueChannel) dst.setBlue(255);
            if (dst == src) continue;
            recordColorChange(meshIndex, vertexIndex, src, dst);
            vertex.color = dst;
            changed = true;
        }
        refreshMeshColorFlags({meshIndex});
    }
    if (changed) {
        update();
        commitPendingStroke();
    } else {
        m_pendingStroke.clear();
        m_pendingStrokeIndex.clear();
        m_strokeActive = false;
    }
}

void ViewportWidget::setMeshVisible(int meshIndex, bool visible) {
    if (!m_scene || meshIndex < 0 || meshIndex >= m_scene->meshes.size()) return;
    m_scene->meshes[meshIndex].visible = visible;
    update();
}

void ViewportWidget::showOnlyMesh(int meshIndex) {
    if (!m_scene || meshIndex < 0 || meshIndex >= m_scene->meshes.size()) return;
    for (int i = 0; i < m_scene->meshes.size(); ++i) m_scene->meshes[i].visible = (i == meshIndex);
    m_selectedMesh = meshIndex;
    update();
}

void ViewportWidget::deleteMesh(int meshIndex) {
    if (!m_scene || meshIndex < 0 || meshIndex >= m_scene->meshes.size()) return;
    m_scene->meshes.removeAt(meshIndex);
    if (m_selectedMesh == meshIndex) m_selectedMesh = -1;
    else if (m_selectedMesh > meshIndex) --m_selectedMesh;
    updateBounds();
    update();
    emit sceneModified();
}

void ViewportWidget::duplicateMesh(int meshIndex) {
    if (!m_scene || meshIndex < 0 || meshIndex >= m_scene->meshes.size()) return;
    MeshData copy = m_scene->meshes[meshIndex];
    copy.name += QStringLiteral("_copy");
    m_scene->meshes.insert(meshIndex + 1, copy);
    m_selectedMesh = meshIndex + 1;
    updateBounds();
    update();
    emit sceneModified();
}

void ViewportWidget::mousePressEvent(QMouseEvent* event) {
    setFocus();
    m_lastMousePos = event->pos();
    m_pressMousePos = event->pos();
    m_activeButton = event->button();
    m_dragged = false;
    if (m_vertexPaintEnabled && event->button() == Qt::LeftButton && !(event->modifiers() & Qt::AltModifier)) {
        updateBrushSurfacePreview(event->pos());
        beginStroke();
        paintVertexAt(event->pos());
        m_dragged = true;
    }
    event->accept();
}

void ViewportWidget::mouseMoveEvent(QMouseEvent* event) {
    const QPoint delta = event->pos() - m_lastMousePos;
    if (delta.manhattanLength() > 1) {
        m_dragged = true;
    }

    const bool mayaNav = event->modifiers() & Qt::AltModifier;
    if (m_vertexPaintEnabled && !mayaNav) {
        updateBrushSurfacePreview(event->pos());
        update();
    }
    if (m_vertexPaintEnabled && (event->buttons() & Qt::LeftButton) && !mayaNav) {
        paintVertexAt(event->pos());
        m_lastMousePos = event->pos();
        event->accept();
        return;
    }
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
    if (m_vertexPaintEnabled && event->button() == Qt::LeftButton && !(event->modifiers() & Qt::AltModifier)) {
        m_lastAffectedVertices = m_pendingStroke.size();
        commitPendingStroke();
    }
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
    if (event->matches(QKeySequence::Undo)) {
        undo();
        event->accept();
        return;
    }
    if (event->matches(QKeySequence::Redo)) {
        redo();
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_F) {
        resetCamera();
        update();
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_B && event->modifiers() == Qt::NoModifier) {
        toggleVertexPaintEnabled();
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_E && event->modifiers() == Qt::NoModifier) {
        togglePaintEraseMode();
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_G && event->modifiers() == Qt::NoModifier) {
        toggleGridVisible();
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_W && event->modifiers() == Qt::NoModifier) {
        toggleWireframeVisible();
        event->accept();
        return;
    }
    if (event->key() >= Qt::Key_1 && event->key() <= Qt::Key_4 && event->modifiers() == Qt::NoModifier) {
        setVisibleChannelPreset(event->key() - Qt::Key_1);
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_Q && event->modifiers() == Qt::NoModifier) {
        restoreSelectionMode();
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_Space && event->modifiers() == Qt::NoModifier) {
        toggleRealtimePreview();
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
        if (!mesh.visible) continue;
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
