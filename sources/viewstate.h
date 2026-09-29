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
     * @brief Viewport double-sided rendering modes.
     *
     * Authored respects each USD primitives's authored doubleSided value.
     * DoubleSided disables back-face culling for all geometry.
     * SingleSided enables back-face culling for all geometry,
     * ignoring authored doubleSided values.
     */
    enum DoubleSidedMode { Primitive, DoubleSided, SingleSided };
    Q_ENUM(DoubleSidedMode)

    /**
     * @brief Ambient occlusion quality presets.
     *
     * Quality controls the number of spiral samples used by the Stageviz
     * screen-space ambient occlusion pass.
     */
    enum AmbientOcclusionQuality {
        AmbientOcclusionLow,
        AmbientOcclusionMedium,
        AmbientOcclusionHigh,
        AmbientOcclusionUltra
    };
    Q_ENUM(AmbientOcclusionQuality)

    /**
     * @brief Ambient occlusion debug presentation.
     */
    enum AmbientOcclusionDebugMode {
        AmbientOcclusionComposite,
        AmbientOcclusionCombined,
        AmbientOcclusionContact,
        AmbientOcclusionBroad
    };
    Q_ENUM(AmbientOcclusionDebugMode)

    /**
     * @brief Canonical Stageviz ambient occlusion settings and defaults.
     *
     * Contact occlusion emphasizes tight intersections and assembly gaps while
     * broad occlusion adds softer large-scale depth. Radii are expressed in
     * device pixels so the apparent effect remains stable while navigating.
     */
    struct AmbientOcclusionSettings {
        bool enabled = false;
        // Tight local occlusion for contacts, seams, and small recesses.
        float contactAmount = 1.0f;
        float contactRadius = 10.0f;
        // Softer large-scale occlusion for overall form definition.
        float broadAmount = 0.35f;
        float broadRadius = 80.0f;
        // Controls the shape and response of the obscurance.
        float normalBias = 0.06f;
        float falloff = 2.0f;
        float contrast = 1.0f;
        // Depth-aware filtering.
        bool blurEnabled = true;
        float blurRadius = 8.0f;
        float edgeSharpness = 0.75f;
        AmbientOcclusionQuality quality = AmbientOcclusionHigh;
        AmbientOcclusionDebugMode debugMode = AmbientOcclusionComposite;
    };

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

    /**
     * @name Camera
     */

    /**
     * @{
     */

    /**
     * @brief Returns the interactive view camera.
     */
    ViewCamera* camera() const;

    /**
     * @}
     */

    /**
     * @name Presentation
     */

    /**
     * @{
     */

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
     * The path identifies a material authored on Session::auxiliary().
     * It must be an absolute prim path within the auxiliary stage
     * Setting a non-empty path activates Override mode. Clearing the
     * path restores All materials when Override is active.
     */
    void setOverrideMaterial(const pxr::SdfPath& materialPath);

    /**
     * @}
     */

    /**
     * @name Lighting and Materials
     */

    /**
     * @{
     */

    /**
     * @brief Returns whether the default camera light is enabled.
     */
    bool defaultCameraLightEnabled() const;

    /**
     * @brief Enables or disables the default camera light.
     */
    void setDefaultCameraLightEnabled(bool enabled);

    /**
     * @brief Returns whether the default viewport dome light is enabled.
     */
    bool defaultDomeLightEnabled() const;

    /**
     * @brief Enables or disables the default viewport dome light.
     */
    void setDefaultDomeLightEnabled(bool enabled);

    /**
     * @brief Returns the custom dome-light texture path.
     *
     * An empty path uses OpenUSD's packaged default dome texture.
     */
    QString domeLightTexture() const;

    /**
     * @brief Sets the custom dome-light texture path.
     *
     * An empty path restores OpenUSD's packaged default dome texture.
     */
    void setDomeLightTexture(const QString& filename);

    /**
     * @brief Returns whether the dome texture is visible to the camera.
     */
    bool domeLightCameraVisibility() const;

    /**
     * @brief Sets whether the dome texture is visible to the camera.
     */
    void setDomeLightCameraVisibility(bool visible);

    /**
     * @name Ambient Occlusion
     *
     * These controls define the Stageviz document-only AO look. Viewport
     * support geometry and selection are rendered after this effect.
     */

    /** @{ */

    /** @brief Returns the complete ambient occlusion state. */
    const AmbientOcclusionSettings& ambientOcclusionSettings() const;
    bool ambientOcclusionEnabled() const;
    void setAmbientOcclusionEnabled(bool enabled);
    float ambientOcclusionContactAmount() const;
    void setAmbientOcclusionContactAmount(float amount);
    float ambientOcclusionContactRadius() const;
    void setAmbientOcclusionContactRadius(float radius);
    float ambientOcclusionBroadAmount() const;
    void setAmbientOcclusionBroadAmount(float amount);
    float ambientOcclusionBroadRadius() const;
    void setAmbientOcclusionBroadRadius(float radius);
    float ambientOcclusionNormalBias() const;
    void setAmbientOcclusionNormalBias(float bias);
    float ambientOcclusionFalloff() const;
    void setAmbientOcclusionFalloff(float falloff);
    float ambientOcclusionContrast() const;
    void setAmbientOcclusionContrast(float contrast);
    float ambientOcclusionEdgeSharpness() const;
    void setAmbientOcclusionEdgeSharpness(float sharpness);
    bool ambientOcclusionBlurEnabled() const;
    void setAmbientOcclusionBlurEnabled(bool enabled);
    float ambientOcclusionBlurRadius() const;
    void setAmbientOcclusionBlurRadius(float radius);
    AmbientOcclusionQuality ambientOcclusionQuality() const;
    void setAmbientOcclusionQuality(AmbientOcclusionQuality quality);
    AmbientOcclusionDebugMode ambientOcclusionDebugMode() const;
    void setAmbientOcclusionDebugMode(AmbientOcclusionDebugMode mode);

    /** @} */

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

    /**
     * @brief Returns the viewport double-sided rendering mode.
     */
    DoubleSidedMode doubleSidedMode() const;

    /**
     * @brief Sets the viewport double-sided rendering mode.
     */
    void setDoubleSidedMode(DoubleSidedMode mode);

    /**
     * @}
     */

    /**
     * @name Rendering
     */

    /**
     * @{
     */

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

    /**
     * @}
     */

    /**
     * @name HUD
     */

    /**
     * @{
     */

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

    /**
     * @}
     */

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
     * @brief Emitted when the default viewport dome light state changes.
     */
    void defaultDomeLightEnabledChanged(bool enabled);

    /**
     * @brief Emitted when the viewport dome texture changes.
     */
    void domeLightTextureChanged(const QString& filename);

    /**
     * @brief Emitted when dome camera visibility changes.
     */
    void domeLightCameraVisibilityChanged(bool visible);
    void ambientOcclusionEnabledChanged(bool enabled);
    void ambientOcclusionContactAmountChanged(float amount);
    void ambientOcclusionContactRadiusChanged(float radius);
    void ambientOcclusionBroadAmountChanged(float amount);
    void ambientOcclusionBroadRadiusChanged(float radius);
    void ambientOcclusionNormalBiasChanged(float bias);
    void ambientOcclusionFalloffChanged(float falloff);
    void ambientOcclusionContrastChanged(float contrast);
    void ambientOcclusionEdgeSharpnessChanged(float sharpness);
    void ambientOcclusionBlurEnabledChanged(bool enabled);
    void ambientOcclusionBlurRadiusChanged(float radius);
    void ambientOcclusionQualityChanged(AmbientOcclusionQuality quality);
    void ambientOcclusionDebugModeChanged(AmbientOcclusionDebugMode mode);

    /**
     * @brief Emitted when the scene light state changes.
     */
    void sceneLightsEnabledChanged(bool enabled);

    /**
     * @brief Emitted when the scene material state changes.
     */
    void sceneMaterialsEnabledChanged(bool enabled);

    /**
     * @brief Emitted when the viewport double-sided rendering mode changes.
     */
    void doubleSidedModeChanged(DoubleSidedMode mode);

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
