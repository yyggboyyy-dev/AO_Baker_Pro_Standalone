#include "AOBaker.h"

#include <QtMath>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QThread>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <future>
#include <limits>
#include <memory>
#include <numeric>
#include <vector>

namespace {

struct FastTriangle {
    QVector3D a;
    QVector3D b;
    QVector3D c;
    QVector3D normal;
    QVector3D centroid;
    QVector3D bmin;
    QVector3D bmax;
    int meshIndex = -1;
    unsigned int smoothingMask = 0;
};

struct BvhNode {
    QVector3D bmin;
    QVector3D bmax;
    int left = -1;
    int right = -1;
    int start = 0;
    int count = 0;
};

struct VertexBakeJob {
    int meshIndex = -1;
    int vertexIndex = -1;
};

struct VertexBakeResult {
    int meshIndex = -1;
    int vertexIndex = -1;
    QColor color;
    float ao = 1.0f;
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

float longestExtent(const QVector3D& minV, const QVector3D& maxV) {
    const QVector3D e = maxV - minV;
    return std::max(e.x(), std::max(e.y(), e.z()));
}

quint64 quantizeFloat(float value, float scale = 10000.0f) {
    return quint64(std::llround(double(value) * double(scale))) & 0x00000000ffffffffULL;
}

quint64 stableSceneHash(const SceneData& scene, const BakeSettings& settings) {
    quint64 h = 1469598103934665603ULL;
    auto mix = [&h](quint64 v) {
        h ^= v;
        h *= 1099511628211ULL;
    };

    mix(quint64(scene.meshes.size()));
    mix(quint64(settings.bakeScope.size()));
    mix(quint64(settings.selectedMeshIndex + 17));
    const bool selectedOnly = settings.bakeScope == QStringLiteral("选中对象") && settings.selectedMeshIndex >= 0;
    const bool visibleOnly = settings.bakeScope == QStringLiteral("可见对象");

    for (int meshIndex = 0; meshIndex < scene.meshes.size(); ++meshIndex) {
        if (selectedOnly && meshIndex != settings.selectedMeshIndex) continue;
        const auto& mesh = scene.meshes[meshIndex];
        if (visibleOnly && !mesh.visible) continue;
        mix(quint64(meshIndex + 1));
        mix(quint64(mesh.visible ? 1 : 0));
        mix(quint64(mesh.vertices.size()));
        mix(quint64(mesh.faces.size()));
        for (const auto& v : mesh.vertices) {
            mix(quantizeFloat(v.position.x()));
            mix(quantizeFloat(v.position.y()));
            mix(quantizeFloat(v.position.z()));
        }
        for (const auto& face : mesh.faces) {
            mix(quint64(face.i0 + 31));
            mix(quint64(face.i1 + 131));
            mix(quint64(face.i2 + 521));
            mix(quint64(face.smoothingMask));
        }
    }
    return h;
}

QVector<FastTriangle> collectFastTriangles(const SceneData& scene, const BakeSettings& settings) {
    QVector<FastTriangle> tris;
    tris.reserve(scene.faceCount());

    const bool selectedOnly = settings.bakeScope == QStringLiteral("选中对象") && settings.selectedMeshIndex >= 0;
    const bool visibleOnly = settings.bakeScope == QStringLiteral("可见对象");
    for (int meshIndex = 0; meshIndex < scene.meshes.size(); ++meshIndex) {
        if (selectedOnly && meshIndex != settings.selectedMeshIndex) continue;
        const auto& mesh = scene.meshes[meshIndex];
        if (visibleOnly && !mesh.visible) continue;
        for (const auto& face : mesh.faces) {
            if (face.i0 < 0 || face.i1 < 0 || face.i2 < 0 ||
                face.i0 >= mesh.vertices.size() || face.i1 >= mesh.vertices.size() || face.i2 >= mesh.vertices.size()) {
                continue;
            }

            FastTriangle tri;
            tri.a = mesh.vertices[face.i0].position;
            tri.b = mesh.vertices[face.i1].position;
            tri.c = mesh.vertices[face.i2].position;
            tri.normal = QVector3D::crossProduct(tri.b - tri.a, tri.c - tri.a).normalized();
            tri.centroid = (tri.a + tri.b + tri.c) / 3.0f;
            tri.bmin = minVec(minVec(tri.a, tri.b), tri.c);
            tri.bmax = maxVec(maxVec(tri.a, tri.b), tri.c);
            tri.meshIndex = meshIndex;
            tri.smoothingMask = face.smoothingMask;
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
            m_nodes.reserve(m_indices.size() * 2 + 1);
            buildNode(0, m_indices.size());
        }
    }

    bool intersects(const QVector3D& origin, const QVector3D& dir, float maxDistance, float* hitDistance = nullptr) const {
        if (m_nodes.isEmpty()) return false;

        int stack[128];
        int stackSize = 0;
        stack[stackSize++] = 0;
        float closest = maxDistance;
        bool hit = false;

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
                        closest = t;
                        hit = true;
                    }
                }
            } else {
                if (node.left >= 0 && stackSize < 128) stack[stackSize++] = node.left;
                if (node.right >= 0 && stackSize < 128) stack[stackSize++] = node.right;
            }
        }

        if (hit && hitDistance) *hitDistance = closest;
        return hit;
    }

    int nodeCount() const { return m_nodes.size(); }
    int triangleCount() const { return m_triangles.size(); }

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

