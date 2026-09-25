// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#pragma once

#include "materialutils.h"
#include "stageviz.h"
#include <QScopedPointer>
#include <QWidget>

class QGraphicsView;

namespace stageviz {

class MaterialGraphPrivate;

/**
 * @class MaterialGraph
 * @brief Interactive UsdShade and MaterialX graph editor.
 *
 * MaterialGraph presents one material network and provides navigation,
 * filtering, zoom/pan interaction, node selection, and connection editing.
 * The widget owns no USD authoring policy itself; edit requests are emitted to
 * the surrounding material dialog.
 */
class MaterialGraph : public QWidget {
    Q_OBJECT
public:
    /**
     * @brief Constructs a material graph page.
     */
    explicit MaterialGraph(QWidget* parent = nullptr);

    /**
     * @brief Releases the graph and its private state.
     */
    ~MaterialGraph() override;

    /**
     * @brief Sets the material network displayed by the graph.
     */
    void setMaterial(const MaterialEntry& material);

    /**
     * @brief Returns the material currently displayed by the graph.
     */
    MaterialEntry material() const;

    /**
     * @brief Returns the path of the current material prim.
     */
    SdfPath materialPath() const;

    /**
     * @brief Returns the path of the currently selected graph node.
     */
    SdfPath selectedNodePath() const;

    /**
     * @brief Returns the graphics view used by the graph page.
     */
    QGraphicsView* graphicsView() const;

    /**
     * @brief Returns the graphics viewport used for drag/drop routing.
     */
    QWidget* viewport() const;

    /**
     * @brief Refreshes the graph while preserving the user's node layout.
     */
    void refresh();

    /**
     * @brief Rebuilds the graph and regenerates the automatic node layout.
     */
    void rebuild();

    /**
     * @brief Frames the complete material network in the view.
     */
    void frameAll();

    /**
     * @brief Frames the currently selected graph nodes.
     */
    void frameSelected();

Q_SIGNALS:
    /**
     * @brief Emitted when a graph node becomes selected.
     */
    void nodeSelected(const SdfPath& path);

    /**
     * @brief Requests connection of an existing shader output to an input.
     */
    void connectionRequested(const SdfPath& inputPath, const SdfPath& sourceOutputPath);

    /**
     * @brief Requests removal of the incoming connection from an input.
     */
    void disconnectRequested(const SdfPath& inputPath);

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
     * @param inputPath Target shader input property.
     * @param nodeDef MaterialX NodeDef identifier.
     * @param nodeName Requested node name.
     */
    void connectMaterialXNodeRequested(const SdfPath& inputPath, const QString& nodeDef, const QString& nodeName);

protected:
    /**
     * @brief Handles graph-view interaction and shortcut routing.
     */
    bool eventFilter(QObject* object, QEvent* event) override;

private:
    QScopedPointer<MaterialGraphPrivate> p;
};

}  // namespace stageviz
