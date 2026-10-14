#include "MainWindow.h"
#include "ModelIO.h"
#include "TextureRasterizer.h"

#include <QApplication>
#include <QCheckBox>
#include <QColorDialog>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QMessageBox>
#include <QMenu>
#include <QMenuBar>
#include <QProgressDialog>
#include <QRadioButton>
#include <QButtonGroup>
#include <QList>
#include <QRegularExpression>
#include <QSplitter>
#include <QStringList>
#include <QStatusBar>
#include <QToolBar>
#include <QVBoxLayout>
#include <QAction>
#include <QVariant>
#include <QVector3D>
#include <algorithm>
#include <cmath>
#include <utility>

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
    setWindowTitle("AO Baker Pro v1.0.0");
    resize(1220, 780);
    buildUi();
    applyStyle();
}

QWidget* MainWindow::group(const QString& title, QWidget* child) {
    auto* box = new QGroupBox(title, this);
    auto* layout = new QVBoxLayout(box);
    layout->addWidget(child);
    return box;
}


void MainWindow::buildMenus() {
    auto* fileMenu = menuBar()->addMenu("文件(&F)");
    fileMenu->addAction("打开", this, &MainWindow::importModel);
    fileMenu->addAction("保存", this, [this](){
        if (m_scene.empty()) { QMessageBox::warning(this, "缺少模型", "请先导入模型。"); return; }
        const QString path = QFileDialog::getSaveFileName(this, "保存模型", outputBaseName("_saved.glb"), "GLB (*.glb);;FBX (*.fbx);;OBJ (*.obj)");
        if (path.isEmpty()) return;
        const QString ext = QFileInfo(path).suffix().toLower();
        const QString fmt = ext == "fbx" ? "fbx" : (ext == "obj" ? "obj" : "glb2");
        try { ModelIO::exportModel(m_scene, path, fmt); log("模型已保存：" + path); }
        catch (const std::exception& e) { QMessageBox::critical(this, "保存失败", e.what()); }
    });
    fileMenu->addAction("导入", this, &MainWindow::importModel);
    fileMenu->addAction("导出", this, [this](){
        if (m_scene.empty()) { QMessageBox::warning(this, "缺少模型", "请先导入模型。"); return; }
        const QString path = QFileDialog::getSaveFileName(this, "导出模型", outputBaseName("_export.glb"), "GLB (*.glb);;FBX (*.fbx);;OBJ (*.obj)");
        if (path.isEmpty()) return;
        const QString ext = QFileInfo(path).suffix().toLower();
        const QString fmt = ext == "fbx" ? "fbx" : (ext == "obj" ? "obj" : "glb2");
        try { ModelIO::exportModel(m_scene, path, fmt); log("模型已导出：" + path); }
        catch (const std::exception& e) { QMessageBox::critical(this, "导出失败", e.what()); }
    });

    auto* editMenu = menuBar()->addMenu("编辑(&E)");
    editMenu->addAction("软件设置", this, [this](){ QMessageBox::information(this, "软件设置", "设置面板已预留：后续可接入默认路径、单位、快捷键、导入导出偏好等配置。"); });
    auto* perfMenu = editMenu->addMenu("软件性能模式选项");
    perfMenu->addAction("高质量模式", this, [this](){ m_settings.sampleCount = 512; m_settings.adaptiveSampling = false; onSettingsChanged(); log("已切换到高质量模式：512 射线，关闭自适应采样。"); });
    perfMenu->addAction("平衡模式", this, [this](){ m_settings.sampleCount = 256; m_settings.adaptiveSampling = true; onSettingsChanged(); log("已切换到平衡模式：256 射线，自适应采样。"); });
    perfMenu->addAction("快速模式", this, [this](){ m_settings.sampleCount = 96; m_settings.adaptiveSampling = true; onSettingsChanged(); log("已切换到快速模式：96 射线，自适应采样。"); });
    editMenu->addAction("重新载入刷新模型", this, [this](){
        if (m_scene.sourcePath.isEmpty()) { QMessageBox::warning(this, "缺少模型", "当前没有可重新载入的模型路径。"); return; }
        try { m_scene = ModelIO::importModel(m_scene.sourcePath); refreshSceneInfo(); if (m_viewport) { m_viewport->setScene(&m_scene); m_viewport->setBakeSettings(m_settings); } log("模型已重新载入刷新。"); }
        catch (const std::exception& e) { QMessageBox::critical(this, "重新载入失败", e.what()); }
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
    connect(wireAction, &QAction::toggled, this, [this](bool checked){ if (m_viewport) m_viewport->setWireframeVisible(checked); });
    auto* vcAction = toggleMenu->addAction("显示 / 隐藏顶点色");
    vcAction->setCheckable(true); vcAction->setChecked(true);
    connect(vcAction, &QAction::toggled, this, [this](bool checked){ if (m_viewport) m_viewport->setUseVertexColor(checked); });
    auto* paintAction = toggleMenu->addAction("启用 / 关闭顶点色绘制");
    paintAction->setCheckable(true);
    connect(paintAction, &QAction::toggled, this, [this](bool checked){ if (m_viewport) m_viewport->setVertexPaintEnabled(checked); });

    auto* toolMenu = menuBar()->addMenu("工具(&T)");
    toolMenu->addAction("笔刷相关的详细设置", this, [this](){ QMessageBox::information(this, "笔刷详细设置", "已支持画笔 / 擦除 / 填充桶、半径、不透明度、软笔刷、图层不透明度。高级笔刷曲线和压力感应接口已预留。"); });
    toolMenu->addAction("颜色填充模式详细设置", this, [this](){ if (m_viewport) m_viewport->setPaintMode(2); QMessageBox::information(this, "颜色填充模式", "已切换到填充桶模式。可在左侧顶点色绘制面板选择颜色、混合模式和图层不透明度。"); });
    toolMenu->addAction("UV集的详细选择和设置", this, [this](){ QMessageBox::information(this, "UV 集设置", "当前保留导入模型的 UV0。UV1/多 UV 集选择、复制、重命名和导出映射设置已预留。"); });

    auto* helpMenu = menuBar()->addMenu("帮助(&H)");
    helpMenu->addAction("软件版本", this, [this](){ QMessageBox::information(this, "软件版本", "AO Baker Pro v1.0.0\n当前构建：Layout/Menu 优化版"); });
    helpMenu->addAction("软件更新网址路径", this, [this](){ QMessageBox::information(this, "软件更新", "更新网址暂未配置。后续可填写 GitHub Release / 官网下载路径。"); });
    helpMenu->addAction("邮箱信息和联系方式", this, [this](){ QMessageBox::information(this, "联系方式", "邮箱和技术支持联系方式暂未配置，当前为占位信息。"); });
}

void MainWindow::buildUi() {
    buildMenus();
    auto* toolbar = addToolBar("工具");
    toolbar->addAction("导入模型", this, &MainWindow::importModel);
    toolbar->addAction("选择输出目录", this, &MainWindow::chooseOutputDir);
    toolbar->addSeparator();
    toolbar->addAction("烘焙AO到顶点色", this, &MainWindow::bakeVertex);
    toolbar->addAction("烘焙AO到贴图", this, &MainWindow::bakeTexture);
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
    root->setSizes({150, 230, 610, 350});
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
    connect(wireframe, &QCheckBox::toggled, this, [this](bool v){ if (m_viewport) m_viewport->setWireframeVisible(v); });
    auto* useVertexColor = new QCheckBox("显示顶点色 / AO 结果", this);
    useVertexColor->setChecked(true);
    connect(useVertexColor, &QCheckBox::toggled, this, [this](bool v){ if (m_viewport) m_viewport->setUseVertexColor(v); });
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
    });
    form->addRow("显示通道", showChannel);

    auto* paintEnabled = new QCheckBox("启用手动绘制", this);
    connect(paintEnabled, &QCheckBox::toggled, this, [this](bool v){ if (m_viewport) m_viewport->setVertexPaintEnabled(v); });
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
    });
    toolLayout->addWidget(brush);
    toolLayout->addWidget(eraser);
    toolLayout->addWidget(bucket);
    form->addRow("画笔 / 擦除 / 填充", toolRow);

    m_paintColorButton = new QPushButton("#ffffff", this);
    connect(m_paintColorButton, &QPushButton::clicked, this, &MainWindow::choosePaintColor);
    form->addRow("颜色选择", m_paintColorButton);

    auto* opacity = new SliderRow(0, 1, 1, 2, this);
    connect(opacity, &SliderRow::valueChanged, this, [this](float v){ if (m_viewport) m_viewport->setPaintOpacity(v); });
    form->addRow("不透明度", opacity);

    auto* size = new QSpinBox(this);
    size->setRange(1, 300);
    size->setValue(24);
    connect(size, &QSpinBox::valueChanged, this, [this](int v){ if (m_viewport) m_viewport->setPaintRadius(float(v)); });
    form->addRow("笔刷大小", size);

    auto* soft = new QCheckBox("软笔刷 / 边缘衰减", this);
    soft->setChecked(true);
    connect(soft, &QCheckBox::toggled, this, [this](bool v){ if (m_viewport) m_viewport->setPaintSoftBrush(v); });
    form->addRow("笔刷选项", soft);

    auto* blend = new QComboBox(this);
    blend->addItems({"正常", "正片叠底", "滤色", "叠加", "加亮", "变暗"});
    connect(blend, &QComboBox::currentTextChanged, this, [this](const QString& v){ if (m_viewport) m_viewport->setPaintBlendMode(v); });
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
        });
        paletteLayout->addWidget(b);
    }
    form->addRow("调色板", paletteRow);

    auto* layerName = new QLabel("Layer 1 - Vertex Color", this);
    auto* newLayer = new QPushButton("新建图层", this);
    connect(newLayer, &QPushButton::clicked, this, [this, layerName](){
        layerName->setText("Layer 2 - Paint Layer");
        log("已新建顶点色绘制图层：Layer 2 - Paint Layer");
    });
    form->addRow("图层", layerName);
    form->addRow(newLayer);

    auto* fillLayer = new QPushButton("填充当前图层 / 选中Mesh", this);
    connect(fillLayer, &QPushButton::clicked, this, [this](){ if (m_viewport) m_viewport->fillSelectedMesh(); });
    form->addRow("图层操作", fillLayer);

    auto* layerOpacity = new SliderRow(0, 1, 1, 2, this);
    connect(layerOpacity, &SliderRow::valueChanged, this, [this](float v){ if (m_viewport) m_viewport->setLayerOpacity(v); });
    form->addRow("图层不透明度", layerOpacity);

    auto* rgbRow = new QWidget(this);
    auto* rgbLayout = new QHBoxLayout(rgbRow);
    rgbLayout->setContentsMargins(0,0,0,0);
    auto* red = new QCheckBox("R", this);
    auto* green = new QCheckBox("G", this);
    auto* blue = new QCheckBox("B", this);
    red->setChecked(true); green->setChecked(true); blue->setChecked(true);
    auto updateChannels = [this, red, green, blue](){
        if (m_viewport) m_viewport->setVisibleChannels(red->isChecked(), green->isChecked(), blue->isChecked());
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
    connect(m_resolution, &QComboBox::currentTextChanged, this, [this](const QString& v){ m_settings.textureResolution = v.toInt(); });
    m_format = new QComboBox(this);
    m_format->addItems({"TGA", "PNG", "JPG", "BMP"});
    m_format->setCurrentText("PNG");
    connect(m_format, &QComboBox::currentTextChanged, this, [this](const QString& v){ m_settings.textureFormat = v; });
    m_outputNameEdit = new QLineEdit(this);
    m_outputNameEdit->setPlaceholderText("留空则使用原模型名称");
    m_outputLabel = new QLabel(m_outputDir, this);
    m_outputLabel->setWordWrap(true);
    auto* pick = new QPushButton("选择输出目录", this);
    connect(pick, &QPushButton::clicked, this, &MainWindow::chooseOutputDir);
    form->addRow("贴图分辨率", m_resolution);
    form->addRow("贴图格式", m_format);
    form->addRow("导出命名", m_outputNameEdit);
    form->addRow("位深度", new QLabel("8-bit", this));
    form->addRow("输出路径", m_outputLabel);
    form->addRow(pick);
    return box;
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
    )");
}

