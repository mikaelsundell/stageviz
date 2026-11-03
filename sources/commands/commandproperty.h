// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#pragma once

#include "command.h"
#include <QList>
#include <QString>
#include <pxr/base/vt/value.h>
#include <pxr/usd/sdf/path.h>
#include <pxr/usd/sdf/types.h>

PXR_NAMESPACE_USING_DIRECTIVE

namespace stageviz {

/**
 * @brief Creates an undoable command that removes authored relationship targets and attribute connections.
 *
 * Only dependency fields authored in the current edit layer are removed. Undo
 * restores the original property specs exactly.
 *
 * @param propertyPaths Relationship or attribute property paths to reset.
 * @return Undoable dependency-reset command.
 */
Command
resetDependencies(const QList<SdfPath>& propertyPaths);

/**
 * @brief Creates an undoable command that adds an attribute to one or more prims.
 *
 * The attribute is authored in the active USD edit target using the specified
 * name and value type. When @p custom is true, the attribute is marked as a
 * custom USD property. Existing properties must not be overwritten.
 *
 * @param primPaths Prim paths that should receive the attribute.
 * @param name Name of the attribute to create.
 * @param type USD value type of the new attribute.
 * @param custom Whether to create a custom attribute.
 * @return Undoable attribute-creation command.
 */
Command
addAttributes(const QList<SdfPath>& primPaths, const TfToken& name, const SdfValueTypeName& type, bool custom);

/**
 * @brief Creates an undoable command that removes custom attributes.
 *
 * Only removable custom attributes may be deleted; schema-defined built-in
 * attributes are protected. The command operates on USD attribute property
 * paths rather than prim paths and preserves the previous authored state for
 * undo.
 *
 * @param attributePaths USD property paths of attributes to remove.
 * @return Undoable attribute-removal command.
 */
Command
removeAttributes(const QList<SdfPath>& attributePaths);

/**
 * @brief Creates an undoable command that renames custom attributes.
 *
 * Schema-defined built-in attributes cannot be renamed. The new name must
 * be a valid USD property name and must not collide with another property
 * on any target prim. Renamed attributes remain custom properties.
 *
 * @param attributePaths USD property paths of attributes to rename.
 * @param newName New attribute name to apply to each target.
 * @return Undoable attribute-rename command.
 */
Command
renameAttributes(const QList<SdfPath>& attributePaths, const TfToken& newName);

/**
 * @brief Creates an undoable command that authors an attribute's default value.
 *
 * The value is authored in the active edit target. Undo restores the previous
 * edit-layer default opinion, or clears the newly authored default when none
 * existed previously.
 *
 * Supports scalar, vector, matrix, token/string, asset-path and VtArray values.
 * PropertyTree array editing reconstructs the typed array before submission.
 *
 * Editing xformOp:translate:pivot rebuilds the canonical paired
 * pivot/matrix/inverse-pivot stack while preserving the evaluated local
 * transform, so moving a pivot does not move the geometry.
 *
 * @param attributePath USD property path identifying the attribute.
 * @param value Typed value to author.
 * @return Undoable attribute-value command.
 */
Command
setAttributeValue(const SdfPath& attributePath, const VtValue& value);

/**
 * @brief Creates one undoable command that authors the same value to several attributes.
 *
 * Every path must identify an existing USD attribute compatible with @p value.
 * Previous edit-layer default opinions are captured individually and restored
 * by a single undo step. Standard xformOp:translate:pivot edits preserve each
 * prim's evaluated local transform.
 */
Command
setAttributeValues(const QList<SdfPath>& attributePaths, const VtValue& value);

/**
 * @brief Creates one undoable command that clears edit-layer default values.
 *
 * Clearing the default value exposes any weaker composed default while leaving
 * the rest of each property spec intact. Undo restores every previous
 * edit-layer default exactly.
 */
Command
resetAttributeValues(const QList<SdfPath>& attributePaths);

/**
 * @brief Creates a command that removes one edit-layer attribute override.
 *
 * The attribute must be authored in the opened edit layer on a prim that also
 * has an underlying opinion from a weaker composition layer, such as a
 * sublayer, reference, or payload. Edit-layer-owned prims are never modified.
 *
 * The complete attribute spec is snapshotted before removal so undo restores
 * the authored value, metadata, connections, and other attribute information
 * exactly.
 *
 * If removing the attribute leaves an inert edit-layer prim spec, the empty
 * prim spec is removed so the weaker composed prim is exposed cleanly.
 *
 * @param attributePath USD attribute path whose edit-layer override should be removed.
 */
Command
resetAttributeOverride(const SdfPath& attributePath);

/**
 * @brief Creates one undoable command that removes edit-layer overrides for several attributes.
 *
 * Attributes without an applicable edit-layer override are skipped. Each
 * removed property spec is snapshotted so undo restores the complete authored
 * property information.
 */
Command
resetAttributeOverrides(const QList<SdfPath>& attributePaths);

/**
 * @brief Creates a command that removes edit-layer property overrides from composed prims.
 *
 * The selected prims and their descendants are inspected recursively. Only
 * property specs on SdfSpecifierOver prim specs authored in the opened edit
 * layer are removed when the prim also has an underlying opinion from a weaker
 * layer, such as a sublayer, reference, or payload. SdfSpecifierDef prim specs
 * are traversed but never modified. This exposes the composed attribute or
 * relationship value again.
 *
 * Prim metadata and composition arcs are preserved, so resetting overrides does
 * not remove references, payloads, variants, or otherwise change payload load
 * state. Edit-layer-owned prims are ignored and are never deleted.
 *
 * If removing property opinions leaves an inert edit-layer prim spec, the empty
 * override spec is removed so the weaker composed prim is exposed cleanly.
 *
 * Undo restores the removed edit-layer property specs exactly.
 *
 * @param paths Root prim paths of the hierarchies whose property overrides should be removed.
 */
Command
resetOverrides(const QList<SdfPath>& paths);

}  // namespace stageviz
