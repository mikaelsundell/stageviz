// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#pragma once

#include "stageviz.h"
#include <QTreeWidgetItem>

namespace stageviz {

class ProgressItemPrivate;

/**
 * @class ProgressItem
 * @brief Tree item representing a progress or status entry.
 *
 * Used by ProgressView to display information such as processing
 * progress, statistics, or scene status values. Each item represents
 * a row in the tree with a name and a corresponding value.
 */
class ProgressItem : public QTreeWidgetItem {
public:
    /**
     * @brief Column indices used by the progress tree.
     */
    enum Column {
        /**
         * @brief Label describing the progress entry.
         */
        Name = 0,
        /**
         * @brief Value associated with the entry.
         */
        Value
    };

    /**
     * @brief Constructs a progress item attached to a tree widget.
     *
     * @param parent Parent tree widget.
     */
    ProgressItem(QTreeWidget* parent);

    /**
     * @brief Constructs a progress item attached to another item.
     *
     * @param parent Parent tree item.
     */
    ProgressItem(QTreeWidgetItem* parent);

    /**
     * @brief Destroys the ProgressItem instance.
     */
    virtual ~ProgressItem();

private:
    QScopedPointer<ProgressItemPrivate> p;
};

}  // namespace stageviz