void MainWindow::importModel() {
    const QString path = QFileDialog::getOpenFileName(this, "导入模型", QString(), "3D Model (*.obj *.fbx *.gltf *.glb);;OBJ (*.obj);;FBX (*.fbx);;glTF (*.gltf *.glb)");
    if (path.isEmpty()) return;
    try {
        m_scene = ModelIO::importModel(path);
        refreshSceneInfo();
        m_viewport->setScene(&m_scene);
        m_viewport->setBakeSettings(m_settings);
        log("已导入模型：" + path);
    } catch (const std::exception& e) {
        QMessageBox::critical(this, "导入失败", e.what());
        log(QString("导入失败：") + e.what());
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
    m_uvInfo->setText(QString("UV Mesh：%1/%2\n导入保留 UV、法线、顶点色；FBX 由 Assimp 独立解析。")
        .arg(m_scene.uvMeshCount()).arg(m_scene.meshes.size()));
}

void MainWindow::chooseOutputDir() {
    const QString dir = QFileDialog::getExistingDirectory(this, "选择输出目录", m_outputDir);
    if (!dir.isEmpty()) {
        m_outputDir = dir;
        m_outputLabel->setText(dir);
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
        log("默认材质颜色已切换：" + c.name());
    }
}

void MainWindow::choosePaintColor() {
    const QColor initial = m_paintColorButton ? QColor(m_paintColorButton->text()) : QColor(255, 255, 255);
    const QColor c = QColorDialog::getColor(initial, this, "选择顶点色画笔颜色");
    if (c.isValid()) {
        if (m_paintColorButton) m_paintColorButton->setText(c.name());
        if (m_viewport) m_viewport->setPaintColor(c);
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
        if (texture) {
            const QString ext = m_settings.textureFormat.toLower() == "jpg" ? QString("jpg") : m_settings.textureFormat.toLower();
            const QString texOut = outputBaseName("_AO." + ext);
            TextureRasterizer::bakeAOTexture(m_scene, texOut, m_settings);
            message += "\nAO 贴图已输出：\n" + texOut;
        }
        progress.setValue(100);
        m_viewport->update();
        log(message);
        QMessageBox::information(this, "完成", message);
    } catch (const std::exception& e) {
        QMessageBox::critical(this, "烘焙失败", e.what());
        log(QString("烘焙失败：") + e.what());
    }
}

void MainWindow::bakeVertex() { runBake(false); }
void MainWindow::bakeTexture() { runBake(true); }

void MainWindow::onMeshSelected(int meshIndex, const QString& meshName) {
    if (!m_tree) return;
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
        QTreeWidgetItem* root = m_tree->topLevelItem(i);
        for (int j = 0; root && j < root->childCount(); ++j) {
            QTreeWidgetItem* child = root->child(j);
            if (child && child->data(0, Qt::UserRole).toInt() == meshIndex) {
                m_tree->setCurrentItem(child);
                log("已选择 Mesh：" + meshName);
                return;
            }
        }
    }
    if (meshIndex < 0) {
        m_tree->clearSelection();
        log("已取消选择 Mesh");
    }
}

