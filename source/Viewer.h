#pragma once

#include <QOpenGLWidget>
#include <QOpenGLFunctions>
// #include <QOpenGLFunctions_3_2_Core>
// #include <QOpenGLFunctions_4_3_Core>
#include <QOpenGLShaderProgram>
#include <QVector3D>
#include <QMatrix4x4>

// class Viewer : public QOpenGLWidget
// class Viewer : public QOpenGLWidget, public QOpenGLFunctions_3_2_Core
class Viewer : public QOpenGLWidget, public QOpenGLFunctions
{
public:
    Viewer();
    virtual void initializeGL() override;

    QMap<QString, QOpenGLShaderProgram*> shaders;

    // Active camera properties
    QMatrix4x4 pvm;
    QVector3D eyePos;

    // Draw primitives
    void drawPoints(const QVector<QVector3D> &points, QColor color, QMatrix4x4 camera, bool isConnected = false);
	void drawOrientedPoints(const QVector< QVector3D > & points, const QVector< QVector3D > & normals, QColor color, QMatrix4x4 camera);
	void drawLines(const QVector<QVector3D> &lines, QColor color, QMatrix4x4 camera, QString shaderName);
    void drawBox(double width, double length, double height, QMatrix4x4 camera);
    void drawQuad(const QImage &img);
    void drawPlane(QVector3D normal, QVector3D origin, QMatrix4x4 camera);
    void drawTriangles(QColor useColor, const QVector<QVector3D> &points, const QVector<QVector3D> &normals, QMatrix4x4 camera);
};
