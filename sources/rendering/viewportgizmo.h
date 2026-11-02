// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#pragma once

#include "stageviz.h"
#include <QPointF>
#include <QScopedPointer>
#include <pxr/base/gf/matrix4d.h>

#include <pxr/base/gf/vec3d.h>
#include <pxr/usd/usd/stage.h>

class QPainter;

PXR_NAMESPACE_USING_DIRECTIVE

namespace stageviz {

class ViewportHost;
class ViewportGizmoPrivate;

/**
 * @brief Owns transform-handle geometry, projection, and hit testing.
 */
class ViewportGizmo {
public:
    /**
     * @brief Identifies the displayed transform handles.
     */
    enum class Mode { None, Move, Rotate, Scale };

    /**
     * @brief Describes the manipulation frame and active visual handles.
     */
    struct Frame {
        Mode mode = Mode::None;
        bool enabled = false;
        bool pivotValid = false;
        bool orientationValid = false;
        GfVec3d pivot = GfVec3d(0.0);
        GfMatrix4d orientation = GfMatrix4d(1.0);
        int hoverAxis = 0;
        int activeAxis = 0;
    };

    /**
     * @brief Constructs gizmo presentation for the host.
     */
    explicit ViewportGizmo(ViewportHost& host);

    /**
     * @brief Destroys the gizmo presentation.
     */
    ~ViewportGizmo();

    /**
     * @brief Invalidates the cached handle presentation.
     */
    void invalidate();

    /**
     * @brief Resets resources when the auxiliary stage changes.
     */
    void reset();

    /**
     * @brief Hides handle geometry.
     */
    void clear();

    /**
     * @brief Updates handle geometry from the manipulation frame.
     */
    void update(const Frame& frame);

    /**
     * @brief Draws the center handle overlay.
     */
    void draw(const Frame& frame, QPainter& painter);

    /**
     * @brief Returns the handle under a logical widget position.
     */
    int hitTest(const Frame& frame, const QPointF& pos);

    /**
     * @brief Returns the screen direction of a handle axis.
     */
    QPointF axisDirection(const Frame& frame, int axis);

    /**
     * @brief Solves a rotation gesture in logical widget coordinates.
     */
    bool rotationAngle(const Frame& frame, int axis, const QPointF& pos, double& angle);

    /**
     * @brief Returns the world distance corresponding to a logical pixel.
     */
    double worldPerPixel(const GfVec3d& pivot);

    /**
     * @brief Returns a world coordinate axis.
     */
    static GfVec3d axisVector(int axis);

private:
    QScopedPointer<ViewportGizmoPrivate> p;
};

}  // namespace stageviz
