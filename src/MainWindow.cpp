#include "MainWindow.h"
#include "ModelIO.h"
#include "TextureRasterizer.h"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QCloseEvent>
#include <QColorDialog>
#include <QCoreApplication>
#include <QDateTime>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QProgressDialog>
#include <QRadioButton>
#include <QButtonGroup>
#include <QGuiApplication>
#include <QInputDialog>
#include <QList>
#include <QRegularExpression>
#include <QSettings>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTableWidget>
#include <QHeaderView>
#include <QToolBar>
#include <QUrl>
#include <QVBoxLayout>
#include <QAction>
#include <QAbstractItemView>
#include <QVariant>
#include <QVector3D>
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {
constexpr auto kOrgName = "AO_Baker_Pro";
constexpr auto kAppName = "AO_Baker_Pro_V2";

QString normalizedTextureExtension(QString format) {
    format = format.trimmed().toLower();
    if (format == QStringLiteral("jpeg")) return QStringLiteral("jpg");
    if (format == QStringLiteral("tif")) return QStringLiteral("tiff");
    return format.isEmpty() ? QStringLiteral("png") : format;
}

QStringList compatibleBitDepthsForFormat(const QString& format) {
    const QString fmt = normalizedTextureExtension(format).toUpper();
    if (fmt == QStringLiteral("EXR")) return {QStringLiteral("32-bit float")};
    if (fmt == QStringLiteral("TIFF")) return {QStringLiteral("8-bit"), QStringLiteral("16-bit"), QStringLiteral("32-bit float")};
    if (fmt == QStringLiteral("PNG")) return {QStringLiteral("8-bit"), QStringLiteral("16-bit")};
    return {QStringLiteral("8-bit")};
}

QString formatHintText(const QString& format, const QString& bitDepth) {
    const QString fmt = normalizedTextureExtension(format).toUpper();
    if (fmt == QStringLiteral("EXR")) return QStringLiteral("EXR 将固定输出 32-bit float RGB AO，适合后期合成与高动态范围流程。");
    if (fmt == QStringLiteral("TIFF")) return QStringLiteral("TIFF 支持 8-bit / 16-bit / 32-bit float，可作为高位深通用交换格式。");
    if (fmt == QStringLiteral("PNG") && bitDepth == QStringLiteral("16-bit")) return QStringLiteral("PNG 当前按 16-bit 灰度输出，适合保留更细腻 AO 渐变。");
    if (fmt == QStringLiteral("PNG")) return QStringLiteral("PNG 当前按 8-bit 灰度输出；如选择 32-bit float 会自动纠正为 16-bit PNG。");
    return QStringLiteral("当前格式将自动纠正为 8-bit RGB 输出，避免因位深不兼容导致导出失败。");
}

QString makeSafeOutputToken(QString text, const QString& fallback) {
    text = text.trimmed();
    text.replace(QRegularExpression(QStringLiteral("[\\/:*?\"<>|]")), "_");
    text.replace(QRegularExpression(QStringLiteral("\\s+")), "_");
    return text.isEmpty() ? fallback : text;
}

QString meshOutputToken(const MeshData& mesh, int meshIndex) {
    return makeSafeOutputToken(mesh.name, QString("Mesh_%1").arg(meshIndex + 1));
}

QString materialOutputToken(int materialIndex) {
    return QString("Material_%1").arg(materialIndex + 1);
}

QString udimOutputToken(int udimTile) {
    return QString("UDIM_%1").arg(udimTile);
}

QString renderOutputStem(QString pattern, const QString& sourcePath, const QString& meshToken = QString(), const QString& materialToken = QString(), const QString& udimToken = QString()) {
    if (pattern.trimmed().isEmpty()) {
        pattern = QStringLiteral("{model}");
    }
    QString model = QFileInfo(sourcePath).completeBaseName();
    if (model.trimmed().isEmpty()) model = QStringLiteral("AO_Bake");
    pattern.replace(QStringLiteral("{model}"), model);
    pattern.replace(QStringLiteral("{mesh}"), meshToken);
    pattern.replace(QStringLiteral("{material}"), materialToken);
    pattern.replace(QStringLiteral("{udim}"), udimToken);
    pattern.replace(QStringLiteral("{date}"), QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd")));
    pattern.replace(QStringLiteral("{time}"), QDateTime::currentDateTime().toString(QStringLiteral("hhmmss")));
    return makeSafeOutputToken(pattern, model);
}

QString buildTextureOutputPath(const QString& outputDir, const QString& pattern, const QString& sourcePath,
                               const QString& fallbackSuffix, const QString& ext,
                               const QString& meshToken = QString(), const QString& materialToken = QString(), const QString& udimToken = QString()) {
    QString stem = renderOutputStem(pattern, sourcePath, meshToken, materialToken, udimToken);
    const bool hasPattern = !pattern.trimmed().isEmpty();
    const bool splitContext = !meshToken.isEmpty() || !materialToken.isEmpty() || !udimToken.isEmpty();
    const bool hasSplitToken = pattern.contains(QStringLiteral("{mesh}"))
        || pattern.contains(QStringLiteral("{material}"))
        || pattern.contains(QStringLiteral("{udim}"));
    if (!hasPattern || (splitContext && !hasSplitToken)) {
        stem += fallbackSuffix;
    }
    stem = makeSafeOutputToken(stem, QStringLiteral("AO_Bake"));
    return outputDir + "/" + stem + "." + ext;
}

QVector<int> collectUvMaterialIndices(const SceneData& scene) {
    QVector<int> materialIndices;
    for (const auto& mesh : scene.meshes) {
        if (!mesh.hasUV) continue;
        for (const auto& face : mesh.faces) {
            if (!materialIndices.contains(face.materialIndex)) {
                materialIndices.push_back(face.materialIndex);
            }
        }
    }
    std::sort(materialIndices.begin(), materialIndices.end());
    return materialIndices;
}

int udimTileForUv(const QVector2D& uv) {
    const int u = int(std::floor(uv.x()));
    const int v = int(std::floor(uv.y()));
    if (u < 0 || v < 0 || u > 9) return -1;
    return 1001 + u + v * 10;
}

int udimTileForFace(const MeshData& mesh, const Face& face) {
    if (face.i0 < 0 || face.i0 >= mesh.vertices.size()
        || face.i1 < 0 || face.i1 >= mesh.vertices.size()
        || face.i2 < 0 || face.i2 >= mesh.vertices.size()) {
        return -1;
    }
    const int t0 = udimTileForUv(mesh.vertices[face.i0].uv);
    const int t1 = udimTileForUv(mesh.vertices[face.i1].uv);
    const int t2 = udimTileForUv(mesh.vertices[face.i2].uv);
    if (t0 < 0 || t1 < 0 || t2 < 0) return -1;
    if (t0 == t1 && t1 == t2) return t0;
    const float u = (mesh.vertices[face.i0].uv.x() + mesh.vertices[face.i1].uv.x() + mesh.vertices[face.i2].uv.x()) / 3.0f;
    const float v = (mesh.vertices[face.i0].uv.y() + mesh.vertices[face.i1].uv.y() + mesh.vertices[face.i2].uv.y()) / 3.0f;
    return udimTileForUv(QVector2D(u, v));
}

QVector<int> collectUdimTiles(const SceneData& scene) {
    QVector<int> tiles;
    for (const auto& mesh : scene.meshes) {
        if (!mesh.hasUV) continue;
        for (const auto& face : mesh.faces) {
            const int tile = udimTileForFace(mesh, face);
            if (tile >= 1001 && !tiles.contains(tile)) {
                tiles.push_back(tile);
            }
        }
    }
    std::sort(tiles.begin(), tiles.end());
    return tiles;
}

Vertex normalizeVertexToUdimTile(Vertex vertex, int udimTile) {
    const int zeroBased = udimTile - 1001;
    const int uTile = zeroBased % 10;
    const int vTile = zeroBased / 10;
    vertex.uv.setX(vertex.uv.x() - float(uTile));
    vertex.uv.setY(vertex.uv.y() - float(vTile));
    return vertex;
}

SceneData buildSceneForMaterial(const SceneData& scene, int materialIndex) {
    SceneData filteredScene;
    filteredScene.sourcePath = scene.sourcePath;

    for (const auto& mesh : scene.meshes) {
        if (!mesh.hasUV || mesh.vertices.isEmpty()) continue;

        MeshData filteredMesh;
        filteredMesh.name = mesh.name;
        filteredMesh.hasUV = true;
        filteredMesh.hasNormals = mesh.hasNormals;
        filteredMesh.hasVertexColor = mesh.hasVertexColor;

        std::vector<int> remap(mesh.vertices.size(), -1);
        auto mapVertex = [&](int sourceIndex) {
            if (sourceIndex < 0 || sourceIndex >= mesh.vertices.size()) return -1;
            if (remap[size_t(sourceIndex)] >= 0) return remap[size_t(sourceIndex)];
            const int newIndex = filteredMesh.vertices.size();
            filteredMesh.vertices.push_back(mesh.vertices[sourceIndex]);
            remap[size_t(sourceIndex)] = newIndex;
            return newIndex;
        };

        for (const auto& face : mesh.faces) {
            if (face.materialIndex != materialIndex) continue;
            Face newFace = face;
            newFace.i0 = mapVertex(face.i0);
            newFace.i1 = mapVertex(face.i1);
            newFace.i2 = mapVertex(face.i2);
            if (newFace.i0 < 0 || newFace.i1 < 0 || newFace.i2 < 0) continue;
            filteredMesh.faces.push_back(newFace);
        }

        if (!filteredMesh.faces.isEmpty()) {
            filteredScene.meshes.push_back(filteredMesh);
        }
    }

    return filteredScene;
}

SceneData buildSceneForUdimTile(const SceneData& scene, int udimTile) {
    SceneData filteredScene;
    filteredScene.sourcePath = scene.sourcePath;

    for (const auto& mesh : scene.meshes) {
        if (!mesh.hasUV || mesh.vertices.isEmpty()) continue;

        MeshData filteredMesh;
        filteredMesh.name = mesh.name;
        filteredMesh.hasUV = true;
        filteredMesh.hasNormals = mesh.hasNormals;
        filteredMesh.hasVertexColor = mesh.hasVertexColor;

        std::vector<int> remap(mesh.vertices.size(), -1);
        auto mapVertex = [&](int sourceIndex) {
            if (sourceIndex < 0 || sourceIndex >= mesh.vertices.size()) return -1;
            if (remap[size_t(sourceIndex)] >= 0) return remap[size_t(sourceIndex)];
            const int newIndex = filteredMesh.vertices.size();
            filteredMesh.vertices.push_back(normalizeVertexToUdimTile(mesh.vertices[sourceIndex], udimTile));
            remap[size_t(sourceIndex)] = newIndex;
            return newIndex;
        };

        for (const auto& face : mesh.faces) {
            if (udimTileForFace(mesh, face) != udimTile) continue;
            Face newFace = face;
            newFace.i0 = mapVertex(face.i0);
            newFace.i1 = mapVertex(face.i1);
            newFace.i2 = mapVertex(face.i2);
            if (newFace.i0 < 0 || newFace.i1 < 0 || newFace.i2 < 0) continue;
            filteredMesh.faces.push_back(newFace);
        }

        if (!filteredMesh.faces.isEmpty()) {
            filteredScene.meshes.push_back(filteredMesh);
        }
    }

    return filteredScene;
}
}

SliderRow::SliderRow(float min, float max, float value, int decimals, QWidget* parent)
    : QWidget(parent), m_min(min), m_max(max), m_decimals(decimals), m_scale(int(std::pow(10, decimals))) {
    m_slider = new QSlider(Qt::Horizontal, this);
    m_slider->setRange(int(min * m_scale), int(max * m_scale));
    m_slider->setValue(int(value * m_scale));
    m_edit = new QLineEdit(QString::number(value, 'f', decimals), this);
    m_edit->setFixedWidth(68);
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_slider, 1);
    layout->addWidget(m_edit);
    connect(m_slider, &QSlider::valueChanged, this, &SliderRow::fromSlider);
    connect(m_edit, &QLineEdit::editingFinished, this, &SliderRow::fromEdit);
}

