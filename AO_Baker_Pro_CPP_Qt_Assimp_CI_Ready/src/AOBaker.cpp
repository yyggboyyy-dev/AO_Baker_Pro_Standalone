#include "AOBaker.h"
#include <QtMath>
#include <algorithm>

AOBaker::AOBaker(QObject* parent) : QObject(parent) {}

QVector<QVector3D> AOBaker::hemisphereSamples(int count) const {
    QVector<QVector3D> out;
    count = std::max(16, count);
    out.reserve(count);
    const float golden = float(M_PI) * (3.0f - std::sqrt(5.0f));
    for (int i = 0; i < count; ++i) {
        const float z = (i + 0.5f) / float(count);
        const float r = std::sqrt(std::max(0.0f, 1.0f - z * z));
        const float theta = i * golden;
        out.push_back(QVector3D(std::cos(theta) * r, std::sin(theta) * r, z));
    }
    return out;
}

QVector<AOBaker::Triangle> AOBaker::collectTriangles(const SceneData& scene) const {
    QVector<Triangle> tris;
    tris.reserve(scene.faceCount());
    for (const auto& mesh : scene.meshes) {
        for (const auto& face : mesh.faces) {
            Triangle t;
            t.a = mesh.vertices[face.i0].position;
            t.b = mesh.vertices[face.i1].position;
            t.c = mesh.vertices[face.i2].position;
            tris.push_back(t);
        }
    }
    return tris;
}

bool AOBaker::rayTriangle(const QVector3D& origin, const QVector3D& dir, const Triangle& tri, float maxDistance) const {
    const float eps = 1e-6f;
    const QVector3D e1 = tri.b - tri.a;
    const QVector3D e2 = tri.c - tri.a;
    const QVector3D p = QVector3D::crossProduct(dir, e2);
    const float det = QVector3D::dotProduct(e1, p);
    if (std::abs(det) < eps) return false;
    const float invDet = 1.0f / det;
    const QVector3D tv = origin - tri.a;
    const float u = QVector3D::dotProduct(tv, p) * invDet;
    if (u < 0.0f || u > 1.0f) return false;
    const QVector3D q = QVector3D::crossProduct(tv, e1);
    const float v = QVector3D::dotProduct(dir, q) * invDet;
    if (v < 0.0f || u + v > 1.0f) return false;
    const float t = QVector3D::dotProduct(e2, q) * invDet;
    return t > 1e-4f && t <= maxDistance;
}

QVector3D AOBaker::sampleToWorld(const QVector3D& sample, const QVector3D& normal) const {
    QVector3D n = normal.normalized();
    if (n.lengthSquared() < 1e-6f) n = QVector3D(0, 0, 1);
    QVector3D helper = std::abs(QVector3D::dotProduct(n, QVector3D(0, 0, 1))) > 0.95f ? QVector3D(1, 0, 0) : QVector3D(0, 0, 1);
    QVector3D tangent = QVector3D::crossProduct(helper, n).normalized();
    QVector3D bitangent = QVector3D::crossProduct(n, tangent).normalized();
    return (tangent * sample.x() + bitangent * sample.y() + n * sample.z()).normalized();
}

float AOBaker::remapAO(float value, const BakeSettings& settings) const {
    float v = std::clamp(value, 0.0f, 1.0f);
    v = std::pow(v, 1.0f / std::max(settings.transitionContrast, 0.2f));
    const float s = std::clamp(settings.transitionSmoothness, 0.0f, 1.0f);
    const float smooth = v * v * (3.0f - 2.0f * v);
    v = v * (1.0f - s) + smooth * s;
    v = settings.darkest + (settings.brightest - settings.darkest) * v;
    return std::clamp(v * settings.aoStrength, 0.0f, 1.0f);
}

void AOBaker::bakeToVertexColor(SceneData& scene, const BakeSettings& settings) {
    const auto tris = collectTriangles(scene);
    const auto samples = hemisphereSamples(settings.sampleCount);
    const int total = std::max(1, scene.vertexCount());
    int done = 0;

    const float groundLuma = settings.groundBounceColor.redF() * 0.2126f + settings.groundBounceColor.greenF() * 0.7152f + settings.groundBounceColor.blueF() * 0.0722f;

    for (auto& mesh : scene.meshes) {
        float minZ = std::numeric_limits<float>::max();
        float maxZ = std::numeric_limits<float>::lowest();
        for (const auto& v : mesh.vertices) {
            minZ = std::min(minZ, v.position.z());
            maxZ = std::max(maxZ, v.position.z());
        }
        const float zSpan = std::max(1e-5f, maxZ - minZ);

        for (auto& vertex : mesh.vertices) {
            int hitCount = 0;
            const QVector3D origin = vertex.position + vertex.normal.normalized() * 0.001f;
            for (const auto& s : samples) {
                const QVector3D dir = sampleToWorld(s, vertex.normal);
                bool hit = false;
                for (const auto& tri : tris) {
                    if (rayTriangle(origin, dir, tri, settings.rayDistance)) {
                        hit = true;
                        break;
                    }
                }
                if (hit) ++hitCount;
            }
            float visible = 1.0f - float(hitCount) / float(samples.size());
            const float height = (vertex.position.z() - minZ) / zSpan;
            visible = std::min(1.0f, visible + groundLuma * settings.groundBounceStrength * std::max(0.0f, 1.0f - height));
            const float ao = remapAO(visible, settings);
            vertex.color = QColor::fromRgbF(ao, ao, ao, 1.0f);
            ++done;
            if (done % 32 == 0 || done == total) {
                emit progress(QStringLiteral("AO 顶点采样：%1/%2").arg(done).arg(total), int(done * 100.0 / total));
            }
        }
        mesh.hasVertexColor = true;
    }
}
