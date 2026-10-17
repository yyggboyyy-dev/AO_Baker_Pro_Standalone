#include "GpuViewportRenderer.h"
#include <QOpenGLContext>
#include <QOpenGLVersionFunctionsFactory>
#include <QSurfaceFormat>
#include <QDebug>
#include <cmath>

GpuViewportRenderer::~GpuViewportRenderer() { release(); }

bool GpuViewportRenderer::initialize(QString* error) {
    release();
    QOpenGLContext* context = QOpenGLContext::currentContext();
    if (!context) {
        m_status = QStringLiteral("GPU Compute 不可用：没有当前 OpenGL 上下文");
        if (error) *error = m_status;
        return false;
    }
    const QSurfaceFormat f = context->format();
    if (f.renderableType() != QSurfaceFormat::OpenGL || f.majorVersion() < 4 || (f.majorVersion() == 4 && f.minorVersion() < 3)) {
        m_status = QStringLiteral("GPU Compute 不可用：需要桌面 OpenGL 4.3+，当前为 %1.%2").arg(f.majorVersion()).arg(f.minorVersion());
        if (error) *error = m_status;
        return false;
    }
    m_gl = QOpenGLVersionFunctionsFactory::get<QOpenGLFunctions_4_3_Core>(context);
    if (!m_gl || !m_gl->initializeOpenGLFunctions()) {
        m_status = QStringLiteral("GPU Compute 不可用：OpenGL 扩展函数初始化失败");
        if (error) *error = m_status;
        return false;
    }

    m_compute = std::make_unique<QOpenGLShaderProgram>();
    const char* computeSource = R"GLSL(#version 430 core
layout(local_size_x = 128) in;
struct VertexOut { vec4 clipPosition; vec4 color; };
layout(std430, binding = 0) readonly buffer PositionBuffer { vec4 positions[]; };
layout(std430, binding = 1) writeonly buffer OutputBuffer { VertexOut transformed[]; };
layout(std430, binding = 2) readonly buffer ColorBuffer { vec4 colors[]; };
uniform float uYaw;
uniform float uPitch;
uniform vec3 uCenter;
uniform float uDistance;
uniform float uZoom;
uniform vec2 uPan;
uniform vec2 uViewport;
uniform int uOrtho;
void main() {
    uint i = gl_GlobalInvocationID.x;
    if (i >= positions.length()) return;
    vec3 q = positions[i].xyz - uCenter;
    float cy = cos(uYaw), sy = sin(uYaw);
    vec3 yRot = vec3(q.x * cy + q.z * sy, q.y, -q.x * sy + q.z * cy);
    float cp = cos(uPitch), sp = sin(uPitch);
    vec3 cam = vec3(yRot.x, yRot.y * cp - yRot.z * sp, yRot.y * sp + yRot.z * cp);
    cam.z -= uDistance;
    float depth = max(0.05, -cam.z);
    float focal = min(uViewport.x, uViewport.y) * 0.95 * uZoom;
    float divisor = (uOrtho != 0) ? 1.0 : depth;
    vec2 screen = vec2(uViewport.x * 0.5 + uPan.x + cam.x * focal / divisor,
                       uViewport.y * 0.5 + uPan.y - cam.y * focal / divisor);
    vec2 ndc = vec2(2.0 * screen.x / uViewport.x - 1.0, 1.0 - 2.0 * screen.y / uViewport.y);
    float farPlane = max(100.0, uDistance * 100.0);
    float zNdc = clamp((depth - 0.05) / (farPlane - 0.05), 0.0, 1.0) * 2.0 - 1.0;
    if (cam.z > -0.02) ndc = vec2(3.0, 3.0);
    transformed[i].clipPosition = vec4(ndc, zNdc, 1.0);
    transformed[i].color = colors[i];
}
)GLSL";
    if (!m_compute->addShaderFromSourceCode(QOpenGLShader::Compute, computeSource) || !m_compute->link()) {
        m_status = QStringLiteral("GPU Compute Shader 编译失败：%1").arg(m_compute->log());
        if (error) *error = m_status;
        m_compute.reset();
        return false;
    }
    m_draw = std::make_unique<QOpenGLShaderProgram>();
    const char* vertexSource = R"GLSL(#version 430 core
struct VertexOut { vec4 clipPosition; vec4 color; };
layout(std430, binding = 1) readonly buffer OutputBuffer { VertexOut transformed[]; };
out vec4 vColor;
void main() { gl_Position = transformed[gl_VertexID].clipPosition; vColor = transformed[gl_VertexID].color; }
)GLSL";
    const char* fragmentSource = R"GLSL(#version 430 core
