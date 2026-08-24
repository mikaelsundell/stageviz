// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#pragma once

#include "session.h"
#include "stageviz.h"
#include <pxr/base/gf/matrix4d.h>
#include <pxr/base/tf/token.h>
#include <pxr/base/vt/value.h>
#include <pxr/usd/sdf/path.h>

PXR_NAMESPACE_USING_DIRECTIVE

namespace stageviz {

/**
 * @class Command
 * @brief Encapsulates an executable operation with optional undo support.
 *
 * A Command represents a unit of work that modifies session or stage state.
 * It provides a redo function (execute) and an optional undo function.
 *
 * Commands are intended to be executed via CommandStack, which manages
 * undo/redo history.
 *
 * All commands operate on a Session, which provides access to the USD stage,
 * selection model, masking, and notification system.
 */
class Command {
public:
    /**
     * @brief Function signature used by commands.
     *
     * The function receives the active Session and is responsible for
     * performing the operation. Implementations may perform work
     * asynchronously but must ensure thread-safe access to the session.
     */
    using Func = std::function<void(Session*)>;

    /**
     * @brief Constructs a command with redo and optional undo functions.
     *
     * @param redo Function executed when the command runs.
     * @param undo Function executed when the command is undone. If omitted,
     *             the command is treated as non-undoable.
     */
    Command(Func redo, Func undo = Func())
        : m_redo(std::move(redo))
        , m_undo(std::move(undo))
    {}

    /**
     * @brief Executes the command (redo).
     *
     * @param s Active session.
     */
    void execute(Session* s)
    {
        if (m_redo)
            m_redo(s);
    }

    /**
     * @brief Undoes the command.
     *
     * @param s Active session.
     */
    void undo(Session* s)
    {
        if (m_undo)
            m_undo(s);
    }

