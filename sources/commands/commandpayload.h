// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#pragma once

#include "command.h"
#include <QList>
#include <QString>
#include <pxr/usd/sdf/path.h>

PXR_NAMESPACE_USING_DIRECTIVE

namespace stageviz {

/**
 * @brief Creates a command that loads payloads resolved from the specified paths.
 *
 * Each input path is expected to identify a payload prim. Callers that start
 * from arbitrary prim selections should resolve those selections to payload
 * paths before constructing the command.
 *
 * If a variant set and value are provided, the variant selection is applied
 * before loading.
 *
 * The operation records enough state to restore both load state and variant
 * selections on undo.
 *
 * @param paths        Input prim paths used to resolve payloads.
 * @param variantSet   Optional variant set name.
 * @param variantValue Optional variant selection value.
 */
Command
loadPayloads(const QList<SdfPath>& paths, const QString& variantSet = QString(),
             const QString& variantValue = QString());

/**
 * @brief Creates a command that loads spatially neighboring payloads.
 *
 * The input paths are resolved to their outermost payload ancestors and their
 * world-space extentsHint bounds are combined into one source bounding box.
 *
 * The source box is expanded by a factor of 1.5 around its center while
 * preserving its proportions. Only unloaded outermost payloads with usable
 * extentsHint are considered. A payload is treated as a neighbor when the
 * center of its world-space extents lies inside the expanded search box.
 *
 * Neighbor payloads are loaded nearest-first by center distance. If a selected
 * source payload itself is unloaded, it is loaded together with its neighbors.
 * Undo restores the previous payload load states, selection, and mask.
 *
 * @param paths Payload prim paths or descendant paths inside payloads.
 */
Command
loadNeighborPayloads(const QList<SdfPath>& paths);

/**
 * @brief Creates a command that unloads payloads resolved from the specified paths.
 *
 * Each input path is expected to identify a payload prim. After unloading,
 * selections and masks on the payload root are preserved, while selections or
 * masks on descendants that disappear are remapped to the payload root.
 *
 * Undo restores the previous load state, selection, and mask exactly.
 *
 * @param paths Payload prim paths to unload.
 */
Command
unloadPayloads(const QList<SdfPath>& paths);

}  // namespace stageviz
