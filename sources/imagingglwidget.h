// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#pragma once

#include "notice.h"
#include "selectionlist.h"
#include "stageviz.h"
#include "viewcamera.h"
#include <QOpenGLFunctions>
#include <QOpenGLWidget>

namespace stageviz {

class ImagingGLWidgetPrivate;
class ViewContext;

/**
 * @class ImagingGLWidget
 * @brief OpenGL-based viewport for rendering USD scenes.
 *
 * Provides a GPU accelerated rendering widget built on
 * QOpenGLWidget. The widget renders USD stages using the
 * USD ImagingGL renderer and supports interactive camera
 * navigation, draw modes, lighting options, and scene updates.
 *
 * The widget integrates with Session and SelectionList
 * to reflect scene data and selection changes.
 */
class ImagingGLWidget : public QOpenGLWidget, protected QOpenGLFunctions {
    Q_OBJECT
public:
    /**
     * @brief Rendering complexity levels.
     */
    enum ComplexityLevel { ComplexityLow, ComplexityMedium, ComplexityHigh, ComplexityVeryHigh };

    /**
     * @brief Supported rendering draw modes.
     */
    enum DrawMode { Points, Wireframe, WireframeOnSurface, ShadedFlat, ShadedSmooth, GeomOnly, GeomFlat, GeomSmooth };

public:
    /**
     * @brief Constructs the OpenGL imaging widget.
     *
     * @param parent Optional parent widget.
     */
    ImagingGLWidget(QWidget* parent = nullptr);

    /**
     * @brief Destroys the ImagingGLWidget instance.
     */
    virtual ~ImagingGLWidget();

    /** @name Context */
    ///@{

    /**
     * @brief Returns the current view context.
     */
    ViewContext* context() const;

    /**
     * @brief Sets the view context used by this widget.
     *
     * @param context View context for stage locking and command execution.
     */
    void setContext(ViewContext* context);

    ///@}

    /** @name Camera */
    ///@{

    /**
     * @brief Returns the current view camera.
     */
    ViewCamera viewCamera() const;

    /**
     * @brief Frames the specified bounding box.
     *
     * @param bbox Bounding box to frame.
     */
    void frame(const GfBBox3d& bbox);

    /**
     * @brief Resets the camera view.
     */
    void resetView();

    ///@}

    /** @name Capture */
    ///@{

    /**
     * @brief Captures the current rendered image.
     *
     * @return Image of the current viewport.
     */
    QImage captureImage();

    ///@}

    /** @name Lifecycle */
    ///@{

    /**
     * @brief Clears the current rendering state.
     */
    void close();

    ///@}

    /** @name Rendering Options */
    ///@{

    /**
     * @brief Returns the current draw mode.
     */
    DrawMode drawMode() const;

    /**
     * @brief Sets the draw mode.
     *
     * @param drawMode Rendering mode.
     */
    void setDrawMode(DrawMode drawMode);

    /**
     * @brief Returns the viewport clear color.
     */
    QColor clearColor() const;

    /**
     * @brief Sets the viewport clear color.
     *
     * @param color Background color.
     */
    void setClearColor(const QColor& color);

    /**
     * @brief Returns whether the default camera light is enabled.
     */
    bool defaultCameraLightEnabled() const;

    /**
     * @brief Enables or disables the default camera light.
     *
     * @param enabled Light state.
     */
    void enableDefaultCameraLight(bool enabled);

    /**
     * @brief Returns whether scene lights are enabled.
     */
    bool sceneLightsEnabled() const;

    /**
     * @brief Enables or disables lights defined in the USD scene.
     *
     * @param enabled Light state.
     */
    void enableSceneLights(bool enabled);

    /**
     * @brief Returns whether scene materials are enabled.
     */
    bool sceneShadersEnabled() const;

    /**
     * @brief Enables or disables USD materials during rendering.
     *
     * @param enabled Material state.
     */
    void enableSceneShaders(bool enabled);

    /**
     * @brief Returns whether  scene tree hud is displayed.
     */
    bool sceneTreeEnabled() const;

