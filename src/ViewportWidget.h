#pragma once

#include "ModelData.h"
#include <QOpenGLWidget>
#include <QPoint>
#include <QPointF>
#include <QVector>

class ViewportWidget : public QOpenGLWidget {
    Q_OBJECT
public:
    explicit ViewportWidget(QWidget* parent = nullptr);

    void setScene(const SceneData* scene);
    void setBakeSettings(const BakeSettings& settings);
    void setRealtimePreviewEnabled(bool enabled);
    void setSelectedMesh(int meshIndex);
    int selectedMesh() const { return m_selectedMesh; }

signals:
    void meshSelected(int meshIndex, const QString& meshName);

protected:
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    struct ProjectedVertex {
        QPointF screen;
        float depth = 0.0f;
        QVector3D camera;
    };

    struct DrawTriangle {
        int meshIndex = -1;
        QPointF a;
        QPointF b;
        QPointF c;
        float depth = 0.0f;
        QColor color;
    };

    const SceneData* m_scene = nullptr;
    BakeSettings m_settings;
    bool m_realtimePreview = true;
    int m_selectedMesh = -1;

    float m_yaw = -35.0f;
    float m_pitch = 22.0f;
    float m_distance = 1.0f;
    float m_zoom = 1.0f;
    QPointF m_pan = QPointF(0.0, 0.0);
    QPoint m_lastMousePos;
    QPoint m_pressMousePos;
    Qt::MouseButton m_activeButton = Qt::NoButton;
    bool m_dragged = false;

    QVector3D m_center = QVector3D(0, 0, 0);
    float m_radius = 1.0f;

    void resetCamera();
    void updateBounds();
    QVector3D rotateToCamera(const QVector3D& world) const;
    ProjectedVertex projectVertex(const QVector3D& world) const;
    QColor previewColor(const MeshData& mesh, const Vertex& v, int meshIndex) const;
    void drawGrid(QPainter& p) const;
    void drawOverlay(QPainter& p) const;
    int pickMeshAt(const QPoint& pos) const;
    static bool pointInTriangle(const QPointF& p, const QPointF& a, const QPointF& b, const QPointF& c);
};
