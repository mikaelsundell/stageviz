// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#pragma once

#include "materialutils.h"
#include "stageviz.h"
#include "treewidget.h"

class QColor;

namespace stageviz {

class MaterialTreePrivate;

/**
 * @class MaterialTree
 * @brief Material network property inspector and node navigator.
 *
 * MaterialTree displays editable shader inputs for one or more selected
 * materials. With a single material it can navigate arbitrary connected shader
 * nodes while preserving the current node context. With multiple materials it
 * exposes only the common editable canonical parameters.
 */
class MaterialTree : public TreeWidget {
    Q_OBJECT
public:
    /**
     * @brief Constructs a material property tree.
     */
    explicit MaterialTree(QWidget* parent = nullptr);

    /**
     * @brief Releases the tree and its private state.
     */
    virtual ~MaterialTree();

    /**
     * @brief Sets the materials displayed by the inspector.
     *
     * A single material enables shader-node navigation. Multiple materials show
     * only common editable parameters.
     */
    void setMaterials(const QList<MaterialEntry>& materials);

    /**
     * @brief Clears all materials and inspector state.
     */
    void clearMaterials();

    /**
     * @brief Navigates the single-material inspector to a shader node.
     *
     * The path must identify a shader node belonging to the current material.
     */
    void navigateToNode(const SdfPath& path);

    /**
     * @brief Returns information for the node currently displayed by the inspector.
     */
    MaterialNodeInfo currentNodeInfo() const;

Q_SIGNALS:
    /**
     * @brief Emitted while interactively previewing a canonical float parameter.
     */
    void floatPreviewChanged(const QString& parameter, double value);

    /**
     * @brief Emitted while interactively previewing a canonical color parameter.
     */
    void colorPreviewChanged(const QString& parameter, const QColor& value);

    /**
     * @brief Emitted when a canonical float parameter is committed.
     */
    void floatChanged(const QString& parameter, double value);

    /**
     * @brief Emitted when a canonical color parameter is committed.
     */
    void colorChanged(const QString& parameter, const QColor& value);

    /**
     * @brief Emitted while previewing one or more concrete shader float inputs.
     *
     * Concrete input paths are used for arbitrary shader nodes where no canonical
     * material parameter name exists.
     */
    void floatInputsPreviewChanged(const QList<SdfPath>& inputPaths, double value);

    /**
     * @brief Emitted while previewing one or more concrete shader color inputs.
     */
    void colorInputsPreviewChanged(const QList<SdfPath>& inputPaths, const QColor& value);

    /**
     * @brief Emitted when one or more concrete shader float inputs are committed.
     */
    void floatInputsChanged(const QList<SdfPath>& inputPaths, double value);

    /**
     * @brief Emitted when one or more concrete shader color inputs are committed.
     */
    void colorInputsChanged(const QList<SdfPath>& inputPaths, const QColor& value);

    /**
     * @brief Requests removal of incoming connections from the specified inputs.
     */
    void disconnectInputsRequested(const QList<SdfPath>& inputPaths);

    /**
     * @brief Requests removal of authored input values from the current edit layer.
     *
     * Removing the local opinion allows weaker authored values or NodeDef defaults
     * to become visible again.
     */
    void resetInputsRequested(const QList<SdfPath>& inputPaths);

    /**
     * @brief Requests creation of a USD shader node connected to an input.
     *
     * @param inputPath Target shader input property.
     * @param shaderId Shader identifier used for the created node.
     * @param nodeName Requested node name.
     * @param outputName Output to connect to the target input.
     */
    void connectShaderNodeRequested(const SdfPath& inputPath, const QString& shaderId, const QString& nodeName,
                                    const TfToken& outputName);

    /**
     * @brief Requests creation of a MaterialX node connected to an input.
     *
     * The created node uses the specified NodeDef and connects its primary output
     * to the target input.
     *
     * @param inputPath Target shader input property.
     * @param nodeDef MaterialX NodeDef identifier.
     * @param nodeName Requested node name.
     */
    void connectMaterialXNodeRequested(const SdfPath& inputPath, const QString& nodeDef, const QString& nodeName);

    /**
     * @brief Emitted when the node displayed by the single-material inspector changes.
     *
     * The signal provides the node path and display metadata used by the surrounding
     * material editor for headers and previews.
     */
    void currentNodeChanged(const SdfPath& path, const QString& name, const QString& type, const QString& shaderId);

private:
    QScopedPointer<MaterialTreePrivate> p;
};

}  // namespace stageviz
