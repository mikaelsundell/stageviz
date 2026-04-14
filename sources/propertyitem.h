// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#pragma once

#include "treeitem.h"

namespace stageviz {

class PropertyItemPrivate;

/**
 * @class PropertyItem
 * @brief Tree item representing a USD property entry.
 *
 * Used by PropertyTree to display property data such as
 * attribute names and values. Each item corresponds to
 * a row in the property tree and typically represents
 * attributes, relationships, or metadata of a USD prim.
 */
class PropertyItem : public TreeItem {
public:
    /**
     * @brief Column indices used by the property tree.
     */
    enum Column {
        Name = 0,  ///< Property name column.
        Value      ///< Property value column.
    };

    /**
     * @brief Constructs a property item attached to a tree widget.
     *
     * @param parent Parent tree widget.
     */
    PropertyItem(QTreeWidget* parent);

    /**
     * @brief Constructs a property item attached to another item.
     *
     * @param parent Parent tree item.
     */
    PropertyItem(QTreeWidgetItem* parent);

    /**
     * @brief Destroys the PropertyItem instance.
     */
    virtual ~PropertyItem();

    /**
     * @brief Returns semantic state flags for the item.
     *
     * Used by the base class or delegates to derive Qt roles
     * (e.g. font, color, enabled state).
     */
    TreeItem::ItemStates itemStates() const;

private:
    QScopedPointer<PropertyItemPrivate> p;
};

}  // namespace stageviz
