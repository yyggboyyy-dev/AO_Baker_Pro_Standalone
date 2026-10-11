#pragma once

#include "AOBaker.h"
#include "ModelData.h"
#include "ViewportWidget.h"
#include <QMainWindow>
#include <QTreeWidget>
#include <QLabel>
#include <QTextEdit>
#include <QSlider>
#include <QLineEdit>
#include <QComboBox>
#include <QSpinBox>
#include <QCheckBox>
#include <QPushButton>
#include <QFutureWatcher>

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

private slots:
    void importModel();
    void chooseOutputDir();
    void chooseGroundColor();
    void bakeVertex();
    void bakeTexture();
    void clearScene();

private:
    void buildUi();
    QWidget* group(const QString& title, QWidget* child);
    QWidget* buildSettingsPanel();
    QWidget* buildOutputPanel();
    void applyStyle();
    void refreshSceneInfo();
    void log(const QString& text);
    void runBake(bool texture);
    QString outputBaseName(const QString& suffix) const;

    SceneData m_scene;
    BakeSettings m_settings;
    QString m_outputDir;

    QTreeWidget* m_tree = nullptr;
    QLabel* m_modelInfo = nullptr;
    QLabel* m_uvInfo = nullptr;
    QTextEdit* m_log = nullptr;
    ViewportWidget* m_viewport = nullptr;
    QLabel* m_outputLabel = nullptr;
    QPushButton* m_colorButton = nullptr;
    QComboBox* m_resolution = nullptr;
    QComboBox* m_format = nullptr;
};
