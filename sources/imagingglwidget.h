// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#pragma once

#include "notice.h"
#include "selectionlist.h"
#include "stageviz.h"
#include <QOpenGLFunctions>
#include <QOpenGLWidget>
#include <pxr/base/tf/token.h>

class QDragEnterEvent;
class QDragMoveEvent;
class QDropEvent;

namespace stageviz {

class ImagingGLWidgetPrivate;
class ViewContext;

/**
 * @class ImagingGLWidget
 * @brief OpenGL-based viewport for rendering USD scenes.
 *
 * Provides a GPU accelerated rendering widget built on QOpenGLWidget.
 * The widget renders USD stages using the Stageviz RenderEngine and supports
 * interactive camera navigation, picking, transform manipulation, material
 * drag and drop, draw modes, lighting options, grid display, and scene updates.
 *
 * The widget integrates with ViewContext, Session, and SelectionList to
 * reflect scene state and selection changes while routing edits through the
 * Stageviz command system.
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
     *
     * @return View context currently associated with the widget.
     */
    ViewContext* context() const;

    /**
     * @brief Sets the view context used by this widget.
     *
     * The view context provides access to the current stage, view state,
     * selection model, stage locks, and command execution.
     *
     * @param context View context to associate with the widget.
     */
    void setContext(ViewContext* context);

    ///@}

    /** @name Capture */
    ///@{

    /**
     * @brief Captures the current rendered viewport image.
     *
     * @return Image containing the current OpenGL framebuffer contents.
     */
    QImage captureImage();

    ///@}

    /** @name Transform */
    ///@{

    /**
     * @brief Returns whether transform interaction is enabled.
     *
     * @return True when the transform manipulator is enabled.
     */
    bool transformEnabled() const;

    /**
     * @brief Enables or disables the transform manipulator.
     *
     * @param enabled True to display and interact with the transform gizmo.
     */
    void setTransformEnabled(bool enabled);

    ///@}

    /** @name Lifecycle */
    ///@{

    /**
     * @brief Clears the current rendering and interaction state.
     *
     * Resets the current stage-related viewport state and releases renderer
     * state associated with the active stage.
     */
    void close();

    ///@}

    /** @name Renderer Outputs */
    ///@{

    /**
     * @brief Returns available renderer AOVs.
     *
     * @return Names of AOVs reported by the active Hydra render delegate.
     */
    QList<QString> rendererAovs() const;

    ///@}

    /** @name Visible Capture */
    ///@{

    /**
     * @brief Captures visible prim paths from the current view.
     *
     * Runs visibility queries for the current camera and viewport and adds
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
     * @brief Updates the primary USD stage used for rendering.
     *
     * @param stage USD stage to render.
     */
    void updateStage(UsdStageRefPtr stage);

    /**
     * @brief Updates the Stageviz-owned auxiliary USD stage used by the renderer.
     *
     * The auxiliary stage contains Stageviz render-support content such as the
     * viewport grid and built-in materials.
     *
     * @param auxiliary Auxiliary USD stage to present.
     */
    void updateAuxiliary(UsdStageRefPtr auxiliary);

    /**
     * @brief Updates the render-space stage up axis.
     *
     * The caller is responsible for translating application-level stage-up
     * state to the corresponding USD token.
     *
     * @param upAxis USD up-axis token, normally UsdGeomTokens->y or
     * UsdGeomTokens->z.
     */
    void updateStageUp(const TfToken& upAxis);

    /**
     * @brief Updates the scene bounding box.
     *
     * @param bbox Scene bounding box.
     */
    void updateBoundingBox(const GfBBox3d& bbox);

    /**
     * @brief Updates the visibility mask used by the viewport.
     *
     * An empty mask renders the complete stage. Otherwise rendering and
     * viewport picking are restricted to the supplied root paths.
     *
     * @param paths Prim paths included in the mask.
     */
    void updateMask(const QList<SdfPath>& paths);

    /**
     * @brief Updates viewport state in response to a USD notice batch.
     *
     * Entries follow UsdNotice::ObjectsChanged semantics and may contain
     * information-only changes, asset resyncs, and structural resyncs.
     *
     * @param batch Batched USD change entries.
     */
    void updatePrims(const NoticeBatch& batch);

    ///@}

Q_SIGNALS:
    /**
     * @brief Emitted when a viewport frame has finished rendering.
     *
     * @param elapsed Rendering time in milliseconds.
     */
    void renderReady(qint64 elapsed);

    /**
     * @brief Emitted when a visible-prim capture has finished.
     *
     * @param elapsed Capture time in milliseconds.
     */
    void captureReady(qint64 elapsed);

protected:
    /** @name OpenGL Events */
    ///@{

    /**
     * @brief Initializes OpenGL state and the viewport render engine.
     */
    void initializeGL() override;

    /**
     * @brief Renders the current USD stage into the OpenGL viewport.
     */
    void paintGL() override;

    /**
     * @brief Paints Qt overlays after the OpenGL viewport has rendered.
     *
     * @param event Paint event.
     */
    void paintEvent(QPaintEvent* event) override;

    ///@}

    /** @name Drag and Drop */
    ///@{

    /**
     * @brief Accepts Stageviz material drags over the viewport.
     *
     * Material drags use the Stageviz material MIME type and carry the USD
     * path of an existing material in the current stage.
     *
     * @param event Drag-enter event.
     */
    void dragEnterEvent(QDragEnterEvent* event) override;

    /**
     * @brief Tracks Stageviz material drags over the viewport.
     *
     * The prim below the cursor is resolved using the same Hydra intersection
     * path used for normal viewport picking.
     *
     * @param event Drag-move event.
     */
    void dragMoveEvent(QDragMoveEvent* event) override;

    /**
     * @brief Binds a dropped Stageviz material to the prim under the cursor.
     *
     * The binding is performed through the Stageviz command system so the
     * operation participates in undo and redo.
     *
     * @param event Drop event containing the Stageviz material MIME payload.
     */
    void dropEvent(QDropEvent* event) override;

    ///@}

    /** @name Mouse Interaction */
    ///@{

    /**
     * @brief Opens the viewport context menu at the requested position.
     *
     * @param event Context-menu event.
     */
    void contextMenuEvent(QContextMenuEvent* event) override;

    /**
     * @brief Handles viewport mouse double-click interaction.
     *
     * @param event Mouse event.
     */
    void mouseDoubleClickEvent(QMouseEvent* event) override;

    /**
     * @brief Handles viewport mouse button presses.
     *
     * @param event Mouse event.
     */
    void mousePressEvent(QMouseEvent* event) override;

    /**
     * @brief Handles viewport mouse movement.
     *
     * @param event Mouse event.
     */
    void mouseMoveEvent(QMouseEvent* event) override;

    /**
     * @brief Handles viewport mouse button releases.
     *
     * @param event Mouse event.
     */
    void mouseReleaseEvent(QMouseEvent* event) override;

    /**
     * @brief Handles viewport mouse-wheel camera navigation.
     *
     * @param event Wheel event.
     */
    void wheelEvent(QWheelEvent* event) override;

    ///@}

private:
    QScopedPointer<ImagingGLWidgetPrivate> p;
};

}  // namespace stageviz