in vec4 vColor;
uniform int uWirePass;
out vec4 fragColor;
void main() { fragColor = (uWirePass != 0) ? vec4(0.28, 0.32, 0.38, 0.95) : vColor; }
)GLSL";
    if (!m_draw->addShaderFromSourceCode(QOpenGLShader::Vertex, vertexSource) ||
        !m_draw->addShaderFromSourceCode(QOpenGLShader::Fragment, fragmentSource) || !m_draw->link()) {
        m_status = QStringLiteral("GPU 视口着色器编译失败：%1").arg(m_draw->log());
        if (error) *error = m_status;
        m_compute.reset(); m_draw.reset();
        return false;
    }
    m_gl->glGenBuffers(1, &m_positionBuffer);
    m_gl->glGenBuffers(1, &m_colorBuffer);
    m_gl->glGenBuffers(1, &m_outputBuffer);
    m_ready = m_positionBuffer != 0 && m_colorBuffer != 0 && m_outputBuffer != 0;
    m_status = m_ready ? QStringLiteral("GPU Compute 已启用（OpenGL %1.%2）").arg(f.majorVersion()).arg(f.minorVersion()) : QStringLiteral("GPU Compute 缓冲区创建失败");
    if (error) *error = m_status;
    return m_ready;
}

bool GpuViewportRenderer::render(const QVector<float>& positions, const QVector<float>& colors, bool geometryChanged,
    float yawDegrees, float pitchDegrees, const QVector3D& center, float distance, float zoom,
    const QPointF& pan, bool orthographic, int width, int height, bool wireframe) {
    if (!m_ready || !m_gl || positions.isEmpty() || colors.size() != positions.size() || positions.size() % 4 != 0 || width <= 0 || height <= 0) return false;
    const int vertexCount = positions.size() / 4;
    const size_t positionBytes = size_t(positions.size()) * sizeof(float);
    const size_t colorBytes = size_t(colors.size()) * sizeof(float);
    const size_t outputBytes = size_t(vertexCount) * 8 * sizeof(float);
    m_gl->glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_positionBuffer);
    if (geometryChanged || m_cachedVertexCount != vertexCount) {
        m_gl->glBufferData(GL_SHADER_STORAGE_BUFFER, GLsizeiptr(positionBytes), positions.constData(), GL_STATIC_DRAW);
        m_cachedVertexCount = vertexCount;
    }
    m_gl->glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, m_positionBuffer);
    m_gl->glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_colorBuffer);
    m_gl->glBufferData(GL_SHADER_STORAGE_BUFFER, GLsizeiptr(colorBytes), colors.constData(), GL_STREAM_DRAW);
    m_gl->glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, m_colorBuffer);
    m_gl->glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_outputBuffer);
    m_gl->glBufferData(GL_SHADER_STORAGE_BUFFER, GLsizeiptr(outputBytes), nullptr, GL_STREAM_DRAW);
    m_gl->glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, m_outputBuffer);

    m_compute->bind();
    constexpr float pi = 3.14159265358979323846f;
    m_compute->setUniformValue("uYaw", yawDegrees * pi / 180.0f);
    m_compute->setUniformValue("uPitch", pitchDegrees * pi / 180.0f);
    m_compute->setUniformValue("uCenter", center);
    m_compute->setUniformValue("uDistance", distance);
    m_compute->setUniformValue("uZoom", zoom);
    m_compute->setUniformValue("uPan", QVector2D(float(pan.x()), float(pan.y())));
    m_compute->setUniformValue("uViewport", QVector2D(float(width), float(height)));
    m_compute->setUniformValue("uOrtho", orthographic ? 1 : 0);
    m_gl->glDispatchCompute(GLuint((vertexCount + 127) / 128), 1, 1);
    m_gl->glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_VERTEX_ATTRIB_ARRAY_BARRIER_BIT);
    m_compute->release();

    m_gl->glViewport(0, 0, width, height);
    m_gl->glEnable(GL_DEPTH_TEST);
    m_gl->glDepthFunc(GL_LEQUAL);
    m_gl->glEnable(GL_BLEND);
    m_gl->glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    m_draw->bind();
    m_draw->setUniformValue("uWirePass", 0);
    m_gl->glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    m_gl->glDrawArrays(GL_TRIANGLES, 0, vertexCount);
    if (wireframe) {
        m_draw->setUniformValue("uWirePass", 1);
        m_gl->glEnable(GL_POLYGON_OFFSET_LINE);
        m_gl->glPolygonOffset(-1.0f, -1.0f);
        m_gl->glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
        m_gl->glLineWidth(1.0f);
        m_gl->glDrawArrays(GL_TRIANGLES, 0, vertexCount);
        m_gl->glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
        m_gl->glDisable(GL_POLYGON_OFFSET_LINE);
    }
    m_draw->release();
    m_gl->glDisable(GL_DEPTH_TEST);
    m_gl->glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
    return true;
}

void GpuViewportRenderer::release() {
    if (m_gl && QOpenGLContext::currentContext()) {
        if (m_positionBuffer) m_gl->glDeleteBuffers(1, &m_positionBuffer);
        if (m_colorBuffer) m_gl->glDeleteBuffers(1, &m_colorBuffer);
        if (m_outputBuffer) m_gl->glDeleteBuffers(1, &m_outputBuffer);
    }
    m_positionBuffer = m_colorBuffer = m_outputBuffer = 0;
    m_cachedVertexCount = -1;
    m_ready = false;
    m_compute.reset();
    m_draw.reset();
    m_gl = nullptr;
}
