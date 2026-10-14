#include "ModelIO.h"

#include <assimp/Importer.hpp>
#include <assimp/Exporter.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <QFileInfo>
#include <QVector4D>
#include <QFile>
#include <QByteArray>
#include <QStringList>
#include <stdexcept>
#include <memory>

static QVector3D toVec3(const aiVector3D& v) { return QVector3D(v.x, v.y, v.z); }
static QVector2D toVec2(const aiVector3D& v) { return QVector2D(v.x, v.y); }

static int detectFbxVersion(const QString& filePath) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) return 0;
    const QByteArray head = file.read(32);
    if (!head.startsWith("Kaydara FBX Binary") || head.size() < 27) return 0;
    const uchar* p = reinterpret_cast<const uchar*>(head.constData() + 23);
    return int(p[0]) | (int(p[1]) << 8) | (int(p[2]) << 16) | (int(p[3]) << 24);
}

static void setupFbxImporter(Assimp::Importer& importer) {
    importer.SetPropertyBool(AI_CONFIG_IMPORT_FBX_PRESERVE_PIVOTS, true);
    importer.SetPropertyBool(AI_CONFIG_IMPORT_FBX_READ_ALL_GEOMETRY_LAYERS, true);
    importer.SetPropertyBool(AI_CONFIG_IMPORT_FBX_READ_ALL_MATERIALS, true);
    importer.SetPropertyBool(AI_CONFIG_IMPORT_FBX_READ_MATERIALS, true);
    importer.SetPropertyBool(AI_CONFIG_IMPORT_FBX_READ_TEXTURES, true);
    importer.SetPropertyBool(AI_CONFIG_IMPORT_FBX_READ_CAMERAS, false);
    importer.SetPropertyBool(AI_CONFIG_IMPORT_FBX_READ_LIGHTS, false);
    importer.SetPropertyBool(AI_CONFIG_IMPORT_FBX_STRICT_MODE, false);
    importer.SetPropertyInteger(AI_CONFIG_PP_SBP_REMOVE, 0);
}

static void computeMissingNormals(MeshData& mesh) {
    bool needsNormals = !mesh.hasNormals;
    if (!needsNormals) {
        for (const auto& v : mesh.vertices) {
            if (v.normal.lengthSquared() < 1e-8f) {
                needsNormals = true;
                break;
            }
        }
    }
    if (!needsNormals) return;

    for (auto& vertex : mesh.vertices) vertex.normal = QVector3D(0, 0, 0);
    for (const auto& face : mesh.faces) {
        if (face.i0 < 0 || face.i1 < 0 || face.i2 < 0 ||
            face.i0 >= mesh.vertices.size() || face.i1 >= mesh.vertices.size() || face.i2 >= mesh.vertices.size()) {
            continue;
        }
        const QVector3D a = mesh.vertices[face.i0].position;
        const QVector3D b = mesh.vertices[face.i1].position;
        const QVector3D c = mesh.vertices[face.i2].position;
        const QVector3D n = QVector3D::crossProduct(b - a, c - a);
        mesh.vertices[face.i0].normal += n;
        mesh.vertices[face.i1].normal += n;
        mesh.vertices[face.i2].normal += n;
    }
    for (auto& vertex : mesh.vertices) {
        vertex.normal = vertex.normal.lengthSquared() > 1e-8f ? vertex.normal.normalized() : QVector3D(0, 0, 1);
    }
    mesh.hasNormals = true;
}

SceneData ModelIO::importModel(const QString& filePath) {
    const bool isFbx = QFileInfo(filePath).suffix().toLower() == "fbx";
    const int fbxVersion = isFbx ? detectFbxVersion(filePath) : 0;

    const unsigned int preserveFlags =
        aiProcess_Triangulate |
        aiProcess_SortByPType |
        aiProcess_ImproveCacheLocality;

    const unsigned int safeFlags =
        aiProcess_Triangulate |
        aiProcess_SortByPType;

    const unsigned int fallbackFlags =
        aiProcess_Triangulate;

    const unsigned int normalFallbackFlags =
        aiProcess_Triangulate |
        aiProcess_GenSmoothNormals |
        aiProcess_SortByPType;

    const unsigned int attempts[] = { preserveFlags, safeFlags, fallbackFlags, normalFallbackFlags };
    QStringList errors;
    const aiScene* ai = nullptr;
    std::unique_ptr<Assimp::Importer> activeImporter;

    for (unsigned int flags : attempts) {
        auto importer = std::make_unique<Assimp::Importer>();
        setupFbxImporter(*importer);
        if (isFbx && (fbxVersion == 7400 || fbxVersion == 7500)) {
            importer->SetPropertyBool(AI_CONFIG_IMPORT_FBX_STRICT_MODE, false);
        }

        ai = importer->ReadFile(filePath.toStdString(), flags);
        if (ai && ai->HasMeshes()) {
            activeImporter = std::move(importer);
            break;
        }
        errors << QString::fromUtf8(importer->GetErrorString());
    }

    if (!ai || !ai->HasMeshes()) {
        QString versionInfo;
        if (isFbx && fbxVersion > 0) {
            versionInfo = QString("\n检测到 FBX 版本：%1。已按 Autodesk FBX 2014/2015 兼容路径尝试导入。").arg(fbxVersion);
        }
        throw std::runtime_error(QString("模型导入失败：%1%2").arg(errors.join(" | ")).arg(versionInfo).toStdString());
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
            face.smoothingMask = 0xffffffffu;
            mesh.faces.push_back(face);
        }
        computeMissingNormals(mesh);
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
