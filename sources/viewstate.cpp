// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#include "viewstate.h"
#include "viewcamera.h"

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
        bool sceneLightsEnabled = true;
        bool sceneMaterialsEnabled = true;
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