class KdTree {
public:
    explicit KdTree(const QVector<FastTriangle>& triangles) : m_triangles(triangles) {
        m_indices.resize(m_triangles.size());
        std::iota(m_indices.begin(), m_indices.end(), 0);
        if (!m_indices.isEmpty()) {
            m_nodes.reserve(m_indices.size() * 2 + 1);
            buildNode(0, m_indices.size(), 0);
        }
    }

    bool intersects(const QVector3D& origin, const QVector3D& dir, float maxDistance, float* hitDistance = nullptr) const {
        if (m_nodes.isEmpty()) return false;

        int stack[128];
        int stackSize = 0;
        stack[stackSize++] = 0;
        float closest = maxDistance;
        bool hit = false;

        while (stackSize > 0) {
            const int nodeIndex = stack[--stackSize];
            const BvhNode& node = m_nodes[nodeIndex];
            if (!rayAabb(origin, dir, node.bmin, node.bmax, closest)) continue;

            if (node.left < 0 && node.right < 0) {
                for (int i = 0; i < node.count; ++i) {
                    const FastTriangle& tri = m_triangles[m_indices[node.start + i]];
                    float t = closest;
                    if (rayTriangle(origin, dir, tri, t)) {
                        closest = t;
                        hit = true;
                    }
                }
            } else {
                if (node.left >= 0 && stackSize < 128) stack[stackSize++] = node.left;
                if (node.right >= 0 && stackSize < 128) stack[stackSize++] = node.right;
            }
        }

        if (hit && hitDistance) *hitDistance = closest;
        return hit;
    }

