// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 - present Mikael Sundell

#pragma once

#include <QPointF>
#include <QRectF>
#include <pxr/base/gf/camera.h>
#include <pxr/base/gf/matrix4d.h>

namespace stageviz::gizmoprojection {

/**
 * @brief Returns the viewport camera with clipping fitted to the gizmo bounds.
 */
pxr::GfCamera
camera(const pxr::GfCamera& viewport, const pxr::GfMatrix4d& gizmoTransform);

/**
 * @brief Projects a handle using the gizmo camera and viewport gate.
 */
bool
project(const pxr::GfCamera& camera, const QRectF& gate, const pxr::GfVec3d& world, QPointF& screen);

}  // namespace stageviz::gizmoprojection
