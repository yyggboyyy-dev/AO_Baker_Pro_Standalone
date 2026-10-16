#pragma once

#include "ModelData.h"
#include <QString>

class ModelIO {
public:
    struct ExportOptions {
        bool exportNormals = true;
        bool exportUVs = true;
        bool exportVertexColors = true;
        bool exportOnlySelectedMesh = false;
        int selectedMeshIndex = -1;
    };

    static SceneData importModel(const QString& filePath);
    static void exportModel(const SceneData& scene, const QString& filePath, const QString& formatHint = QString());
    static void exportModel(const SceneData& scene, const QString& filePath, const QString& formatHint, const ExportOptions& options);

private:
    static QString extensionForAssimp(const QString& filePath, const QString& formatHint);
};
