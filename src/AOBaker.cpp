#include "AOBaker.h"

#include <QtMath>
#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>

namespace {

struct FastTriangle {
    QVector3D a;
    QVector3D b;
    QVector3D c;
    QVector3D centroid;
    QVector3D bmin;
    QVector3D bmax;
};

struct BvhNode {
    QVector3D bmin;
    QVector3D bmax;
    int left = -1;
    int right = -1;
    int start = 0;
    int count = 0;
};

float axisValue(const QVector3D& v, int axis) {
    if (axis == 0) return v.x();
    if (axis == 1) return v.y();
    return v.z();
}

QVector3D minVec(const QVector3D& a, const QVector3D& b) {
    return QVector3D(std::min(a.x(), b.x()), std::min(a.y(), b.y()), std::min(a.z(), b.z()));
}

QVector3D maxVec(const QVector3D& a, const QVector3D& b) {
    return QVector3D(std::max(a.x(), b.x()), std::max(a.y(), b.y()), std::max(a.z(), b.z()));
}

QVector<FastTriangle> collectFastTriangles(const SceneData& scene) {
    QVector<FastTriangle> tris;
    tris.reserve(scene.faceCount());

    for (const auto& mesh : scene.meshes) {
        for (const auto& face : mesh.faces) {
            if (face.i0 < 0 || face.i1 < 0 || face.i2 < 0 ||
                face.i0 >= mesh.vertices.size() || face.i1 >= mesh.vertices.size() || face.i2 >= mesh.vertices.size()) {
                continue;
            }

            FastTriangle tri;
            tri.a = mesh.vertices[face.i0].position;
            tri.b = mesh.vertices[face.i1].position;
            tri.c = mesh.vertices[face.i2].position;
            tri.centroid = (tri.a + tri.b + tri.c) / 3.0f;
            tri.bmin = minVec(minVec(tri.a, tri.b), tri.c);
            tri.bmax = maxVec(maxVec(tri.a, tri.b), tri.c);
            tris.push_back(tri);
        }
    }

    return tris;
}

class BvhTree {
public:
    explicit BvhTree(const QVector<FastTriangle>& triangles) : m_triangles(triangles) {
        m_indices.resize(m_triangles.size());
        std::iota(m_indices.begin(), m_indices.end(), 0);
        if (!m_indices.isEmpty()) {
            m_nodes.reserve(std::max(1, m_indices.size() * 2));
            buildNode(0, m_indices.size());
        }
    }

    bool intersects(const QVector3D& origin, const QVector3D& dir, float maxDistance) const {
        if (m_nodes.isEmpty()) return false;

        int stack[96];
        int stackSize = 0;
        stack[stackSize++] = 0;
        float closest = maxDistance;

        while (stackSize > 0) {
            const int nodeIndex = stack[--stackSize];
            const BvhNode& node = m_nodes[nodeIndex];
            if (!rayAabb(origin, dir, node.bmin, node.bmax, closest)) {
                continue;
            }

            if (node.left < 0 && node.right < 0) {
                for (int i = 0; i < node.count; ++i) {
                    const FastTriangle& tri = m_triangles[m_indices[node.start + i]];
                    float t = closest;
                    if (rayTriangle(origin, dir, tri, t)) {
                        return true;
                    }
                }
            } else {
                if (node.left >= 0 && stackSize < 96) stack[stackSize++] = node.left;
                if (node.right >= 0 && stackSize < 96) stack[stackSize++] = node.right;
            }
        }

        return false;
    }

private:
    int buildNode(int start, int count) {
        BvhNode node;
        node.start = start;
        node.count = count;
        node.bmin = QVector3D(std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max());
        node.bmax = QVector3D(std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest());

        QVector3D cmin(std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max());
        QVector3D cmax(std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest());

        for (int i = 0; i < count; ++i) {
            const FastTriangle& tri = m_triangles[m_indices[start + i]];
            node.bmin = minVec(node.bmin, tri.bmin);
            node.bmax = maxVec(node.bmax, tri.bmax);
            cmin = minVec(cmin, tri.centroid);
            cmax = maxVec(cmax, tri.centroid);
        }

        const int thisIndex = m_nodes.size();
        m_nodes.push_back(node);

        constexpr int leafSize = 8;
        if (count <= leafSize) {
            return thisIndex;
        }

        const QVector3D extent = cmax - cmin;
        int axis = 0;
        if (extent.y() > extent.x() && extent.y() >= extent.z()) axis = 1;
        else if (extent.z() > extent.x() && extent.z() >= extent.y()) axis = 2;

        if (axisValue(extent, axis) < 1e-6f) {
            return thisIndex;
        }

        const int mid = start + count / 2;
        std::nth_element(m_indices.begin() + start, m_indices.begin() + mid, m_indices.begin() + start + count,
            [this, axis](int lhs, int rhs) {
                return axisValue(m_triangles[lhs].centroid, axis) < axisValue(m_triangles[rhs].centroid, axis);
            });

        m_nodes[thisIndex].left = buildNode(start, mid - start);
        m_nodes[thisIndex].right = buildNode(mid, start + count - mid);
        return thisIndex;
    }

