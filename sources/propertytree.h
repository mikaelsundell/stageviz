// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#pragma once

#include "stageviz.h"

#include "notice.h"
#include "treewidget.h"
#include <QTreeWidget>
#include <pxr/usd/usd/stage.h>

PXR_NAMESPACE_USING_DIRECTIVE

namespace stageviz {

class PropertyTreePrivate;
class ViewContext;

/**
 * @class PropertyTree
 * @brief Tree view displaying properties of selected USD prims.
 *
 * Provides a hierarchical view of prim properties such as attributes,
 * relationships, and metadata. The tree updates when the stage changes
 * or when the selection of prims is modified.
 *
 * Typically used alongside the StageTree and RenderView to inspect
 * detailed data for the currently selected prims.
 */
class PropertyTree : public TreeWidget {
    Q_OBJECT
public:
    /**
     * @brief Constructs the property tree widget.
     *
     * @param parent Optional parent widget.
     */
    PropertyTree(QWidget* parent = nullptr);

    /**
     * @brief Destroys the PropertyTree instance.
     */
    virtual ~PropertyTree();

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
     * @brief Clears the current property view.
     */
    void close();

    ///@}

    /** @name Stage Updates */
    ///@{

    /**
     * @brief Updates the property tree for the given USD stage.
     *
     * @param stage USD stage to inspect.
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
     * @brief Updates the tree to reflect the current selection.
     *
     * @param paths Selected prim paths.
     */
    void updateSelection(const QList<SdfPath>& paths);

    ///@}

private:
    QScopedPointer<PropertyTreePrivate> p;
};

}  // namespace stageviz
