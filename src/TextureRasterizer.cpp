#include "TextureRasterizer.h"

#include <QByteArray>
#include <QFile>
#include <QImage>
#include <QVector>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <atomic>
#include <thread>
#include <numeric>
#include <QVector3D>

namespace {

float edge(const QPointF& a, const QPointF& b, const QPointF& c) {
    return float((c.x() - a.x()) * (b.y() - a.y()) - (c.y() - a.y()) * (b.x() - a.x()));
}

void appendU8(QByteArray& out, std::uint8_t value) {
    out.append(char(value));
}

void appendLe16(QByteArray& out, std::uint16_t value) {
    out.append(char(value & 0xFF));
    out.append(char((value >> 8) & 0xFF));
}

void appendLe32(QByteArray& out, std::uint32_t value) {
    out.append(char(value & 0xFF));
    out.append(char((value >> 8) & 0xFF));
    out.append(char((value >> 16) & 0xFF));
    out.append(char((value >> 24) & 0xFF));
}

void appendLe64(QByteArray& out, std::uint64_t value) {
    for (int i = 0; i < 8; ++i) {
        out.append(char((value >> (i * 8)) & 0xFF));
    }
}

void appendFloat32(QByteArray& out, float value) {
    std::uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    appendLe32(out, bits);
}

void appendString(QByteArray& out, const char* text) {
    out.append(text);
    out.append('\0');
}

void appendAttribute(QByteArray& header, const char* name, const char* type, const QByteArray& value) {
    appendString(header, name);
    appendString(header, type);
    appendLe32(header, static_cast<std::uint32_t>(value.size()));
    header.append(value);
}

void appendIfdEntry(QByteArray& out, std::uint16_t tag, std::uint16_t type, std::uint32_t count, std::uint32_t valueOrOffset) {
    appendLe16(out, tag);
    appendLe16(out, type);
    appendLe32(out, count);
    appendLe32(out, valueOrOffset);
}

std::uint8_t toByte(float v) {
    const float clamped = std::clamp(v, 0.0f, 1.0f);
    return static_cast<std::uint8_t>(std::lround(clamped * 255.0f));
}

std::uint16_t toUInt16(float v) {
    const float clamped = std::clamp(v, 0.0f, 1.0f);
    return static_cast<std::uint16_t>(std::lround(clamped * 65535.0f));
}

QImage makeGray8Image(const QVector<float>& values, int width, int height) {
    QImage image(width, height, QImage::Format_Grayscale8);
    for (int y = 0; y < height; ++y) {
        auto* line = image.scanLine(y);
        for (int x = 0; x < width; ++x) {
            line[x] = static_cast<uchar>(toByte(values[y * width + x]));
        }
    }
    return image;
}

QImage makeGray16Image(const QVector<float>& values, int width, int height) {
    QImage image(width, height, QImage::Format_Grayscale16);
    for (int y = 0; y < height; ++y) {
        auto* line = reinterpret_cast<std::uint16_t*>(image.scanLine(y));
        for (int x = 0; x < width; ++x) {
            line[x] = toUInt16(values[y * width + x]);
        }
    }
    return image;
}

bool saveTga(const QImage& image, const QString& filePath) {
    QImage src = image.convertToFormat(QImage::Format_RGB888);
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) return false;

    unsigned char header[18] = {};
    header[2] = 2;
    header[12] = static_cast<unsigned char>(src.width() & 0xFF);
    header[13] = static_cast<unsigned char>((src.width() >> 8) & 0xFF);
    header[14] = static_cast<unsigned char>(src.height() & 0xFF);
    header[15] = static_cast<unsigned char>((src.height() >> 8) & 0xFF);
    header[16] = 24;
    header[17] = 0x20;
    if (file.write(reinterpret_cast<const char*>(header), 18) != 18) return false;

    QByteArray row;
    row.resize(src.width() * 3);
    for (int y = 0; y < src.height(); ++y) {
        const uchar* line = src.constScanLine(y);
        for (int x = 0; x < src.width(); ++x) {
            row[x * 3 + 0] = static_cast<char>(line[x * 3 + 2]);
            row[x * 3 + 1] = static_cast<char>(line[x * 3 + 1]);
            row[x * 3 + 2] = static_cast<char>(line[x * 3 + 0]);
        }
        if (file.write(row) != row.size()) return false;
    }
    return true;
}