float SliderRow::value() const { return float(m_slider->value()) / float(m_scale); }

void SliderRow::fromSlider(int raw) {
    const float v = float(raw) / float(m_scale);
    m_edit->setText(QString::number(v, 'f', m_decimals));
    emit valueChanged(v);
}

void SliderRow::fromEdit() {
    float v = m_edit->text().toFloat();
    v = std::max(m_min, std::min(m_max, v));
    m_slider->setValue(int(v * m_scale));
    m_edit->setText(QString::number(v, 'f', m_decimals));
    emit valueChanged(v);
}

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent), m_outputDir(QDir::currentPath() + "/AO_Bake_Output") {
    setWindowTitle("AO Baker Pro V2");
    resize(1280, 820);
    buildUi();
    applyStyle();
    loadAppSettings();
    updateWindowTitle();
    updateStatusSummary();
}

void MainWindow::closeEvent(QCloseEvent* event) {
    if (!confirmLoseUnsavedChanges("退出软件")) {
        event->ignore();
        return;
    }
    saveAppSettings();
    event->accept();
}

QWidget* MainWindow::group(const QString& title, QWidget* child) {
    auto* box = new QGroupBox(title, this);
    auto* layout = new QVBoxLayout(box);
    layout->addWidget(child);
    return box;
}

void MainWindow::buildMenus() {
    auto* fileMenu = menuBar()->addMenu("文件(&F)");
    fileMenu->addAction("新建场景", this, &MainWindow::newScene, QKeySequence::New);
    fileMenu->addAction("打开项目", this, &MainWindow::loadProject, QKeySequence::Open);
    fileMenu->addAction("打开", this, &MainWindow::importModel);
    fileMenu->addAction("导入", this, &MainWindow::importModel);

    m_recentFilesMenu = fileMenu->addMenu("最近打开");
    updateRecentFilesMenu();

    fileMenu->addSeparator();
    fileMenu->addAction("保存项目", this, &MainWindow::saveProject, QKeySequence::Save);
    fileMenu->addAction("另存为项目", this, &MainWindow::saveProjectAs, QKeySequence::SaveAs);
    fileMenu->addAction("保存", this, [this](){
        if (m_scene.empty()) { QMessageBox::warning(this, "缺少模型", "请先导入模型。"); return; }
        const QString path = QFileDialog::getSaveFileName(this, "保存模型", outputBaseName("_saved.glb"), "GLB (*.glb);;FBX (*.fbx);;OBJ (*.obj)");
        if (path.isEmpty()) return;
        const QString ext = QFileInfo(path).suffix().toLower();
        const QString fmt = ext == "fbx" ? "fbx" : (ext == "obj" ? "obj" : "glb2");
        try {
            ModelIO::exportModel(m_scene, path, fmt);
            addRecentFile(path);
            log("模型已保存：" + path);
        } catch (const std::exception& e) {
            QMessageBox::critical(this, "保存失败", e.what());
        }
    });
    fileMenu->addAction("导出", this, [this](){
        if (m_scene.empty()) { QMessageBox::warning(this, "缺少模型", "请先导入模型。"); return; }
        const QString path = QFileDialog::getSaveFileName(this, "导出模型", outputBaseName("_export.glb"), "GLB (*.glb);;FBX (*.fbx);;OBJ (*.obj)");
        if (path.isEmpty()) return;
        const QString ext = QFileInfo(path).suffix().toLower();
        const QString fmt = ext == "fbx" ? "fbx" : (ext == "obj" ? "obj" : "glb2");
        try {
            ModelIO::exportModel(m_scene, path, fmt);
            addRecentFile(path);
            log("模型已导出：" + path);
        } catch (const std::exception& e) {
            QMessageBox::critical(this, "导出失败", e.what());
        }
    });
    fileMenu->addAction("导出 AO 贴图", this, &MainWindow::bakeTexture);
    fileMenu->addAction("批量导入 / 批量烘焙 / 批量导出", this, &MainWindow::batchProcessModels);
    fileMenu->addSeparator();
    fileMenu->addAction("首选项", this, &MainWindow::openPreferences);
    fileMenu->addAction("退出", this, [this](){ close(); }, QKeySequence::Quit);

    auto* editMenu = menuBar()->addMenu("编辑(&E)");
    m_undoAction = editMenu->addAction("撤销", this, &MainWindow::undoActionTriggered, QKeySequence::Undo);
    m_redoAction = editMenu->addAction("重做", this, &MainWindow::redoActionTriggered, QKeySequence::Redo);
    m_undoAction->setEnabled(false);
    m_redoAction->setEnabled(false);
    editMenu->addSeparator();
    editMenu->addAction("软件设置", this, &MainWindow::openPreferences);
    auto* perfMenu = editMenu->addMenu("软件性能模式选项");
    perfMenu->addAction("高质量模式", this, [this](){ m_settings.sampleCount = 512; m_settings.adaptiveSampling = false; onSettingsChanged(); log("已切换到高质量模式：512 射线，关闭自适应采样。"); });
    perfMenu->addAction("平衡模式", this, [this](){ m_settings.sampleCount = 256; m_settings.adaptiveSampling = true; onSettingsChanged(); log("已切换到平衡模式：256 射线，自适应采样。"); });
    perfMenu->addAction("快速模式", this, [this](){ m_settings.sampleCount = 96; m_settings.adaptiveSampling = true; onSettingsChanged(); log("已切换到快速模式：96 射线，自适应采样。"); });
    editMenu->addAction("清除顶点色", this, &MainWindow::clearVertexColors);
    editMenu->addAction("重新载入刷新模型", this, [this](){
        if (m_scene.sourcePath.isEmpty()) { QMessageBox::warning(this, "缺少模型", "当前没有可重新载入的模型路径。"); return; }
        try {
            m_scene = ModelIO::importModel(m_scene.sourcePath);
            refreshSceneInfo();
            if (m_viewport) { m_viewport->setScene(&m_scene); m_viewport->setBakeSettings(m_settings); }
            markDirty();
            log("模型已重新载入刷新。");
        } catch (const std::exception& e) {
            QMessageBox::critical(this, "重新载入失败", e.what());
        }
    });

    auto* viewMenu = menuBar()->addMenu("视图(&V)");
    const QStringList views = {"透视图", "正面正交视图", "左侧视图", "右侧视图", "背后视图", "顶部视图", "底部视图"};
    for (const QString& v : views) {
        viewMenu->addAction(v, this, [this, v](){ if (m_viewport) m_viewport->setCameraView(v); log("已切换视图：" + v); });
    }
    viewMenu->addSeparator();
    viewMenu->addAction("全屏显示模型", this, [this](){ if (m_viewport) m_viewport->focusModel(); log("已全屏适配显示模型。"); });
    auto* toggleMenu = viewMenu->addMenu("切换功能");
    auto* wireAction = toggleMenu->addAction("显示 / 隐藏线框");
    wireAction->setCheckable(true); wireAction->setChecked(true);
    connect(wireAction, &QAction::toggled, this, [this](bool checked){ if (m_viewport) m_viewport->setWireframeVisible(checked); updateStatusSummary(); });
    auto* vcAction = toggleMenu->addAction("显示 / 隐藏顶点色");
    vcAction->setCheckable(true); vcAction->setChecked(true);
    connect(vcAction, &QAction::toggled, this, [this](bool checked){ if (m_viewport) m_viewport->setUseVertexColor(checked); updateStatusSummary(); });
    auto* paintAction = toggleMenu->addAction("启用 / 关闭顶点色绘制");
    paintAction->setCheckable(true);
    connect(paintAction, &QAction::toggled, this, [this](bool checked){ if (m_viewport) m_viewport->setVertexPaintEnabled(checked); updateStatusSummary(); });

    auto* toolMenu = menuBar()->addMenu("工具(&T)");
    toolMenu->addAction("烘焙 AO 到贴图", this, &MainWindow::bakeTexture);
    toolMenu->addAction("烘焙 AO 到顶点色", this, &MainWindow::bakeVertex);
    toolMenu->addAction("批量处理队列", this, &MainWindow::batchProcessModels);
    toolMenu->addAction("笔刷相关的详细设置", this, [this](){ QMessageBox::information(this, "笔刷详细设置", "已支持画笔 / 擦除 / 填充桶、半径、不透明度、软笔刷、忽略背面、锁定通道、图层不透明度。高级笔刷曲线和压力感应接口已预留。"); });
    toolMenu->addAction("颜色填充模式详细设置", this, [this](){ if (m_viewport) m_viewport->setPaintMode(2); QMessageBox::information(this, "颜色填充模式", "已切换到填充桶模式。可在左侧顶点色绘制面板选择颜色、混合模式和图层不透明度。"); });
    toolMenu->addAction("UV集的详细选择和设置", this, [this](){ QMessageBox::information(this, "UV 集设置", "当前保留导入模型的 UV0。UV1/多 UV 集选择、复制、重命名和导出映射设置已预留。"); });
    toolMenu->addAction("法线 / 光滑组工具", this, &MainWindow::unifySmoothingGroups);

    auto* helpMenu = menuBar()->addMenu("帮助(&H)");
    helpMenu->addAction("用户手册", this, [this](){ QMessageBox::information(this, "用户手册", "AO_Baker_Pro_V2 开发文档需求已接入当前版本，后续可继续补充正式在线手册。 "); });
    helpMenu->addAction("快捷键列表", this, [this](){ QMessageBox::information(this, "快捷键", "F：聚焦模型\nCtrl+Z：撤销\nCtrl+Shift+Z / Ctrl+Y：重做\nAlt+左键：旋转\nAlt+中键：平移\nAlt+右键 / 滚轮：缩放"); });
    helpMenu->addAction("软件版本", this, [this](){ QMessageBox::information(this, "软件版本", QString("AO Baker Pro V2\n版本：%1").arg(QCoreApplication::applicationVersion())); });
    helpMenu->addAction("软件更新网址路径", this, [this](){ QMessageBox::information(this, "软件更新", "更新网址暂未配置。后续可填写 GitHub Release / 官网下载路径。"); });
    helpMenu->addAction("邮箱信息和联系方式", this, [this](){ QMessageBox::information(this, "联系方式", "邮箱和技术支持联系方式暂未配置，当前为占位信息。"); });
}

