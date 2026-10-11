#include "ModelData.h"

int SceneData::vertexCount() const {
    int total = 0;
    for (const auto& mesh : meshes) total += mesh.vertices.size();
    return total;
}

int SceneData::faceCount() const {
    int total = 0;
    for (const auto& mesh : meshes) total += mesh.faces.size();
    return total;
}

int SceneData::uvMeshCount() const {
    int total = 0;
    for (const auto& mesh : meshes) if (mesh.hasUV) ++total;
    return total;
}

void SceneData::clear() {
    sourcePath.clear();
    meshes.clear();
}
