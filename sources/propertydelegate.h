// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#pragma once

#include "stageviz.h"
#include "treewidget.h"

namespace stageviz {

/**
 * @class PropertyDelegate
 * @brief StageViz tree delegate with type-aware property editors.
 *
 * Inherits the shared TreeWidget item layout and painting so PropertyTree
 * remains visually identical to StageTree while providing specialized
 * editors for USD property values.
 */
class PropertyDelegate : public TreeWidget::ItemDelegate {
    Q_OBJECT
public:
    explicit PropertyDelegate(QObject* parent = nullptr);
    ~PropertyDelegate() override;

    QWidget* createEditor(QWidget* parent, const QStyleOptionViewItem& option, const QModelIndex& index) const override;
    void setEditorData(QWidget* editor, const QModelIndex& index) const override;
    void setModelData(QWidget* editor, QAbstractItemModel* model, const QModelIndex& index) const override;
};

}  // namespace stageviz
