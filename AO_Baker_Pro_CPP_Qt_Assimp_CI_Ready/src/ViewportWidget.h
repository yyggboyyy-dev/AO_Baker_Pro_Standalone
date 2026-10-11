#pragma once

#include "ModelData.h"
#include <QOpenGLWidget>

class ViewportWidget : public QOpenGLWidget {
    Q_OBJECT
public:
    explicit ViewportWidget(QWidget* parent = nullptr);
    void setScene(const SceneData* scene);

protected:
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;

private:
    const SceneData* m_scene = nullptr;
};