bool saveTiffGray(const QVector<float>& values, int width, int height, const QString& filePath, const QString& bitDepth) {
    int bitsPerSample = 8;
    int bytesPerSample = 1;
    std::uint32_t sampleFormat = 1; // unsigned integer

    if (bitDepth == QStringLiteral("16-bit")) {
        bitsPerSample = 16;
        bytesPerSample = 2;
    } else if (bitDepth == QStringLiteral("32-bit float")) {
        bitsPerSample = 32;
        bytesPerSample = 4;
        sampleFormat = 3; // IEEE floating point
    }

    QByteArray pixelData;
    pixelData.reserve(width * height * bytesPerSample);
    for (float value : values) {
        if (bitsPerSample == 8) {
            pixelData.append(char(toByte(value)));
        } else if (bitsPerSample == 16) {
            appendLe16(pixelData, toUInt16(value));
        } else {
            appendFloat32(pixelData, std::clamp(value, 0.0f, 1.0f));
        }
    }

    const std::uint32_t dataOffset = 8;
    const std::uint32_t ifdOffset = dataOffset + static_cast<std::uint32_t>(pixelData.size());
    const std::uint16_t entryCount = 10;

    QByteArray out;
    out.reserve(int(ifdOffset) + 2 + entryCount * 12 + 4);
    out.append('I');
    out.append('I');
    appendLe16(out, 42);
    appendLe32(out, ifdOffset);
    out.append(pixelData);

    appendLe16(out, entryCount);
    appendIfdEntry(out, 256, 4, 1, static_cast<std::uint32_t>(width));
    appendIfdEntry(out, 257, 4, 1, static_cast<std::uint32_t>(height));
    appendIfdEntry(out, 258, 3, 1, static_cast<std::uint32_t>(bitsPerSample));
    appendIfdEntry(out, 259, 3, 1, 1);
    appendIfdEntry(out, 262, 3, 1, 1);
    appendIfdEntry(out, 273, 4, 1, dataOffset);
    appendIfdEntry(out, 277, 3, 1, 1);
    appendIfdEntry(out, 278, 4, 1, static_cast<std::uint32_t>(height));
    appendIfdEntry(out, 279, 4, 1, static_cast<std::uint32_t>(pixelData.size()));
    appendIfdEntry(out, 339, 3, 1, sampleFormat);
    appendLe32(out, 0);

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) return false;
    return file.write(out) == out.size();
}

bool saveExrFloatRgb(const QVector<float>& values, int width, int height, const QString& filePath) {
    if (width <= 0 || height <= 0) return false;

    QByteArray header;
    appendLe32(header, 20000630); // OpenEXR magic number
    appendLe32(header, 2);        // version 2, no flags

    QByteArray channels;
    for (const char* channelName : {"B", "G", "R"}) {
        appendString(channels, channelName);
        appendLe32(channels, 2); // FLOAT
        appendU8(channels, 0);   // pLinear
        appendU8(channels, 0);
        appendU8(channels, 0);
        appendU8(channels, 0);
        appendLe32(channels, 1); // xSampling
        appendLe32(channels, 1); // ySampling
    }
    appendU8(channels, 0);
    appendAttribute(header, "channels", "chlist", channels);

    QByteArray compression;
    appendU8(compression, 0); // NO_COMPRESSION
    appendAttribute(header, "compression", "compression", compression);

    QByteArray dataWindow;
    appendLe32(dataWindow, 0);
    appendLe32(dataWindow, 0);
    appendLe32(dataWindow, static_cast<std::uint32_t>(width - 1));
    appendLe32(dataWindow, static_cast<std::uint32_t>(height - 1));
    appendAttribute(header, "dataWindow", "box2i", dataWindow);
    appendAttribute(header, "displayWindow", "box2i", dataWindow);

    QByteArray lineOrder;
    appendU8(lineOrder, 0); // INCREASING_Y
    appendAttribute(header, "lineOrder", "lineOrder", lineOrder);

    QByteArray pixelAspectRatio;
    appendFloat32(pixelAspectRatio, 1.0f);
    appendAttribute(header, "pixelAspectRatio", "float", pixelAspectRatio);

    QByteArray screenWindowCenter;
    appendFloat32(screenWindowCenter, 0.0f);
    appendFloat32(screenWindowCenter, 0.0f);
    appendAttribute(header, "screenWindowCenter", "v2f", screenWindowCenter);

    QByteArray screenWindowWidth;
    appendFloat32(screenWindowWidth, 1.0f);
    appendAttribute(header, "screenWindowWidth", "float", screenWindowWidth);

    appendU8(header, 0); // end of header

    const std::uint64_t rowPixelBytes = static_cast<std::uint64_t>(width) * 3ULL * 4ULL;
    const std::uint64_t chunkBytes = 8ULL + rowPixelBytes;
    std::uint64_t chunkOffset = static_cast<std::uint64_t>(header.size()) + static_cast<std::uint64_t>(height) * 8ULL;

    QByteArray out;
    out.reserve(header.size() + int(std::min<std::uint64_t>(static_cast<std::uint64_t>(height) * chunkBytes + static_cast<std::uint64_t>(height) * 8ULL, 256ULL * 1024ULL * 1024ULL)));
    out.append(header);

    for (int y = 0; y < height; ++y) {
        appendLe64(out, chunkOffset);
        chunkOffset += chunkBytes;
    }

    for (int y = 0; y < height; ++y) {
        appendLe32(out, static_cast<std::uint32_t>(y));
        appendLe32(out, static_cast<std::uint32_t>(rowPixelBytes));
        for (int channel = 0; channel < 3; ++channel) {
            Q_UNUSED(channel);
            for (int x = 0; x < width; ++x) {
                appendFloat32(out, std::clamp(values[y * width + x], 0.0f, 1.0f));
            }
        }
    }

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) return false;
    return file.write(out) == out.size();
}

