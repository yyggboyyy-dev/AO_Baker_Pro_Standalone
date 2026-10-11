#include "ModelIO.h"

#include <assimp/Importer.hpp>
#include <assimp/Exporter.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <QFileInfo>
#include <QVector4D>
#include <stdexcept>
#include <memory>

static QVector3D toVec3(const aiVector3D& v) { return QVector3D(v.x, v.y, v.z); }
static QVector2D toVec2(const aiVector3D& v) { return QVector2D(v.x, v.y); }

SceneData ModelIO::importModel(const QString& filePath) {
    Assimp::Importer importer;
    importer.SetPropertyBool(AI_CONFIG_IMPORT_FBX_PRESERVE_PIVOTS, true);
    importer.SetPropertyBool(AI_CONFIG_IMPORT_FBX_READ_ALL_GEOMETRY_LAYERS, true);
    importer.SetPropertyBool(AI_CONFIG_IMPORT_FBX_READ_ALL_MATERIALS, true);
    importer.SetPropertyBool(AI_CONFIG_IMPORT_FBX_READ_MATERIALS, true);
    importer.SetPropertyBool(AI_CONFIG_IMPORT_FBX_READ_TEXTURES, true);
    importer.SetPropertyBool(AI_CONFIG_IMPORT_FBX_READ_CAMERAS, false);
    importer.SetPropertyBool(AI_CONFIG_IMPORT_FBX_READ_LIGHTS, false);
    importer.SetPropertyBool(AI_CONFIG_IMPORT_FBX_STRICT_MODE, false);

    const unsigned int flags =
        aiProcess_Triangulate |
        aiProcess_JoinIdenticalVertices |
        aiProcess_SortByPType |
        aiProcess_ImproveCacheLocality |
        aiProcess_ValidateDataStructure |
        aiProcess_OptimizeMeshes;

    const aiScene* ai = importer.ReadFile(filePath.toStdString(), flags);
    if (!ai || !ai->HasMeshes()) {
        throw std::runtime_error(QString("模型导入失败：%1").arg(importer.GetErrorString()).toStdString());
    }

    SceneData scene;
    scene.sourcePath = filePath;
    scene.meshes.reserve(static_cast<int>(ai->mNumMeshes));

    for (unsigned int mi = 0; mi < ai->mNumMeshes; ++mi) {
        const aiMesh* src = ai->mMeshes[mi];
        MeshData mesh;
        mesh.name = src->mName.length ? QString::fromUtf8(src->mName.C_Str()) : QString("Mesh_%1").arg(mi);
        mesh.hasUV = src->HasTextureCoords(0);
        mesh.hasNormals = src->HasNormals();
        mesh.hasVertexColor = src->HasVertexColors(0);
        mesh.vertices.reserve(static_cast<int>(src->mNumVertices));

        for (unsigned int vi = 0; vi < src->mNumVertices; ++vi) {
            Vertex v;
            v.position = toVec3(src->mVertices[vi]);
            if (src->HasNormals()) v.normal = toVec3(src->mNormals[vi]);
            if (src->HasTextureCoords(0)) v.uv = toVec2(src->mTextureCoords[0][vi]);
            if (src->HasVertexColors(0)) {
                const aiColor4D c = src->mColors[0][vi];
                v.color = QColor::fromRgbF(c.r, c.g, c.b, c.a);
            }
            v.originalIndex = static_cast<int>(vi);
            mesh.vertices.push_back(v);
        }

        mesh.faces.reserve(static_cast<int>(src->mNumFaces));
        for (unsigned int fi = 0; fi < src->mNumFaces; ++fi) {
            const aiFace& f = src->mFaces[fi];
            if (f.mNumIndices != 3) continue;
            Face face;
            face.i0 = static_cast<int>(f.mIndices[0]);
            face.i1 = static_cast<int>(f.mIndices[1]);
            face.i2 = static_cast<int>(f.mIndices[2]);
            face.materialIndex = static_cast<int>(src->mMaterialIndex);
            if (src->mFaces[fi].mNumIndices >= 3) face.smoothingMask = 0xffffffffu;
            mesh.faces.push_back(face);
        }
        scene.meshes.push_back(mesh);
    }
    return scene;
}

