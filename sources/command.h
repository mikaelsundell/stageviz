// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#pragma once

#include "session.h"
#include "stageviz.h"
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

/** @name Command Factory Helpers */
///@{

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
 * @brief Creates a command that selects all leaf prim paths within the current mask.
 *
 * Traverses the stage and collects leaf prims (prims without traversable children),
 * optionally constrained by the current session mask.
 *
 * Undo restores the previous selection.
 */
Command
selectAll();

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
 * @brief Creates a command that reparents or reorders a prim.
 *
 * Moves a prim from @p fromPath to @p newParentPath. If the parent remains
 * the same, the operation behaves as a reorder using @p insertIndex.
 *
 * Child ordering is preserved and updated for both source and destination
 * parents. Selection and mask are remapped accordingly.
 *
 * Undo restores the original hierarchy and ordering.
 *
 * @param fromPath      Prim path to move.
 * @param newParentPath Destination parent path.
 * @param insertIndex   Target index in the new parent's child order.
 */
Command
movePath(const SdfPath& fromPath, const SdfPath& newParentPath, int insertIndex);

///@}

}  // namespace stageviz
