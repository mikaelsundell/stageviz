// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#pragma once

#include "selectionlist.h"
#include "session.h"
#include "stageviz.h"
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

    /** @brief Destroys the RenderView instance. */
    virtual ~RenderView();

    /** @name Capture */
    ///@{

    /**
     * @brief Captures the current viewport image.
     * @return Image of the rendered viewport.
     */
    QImage captureImage();

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

    /** @brief Clears the captured visible prim paths. */
    void clearVisibleCapture();

    /**
     * @brief Returns the currently captured visible prim paths.
     * @return Accumulated visible prim paths captured from one or more views.
     */
    QList<SdfPath> visibleCapturePaths() const;

    ///@}

    /** @name Transform */
    ///@{

    /**
     * @brief Returns whether transform interaction is enabled.
     */
    bool transformEnabled() const;

    /**
     * @brief Enables or disables the viewport transform manipulator.
     *
     * @param enabled True to display and interact with the transform handle.
     */
    void setTransformEnabled(bool enabled);

    ///@}

    /** @name Scene Updates */
    ///@{

    /**
     * @brief Updates the Stageviz-owned auxiliary USD stage presented by the viewport.
     *
     * @param auxiliary Auxiliary USD stage to present.
     */
    void updateAuxiliary(UsdStageRefPtr auxiliary);

    ///@}

private:
    QScopedPointer<RenderViewPrivate> p;
};

}  // namespace stageviz
