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
 * @brief Creates an undoable command that merges authored USD content into the current edit layer.
 *
 * The active edit layer is snapshotted before the merge. Undo restores the
 * complete previous edit-layer contents exactly.
 */
Command
mergeStage(const QString& filename);

/**
 * @brief Creates an undoable command that flattens an incoming stage and merges it into the current edit layer.
 */
Command
mergeFlattenedStage(const QString& filename);

/**
 * @brief Creates an undoable command that authors a reference on an existing prim.
 */
Command
addReference(const QString& filename, const SdfPath& targetPath);

/**
 * @brief Creates an undoable command that authors a payload on an existing prim.
 */
Command
addPayload(const QString& filename, const SdfPath& targetPath);

/**
 * @brief Creates an undoable command that sets a USD variant selection.
 *
 * The selection is authored into the stage's current edit target, so composed
 * prims receive a stronger variant-selection opinion without modifying their
 * source layer. Only prims that expose @p setName and contain @p value are
 * modified.
 *
 * Undo restores the edit layer's previous variantSelection field exactly. If
 * the command created an otherwise-inert override prim solely for the variant
 * opinion, undo removes that empty override again.
 *
 * @param paths Prim paths whose variant selection should be changed.
 * @param setName Variant set name.
 * @param value Variant value to select.
 * @return Undoable variant-selection command.
 */
Command
setVariantSelection(const QList<SdfPath>& paths, const QString& setName, const QString& value);

/**
 * @brief Creates an Xform prim with a reference composition arc.
 *
 * The prim is created under @p parentPath using a unique name and authors a
 * reference to @p assetPath. When @p primPath is empty, the referenced file's
 * default prim is used.
 *
 * @param parentPath Parent prim path.
 * @param nameInput Desired prim name.
 * @param assetPath Referenced USD asset path.
 * @param primPath Optional prim path inside the referenced asset.
 */
Command
newReferencePath(const SdfPath& parentPath, const QString& nameInput, const QString& assetPath,
                 const SdfPath& primPath = SdfPath());

/**
 * @brief Creates an Xform prim with a payload composition arc.
 *
 * The prim is created under @p parentPath using a unique name and authors a
 * payload to @p assetPath. When @p primPath is empty, the payload file's
 * default prim is used.
 *
 * @param parentPath Parent prim path.
 * @param nameInput Desired prim name.
 * @param assetPath Payload USD asset path.
 * @param primPath Optional prim path inside the payload asset.
 */
Command
newPayloadPath(const SdfPath& parentPath, const QString& nameInput, const QString& assetPath,
               const SdfPath& primPath = SdfPath());

}  // namespace stageviz
