#pragma once

#include <QOpenGLFunctions_4_3_Core>
#include <QOpenGLShaderProgram>
#include <QVector>
#include <QVector3D>
#include <QVector2D>
#include <QPointF>
#include <memory>

// GPU viewport transform pipeline. Input records are 8 floats: xyz1 + rgba.
// OpenGL 4.3 Compute Shader transforms vertices into clip space; rasterization is performed by the GPU.
class GpuViewportRenderer {
public:
    GpuViewportRenderer() = default;
    ~GpuViewportRenderer();
    bool initialize(QString* error = nullptr);
    bool isAvailable() const { return m_ready; }
    QString status() const { return m_status; }
    bool render(const QVector<float>& positions, const QVector<float>& colors, bool geometryChanged,
                float yawDegrees, float pitchDegrees,
                const QVector3D& center, float distance, float zoom,
                const QPointF& pan, bool orthographic, int width, int height,
                bool wireframe);
    void release();
private:
    QOpenGLFunctions_4_3_Core* m_gl = nullptr;
    std::unique_ptr<QOpenGLShaderProgram> m_compute;
    std::unique_ptr<QOpenGLShaderProgram> m_draw;
    unsigned int m_positionBuffer = 0;
    unsigned int m_colorBuffer = 0;
    unsigned int m_outputBuffer = 0;
    unsigned int m_vertexArray = 0;
    int m_cachedVertexCount = -1;
    bool m_ready = false;
    QString m_status = QStringLiteral("GPU Compute 未初始化");
};
