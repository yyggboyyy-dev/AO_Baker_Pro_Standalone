#pragma once

#include "ModelData.h"
#include <QObject>
#include <QVector>

class AOBaker : public QObject {
    Q_OBJECT
public:
    explicit AOBaker(QObject* parent = nullptr);
    void bakeToVertexColor(SceneData& scene, const BakeSettings& settings);

signals:
    void progress(const QString& message, int percent);

private:
    struct Triangle {
        QVector3D a;
        QVector3D b;
        QVector3D c;
    };

    QVector<QVector3D> hemisphereSamples(int count) const;
    QVector<Triangle> collectTriangles(const SceneData& scene) const;
    bool rayTriangle(const QVector3D& origin, const QVector3D& dir, const Triangle& tri, float maxDistance) const;
    float remapAO(float value, const BakeSettings& settings) const;
    QVector3D sampleToWorld(const QVector3D& sample, const QVector3D& normal) const;
};
