// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 - present Mikael Sundell

#include "gizmoprojection.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <pxr/base/gf/frustum.h>

namespace stageviz::gizmoprojection {

pxr::GfCamera
camera(const pxr::GfCamera& viewport, const pxr::GfMatrix4d& gizmoTransform)
{
    auto result = viewport;
    const auto view = viewport.GetFrustum().ComputeViewMatrix();
    const double depth = -view.Transform(gizmoTransform.ExtractTranslation())[2];
    // Each fixed handle mesh fits within a local +/-1.2 cube. The sum of
    // transformed basis lengths conservatively bounds rotations and scaling.
    double extent = 0.0;
    for (int axis = 0; axis < 3; ++axis)
        extent += pxr::GfVec3d(gizmoTransform[axis][0], gizmoTransform[axis][1], gizmoTransform[axis][2]).GetLength();
    extent *= 1.2;
    if (!std::isfinite(depth) || !std::isfinite(extent) || extent <= 0.0)
        return result;

    double nearClip = depth - extent;
    double farClip = depth + extent;
    if (viewport.GetProjection() == pxr::GfCamera::Perspective) {
        if (depth <= 1e-8)
            return result;
        nearClip = std::max(nearClip, std::max(1e-9, depth * 1e-4));
        farClip = std::max(farClip, nearClip * 2.0);
    }
    if (nearClip >= farClip || std::abs(nearClip) >= std::numeric_limits<float>::max()
        || std::abs(farClip) >= std::numeric_limits<float>::max())
        return result;
    result.SetClippingRange(pxr::GfRange1f(static_cast<float>(nearClip), static_cast<float>(farClip)));
    return result;
}

bool
project(const pxr::GfCamera& camera, const QRectF& gate, const pxr::GfVec3d& world, QPointF& screen)
{
    const auto frustum = camera.GetFrustum();
    const auto point = frustum.ComputeViewMatrix().Transform(world);
    const double depth = -point[2];
    const auto range = frustum.GetNearFar();
    if (!std::isfinite(depth) || depth <= range.GetMin() || depth >= range.GetMax())
        return false;
    const auto ndc = frustum.ComputeProjectionMatrix().Transform(point);
    if (!std::isfinite(ndc[0]) || !std::isfinite(ndc[1]))
        return false;
    screen.setX(gate.left() + (ndc[0] * 0.5 + 0.5) * gate.width());
    screen.setY(gate.top() + (0.5 - ndc[1] * 0.5) * gate.height());
    return std::isfinite(screen.x()) && std::isfinite(screen.y());
}

}  // namespace stageviz::gizmoprojection
