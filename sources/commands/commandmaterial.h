// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#pragma once

#include "command.h"
#include <QList>
#include <QString>
#include <pxr/usd/sdf/path.h>
#include <pxr/usd/sdf/types.h>

PXR_NAMESPACE_USING_DIRECTIVE

namespace stageviz {

struct MaterialXNodeDefinition;

/**
 * @brief Built-in material network type created by newMaterial().
 */
enum class MaterialType { PreviewSurface = 0, StandardSurface, OpenPBRSurface };

/**
 * @brief Creates one built-in material network in the stage material scope.
 *
 * Creation is undoable and preserves the current scene selection and mask.
 * The material is authored in the active edit layer through MaterialUtils so
 * the Material Editor and command API use the same network definitions.
 *
 * @param type Built-in material network type to create.
 * @return Undoable material-creation command.
 */
Command
newMaterial(MaterialType type);

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
 * @brief Creates a command that disconnects shader inputs.
 *
 * Incoming connections authored in the current edit layer are removed while
 * the input properties remain. Undo restores the previous input specs.
 *
 * @param inputPaths Shader input property paths to disconnect.
 * @return Undoable shader-input disconnection command.
 */
Command
disconnectShaderInputs(const QList<SdfPath>& inputPaths);

/**
 * @brief Creates a command that connects an existing shader output to an input.
 *
 * The input and output must already exist. Undo restores the input's previous
 * authored state.
 *
 * @param inputPath Shader input property path.
 * @param sourceOutputPath Shader output property path.
 * @return Undoable shader-connection command.
 */
Command
connectShaderInput(const SdfPath& inputPath, const SdfPath& sourceOutputPath);

/**
 * @brief Creates a command that resets shader-input opinions in the edit layer.
 *
 * Removing the authored input property exposes weaker opinions or its schema
 * default. Undo restores the original property spec.
 *
 * @param inputPaths Shader input property paths to reset.
 * @return Undoable shader-input reset command.
 */
Command
resetShaderInputs(const QList<SdfPath>& inputPaths);

/**
 * @brief Creates a shader node and connects one of its outputs to an input.
 *
 * The node is created beside the prim that owns the input. Its selected output
 * is connected to the input, and undo restores the input and removes the node.
 *
 * @param inputPath Shader input property path.
 * @param shaderId USD shader identifier.
 * @param nodeName Desired name for the new node.
 * @param outputName Shader output to connect.
 * @return Undoable shader-node creation and connection command.
 */
Command
connectShaderNode(const SdfPath& inputPath, const QString& shaderId, const QString& nodeName,
                  const TfToken& outputName);

/**
 * @brief Creates a MaterialX node from a NodeDef and connects its output to an input.
 *
 * The complete typed NodeDef interface and declared defaults are authored so
 * USD socket types match the MaterialX definition.
 *
 * @param inputPath Shader input property path.
 * @param nodeDef MaterialX NodeDef identifier.
 * @param nodeName Desired name for the new node.
 * @return Undoable MaterialX-node creation and connection command.
 */
Command
connectMaterialXNode(const SdfPath& inputPath, const QString& nodeDef, const QString& nodeName);

/**
 * @brief Creates an unconnected shader node below a material.
 *
 * The shader identifier and declared default inputs are authored. An output is
 * created when @p outputName is non-empty and does not already exist.
 *
 * @param materialPath Material prim path that will own the shader node.
 * @param shaderId USD shader identifier.
 * @param nodeName Desired name for the new node.
 * @param outputName Optional output name to create.
 * @param outputType Type for the optional output.
 * @return Undoable shader-node creation command.
 */
Command
newShaderNode(const SdfPath& materialPath, const QString& shaderId, const QString& nodeName, const TfToken& outputName,
              const SdfValueTypeName& outputType);

/**
 * @brief Creates an unconnected MaterialX node below a material.
 *
 * The complete typed NodeDef interface and declared defaults are authored.
 *
 * @param materialPath Material prim path that will own the node.
 * @param definition MaterialX NodeDef definition and interface.
 * @return Undoable MaterialX-node creation command.
 */
Command
newMaterialXNode(const SdfPath& materialPath, const MaterialXNodeDefinition& definition);

/**
 * @brief Creates a command that deletes a shader node.
 *
 * An edit-layer spec is removed directly. If the node comes from a weaker
 * layer, an inactive override hides it without modifying its source layer.
 * Undo restores the prior spec or removes the inactive override.
 *
 * @param nodePath Shader prim path to delete.
 * @return Undoable shader-node deletion command.
 */
Command
deleteShaderNode(const SdfPath& nodePath);

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

}  // namespace stageviz
