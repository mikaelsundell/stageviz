// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#pragma once

#include "stageviz.h"
#include <QStyledItemDelegate>

namespace stageviz {

/**
 * @class PropertyDelegate
 * @brief Creates type-aware editors for PropertyTree value cells.
 */
class PropertyDelegate : public QStyledItemDelegate {
    Q_OBJECT
public:
    PropertyDelegate(QObject* parent = nullptr);
    virtual ~PropertyDelegate();

    QWidget* createEditor(QWidget* parent, const QStyleOptionViewItem& option,
                          const QModelIndex& index) const override;
    void setEditorData(QWidget* editor, const QModelIndex& index) const override;
    void setModelData(QWidget* editor, QAbstractItemModel* model,
                      const QModelIndex& index) const override;
};

}  // namespace stageviz
