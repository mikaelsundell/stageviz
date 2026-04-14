// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#include "propertyitem.h"
#include <QPointer>

namespace stageviz {
class PropertyItemPrivate {
public:
    void init();
    struct Data {
        PropertyItem* item;
    };
    Data d;
};

void
PropertyItemPrivate::init()
{
    Qt::ItemFlags itemFlags = d.item->flags();
    itemFlags &= ~Qt::ItemIsUserCheckable;
    itemFlags &= ~Qt::ItemIsEditable;
    d.item->setFlags(itemFlags);
}

PropertyItem::PropertyItem(QTreeWidget* parent)
    : TreeItem(parent)
    , p(new PropertyItemPrivate())
{
    p->d.item = this;
    p->init();
}

PropertyItem::PropertyItem(QTreeWidgetItem* parent)
    : TreeItem(parent)
    , p(new PropertyItemPrivate())
{
    p->d.item = this;
    p->init();
}

PropertyItem::~PropertyItem() = default;

TreeItem::ItemStates
PropertyItem::itemStates() const
{
    return Visible;
}

}  // namespace stageviz
