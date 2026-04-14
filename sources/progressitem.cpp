// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#include "progressitem.h"
#include <QPointer>

namespace stageviz {
class ProgressItemPrivate {
public:
    void init();
    struct Data {
        ProgressItem* item;
    };
    Data d;
};

void
ProgressItemPrivate::init()
{
    Qt::ItemFlags itemFlags = d.item->flags();
    itemFlags &= ~Qt::ItemIsUserCheckable;
    itemFlags &= ~Qt::ItemIsEditable;
    d.item->setFlags(itemFlags);
}

ProgressItem::ProgressItem(QTreeWidget* parent)
    : QTreeWidgetItem(parent)
    , p(new ProgressItemPrivate())
{}

ProgressItem::ProgressItem(QTreeWidgetItem* parent)
    : QTreeWidgetItem(parent)
    , p(new ProgressItemPrivate())
{}

ProgressItem::~ProgressItem() = default;

}  // namespace stageviz
