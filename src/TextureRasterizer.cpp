#include "TextureRasterizer.h"
#include <QImage>
#include <QFile>
#include <QtMath>
#include <algorithm>
#include <stdexcept>

static float edge(const QPointF& a, const QPointF& b, const QPointF& c) {
    return float((c.x() - a.x()) * (b.y() - a.y()) - (c.y() - a.y()) * (b.x() - a.x()));
}

static bool saveTgaRgb24(const QImage& src, const QString& filePath) {
    QImage img = src.convertToFormat(QImage::Format_RGB888);
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) return false;

    unsigned char header[18] = {};
    header[2] = 2;
    header[12] = static_cast<unsigned char>(img.width() & 0xff);
    header[13] = static_cast<unsigned char>((img.width() >> 8) & 0xff);
    header[14] = static_cast<unsigned char>(img.height() & 0xff);
    header[15] = static_cast<unsigned char>((img.height() >> 8) & 0xff);
    header[16] = 24;
    header[17] = 0x20;
    if (file.write(reinterpret_cast<const char*>(header), 18) != 18) return false;

    QByteArray row;
    row.resize(img.width() * 3);
    for (int y = 0; y < img.height(); ++y) {
        const uchar* line = img.constScanLine(y);
        for (int x = 0; x < img.width(); ++x) {
            row[x * 3 + 0] = static_cast<char>(line[x * 3 + 2]);
            row[x * 3 + 1] = static_cast<char>(line[x * 3 + 1]);
            row[x * 3 + 2] = static_cast<char>(line[x * 3 + 0]);
        }
        if (file.write(row) != row.size()) return false;
    }
    return true;
}

static bool saveTga(const QImage& image, const QString& filePath) {
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

    for (int y = 0; y < src.height(); ++y) {
        for (int x = 0; x < src.width(); ++x) {
            const QColor c = src.pixelColor(x, y);
            unsigned char bgr[3] = {
                static_cast<unsigned char>(c.blue()),
                static_cast<unsigned char>(c.green()),
                static_cast<unsigned char>(c.red())
            };
            if (file.write(reinterpret_cast<const char*>(bgr), 3) != 3) return false;
        }
    }
    return true;
}

void TextureRasterizer::bakeAOTexture(const SceneData& scene, const QString& filePath, const BakeSettings& settings) {
    const int res = settings.textureResolution;
    QImage img(res, res, QImage::Format_RGB888);
    img.fill(Qt::black);
    QImage mask(res, res, QImage::Format_Grayscale8);
    mask.fill(0);
    bool anyUV = false;

    for (const auto& mesh : scene.meshes) {
        if (!mesh.hasUV) continue;
        anyUV = true;
        for (const auto& face : mesh.faces) {
            const Vertex& v0 = mesh.vertices[face.i0];
            const Vertex& v1 = mesh.vertices[face.i1];
            const Vertex& v2 = mesh.vertices[face.i2];
            QPointF p0(v0.uv.x() * (res - 1), (1.0f - v0.uv.y()) * (res - 1));
            QPointF p1(v1.uv.x() * (res - 1), (1.0f - v1.uv.y()) * (res - 1));
            QPointF p2(v2.uv.x() * (res - 1), (1.0f - v2.uv.y()) * (res - 1));

            const int minX = std::max(0, int(std::floor(std::min({p0.x(), p1.x(), p2.x()}))));
            const int maxX = std::min(res - 1, int(std::ceil(std::max({p0.x(), p1.x(), p2.x()}))));
            const int minY = std::max(0, int(std::floor(std::min({p0.y(), p1.y(), p2.y()}))));
            const int maxY = std::min(res - 1, int(std::ceil(std::max({p0.y(), p1.y(), p2.y()}))));
            const float area = edge(p0, p1, p2);
            if (std::abs(area) < 1e-6f) continue;

            for (int y = minY; y <= maxY; ++y) {
                for (int x = minX; x <= maxX; ++x) {
                    QPointF p(x + 0.5, y + 0.5);
                    const float w0 = edge(p1, p2, p) / area;
                    const float w1 = edge(p2, p0, p) / area;
                    const float w2 = 1.0f - w0 - w1;
                    if (w0 >= -1e-4f && w1 >= -1e-4f && w2 >= -1e-4f) {
                        const float c = w0 * v0.color.redF() + w1 * v1.color.redF() + w2 * v2.color.redF();
                        const int g = std::clamp(int(c * 255.0f), 0, 255);
                        img.setPixelColor(x, y, QColor(g, g, g));
                        mask.setPixelColor(x, y, QColor(255, 255, 255));
                    }
                }
            }
        }
    }

    if (!anyUV) throw std::runtime_error("没有可用于贴图烘焙的 UV 数据。");

    // 简单 UV 岛扩边，减少边缘黑线。
    for (int pass = 0; pass < 4; ++pass) {
        QImage next = img;
        QImage nextMask = mask;
        for (int y = 1; y < res - 1; ++y) {
            for (int x = 1; x < res - 1; ++x) {
                if (mask.pixelColor(x, y).red() > 0) continue;
                int count = 0, sum = 0;
                for (int oy = -1; oy <= 1; ++oy) {
                    for (int ox = -1; ox <= 1; ++ox) {
                        if (mask.pixelColor(x + ox, y + oy).red() > 0) {
                            sum += img.pixelColor(x + ox, y + oy).red();
                            ++count;
                        }
                    }
                }
                if (count > 0) {
                    const int g = sum / count;
                    next.setPixelColor(x, y, QColor(g, g, g));
                    nextMask.setPixelColor(x, y, QColor(255, 255, 255));
                }
            }
        }
        img = next;
        mask = nextMask;
    }

    const QString fmt = settings.textureFormat.toUpper();
    bool saved = false;
    if (fmt == "TGA") {
        saved = saveTga(img, filePath);
    } else if (fmt == "JPG") {
        saved = img.save(filePath, "JPG");
    } else {
        saved = img.save(filePath, fmt.toUtf8().constData());
    }
    if (!saved) {
        throw std::runtime_error("AO 贴图保存失败。当前支持 TGA / PNG / JPG / BMP，请检查输出目录权限和文件名。");
    }
}