    static bool rayAabb(const QVector3D& origin, const QVector3D& dir, const QVector3D& bmin, const QVector3D& bmax, float maxDistance) {
        float tMin = 0.0f;
        float tMax = maxDistance;

        for (int axis = 0; axis < 3; ++axis) {
            const float o = axisValue(origin, axis);
            const float d = axisValue(dir, axis);
            const float mn = axisValue(bmin, axis) - 1e-4f;
            const float mx = axisValue(bmax, axis) + 1e-4f;

            if (std::abs(d) < 1e-8f) {
                if (o < mn || o > mx) return false;
                continue;
            }

            const float invD = 1.0f / d;
            float t0 = (mn - o) * invD;
            float t1 = (mx - o) * invD;
            if (t0 > t1) std::swap(t0, t1);
            tMin = std::max(tMin, t0);
            tMax = std::min(tMax, t1);
            if (tMax < tMin) return false;
        }

        return tMax >= 0.0f && tMin <= maxDistance;
    }

    static bool rayTriangle(const QVector3D& origin, const QVector3D& dir, const FastTriangle& tri, float& closest) {
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
        if (t > 1e-4f && t < closest) {
            closest = t;
            return true;
        }
        return false;
    }

    const QVector<FastTriangle>& m_triangles;
    QVector<int> m_indices;
    QVector<BvhNode> m_nodes;
};

} // namespace

AOBaker::AOBaker(QObject* parent) : QObject(parent) {}

QVector<QVector3D> AOBaker::hemisphereSamples(int count) const {
    QVector<QVector3D> out;
    count = std::max(16, count);
    out.reserve(count);

    const float golden = float(M_PI) * (3.0f - std::sqrt(5.0f));
    for (int i = 0; i < count; ++i) {
        // Cosine-weighted Fibonacci hemisphere: faster convergence for vertex AO than purely uniform rays.
        const float u = (i + 0.5f) / float(count);
        const float z = std::sqrt(1.0f - u);
        const float r = std::sqrt(std::max(0.0f, 1.0f - z * z));
        const float theta = i * golden;
        out.push_back(QVector3D(std::cos(theta) * r, std::sin(theta) * r, z));
    }
    return out;
}

QVector3D AOBaker::sampleToWorld(const QVector3D& sample, const QVector3D& normal) const {
    QVector3D n = normal.normalized();
    if (n.lengthSquared() < 1e-6f) n = QVector3D(0, 0, 1);
    const QVector3D helper = std::abs(QVector3D::dotProduct(n, QVector3D(0, 0, 1))) > 0.95f ? QVector3D(1, 0, 0) : QVector3D(0, 0, 1);
    const QVector3D tangent = QVector3D::crossProduct(helper, n).normalized();
    const QVector3D bitangent = QVector3D::crossProduct(n, tangent).normalized();
    return (tangent * sample.x() + bitangent * sample.y() + n * sample.z()).normalized();
}

