#pragma once

#include "ModelData.h"
#include <QString>

class TextureRasterizer {
public:
    static void bakeAOTexture(const SceneData& scene, const QString& filePath, const BakeSettings& settings);
};