void MainWindow::buildUi() {
    buildMenus();
    auto* toolbar = addToolBar("工具");
    toolbar->addAction("新建场景", this, &MainWindow::newScene);
    toolbar->addAction("导入模型", this, &MainWindow::importModel);
    toolbar->addAction("保存项目", this, &MainWindow::saveProject);
    toolbar->addAction("选择输出目录", this, &MainWindow::chooseOutputDir);
    toolbar->addSeparator();
    toolbar->addAction("烘焙AO到顶点色", this, &MainWindow::bakeVertex);
    toolbar->addAction("烘焙AO到贴图", this, &MainWindow::bakeTexture);
    toolbar->addAction("批量处理", this, &MainWindow::batchProcessModels);
    toolbar->addAction("修复面片朝向", this, &MainWindow::repairFaceOrientation);
    toolbar->addAction("统一光滑组", this, &MainWindow::unifySmoothingGroups);
    toolbar->addAction("清空场景", this, &MainWindow::clearScene);

    auto* root = new QSplitter(Qt::Horizontal, this);
    setCentralWidget(root);

    auto* paintColumn = new QWidget(this);
    auto* paintColumnLayout = new QVBoxLayout(paintColumn);
    paintColumnLayout->setContentsMargins(4, 4, 4, 4);
    paintColumnLayout->addWidget(buildVertexPaintPanel(), 1);

    auto* left = new QWidget(this);
    auto* leftLayout = new QVBoxLayout(left);
    leftLayout->setContentsMargins(4, 4, 4, 4);
    m_tree = new QTreeWidget(this);
    m_tree->setHeaderLabel("模型");
    connect(m_tree, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem* item, int){
        if (!item || !m_viewport) return;
        const int meshIndex = item->data(0, Qt::UserRole).toInt();
        m_viewport->setSelectedMesh(meshIndex);
        updateStatusSummary();
    });
    m_modelInfo = new QLabel("未导入模型", this);
    m_modelInfo->setWordWrap(true);
    m_uvInfo = new QLabel("UV 通道：未检测", this);
    m_uvInfo->setWordWrap(true);
    m_log = new QTextEdit(this);
    m_log->setReadOnly(true);
    leftLayout->addWidget(group("模型", m_tree), 3);
    leftLayout->addWidget(group("模型信息", m_modelInfo), 2);
    leftLayout->addWidget(group("UV 通道", m_uvInfo), 1);
    leftLayout->addWidget(group("日志", m_log), 2);

    auto* center = new QWidget(this);
    auto* centerLayout = new QVBoxLayout(center);
    m_viewport = new ViewportWidget(this);
    connect(m_viewport, &ViewportWidget::meshSelected, this, &MainWindow::onMeshSelected);
    connect(m_viewport, &ViewportWidget::sceneModified, this, [this](){ markDirty(); refreshSceneInfo(); updateStatusSummary(); });
    connect(m_viewport, &ViewportWidget::undoAvailabilityChanged, this, [this](bool canUndo, bool canRedo){
        if (m_undoAction) m_undoAction->setEnabled(canUndo);
        if (m_redoAction) m_redoAction->setEnabled(canRedo);
    });
    centerLayout->addWidget(m_viewport, 5);
    auto* bakePanel = new QWidget(this);
    auto* bakeLayout = new QHBoxLayout(bakePanel);
    auto* vertexBtn = new QPushButton("烘焙到顶点色", this);
    auto* textureBtn = new QPushButton("烘焙到贴图", this);
    vertexBtn->setMinimumHeight(72);
    textureBtn->setMinimumHeight(72);
    connect(vertexBtn, &QPushButton::clicked, this, &MainWindow::bakeVertex);
    connect(textureBtn, &QPushButton::clicked, this, &MainWindow::bakeTexture);
    bakeLayout->addWidget(vertexBtn);
    bakeLayout->addWidget(textureBtn);
    centerLayout->addWidget(group("烘焙输出", bakePanel), 1);

    auto* right = new QWidget(this);
    auto* rightLayout = new QVBoxLayout(right);
    rightLayout->addWidget(buildSettingsPanel(), 5);
    rightLayout->addWidget(buildOutputPanel(), 2);

    root->addWidget(paintColumn);
    root->addWidget(left);
    root->addWidget(center);
    root->addWidget(right);
    root->setSizes({170, 250, 640, 360});

    m_statusSummary = new QLabel(this);
    statusBar()->addPermanentWidget(m_statusSummary, 1);
    statusBar()->showMessage("就绪");
}

QWidget* MainWindow::buildSettingsPanel() {
    auto* box = new QGroupBox("AO 设置", this);
    auto* form = new QFormLayout(box);
    auto addSlider = [&](const QString& name, float min, float max, float initial, auto setter) {
        auto* row = new SliderRow(min, max, initial, 2, this);
        connect(row, &SliderRow::valueChanged, this, setter);
        form->addRow(name, row);
    };
    addSlider("AO 强度", 0, 3, m_settings.aoStrength, [this](float v){ m_settings.aoStrength = v; onSettingsChanged(); });
    auto* colorRow = new QWidget(this);
    auto* colorLayout = new QHBoxLayout(colorRow);
    colorLayout->setContentsMargins(0,0,0,0);
    m_colorButton = new QPushButton(m_settings.groundBounceColor.name(), this);
    connect(m_colorButton, &QPushButton::clicked, this, &MainWindow::chooseGroundColor);
    auto* ground = new SliderRow(0, 2, m_settings.groundBounceStrength, 2, this);
    connect(ground, &SliderRow::valueChanged, this, [this](float v){ m_settings.groundBounceStrength = v; onSettingsChanged(); });
    colorLayout->addWidget(m_colorButton);
    colorLayout->addWidget(ground, 1);
    form->addRow("地面反光", colorRow);
    addSlider("过渡范围", 0, 1, m_settings.transitionSmoothness, [this](float v){ m_settings.transitionSmoothness = v; onSettingsChanged(); });
    addSlider("过渡对比度", 0.2f, 4, m_settings.transitionContrast, [this](float v){ m_settings.transitionContrast = v; onSettingsChanged(); });
    addSlider("最暗处", 0, 1, m_settings.darkest, [this](float v){ m_settings.darkest = v; onSettingsChanged(); });
    addSlider("最亮处", 0, 1, m_settings.brightest, [this](float v){ m_settings.brightest = v; onSettingsChanged(); });
    auto* samples = new QSpinBox(this);
    samples->setRange(16, 4096);
    samples->setValue(m_settings.sampleCount);
    connect(samples, &QSpinBox::valueChanged, this, [this](int v){ m_settings.sampleCount = v; onSettingsChanged(); });
    form->addRow("射线数量", samples);
    addSlider("最大烘焙距离", 0.1f, 100, m_settings.rayDistance, [this](float v){ m_settings.rayDistance = v; onSettingsChanged(); });
    addSlider("细节保留", 0, 1, m_settings.detailPreserve, [this](float v){ m_settings.detailPreserve = v; onSettingsChanged(); });
    auto* keepSmooth = new QCheckBox("保留光滑组 / 软硬边", this);
    keepSmooth->setChecked(true);
    connect(keepSmooth, &QCheckBox::toggled, this, [this](bool v){ m_settings.keepSmoothingGroups = v; onSettingsChanged(); });
    auto* keepUv = new QCheckBox("保留原始 UV 集和 UV 内容", this);
    keepUv->setChecked(true);
    connect(keepUv, &QCheckBox::toggled, this, [this](bool v){ m_settings.keepUVs = v; onSettingsChanged(); });
    auto* adaptive = new QCheckBox("自适应采样", this);
    adaptive->setChecked(true);
    connect(adaptive, &QCheckBox::toggled, this, [this](bool v){ m_settings.adaptiveSampling = v; onSettingsChanged(); });
    auto* wireframe = new QCheckBox("显示模型网格线框", this);
    wireframe->setChecked(true);
    connect(wireframe, &QCheckBox::toggled, this, [this](bool v){ if (m_viewport) m_viewport->setWireframeVisible(v); updateStatusSummary(); });
    auto* useVertexColor = new QCheckBox("显示顶点色 / AO 结果", this);
    useVertexColor->setChecked(true);
    connect(useVertexColor, &QCheckBox::toggled, this, [this](bool v){ if (m_viewport) m_viewport->setUseVertexColor(v); updateStatusSummary(); });
    auto* materialRow = new QWidget(this);
    auto* materialLayout = new QHBoxLayout(materialRow);
    materialLayout->setContentsMargins(0,0,0,0);
    m_materialColorButton = new QPushButton("#bcc6d2", this);
    connect(m_materialColorButton, &QPushButton::clicked, this, &MainWindow::chooseMaterialColor);
    materialLayout->addWidget(m_materialColorButton);
    form->addRow(keepSmooth);
    form->addRow(keepUv);
    form->addRow(adaptive);
    form->addRow(wireframe);
    form->addRow(useVertexColor);
    form->addRow("默认材质颜色", materialRow);
    return box;
}

