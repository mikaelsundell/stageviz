// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz
#include "viewstate.h"
#include "viewcamera.h"
#include <algorithm>
namespace stageviz {
class ViewStatePrivate {
public:
    void init();
    struct Data {
        QScopedPointer<ViewCamera> viewCamera;
        QColor backgroundColor;
        QColor gridColor;
        bool gridEnabled = true;
        ViewState::MaterialMode materialMode = ViewState::All;
        pxr::SdfPath overrideMaterial;
        bool defaultCameraLightEnabled = true;
        bool defaultDomeLightEnabled = false;
        QString domeLightTexture;
        bool domeLightCameraVisibility = false;
        ViewState::AmbientOcclusionSettings ambientOcclusion;
        bool sceneLightsEnabled = true;
        bool sceneMaterialsEnabled = true;
        ViewState::DoubleSidedMode doubleSidedMode = ViewState::Primitive;
        ViewState::RenderMode renderMode = ViewState::Shaded;
        ViewState::ComplexityLevel complexityLevel = ViewState::Low;
        QString rendererAov = QStringLiteral("color");
        bool sceneStatsEnabled = true;
        bool performanceStatsEnabled = false;
        bool cameraAxisEnabled = true;
    };
    Data d;
};
void
ViewStatePrivate::init()
{
    d.viewCamera.reset(new ViewCamera());
}
ViewState::ViewState(QObject* parent)
    : QObject(parent)
    , p(new ViewStatePrivate())
{
    p->init();
}
ViewState::~ViewState() = default;
ViewCamera*
ViewState::camera() const
{
    return p->d.viewCamera.data();
}
QColor
ViewState::backgroundColor() const
{
    return p->d.backgroundColor;
}
void
ViewState::setBackgroundColor(const QColor& color)
{
    if (color == p->d.backgroundColor)
        return;
    p->d.backgroundColor = color;
    Q_EMIT backgroundColorChanged(color);
}
QColor
ViewState::gridColor() const
{
    return p->d.gridColor;
}
void
ViewState::setGridColor(const QColor& color)
{
    if (color == p->d.gridColor)
        return;
    p->d.gridColor = color;
    Q_EMIT gridColorChanged(color);
}
bool
ViewState::gridEnabled() const
{
    return p->d.gridEnabled;
}
void
ViewState::setGridEnabled(bool enabled)
{
    if (enabled == p->d.gridEnabled)
        return;
    p->d.gridEnabled = enabled;
    Q_EMIT gridEnabledChanged(enabled);
}
ViewState::MaterialMode
ViewState::materialMode() const
{
    return p->d.materialMode;
}
void
ViewState::setMaterialMode(MaterialMode mode)
{
    if (mode == p->d.materialMode)
        return;
    p->d.materialMode = mode;
    Q_EMIT materialModeChanged(mode);
}
pxr::SdfPath
ViewState::overrideMaterial() const
{
    return p->d.overrideMaterial;
}
void
ViewState::setOverrideMaterial(const pxr::SdfPath& materialPath)
{
    if (materialPath != p->d.overrideMaterial) {
        p->d.overrideMaterial = materialPath;
        Q_EMIT overrideMaterialChanged(materialPath);
    }
    if (!materialPath.IsEmpty())
        setMaterialMode(Override);
    else if (p->d.materialMode == Override)
        setMaterialMode(All);
}
bool
ViewState::defaultCameraLightEnabled() const
{
    return p->d.defaultCameraLightEnabled;
}
void
ViewState::setDefaultCameraLightEnabled(bool enabled)
{
    if (enabled == p->d.defaultCameraLightEnabled)
        return;
    p->d.defaultCameraLightEnabled = enabled;
    Q_EMIT defaultCameraLightEnabledChanged(enabled);
}
bool
ViewState::defaultDomeLightEnabled() const
{
    return p->d.defaultDomeLightEnabled;
}
void
ViewState::setDefaultDomeLightEnabled(bool enabled)
{
    if (enabled == p->d.defaultDomeLightEnabled)
        return;
    p->d.defaultDomeLightEnabled = enabled;
    Q_EMIT defaultDomeLightEnabledChanged(enabled);
}
QString
ViewState::domeLightTexture() const
{
    return p->d.domeLightTexture;
}
void
ViewState::setDomeLightTexture(const QString& filename)
{
    const QString normalized = filename.trimmed();
    if (normalized == p->d.domeLightTexture)
        return;
    p->d.domeLightTexture = normalized;
    Q_EMIT domeLightTextureChanged(normalized);
}
bool
ViewState::domeLightCameraVisibility() const
{
    return p->d.domeLightCameraVisibility;
}
void
ViewState::setDomeLightCameraVisibility(bool visible)
{
    if (visible == p->d.domeLightCameraVisibility)
        return;
    p->d.domeLightCameraVisibility = visible;
    Q_EMIT domeLightCameraVisibilityChanged(visible);
}
const ViewState::AmbientOcclusionSettings&
ViewState::ambientOcclusionSettings() const
{
    return p->d.ambientOcclusion;
}
bool
ViewState::ambientOcclusionEnabled() const
{
    return p->d.ambientOcclusion.enabled;
}
void
ViewState::setAmbientOcclusionEnabled(bool enabled)
{
    if (enabled == p->d.ambientOcclusion.enabled)
        return;
    p->d.ambientOcclusion.enabled = enabled;
    Q_EMIT ambientOcclusionEnabledChanged(enabled);
}
float
ViewState::ambientOcclusionContactAmount() const
{
    return p->d.ambientOcclusion.contactAmount;
}
void
ViewState::setAmbientOcclusionContactAmount(float amount)
{
    amount = std::clamp(amount, 0.0f, 10.0f);
    if (amount == p->d.ambientOcclusion.contactAmount)
        return;
    p->d.ambientOcclusion.contactAmount = amount;
    Q_EMIT ambientOcclusionContactAmountChanged(amount);
}
float
ViewState::ambientOcclusionContactRadius() const
{
    return p->d.ambientOcclusion.contactRadius;
}
void
ViewState::setAmbientOcclusionContactRadius(float radius)
{
    radius = std::clamp(radius, 1.0f, 128.0f);
    if (radius == p->d.ambientOcclusion.contactRadius)
        return;
    p->d.ambientOcclusion.contactRadius = radius;
    Q_EMIT ambientOcclusionContactRadiusChanged(radius);
}
float
ViewState::ambientOcclusionBroadAmount() const
{
    return p->d.ambientOcclusion.broadAmount;
}
void
ViewState::setAmbientOcclusionBroadAmount(float amount)
{
    amount = std::clamp(amount, 0.0f, 10.0f);
    if (amount == p->d.ambientOcclusion.broadAmount)
        return;
    p->d.ambientOcclusion.broadAmount = amount;
    Q_EMIT ambientOcclusionBroadAmountChanged(amount);
}
float
ViewState::ambientOcclusionBroadRadius() const
{
    return p->d.ambientOcclusion.broadRadius;
}
void
ViewState::setAmbientOcclusionBroadRadius(float radius)
{
    radius = std::clamp(radius, 1.0f, 512.0f);
    if (radius == p->d.ambientOcclusion.broadRadius)
        return;
    p->d.ambientOcclusion.broadRadius = radius;
    Q_EMIT ambientOcclusionBroadRadiusChanged(radius);
}
float
ViewState::ambientOcclusionNormalBias() const
{
    return p->d.ambientOcclusion.normalBias;
}
void
ViewState::setAmbientOcclusionNormalBias(float bias)
{
    bias = std::clamp(bias, 0.0f, 0.5f);
    if (bias == p->d.ambientOcclusion.normalBias)
        return;
    p->d.ambientOcclusion.normalBias = bias;
    Q_EMIT ambientOcclusionNormalBiasChanged(bias);
}
float
ViewState::ambientOcclusionFalloff() const
{
    return p->d.ambientOcclusion.falloff;
}
void
ViewState::setAmbientOcclusionFalloff(float falloff)
{
    falloff = std::clamp(falloff, 0.25f, 8.0f);
    if (falloff == p->d.ambientOcclusion.falloff)
        return;
    p->d.ambientOcclusion.falloff = falloff;
    Q_EMIT ambientOcclusionFalloffChanged(falloff);
}
float
ViewState::ambientOcclusionContrast() const
{
    return p->d.ambientOcclusion.contrast;
}
void
ViewState::setAmbientOcclusionContrast(float contrast)
{
    contrast = std::clamp(contrast, 0.1f, 4.0f);
    if (contrast == p->d.ambientOcclusion.contrast)
        return;
    p->d.ambientOcclusion.contrast = contrast;
    Q_EMIT ambientOcclusionContrastChanged(contrast);
}
float
ViewState::ambientOcclusionEdgeSharpness() const
{
    return p->d.ambientOcclusion.edgeSharpness;
}
void
ViewState::setAmbientOcclusionEdgeSharpness(float sharpness)
{
    sharpness = std::clamp(sharpness, 0.0f, 4.0f);
    if (sharpness == p->d.ambientOcclusion.edgeSharpness)
        return;
    p->d.ambientOcclusion.edgeSharpness = sharpness;
    Q_EMIT ambientOcclusionEdgeSharpnessChanged(sharpness);
}
bool
ViewState::ambientOcclusionBlurEnabled() const
{
    return p->d.ambientOcclusion.blurEnabled;
}
void
ViewState::setAmbientOcclusionBlurEnabled(bool enabled)
{
    if (enabled == p->d.ambientOcclusion.blurEnabled)
        return;
    p->d.ambientOcclusion.blurEnabled = enabled;
    Q_EMIT ambientOcclusionBlurEnabledChanged(enabled);
}
float
ViewState::ambientOcclusionBlurRadius() const
{
    return p->d.ambientOcclusion.blurRadius;
}
void
ViewState::setAmbientOcclusionBlurRadius(float radius)
{
    radius = std::clamp(radius, 1.0f, 32.0f);
    if (radius == p->d.ambientOcclusion.blurRadius)
        return;
    p->d.ambientOcclusion.blurRadius = radius;
    Q_EMIT ambientOcclusionBlurRadiusChanged(radius);
}
ViewState::AmbientOcclusionQuality
ViewState::ambientOcclusionQuality() const
{
    return p->d.ambientOcclusion.quality;
}
void
ViewState::setAmbientOcclusionQuality(AmbientOcclusionQuality quality)
{
    if (quality == p->d.ambientOcclusion.quality)
        return;
    p->d.ambientOcclusion.quality = quality;
    Q_EMIT ambientOcclusionQualityChanged(quality);
}
ViewState::AmbientOcclusionDebugMode
ViewState::ambientOcclusionDebugMode() const
{
    return p->d.ambientOcclusion.debugMode;
}
void
ViewState::setAmbientOcclusionDebugMode(AmbientOcclusionDebugMode mode)
{
    if (mode == p->d.ambientOcclusion.debugMode)
        return;
    p->d.ambientOcclusion.debugMode = mode;
    Q_EMIT ambientOcclusionDebugModeChanged(mode);
}
bool
ViewState::sceneLightsEnabled() const
{
    return p->d.sceneLightsEnabled;
}
void
ViewState::setSceneLightsEnabled(bool enabled)
{
    if (enabled == p->d.sceneLightsEnabled)
        return;
    p->d.sceneLightsEnabled = enabled;
    Q_EMIT sceneLightsEnabledChanged(enabled);
}
bool
ViewState::sceneMaterialsEnabled() const
{
    return p->d.sceneMaterialsEnabled;
}
void
ViewState::setSceneMaterialsEnabled(bool enabled)
{
    if (enabled == p->d.sceneMaterialsEnabled)
        return;
    p->d.sceneMaterialsEnabled = enabled;
    Q_EMIT sceneMaterialsEnabledChanged(enabled);
}
ViewState::DoubleSidedMode
ViewState::doubleSidedMode() const
{
    return p->d.doubleSidedMode;
}
void
ViewState::setDoubleSidedMode(DoubleSidedMode mode)
{
    if (mode == p->d.doubleSidedMode)
        return;
    p->d.doubleSidedMode = mode;
    Q_EMIT doubleSidedModeChanged(mode);
}
ViewState::RenderMode
ViewState::renderMode() const
{
    return p->d.renderMode;
}
void
ViewState::setRenderMode(RenderMode mode)
{
    if (mode == p->d.renderMode)
        return;
    p->d.renderMode = mode;
    Q_EMIT renderModeChanged(mode);
}
ViewState::ComplexityLevel
ViewState::complexityLevel() const
{
    return p->d.complexityLevel;
}
void
ViewState::setComplexityLevel(ComplexityLevel level)
{
    if (level == p->d.complexityLevel)
        return;
    p->d.complexityLevel = level;
    Q_EMIT complexityLevelChanged(level);
}
QString
ViewState::rendererAov() const
{
    return p->d.rendererAov;
}
void
ViewState::setRendererAov(const QString& aov)
{
    if (aov == p->d.rendererAov)
        return;
    p->d.rendererAov = aov;
    Q_EMIT rendererAovChanged(aov);
}
bool
ViewState::sceneStatsEnabled() const
{
    return p->d.sceneStatsEnabled;
}
void
ViewState::setSceneStatsEnabled(bool enabled)
{
    if (enabled == p->d.sceneStatsEnabled)
        return;
    p->d.sceneStatsEnabled = enabled;
    Q_EMIT sceneStatsEnabledChanged(enabled);
}
bool
ViewState::performanceStatsEnabled() const
{
    return p->d.performanceStatsEnabled;
}
void
ViewState::setPerformanceStatsEnabled(bool enabled)
{
    if (enabled == p->d.performanceStatsEnabled)
        return;
    p->d.performanceStatsEnabled = enabled;
    Q_EMIT performanceStatsEnabledChanged(enabled);
}
bool
ViewState::cameraAxisEnabled() const
{
    return p->d.cameraAxisEnabled;
}
void
ViewState::setCameraAxisEnabled(bool enabled)
{
    if (enabled == p->d.cameraAxisEnabled)
        return;
    p->d.cameraAxisEnabled = enabled;
    Q_EMIT cameraAxisEnabledChanged(enabled);
}
}  // namespace stageviz
