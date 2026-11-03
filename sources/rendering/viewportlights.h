// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 - present Mikael Sundell

#pragma once

#include <QList>
#include <QPointF>
#include <QRectF>
#include <QScopedPointer>
#include <pxr/usd/sdf/path.h>

class QPainter;
PXR_NAMESPACE_USING_DIRECTIVE

namespace stageviz {
class ViewportHost;
class ViewportLightsPrivate;
/**
 * @brief Draws and picks document light guides without adding USD geometry.
 *
 * Guides are an always-visible editor overlay, excluded from render exports,
 * scene bounds, material overrides and viewport color capture. Picks return
 * the original USD light path, including instance proxies.
 */
class ViewportLights {
public:
    /**
     * @brief Constructs the light guide component for its owning viewport.
     */
    explicit ViewportLights(ViewportHost& host);
    /**
     * @brief Destroys the component.
     */
    ~ViewportLights();
    /**
     * @brief Invalidates the cached light path inventory after a stage resync.
     */
    void invalidate();
    /**
     * @brief Sets the evaluated USD animation time.
     */
    void setTimeCode(double time);
    /**
     * @brief Draws light shapes using the current camera and selection colors.
     */
    void draw(QPainter& painter);
    /**
     * @brief Returns the nearest guide hit in logical viewport coordinates.
     */
    SdfPath pick(const QPointF& pos);
    /**
     * @brief Returns light paths whose guides intersect a logical sweep rectangle.
     */
    QList<SdfPath> pathsInRect(const QRectF& rect);

private:
    QScopedPointer<ViewportLightsPrivate> p;
};
}  // namespace stageviz