    int nodeCount() const { return m_nodes.size(); }

private:
    int buildNode(int start, int count, int depth) {
        BvhNode node;
        node.start = start;
        node.count = count;
        node.bmin = QVector3D(std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max());
        node.bmax = QVector3D(std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest());

        for (int i = 0; i < count; ++i) {
            const FastTriangle& tri = m_triangles[m_indices[start + i]];
            node.bmin = minVec(node.bmin, tri.bmin);
            node.bmax = maxVec(node.bmax, tri.bmax);
        }

        const int thisIndex = m_nodes.size();
        m_nodes.push_back(node);

        constexpr int leafSize = 8;
        if (count <= leafSize) return thisIndex;

        const QVector3D extent = node.bmax - node.bmin;
        int axis = depth % 3;
        if (extent.y() > extent.x() && extent.y() >= extent.z()) axis = 1;
        else if (extent.z() > extent.x() && extent.z() >= extent.y()) axis = 2;
        if (axisValue(extent, axis) < 1e-6f) return thisIndex;

        const int mid = start + count / 2;
        std::nth_element(m_indices.begin() + start, m_indices.begin() + mid, m_indices.begin() + start + count,
            [this, axis](int lhs, int rhs) {
                return axisValue(m_triangles[lhs].centroid, axis) < axisValue(m_triangles[rhs].centroid, axis);
            });

        m_nodes[thisIndex].left = buildNode(start, mid - start, depth + 1);
        m_nodes[thisIndex].right = buildNode(mid, start + count - mid, depth + 1);
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

struct AccelerationCache {
    quint64 sceneHash = 0;
    QVector<FastTriangle> triangles;
    std::unique_ptr<BvhTree> bvh;
    std::unique_ptr<KdTree> kdTree;
    QString mode;
    qint64 buildMs = 0;
};

AccelerationCache g_cache;

QVector<QVector3D> makeHemisphereSamples(int count) {
    QVector<QVector3D> out;
    count = std::max(16, count);
    out.reserve(count);

    const float golden = float(M_PI) * (3.0f - std::sqrt(5.0f));
    for (int i = 0; i < count; ++i) {
        const float u = (i + 0.5f) / float(count);
        const float z = std::sqrt(1.0f - u);
        const float r = std::sqrt(std::max(0.0f, 1.0f - z * z));
        const float theta = i * golden;
        out.push_back(QVector3D(std::cos(theta) * r, std::sin(theta) * r, z));
    }
    return out;
}

QVector3D sampleToWorldFast(const QVector3D& sample, const QVector3D& normal) {
    QVector3D n = normal.normalized();
    if (n.lengthSquared() < 1e-6f) n = QVector3D(0, 0, 1);
    const QVector3D helper = std::abs(QVector3D::dotProduct(n, QVector3D(0, 0, 1))) > 0.95f ? QVector3D(1, 0, 0) : QVector3D(0, 0, 1);
    const QVector3D tangent = QVector3D::crossProduct(helper, n).normalized();
    const QVector3D bitangent = QVector3D::crossProduct(n, tangent).normalized();
    return (tangent * sample.x() + bitangent * sample.y() + n * sample.z()).normalized();
}

float remapAOFast(float visibility, const BakeSettings& settings) {
    float v = std::clamp(visibility, 0.0f, 1.0f);
    v = std::pow(v, 1.0f / std::max(settings.transitionContrast, 0.2f));

    const float s = std::clamp(settings.transitionSmoothness, 0.0f, 1.0f);
    const float smooth = v * v * (3.0f - 2.0f * v);
    v = v * (1.0f - s) + smooth * s;
    v = 1.0f - (1.0f - v) * std::max(0.0f, settings.aoStrength);
    v = settings.darkest + (settings.brightest - settings.darkest) * std::clamp(v, 0.0f, 1.0f);
    return std::clamp(v, 0.0f, 1.0f);
}

bool shouldUseKdTree(const BakeSettings& settings, int triangleCount) {
    if (settings.accelerationMode == QStringLiteral("KD-Tree")) return true;
    if (settings.accelerationMode == QStringLiteral("自动") && triangleCount > 350000) return true;
    return false;
}

bool shouldUseEmbreeLike(const BakeSettings& settings, int triangleCount) {
    if (settings.accelerationMode == QStringLiteral("Embree 类 CPU")) return true;
    if (settings.accelerationMode == QStringLiteral("自动") && triangleCount > 120000) return true;
    return false;
}

VertexBakeResult bakeSingleVertex(const SceneData& scene,
                                  const BakeSettings& settings,
                                  const QVector<QVector3D>& samples,
                                  const BvhTree* bvh,
                                  const KdTree* kdTree,
                                  const VertexBakeJob& job,
                                  float groundLuma) {
    VertexBakeResult result;
    result.meshIndex = job.meshIndex;
    result.vertexIndex = job.vertexIndex;

    const auto& vertex = scene.meshes[job.meshIndex].vertices[job.vertexIndex];
    QVector3D n = vertex.normal.normalized();
    if (n.lengthSquared() < 1e-6f) n = QVector3D(0, 0, 1);

    float occlusion = 0.0f;
    int usedSamples = 0;
    const QVector3D origin = vertex.position + n * std::max(0.001f, settings.rayDistance * 0.00005f);

    for (int si = 0; si < samples.size(); ++si) {
        const QVector3D dir = sampleToWorldFast(samples[si], n);
        float hitDistance = settings.rayDistance;
        bool hit = false;
        if (kdTree) hit = kdTree->intersects(origin, dir, settings.rayDistance, &hitDistance);
        else if (bvh) hit = bvh->intersects(origin, dir, settings.rayDistance, &hitDistance);
        if (hit) {
            const float distanceWeight = 1.0f - std::clamp(hitDistance / std::max(0.0001f, settings.rayDistance), 0.0f, 1.0f);
            const float detailWeight = settings.detailPreserve + (1.0f - settings.detailPreserve) * distanceWeight;
            occlusion += std::clamp(distanceWeight * detailWeight, 0.0f, 1.0f);
        }
        ++usedSamples;

        if (settings.adaptiveSampling && usedSamples >= 32 && (usedSamples % 16) == 0) {
            const float blockedRatio = occlusion / float(usedSamples);
            if (blockedRatio <= 0.015f || blockedRatio >= 0.985f) break;
            if (usedSamples >= samples.size() / 2 && (blockedRatio <= 0.04f || blockedRatio >= 0.96f)) break;
        }
    }

    const float blocked = occlusion / float(std::max(1, usedSamples));
    float visible = 1.0f - blocked;
    const float upward = std::clamp((n.z() + 1.0f) * 0.5f, 0.0f, 1.0f);
    visible = std::min(1.0f, visible + groundLuma * settings.groundBounceStrength * upward * (1.0f - blocked));

    const float ao = remapAOFast(visible, settings);
    result.ao = ao;

    const QColor oldColor = vertex.color.isValid() ? vertex.color : QColor(255, 255, 255, 255);
    QColor bakedColor;
    if (settings.vertexColorWriteMode == QStringLiteral("混合")) {
        bakedColor = QColor::fromRgbF(std::clamp(float(oldColor.redF() * ao), 0.0f, 1.0f),
                                      std::clamp(float(oldColor.greenF() * ao), 0.0f, 1.0f),
                                      std::clamp(float(oldColor.blueF() * ao), 0.0f, 1.0f),
                                      oldColor.alphaF());
    } else {
        bakedColor = QColor::fromRgbF(ao, ao, ao, oldColor.alphaF());
    }

    QColor finalColor = oldColor;
    const QString channels = settings.vertexColorChannels.toUpper();
    if (channels.contains(QStringLiteral("R"))) finalColor.setRed(bakedColor.red());
    if (channels.contains(QStringLiteral("G"))) finalColor.setGreen(bakedColor.green());
    if (channels.contains(QStringLiteral("B"))) finalColor.setBlue(bakedColor.blue());
    if (channels.contains(QStringLiteral("A"))) finalColor.setAlpha(bakedColor.alpha());
    result.color = finalColor;
    return result;
}

} // namespace

AOBaker::AOBaker(QObject* parent) : QObject(parent) {}

QVector<QVector3D> AOBaker::hemisphereSamples(int count) const {
    return makeHemisphereSamples(count);
}

QVector3D AOBaker::sampleToWorld(const QVector3D& sample, const QVector3D& normal) const {
    return sampleToWorldFast(sample, normal);
}

float AOBaker::remapAO(float visibility, const BakeSettings& settings) const {
    return remapAOFast(visibility, settings);
}

void AOBaker::requestCancel() {
    m_cancelRequested.store(true);
}

bool AOBaker::isCancelRequested() const {
    return m_cancelRequested.load();
}

void AOBaker::bakeToVertexColor(SceneData& scene, const BakeSettings& settings) {
    m_cancelRequested.store(false);

    const quint64 sceneHash = stableSceneHash(scene, settings);
    QElapsedTimer buildTimer;
    buildTimer.start();

    const bool canReuseCache = settings.cacheAccelerationStructure &&
        g_cache.sceneHash == sceneHash &&
        g_cache.mode == settings.accelerationMode &&
        !g_cache.triangles.isEmpty() &&
        ((g_cache.bvh && !shouldUseKdTree(settings, g_cache.triangles.size())) ||
         (g_cache.kdTree && shouldUseKdTree(settings, g_cache.triangles.size())));

    if (!canReuseCache) {
        g_cache = AccelerationCache();
        g_cache.sceneHash = sceneHash;
        g_cache.mode = settings.accelerationMode;
        g_cache.triangles = collectFastTriangles(scene, settings);

        if (shouldUseKdTree(settings, g_cache.triangles.size())) {
            g_cache.kdTree.reset(new KdTree(g_cache.triangles));
        } else {
            g_cache.bvh.reset(new BvhTree(g_cache.triangles));
        }
        g_cache.buildMs = buildTimer.elapsed();
    }

    const QVector<FastTriangle>& tris = g_cache.triangles;
    const BvhTree* bvh = g_cache.bvh.get();
    const KdTree* kdTree = g_cache.kdTree.get();
    const QVector<QVector3D> samples = hemisphereSamples(settings.sampleCount);
    const bool selectedOnly = settings.bakeScope == QStringLiteral("选中对象") && settings.selectedMeshIndex >= 0;
    const bool visibleOnly = settings.bakeScope == QStringLiteral("可见对象");

    QVector<VertexBakeJob> jobs;
    jobs.reserve(scene.vertexCount());
    for (int meshIndex = 0; meshIndex < scene.meshes.size(); ++meshIndex) {
        if (selectedOnly && meshIndex != settings.selectedMeshIndex) continue;
        if (visibleOnly && !scene.meshes[meshIndex].visible) continue;
        for (int vertexIndex = 0; vertexIndex < scene.meshes[meshIndex].vertices.size(); ++vertexIndex) {
            jobs.push_back({meshIndex, vertexIndex});
        }
    }

    const int total = std::max(1, static_cast<int>(jobs.size()));
    const int threadCount = settings.multiThreadedBake ? std::max(1, QThread::idealThreadCount()) : 1;
    const int blockSize = std::max(64, settings.bakeBlockSize);
    const QString accelName = kdTree ? QStringLiteral("KD-Tree") : QStringLiteral("BVH");
    const bool embreeLike = shouldUseEmbreeLike(settings, tris.size());
    emit progress(QStringLiteral("加速结构：%1%2，三角面=%3，节点=%4，构建=%5ms，线程=%6，分块=%7，缓存=%8")
        .arg(accelName)
        .arg(embreeLike ? QStringLiteral(" / Embree 类 CPU 遍历") : QString())
        .arg(tris.size())
        .arg(kdTree ? kdTree->nodeCount() : (bvh ? bvh->nodeCount() : 0))
        .arg(canReuseCache ? 0 : g_cache.buildMs)
        .arg(threadCount)
        .arg(blockSize)
        .arg(canReuseCache ? QStringLiteral("复用") : QStringLiteral("重建")), 1);

    const float groundLuma = settings.groundBounceColor.redF() * 0.2126f +
                             settings.groundBounceColor.greenF() * 0.7152f +
                             settings.groundBounceColor.blueF() * 0.0722f;

    int done = 0;
    for (int blockStart = 0; blockStart < static_cast<int>(jobs.size()); blockStart += blockSize) {
        if (isCancelRequested()) break;
        const int blockEnd = std::min(blockStart + blockSize, static_cast<int>(jobs.size()));
        QVector<VertexBakeJob> blockJobs;
        blockJobs.reserve(blockEnd - blockStart);
        for (int i = blockStart; i < blockEnd; ++i) blockJobs.push_back(jobs[i]);

        QVector<VertexBakeResult> results;
        results.reserve(blockJobs.size());

        if (threadCount > 1 && blockJobs.size() > 128) {
            const int blockJobCount = static_cast<int>(blockJobs.size());
            const int workerCount = std::min(threadCount, std::max(1, blockJobCount / 64));
            std::vector<std::future<QVector<VertexBakeResult>>> futures;
            futures.reserve(workerCount);
            for (int worker = 0; worker < workerCount; ++worker) {
                const int begin = worker * blockJobCount / workerCount;
                const int end = (worker + 1) * blockJobCount / workerCount;
                futures.push_back(std::async(std::launch::async, [&, begin, end]() {
                    QVector<VertexBakeResult> local;
                    local.reserve(end - begin);
                    for (int i = begin; i < end; ++i) {
                        local.push_back(bakeSingleVertex(scene, settings, samples, bvh, kdTree, blockJobs[i], groundLuma));
                    }
                    return local;
                }));
            }
            for (auto& future : futures) {
                const QVector<VertexBakeResult> local = future.get();
                for (const auto& item : local) results.push_back(item);
            }
        } else {
            for (const auto& job : blockJobs) {
                results.push_back(bakeSingleVertex(scene, settings, samples, bvh, kdTree, job, groundLuma));
            }
        }

        for (const auto& result : results) {
            if (result.meshIndex < 0 || result.meshIndex >= scene.meshes.size()) continue;
            auto& mesh = scene.meshes[result.meshIndex];
            if (result.vertexIndex < 0 || result.vertexIndex >= mesh.vertices.size()) continue;
            mesh.vertices[result.vertexIndex].bakedAO = result.ao;
            mesh.vertices[result.vertexIndex].color = result.color;
            mesh.hasVertexColor = true;
        }

        done += blockJobs.size();
        emit progress(QStringLiteral("顶点 AO 分块计算：%1/%2（%3，通道：%4，采样：%5，距离：%6）")
            .arg(done).arg(total).arg(accelName).arg(settings.vertexColorChannels).arg(settings.sampleCount).arg(settings.rayDistance), int(done * 100.0 / total));
        QCoreApplication::processEvents();
    }

    if (isCancelRequested()) {
        emit progress(QStringLiteral("顶点 AO 烘焙已取消：已完成 %1/%2").arg(done).arg(total), int(done * 100.0 / total));
    }
}
