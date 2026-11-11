// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#pragma once

#include "command.h"
#include <QString>
#include <QStringList>

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
 * @brief Removes selected layer arcs as one undo step, processing children before parents.
 *
 * @param layerIdentifiers Unique local layer identifiers to remove.
 * @return Undoable batch using the single-layer removal rules.
 */
Command
removeLayers(const QStringList& layerIdentifiers);

/**
 * @brief Reloads selected file-backed layers from disk as one undo step.
 *
 * Only selected layers are reloaded. Undo restores their previous in-memory
 * contents; redo restores the captured update without reading the files again.
 *
 * @param layerIdentifiers Local, unmuted, editable layer identifiers to update.
 * @return Undoable layer update command.
 */
Command
updateLayers(const QStringList& layerIdentifiers);

/**
 * @brief Changes selected layer mute states as one undo step.
 *
 * Children are muted before parents; parents are unmuted before children.
 * Undo restores each layer's original state in reverse order.
 *
 * @param layerIdentifiers Layer identifiers to update.
 * @param muted Target mute state.
 * @return Undoable batch using the single-layer muting rules.
 */
Command
setLayersMuted(const QStringList& layerIdentifiers, bool muted);

/**
 * @brief Changes selected layer edit permissions as one undo step.
 *
 * @param layerIdentifiers Layer identifiers to update.
 * @param editable Target edit permission.
 * @return Undoable batch using the single-layer permission rules.
 */
Command
setLayersPermissionToEdit(const QStringList& layerIdentifiers, bool editable);

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
