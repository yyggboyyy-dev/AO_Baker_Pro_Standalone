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
#include <QMessageBox>
#include <QProgressDialog>
#include <QSplitter>
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

void MainWindow::buildUi() {
    auto* toolbar = addToolBar("工具");
    toolbar->addAction("导入模型", this, &MainWindow::importModel);
    toolbar->addAction("选择输出目录", this, &MainWindow::chooseOutputDir);
    toolbar->addSeparator();
    toolbar->addAction("烘焙AO到顶点色", this, &MainWindow::bakeVertex);
    toolbar->addAction("烘焙AO到贴图", this, &MainWindow::bakeTexture);
    toolbar->addAction("修复面片朝向", this, &MainWindow::repairFaceOrientation);
    toolbar->addAction("清空场景", this, &MainWindow::clearScene);

    auto* root = new QSplitter(Qt::Horizontal, this);
    setCentralWidget(root);

    auto* left = new QWidget(this);
    auto* leftLayout = new QVBoxLayout(left);
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
    leftLayout->addWidget(group("模型", m_tree), 4);
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

    root->addWidget(left);
    root->addWidget(center);
    root->addWidget(right);
    root->setSizes({260, 610, 350});
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

QWidget* MainWindow::buildOutputPanel() {
    auto* box = new QGroupBox("输出设置（贴图）", this);
    auto* form = new QFormLayout(box);
    m_resolution = new QComboBox(this);
    m_resolution->addItems({"512", "1024", "2048", "4096", "8192"});
    m_resolution->setCurrentText("2048");
    connect(m_resolution, &QComboBox::currentTextChanged, this, [this](const QString& v){ m_settings.textureResolution = v.toInt(); });
    m_format = new QComboBox(this);
    m_format->addItems({"PNG", "JPG", "BMP"});
    connect(m_format, &QComboBox::currentTextChanged, this, [this](const QString& v){ m_settings.textureFormat = v == "JPG" ? "JPEG" : v; });
    m_outputLabel = new QLabel(m_outputDir, this);
    m_outputLabel->setWordWrap(true);
    auto* pick = new QPushButton("选择输出目录", this);
    connect(pick, &QPushButton::clicked, this, &MainWindow::chooseOutputDir);
    form->addRow("贴图分辨率", m_resolution);
    form->addRow("贴图格式", m_format);
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

QString MainWindow::outputBaseName(const QString& suffix) const {
    QDir().mkpath(m_outputDir);
    return m_outputDir + "/" + QFileInfo(m_scene.sourcePath).completeBaseName() + suffix;
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
            const QString texOut = outputBaseName("_AO." + (m_settings.textureFormat == "JPEG" ? QString("jpg") : m_settings.textureFormat.toLower()));
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