float AOBaker::remapAO(float value, const BakeSettings& settings) const {
    float v = std::clamp(value, 0.0f, 1.0f);
    v = std::pow(v, 1.0f / std::max(settings.transitionContrast, 0.2f));

    const float s = std::clamp(settings.transitionSmoothness, 0.0f, 1.0f);
    const float smooth = v * v * (3.0f - 2.0f * v);
    v = v * (1.0f - s) + smooth * s;
    v = settings.darkest + (settings.brightest - settings.darkest) * v;

    // Similar to Faogen's exposed overbright/gamma idea: AO strength scales final visibility.
    return std::clamp(v * settings.aoStrength, 0.0f, 1.0f);
}

void AOBaker::bakeToVertexColor(SceneData& scene, const BakeSettings& settings) {
    const QVector<FastTriangle> tris = collectFastTriangles(scene);
    const BvhTree bvh(tris);
    const QVector<QVector3D> samples = hemisphereSamples(settings.sampleCount);
    const int total = std::max(1, scene.vertexCount());
    int done = 0;

    emit progress(QStringLiteral("正在构建 AO 加速结构：%1 个三角面").arg(tris.size()), 1);

    const float groundLuma = settings.groundBounceColor.redF() * 0.2126f +
                             settings.groundBounceColor.greenF() * 0.7152f +
                             settings.groundBounceColor.blueF() * 0.0722f;

    for (auto& mesh : scene.meshes) {
        float minZ = std::numeric_limits<float>::max();
        float maxZ = std::numeric_limits<float>::lowest();
        for (const auto& v : mesh.vertices) {
            minZ = std::min(minZ, v.position.z());
            maxZ = std::max(maxZ, v.position.z());
        }
        const float zSpan = std::max(1e-5f, maxZ - minZ);

        for (auto& vertex : mesh.vertices) {
            QVector3D n = vertex.normal.normalized();
            if (n.lengthSquared() < 1e-6f) n = QVector3D(0, 0, 1);

            int hitCount = 0;
            int usedSamples = 0;
            const QVector3D origin = vertex.position + n * std::max(0.001f, settings.rayDistance * 0.00005f);

            for (int si = 0; si < samples.size(); ++si) {
                const QVector3D dir = sampleToWorld(samples[si], n);
                if (bvh.intersects(origin, dir, settings.rayDistance)) {
                    ++hitCount;
                }
                ++usedSamples;

                if (settings.adaptiveSampling && usedSamples >= 32 && (usedSamples % 16) == 0) {
                    const float hitRatio = float(hitCount) / float(usedSamples);
                    if (hitRatio <= 0.03f || hitRatio >= 0.97f) {
                        break;
                    }
                    if (usedSamples >= samples.size() / 2 && (hitRatio <= 0.08f || hitRatio >= 0.92f)) {
                        break;
                    }
                }
            }

            float visible = 1.0f - float(hitCount) / float(std::max(1, usedSamples));
            const float height = (vertex.position.z() - minZ) / zSpan;
            visible = std::min(1.0f, visible + groundLuma * settings.groundBounceStrength * std::max(0.0f, 1.0f - height));

            const float ao = remapAO(visible, settings);
            vertex.color = QColor::fromRgbF(ao, ao, ao, 1.0f);

            ++done;
            if (done % 64 == 0 || done == total) {
                emit progress(QStringLiteral("AO 快速烘焙：%1/%2（BVH 加速 + 自适应采样）").arg(done).arg(total), int(done * 100.0 / total));
            }
        }
        mesh.hasVertexColor = true;
    }
}