    /**
     * @brief Enables or disables scene tree hud.
     *
     * @param enabled Scene tree statistics display state.
     */
    void enableSceneTree(bool enabled);

    /**
     * @brief Returns whether rendering gpu performance hud are displayed.
     */
    bool gpuPerformanceEnabled() const;

    /**
     * @brief Enables or disables gpu performance hud.
     *
     * @param enabled Gpu performance hud display state.
     */
    void enableGpuPerformance(bool enabled);

    /**
     * @brief Returns whether rendering camera axis hud are displayed.
     */
    bool cameraAxisEnabled() const;

    /**
     * @brief Enables or disables camera axis hud.
     *
     * @param enabled Camera axis hud display state.
     */
    void enableCameraAxis(bool enabled);

    ///@}

    /** @name Renderer Outputs */
    ///@{

    /**
     * @brief Returns available renderer AOVs.
     */
    QList<QString> rendererAovs() const;

    /**
     * @brief Sets the active renderer AOV.
     *
     * @param aov Name of the AOV to display.
     */
    void setRendererAov(const QString& aov);

    ///@}

    /** @name Visible Capture */
    ///@{

    /**
     * @brief Captures visible prim paths from the current view.
     *
     * Runs a visibility query for the current camera and viewport and adds
     * the resulting prim paths to the internal captured set.
     */
    void captureVisible();

    /**
     * @brief Clears the captured visible prim paths.
     */
    void clearVisibleCapture();

    /**
     * @brief Returns the currently captured visible prim paths.
     *
     * @return Accumulated visible prim paths captured from one or more views.
     */
    QList<SdfPath> visibleCapturePaths() const;

    ///@}


    /** @name Scene Updates */
    ///@{

    /**
     * @brief Updates the stage used for rendering.
     *
     * @param stage USD stage to render.
     */
    void updateStage(UsdStageRefPtr stage);

    /**
     * @brief Updates the scene bounding box.
     *
     * @param bbox Scene bounding box.
     */
    void updateBoundingBox(const GfBBox3d& bbox);

    /**
     * @brief Updates the visibility mask for prims.
     *
     * @param paths Prim paths included in the mask.
     */
    void updateMask(const QList<SdfPath>& paths);

    /**
    * @brief Updates prims using a USD notice batch.
    *
    * Entries follow UsdNotice::ObjectsChanged semantics:
    * info-only changes, asset resyncs, and structural resyncs.
    *
    * @param batch Batched USD change entries.
    */
    void updatePrims(const NoticeBatch& batch);

    /**
     * @brief Updates the current selection.
     *
     * @param paths Selected prim paths.
     */
    void updateSelection(const QList<SdfPath>& paths);

    ///@}

Q_SIGNALS:

    /**
     * @brief Emitted when a frame has finished rendering.
     *
     * @param elapsed Rendering time in milliseconds.
     */
    void renderReady(qint64 elapsed);

    /**
     * @brief Emitted when a capture has finished rendering.
     *
     * @param elapsed Capture time in milliseconds.
     */
    void captureReady(qint64 elapsed);

protected:
    /** @name OpenGL Events */
    ///@{

    /**
     * @brief Initializes OpenGL resources.
     */
    void initializeGL() override;

    /**
     * @brief Performs OpenGL rendering.
     */
    void paintGL() override;

    /**
     * @brief Handles Qt paint events.
     */
    void paintEvent(QPaintEvent* event) override;

    ///@}

    /** @name Mouse Interaction */
    ///@{

    /**
     * @brief Handles mouse double-click events.
     */
    void mouseDoubleClickEvent(QMouseEvent* event) override;

    /**
     * @brief Handles mouse press events.
     */
    void mousePressEvent(QMouseEvent* event) override;

    /**
     * @brief Handles mouse move events.
     */
    void mouseMoveEvent(QMouseEvent* event) override;

    /**
     * @brief Handles mouse release events.
     */
    void mouseReleaseEvent(QMouseEvent* event) override;

    /**
     * @brief Handles mouse wheel events.
     */
    void wheelEvent(QWheelEvent* event) override;

    ///@}

private:
    QScopedPointer<ImagingGLWidgetPrivate> p;
};

}  // namespace stageviz