QWidget* MainWindow::buildVertexPaintPanel() {
    auto* box = new QGroupBox("顶点绘制", this);
    auto* form = new QFormLayout(box);

    auto* showChannel = new QComboBox(this);
    showChannel->addItems({"顶点色", "AO结果", "默认材质"});
    connect(showChannel, &QComboBox::currentTextChanged, this, [this](const QString& text){
        if (!m_viewport) return;
        m_viewport->setUseVertexColor(text != "默认材质");
        updateStatusSummary();
    });
    form->addRow("显示通道", showChannel);

    auto* paintEnabled = new QCheckBox("启用手动绘制", this);
    connect(paintEnabled, &QCheckBox::toggled, this, [this](bool v){ if (m_viewport) m_viewport->setVertexPaintEnabled(v); updateStatusSummary(); });
    form->addRow(paintEnabled);

    auto* toolRow = new QWidget(this);
    auto* toolLayout = new QHBoxLayout(toolRow);
    toolLayout->setContentsMargins(0,0,0,0);
    auto* brush = new QRadioButton("画笔", this);
    auto* eraser = new QRadioButton("擦除", this);
    auto* bucket = new QRadioButton("填充桶", this);
    brush->setChecked(true);
    auto* toolGroup = new QButtonGroup(this);
    toolGroup->addButton(brush, 0);
    toolGroup->addButton(eraser, 1);
    toolGroup->addButton(bucket, 2);
    connect(toolGroup, &QButtonGroup::idClicked, this, [this](int id){
        if (m_viewport) {
            m_viewport->setPaintEraseMode(id == 1);
            m_viewport->setPaintMode(id);
        }
        updateStatusSummary();
    });
    toolLayout->addWidget(brush);
    toolLayout->addWidget(eraser);
    toolLayout->addWidget(bucket);
    form->addRow("画笔 / 擦除 / 填充", toolRow);

    m_paintColorButton = new QPushButton("#ffffff", this);
    connect(m_paintColorButton, &QPushButton::clicked, this, &MainWindow::choosePaintColor);
    form->addRow("颜色选择", m_paintColorButton);

    auto* opacity = new SliderRow(0, 1, 1, 2, this);
    connect(opacity, &SliderRow::valueChanged, this, [this](float v){ if (m_viewport) m_viewport->setPaintOpacity(v); markDirty(); });
    form->addRow("不透明度", opacity);

    auto* size = new QSpinBox(this);
    size->setRange(1, 300);
    size->setValue(24);
    connect(size, &QSpinBox::valueChanged, this, [this](int v){ if (m_viewport) m_viewport->setPaintRadius(float(v)); markDirty(); });
    form->addRow("笔刷大小", size);

    auto* soft = new QCheckBox("软笔刷 / 边缘衰减", this);
    soft->setChecked(true);
    connect(soft, &QCheckBox::toggled, this, [this](bool v){ if (m_viewport) m_viewport->setPaintSoftBrush(v); markDirty(); });
    form->addRow("笔刷选项", soft);

    m_ignoreBackfacesCheck = new QCheckBox("忽略背面", this);
    m_ignoreBackfacesCheck->setChecked(true);
    connect(m_ignoreBackfacesCheck, &QCheckBox::toggled, this, [this](bool v){ if (m_viewport) m_viewport->setIgnoreBackfaces(v); markDirty(); });
    form->addRow("绘制限制", m_ignoreBackfacesCheck);

    auto* blend = new QComboBox(this);
    blend->addItems({"正常", "正片叠底", "滤色", "叠加", "加亮", "变暗"});
    connect(blend, &QComboBox::currentTextChanged, this, [this](const QString& v){ if (m_viewport) m_viewport->setPaintBlendMode(v); markDirty(); });
    form->addRow("混合模式", blend);

    auto* paletteRow = new QWidget(this);
    auto* paletteLayout = new QHBoxLayout(paletteRow);
    paletteLayout->setContentsMargins(0,0,0,0);
    const QList<QColor> colors = {Qt::white, Qt::black, Qt::red, Qt::green, Qt::blue, Qt::yellow};
    for (const QColor& c : colors) {
        auto* b = new QPushButton(this);
        b->setFixedSize(24, 22);
        b->setStyleSheet(QString("background:%1; border:1px solid #39424c;").arg(c.name()));
        connect(b, &QPushButton::clicked, this, [this, c](){
            if (m_paintColorButton) m_paintColorButton->setText(c.name());
            if (m_viewport) m_viewport->setPaintColor(c);
            markDirty();
        });
        paletteLayout->addWidget(b);
    }
    form->addRow("调色板", paletteRow);

    auto* layerName = new QLabel("Layer 1 - Vertex Color", this);
    auto* newLayer = new QPushButton("新建图层", this);
    connect(newLayer, &QPushButton::clicked, this, [this, layerName](){
        layerName->setText("Layer 2 - Paint Layer");
        log("已新建顶点色绘制图层：Layer 2 - Paint Layer");
        markDirty();
    });
    form->addRow("图层", layerName);
    form->addRow(newLayer);

    auto* fillLayer = new QPushButton("填充当前图层 / 选中Mesh", this);
    connect(fillLayer, &QPushButton::clicked, this, [this](){ if (m_viewport) m_viewport->fillSelectedMesh(); });
    form->addRow("图层操作", fillLayer);

    auto* clearLayer = new QPushButton("清除全部顶点色", this);
    connect(clearLayer, &QPushButton::clicked, this, &MainWindow::clearVertexColors);
    form->addRow(clearLayer);

    auto* layerOpacity = new SliderRow(0, 1, 1, 2, this);
    connect(layerOpacity, &SliderRow::valueChanged, this, [this](float v){ if (m_viewport) m_viewport->setLayerOpacity(v); markDirty(); });
    form->addRow("图层不透明度", layerOpacity);

    auto* lockRow = new QWidget(this);
    auto* lockLayout = new QHBoxLayout(lockRow);
    lockLayout->setContentsMargins(0,0,0,0);
    m_lockRedCheck = new QCheckBox("锁R", this);
    m_lockGreenCheck = new QCheckBox("锁G", this);
    m_lockBlueCheck = new QCheckBox("锁B", this);
    auto updateLocks = [this](){
        if (m_viewport) m_viewport->setLockedChannels(m_lockRedCheck->isChecked(), m_lockGreenCheck->isChecked(), m_lockBlueCheck->isChecked());
        markDirty();
    };
    connect(m_lockRedCheck, &QCheckBox::toggled, this, updateLocks);
    connect(m_lockGreenCheck, &QCheckBox::toggled, this, updateLocks);
    connect(m_lockBlueCheck, &QCheckBox::toggled, this, updateLocks);
    lockLayout->addWidget(m_lockRedCheck);
    lockLayout->addWidget(m_lockGreenCheck);
    lockLayout->addWidget(m_lockBlueCheck);
    form->addRow("锁定通道", lockRow);

    auto* rgbRow = new QWidget(this);
    auto* rgbLayout = new QHBoxLayout(rgbRow);
    rgbLayout->setContentsMargins(0,0,0,0);
    auto* red = new QCheckBox("R", this);
    auto* green = new QCheckBox("G", this);
    auto* blue = new QCheckBox("B", this);
    red->setChecked(true); green->setChecked(true); blue->setChecked(true);
    auto updateChannels = [this, red, green, blue](){
        if (m_viewport) m_viewport->setVisibleChannels(red->isChecked(), green->isChecked(), blue->isChecked());
        markDirty();
    };
    connect(red, &QCheckBox::toggled, this, updateChannels);
    connect(green, &QCheckBox::toggled, this, updateChannels);
    connect(blue, &QCheckBox::toggled, this, updateChannels);
    rgbLayout->addWidget(red);
    rgbLayout->addWidget(green);
    rgbLayout->addWidget(blue);
    form->addRow("RGB显示/隐藏", rgbRow);

    return box;
}

QWidget* MainWindow::buildOutputPanel() {
    auto* box = new QGroupBox("输出设置（贴图）", this);
    auto* form = new QFormLayout(box);
    m_resolution = new QComboBox(this);
    m_resolution->addItems({"512", "1024", "2048", "4096", "8192"});
    m_resolution->setCurrentText("2048");
    connect(m_resolution, &QComboBox::currentTextChanged, this, [this](const QString& v){ m_settings.textureResolution = v.toInt(); markDirty(); });
    m_format = new QComboBox(this);
    m_format->addItems({"TGA", "PNG", "JPG", "BMP", "EXR", "TIFF"});
    m_format->setCurrentText("PNG");
    connect(m_format, &QComboBox::currentTextChanged, this, [this](const QString& v){
        m_settings.textureFormat = v;
        updateOutputFormatUi();
        markDirty();
    });
    m_bitDepth = new QComboBox(this);
    m_bitDepth->addItems({"8-bit", "16-bit", "32-bit float"});
    m_bitDepth->setCurrentText("8-bit");
    connect(m_bitDepth, &QComboBox::currentTextChanged, this, [this](const QString& v){
        m_settings.textureBitDepth = v;
        updateOutputFormatUi();
        markDirty();
    });
    m_splitMode = new QComboBox(this);
    m_splitMode->addItems({"单张AO图", "按Mesh拆分", "按材质拆分", "UDIM拆分"});
    connect(m_splitMode, &QComboBox::currentTextChanged, this, [this](const QString& v){ m_settings.textureSplitMode = v; markDirty(); });
    m_outputNameEdit = new QLineEdit(this);
    m_outputNameEdit->setPlaceholderText("命名模板：{model}_{mesh}_{material}_{udim}_AO，可留空");
    connect(m_outputNameEdit, &QLineEdit::textChanged, this, [this](){ markDirty(); });
    m_formatHint = new QLabel(this);
    m_formatHint->setWordWrap(true);
    m_formatHint->setStyleSheet("color:#f4c36a; background:#1b2128; border:1px solid #39424c; padding:6px; border-radius:4px;");
    m_outputLabel = new QLabel(m_outputDir, this);
    m_outputLabel->setWordWrap(true);
    auto* pick = new QPushButton("选择输出目录", this);
    connect(pick, &QPushButton::clicked, this, &MainWindow::chooseOutputDir);
    auto* openDir = new QPushButton("打开输出目录", this);
    connect(openDir, &QPushButton::clicked, this, [this](){ QDesktopServices::openUrl(QUrl::fromLocalFile(m_outputDir)); });
    form->addRow("贴图分辨率", m_resolution);
    form->addRow("贴图格式", m_format);
    form->addRow("位深度", m_bitDepth);
    form->addRow("格式提示", m_formatHint);
    form->addRow("拆分导出", m_splitMode);
    form->addRow("导出命名", m_outputNameEdit);
    form->addRow("输出路径", m_outputLabel);
    form->addRow(pick);
    auto* batch = new QPushButton("批量导入 / 批量烘焙 / 批量导出", this);
    connect(batch, &QPushButton::clicked, this, &MainWindow::batchProcessModels);
    form->addRow(batch);
    form->addRow(openDir);
    updateOutputFormatUi();
    return box;
}

void MainWindow::updateOutputFormatUi() {
    if (!m_format || !m_bitDepth) return;
    const QString format = m_format->currentText();
    const QString previous = m_bitDepth->currentText();
    const QStringList compatible = compatibleBitDepthsForFormat(format);

    {
        QSignalBlocker blocker(m_bitDepth);
        m_bitDepth->clear();
        m_bitDepth->addItems(compatible);
        const QString chosen = compatible.contains(previous) ? previous : compatible.last();
        m_bitDepth->setCurrentText(chosen);
        m_settings.textureBitDepth = chosen;
    }

    m_settings.textureFormat = format;
    if (m_formatHint) {
        m_formatHint->setText(formatHintText(format, m_settings.textureBitDepth));
    }
}