QString ModelIO::extensionForAssimp(const QString& filePath, const QString& formatHint) {
    if (!formatHint.isEmpty()) return formatHint;
    const QString ext = QFileInfo(filePath).suffix().toLower();
    if (ext == "fbx") return "fbx";
    if (ext == "obj") return "obj";
    if (ext == "glb") return "glb2";
    if (ext == "gltf") return "gltf2";
    return "glb2";
}

void ModelIO::exportModel(const SceneData& sceneData, const QString& filePath, const QString& formatHint) {
    aiScene scene;
    scene.mRootNode = new aiNode();
    scene.mRootNode->mName = aiString("RootNode");
    scene.mNumMeshes = static_cast<unsigned int>(sceneData.meshes.size());
    scene.mMeshes = new aiMesh*[scene.mNumMeshes]();
    scene.mRootNode->mNumMeshes = scene.mNumMeshes;
    scene.mRootNode->mMeshes = new unsigned int[scene.mNumMeshes]();

    for (unsigned int mi = 0; mi < scene.mNumMeshes; ++mi) {
        const MeshData& src = sceneData.meshes[static_cast<int>(mi)];
        aiMesh* mesh = new aiMesh();
        mesh->mName = aiString(src.name.toUtf8().constData());
        mesh->mPrimitiveTypes = aiPrimitiveType_TRIANGLE;
        mesh->mNumVertices = static_cast<unsigned int>(src.vertices.size());
        mesh->mVertices = new aiVector3D[mesh->mNumVertices]();
        mesh->mNormals = new aiVector3D[mesh->mNumVertices]();
        mesh->mTextureCoords[0] = src.hasUV ? new aiVector3D[mesh->mNumVertices]() : nullptr;
        mesh->mNumUVComponents[0] = src.hasUV ? 2 : 0;
        mesh->mColors[0] = new aiColor4D[mesh->mNumVertices]();

        for (unsigned int vi = 0; vi < mesh->mNumVertices; ++vi) {
            const Vertex& v = src.vertices[static_cast<int>(vi)];
            mesh->mVertices[vi] = aiVector3D(v.position.x(), v.position.y(), v.position.z());
            mesh->mNormals[vi] = aiVector3D(v.normal.x(), v.normal.y(), v.normal.z());
            if (src.hasUV) mesh->mTextureCoords[0][vi] = aiVector3D(v.uv.x(), v.uv.y(), 0.0f);
            mesh->mColors[0][vi] = aiColor4D(v.color.redF(), v.color.greenF(), v.color.blueF(), v.color.alphaF());
        }

        mesh->mNumFaces = static_cast<unsigned int>(src.faces.size());
        mesh->mFaces = new aiFace[mesh->mNumFaces]();
        for (unsigned int fi = 0; fi < mesh->mNumFaces; ++fi) {
            const Face& f = src.faces[static_cast<int>(fi)];
            aiFace& face = mesh->mFaces[fi];
            face.mNumIndices = 3;
            face.mIndices = new unsigned int[3]{static_cast<unsigned int>(f.i0), static_cast<unsigned int>(f.i1), static_cast<unsigned int>(f.i2)};
        }
        scene.mMeshes[mi] = mesh;
        scene.mRootNode->mMeshes[mi] = mi;
    }

    scene.mNumMaterials = 1;
    scene.mMaterials = new aiMaterial*[1]();
    scene.mMaterials[0] = new aiMaterial();

    Assimp::Exporter exporter;
    const QString fmt = extensionForAssimp(filePath, formatHint);
    const aiReturn result = exporter.Export(&scene, fmt.toStdString(), filePath.toStdString(), aiProcess_ValidateDataStructure);
    if (result != AI_SUCCESS) {
        throw std::runtime_error(QString("模型导出失败：%1").arg(exporter.GetErrorString()).toStdString());
    }
}
