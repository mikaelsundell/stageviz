// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#pragma once

#include "stageviz.h"

#include "treeitem.h"
#include <pxr/usd/usd/stage.h>

PXR_NAMESPACE_USING_DIRECTIVE

namespace stageviz {

class PrimItemPrivate;

/**
 * @class PrimItem
 * @brief Tree item representing a USD prim in the stage hierarchy.
 *
 * Used by StageTree to represent a prim within the USD stage.
 * Each item corresponds to a prim path and displays information
 * such as the prim name, type, and visibility state.
 */
class PrimItem : public TreeItem {
public:
    enum PrimRole { Path = Qt::UserRole + 1, EditName };

    /**
     * @brief Column indices used by the stage tree.
     */
    enum Column {
        Name = 0,  ///< Name column.
        Vis = 1    ///< Visibility state column.
    };

    /**
     * @brief Constructs a root-level prim item.
     *
     * @param parent Parent tree widget.
     * @param stage  USD stage containing the prim.
     * @param path   Path of the prim represented by the item.
     */
    PrimItem(QTreeWidget* parent, const UsdStageRefPtr& stage, const SdfPath& path);

    /**
     * @brief Constructs a child prim item.
     *
     * @param parent Parent tree item.
     * @param stage  USD stage containing the prim.
     * @param path   Path of the prim represented by the item.
     */
    PrimItem(QTreeWidgetItem* parent, const UsdStageRefPtr& stage, const SdfPath& path);

    /**
     * @brief Destroys the PrimItem instance.
     */
    virtual ~PrimItem();

    /**
     * @brief Returns item data for a column and role.
     *
     * Provides text, icons, and state used by the view.
     */
    QVariant data(int column, int role) const override;

    /**
     * @brief Sets item data for a column and role.
     *
     * Handles user edits or state changes (e.g. checkboxes) and may
     * propagate updates to the underlying model.
     */
    void setData(int column, int role, const QVariant& value) override;

    /**
     * @brief Returns the prim path for this item.
     */
    SdfPath path() const;

    /**
     * @brief Sets the prim path and invalidates cached state.
     */
    void setPath(const SdfPath& path);

    /**
      * @brief Marks the item's cached data as invalid.
      *
      * Forces the item to refresh its cached state (e.g. visibility, payload)
      * on the next access or update.
      */
    void invalidate();

    /**
     * @brief Returns semantic state flags for the item.
     *
     * Used by the base class or delegates to derive Qt roles
     * (e.g. font, color, enabled state).
     */
    TreeItem::ItemStates itemStates() const override;

private:
    QScopedPointer<PrimItemPrivate> p;
};

}  // namespace stageviz
