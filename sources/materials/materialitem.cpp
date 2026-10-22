// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#include "materialitem.h"

namespace stageviz {

class MaterialItemPrivate {
public:
    void init();

    struct Data {
        MaterialItem* item = nullptr;
        int sourceRow = -1;
        SdfPath materialPath;
        SdfPath shaderPath;
        QString shaderId;
        QImage swatch;
    };

    Data d;
};

void
MaterialItemPrivate::init()
{
    Qt::ItemFlags flags = d.item->flags();
    flags &= ~Qt::ItemIsUserCheckable;
    flags &= ~Qt::ItemIsEditable;
    flags |= Qt::ItemIsDragEnabled;
    d.item->setFlags(flags);
}

MaterialItem::MaterialItem(QTreeWidget* parent)
    : TreeItem(parent)
    , p(new MaterialItemPrivate())
{
    p->d.item = this;
    p->init();
}

MaterialItem::MaterialItem(QTreeWidgetItem* parent)
    : TreeItem(parent)
    , p(new MaterialItemPrivate())
{
    p->d.item = this;
    p->init();
}

MaterialItem::~MaterialItem() = default;

TreeItem::ItemStates
MaterialItem::itemStates() const
{
    return Visible;
}

int
MaterialItem::sourceRow() const
{
    return p->d.sourceRow;
}

void
MaterialItem::setSourceRow(int row)
{
    p->d.sourceRow = row;
}

SdfPath
MaterialItem::materialPath() const
{
    return p->d.materialPath;
}

void
MaterialItem::setMaterialPath(const SdfPath& path)
{
    p->d.materialPath = path;
}

SdfPath
MaterialItem::shaderPath() const
{
    return p->d.shaderPath;
}

void
MaterialItem::setShaderPath(const SdfPath& path)
{
    p->d.shaderPath = path;
}

QString
MaterialItem::shaderId() const
{
    return p->d.shaderId;
}

void
MaterialItem::setShaderId(const QString& shaderId)
{
    p->d.shaderId = shaderId;
}

QImage
MaterialItem::swatch() const
{
    return p->d.swatch;
}

void
MaterialItem::setSwatch(const QImage& image)
{
    p->d.swatch = image;
}

}  // namespace stageviz
