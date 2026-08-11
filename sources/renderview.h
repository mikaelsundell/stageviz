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

    /** @name Camera Control */
    ///@{

    /** @brief Frames the entire scene. */
    void frameAll();

    /** @brief Frames the currently selected prims. */
    void frameSelected();

    /** @brief Resets the camera to its default view. */
    void resetView();

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

private:
    QScopedPointer<RenderViewPrivate> p;
};

}  // namespace stageviz
