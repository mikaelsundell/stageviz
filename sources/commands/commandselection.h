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
 * @brief Selects the nearest payload ancestor for the current selection.
 *
 * Each selected prim is walked upward until the first payload prim is found.
 * If a selected prim is itself a payload, that prim is selected directly.
 * Results are deduplicated, so multiple selected prims inside the same payload
 * resolve to a single payload path.
 *
 * Undo restores the previous selection.
 */
Command
selectPayload();

/**
 * @brief Selects the nearest enclosing payload authored in the active edit layer.
 *
 * This provides the layer-level navigation counterpart to selectPayload().
 * Nested payloads from weaker/composed layers are skipped until a payload
 * opinion authored in the current edit layer is found.
 */
Command
selectLayer();

/**
 * @brief Inverts selection within the nearest enclosing payload scope.
 *
 * The current selection is resolved to nearest payload ancestors and the
 * normal leaf-selection inversion is performed only within those payloads.
 * Undo restores the previous selection.
 */
Command
selectInvertInPayload();

/**
 * @brief Inverts payload selection within the current edit layer.
 *
 * The current selection is resolved to the nearest payload authored in the
 * active edit layer. The result selects all other payload roots authored in
 * that same layer. Undo restores the previous selection.
 */
Command
selectInvertInLayer();

/**
 * @brief Creates a command that sets the session mask to the specified paths.
 *
 * This effectively isolates the given prim paths by restricting traversal
 * and visibility to the masked subset of the stage.
 *
 * Undo restores the previous mask.
 *
 * @param paths Prim paths to isolate.
 */
Command
isolatePaths(const QList<SdfPath>& paths);

/**
 * @brief Creates a command that replaces the current selection.
 *
 * The provided paths become the new selection exactly as given.
 *
 * Undo restores the previous selection.
 *
 * @param paths Prim paths to select.
 */
Command
selectPaths(const QList<SdfPath>& paths);

/**
 * @brief Creates a command that selects prims from the stage.
 *
 * By default, selects only the direct children of the stage pseudo-root.
 * When @p recursive is true, selects all traversable prims in the stage.
 *
 * Undo restores the previous selection.
 *
 * @param recursive If true, select all prims recursively.
 */
Command
selectAll(bool recursive = false);

/**
 * @brief Creates a command that inverts the current selection across leaf prims.
 *
 * The domain is defined as all leaf prim paths within the current mask.
 * A leaf is considered selected if it is directly selected or covered by
 * a selected ancestor path.
 *
 * The resulting selection contains all leaf prim paths not covered by the
 * current selection.
 *
 * Undo restores the previous selection.
 */
Command
selectInvert();

}  // namespace stageviz