void dilateUvIslands(QVector<float>& values, QVector<uchar>& mask, int resolution, int passes) {
    for (int pass = 0; pass < passes; ++pass) {
        QVector<float> nextValues = values;
        QVector<uchar> nextMask = mask;
        for (int y = 1; y < resolution - 1; ++y) {
            for (int x = 1; x < resolution - 1; ++x) {
                const int index = y * resolution + x;
                if (mask[index] > 0) continue;

                int count = 0;
                float sum = 0.0f;
                for (int oy = -1; oy <= 1; ++oy) {
                    for (int ox = -1; ox <= 1; ++ox) {
                        const int neighbor = (y + oy) * resolution + (x + ox);
                        if (mask[neighbor] > 0) {
                            sum += values[neighbor];
                            ++count;
                        }
                    }
                }
                if (count > 0) {
                    nextValues[index] = sum / float(count);
                    nextMask[index] = 255;
                }
            }
        }
        values.swap(nextValues);
        mask.swap(nextMask);
    }
}

} // namespace

namespace {
struct RayTriangle {
    QVector3D p0, p1, p2;
    QVector3D n0, n1, n2;
    QVector3D geometricNormal;
    QVector3D bmin, bmax, centroid;
};
struct RayNode {
    QVector3D bmin, bmax;
    int left=-1, right=-1, start=0, count=0;
};
struct PixelHit { int tri=-1; float b1=0.0f, b2=0.0f; };

float component(const QVector3D& v, int axis) { return axis==0 ? v.x() : (axis==1 ? v.y() : v.z()); }
QVector3D min3(const QVector3D& a,const QVector3D& b) { return {std::min(a.x(),b.x()),std::min(a.y(),b.y()),std::min(a.z(),b.z())}; }
QVector3D max3(const QVector3D& a,const QVector3D& b) { return {std::max(a.x(),b.x()),std::max(a.y(),b.y()),std::max(a.z(),b.z())}; }

class TextureBvh {
public:
    explicit TextureBvh(const QVector<RayTriangle>& t): tris(t) {
        indices.resize(tris.size()); std::iota(indices.begin(),indices.end(),0);
        if (!indices.isEmpty()) { nodes.reserve(indices.size()*2); build(0,indices.size()); }
    }
    bool hit(const QVector3D& o,const QVector3D& d,float maxT) const {
        if(nodes.isEmpty()) return false;
        int stack[256]; int top=0; stack[top++]=0;
        while(top) {
            const RayNode& n=nodes[stack[--top]];
            if(!boxHit(o,d,n.bmin,n.bmax,maxT)) continue;
            if(n.left<0 && n.right<0) {
                for(int i=0;i<n.count;++i) if(triHit(o,d,tris[indices[n.start+i]],maxT)) return true;
            } else {
                if(n.left>=0 && top<254) stack[top++]=n.left;
                if(n.right>=0 && top<255) stack[top++]=n.right;
            }
        }
        return false;
    }
private:
    QVector<RayTriangle> tris; QVector<int> indices; QVector<RayNode> nodes;
    int build(int start,int count) {
        RayNode n; n.start=start; n.count=count;
        const float inf=std::numeric_limits<float>::max();
        n.bmin={inf,inf,inf}; n.bmax={-inf,-inf,-inf};
        QVector3D cmin(inf,inf,inf),cmax(-inf,-inf,-inf);
        for(int i=start;i<start+count;++i) { const auto& t=tris[indices[i]]; n.bmin=min3(n.bmin,t.bmin); n.bmax=max3(n.bmax,t.bmax); cmin=min3(cmin,t.centroid); cmax=max3(cmax,t.centroid); }
        const int id=nodes.size(); nodes.push_back(n);
        if(count<=8) return id;
        const QVector3D e=cmax-cmin; int axis=(e.y()>e.x() && e.y()>=e.z())?1:((e.z()>e.x() && e.z()>=e.y())?2:0);
        if(component(e,axis)<1e-7f) return id;
        const int mid=start+count/2;
        std::nth_element(indices.begin()+start,indices.begin()+mid,indices.begin()+start+count,[&](int a,int b){return component(tris[a].centroid,axis)<component(tris[b].centroid,axis);});
        nodes[id].left=build(start,mid-start); nodes[id].right=build(mid,start+count-mid); nodes[id].count=0; return id;
    }
    static bool boxHit(const QVector3D& o,const QVector3D& d,const QVector3D& mn,const QVector3D& mx,float maxT) {
        float lo=0.0f,hi=maxT;
        for(int a=0;a<3;++a) { const float ov=component(o,a),dv=component(d,a),mnv=component(mn,a)-1e-5f,mxv=component(mx,a)+1e-5f;
            if(std::abs(dv)<1e-9f) {if(ov<mnv||ov>mxv)return false;continue;}
            float t0=(mnv-ov)/dv,t1=(mxv-ov)/dv;if(t0>t1)std::swap(t0,t1);lo=std::max(lo,t0);hi=std::min(hi,t1);if(hi<lo)return false;
        } return hi>=0.0f;
    }
    static bool triHit(const QVector3D& o,const QVector3D& d,const RayTriangle& t,float maxT) {
        const QVector3D e1=t.p1-t.p0,e2=t.p2-t.p0,p=QVector3D::crossProduct(d,e2); const float det=QVector3D::dotProduct(e1,p);
        if(std::abs(det)<1e-7f)return false; const float inv=1.0f/det; const QVector3D tv=o-t.p0; const float u=QVector3D::dotProduct(tv,p)*inv;if(u<0||u>1)return false;
        const QVector3D q=QVector3D::crossProduct(tv,e1);const float v=QVector3D::dotProduct(d,q)*inv;if(v<0||u+v>1)return false;
        const float dist=QVector3D::dotProduct(e2,q)*inv;return dist>1e-4f&&dist<maxT;
    }
};

QVector3D toWorld(const QVector3D& sample,const QVector3D& normal) {
    QVector3D n=normal.normalized(); if(n.lengthSquared()<1e-8f)n={0,0,1};
    const QVector3D helper=std::abs(QVector3D::dotProduct(n,{0,0,1}))>0.95f?QVector3D(1,0,0):QVector3D(0,0,1);
    const QVector3D tangent=QVector3D::crossProduct(helper,n).normalized(); const QVector3D bitangent=QVector3D::crossProduct(n,tangent).normalized();
    return (tangent*sample.x()+bitangent*sample.y()+n*sample.z()).normalized();
}
QVector<QVector3D> makeSamples(int count) {
    QVector<QVector3D> out; count=std::clamp(count,1,4096);out.reserve(count);const float golden=2.39996322972865332f;
    for(int i=0;i<count;++i){const float u=(i+0.5f)/float(count);const float z=std::sqrt(1.0f-u);const float r=std::sqrt(u);const float a=golden*float(i);out.push_back({std::cos(a)*r,std::sin(a)*r,z});}return out;
}
float remapVisibility(float v,const BakeSettings& s) {
    v=std::clamp(v,0.0f,1.0f);v=std::pow(v,1.0f/std::max(s.transitionContrast,0.2f));const float sm=std::clamp(s.transitionSmoothness,0.0f,1.0f);const float smooth=v*v*(3.0f-2.0f*v);v=v*(1-sm)+smooth*sm;
    v=1.0f-(1.0f-v)*std::max(0.0f,s.aoStrength);return std::clamp(s.darkest+(s.brightest-s.darkest)*std::clamp(v,0.0f,1.0f),0.0f,1.0f);
}
} // namespace

