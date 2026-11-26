// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 - present Mikael Sundell

#pragma once

#include "command.h"
#include <QList>
#include <pxr/usd/usd/stage.h>

PXR_NAMESPACE_USING_DIRECTIVE

namespace stageviz {

/**
 * @brief Tests whether every selected prim supports in-place mesh conversion.
 *
 * Supports Cube, Sphere, Cone, Cylinder, Capsule and Plane in an editable target.
 * Instances, proxies, prototypes, inactive prims and mixed unsupported selections
 * are rejected. The caller must hold the stage lock.
 *
 * @param stage Stage containing the selection.
 * @param paths Explicit prim paths to convert.
 * @return True when all paths can be converted.
 */
bool
canConvertToMesh(const UsdStageRefPtr& stage, const QList<SdfPath>& paths);

/**
 * @brief Converts selected geometric primitives to polygon meshes in place.
 *
 * Retains paths, transforms, materials, children and selection. Shape dimension
 * time samples are baked into points and normals. Undo restores exact opinions
 * in the original edit target, including removing newly created overrides.
 * The whole batch is rejected if a prim is unsupported or dimensions are invalid.
 *
 * @param paths Explicit prim paths to convert.
 * @param segments Radial resolution, from 4 to 256; axial resolution is half.
 *                 Zero uses the current viewport complexity (16, 32, 64 or 128
 *                 radial segments for Low through VeryHigh), captured on first
 *                 execution and retained for redo.
 * @return Undoable mesh conversion command.
 */
Command
convertToMesh(const QList<SdfPath>& paths, int segments = 0);

}  // namespace stageviz
