// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#include "propertyitem.h"

namespace stageviz {

class PropertyItemPrivate {
public:
    void init();

    struct Data {
        PropertyItem* item = nullptr;
        PropertyItem::Kind kind = PropertyItem::Group;
        SdfPath propertyPath;
        int arrayIndex = -1;
        int chunkStart = 0;
        int chunkCount = 0;
        bool chunkPopulated = false;
        bool valueEditable = false;
    };
    Data d;
};

void
PropertyItemPrivate::init()
{
    Qt::ItemFlags flags = d.item->flags();
    flags &= ~Qt::ItemIsUserCheckable;
    flags &= ~Qt::ItemIsEditable;
    d.item->setFlags(flags);
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

PropertyItem::Kind
PropertyItem::kind() const
{
    return p->d.kind;
}

void
PropertyItem::setKind(Kind kind)
{
    p->d.kind = kind;
}

SdfPath
PropertyItem::propertyPath() const
{
    return p->d.propertyPath;
}

void
PropertyItem::setPropertyPath(const SdfPath& path)
{
    p->d.propertyPath = path;
}

int
PropertyItem::arrayIndex() const
{
    return p->d.arrayIndex;
}

void
PropertyItem::setArrayIndex(int index)
{
    p->d.arrayIndex = index;
}

int
PropertyItem::chunkStart() const
{
    return p->d.chunkStart;
}

int
PropertyItem::chunkCount() const
{
    return p->d.chunkCount;
}

void
PropertyItem::setChunkRange(int start, int count)
{
    p->d.chunkStart = start;
    p->d.chunkCount = count;
}

bool
PropertyItem::chunkPopulated() const
{
    return p->d.chunkPopulated;
}

void
PropertyItem::setChunkPopulated(bool populated)
{
    p->d.chunkPopulated = populated;
}

bool
PropertyItem::valueEditable() const
{
    return p->d.valueEditable;
}

void
PropertyItem::setValueEditable(bool editable)
{
    p->d.valueEditable = editable;

    Qt::ItemFlags flags = this->flags();
    if (editable)
        flags |= Qt::ItemIsEditable;
    else
        flags &= ~Qt::ItemIsEditable;

    setFlags(flags);
}

}  // namespace stageviz
