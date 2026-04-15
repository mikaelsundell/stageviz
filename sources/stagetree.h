// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#pragma once

#include "stageviz.h"

#include "selectionlist.h"
#include "session.h"
#include "treewidget.h"
#include <QTreeWidget>
#include <pxr/usd/usd/stage.h>

PXR_NAMESPACE_USING_DIRECTIVE

namespace stageviz {

class StageTreePrivate;
class ViewContext;

/**
 * @class StageTree
 * @brief Tree view for browsing USD stage hierarchies.
 *
 * Displays the prim hierarchy of a USD stage and allows the user
 * to navigate, filter, and select prims. The widget integrates
 * with the viewer selection list and emits signals when the
 * prim selection changes.
 */
class StageTree : public TreeWidget {
    Q_OBJECT
public:
    /**
     * @brief Constructs the stage tree widget.
     *
     * @param parent Optional parent widget.
     */
    StageTree(QWidget* parent = nullptr);

    /**
     * @brief Destroys the StageTree instance.
     */
    virtual ~StageTree();

    /** @name Context */
    ///@{

    /**
     * @brief Returns the current view context.
     */
    ViewContext* context() const;

    /**
     * @brief Sets the view context used by this widget.
     *
     * @param context View context for stage locking and command execution.
     */
    void setContext(ViewContext* context);

    ///@}

    /** @name Tree Control */
    ///@{

    /**
     * @brief Clears and closes the current stage view.
     */
    void close();

    /**
     * @brief Collapses all tree nodes.
     */
    void collapse();

    /**
     * @brief Expands all tree nodes.
     */
    void expand();

    /**
      * @brief Expands the tree to the specified depth.
      *
      * If a path is provided, the expansion is applied relative to that node.
      */
    void expandDepth(int depth, const SdfPath& path = SdfPath());

    /**
      * @brief Returns the depth of a node in the tree.
      */
    int depth(const SdfPath& path = SdfPath()) const;

    /**
      * @brief Returns the maximum depth reachable from a node.
      */
    int maxDepth(const SdfPath& path = SdfPath()) const;

    ///@}

    /** @name Filtering */
    ///@{

    /**
     * @brief Returns the active prim name filter.
     */
    QString filter() const;

    /**
     * @brief Sets a filter used to restrict visible prims.
     *
     * @param filter Filter string applied to prim names.
     */
    void setFilter(const QString& filter);

    ///@}

    /** @name Payload Control */
    ///@{

    /**
     * @brief Returns whether payload loading is enabled.
     */
    bool payloadEnabled() const;

    /**
     * @brief Enables or disables payload loading.
     *
     * @param enabled Payload loading state.
     */
    void setPayloadEnabled(bool enabled);

    ///@}

    /** @name Stage Updates */
    ///@{

    /**
     * @brief Updates the active stage mask state in the tree.
     *
     * Applies the current isolation or masking paths so the widget can
     * reflect which prims are considered active in the stage view.
     *
     * @param paths Masked prim paths.
     */
    void updateMask(const QList<SdfPath>& paths);

    /**
     * @brief Updates the tree from a USD stage.
     *
     * Rebuilds the tree hierarchy to reflect the contents
     * of the provided stage.
     *
     * @param stage USD stage to display.
     */
    void updateStage(UsdStageRefPtr stage);

    /**
    * @brief Updates prims using a USD notice batch.
    *
    * Entries follow UsdNotice::ObjectsChanged semantics:
    * info-only changes, asset resyncs, and structural resyncs.
    *
    * @param batch Batched USD change entries.
    */
    void updatePrims(const NoticeBatch& batch);

    /**
     * @brief Updates the tree selection.
     *
     * Synchronizes the widget selection with the provided
     * list of prim paths.
     *
     * @param paths Selected prim paths.
     */
    void updateSelection(const QList<SdfPath>& paths);

    ///@}

Q_SIGNALS:

    /**
     * @brief Emitted when the prim selection changes.
     *
     * @param paths Selected prim paths.
     */
    void primSelectionChanged(const QList<SdfPath>& paths);

protected:
    /** @name Event Handling */
    ///@{

    /**
     * @brief Starts a drag operation from the current tree selection.
     *
     * Packages the selected prim paths into drag mime data and begins
     * a drag using the supported drop actions.
     *
     * @param supportedActions Allowed drag-and-drop actions.
     */
    void startDrag(Qt::DropActions supportedActions) override;

    /**
     * @brief Handles drag enter events for incoming tree drops.
     *
     * Validates the incoming mime data and accepts the event when the
     * dragged payload can be handled by the stage tree.
     *
     * @param event Drag enter event.
     */
    void dragEnterEvent(QDragEnterEvent* event) override;

    /**
     * @brief Handles drag move events over the tree.
     *
     * Updates drag feedback while the cursor moves across potential
     * drop targets in the stage hierarchy.
     *
     * @param event Drag move event.
     */
    void dragMoveEvent(QDragMoveEvent* event) override;

    /**
     * @brief Handles drop events in the tree.
     *
     * Resolves the target item under the cursor and applies the
     * corresponding stage operation for the dropped prim paths.
     *
     * @param event Drop event.
     */
    void dropEvent(QDropEvent* event) override;

    /**
     * @brief Handles context menu requests.
     */
    void contextMenuEvent(QContextMenuEvent* event) override;

    /**
     * @brief Handles key press events.
     */
    void keyPressEvent(QKeyEvent* event) override;

    /**
     * @brief Handles mouse press events.
     */
    void mousePressEvent(QMouseEvent* event) override;

    /**
     * @brief Handles mouse move events.
     */
    void mouseMoveEvent(QMouseEvent* event) override;

    ///@}

private:
    Q_DISABLE_COPY_MOVE(StageTree)
    QScopedPointer<StageTreePrivate> p;
};

}  // namespace stageviz
