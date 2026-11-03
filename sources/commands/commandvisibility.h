// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#pragma once

#include "command.h"
#include <QList>
#include <pxr/usd/sdf/path.h>

PXR_NAMESPACE_USING_DIRECTIVE

namespace stageviz {

/**
 * @brief Creates a command that makes prims visible.
 *
 * Authors visibility = inherited (visible) on the specified prims.
 * If @p recursive is true, the visibility change is applied to all descendants.
 *
 * Undo restores the previous visibility state per prim.
 *
 * @param paths     Prim paths to show.
 * @param recursive If true, apply recursively to descendants.
 */
Command
showPaths(const QList<SdfPath>& paths, bool recursive);

/**
 * @brief Creates a command that hides prims.
 *
 * Authors visibility = invisible on the specified prims.
 * If @p recursive is true, the visibility change is applied to all descendants.
 *
 * Undo restores the previous visibility state per prim.
 *
 * @param paths     Prim paths to hide.
 * @param recursive If true, apply recursively to descendants.
 */
Command
hidePaths(const QList<SdfPath>& paths, bool recursive);

}  // namespace stageviz
