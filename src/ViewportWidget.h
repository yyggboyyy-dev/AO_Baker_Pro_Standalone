#pragma once

#include "ModelData.h"
#include <QHash>
#include <QJsonObject>
#include <QOpenGLWidget>
#include <QPoint>
#include <QPointF>
#include <QVector>

class ViewportWidget : public QOpenGLWidget {
    Q_OBJECT
public:
    explicit ViewportWidget(QWidget* parent = nullptr);

    void setScene(SceneData* scene);
    void setBakeSettings(const BakeSettings& settings);
    void setRealtimePreviewEnabled(bool enabled);
    void setCameraView(const QString& viewName);
    void focusModel();
    void setWireframeVisible(bool visible);
    void setDefaultMaterialColor(const QColor& color);
    void setUseVertexColor(bool enabled);
    void setVertexPaintEnabled(bool enabled);
    void setPaintEraseMode(bool enabled);
    void setPaintMode(int mode);
    void setPaintBlendMode(const QString& mode);
    void setPaintColor(const QColor& color);
    void setPaintOpacity(float opacity);
    void setPaintRadius(float radius);
    void setPaintSoftBrush(bool enabled);
    void setLayerOpacity(float opacity);
    void setVisibleChannels(bool red, bool green, bool blue);
    void setIgnoreBackfaces(bool enabled);
    void setLockedChannels(bool lockRed, bool lockGreen, bool lockBlue);
    void fillSelectedMesh();
    void clearVertexColors();
    void setSelectedMesh(int meshIndex);
    int selectedMesh() const { return m_selectedMesh; }

    bool canUndo() const { return !m_undoStack.isEmpty(); }
    bool canRedo() const { return !m_redoStack.isEmpty(); }
    void undo();
    void redo();

    QJsonObject saveViewState() const;
    void restoreViewState(const QJsonObject& state);

signals:
    void meshSelected(int meshIndex, const QString& meshName);
    void sceneModified();
    void undoAvailabilityChanged(bool canUndo, bool canRedo);

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

    struct ColorChange {
        int meshIndex = -1;
        int vertexIndex = -1;
        QColor oldColor;
        QColor newColor;
    };

    SceneData* m_scene = nullptr;
    BakeSettings m_settings;
    bool m_realtimePreview = true;
    bool m_wireframeVisible = true;
    bool m_useVertexColor = true;
    bool m_vertexPaintEnabled = false;
    bool m_paintEraseMode = false;
    bool m_paintSoftBrush = true;
    bool m_ignoreBackfaces = true;
    bool m_lockRed = false;
    bool m_lockGreen = false;
    bool m_lockBlue = false;
    int m_paintMode = 0;
    bool m_showRedChannel = true;
    bool m_showGreenChannel = true;
    bool m_showBlueChannel = true;
    float m_paintOpacity = 1.0f;
    float m_paintRadius = 24.0f;
    float m_layerOpacity = 1.0f;
    QColor m_defaultMaterialColor = QColor(188, 198, 210);
    QColor m_paintColor = QColor(255, 255, 255);
    QString m_paintBlendMode = QStringLiteral("正常");
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

    QVector<QVector<ColorChange>> m_undoStack;
    QVector<QVector<ColorChange>> m_redoStack;
    QVector<ColorChange> m_pendingStroke;
    QHash<quint64, int> m_pendingStrokeIndex;
    bool m_strokeActive = false;

    QVector3D m_center = QVector3D(0, 0, 0);
    float m_radius = 1.0f;

    void resetCamera();
    void updateBounds();
    QVector3D rotateToCamera(const QVector3D& world) const;
    QVector3D rotateDirectionToCamera(const QVector3D& dir) const;
    ProjectedVertex projectVertex(const QVector3D& world) const;
    QColor previewColor(const MeshData& mesh, const Vertex& v, int meshIndex) const;
    void drawGrid(QPainter& p) const;
    void drawOverlay(QPainter& p) const;
    int pickMeshAt(const QPoint& pos) const;
    void paintVertexAt(const QPoint& pos);
    QColor channelFilteredColor(const QColor& color) const;
    QColor blendPaintColor(const QColor& base, const QColor& paint, float alpha) const;
    QColor applyLockedChannels(const QColor& base, const QColor& painted) const;
    bool isVertexPaintable(const Vertex& vertex) const;
    void beginStroke();
    void recordColorChange(int meshIndex, int vertexIndex, const QColor& oldColor, const QColor& newColor);
    void commitPendingStroke();
    void pushUndoStep(const QVector<ColorChange>& changes);
    void applyColorChanges(const QVector<ColorChange>& changes, bool useNewColor);
    void refreshMeshColorFlags(const QVector<int>& meshIndices);
    static bool pointInTriangle(const QPointF& p, const QPointF& a, const QPointF& b, const QPointF& c);
};
