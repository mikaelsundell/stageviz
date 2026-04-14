// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#pragma once

#include "selectionlist.h"
#include "session.h"
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
 * and statistics overlays.
 *
 * The view integrates with a Session for scene content and a
 * SelectionList for prim selection synchronization.
 */
class RenderView : public QWidget {
    Q_OBJECT
public:
    /**
     * @brief Rendering modes supported by the viewport.
     */
    enum RenderMode {
        Shaded,
        Wireframe,
    };

public:
    /**
     * @brief Constructs the render view widget.
     *
     * @param parent Optional parent widget.
     */
    RenderView(QWidget* parent = nullptr);

    /**
     * @brief Destroys the RenderView instance.
     */
    virtual ~RenderView();

    /** @name Capture */
    ///@{

    /**
     * @brief Captures the current viewport image.
     *
     * @return Image of the rendered viewport.
     */
    QImage captureImage();

    ///@}

    /** @name Camera Control */
    ///@{

    /**
     * @brief Frames the entire scene.
     */
    void frameAll();

    /**
     * @brief Frames the currently selected prims.
     */
    void frameSelected();

    /**
     * @brief Resets the camera to its default view.
     */
    void resetView();

    ///@}

    /** @name Appearance */
    ///@{

    /**
     * @brief Returns the viewport background color.
     */
    QColor backgroundColor() const;

    /**
     * @brief Sets the viewport background color.
     *
     * @param color Background color.
     */
    void setBackgroundColor(const QColor& color);

    ///@}

    /** @name Lighting and Materials */
    ///@{

    /**
     * @brief Returns whether the default camera light is enabled.
     */
    bool defaultCameraLightEnabled() const;

    /**
     * @brief Enables or disables the default camera light.
     *
     * @param enabled Light state.
     */
    void setDefaultCameraLightEnabled(bool enabled);

    /**
     * @brief Returns whether scene lights are enabled.
     */
    bool sceneLightsEnabled() const;

    /**
     * @brief Enables or disables lights defined in the USD scene.
     *
     * @param enabled Light state.
     */
    void setSceneLightsEnabled(bool enabled);

    /**
     * @brief Returns whether scene materials are enabled.
     */
    bool sceneMaterialsEnabled() const;

    /**
     * @brief Enables or disables USD materials during rendering.
     *
     * @param enabled Material state.
     */
    void setSceneMaterialsEnabled(bool enabled);

    ///@}

    /** @name View Options */
    ///@{

    /**
     * @brief Returns whether  scene tree hud is displayed.
     */
    bool sceneTreeEnabled() const;

    /**
     * @brief Enables or disables scene tree hud.
     *
     * @param enabled Scene tree statistics display state.
     */
    void setSceneTreeEnabled(bool enabled);

    /**
     * @brief Returns whether rendering gpu performance hud are displayed.
     */
    bool gpuPerformanceEnabled() const;

    /**
     * @brief Enables or disables gpu performance hud.
     *
     * @param enabled Gpu performance hud display state.
     */
    void setGpuPerformanceEnabled(bool enabled);

    /**
     * @brief Returns whether rendering camera axis hud are displayed.
     */
    bool cameraAxisEnabled() const;

    /**
     * @brief Enables or disables camera axis hud.
     *
     * @param enabled Camera axis hud display state.
     */
    void setCameraAxisEnabled(bool enabled);

    /**
     * @brief Returns the current render mode.
     */
    RenderMode renderMode() const;

    /**
     * @brief Sets the render mode.
     *
     * @param renderMode Rendering mode.
     */
    void setRenderMode(RenderMode renderMode);

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

private:
    QScopedPointer<RenderViewPrivate> p;
};

}  // namespace stageviz