void MainWindow::applyStyle() {
    setStyleSheet(R"(
        QMainWindow, QWidget { background: #20262d; color: #d7dde5; font-size: 13px; }
        QGroupBox { border: 1px solid #39424c; border-radius: 4px; margin-top: 8px; padding-top: 12px; }
        QGroupBox::title { subcontrol-origin: margin; left: 8px; padding: 0 4px; }
        QLineEdit, QTextEdit, QTreeWidget, QComboBox, QSpinBox { background: #151a20; border: 1px solid #39424c; color: #d7dde5; padding: 4px; }
        QCheckBox, QRadioButton, QLabel { color: #d7dde5; }
        QPushButton { background: #206dd6; border: 0; padding: 8px 12px; border-radius: 4px; color: white; }
        QPushButton:hover { background: #2b7de9; }
        QSlider::groove:horizontal { height: 4px; background: #34404b; }
        QSlider::handle:horizontal { width: 12px; background: #2d8cff; margin: -5px 0; border-radius: 6px; }
        QStatusBar { background: #171c22; border-top: 1px solid #39424c; }
    )");
}

void MainWindow::importModel() {
    if (!confirmLoseUnsavedChanges("导入新模型")) return;
    const QStringList files = QFileDialog::getOpenFileNames(this, "导入模型", QString(), "3D Model (*.obj *.fbx *.gltf *.glb *.dae *.ply *.stl);;OBJ (*.obj);;FBX (*.fbx);;glTF (*.gltf *.glb)");
    if (files.isEmpty()) return;
    try {
        SceneData merged;
        merged.sourcePath = files.join(";");
        for (const QString& path : files) {
            SceneData imported = ModelIO::importModel(path);
            if (merged.meshes.isEmpty()) {
                merged = imported;
            } else {
                for (auto& mesh : imported.meshes) {
                    if (mesh.name.trimmed().isEmpty()) {
                        mesh.name = QFileInfo(path).completeBaseName();
                    } else {
                        mesh.name = QFileInfo(path).completeBaseName() + " / " + mesh.name;
                    }
                    merged.meshes.push_back(mesh);
                }
            }
            addRecentFile(path);
        }
        if (files.size() > 1 && merged.sourcePath.isEmpty()) {
            merged.sourcePath = files.first();
        }
        m_scene = merged;
        refreshSceneInfo();
        m_viewport->setScene(&m_scene);
        m_viewport->setBakeSettings(m_settings);
        m_currentProjectPath.clear();
        markDirty(true);
        log(files.size() == 1 ? ("已导入模型：" + files.first())
                              : QString("已导入 %1 个模型并合并为单一场景。").arg(files.size()));
    } catch (const std::exception& e) {
        QMessageBox::critical(this, "导入失败", e.what());
        log(QString("导入失败：") + e.what());
    }
}

void MainWindow::importRecentModel() {
    auto* action = qobject_cast<QAction*>(sender());
    if (!action) return;
    const QString path = action->data().toString();
    if (path.endsWith(".aobproj", Qt::CaseInsensitive)) {
        loadProjectFromFile(path);
        return;
    }
    if (!QFileInfo::exists(path)) {
        QMessageBox::warning(this, "最近文件不存在", "该文件不存在，已从最近记录中移除。");
        m_recentFiles.removeAll(path);
        updateRecentFilesMenu();
        saveAppSettings();
        return;
    }
    if (!confirmLoseUnsavedChanges("打开最近文件")) return;
    try {
        m_scene = ModelIO::importModel(path);
        refreshSceneInfo();
        m_viewport->setScene(&m_scene);
        m_viewport->setBakeSettings(m_settings);
        addRecentFile(path);
        m_currentProjectPath.clear();
        markDirty(true);
        log("已从最近记录打开模型：" + path);
    } catch (const std::exception& e) {
        QMessageBox::critical(this, "打开失败", e.what());
    }
}

void MainWindow::refreshSceneInfo() {
    m_tree->clear();
    auto* root = new QTreeWidgetItem(QStringList() << QFileInfo(m_scene.sourcePath).fileName());
    root->setData(0, Qt::UserRole, -1);
    for (int i = 0; i < m_scene.meshes.size(); ++i) {
        auto* item = new QTreeWidgetItem(QStringList() << m_scene.meshes[i].name);
        item->setData(0, Qt::UserRole, i);
        root->addChild(item);
    }
    root->setExpanded(true);
    m_tree->addTopLevelItem(root);
    m_modelInfo->setText(QString("文件：%1\n顶点数：%2\n三角面：%3\nMesh：%4")
        .arg(m_scene.sourcePath).arg(m_scene.vertexCount()).arg(m_scene.faceCount()).arg(m_scene.meshes.size()));
    m_uvInfo->setText(QString("UV Mesh：%1/%2\n导入保留 UV、法线、顶点色；FBX 由 Assimp + 内置 fallback 独立解析。")
        .arg(m_scene.uvMeshCount()).arg(m_scene.meshes.size()));
    updateStatusSummary();
}

void MainWindow::chooseOutputDir() {
    const QString dir = QFileDialog::getExistingDirectory(this, "选择输出目录", m_outputDir);
    if (!dir.isEmpty()) {
        m_outputDir = dir;
        m_outputLabel->setText(dir);
        markDirty();
        addRecentFile(dir);
    }
}

void MainWindow::chooseGroundColor() {
    const QColor c = QColorDialog::getColor(m_settings.groundBounceColor, this, "选择地面反光颜色");
    if (c.isValid()) {
        m_settings.groundBounceColor = c;
        m_colorButton->setText(c.name());
        onSettingsChanged();
    }
}

void MainWindow::chooseMaterialColor() {
    const QColor initial = m_materialColorButton ? QColor(m_materialColorButton->text()) : QColor(188, 198, 210);
    const QColor c = QColorDialog::getColor(initial, this, "选择默认材质颜色");
    if (c.isValid()) {
        if (m_materialColorButton) m_materialColorButton->setText(c.name());
        if (m_viewport) m_viewport->setDefaultMaterialColor(c);
        markDirty();
        log("默认材质颜色已切换：" + c.name());
    }
}

void MainWindow::choosePaintColor() {
    const QColor initial = m_paintColorButton ? QColor(m_paintColorButton->text()) : QColor(255, 255, 255);
    const QColor c = QColorDialog::getColor(initial, this, "选择顶点色画笔颜色");
    if (c.isValid()) {
        if (m_paintColorButton) m_paintColorButton->setText(c.name());
        if (m_viewport) m_viewport->setPaintColor(c);
        markDirty();
        log("顶点色画笔颜色已切换：" + c.name());
    }
}

QString MainWindow::outputBaseName(const QString& suffix) const {
    QDir().mkpath(m_outputDir);
    QString baseName = m_outputNameEdit ? m_outputNameEdit->text().trimmed() : QString();
    if (baseName.isEmpty()) {
        baseName = QFileInfo(m_scene.sourcePath).completeBaseName();
    }
    const QRegularExpression invalidChars(QStringLiteral("[\\\\/:*?\"<>|]"));
    baseName.replace(invalidChars, "_");
    return m_outputDir + "/" + baseName + suffix;
}

void MainWindow::runBake(bool texture) {
    if (m_scene.empty()) {
        QMessageBox::warning(this, "缺少模型", "请先导入模型。");
        return;
    }
    QDir().mkpath(m_outputDir);
    QProgressDialog progress("正在烘焙 AO...", "取消", 0, 100, this);
    progress.setWindowModality(Qt::WindowModal);
    progress.show();

    try {
        AOBaker baker;
        connect(&baker, &AOBaker::progress, this, [&](const QString& msg, int percent){
            progress.setValue(percent);
            progress.setLabelText(msg);
            QApplication::processEvents();
            log(msg);
            if (progress.wasCanceled()) throw std::runtime_error("用户取消烘焙");
        });
        baker.bakeToVertexColor(m_scene, m_settings);
        const QString modelOut = outputBaseName(texture ? "_ao_texture.glb" : "_ao_vertex.glb");
        ModelIO::exportModel(m_scene, modelOut, "glb2");
        QString message = "模型已输出：\n" + modelOut;
        m_lastBakeCachePath = modelOut;
        addRecentFile(modelOut);
        if (texture) {
            QString ext = m_settings.textureFormat.toLower();
            if (ext == "jpeg") ext = "jpg";
            const QString outputPattern = m_outputNameEdit ? m_outputNameEdit->text().trimmed() : QString();
            QStringList textureOutputs;
            if (m_settings.textureSplitMode == QStringLiteral("按Mesh拆分")) {
                for (int meshIndex = 0; meshIndex < m_scene.meshes.size(); ++meshIndex) {
                    const auto& mesh = m_scene.meshes[meshIndex];
                    if (!mesh.hasUV) continue;
                    SceneData single;
                    single.sourcePath = m_scene.sourcePath;
                    single.meshes.push_back(mesh);
                    const QString safeMeshName = meshOutputToken(mesh, meshIndex);
                    const QString texOut = buildTextureOutputPath(m_outputDir, outputPattern, m_scene.sourcePath, "_" + safeMeshName + "_AO", ext, safeMeshName);
                    TextureRasterizer::bakeAOTexture(single, texOut, m_settings);
                    textureOutputs << texOut;
                    addRecentFile(texOut);
                }
                if (textureOutputs.isEmpty()) {
                    throw std::runtime_error("按 Mesh 拆分导出失败：没有检测到带 UV 的 Mesh。");
                }
            } else if (m_settings.textureSplitMode == QStringLiteral("按材质拆分")) {
                const QVector<int> materialIndices = collectUvMaterialIndices(m_scene);
                for (int materialIndex : materialIndices) {
                    const SceneData materialScene = buildSceneForMaterial(m_scene, materialIndex);
                    if (materialScene.meshes.isEmpty()) continue;
                    const QString materialToken = materialOutputToken(materialIndex);
                    const QString texOut = buildTextureOutputPath(m_outputDir, outputPattern, m_scene.sourcePath, "_" + materialToken + "_AO", ext, QString(), materialToken);
                    TextureRasterizer::bakeAOTexture(materialScene, texOut, m_settings);
                    textureOutputs << texOut;
                    addRecentFile(texOut);
                }
                if (textureOutputs.isEmpty()) {
                    throw std::runtime_error("按材质拆分导出失败：没有检测到带 UV 的材质面。");
                }
            } else if (m_settings.textureSplitMode == QStringLiteral("UDIM拆分")) {
                const QVector<int> udimTiles = collectUdimTiles(m_scene);
                for (int udimTile : udimTiles) {
                    const SceneData udimScene = buildSceneForUdimTile(m_scene, udimTile);
                    if (udimScene.meshes.isEmpty()) continue;
                    const QString texOut = buildTextureOutputPath(m_outputDir, outputPattern, m_scene.sourcePath, "_" + udimOutputToken(udimTile) + "_AO", ext, QString(), QString(), udimOutputToken(udimTile));
                    TextureRasterizer::bakeAOTexture(udimScene, texOut, m_settings);
                    textureOutputs << texOut;
                    addRecentFile(texOut);
                }
                if (textureOutputs.isEmpty()) {
                    throw std::runtime_error("UDIM 拆分导出失败：没有检测到有效 UDIM UV 面。请确认 UV 位于 1001 起始的 UDIM tile 中。");
                }
            } else {
                const QString texOut = buildTextureOutputPath(m_outputDir, outputPattern, m_scene.sourcePath, "_AO", ext);
                TextureRasterizer::bakeAOTexture(m_scene, texOut, m_settings);
                textureOutputs << texOut;
                addRecentFile(texOut);
            }
            m_lastBakeCachePath = textureOutputs.last();
            message += "\nAO 贴图已输出：\n" + textureOutputs.join("\n");
        }
        progress.setValue(100);
        if (m_viewport) m_viewport->update();
        markDirty();
        log(message);
        QMessageBox msgBox(this);
        msgBox.setWindowTitle("完成");
        msgBox.setText(message);
        auto* openBtn = msgBox.addButton("打开所在目录", QMessageBox::AcceptRole);
        auto* copyBtn = msgBox.addButton("复制路径", QMessageBox::ActionRole);
        msgBox.addButton("继续编辑", QMessageBox::RejectRole);
        msgBox.exec();
        if (msgBox.clickedButton() == openBtn) {
            QDesktopServices::openUrl(QUrl::fromLocalFile(m_outputDir));
        } else if (msgBox.clickedButton() == copyBtn) {
            QGuiApplication::clipboard()->setText(texture ? m_lastBakeCachePath : modelOut);
        }
    } catch (const std::exception& e) {
        QMessageBox::critical(this, "烘焙失败", e.what());
        log(QString("烘焙失败：") + e.what());
    }
}

void MainWindow::bakeVertex() { runBake(false); }
void MainWindow::bakeTexture() { runBake(true); }

void MainWindow::undoActionTriggered() {
    if (m_viewport) {
        m_viewport->undo();
        refreshSceneInfo();
        log("已执行撤销。");
    }
}

void MainWindow::redoActionTriggered() {
    if (m_viewport) {
        m_viewport->redo();
        refreshSceneInfo();
        log("已执行重做。");
    }
}

void MainWindow::clearVertexColors() {
    if (m_scene.empty() || !m_viewport) {
        QMessageBox::warning(this, "缺少模型", "请先导入模型。");
        return;
    }
    m_viewport->clearVertexColors();
    refreshSceneInfo();
    log("已清除全部顶点色。");
}

void MainWindow::batchProcessModels() {
    const QStringList files = QFileDialog::getOpenFileNames(this, "选择批量处理模型", QString(), "3D Model (*.obj *.fbx *.gltf *.glb *.dae *.ply *.stl);;OBJ (*.obj);;FBX (*.fbx);;glTF (*.gltf *.glb)");
    if (files.isEmpty()) return;

    QDialog queueDialog(this);
    queueDialog.setWindowTitle("批处理任务列表预览");
    queueDialog.resize(860, 420);
    auto* queueLayout = new QVBoxLayout(&queueDialog);
    auto* infoLabel = new QLabel(QString("已选择 %1 个模型。确认后将按当前 AO 参数、输出格式、位深和拆分模式批量处理。").arg(files.size()), &queueDialog);
    infoLabel->setWordWrap(true);
    queueLayout->addWidget(infoLabel);
    auto* queueTable = new QTableWidget(files.size(), 5, &queueDialog);
    queueTable->setHorizontalHeaderLabels({"序号", "文件名", "格式/位深", "拆分模式", "状态"});
    queueTable->horizontalHeader()->setStretchLastSection(true);
    queueTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    queueTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    queueTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    for (int i = 0; i < files.size(); ++i) {
        queueTable->setItem(i, 0, new QTableWidgetItem(QString::number(i + 1)));
        queueTable->setItem(i, 1, new QTableWidgetItem(QFileInfo(files[i]).fileName()));
        queueTable->setItem(i, 2, new QTableWidgetItem(m_settings.textureFormat + " / " + m_settings.textureBitDepth));
        queueTable->setItem(i, 3, new QTableWidgetItem(m_settings.textureSplitMode));
        queueTable->setItem(i, 4, new QTableWidgetItem("等待处理"));
    }
    queueLayout->addWidget(queueTable);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &queueDialog);
    buttons->button(QDialogButtonBox::Ok)->setText("开始批处理");
    buttons->button(QDialogButtonBox::Cancel)->setText("取消");
    connect(buttons, &QDialogButtonBox::accepted, &queueDialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &queueDialog, &QDialog::reject);
    queueLayout->addWidget(buttons);
    if (queueDialog.exec() != QDialog::Accepted) return;

    const QString outDir = QFileDialog::getExistingDirectory(this, "选择批量输出目录", m_outputDir);
    if (outDir.isEmpty()) return;

    const QStringList modes = {"烘焙顶点色并导出模型", "烘焙顶点色 + AO贴图", "仅导出AO贴图"};
    bool ok = false;
    const QString mode = QInputDialog::getItem(this, "批量处理模式", "处理方式", modes, 1, false, &ok);
    if (!ok || mode.isEmpty()) return;

    const SceneData previousScene = m_scene;
    const QString previousOutputDir = m_outputDir;
    m_outputDir = outDir;
    if (m_outputLabel) m_outputLabel->setText(m_outputDir);
    queueDialog.setWindowTitle("批处理任务状态");
    queueDialog.show();
    queueDialog.raise();
    queueDialog.activateWindow();

    QProgressDialog progress("正在批量处理...", "取消", 0, files.size(), this);
    progress.setWindowModality(Qt::WindowModal);
    progress.show();

    QStringList batchLog;
    int success = 0;
    int failed = 0;
    const QString batchStart = QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss");
    batchLog << "AO Baker Pro V2 批处理日志";
    batchLog << "开始时间：" + batchStart;
    batchLog << "输出目录：" + outDir;
    batchLog << "处理模式：" + mode;
    batchLog << "";

    for (int i = 0; i < files.size(); ++i) {
        const QString file = files[i];
        if (queueTable && queueTable->item(i, 4)) {
            queueTable->item(i, 4)->setText("处理中");
            queueTable->scrollToItem(queueTable->item(i, 4));
        }
        progress.setValue(i);
        progress.setLabelText(QString("正在处理 %1 / %2：%3").arg(i + 1).arg(files.size()).arg(QFileInfo(file).fileName()));
        QApplication::processEvents();
        if (progress.wasCanceled()) {
            if (queueTable && queueTable->item(i, 4)) queueTable->item(i, 4)->setText("已取消");
            batchLog << QString("[已取消] 用户在第 %1 个任务处取消。").arg(i + 1);
            break;
        }

        try {
            SceneData imported = ModelIO::importModel(file);
            AOBaker baker;
            connect(&baker, &AOBaker::progress, this, [&](const QString& msg, int percent){
                progress.setLabelText(QString("%1\n%2 %3%").arg(QFileInfo(file).fileName()).arg(msg).arg(percent));
                QApplication::processEvents();
                if (progress.wasCanceled()) throw std::runtime_error("用户取消批量烘焙");
            });
            baker.bakeToVertexColor(imported, m_settings);

            QStringList outputs;
            const QString baseName = QFileInfo(file).completeBaseName();
            const QString safeBase = QString(baseName).replace(QRegularExpression(QStringLiteral("[\\/:*?\"<>|]")), "_");
            if (mode != QStringLiteral("仅导出AO贴图")) {
                const QString modelOut = outDir + "/" + safeBase + "_ao_vertex.glb";
                ModelIO::exportModel(imported, modelOut, "glb2");
                outputs << modelOut;
                addRecentFile(modelOut);
            }
            if (mode != QStringLiteral("烘焙顶点色并导出模型")) {
                QString ext = m_settings.textureFormat.toLower();
                if (ext == "jpeg") ext = "jpg";
                const QString outputPattern = m_outputNameEdit ? m_outputNameEdit->text().trimmed() : QString();
                if (m_settings.textureSplitMode == QStringLiteral("按Mesh拆分")) {
                    for (int meshIndex = 0; meshIndex < imported.meshes.size(); ++meshIndex) {
                        const auto& mesh = imported.meshes[meshIndex];
                        if (!mesh.hasUV) continue;
                        SceneData single;
                        single.sourcePath = imported.sourcePath;
                        single.meshes.push_back(mesh);
                        const QString safeMeshName = meshOutputToken(mesh, meshIndex);
                        const QString texOut = buildTextureOutputPath(outDir, outputPattern, file, "_" + safeMeshName + "_AO", ext, safeMeshName);
                        TextureRasterizer::bakeAOTexture(single, texOut, m_settings);
                        outputs << texOut;
                        addRecentFile(texOut);
                    }
                    const bool onlyTextureMode = (mode == QStringLiteral("仅导出AO贴图"));
                    const bool modelOnlyOutput = (outputs.size() == 1 && outputs.first().endsWith(QStringLiteral("_ao_vertex.glb")));
                    if (onlyTextureMode ? outputs.isEmpty() : modelOnlyOutput) {
                        throw std::runtime_error("按 Mesh 拆分导出失败：没有检测到带 UV 的 Mesh。");
                    }
                } else if (m_settings.textureSplitMode == QStringLiteral("按材质拆分")) {
                    const QVector<int> materialIndices = collectUvMaterialIndices(imported);
                    for (int materialIndex : materialIndices) {
                        const SceneData materialScene = buildSceneForMaterial(imported, materialIndex);
                        if (materialScene.meshes.isEmpty()) continue;
                        const QString materialToken = materialOutputToken(materialIndex);
                        const QString texOut = buildTextureOutputPath(outDir, outputPattern, file, "_" + materialToken + "_AO", ext, QString(), materialToken);
                        TextureRasterizer::bakeAOTexture(materialScene, texOut, m_settings);
                        outputs << texOut;
                        addRecentFile(texOut);
                    }
                    const bool onlyTextureMode = (mode == QStringLiteral("仅导出AO贴图"));
                    const bool modelOnlyOutput = (outputs.size() == 1 && outputs.first().endsWith(QStringLiteral("_ao_vertex.glb")));
                    if (onlyTextureMode ? outputs.isEmpty() : modelOnlyOutput) {
                        throw std::runtime_error("按材质拆分导出失败：没有检测到带 UV 的材质面。");
                    }
                } else if (m_settings.textureSplitMode == QStringLiteral("UDIM拆分")) {
                    const QVector<int> udimTiles = collectUdimTiles(imported);
                    for (int udimTile : udimTiles) {
                        const SceneData udimScene = buildSceneForUdimTile(imported, udimTile);
                        if (udimScene.meshes.isEmpty()) continue;
                        const QString texOut = buildTextureOutputPath(outDir, outputPattern, file, "_" + udimOutputToken(udimTile) + "_AO", ext, QString(), QString(), udimOutputToken(udimTile));
                        TextureRasterizer::bakeAOTexture(udimScene, texOut, m_settings);
                        outputs << texOut;
                        addRecentFile(texOut);
                    }
                    const bool onlyTextureMode = (mode == QStringLiteral("仅导出AO贴图"));
                    const bool modelOnlyOutput = (outputs.size() == 1 && outputs.first().endsWith(QStringLiteral("_ao_vertex.glb")));
                    if (onlyTextureMode ? outputs.isEmpty() : modelOnlyOutput) {
                        throw std::runtime_error("UDIM 拆分导出失败：没有检测到有效 UDIM UV 面。请确认 UV 位于 1001 起始的 UDIM tile 中。");
                    }
                } else {
                    const QString texOut = buildTextureOutputPath(outDir, outputPattern, file, "_AO", ext);
                    TextureRasterizer::bakeAOTexture(imported, texOut, m_settings);
                    outputs << texOut;
                    addRecentFile(texOut);
                }
            }
            ++success;
            if (queueTable && queueTable->item(i, 4)) queueTable->item(i, 4)->setText("成功");
            batchLog << QString("[成功] %1").arg(file);
            for (const QString& out : outputs) batchLog << "  -> " + out;
            log(QString("批处理成功：%1").arg(QFileInfo(file).fileName()));
        } catch (const std::exception& e) {
            ++failed;
            if (queueTable && queueTable->item(i, 4)) queueTable->item(i, 4)->setText(QString("失败：%1").arg(e.what()));
            batchLog << QString("[失败] %1：%2").arg(file, e.what());
            log(QString("批处理失败：%1：%2").arg(QFileInfo(file).fileName(), e.what()));
            continue;
        }
    }

    progress.setValue(files.size());
    batchLog << "";
    batchLog << "结束时间：" + QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss");
    batchLog << QString("成功：%1，失败：%2").arg(success).arg(failed);
    const QString logPath = outDir + "/AO_Baker_Pro_Batch_Log_" + QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss") + ".txt";
    QFile logFile(logPath);
    if (logFile.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        logFile.write(batchLog.join("\n").toUtf8());
        logFile.close();
        addRecentFile(logPath);
    }

    m_scene = previousScene;
    m_outputDir = previousOutputDir;
    if (m_outputLabel) m_outputLabel->setText(m_outputDir);
    if (m_viewport) {
        m_viewport->setScene(&m_scene);
        m_viewport->setBakeSettings(m_settings);
    }
    refreshSceneInfo();
    updateStatusSummary();
    QMessageBox::information(this, "批量处理完成", QString("批量处理完成。\n成功：%1\n失败：%2\n日志：%3").arg(success).arg(failed).arg(logPath));
}

void MainWindow::onMeshSelected(int meshIndex, const QString& meshName) {
    if (!m_tree) return;
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
        QTreeWidgetItem* root = m_tree->topLevelItem(i);
        for (int j = 0; root && j < root->childCount(); ++j) {
            QTreeWidgetItem* child = root->child(j);
            if (child && child->data(0, Qt::UserRole).toInt() == meshIndex) {
                m_tree->setCurrentItem(child);
                log("已选择 Mesh：" + meshName);
                updateStatusSummary();
                return;
            }
        }
    }
    if (meshIndex < 0) {
        m_tree->clearSelection();
        log("已取消选择 Mesh");
    }
    updateStatusSummary();
}

void MainWindow::onSettingsChanged() {
    if (m_viewport) {
        m_viewport->setBakeSettings(m_settings);
    }
    markDirty();
    statusBar()->showMessage("AO 参数已更新，视口实时预览已刷新", 1500);
}

void MainWindow::recalculateNormals() {
    for (auto& mesh : m_scene.meshes) {
        for (auto& vertex : mesh.vertices) {
            vertex.normal = QVector3D(0, 0, 0);
        }
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
}

void MainWindow::repairFaceOrientation() {
    if (m_scene.empty()) {
        QMessageBox::warning(this, "缺少模型", "请先导入模型。");
        return;
    }

    QVector3D minV(1e9f, 1e9f, 1e9f), maxV(-1e9f, -1e9f, -1e9f);
    bool hasVertex = false;
    for (const auto& mesh : m_scene.meshes) {
        for (const auto& v : mesh.vertices) {
            hasVertex = true;
            minV.setX(std::min(minV.x(), v.position.x()));
            minV.setY(std::min(minV.y(), v.position.y()));
            minV.setZ(std::min(minV.z(), v.position.z()));
            maxV.setX(std::max(maxV.x(), v.position.x()));
            maxV.setY(std::max(maxV.y(), v.position.y()));
            maxV.setZ(std::max(maxV.z(), v.position.z()));
        }
    }
    if (!hasVertex) return;

    const QVector3D center = (minV + maxV) * 0.5f;
    int flipped = 0;
    for (auto& mesh : m_scene.meshes) {
        for (auto& face : mesh.faces) {
            if (face.i0 < 0 || face.i1 < 0 || face.i2 < 0 ||
                face.i0 >= mesh.vertices.size() || face.i1 >= mesh.vertices.size() || face.i2 >= mesh.vertices.size()) {
                continue;
            }
            const QVector3D a = mesh.vertices[face.i0].position;
            const QVector3D b = mesh.vertices[face.i1].position;
            const QVector3D c = mesh.vertices[face.i2].position;
            const QVector3D normal = QVector3D::crossProduct(b - a, c - a);
            const QVector3D faceCenter = (a + b + c) / 3.0f;
            if (normal.lengthSquared() > 1e-8f && QVector3D::dotProduct(normal, faceCenter - center) < 0.0f) {
                std::swap(face.i1, face.i2);
                ++flipped;
            }
        }
    }

    recalculateNormals();
    if (m_viewport) {
        m_viewport->setScene(&m_scene);
        m_viewport->setBakeSettings(m_settings);
    }
    markDirty();
    const QString msg = QString("面片朝向修复完成：已翻转 %1 个疑似反向三角面，并重新计算法线。").arg(flipped);
    log(msg);
    QMessageBox::information(this, "修复完成", msg);
}

void MainWindow::unifySmoothingGroups() {
    if (m_scene.empty()) {
        QMessageBox::warning(this, "缺少模型", "请先导入模型。");
        return;
    }
    int faces = 0;
    for (auto& mesh : m_scene.meshes) {
        for (auto& face : mesh.faces) {
            face.smoothingMask = 0xffffffffu;
            ++faces;
        }
    }
    recalculateNormals();
    if (m_viewport) {
        m_viewport->setScene(&m_scene);
        m_viewport->setBakeSettings(m_settings);
    }
    markDirty();
    const QString msg = QString("统一光滑组完成：已将 %1 个三角面合并为统一光滑组，并重新计算连续法线。").arg(faces);
    log(msg);
    QMessageBox::information(this, "统一光滑组完成", msg);
}

void MainWindow::clearScene() {
    if (!confirmLoseUnsavedChanges("清空场景")) return;
    m_scene.clear();
    if (m_viewport) m_viewport->setScene(nullptr);
    m_tree->clear();
    m_modelInfo->setText("未导入模型");
    m_uvInfo->setText("UV 通道：未检测");
    m_currentProjectPath.clear();
    m_lastBakeCachePath.clear();
    markDirty(false);
    log("场景已清空");
}

void MainWindow::newScene() {
    if (!confirmLoseUnsavedChanges("新建场景")) return;
    m_scene.clear();
    if (m_viewport) m_viewport->setScene(nullptr);
    if (m_outputNameEdit) m_outputNameEdit->clear();
    m_currentProjectPath.clear();
    m_lastBakeCachePath.clear();
    m_modelInfo->setText("未导入模型");
    m_uvInfo->setText("UV 通道：未检测");
    m_tree->clear();
    markDirty(false);
    log("已新建场景");
}

void MainWindow::saveProject() {
    if (m_currentProjectPath.isEmpty()) {
        saveProjectAs();
        return;
    }
    saveProjectToFile(m_currentProjectPath);
}

void MainWindow::saveProjectAs() {
    const QString path = QFileDialog::getSaveFileName(this, "另存为项目", projectBaseName() + ".aobproj", "AO Baker Project (*.aobproj)");
    if (path.isEmpty()) return;
    saveProjectToFile(path);
}

void MainWindow::loadProject() {
    if (!confirmLoseUnsavedChanges("打开项目")) return;
    const QString path = QFileDialog::getOpenFileName(this, "打开项目", QString(), "AO Baker Project (*.aobproj)");
    if (path.isEmpty()) return;
    loadProjectFromFile(path);
}

void MainWindow::openPreferences() {
    QMessageBox::information(this, "首选项", QString("当前已接入：\n- 最近文件记录\n- 输出目录持久化\n- 窗口几何与布局保存\n- AO 参数与输出格式保存\n- 视口状态保存\n- 位深/拆分导出设置\n- 批处理队列入口\n\n后续可继续补充快捷键、语言、自动保存周期等完整首选项面板。"));
}

void MainWindow::log(const QString& text) {
    if (m_log) m_log->append(text);
    statusBar()->showMessage(text, 5000);
    updateStatusSummary();
}

void MainWindow::markDirty(bool dirty) {
    m_isDirty = dirty;
    updateWindowTitle();
    updateStatusSummary();
}

void MainWindow::updateWindowTitle() {
    QString title = QString("AO_Baker_Pro_V2 - %1").arg(projectBaseName());
    if (m_isDirty) title += " *";
    setWindowTitle(title);
}

bool MainWindow::confirmLoseUnsavedChanges(const QString& actionText) {
    if (!m_isDirty) return true;
    QMessageBox box(this);
    box.setWindowTitle("未保存更改");
    box.setText(QString("当前项目存在未保存内容，是否在%1前保存？").arg(actionText));
    auto* saveBtn = box.addButton("保存", QMessageBox::AcceptRole);
    auto* dontSaveBtn = box.addButton("不保存", QMessageBox::DestructiveRole);
    box.addButton("取消", QMessageBox::RejectRole);
    box.exec();
    if (box.clickedButton() == saveBtn) {
        saveProject();
        return !m_isDirty;
    }
    return box.clickedButton() == dontSaveBtn;
}

QString MainWindow::projectBaseName() const {
    if (!m_currentProjectPath.isEmpty()) return QFileInfo(m_currentProjectPath).fileName();
    if (!m_scene.sourcePath.isEmpty()) return QFileInfo(m_scene.sourcePath).fileName();
    return QStringLiteral("untitled.aobproj");
}

void MainWindow::updateStatusSummary() {
    if (!m_statusSummary) return;
    const QString summary = QString("状态：%1 | 顶点：%2 | 三角面：%3 | UV Mesh：%4 | 选中 Mesh：%5 | 输出：%6")
        .arg(m_isDirty ? "未保存" : "已保存")
        .arg(m_scene.vertexCount())
        .arg(m_scene.faceCount())
        .arg(m_scene.uvMeshCount())
        .arg(m_viewport ? m_viewport->selectedMesh() : -1)
        .arg(QFileInfo(m_outputDir).fileName());
    m_statusSummary->setText(summary);
}

void MainWindow::updateRecentFilesMenu() {
    if (!m_recentFilesMenu) return;
    m_recentFilesMenu->clear();
    if (m_recentFiles.isEmpty()) {
        auto* emptyAction = m_recentFilesMenu->addAction("暂无最近记录");
        emptyAction->setEnabled(false);
        return;
    }
    const int count = std::min(10, static_cast<int>(m_recentFiles.size()));
    for (int i = 0; i < count; ++i) {
        const QString path = m_recentFiles[i];
        auto* act = m_recentFilesMenu->addAction(path, this, &MainWindow::importRecentModel);
        act->setData(path);
    }
    m_recentFilesMenu->addSeparator();
    m_recentFilesMenu->addAction("清空最近记录", this, [this](){
        m_recentFiles.clear();
        updateRecentFilesMenu();
        saveAppSettings();
    });
}

void MainWindow::addRecentFile(const QString& path) {
    if (path.isEmpty()) return;
    m_recentFiles.removeAll(path);
    m_recentFiles.prepend(path);
    while (m_recentFiles.size() > 10) m_recentFiles.removeLast();
    updateRecentFilesMenu();
    saveAppSettings();
}

QString MainWindow::settingsDir() const {
    QString dir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    if (dir.isEmpty()) dir = QDir::homePath() + "/.ao_baker_pro_v2";
    QDir().mkpath(dir);
    return dir;
}

void MainWindow::loadAppSettings() {
    QSettings settings(kOrgName, kAppName);
    restoreGeometry(settings.value("geometry").toByteArray());
    restoreState(settings.value("windowState").toByteArray());
    m_outputDir = settings.value("outputDir", m_outputDir).toString();
    m_recentFiles = settings.value("recentFiles").toStringList();
    m_currentProjectPath = settings.value("lastProjectPath").toString();
    m_lastBakeCachePath = settings.value("lastBakeCachePath").toString();
    m_settings.aoStrength = settings.value("ao/strength", m_settings.aoStrength).toFloat();
    m_settings.groundBounceStrength = settings.value("ao/ground", m_settings.groundBounceStrength).toFloat();
    m_settings.transitionSmoothness = settings.value("ao/smoothness", m_settings.transitionSmoothness).toFloat();
    m_settings.transitionContrast = settings.value("ao/contrast", m_settings.transitionContrast).toFloat();
    m_settings.darkest = settings.value("ao/darkest", m_settings.darkest).toFloat();
    m_settings.brightest = settings.value("ao/brightest", m_settings.brightest).toFloat();
    m_settings.sampleCount = settings.value("ao/samples", m_settings.sampleCount).toInt();
    m_settings.rayDistance = settings.value("ao/rayDistance", m_settings.rayDistance).toFloat();
    m_settings.detailPreserve = settings.value("ao/detailPreserve", m_settings.detailPreserve).toFloat();
    m_settings.textureResolution = settings.value("output/resolution", m_settings.textureResolution).toInt();
    m_settings.textureFormat = settings.value("output/format", m_settings.textureFormat).toString();
    m_settings.textureBitDepth = settings.value("output/bitDepth", m_settings.textureBitDepth).toString();
    m_settings.textureSplitMode = settings.value("output/splitMode", m_settings.textureSplitMode).toString();

    if (m_outputLabel) m_outputLabel->setText(m_outputDir);
    if (m_resolution) m_resolution->setCurrentText(QString::number(m_settings.textureResolution));
    if (m_format) m_format->setCurrentText(m_settings.textureFormat);
    if (m_bitDepth) m_bitDepth->setCurrentText(m_settings.textureBitDepth);
    if (m_splitMode) m_splitMode->setCurrentText(m_settings.textureSplitMode);
    updateOutputFormatUi();
    if (m_viewport) {
        m_viewport->setBakeSettings(m_settings);
        m_viewport->restoreViewState(settings.value("viewport/state").toJsonObject());
    }
    updateRecentFilesMenu();

    const QString autoRecoverPath = settingsDir() + "/autosave_recovery.aobproj";
    if (QFileInfo::exists(autoRecoverPath) && m_scene.empty()) {
        if (loadProjectFromFile(autoRecoverPath)) {
            m_currentProjectPath.clear();
            markDirty(true);
            log("已自动恢复上次未完成项目。 ");
        }
    }
}

void MainWindow::saveAppSettings() const {
    QSettings settings(kOrgName, kAppName);
    settings.setValue("geometry", saveGeometry());
    settings.setValue("windowState", saveState());
    settings.setValue("outputDir", m_outputDir);
    settings.setValue("recentFiles", m_recentFiles);
    settings.setValue("lastProjectPath", m_currentProjectPath);
    settings.setValue("lastBakeCachePath", m_lastBakeCachePath);
    settings.setValue("ao/strength", m_settings.aoStrength);
    settings.setValue("ao/ground", m_settings.groundBounceStrength);
    settings.setValue("ao/smoothness", m_settings.transitionSmoothness);
    settings.setValue("ao/contrast", m_settings.transitionContrast);
    settings.setValue("ao/darkest", m_settings.darkest);
    settings.setValue("ao/brightest", m_settings.brightest);
    settings.setValue("ao/samples", m_settings.sampleCount);
    settings.setValue("ao/rayDistance", m_settings.rayDistance);
    settings.setValue("ao/detailPreserve", m_settings.detailPreserve);
    settings.setValue("output/resolution", m_settings.textureResolution);
    settings.setValue("output/format", m_settings.textureFormat);
    settings.setValue("output/bitDepth", m_settings.textureBitDepth);
    settings.setValue("output/splitMode", m_settings.textureSplitMode);
    if (m_viewport) settings.setValue("viewport/state", m_viewport->saveViewState());
    const QString autoRecoverPath = settingsDir() + "/autosave_recovery.aobproj";
    QFile file(autoRecoverPath);
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        file.write(QJsonDocument(saveProjectJson()).toJson(QJsonDocument::Indented));
    }
}

bool MainWindow::saveProjectToFile(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        QMessageBox::critical(this, "保存项目失败", "无法写入项目文件。");
        return false;
    }
    file.write(QJsonDocument(saveProjectJson()).toJson(QJsonDocument::Indented));
    file.close();
    m_currentProjectPath = path;
    addRecentFile(path);
    markDirty(false);
    saveAppSettings();
    log("项目已保存：" + path);
    return true;
}

bool MainWindow::loadProjectFromFile(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        QMessageBox::critical(this, "打开项目失败", "无法读取项目文件。");
        return false;
    }
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();
    if (!doc.isObject() || !restoreProjectJson(doc.object())) {
        QMessageBox::critical(this, "打开项目失败", "项目文件格式无效。");
        return false;
    }
    m_currentProjectPath = path;
    addRecentFile(path);
    markDirty(false);
    log("项目已打开：" + path);
    return true;
}

QJsonObject MainWindow::saveProjectJson() const {
    QJsonObject root;
    root["version"] = 2;
    root["sourcePath"] = m_scene.sourcePath;
    root["outputDir"] = m_outputDir;
    root["outputName"] = m_outputNameEdit ? m_outputNameEdit->text() : QString();
    root["lastBakeCachePath"] = m_lastBakeCachePath;

    QJsonObject settingsObj;
    settingsObj["aoStrength"] = m_settings.aoStrength;
    settingsObj["groundBounceStrength"] = m_settings.groundBounceStrength;
    settingsObj["groundBounceColor"] = m_settings.groundBounceColor.name(QColor::HexArgb);
    settingsObj["transitionSmoothness"] = m_settings.transitionSmoothness;
    settingsObj["transitionContrast"] = m_settings.transitionContrast;
    settingsObj["darkest"] = m_settings.darkest;
    settingsObj["brightest"] = m_settings.brightest;
    settingsObj["sampleCount"] = m_settings.sampleCount;
    settingsObj["rayDistance"] = m_settings.rayDistance;
    settingsObj["detailPreserve"] = m_settings.detailPreserve;
    settingsObj["textureResolution"] = m_settings.textureResolution;
    settingsObj["textureFormat"] = m_settings.textureFormat;
    settingsObj["textureBitDepth"] = m_settings.textureBitDepth;
    settingsObj["textureSplitMode"] = m_settings.textureSplitMode;
    settingsObj["keepSmoothingGroups"] = m_settings.keepSmoothingGroups;
    settingsObj["keepUVs"] = m_settings.keepUVs;
    settingsObj["adaptiveSampling"] = m_settings.adaptiveSampling;
    root["bakeSettings"] = settingsObj;

    if (m_viewport) root["viewportState"] = m_viewport->saveViewState();

    QJsonArray meshes;
    for (const auto& mesh : m_scene.meshes) {
        QJsonObject meshObj;
        meshObj["name"] = mesh.name;
        meshObj["hasUV"] = mesh.hasUV;
        meshObj["hasNormals"] = mesh.hasNormals;
        meshObj["hasVertexColor"] = mesh.hasVertexColor;
        QJsonArray vertices;
        for (const auto& v : mesh.vertices) {
            QJsonObject vo;
            vo["px"] = v.position.x(); vo["py"] = v.position.y(); vo["pz"] = v.position.z();
            vo["nx"] = v.normal.x(); vo["ny"] = v.normal.y(); vo["nz"] = v.normal.z();
            vo["u"] = v.uv.x(); vo["v"] = v.uv.y();
            vo["color"] = v.color.name(QColor::HexArgb);
            vo["originalIndex"] = v.originalIndex;
            vertices.append(vo);
        }
        QJsonArray faces;
        for (const auto& f : mesh.faces) {
            QJsonObject fo;
            fo["i0"] = f.i0; fo["i1"] = f.i1; fo["i2"] = f.i2;
            fo["materialIndex"] = f.materialIndex;
            fo["smoothingMask"] = int(f.smoothingMask);
            faces.append(fo);
        }
        meshObj["vertices"] = vertices;
        meshObj["faces"] = faces;
        meshes.append(meshObj);
    }
    root["meshes"] = meshes;
    return root;
}

bool MainWindow::restoreProjectJson(const QJsonObject& root) {
    SceneData loadedScene;
    loadedScene.sourcePath = root.value("sourcePath").toString();
    const QJsonArray meshes = root.value("meshes").toArray();
    for (const auto& meshValue : meshes) {
        const QJsonObject meshObj = meshValue.toObject();
        MeshData mesh;
        mesh.name = meshObj.value("name").toString();
        mesh.hasUV = meshObj.value("hasUV").toBool();
        mesh.hasNormals = meshObj.value("hasNormals").toBool();
        mesh.hasVertexColor = meshObj.value("hasVertexColor").toBool();
        const QJsonArray vertices = meshObj.value("vertices").toArray();
        for (const auto& vertexValue : vertices) {
            const QJsonObject vo = vertexValue.toObject();
            Vertex v;
            v.position = QVector3D(float(vo.value("px").toDouble()), float(vo.value("py").toDouble()), float(vo.value("pz").toDouble()));
            v.normal = QVector3D(float(vo.value("nx").toDouble()), float(vo.value("ny").toDouble()), float(vo.value("nz").toDouble()));
            v.uv = QVector2D(float(vo.value("u").toDouble()), float(vo.value("v").toDouble()));
            v.color = QColor(vo.value("color").toString("#ffffffff"));
            v.originalIndex = vo.value("originalIndex").toInt(-1);
            mesh.vertices.push_back(v);
        }
        const QJsonArray faces = meshObj.value("faces").toArray();
        for (const auto& faceValue : faces) {
            const QJsonObject fo = faceValue.toObject();
            Face f;
            f.i0 = fo.value("i0").toInt();
            f.i1 = fo.value("i1").toInt();
            f.i2 = fo.value("i2").toInt();
            f.materialIndex = fo.value("materialIndex").toInt();
            f.smoothingMask = unsigned(fo.value("smoothingMask").toInt());
            mesh.faces.push_back(f);
        }
        loadedScene.meshes.push_back(mesh);
    }

    const QJsonObject settingsObj = root.value("bakeSettings").toObject();
    if (!settingsObj.isEmpty()) {
        m_settings.aoStrength = float(settingsObj.value("aoStrength").toDouble(m_settings.aoStrength));
        m_settings.groundBounceStrength = float(settingsObj.value("groundBounceStrength").toDouble(m_settings.groundBounceStrength));
        m_settings.groundBounceColor = QColor(settingsObj.value("groundBounceColor").toString(m_settings.groundBounceColor.name(QColor::HexArgb)));
        m_settings.transitionSmoothness = float(settingsObj.value("transitionSmoothness").toDouble(m_settings.transitionSmoothness));
        m_settings.transitionContrast = float(settingsObj.value("transitionContrast").toDouble(m_settings.transitionContrast));
        m_settings.darkest = float(settingsObj.value("darkest").toDouble(m_settings.darkest));
        m_settings.brightest = float(settingsObj.value("brightest").toDouble(m_settings.brightest));
        m_settings.sampleCount = settingsObj.value("sampleCount").toInt(m_settings.sampleCount);
        m_settings.rayDistance = float(settingsObj.value("rayDistance").toDouble(m_settings.rayDistance));
        m_settings.detailPreserve = float(settingsObj.value("detailPreserve").toDouble(m_settings.detailPreserve));
        m_settings.textureResolution = settingsObj.value("textureResolution").toInt(m_settings.textureResolution);
        m_settings.textureFormat = settingsObj.value("textureFormat").toString(m_settings.textureFormat);
        m_settings.textureBitDepth = settingsObj.value("textureBitDepth").toString(m_settings.textureBitDepth);
        m_settings.textureSplitMode = settingsObj.value("textureSplitMode").toString(m_settings.textureSplitMode);
        m_settings.keepSmoothingGroups = settingsObj.value("keepSmoothingGroups").toBool(m_settings.keepSmoothingGroups);
        m_settings.keepUVs = settingsObj.value("keepUVs").toBool(m_settings.keepUVs);
        m_settings.adaptiveSampling = settingsObj.value("adaptiveSampling").toBool(m_settings.adaptiveSampling);
    }

    m_scene = loadedScene;
    m_outputDir = root.value("outputDir").toString(m_outputDir);
    m_lastBakeCachePath = root.value("lastBakeCachePath").toString();
    if (m_outputNameEdit) m_outputNameEdit->setText(root.value("outputName").toString());
    if (m_outputLabel) m_outputLabel->setText(m_outputDir);
    if (m_resolution) m_resolution->setCurrentText(QString::number(m_settings.textureResolution));
    if (m_format) m_format->setCurrentText(m_settings.textureFormat);
    if (m_bitDepth) m_bitDepth->setCurrentText(m_settings.textureBitDepth);
    if (m_splitMode) m_splitMode->setCurrentText(m_settings.textureSplitMode);
    updateOutputFormatUi();
    if (m_colorButton) m_colorButton->setText(m_settings.groundBounceColor.name());
    if (m_viewport) {
        m_viewport->setScene(&m_scene);
        m_viewport->setBakeSettings(m_settings);
        m_viewport->restoreViewState(root.value("viewportState").toObject());
    }
    refreshSceneInfo();
    updateWindowTitle();
    return true;
}
