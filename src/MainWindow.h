#pragma once

#include "AOBaker.h"
#include "ModelData.h"
#include "ViewportWidget.h"
#include <QMainWindow>
#include <QJsonObject>
#include <QTreeWidget>
#include <QLabel>
#include <QTextEdit>
#include <QSlider>
#include <QLineEdit>
#include <QComboBox>
#include <QSpinBox>
#include <QCheckBox>
#include <QPushButton>
#include <QAction>
#include <QPixmap>
#include <QStringList>

class QSplitter;
class QGroupBox;
class QTimer;

class SliderRow : public QWidget {
    Q_OBJECT
public:
    SliderRow(float min, float max, float value, int decimals, QWidget* parent = nullptr);
    float value() const;
signals:
    void valueChanged(float value);
private slots:
    void fromSlider(int raw);
    void fromEdit();
private:
    float m_min, m_max;
    int m_decimals, m_scale;
    QSlider* m_slider;
    QLineEdit* m_edit;
};

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);

protected:
    void closeEvent(QCloseEvent* event) override;

private slots:
    void importModel();
    void importRecentModel();
    void chooseOutputDir();
    void chooseGroundColor();
    void chooseMaterialColor();
    void choosePaintColor();
    void repairFaceOrientation();
    void unifySmoothingGroups();
    void bakeVertex();
    void bakeTexture();
    void exportCurrentModel();
    void clearScene();
    void newScene();
    void saveProject();
    void saveProjectAs();
    void loadProject();
    void openPreferences();
    void undoActionTriggered();
    void redoActionTriggered();
    void clearVertexColors();
    void batchProcessModels();
    void runModelChecker();
    void runUvChecker();
    void exportLog();
    void clearLog();
    void onMeshSelected(int meshIndex, const QString& meshName);
    void onSettingsChanged();
    void keyPressEvent(QKeyEvent* event) override;

private:
    void buildUi();
    void buildMenus();
    QWidget* group(const QString& title, QWidget* child);
    QWidget* buildSettingsPanel();
    QWidget* buildVertexPaintPanel();
    QWidget* buildOutputPanel();
    QWidget* buildPreviewPanel();
    QPixmap renderPreviewPixmap(const QString& mode, const QSize& size, bool before = false) const;
    void updatePreviewThumbnail();
    void openPreviewLarge();
    void savePreviewImage();
    void copyPreviewImage();
    void applyStyle();
    void refreshSceneInfo();
    void showMeshContextMenu(const QPoint& pos);
    void log(const QString& text);
    void runBake(bool texture);
    void recalculateNormals(bool splitNormals = false);
    QString outputBaseName(const QString& suffix) const;
    void markDirty(bool dirty = true);
    void updateWindowTitle();
    bool confirmLoseUnsavedChanges(const QString& actionText);
    QString projectBaseName() const;
    void updateStatusSummary();
    void updateOutputFormatUi();
    void applyNativeWindowPolish();
    void toggleFullScreenViewport();
    void resetPanelLayout();
    void savePanelLayout();
    void restorePanelLayout();
    void updateRecentFilesMenu();
    void addRecentFile(const QString& path);
    QString settingsDir() const;
    void loadAppSettings();
    void saveAppSettings() const;
    bool saveProjectToFile(const QString& path);
    bool loadProjectFromFile(const QString& path);
    QJsonObject saveProjectJson() const;
    bool restoreProjectJson(const QJsonObject& root);
    bool m_bakeRunning = false;
    bool m_viewportFullScreen = false;

    SceneData m_scene;
    BakeSettings m_settings;
    QString m_outputDir;
    QString m_currentProjectPath;
    QString m_lastBakeCachePath;
    QStringList m_recentFiles;
    bool m_isDirty = false;

    QTreeWidget* m_tree = nullptr;
    QWidget* m_paintColumn = nullptr;
    QGroupBox* m_modelTreeGroup = nullptr;
    QGroupBox* m_modelInfoGroup = nullptr;
    QGroupBox* m_uvGroup = nullptr;
    QGroupBox* m_logGroup = nullptr;
    QGroupBox* m_aoSettingsGroup = nullptr;
    QGroupBox* m_outputGroup = nullptr;
    QGroupBox* m_previewGroup = nullptr;
    QGroupBox* m_previewPanelGroup = nullptr;
    QGroupBox* m_bakeOutputGroup = nullptr;
    QWidget* m_previewImageBox = nullptr;
    QPushButton* m_previewImage = nullptr;
    QLabel* m_previewInfo = nullptr;
    QComboBox* m_previewMode = nullptr;
    QPushButton* m_previewCompareButton = nullptr;
    QTimer* m_previewUpdateTimer = nullptr;
    QPixmap m_currentPreviewPixmap;
    QPixmap m_beforePreviewPixmap;
    bool m_previewCompareEnabled = false;
    QWidget* m_rightPanel = nullptr;
    QWidget* m_leftPanel = nullptr;
    QLabel* m_modelInfo = nullptr;
    QLabel* m_uvInfo = nullptr;
    QTextEdit* m_log = nullptr;
    QSplitter* m_rootSplitter = nullptr;
    ViewportWidget* m_viewport = nullptr;
    QLabel* m_outputLabel = nullptr;
    QPushButton* m_colorButton = nullptr;
    QPushButton* m_materialColorButton = nullptr;
    QPushButton* m_paintColorButton = nullptr;
    QLineEdit* m_outputNameEdit = nullptr;
    QComboBox* m_resolution = nullptr;
    QComboBox* m_format = nullptr;
    QComboBox* m_bitDepth = nullptr;
    QComboBox* m_splitMode = nullptr;
    QComboBox* m_vertexWriteMode = nullptr;
    QComboBox* m_bakeScope = nullptr;
    QPushButton* m_autoOpenOutputDir = nullptr;
    QLabel* m_formatHint = nullptr;
    QLabel* m_statusSummary = nullptr;
    QAction* m_undoAction = nullptr;
    QAction* m_redoAction = nullptr;
    QMenu* m_recentFilesMenu = nullptr;
    QPushButton* m_ignoreBackfacesCheck = nullptr;
    QPushButton* m_lockRedCheck = nullptr;
    QPushButton* m_lockGreenCheck = nullptr;
    QPushButton* m_lockBlueCheck = nullptr;
};
