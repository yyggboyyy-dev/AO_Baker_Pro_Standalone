#include "ModelIO.h"

#include <assimp/Importer.hpp>
#include <assimp/Exporter.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <QByteArray>
#include <QFile>
#include <QFileInfo>
#include <QMap>
#include <QStringList>
#include <QVector4D>
#include <zlib.h>
#include <algorithm>
#include <cstdint>
#include <memory>
#include <cstring>
#include <stdexcept>

static QVector3D toVec3(const aiVector3D& v) { return QVector3D(v.x, v.y, v.z); }
static QVector2D toVec2(const aiVector3D& v) { return QVector2D(v.x, v.y); }

namespace {

int detectFbxVersion(const QString& filePath) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) return 0;
    const QByteArray head = file.read(32);
    if (!head.startsWith("Kaydara FBX Binary") || head.size() < 27) return 0;
    const uchar* p = reinterpret_cast<const uchar*>(head.constData() + 23);
    return int(p[0]) | (int(p[1]) << 8) | (int(p[2]) << 16) | (int(p[3]) << 24);
}

void setupFbxImporter(Assimp::Importer& importer) {
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

void computeMissingNormals(MeshData& mesh) {
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

uint8_t readU8(const QByteArray& data, qsizetype& pos) {
    if (pos + 1 > data.size()) throw std::runtime_error("FBX binary read overflow");
    return static_cast<uint8_t>(data.at(pos++));
}

uint16_t readU16(const QByteArray& data, qsizetype& pos) {
    if (pos + 2 > data.size()) throw std::runtime_error("FBX binary read overflow");
    const uchar* p = reinterpret_cast<const uchar*>(data.constData() + pos);
    pos += 2;
    return uint16_t(p[0]) | (uint16_t(p[1]) << 8);
}

uint32_t readU32(const QByteArray& data, qsizetype& pos) {
    if (pos + 4 > data.size()) throw std::runtime_error("FBX binary read overflow");
    const uchar* p = reinterpret_cast<const uchar*>(data.constData() + pos);
    pos += 4;
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}

uint64_t readU64(const QByteArray& data, qsizetype& pos) {
    const uint64_t lo = readU32(data, pos);
    const uint64_t hi = readU32(data, pos);
    return lo | (hi << 32);
}

float readF32(const QByteArray& data, qsizetype& pos) {
    const uint32_t raw = readU32(data, pos);
    float f;
    std::memcpy(&f, &raw, sizeof(float));
    return f;
}

double readF64(const QByteArray& data, qsizetype& pos) {
    const uint64_t raw = readU64(data, pos);
    double d;
    std::memcpy(&d, &raw, sizeof(double));
    return d;
}

QString cleanFbxName(const QString& name) {
    const int nul = name.indexOf(QChar('\0'));
    return nul >= 0 ? name.left(nul) : name;
}

struct FbxValue {
    QString str;
    QVector<double> doubles;
    QVector<int> ints;
    qint64 integer = 0;
    double number = 0.0;
};

struct FbxNode {
    QString name;
    QVector<FbxValue> props;
    QVector<FbxNode> children;
};

QByteArray inflateFbxArray(const QByteArray& compressed, qsizetype uncompressedSize) {
    QByteArray out;
    out.resize(uncompressedSize);
    uLongf destLen = static_cast<uLongf>(uncompressedSize);
    const int result = ::uncompress(reinterpret_cast<Bytef*>(out.data()), &destLen,
                                    reinterpret_cast<const Bytef*>(compressed.constData()),
                                    static_cast<uLong>(compressed.size()));
    if (result != Z_OK) throw std::runtime_error("FBX compressed array inflate failed");
    out.resize(static_cast<qsizetype>(destLen));
    return out;
}

FbxValue readFbxProperty(const QByteArray& data, qsizetype& pos) {
    FbxValue value;
    const char type = char(readU8(data, pos));
    switch (type) {
    case 'Y': value.integer = static_cast<qint16>(readU16(data, pos)); break;
    case 'C': value.integer = readU8(data, pos) ? 1 : 0; break;
    case 'I': value.integer = static_cast<qint32>(readU32(data, pos)); break;
    case 'F': value.number = readF32(data, pos); break;
    case 'D': value.number = readF64(data, pos); break;
    case 'L': value.integer = static_cast<qint64>(readU64(data, pos)); break;
    case 'S':
    case 'R': {
        const uint32_t len = readU32(data, pos);
        if (pos + qsizetype(len) > data.size()) throw std::runtime_error("FBX string read overflow");
        const QByteArray bytes = data.mid(pos, len);
        pos += len;
        value.str = QString::fromUtf8(bytes);
        break;
    }
    case 'd':
    case 'f':
    case 'i':
    case 'l':
    case 'b': {
        const uint32_t count = readU32(data, pos);
        const uint32_t encoding = readU32(data, pos);
        const uint32_t compressedLen = readU32(data, pos);
        const int elemSize = (type == 'd' || type == 'l') ? 8 : 4;
        QByteArray arrayBytes;
        if (encoding == 0) {
            const qsizetype rawLen = qsizetype(count) * elemSize;
            const qsizetype storedLen = compressedLen > 0 ? qsizetype(compressedLen) : rawLen;
            if (pos + storedLen > data.size()) throw std::runtime_error("FBX array read overflow");
            arrayBytes = data.mid(pos, storedLen);
            pos += storedLen;
        } else {
            if (pos + qsizetype(compressedLen) > data.size()) throw std::runtime_error("FBX compressed array read overflow");
            arrayBytes = inflateFbxArray(data.mid(pos, compressedLen), qsizetype(count) * elemSize);
            pos += compressedLen;
        }
        qsizetype ap = 0;
        if (type == 'd' || type == 'f') {
            value.doubles.reserve(static_cast<int>(count));
            for (uint32_t i = 0; i < count; ++i) value.doubles.push_back(type == 'd' ? readF64(arrayBytes, ap) : double(readF32(arrayBytes, ap)));
        } else {
            value.ints.reserve(static_cast<int>(count));
            for (uint32_t i = 0; i < count; ++i) value.ints.push_back(static_cast<int>(type == 'l' ? readU64(arrayBytes, ap) : readU32(arrayBytes, ap)));
        }
        break;
    }
    default:
        throw std::runtime_error("Unsupported FBX binary property type");
    }
    return value;
}

FbxNode readFbxNode(const QByteArray& data, qsizetype& pos, bool largeOffsets) {
    const uint64_t endOffset = largeOffsets ? readU64(data, pos) : readU32(data, pos);
    const uint64_t numProps = largeOffsets ? readU64(data, pos) : readU32(data, pos);
    const uint64_t propListLen = largeOffsets ? readU64(data, pos) : readU32(data, pos);
    Q_UNUSED(propListLen);
    const uint8_t nameLen = readU8(data, pos);

    FbxNode node;
    if (endOffset == 0 && numProps == 0 && nameLen == 0) return node;
    if (pos + nameLen > data.size()) throw std::runtime_error("FBX node name read overflow");
    node.name = QString::fromLatin1(data.constData() + pos, nameLen);
    pos += nameLen;

    node.props.reserve(static_cast<int>(std::min<uint64_t>(numProps, 1024)));
    for (uint64_t i = 0; i < numProps; ++i) node.props.push_back(readFbxProperty(data, pos));

    while (pos < qsizetype(endOffset)) {
        const qsizetype before = pos;
        FbxNode child = readFbxNode(data, pos, largeOffsets);
        if (child.name.isEmpty() && pos == before + (largeOffsets ? 25 : 13)) break;
        if (!child.name.isEmpty()) node.children.push_back(std::move(child));
        if (pos <= before) break;
    }
    pos = std::min<qsizetype>(qsizetype(endOffset), data.size());
    return node;
}

QVector<FbxNode> readFbxRootNodes(const QByteArray& data, int version) {
    QVector<FbxNode> roots;
    if (!data.startsWith("Kaydara FBX Binary") || data.size() < 27) return roots;
    qsizetype pos = 27;
    const bool largeOffsets = version >= 7500;
    while (pos < data.size() - (largeOffsets ? 25 : 13)) {
        const qsizetype before = pos;
        FbxNode n = readFbxNode(data, pos, largeOffsets);
        if (n.name.isEmpty()) break;
        roots.push_back(std::move(n));
        if (pos <= before) break;
    }
    return roots;
}

const FbxNode* findChild(const FbxNode& node, const QString& name) {
    for (const auto& c : node.children) if (c.name == name) return &c;
    return nullptr;
}

QVector<const FbxNode*> findChildren(const FbxNode& node, const QString& name) {
    QVector<const FbxNode*> out;
    for (const auto& c : node.children) if (c.name == name) out.push_back(&c);
    return out;
}

QString nodeString(const FbxNode* node, const QString& fallback = QString()) {
    if (!node || node->props.isEmpty()) return fallback;
    return node->props.first().str.isEmpty() ? fallback : node->props.first().str;
}

QVector3D normalFromLayer(const FbxNode* layer, int controlIndex, int polygonIndex, int polygonVertexIndex) {
    if (!layer) return QVector3D();
    const FbxNode* normalsNode = findChild(*layer, "Normals");
    if (!normalsNode || normalsNode->props.isEmpty()) return QVector3D();
    const QVector<double>& normals = normalsNode->props.first().doubles;
    if (normals.isEmpty()) return QVector3D();

    const QVector<int> normalIndex = [&]() -> QVector<int> {
        const FbxNode* n = findChild(*layer, "NormalsIndex");
        return (n && !n->props.isEmpty()) ? n->props.first().ints : QVector<int>();
    }();
    const QString mapping = nodeString(findChild(*layer, "MappingInformationType"), "ByPolygonVertex");
    const QString reference = nodeString(findChild(*layer, "ReferenceInformationType"), "Direct");

    int idx = polygonVertexIndex;
    if (mapping.contains("Vert", Qt::CaseInsensitive) && !mapping.contains("PolygonVertex", Qt::CaseInsensitive)) idx = controlIndex;
    else if (mapping == "ByPolygon") idx = polygonIndex;
    else if (mapping == "AllSame") idx = 0;
    if (reference.contains("Index", Qt::CaseInsensitive) && idx >= 0 && idx < normalIndex.size()) idx = normalIndex[idx];

    const int base = idx * 3;
    if (base < 0 || base + 2 >= normals.size()) return QVector3D();
    return QVector3D(float(normals[base]), float(normals[base + 1]), float(normals[base + 2])).normalized();
}

QVector2D uvFromLayer(const FbxNode* layer, int controlIndex, int polygonIndex, int polygonVertexIndex) {
    if (!layer) return QVector2D();
    const FbxNode* uvNode = findChild(*layer, "UV");
    if (!uvNode || uvNode->props.isEmpty()) return QVector2D();
    const QVector<double>& uvs = uvNode->props.first().doubles;
    if (uvs.isEmpty()) return QVector2D();

    QVector<int> uvIndex;
    if (const FbxNode* n = findChild(*layer, "UVIndex"); n && !n->props.isEmpty()) uvIndex = n->props.first().ints;
    const QString mapping = nodeString(findChild(*layer, "MappingInformationType"), "ByPolygonVertex");
    const QString reference = nodeString(findChild(*layer, "ReferenceInformationType"), "IndexToDirect");

    int idx = polygonVertexIndex;
    if (mapping.contains("Vert", Qt::CaseInsensitive) && !mapping.contains("PolygonVertex", Qt::CaseInsensitive)) idx = controlIndex;
    else if (mapping == "ByPolygon") idx = polygonIndex;
    else if (mapping == "AllSame") idx = 0;
    if ((reference.contains("Index", Qt::CaseInsensitive) || !uvIndex.isEmpty()) && idx >= 0 && idx < uvIndex.size()) idx = uvIndex[idx];

    const int base = idx * 2;
    if (base < 0 || base + 1 >= uvs.size()) return QVector2D();
    return QVector2D(float(uvs[base]), float(uvs[base + 1]));
}

QString geometryName(const FbxNode& geom, int index) {
    if (geom.props.size() > 1 && !geom.props[1].str.isEmpty()) return cleanFbxName(geom.props[1].str);
    return QString("FBX_Geometry_%1").arg(index);
}

SceneData importBinaryFbxFallback(const QString& filePath, int version) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) throw std::runtime_error("FBX 文件无法读取");
    const QByteArray data = file.readAll();
    QVector<FbxNode> roots = readFbxRootNodes(data, version);

    const FbxNode* objects = nullptr;
    for (const auto& n : roots) if (n.name == "Objects") { objects = &n; break; }
    if (!objects) throw std::runtime_error("FBX fallback 未找到 Objects 节点");

    SceneData scene;
    scene.sourcePath = filePath;
    const QVector<const FbxNode*> geometries = findChildren(*objects, "Geometry");
    int geometryIndex = 0;

    for (const FbxNode* geom : geometries) {
        const FbxNode* verticesNode = findChild(*geom, "Vertices");
        const FbxNode* polyNode = findChild(*geom, "PolygonVertexIndex");
        if (!verticesNode || verticesNode->props.isEmpty() || !polyNode || polyNode->props.isEmpty()) continue;
        const QVector<double>& control = verticesNode->props.first().doubles;
        const QVector<int>& indices = polyNode->props.first().ints;
        if (control.size() < 3 || indices.isEmpty()) continue;

        const FbxNode* normalLayer = nullptr;
        const auto normalLayers = findChildren(*geom, "LayerElementNormal");
        if (!normalLayers.isEmpty()) normalLayer = normalLayers.first();
        const FbxNode* uvLayer = nullptr;
        const auto uvLayers = findChildren(*geom, "LayerElementUV");
        if (!uvLayers.isEmpty()) uvLayer = uvLayers.first();

        MeshData mesh;
        mesh.name = geometryName(*geom, geometryIndex++);
        mesh.hasUV = uvLayer != nullptr;
        mesh.hasNormals = normalLayer != nullptr;
        mesh.hasVertexColor = false;

        struct Pv { int controlIndex; int polygonVertexIndex; };
        QVector<Pv> polygon;
        int polygonIndex = 0;
        int polygonVertexIndex = 0;

        auto addVertex = [&](const Pv& pv) -> int {
            const int base = pv.controlIndex * 3;
            Vertex v;
            if (base >= 0 && base + 2 < control.size()) {
                v.position = QVector3D(float(control[base]), float(control[base + 1]), float(control[base + 2]));
            }
            v.normal = normalFromLayer(normalLayer, pv.controlIndex, polygonIndex, pv.polygonVertexIndex);
            v.uv = uvFromLayer(uvLayer, pv.controlIndex, polygonIndex, pv.polygonVertexIndex);
            v.originalIndex = pv.controlIndex;
            mesh.vertices.push_back(v);
            return mesh.vertices.size() - 1;
        };

        for (int raw : indices) {
            int ci = raw;
            bool endPoly = false;
            if (raw < 0) {
                ci = -raw - 1;
                endPoly = true;
            }
            polygon.push_back({ci, polygonVertexIndex++});
            if (endPoly) {
                if (polygon.size() >= 3) {
                    for (int i = 1; i + 1 < polygon.size(); ++i) {
                        Face f;
                        f.i0 = addVertex(polygon[0]);
                        f.i1 = addVertex(polygon[i]);
                        f.i2 = addVertex(polygon[i + 1]);
                        f.materialIndex = 0;
                        f.smoothingMask = 0xffffffffu;
                        mesh.faces.push_back(f);
                    }
                }
                polygon.clear();
                ++polygonIndex;
            }
        }

        if (!mesh.vertices.isEmpty() && !mesh.faces.isEmpty()) {
            computeMissingNormals(mesh);
            scene.meshes.push_back(mesh);
        }
    }

    if (scene.meshes.isEmpty()) throw std::runtime_error("FBX fallback 未读取到可用网格数据");
    return scene;
}

} // namespace

SceneData ModelIO::importModel(const QString& filePath) {
    const bool isFbx = QFileInfo(filePath).suffix().toLower() == "fbx";
    const int fbxVersion = isFbx ? detectFbxVersion(filePath) : 0;

    const unsigned int attempts[] = {
        aiProcess_Triangulate | aiProcess_SortByPType | aiProcess_ImproveCacheLocality,
        aiProcess_Triangulate | aiProcess_SortByPType,
        aiProcess_Triangulate,
        aiProcess_Triangulate | aiProcess_GenSmoothNormals | aiProcess_SortByPType
    };
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

    if ((!ai || !ai->HasMeshes()) && isFbx && fbxVersion >= 7000) {
        try {
            return importBinaryFbxFallback(filePath, fbxVersion);
        } catch (const std::exception& fallbackError) {
            errors << QString("内置 FBX 2014/2015 fallback 失败：%1").arg(fallbackError.what());
        }
    }

    if (!ai || !ai->HasMeshes()) {
        QString versionInfo;
        if (isFbx && fbxVersion > 0) {
            versionInfo = QString("\n检测到 FBX 版本：%1。已尝试 Assimp 兼容导入与内置 Binary FBX fallback。").arg(fbxVersion);
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
