// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 - present Mikael Sundell

#pragma once

#include "command.h"
#include <QString>
#include <pxr/usd/sdf/path.h>

PXR_NAMESPACE_USING_DIRECTIVE

namespace stageviz {
/**
 * @brief Supported standard USD light creation presets.
 */
enum class LightType { Point, Spot, Distant, Rect, Disk, Cylinder, Dome };

/**
 * @brief Creates a named USD light through the common undoable prim command.
 *
 * Spot uses SphereLight with ShapingAPI. Parameters remain standard USD
 * attributes editable in the Property Editor. Undo restores selection and
 * removes the light; redo recreates the same preset.
 *
 * @param parentPath Parent of the new light.
 * @param nameInput Preferred name, made unique among siblings.
 * @param type Standard light preset.
 * @param selectCreated Selects the created light when true.
 */
Command
newLightPath(const SdfPath& parentPath, const QString& nameInput, LightType type, bool selectCreated = true);
}  // namespace stageviz
