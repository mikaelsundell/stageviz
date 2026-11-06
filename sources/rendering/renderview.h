// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#pragma once

#include "selectionlist.h"
#include "session.h"
#include "stageviz.h"
#include "viewportcapture.h"
#include <QJsonObject>
#include <QTreeWidget>
#include <pxr/usd/usd/stage.h>

PXR_NAMESPACE_USING_DIRECTIVE

namespace stageviz {

class RenderViewPrivate;

/**
 * @class RenderView
 * @brief Interactive viewport for rendering and navigating USD scenes.
 *
 * Provides a real-time rendering widget used to display the USD stage.
 * The view supports camera navigation, framing operations, rendering
 * modes, and scene visualization options such as lighting, materials,
 * grid display, and statistics overlays.
 *
 * The view integrates with a Session for scene content and a
 * SelectionList for prim selection synchronization.
 */
class RenderView : public QWidget {
    Q_OBJECT
public:
    /**
     * @brief Constructs the render view widget.
     * @param parent Optional parent widget.
     */
    RenderView(QWidget* parent = nullptr);

    /**
     * @brief Destroys the RenderView instance.
     */
    virtual ~RenderView();

    /**
     * @name Capture
     */
    /**
     * @{
     */

    /**
     * @brief Captures the current viewport image.
     * @return Image of the rendered viewport.
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
     * @name Visible Capture
     */
    /**
     * @{
     */

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
     * @return Accumulated visible prim paths captured from one or more views.
     */
    QList<SdfPath> visibleCapturePaths() const;

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
     */
    bool transformEnabled() const;
    /**
     * @brief Returns whether the gizmo edits pivot frames instead of object transforms.
     */
    bool pivotEnabled() const;

    /**
     * @brief Switches the existing manipulation tools to pivot-frame editing.
     */
    void setPivotEnabled(bool enabled);



    /**
     * @brief Enables or disables the viewport transform manipulator.
     *
     * @param enabled True to display and interact with the transform handle.
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
     * @brief Shows or hides the panel below the viewport.
     *
     * @param visible True to show the panel.
     */
    void setPanelVisible(bool visible);

    /**
     * @brief Sets the USD time code evaluated by the viewport.
     */
    void setTimeCode(double timeCode);

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
     * @brief Updates the Stageviz-owned auxiliary USD stage presented by the viewport.
     *
     * @param auxiliary Auxiliary USD stage to present.
     */
    void updateAuxiliary(UsdStageRefPtr auxiliary);

    /**
     * @}
     */

Q_SIGNALS:
    /**
     * @brief Forwarded when a viewport frame has finished rendering.
     */
    void renderReady(qint64 elapsed);

    /**
     * @brief Forwarded when a visible-prim capture has finished.
     */
    void captureReady(qint64 elapsed);

    /**
     * @brief Forwarded transient viewport status text.
     */
    void statusMessage(const QString& message);

    /**
     * @brief Forwarded when transient viewport status has finished.
     */
    void statusReady();

private:
    QScopedPointer<RenderViewPrivate> p;
};

}  // namespace stageviz
