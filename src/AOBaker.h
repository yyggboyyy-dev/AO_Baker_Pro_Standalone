#pragma once

#include "ModelData.h"
#include <QObject>
#include <QVector>
#include <atomic>

class AOBaker : public QObject {
    Q_OBJECT
public:
    explicit AOBaker(QObject* parent = nullptr);
    void bakeToVertexColor(SceneData& scene, const BakeSettings& settings);
    void requestCancel();
    bool isCancelRequested() const;

signals:
    void progress(const QString& message, int percent);

private:
    QVector<QVector3D> hemisphereSamples(int count) const;
    float remapAO(float value, const BakeSettings& settings) const;
    QVector3D sampleToWorld(const QVector3D& sample, const QVector3D& normal) const;
    std::atomic_bool m_cancelRequested = false;
};
