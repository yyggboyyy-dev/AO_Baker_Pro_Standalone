#pragma once

#include "ModelData.h"
#include <QString>

class ModelIO {
public:
    static SceneData importModel(const QString& filePath);
    static void exportModel(const SceneData& scene, const QString& filePath, const QString& formatHint = QString());

private:
    static QString extensionForAssimp(const QString& filePath, const QString& formatHint);
};