void MainWindow::onSettingsChanged() {
    if (m_viewport) {
        m_viewport->setBakeSettings(m_settings);
    }
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
    const QString msg = QString("面片朝向修复完成：已翻转 %1 个疑似反向三角面，并重新计算法线。") .arg(flipped);
    log(msg);
    QMessageBox::information(this, "修复完成", msg);
}

void MainWindow::unifySmoothingGroups() {
    if (m_scene.empty()) {
        QMessageBox::warning(this, "缺少模型", "请先导入模型。");
        return;
    }
    for (auto& mesh : m_scene.meshes) {
        for (auto& face : mesh.faces) {
            face.smoothingMask = 0xffffffffu;
        }
    }
    recalculateNormals();
    if (m_viewport) {
        m_viewport->setScene(&m_scene);
        m_viewport->setBakeSettings(m_settings);
    }
    log("已统一所有面片光滑组并重新计算顶点法线。");
}

void MainWindow::clearScene() {
    m_scene.clear();
    m_tree->clear();
    m_modelInfo->setText("未导入模型");
    m_uvInfo->setText("UV 通道：未检测");
    m_viewport->setScene(nullptr);
    log("场景已清空");
}

void MainWindow::log(const QString& text) {
    m_log->append(text);
    statusBar()->showMessage(text, 5000);
}
