// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#pragma once

#include "command.h"
#include <QString>

namespace stageviz {

/**
 * @brief Creates an undoable command that adds a USD file as a sublayer of the current edit layer.
 */
Command
addSublayer(const QString& filename);

/**
 * @brief Creates an undoable command that reorders sibling sublayers.
 *
 * The source layer is moved before or after the target layer in the parent
 * layer's sublayer list, changing their composition strength.
 */
Command
reorderSublayers(const QString& parentIdentifier, const QString& sourceIdentifier, const QString& targetIdentifier,
                 bool afterTarget);

/**
 * @brief Creates an undoable command that removes a layer from the stage.
 *
 * The layer's sublayer reference is removed from its parent. The USD file is
 * kept on disk, and undo restores the original sublayer list.
 */
Command
removeLayer(const QString& layerIdentifier);

/**
 * @brief Creates an undoable command that mutes or unmutes a local stage layer.
 *
 * The layer and its sublayers are retained while muted so their contents remain
 * available to the application and to undo/redo.
 */
Command
setLayerMuted(const QString& layerIdentifier, bool muted);

/**
 * @brief Creates an undoable command that changes a layer's edit permission.
 */
Command
setLayerPermissionToEdit(const QString& layerIdentifier, bool editable);

/**
 * @brief Creates a command that changes the active edit layer.
 *
 * The target layer must already be a local layer of the current stage. The
 * layer is addressed by its SdfLayer identifier so UI and Python callers use
 * the same command-stack path. Session::editLayerChanged is emitted whenever
 * the command changes or restores the edit target. Undo restores the previously
 * active edit layer.
 *
 * @param layerIdentifier Identifier of a local SdfLayer in the current stage.
 * @return Undoable edit-layer selection command.
 */
Command
setEditLayer(const QString& layerIdentifier);

}  // namespace stageviz