    /**
     * @brief Returns whether the command provides an undo operation.
     */
    bool isUndoable() const { return static_cast<bool>(m_undo); }

private:
    Func m_redo;  ///< Redo operation.
    Func m_undo;  ///< Undo operation.
};


/**
 * @brief Captures the edit-layer transform opinions that existed before an
 * interactive transform preview authored a stronger matrix override.
 *
 * The state is used by transform undo to restore the original edit-layer
 * xformOpOrder/matrix opinions exactly, or remove the temporary override when
 * no edit-layer transform opinion existed before the drag.
 */
struct TransformRootState {
    bool hadPrimSpec = false;
    bool hadXformOpOrderSpec = false;
    bool hadXformOpOrderDefault = false;
    VtValue xformOpOrderDefault;
    bool hadMatrixOpSpec = false;
    bool hadMatrixOpDefault = false;
    VtValue matrixOpDefault;
};

/** @name Command Factory Helpers */
///@{

/**
 * @brief Creates an undoable command that binds a material to prims.
 *
 * The material must already exist in the current USD stage. A direct material
 * binding is authored on every valid target prim. Undo restores the previous
 * direct binding when one existed, or removes the newly-authored direct binding
 * otherwise.
 *
 * @param paths Prim paths that should receive the material.
 * @param materialPath Path to an existing UsdShadeMaterial prim.
 * @return Undoable material-binding command.
 */
Command
bindMaterial(const QList<SdfPath>& paths, const SdfPath& materialPath);

/**
 * @brief Creates a command that loads payloads resolved from the specified paths.
 *
 * Each input path is interpreted as either:
 * - A payload prim path, or
 * - A path within or above a payload hierarchy, resolved via
 *   stage::selectionPayloadPaths().
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
 * @brief Creates a command that unloads payloads resolved from the specified paths.
 *
 * Each input path is expected to identify a payload prim. Successfully unloaded
 * payload paths are removed from the current selection and mask.
 *
 * Undo restores the previous load state, selection, and mask.
 *
 * @param paths Payload prim paths to unload.
 */
Command
unloadPayloads(const QList<SdfPath>& paths);

/**
 * @brief Creates a command that inverts the current selection at payload-root level.
 *
 * The current selection is resolved to payload prim paths using
 * stage::selectionPayloadPaths(). The command then selects all other payload
 * prim paths found in the stage, excluding those already resolved.
 *
 * Multiple selected prims within the same payload hierarchy resolve to a single
 * payload root. The resulting selection contains top-level, deduplicated payload paths.
 *
 * Undo restores the previous selection.
 */
Command
selectInvertPayload();

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

/**
 * @brief Creates a command that sets the stage up axis.
 *
 * Updates the stage metadata to use the specified up axis.
 *
 * Undo restores the previous up axis.
 *
 * @param stageUp Up axis to apply (Y or Z).
 */
Command
stageUp(Session::StageUp stageUp);

/**
 * @brief Creates a command that sets the stage default prim.
 *
 * The path must refer to a valid root prim, for example `/World`.
 * The pseudo-root `/`, empty paths, child prims such as `/World/Geom`,
 * and invalid prim paths are rejected.
 *
 * The previous default prim is stored so the operation can be undone.
 * If the stage had no default prim before the command, undo clears the
 * default prim metadata.
 *
 * @param path Root prim path to set as the stage default prim.
 * @return Command that applies and undoes the default prim change.
 */
Command
defaultPrimPath(const SdfPath& path);

/**
 * @brief Creates a command that deletes prims at the specified paths.
 *
 * Paths are reduced to minimal root paths to avoid redundant edits.
 * Only strongest-editable prims in the current edit target are affected.
 *
 * The command captures sufficient snapshot state to fully restore deleted
 * prims, including child order, on undo.
 *
 * Selection and mask are updated to remove affected paths.
 *
 * @param paths Prim paths to delete.
 */
Command
deletePaths(const QList<SdfPath>& paths);

/**
 * @brief Creates a command that duplicates prim specs as sibling prims.
 *
 * Each input path is reduced to a minimal root path and resolved to its
 * strongest authored SdfPrimSpec. That spec is copied into the opened root
 * layer under the same parent using a unique sibling name.
 *
 * This allows a prim introduced by a payload, reference, or other composition
 * arc to be copied into the editable stage without modifying the source asset.
 *
 * On success, the newly-created prims become the current selection.
 * Undo removes the duplicated prim specs and restores child ordering,
 * selection, and mask.
 *
 * @param paths Prim paths to duplicate.
 */
Command
duplicatePaths(const QList<SdfPath>& paths);

/**
 * @brief Creates a command that defines a generic prim under a parent.
 *
 * The name is sanitized and made unique under the parent. When @p typeName is
 * non-empty, the prim is defined with that USD type; otherwise an untyped prim
 * is defined.
 *
 * On success, the new prim becomes the current selection. Undo removes the
 * created prim and restores child ordering, selection, and mask.
 *
 * @param parentPath Parent prim path.
 * @param nameInput Desired name for the new prim.
 * @param typeName Optional USD schema type name, for example "Xform", "Mesh",
 *                 "Camera", or "Scope".
 */
Command
newPrimPath(const SdfPath& parentPath, const QString& nameInput, const TfToken& typeName = TfToken());

/**
 * @brief Creates a Scope prim under a parent.
 *
 * This is a convenience wrapper around newPrimPath() using the USD "Scope"
 * type. The new scope is selected on success and is fully undoable.
 *
 * @param parentPath Parent prim path.
 * @param nameInput Desired scope name.
 */
Command
newScopePath(const SdfPath& parentPath, const QString& nameInput);

/**
 * @brief Creates a UsdShadeMaterial with a default UsdPreviewSurface shader.
 *
 * The material is created under the specified parent using a unique name. A
 * child shader named "PreviewSurface" is created, assigned the
 * "UsdPreviewSurface" shader id, and connected to the material surface output.
 *
 * On success, the material becomes the current selection. Undo removes the
 * complete material hierarchy and restores child ordering, selection, and mask.
 *
 * @param parentPath Parent path, typically a Looks or Materials scope.
 * @param nameInput Desired material name.
 */
Command
newMaterialPath(const SdfPath& parentPath, const QString& nameInput);

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
 * On success, the new prim becomes the current selection.
 *
 * Undo removes the created prim and restores child ordering and mask.
 *
 * @param parentPath Parent prim path.
 * @param nameInput  Desired name for the new prim.
 */
Command
newXformPath(const SdfPath& parentPath, const QString& nameInput);

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


/**
 * @brief Creates a command that authors the default value of a USD attribute.
 *
 * The value is authored into the edit layer edit target. Undo restores the
 * previous edit-layer default opinion exactly; if there was no edit-layer
 * default opinion, undo clears the newly authored default.
 *
 * This command accepts scalar, vector, matrix, token/string, asset-path and
 * VtArray values. Array element editing in PropertyTree reconstructs the typed
 * array and submits it through this same command.
 *
 * @param attributePath USD property path identifying an attribute.
 * @param value Typed value to author.
 */
Command
setAttributeValue(const SdfPath& attributePath, const VtValue& value);

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
 * @brief Creates a command that resets local transforms for xformable prims.
 *
 * When a selected prim has an underlying transform opinion in a weaker layer,
 * the Stageviz edit-layer matrix/order override is removed so the authored
 * asset transform becomes visible again. When no weaker transform opinion
 * exists, a local identity matrix is authored in the opened edit layer.
 *
 * Undo restores the previous edit-layer transform opinions exactly.
 *
 * @param paths Prim paths whose local transforms should be reset.
 */
Command
resetTransforms(const QList<SdfPath>& paths);

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

/**
 * @brief Creates a command that applies world-space transforms to prims.
 *
 * The paths, before matrices, and after matrices are supplied explicitly so
 * interactive tools can preview transforms while dragging and still create a
 * single deterministic undo step when the drag finishes.
 *
 * The three lists must contain the same number of entries. Each matrix at an
 * index corresponds to the prim path at the same index.
 *
 * @param paths Prim paths to transform.
 * @param before World transforms captured at drag start.
 * @param after World transforms to apply for redo.
 * @param rootBefore Optional edit-layer transform state captured before an
 *                   interactive preview. When supplied, undo restores those
 *                   authored opinions exactly instead of leaving a matrix
 *                   override behind.
 */
Command
setTransforms(const QList<SdfPath>& paths, const QList<GfMatrix4d>& before, const QList<GfMatrix4d>& after,
              const QList<TransformRootState>& rootBefore = {});

///@}

}  // namespace stageviz
