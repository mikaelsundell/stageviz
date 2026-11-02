// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#pragma once

#include "stageviz.h"
#include <QList>
#include <QPoint>
#include <QRect>
#include <QScopedPointer>

#include <pxr/base/gf/vec3d.h>
#include <pxr/usd/usd/stage.h>

class QMouseEvent;
class QPainter;

PXR_NAMESPACE_USING_DIRECTIVE

namespace stageviz {

class ViewportHost;
class ViewportSelectionPrivate;

/**
 * @brief Owns viewport picking, sweep queries, and visible-selection caching.
 */
class ViewportSelection {
public:
    /**
     * @brief Constructs a selection tool for the host.
     */
    explicit ViewportSelection(ViewportHost& host);

    /**
     * @brief Destroys the selection tool.
     */
    ~ViewportSelection();

    /**
     * @brief Clears stage-dependent picking state and caches.
     */
    void reset();

    /**
     * @brief Clears the pick cycle after camera navigation.
     */
    void resetPickCycle();

    /**
     * @brief Returns whether a selection gesture is active.
     */
    bool active() const;

    /**
     * @brief Starts a selection gesture in logical widget coordinates.
     */
    void begin(const QPoint& pos);

    /**
     * @brief Updates the selection rectangle.
     */
    void update(const QPoint& pos);

    /**
     * @brief Applies and finishes the selection gesture.
     */
    void end(QMouseEvent* event);

    /**
     * @brief Cancels the selection rectangle.
     */
    void cancel();

    /**
     * @brief Draws the active selection rectangle.
     */
    void draw(QPainter& painter);


    /**
     * @brief Invalidates the spatial index and visible-selection cache.
     */
    void invalidateStage();

    /**
     * @brief Invalidates the visible-selection cache.
     */
    void invalidateVisibleSelection();

    /**
     * @brief Picks the nearest masked prim.
     */
    SdfPath pickNearestPath(const QPoint& pos);

    /**
     * @brief Applies a click or sweep selection gesture.
     */
    void sweep(const QRect& rect, QMouseEvent* event);

    /**
     * @brief Tests whether a prim is included by the render mask.
     */
    bool isPathMaskedIn(const SdfPath& path) const;

    /**
     * @brief Returns cached selected paths visible through the render mask.
     */
    const QList<SdfPath>& visiblePaths();

private:
    QScopedPointer<ViewportSelectionPrivate> p;
};

}  // namespace stageviz
