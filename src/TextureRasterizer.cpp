#include "TextureRasterizer.h"

#include <QByteArray>
#include <QFile>
#include <QImage>
#include <QVector>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <stdexcept>

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

void TextureRasterizer::bakeAOTexture(const SceneData& scene, const QString& filePath, const BakeSettings& settings) {
    const int res = settings.textureResolution;
    QVector<float> values(res * res, 0.0f);
    QVector<uchar> mask(res * res, 0);
    bool anyUV = false;

    for (const auto& mesh : scene.meshes) {
        if (!mesh.hasUV) continue;
        anyUV = true;
        for (const auto& face : mesh.faces) {
            const Vertex& v0 = mesh.vertices[face.i0];
            const Vertex& v1 = mesh.vertices[face.i1];
            const Vertex& v2 = mesh.vertices[face.i2];
            const QPointF p0(v0.uv.x() * (res - 1), (1.0f - v0.uv.y()) * (res - 1));
            const QPointF p1(v1.uv.x() * (res - 1), (1.0f - v1.uv.y()) * (res - 1));
            const QPointF p2(v2.uv.x() * (res - 1), (1.0f - v2.uv.y()) * (res - 1));

            const int minX = std::max(0, int(std::floor(std::min({p0.x(), p1.x(), p2.x()}))));
            const int maxX = std::min(res - 1, int(std::ceil(std::max({p0.x(), p1.x(), p2.x()}))));
            const int minY = std::max(0, int(std::floor(std::min({p0.y(), p1.y(), p2.y()}))));
            const int maxY = std::min(res - 1, int(std::ceil(std::max({p0.y(), p1.y(), p2.y()}))));
            const float area = edge(p0, p1, p2);
            if (std::abs(area) < 1e-6f) continue;

            for (int y = minY; y <= maxY; ++y) {
                for (int x = minX; x <= maxX; ++x) {
                    const QPointF p(x + 0.5, y + 0.5);
                    const float w0 = edge(p1, p2, p) / area;
                    const float w1 = edge(p2, p0, p) / area;
                    const float w2 = 1.0f - w0 - w1;
                    if (w0 >= -1e-4f && w1 >= -1e-4f && w2 >= -1e-4f) {
                        const float ao = std::clamp(w0 * v0.color.redF() + w1 * v1.color.redF() + w2 * v2.color.redF(), 0.0f, 1.0f);
                        const int index = y * res + x;
                        values[index] = ao;
                        mask[index] = 255;
                    }
                }
            }
        }
    }

    if (!anyUV) {
        throw std::runtime_error("没有可用于贴图烘焙的 UV 数据。");
    }

    dilateUvIslands(values, mask, res, 4);

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
