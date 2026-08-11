// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#pragma once

#include "stageviz.h"
#include <QColor>
#include <QObject>
#include <QScopedPointer>
#include <QString>
#include <pxr/usd/sdf/path.h>

namespace stageviz {

class ViewCamera;
class ViewStatePrivate;

/**
 * @class ViewState
 * @brief Shared state for viewport presentation and rendering.
 *
 * ViewState is owned by the session and shared with view widgets through
 * ViewContext. It is the source of truth for camera, presentation, lighting,
 * material, grid, HUD, render mode, complexity, and renderer output state.
 */
class ViewState : public QObject {
    Q_OBJECT

public:
    /**
     * @brief Viewport rendering modes.
     */
    enum RenderMode { Shaded, Wireframe };
    Q_ENUM(RenderMode)

    /**
     * @brief Viewport complexity levels.
     */
    enum ComplexityLevel { Low, Medium, High, VeryHigh };
    Q_ENUM(ComplexityLevel)

    /**
     * @brief Viewport material modes.
     */
    enum MaterialMode { All, Clay, Override };
    Q_ENUM(MaterialMode)

    /**
     * @brief Constructs a ViewState.
     *
     * @param parent Optional parent object.
     */
    explicit ViewState(QObject* parent = nullptr);

    /**
     * @brief Destroys the ViewState instance.
     */
    ~ViewState() override;

    /** @name Camera */
    ///@{

    /**
     * @brief Returns the interactive view camera.
     */
    ViewCamera* camera() const;

    ///@}

    /** @name Presentation */
    ///@{

    /**
     * @brief Returns the viewport background color.
     */
    QColor backgroundColor() const;

    /**
     * @brief Sets the viewport background color.
     */
    void setBackgroundColor(const QColor& color);

    /**
     * @brief Returns the viewport grid color.
     */
    QColor gridColor() const;

    /**
     * @brief Sets the viewport grid color.
     */
    void setGridColor(const QColor& color);

    /**
     * @brief Returns whether the viewport grid is displayed.
     */
    bool gridEnabled() const;

    /**
     * @brief Enables or disables the viewport grid.
     */
    void setGridEnabled(bool enabled);

    /**
     * @brief Returns the active viewport material mode.
     */
    MaterialMode materialMode() const;

    /**
     * @brief Sets the active viewport material mode.
     */
    void setMaterialMode(MaterialMode mode);

    /**
     * @brief Returns the viewport override material path.
     */
    pxr::SdfPath overrideMaterial() const;

    /**
     * @brief Sets the viewport override material path.
     *
     * The material is expected to exist in the composed stage, typically
     * authored in the USD session layer. Setting a non-empty path activates
     * the Override material mode. Clearing the path restores All materials
     * when Override is active.
     */
    void setOverrideMaterial(const pxr::SdfPath& materialPath);

    ///@}

    /** @name Lighting and Materials */
    ///@{

    /**
     * @brief Returns whether the default camera light is enabled.
     */
    bool defaultCameraLightEnabled() const;

    /**
     * @brief Enables or disables the default camera light.
     */
    void setDefaultCameraLightEnabled(bool enabled);

    /**
     * @brief Returns whether scene lights are enabled.
     */
    bool sceneLightsEnabled() const;

    /**
     * @brief Enables or disables scene lights.
     */
    void setSceneLightsEnabled(bool enabled);

    /**
     * @brief Returns whether authored scene materials are enabled.
     */
    bool sceneMaterialsEnabled() const;

    /**
     * @brief Enables or disables authored scene materials.
     */
    void setSceneMaterialsEnabled(bool enabled);

    ///@}

    /** @name Rendering */
    ///@{

    /**
     * @brief Returns the viewport render mode.
     */
    RenderMode renderMode() const;

    /**
     * @brief Sets the viewport render mode.
     */
    void setRenderMode(RenderMode mode);

    /**
     * @brief Returns the viewport complexity level.
     */
    ComplexityLevel complexityLevel() const;

    /**
     * @brief Sets the viewport complexity level.
     */
    void setComplexityLevel(ComplexityLevel level);

    /**
     * @brief Returns the renderer AOV name.
     */
    QString rendererAov() const;

    /**
     * @brief Sets the renderer AOV name.
     */
    void setRendererAov(const QString& aov);

    ///@}

    /** @name HUD */
    ///@{

    /**
     * @brief Returns whether scene statistics are displayed.
     */
    bool sceneStatsEnabled() const;

    /**
     * @brief Enables or disables scene statistics.
     */
    void setSceneStatsEnabled(bool enabled);

    /**
     * @brief Returns whether performance statistics are displayed.
     */
    bool performanceStatsEnabled() const;

    /**
     * @brief Enables or disables performance statistics.
     */
    void setPerformanceStatsEnabled(bool enabled);

    /**
     * @brief Returns whether the camera axis is displayed.
     */
    bool cameraAxisEnabled() const;

    /**
     * @brief Enables or disables the camera axis.
     */
    void setCameraAxisEnabled(bool enabled);

    ///@}

Q_SIGNALS:
    /**
     * @brief Emitted when the viewport background color changes.
     */
    void backgroundColorChanged(const QColor& color);

    /**
     * @brief Emitted when the viewport grid color changes.
     */
    void gridColorChanged(const QColor& color);

    /**
     * @brief Emitted when the viewport grid display state changes.
     */
    void gridEnabledChanged(bool enabled);

    /**
     * @brief Emitted when the viewport material mode changes.
     */
    void materialModeChanged(MaterialMode mode);

    /**
     * @brief Emitted when the viewport override material changes.
     */
    void overrideMaterialChanged(const pxr::SdfPath& materialPath);

    /**
     * @brief Emitted when the default camera light state changes.
     */
    void defaultCameraLightEnabledChanged(bool enabled);

    /**
     * @brief Emitted when the scene light state changes.
     */
    void sceneLightsEnabledChanged(bool enabled);

    /**
     * @brief Emitted when the scene material state changes.
     */
    void sceneMaterialsEnabledChanged(bool enabled);

    /**
     * @brief Emitted when the viewport render mode changes.
     */
    void renderModeChanged(RenderMode mode);

    /**
     * @brief Emitted when the viewport complexity level changes.
     */
    void complexityLevelChanged(ComplexityLevel level);

    /**
     * @brief Emitted when the renderer AOV changes.
     */
    void rendererAovChanged(const QString& aov);

    /**
     * @brief Emitted when the scene statistics display state changes.
     */
    void sceneStatsEnabledChanged(bool enabled);

    /**
     * @brief Emitted when the performance statistics display state changes.
     */
    void performanceStatsEnabledChanged(bool enabled);

    /**
     * @brief Emitted when the camera axis display state changes.
     */
    void cameraAxisEnabledChanged(bool enabled);

private:
    Q_DISABLE_COPY_MOVE(ViewState)
    QScopedPointer<ViewStatePrivate> p;
};

}  // namespace stageviz
