// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#include "primitem.h"
#include "application.h"
#include "command.h"
#include "qtutils.h"
#include "style.h"
#include "usdutils.h"
#include <QIcon>
#include <QPixmap>
#include <QPointer>
#include <pxr/usd/usd/prim.h>
#include <pxr/usd/usdGeom/imageable.h>
#include <pxr/usd/usdGeom/tokens.h>

PXR_NAMESPACE_USING_DIRECTIVE

namespace stageviz {

class PrimItemPrivate {
public:
    void init();
    void updateCache();
    struct Data {
        UsdStageRefPtr stage;
        SdfPath path;
        PrimItem* item = nullptr;
        bool dirty = true;
        bool visible = true;
        bool active = true;
        bool hasPayload = false;
        bool isEditTarget = true;
        bool isRoot = false;
        QString editName;
        QString name;
        QString typeName;
    };
    Data d;
};

void
PrimItemPrivate::init()
{
    d.item->setFlags(d.item->flags() | Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable
                     | Qt::ItemIsEditable);
    d.item->setCheckState(0, Qt::Unchecked);
}

void
PrimItemPrivate::updateCache()
{
    if (!d.dirty)
        return;
    d.visible = true;
    d.active = false;
    d.hasPayload = false;
    d.isEditTarget = true;
    d.isRoot = false;
    d.name.clear();
    d.typeName.clear();
    if (!d.stage) {
        d.dirty = false;
        return;
    }

    const UsdPrim prim = d.stage->GetPrimAtPath(d.path);
    d.isRoot = prim == d.stage->GetPseudoRoot();

    if (prim) {
        d.active = prim.IsActive();
        d.hasPayload = prim.HasPayload();
        d.name = StringToQString(prim.GetName().GetString());
        d.typeName = StringToQString(prim.GetTypeName().GetString());
        d.isEditTarget = stage::isEditTarget(d.stage, d.path);

        if (d.active && prim != d.stage->GetPseudoRoot())
            d.visible = stage::isVisible(d.stage, d.path);
    }
    else {
        d.name = StringToQString(d.path.GetName());
    }

    d.dirty = false;
}

PrimItem::PrimItem(QTreeWidget* parent, const UsdStageRefPtr& stage, const SdfPath& path)
    : TreeItem(parent)
    , p(new PrimItemPrivate())
{
    p->d.item = this;
    p->d.stage = stage;
    p->d.path = path;
    p->init();
}

PrimItem::PrimItem(QTreeWidgetItem* parent, const UsdStageRefPtr& stage, const SdfPath& path)
    : TreeItem(parent)
    , p(new PrimItemPrivate())
{
    p->d.item = this;
    p->d.stage = stage;
    p->d.path = path;
    p->init();
}

PrimItem::~PrimItem() = default;

QVariant
PrimItem::data(int column, int role) const
{
    p->updateCache();
    const SdfPath path = p->d.path;

    if (column == Name) {
        if (role == Qt::DisplayRole)
            return p->d.name;

        if (role == Qt::EditRole)
            return p->d.editName.isEmpty() ? p->d.name : p->d.editName;

        if (role == PrimItem::EditName)
            return p->d.editName;
    }

    if (role == Qt::DisplayRole || role == Qt::EditRole) {
        switch (column) {
        case Vis: return QString();
        default: break;
        }
    }

    if (role == Qt::ToolTipRole) {
        if (p->d.name.isEmpty())
            return QVariant();

        return QString("%1 (%2)").arg(StringToQString(path.GetString())).arg(p->d.typeName);
    }

    if (role == Qt::DecorationRole && column == Name) {
        Style::IconRole iconRole = Style::IconRole::Prim;

        if (p->d.typeName == "Material" || p->d.typeName == "Shader")
            iconRole = Style::IconRole::Material;
        else if (p->d.typeName == "Mesh")
            iconRole = Style::IconRole::Mesh;

        if (p->d.hasPayload)
            iconRole = Style::IconRole::Payload;

        return QIcon(style()->icon(iconRole, Style::UIScale::Medium));
    }

    if (role == Qt::DecorationRole && column == Vis) {
        if (!p->d.active || p->d.isRoot)
            return QVariant();

        return style()->icon(p->d.visible ? Style::IconRole::Visible : Style::IconRole::Hidden, Style::UIScale::Medium);
    }

    if (role == PrimItem::Path)
        return StringToQString(path.GetString());

    return TreeItem::data(column, role);
}

void
PrimItem::setData(int column, int role, const QVariant& value)
{
    if (column == Name && (role == Qt::EditRole || role == PrimItem::EditName)) {
        const QString newName = value.toString().trimmed();

        p->updateCache();

        if (newName.isEmpty() || newName == p->d.name) {
            p->d.editName.clear();
            TreeItem::setData(column, PrimItem::EditName, QString());
            return;
        }

        p->d.editName = newName;
        TreeItem::setData(column, PrimItem::EditName, newName);
        return;
    }

    TreeItem::setData(column, role, value);
}

SdfPath
PrimItem::path() const
{
    return p->d.path;
}

void
PrimItem::setPath(const SdfPath& path)
{
    if (p->d.path == path)
        return;

    p->d.path = path;
    p->d.dirty = true;
    p->d.editName.clear();
}

void
PrimItem::invalidate()
{
    p->d.dirty = true;
    p->d.editName.clear();
}

TreeItem::ItemStates
PrimItem::itemStates() const
{
    p->updateCache();

    ItemStates states = None;

    if (!p->d.active)
        return states;

    if (p->d.visible)
        states |= Visible;

    if (!p->d.isEditTarget)
        states |= ReadOnly;

    return states;
}

}  // namespace stageviz
