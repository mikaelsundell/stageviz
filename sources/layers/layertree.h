// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#pragma once

#include "stageviz.h"
#include "treewidget.h"
#include <QContextMenuEvent>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QMouseEvent>
#include <QScopedPointer>

namespace stageviz {

class LayerTreePrivate;

/**
 * @class LayerTree
 * @brief Lists the local layers in the current USD stage.
 *
 * Selecting a row does not change the stage edit target. Context actions
 * provide layer operations and requests to create or add layers.
 */
class LayerTree : public TreeWidget {
    Q_OBJECT
public:
    /**
     * @brief Constructs a layer tree.
     *
     * @param parent Optional parent widget.
     */
    explicit LayerTree(QWidget* parent = nullptr);

    /**
     * @brief Destroys the LayerTree instance.
     */
    ~LayerTree() override;

    /**
     * @brief Refreshes the displayed layer stack from the current stage.
     */
    void refresh();

    /**
     * @brief Selects composed prim paths authored by the current layer row.
     */
    void selectCurrentLayer();

Q_SIGNALS:
    /**
     * @brief Requests creation of a new layer file and its addition to the stage.
     */
    void newLayerRequested();

    /**
     * @brief Requests addition of an existing layer file to the stage.
     */
    void addLayerRequested();

    /**
     * @brief Emitted after a dirty layer has been saved.
     */
    void layerSaved();

protected:
    /**
     * @brief Starts a drag operation for the current layer row.
     *
     * @param supportedActions Actions supported by the drag operation.
     */
    void startDrag(Qt::DropActions supportedActions) override;

    /**
     * @brief Handles a layer drag entering the tree.
     *
     * @param event Drag-enter event.
     */
    void dragEnterEvent(QDragEnterEvent* event) override;

    /**
     * @brief Updates the drop indicator while a layer is dragged over the tree.
     *
     * @param event Drag-move event.
     */
    void dragMoveEvent(QDragMoveEvent* event) override;

    /**
     * @brief Handles a layer drop on the tree.
     *
     * @param event Drop event.
     */
    void dropEvent(QDropEvent* event) override;

    /**
     * @brief Displays the context menu for the current layer row.
     *
     * @param event Context-menu event.
     */
    void contextMenuEvent(QContextMenuEvent* event) override;

    /**
     * @brief Handles mouse presses on the tree.
     *
     * @param event Mouse event.
     */
    void mousePressEvent(QMouseEvent* event) override;

private:
    Q_DISABLE_COPY_MOVE(LayerTree)
    QScopedPointer<LayerTreePrivate> p;
};

}  // namespace stageviz
