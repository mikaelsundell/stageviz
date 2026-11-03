// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#pragma once

#include "command.h"
#include <QList>
#include <QString>
#include <pxr/base/tf/token.h>
#include <pxr/usd/sdf/path.h>

PXR_NAMESPACE_USING_DIRECTIVE

namespace stageviz {

/**
 * @brief Creates a command that deletes prims at the specified paths.
 *
 * Paths are reduced to minimal root paths to avoid redundant edits.
 * Only strongest-editable prims in the current edit target are affected.
 *
 * Before deletion, edit-layer relationship targets and attribute connections
 * that point at the deleted prims or anything below them are removed so the
 * stage does not retain dangling authored dependencies. Material bindings,
 * collection relationships, shader connections, and custom relationship
 * targets are covered by the same generic dependency cleanup.
 *
 * The command captures sufficient snapshot state to fully restore deleted
 * prims, dependency properties, and child order on undo.
 *
 * Selection and mask are updated to remove affected paths.
 *
 * @param paths Prim paths to delete.
 */
Command
deletePaths(const QList<SdfPath>& paths);

/**
 * @brief Creates sibling copies of prim specs.
 *
 * Each input path is reduced to a minimal root path and resolved to its
 * strongest authored SdfPrimSpec. That spec is copied into the opened root
 * layer under the same parent using a unique sibling name. Authored subtree
 * content and composition arcs from the strongest spec are retained.
 *
 * @param paths Prim paths to copy.
 * @param selectCopies When true, select the new copies on success. Set false
 *                     for callers that need to preserve the current selection.
 */
Command
copyPaths(const QList<SdfPath>& paths, bool selectCopies = true);

/**
 * @brief Creates instanceable sibling prims that internally reference the selected prims.
 *
 * Each input path is reduced to a minimal root path. For every valid source
 * prim, an instanceable prim with a unique sibling name is authored in the
 * active edit layer and references the source prim within the same stage.
 * The new instance prims become the current selection on success. Undo removes
 * the instance prims and restores child ordering, selection, and mask.
 *
 * @param paths Prim paths to instance.
 */
Command
instancePaths(const QList<SdfPath>& paths);

/**
 * @brief Creates a command that defines a generic prim under a parent.
 *
 * The name is sanitized and made unique under the parent. When @p typeName is
 * non-empty, the prim is defined with that USD type; otherwise an untyped prim
 * is defined.
 *
 * By default the new prim becomes the current selection. Callers may preserve
 * the current selection by passing selectCreated = false. Undo removes the
 * created prim and restores child ordering, selection, and mask.
 *
 * @param parentPath Parent prim path.
 * @param nameInput Desired name for the new prim.
 * @param typeName Optional USD schema type name, for example "Xform", "Mesh",
 *                 "Camera", or "Scope". Axis-based built-in primitives follow
 *                 the stage up axis.
 * @param selectCreated If true, select the new prim on success.
 */
Command
newPrimPath(const SdfPath& parentPath, const QString& nameInput, const TfToken& typeName = TfToken(),
            bool selectCreated = true);

/**
 * @brief Creates a Scope prim under a parent.
 *
 * This is a convenience wrapper around newPrimPath() using the USD "Scope"
 * type. The command is fully undoable.
 *
 * @param parentPath Parent prim path.
 * @param nameInput Desired scope name.
 * @param selectCreated If true, select the new scope on success.
 */
Command
newScopePath(const SdfPath& parentPath, const QString& nameInput, bool selectCreated = true);

/**
 * @brief Creates a visible wavy UsdGeomMesh example under a parent.
 *
 * The mesh contains a regular quad grid whose Z positions are displaced by a
 * deterministic sinusoidal function, so Create > Mesh produces immediately
 * visible geometry instead of an empty Mesh prim.
 */
Command
newMeshPath(const SdfPath& parentPath, const QString& nameInput, bool selectCreated = true);

/**
 * @brief Creates a visible UsdGeomPoints particle-style point cloud.
 */
Command
newPointsPath(const SdfPath& parentPath, const QString& nameInput, bool selectCreated = true);

/**
 * @brief Creates visible cubic B-spline UsdGeomBasisCurves.
 */
Command
newBasisCurvesPath(const SdfPath& parentPath, const QString& nameInput, bool selectCreated = true);

/**
 * @brief Creates visible cubic UsdGeomNurbsCurves.
 */
Command
newNurbsCurvesPath(const SdfPath& parentPath, const QString& nameInput, bool selectCreated = true);

/**
 * @brief Creates a visible wavy UsdGeomNurbsPatch.
 */
Command
newNurbsPatchPath(const SdfPath& parentPath, const QString& nameInput, bool selectCreated = true);

/**
 * @brief Creates a UsdGeomPointInstancer with an internal Cube prototype and
 * a small deterministic grid of instances.
 */
Command
newPointInstancerPath(const SdfPath& parentPath, const QString& nameInput, bool selectCreated = true);

/**
 * @brief Creates a command that renames a prim.
 *
 * The new name is sanitized and made unique under the parent.
 * The operation preserves child ordering and remaps selection, mask,
 * and load rules accordingly.
 *
 * Undo restores the original name and ordering.
 *
 * @param path         Prim path to rename.
 * @param newNameInput Desired new name.
 */
Command
renamePath(const SdfPath& path, const QString& newNameInput);

/**
 * @brief Creates a command that defines a new Xform prim under a parent.
 *
 * The name is sanitized and made unique under the parent. The new prim
 * is appended to the child order.
 *
 * By default the new prim becomes the current selection. Callers may preserve
 * the current selection by passing selectCreated = false.
 *
 * Undo removes the created prim and restores child ordering and mask.
 *
 * @param parentPath Parent prim path.
 * @param nameInput  Desired name for the new prim.
 * @param selectCreated If true, select the new Xform on success. When false and
 *                      selected paths are grouped below it, their selection is
 *                      remapped to the new child paths instead.
 */
Command
newXformPath(const SdfPath& parentPath, const QString& nameInput, bool selectCreated = true);

/**
 * @brief Creates a command that reparents or reorders one or more prims.
 *
 * Moves the prims in @p paths to @p newParentPath. When the destination parent
 * is the same as the current parent, the operation behaves as a reorder using
 * @p insertIndex. When multiple prims are moved, their relative order is
 * preserved.
 *
 * When @p preserveWorldTransform is true and a prim changes parent, its
 * world-space transform is preserved by recomputing its local transform
 * relative to the new parent.
 *
 * Child ordering is updated for all affected parents. Selection and mask are
 * remapped to the new paths for every moved prim.
 *
 * Undo restores the original hierarchy, child ordering, selection, and mask.
 *
 * @param paths         Prim paths to move.
 * @param newParentPath Destination parent path.
 * @param insertIndex   Target index for the first moved prim in the destination
 *                      parent's child order.
 * @param preserveTransform If true, preserve each moved prim's world-space
 *                           transform when changing parent.
 */
Command
movePath(const QList<SdfPath>& paths, const SdfPath& newParentPath, int insertIndex, bool preserveTransform = true);

}  // namespace stageviz
