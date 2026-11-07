// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#pragma once

#include "notice.h"
#include "selectionlist.h"
#include "stageviz.h"
#include "viewportcapture.h"
#include "viewportgizmo.h"
#include <QJsonObject>
#include <QOpenGLFunctions>
#include <QOpenGLWidget>
#include <pxr/base/tf/token.h>

class QDragEnterEvent;
class QDragMoveEvent;
class QDropEvent;

namespace stageviz {

class ViewportWidgetPrivate;
class ViewContext;

/**
 * @class ViewportWidget
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
class ViewportWidget : public QOpenGLWidget, protected QOpenGLFunctions {
    Q_OBJECT
public:
    /**
     * @brief Picks an editor light guide using logical viewport coordinates.
     */
    SdfPath pickLight(const QPointF& pos);

    /**
     * @brief Returns document light guides intersecting a logical sweep rectangle.
     */
    QList<SdfPath> lightPathsInRect(const QRectF& rect);

    /**
     * @brief Constructs the viewport widget.
     *
     * @param parent Optional parent widget.
     */
    ViewportWidget(QWidget* parent = nullptr);

    /**
     * @brief Destroys the ViewportWidget instance.
     */
    virtual ~ViewportWidget();

    /**
     * @name Context
     */
    /**
     * @{
     */

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

    /**
     * @}
     */

    /**
     * @name Capture
     */
    /**
     * @{
     */

    /**
     * @brief Captures the current rendered viewport image.
     *
     * @return Image containing the current OpenGL framebuffer contents.
     */
    QImage captureImage();

    /**
     * @brief Captures document colors and selectable object paths from this viewport.
     *
     * @param error Receives a failure reason; must be called on the GUI thread.
     *
     * @return Owned snapshot with top-left image coordinates.
     */
    ViewportCapture captureViewport(QString& error);

    /**
     * @brief Reads current USD and last-synchronized Hydra state for one geometry prim.
     *
     * Does not render, read back GPU buffers, author USD, or change selection.
     * Must run on the viewport GUI thread.
     *
     * @param path Exact USD geometry path, including native instance proxies.
     * @param error Receives an unavailable-stage, renderer, or path error.
     *
     * @return Owned diagnostic data, or an empty object on failure.
     */
    QJsonObject inspectPrim(const SdfPath& path, QString& error);



    /**
     * @}
     */

    /**
     * @name Transform
     */
    /**
     * @{
     */

    /**
     * @brief Returns whether transform interaction is enabled.
     *
     * @return True when the transform manipulator is enabled.
     */
    bool transformEnabled() const;
    /**
     * @brief Returns whether the current selection has a visible transform handle.
     */
    bool transformHandleAvailable();
    /**
     * @brief Returns whether the gizmo edits pivot frames instead of object transforms.
     */
    bool pivotEnabled() const;

    /**
     * @brief Switches the existing manipulation tools to pivot-frame editing.
     */
    void setPivotEnabled(bool enabled);

    /**
     * @brief Returns the active transform coordinate frame.
     */
    ViewportGizmo::ReferenceFrame transformReferenceFrame() const;

    /**
     * @brief Sets the transform coordinate frame.
     */
    void setTransformReferenceFrame(ViewportGizmo::ReferenceFrame frame);

    /**
     * @brief Applies numeric values using the active transform tool.
     *
     * Translation uses stage units, rotation uses degrees, and scale uses factors.
     */
    bool applyNumericTransform(const GfVec3d& values, QString& error);

    /**
     * @brief Starts a live numeric transform edit.
     */
    bool beginNumericTransform(QString& error);

    /**
     * @brief Updates the live numeric transform preview.
     */
    bool updateNumericTransform(const GfVec3d& values, QString& error);

    /**
     * @brief Commits the live numeric transform edit.
     */
    void finishNumericTransform();

    /**
     * @brief Enables or disables the transform manipulator.
     *
     * @param enabled True to display and interact with the transform gizmo.
     */
    void setTransformEnabled(bool enabled);

    /**
     * @brief Returns whether the move gizmo is active.
     */
    bool moveEnabled() const;

    /**
     * @brief Activates or deactivates the move gizmo.
     */
    void setMoveEnabled(bool enabled);

    /**
     * @brief Returns whether the rotate gizmo is active.
     */
    bool rotateEnabled() const;

    /**
     * @brief Activates or deactivates the rotate gizmo.
     */
    void setRotateEnabled(bool enabled);

    /**
     * @brief Returns whether the scale gizmo is active.
     */
    bool scaleEnabled() const;

    /**
     * @brief Activates or deactivates the scale gizmo.
     */
    void setScaleEnabled(bool enabled);

    /**
     * @}
     */

    /**
     * @name Lifecycle
     */
    /**
     * @{
     */

    /**
     * @brief Clears the current rendering and interaction state.
     *
     * Resets the current stage-related viewport state and releases renderer
     * state associated with the active stage.
     */
    void close();

    /**
     * @}
     */

    /**
     * @name Renderer Outputs
     */
    /**
     * @{
     */

    /**
     * @brief Returns available renderer AOVs.
     *
     * @return Names of AOVs reported by the active Hydra render delegate.
     */
    QList<QString> rendererAovs() const;

    /**
     * @}
     */

    /**
     * @name Visible Capture
     */
    /**
     * @{
     */

    /**
     * @brief Scans visible prim paths after synchronizing the current view.
     *
     * Uses the shared 4-by-4 tiled scene-ID scan. Must run on the GUI thread
     * with a rendered stage. The previous GL context is restored afterward.
     *
     * @param accumulate Add results to the shared menu collection when true;
     *                   otherwise leave the collection unchanged.
     *
     * @return Accumulated paths, or the current scan; empty if no view is available.
     */
    QList<SdfPath> capturePaths(bool accumulate = true);

    /**
     * @brief Adds a scan to the visible collection through capturePaths().
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

    /**
     * @}
     */

    /**
     * @name Scene Updates
     */
    /**
     * @{
     */

    /**
     * @brief Updates the primary USD stage used for rendering.
     *
     * @param stage USD stage to render.
     */
    void updateStage(UsdStageRefPtr stage);

    /**
     * @brief Sets the USD time code used to evaluate the viewport.
     *
     * @param timeCode Stage time code to render.
     */
    void setTimeCode(double timeCode);

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

    /**
     * @}
     */

Q_SIGNALS:
    /**
     * @brief Forwarded when the active transform tool or reference frame changes.
     */
    void transformStateChanged();

    /**
     * @brief Emitted when the active selection changes.
     */
    void transformSelectionChanged();

    /**
     * @brief Forwards the current gizmo delta to the numeric transform fields.
     */
    void transformInputValuesChanged(const GfVec3d& values);

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

    /**
     * @brief Emitted when the viewport has a transient status message.
     *
     * This is intended for high-frequency interactive feedback such as transform
     * gizmo values and is not routed through the application notification log.
     *
     * @param message Status text to display.
     */
    void statusMessage(const QString& message);

    /**
     * @brief Emitted when transient viewport status has finished.
     *
     * Receivers should restore their normal ready/idle status text.
     */
    void statusReady();

protected:
    /**
     * @name OpenGL Events
     */
    /**
     * @{
     */

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

    /**
     * @}
     */

    /**
     * @name Drag and Drop
     */
    /**
     * @{
     */

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

    /**
     * @}
     */

    /**
     * @name Mouse Interaction
     */
    /**
     * @{
     */

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

    /**
     * @}
     */

private:
    QScopedPointer<ViewportWidgetPrivate> p;
};

}  // namespace stageviz
