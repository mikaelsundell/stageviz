// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#include "treeitem.h"
#include "application.h"
#include "style.h"

namespace stageviz {

class TreeItemPrivate {
public:
    struct Data {};
    Data d;
};

TreeItem::TreeItem(QTreeWidget* parent)
    : QTreeWidgetItem(parent)
    , p(new TreeItemPrivate())
{}

TreeItem::TreeItem(QTreeWidgetItem* parent)
    : QTreeWidgetItem(parent)
    , p(new TreeItemPrivate())
{}

TreeItem::~TreeItem() {}

TreeItem::ItemStates
TreeItem::itemStates() const
{
    return None;
}

QVariant
TreeItem::data(int column, int role) const
{
    const ItemStates states = itemStates();
    if (role == Qt::FontRole && (states & ReadOnly)) {
        QFont f;
        f.setItalic(true);
        return f;
    }
    if (role == Qt::ForegroundRole && !(states & Visible)) {
        return QBrush(style()->color(Style::ColorRole::Text, Style::UIState::Disabled));
    }
    return QTreeWidgetItem::data(column, role);
}

Qt::ItemFlags
TreeItem::flags() const
{
    Qt::ItemFlags f = QTreeWidgetItem::flags();
    f &= ~Qt::ItemIsUserCheckable;
    return f;
}

}  // namespace stageviz
