// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#pragma once

#include "notice.h"
#include "selectionlist.h"
#include "stageviz.h"
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
 * navigation, draw modes, lighting options, grid display,
 * and scene updates.
 *
 * The widget integrates with Session and SelectionList
 * to reflect scene data and selection changes.
 */
class ImagingGLWidget : public QOpenGLWidget, protected QOpenGLFunctions {
    Q_OBJECT
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

    /** @name Renderer Outputs */
    ///@{

    /**
     * @brief Returns available renderer AOVs.
     */
    QList<QString> rendererAovs() const;

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
     * @brief Updates the Stageviz-owned auxiliary USD stage used by the renderer.
     *
     * The auxiliary stage is separate from the document stage and may contain
     * application-owned render content such as override materials, guides,
     * helpers, diagnostics, or other non-document scene data.
     *
     * @param auxiliary Auxiliary USD stage to present.
     */
    void updateAuxiliary(UsdStageRefPtr auxiliary);

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