void TextureRasterizer::bakeAOTexture(const SceneData& scene, const QString& filePath, const BakeSettings& settings) {
    const int res = std::clamp(settings.textureResolution, 16, 8192);
    const qint64 pixelCount = qint64(res) * qint64(res);
    if (pixelCount > 20LL*1024*1024) throw std::runtime_error("贴图分辨率过高：当前逐纹素 CPU 路径预计会占用过多内存，请降低分辨率后重试。");
    QVector<float> values(int(pixelCount), 1.0f);
    QVector<uchar> mask(int(pixelCount), 0);
    QVector<PixelHit> pixelHits; pixelHits.resize(int(pixelCount));
    QVector<RayTriangle> triangles;
    QVector<QVector<int>> faceRayIndices; faceRayIndices.resize(scene.meshes.size());
    triangles.reserve(scene.faceCount());
    bool anyUV=false;

    // Build the occluder scene from all valid faces; UVs are only required for target triangles.
    for(int mi=0; mi<scene.meshes.size(); ++mi) {
      const auto& mesh=scene.meshes[mi]; faceRayIndices[mi].fill(-1, mesh.faces.size());
      for(int fi=0; fi<mesh.faces.size(); ++fi) { const auto& f=mesh.faces[fi];
        if(f.i0<0||f.i1<0||f.i2<0||f.i0>=mesh.vertices.size()||f.i1>=mesh.vertices.size()||f.i2>=mesh.vertices.size()) continue;
        const Vertex& a=mesh.vertices[f.i0];const Vertex& b=mesh.vertices[f.i1];const Vertex& c=mesh.vertices[f.i2];
        RayTriangle t; t.p0=a.position;t.p1=b.position;t.p2=c.position;
        t.geometricNormal=QVector3D::crossProduct(t.p1-t.p0,t.p2-t.p0).normalized();if(t.geometricNormal.lengthSquared()<1e-8f)continue;
        t.n0=a.normal.lengthSquared()>1e-8f?a.normal.normalized():t.geometricNormal;t.n1=b.normal.lengthSquared()>1e-8f?b.normal.normalized():t.geometricNormal;t.n2=c.normal.lengthSquared()>1e-8f?c.normal.normalized():t.geometricNormal;
        t.bmin=min3(min3(t.p0,t.p1),t.p2);t.bmax=max3(max3(t.p0,t.p1),t.p2);t.centroid=(t.p0+t.p1+t.p2)/3.0f;faceRayIndices[mi][fi]=triangles.size();triangles.push_back(t);
      }
    }
    if(triangles.isEmpty()) throw std::runtime_error("场景中没有有效三角面，无法计算 AO。");

    // Rasterize UV triangles to per-texel surface records (triangle + barycentric coordinates).
    for(int mi=0; mi<scene.meshes.size(); ++mi) {
        const auto& mesh=scene.meshes[mi];
        if(!mesh.hasUV) continue;
        anyUV=true;
        for(int fi=0; fi<mesh.faces.size(); ++fi) {
            const auto& face=mesh.faces[fi];
            if(face.i0<0||face.i1<0||face.i2<0||face.i0>=mesh.vertices.size()||face.i1>=mesh.vertices.size()||face.i2>=mesh.vertices.size()) {continue;}
            const Vertex& v0=mesh.vertices[face.i0];const Vertex& v1=mesh.vertices[face.i1];const Vertex& v2=mesh.vertices[face.i2];
            const QPointF p0(v0.uv.x()*(res-1),(1.0f-v0.uv.y())*(res-1)),p1(v1.uv.x()*(res-1),(1.0f-v1.uv.y())*(res-1)),p2(v2.uv.x()*(res-1),(1.0f-v2.uv.y())*(res-1));
            const float area=edge(p0,p1,p2);if(std::abs(area)<1e-7f)continue;
            const int minX=std::max(0,int(std::floor(std::min({p0.x(),p1.x(),p2.x()}))));const int maxX=std::min(res-1,int(std::ceil(std::max({p0.x(),p1.x(),p2.x()}))));
            const int minY=std::max(0,int(std::floor(std::min({p0.y(),p1.y(),p2.y()}))));const int maxY=std::min(res-1,int(std::ceil(std::max({p0.y(),p1.y(),p2.y()}))));
            const int rayTri=faceRayIndices[mi][fi];
            if(rayTri<0)continue;
            for(int y=minY;y<=maxY;++y)for(int x=minX;x<=maxX;++x){const QPointF p(x+0.5,y+0.5);const float b0=edge(p1,p2,p)/area,b1=edge(p2,p0,p)/area,b2=1.0f-b0-b1;
                if(b0>=-1e-4f&&b1>=-1e-4f&&b2>=-1e-4f){const int idx=y*res+x;pixelHits[idx]={rayTri,b1,b2};mask[idx]=255;}}
        }
    }
    if(!anyUV)throw std::runtime_error("没有可用于贴图烘焙的 UV 数据。");

    TextureBvh bvh(triangles);
    const QVector<QVector3D> samples=makeSamples(settings.sampleCount);
    const float maxDistance=std::max(0.0001f,settings.rayDistance);
    QVector3D sceneMin(std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max());
    QVector3D sceneMax(-std::numeric_limits<float>::max(), -std::numeric_limits<float>::max(), -std::numeric_limits<float>::max());
    for (const RayTriangle& tri : triangles) { sceneMin = min3(sceneMin, tri.bmin); sceneMax = max3(sceneMax, tri.bmax); }
    const float sceneScale = std::max((sceneMax - sceneMin).length(), 1.0e-3f);
    const float rayBias = sceneScale * std::clamp(settings.rayOriginOffset, 1.0e-7f, 0.05f);
    std::atomic<int> nextRow{0};
    const unsigned int hw=std::max(1u,std::thread::hardware_concurrency());
    const int threadCount=settings.multiThreadedBake?int(std::min<unsigned int>(hw,16u)):1;
    auto worker=[&](){
        while(true){const int y=nextRow.fetch_add(1);if(y>=res)break;
            for(int x=0;x<res;++x){const int idx=y*res+x;if(!mask[idx])continue;const PixelHit& ph=pixelHits[idx];if(ph.tri<0||ph.tri>=triangles.size())continue;
                const RayTriangle& t=triangles[ph.tri];const float b1=ph.b1,b2=ph.b2,b0=1.0f-b1-b2;
                const QVector3D pos=t.p0*b0+t.p1*b1+t.p2*b2;QVector3D normal=(t.n0*b0+t.n1*b1+t.n2*b2).normalized();if(normal.lengthSquared()<1e-8f)normal=t.geometricNormal;
                if(QVector3D::dotProduct(normal,t.geometricNormal)<0.0f)normal=-normal;
                const QVector3D origin=pos+normal*rayBias;int blocked=0;
                for(const QVector3D& sample:samples){const QVector3D dir=toWorld(sample,normal);if(bvh.hit(origin,dir,maxDistance))++blocked;}
                const float visibility=1.0f-float(blocked)/float(samples.size());values[idx]=remapVisibility(visibility,settings);
            }
        }
    };
    QVector<std::thread> workers;workers.reserve(threadCount);for(int i=0;i<threadCount;++i)workers.emplace_back(worker);for(auto& t:workers)t.join();
    dilateUvIslands(values,mask,res,std::clamp(settings.uvPaddingPixels, 0, 64));

    const QString fmt = settings.textureFormat.toUpper();
    const QString bitDepth = settings.textureBitDepth;
    bool saved = false;

    if (fmt == QStringLiteral("TGA")) {
        saved = saveTga(makeGray8Image(values, res, res), filePath);
    } else if (fmt == QStringLiteral("JPG") || fmt == QStringLiteral("JPEG")) {
        saved = makeGray8Image(values, res, res).convertToFormat(QImage::Format_RGB888).save(filePath, "JPG");
    } else if (fmt == QStringLiteral("BMP")) {
        saved = makeGray8Image(values, res, res).convertToFormat(QImage::Format_RGB888).save(filePath, "BMP");
    } else if (fmt == QStringLiteral("PNG")) {
        saved = (bitDepth == QStringLiteral("16-bit") || bitDepth == QStringLiteral("32-bit float"))
            ? makeGray16Image(values, res, res).save(filePath, "PNG")
            : makeGray8Image(values, res, res).save(filePath, "PNG");
    } else if (fmt == QStringLiteral("TIFF") || fmt == QStringLiteral("TIF")) {
        saved = saveTiffGray(values, res, res, filePath, bitDepth);
    } else if (fmt == QStringLiteral("EXR")) {
        saved = saveExrFloatRgb(values, res, res, filePath);
    } else {
        saved = makeGray8Image(values, res, res).save(filePath, fmt.toUtf8().constData());
    }

    if (!saved) {
        throw std::runtime_error("AO 贴图保存失败。当前支持 TGA / PNG / JPG / BMP / TIFF / EXR；不兼容位深会自动纠正为该格式可写入的最高稳定位深，请检查输出目录权限和文件名。");
    }
}
