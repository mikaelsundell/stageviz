// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#pragma once

#include "stageviz.h"
#include <QList>
#include <QPointF>
#include <QRectF>
#include <pxr/base/gf/vec2i.h>
#include <pxr/base/gf/vec4d.h>

#include <pxr/base/gf/vec3d.h>
#include <pxr/usd/usd/stage.h>

PXR_NAMESPACE_USING_DIRECTIVE

namespace stageviz {

class ViewportWidget;
class ViewContext;
class ViewCamera;
class ViewState;
class RenderEngine;

/**
 * @brief Provides viewport services to interaction and presentation components.
 *
 * Components are owned by the host and must not outlive it. This interface
 * keeps their implementations independent of ViewportWidgetPrivate state.
 */
class ViewportHost {
public:
    /**
     * @brief Destroys the viewport service interface.
     */
    virtual ~ViewportHost() = default;

    /**
     * @brief Returns the viewport widget.
     */
    virtual ViewportWidget* widget() const = 0;

    /**
     * @brief Returns the editing context.
     */
    virtual ViewContext* context() const = 0;

    /**
     * @brief Returns the document stage without acquiring the stage lock.
     *
     * Callers synchronize USD access through context()->stageLock().
     */
    virtual UsdStageRefPtr stage() const = 0;

    /**
     * @brief Returns the auxiliary display stage.
     */
    virtual UsdStageRefPtr auxiliaryStage() const = 0;

    /**
     * @brief Returns the current renderer, when initialized.
     */
    virtual RenderEngine* renderEngine() const = 0;

    /**
     * @brief Returns the viewport camera.
     */
    virtual ViewCamera* viewCamera() = 0;

    /**
     * @brief Returns the viewport display settings.
     */
    virtual ViewState* viewState() = 0;

    /**
     * @brief Returns the session-derived viewport selection.
     */
    virtual const QList<SdfPath>& selectedPaths() const = 0;

    /**
     * @brief Returns the active render mask.
     */
    virtual const QList<SdfPath>& maskPaths() const = 0;

    /**
     * @brief Tests a path against the render mask.
     */
    virtual bool pathMaskedIn(const SdfPath& path) const = 0;

    /**
     * @brief Returns the camera gate in logical widget coordinates.
     */
    virtual QRectF cameraGateRect() = 0;

    /**
     * @brief Returns the viewport in device pixels.
     */
    virtual GfVec4d widgetViewport() = 0;

    /**
     * @brief Returns the framebuffer size in device pixels.
     */
    virtual GfVec2i widgetSize() const = 0;

    /**
     * @brief Projects a world point into logical widget coordinates.
     */
    virtual bool projectWorldToScreen(const GfVec3d& world, QPointF& screen) = 0;

    /**
     * @brief Schedules an overlay repaint.
     */
    virtual void updateOverlay() = 0;

    /**
     * @brief Synchronizes the viewport selection before dispatching its command.
     */
    virtual void syncSelection(const QList<SdfPath>& paths) = 0;
};

}  // namespace stageviz
