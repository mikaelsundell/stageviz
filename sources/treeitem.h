// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#pragma once

#include <QTreeWidgetItem>

namespace stageviz {

class TreeItemPrivate;

/**
 * @class TreeItem
 * @brief Generic tree item providing shared semantic roles.
 *
 * TreeItem extends QTreeWidgetItem with a minimal set of
 * standardized roles used by delegates and views.
 *
 * It is intentionally domain-agnostic and should not depend
 * on USD or any specific session.
 *
 * Derived classes (e.g. PrimItem) provide actual data.
 */
class TreeItem : public QTreeWidgetItem {
public:
    /**
     * @brief Custom roles shared across all tree items.
     */
    enum ItemState { None = 0, ReadOnly = 1 << 0, Visible = 1 << 1 };
    Q_DECLARE_FLAGS(ItemStates, ItemState)

    /**
     * @brief Constructs a top-level tree item.
     *
     * Creates an item owned by the given QTreeWidget. The item
     * is inserted as a top-level entry in the widget.
     *
     * @param parent The owning tree widget.
     */
    TreeItem(QTreeWidget* parent);

    /**
     * @brief Constructs a child tree item.
     *
     * Creates an item owned by the given parent item. The item
     * is inserted as a child of the parent in the tree hierarchy.
     *
     * @param parent The parent tree item.
     */
    TreeItem(QTreeWidgetItem* parent);

    /**
     * @brief Destroys the TreeItem instance.
     */
    virtual ~TreeItem();

    /**
     * @brief Returns item data for the specified role.
     *
     * The base implementation provides default values for
     * common roles. Derived classes override as needed.
     */
    QVariant data(int column, int role) const override;

    /**
     * @brief Returns semantic state flags for the item.
     *
     * Used by the base class or delegates to derive Qt roles
     * (e.g. font, color, enabled state).
     */
    virtual ItemStates itemStates() const;

    /**
     * @brief Returns Qt item flags.
     *
     * Defines interaction (selectable, editable, checkable, etc.).
     */
    Qt::ItemFlags flags() const;

private:
    QScopedPointer<TreeItemPrivate> p;
};

}  // namespace stageviz
