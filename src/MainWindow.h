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
#include <QStringList>

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
    void onMeshSelected(int meshIndex, const QString& meshName);
    void onSettingsChanged();

private:
    void buildUi();
    void buildMenus();
    QWidget* group(const QString& title, QWidget* child);
    QWidget* buildSettingsPanel();
    QWidget* buildVertexPaintPanel();
    QWidget* buildOutputPanel();
    void applyStyle();
    void refreshSceneInfo();
    void log(const QString& text);
    void runBake(bool texture);
    void recalculateNormals();
    QString outputBaseName(const QString& suffix) const;
    void markDirty(bool dirty = true);
    void updateWindowTitle();
    bool confirmLoseUnsavedChanges(const QString& actionText);
    QString projectBaseName() const;
    void updateStatusSummary();
    void updateOutputFormatUi();
    void updateRecentFilesMenu();
    void addRecentFile(const QString& path);
    QString settingsDir() const;
    void loadAppSettings();
    void saveAppSettings() const;
    bool saveProjectToFile(const QString& path);
    bool loadProjectFromFile(const QString& path);
    QJsonObject saveProjectJson() const;
    bool restoreProjectJson(const QJsonObject& root);

    SceneData m_scene;
    BakeSettings m_settings;
    QString m_outputDir;
    QString m_currentProjectPath;
    QString m_lastBakeCachePath;
    QStringList m_recentFiles;
    bool m_isDirty = false;

    QTreeWidget* m_tree = nullptr;
    QLabel* m_modelInfo = nullptr;
    QLabel* m_uvInfo = nullptr;
    QTextEdit* m_log = nullptr;
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
    QLabel* m_formatHint = nullptr;
    QLabel* m_statusSummary = nullptr;
    QAction* m_undoAction = nullptr;
    QAction* m_redoAction = nullptr;
    QMenu* m_recentFilesMenu = nullptr;
    QCheckBox* m_ignoreBackfacesCheck = nullptr;
    QCheckBox* m_lockRedCheck = nullptr;
    QCheckBox* m_lockGreenCheck = nullptr;
    QCheckBox* m_lockBlueCheck = nullptr;
};
