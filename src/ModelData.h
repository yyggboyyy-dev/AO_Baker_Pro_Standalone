#pragma once

#include <QColor>
#include <QString>
#include <QVector>
#include <QVector2D>
#include <QVector3D>
#include <QMatrix4x4>

struct Vertex {
    QVector3D position;
    QVector3D normal;
    QVector2D uv;
    QColor color = QColor(255, 255, 255, 255);
    float bakedAO = 1.0f;
    int originalIndex = -1;
};

struct Face {
    int i0 = 0;
    int i1 = 0;
    int i2 = 0;
    int materialIndex = 0;
    unsigned int smoothingMask = 0;
};

struct MeshData {
    QString name;
    QVector<Vertex> vertices;
    QVector<Face> faces;
    bool hasUV = false;
    bool hasNormals = false;
    bool hasVertexColor = false;
    bool visible = true;
};

struct SceneData {
    QString sourcePath;
    QVector<MeshData> meshes;

    int vertexCount() const;
    int faceCount() const;
    int uvMeshCount() const;
    bool empty() const { return meshes.isEmpty(); }
    void clear();
};

struct BakeSettings {
    float aoStrength = 1.0f;
    QColor groundBounceColor = QColor(255, 255, 255);
    float groundBounceStrength = 0.35f;
    float transitionSmoothness = 0.30f;
    float transitionContrast = 1.50f;
    float darkest = 0.00f;
    float brightest = 1.00f;
    int sampleCount = 256;
    float rayDistance = 10.0f;
    // Relative to scene bounding-box diagonal; avoids self-intersection at different model scales.
    float rayOriginOffset = 0.0001f;
    int uvPaddingPixels = 4;
    float detailPreserve = 0.80f;
    int textureResolution = 2048;
    QString textureFormat = "PNG";
    QString textureBitDepth = "8-bit";
    QString textureSplitMode = "单张AO图";
    QString vertexColorChannels = "RGB";
    QString vertexColorWriteMode = "覆盖";
    QString bakeScope = "全场景";
    int selectedMeshIndex = -1;
    bool autoOpenOutputDir = false;
    bool keepSmoothingGroups = true;
    bool keepUVs = true;
    bool adaptiveSampling = true;
    QString accelerationMode = "自动";
    bool multiThreadedBake = true;
    int bakeBlockSize = 512;
    bool cacheAccelerationStructure = true;
    bool gpuComputePreview = false;
};
