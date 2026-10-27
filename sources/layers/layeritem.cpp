// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#include "layeritem.h"
#include "application.h"
#include "roles.h"
#include "style.h"
#include <QFont>
#include <QIcon>
#include <pxr/usd/usd/prim.h>

namespace stageviz {

class LayerItemPrivate {
public:
    struct Data {
        LayerItem::State state;
    };
    Data d;
};

LayerItem::LayerItem(QTreeWidget* parent)
    : TreeItem(parent)
    , p(new LayerItemPrivate())
{}

LayerItem::~LayerItem() = default;

void
LayerItem::setState(const State& state)
{
    p->d.state = state;

    if (text(Name) != state.name)
        setText(Name, state.name);

    QFont nameFont = font(Name);
    if (nameFont.bold() != state.editTarget) {
        nameFont.setBold(state.editTarget);
        setFont(Name, nameFont);
    }

    const auto setRole = [this](int role, const QVariant& value) {
        if (data(Name, role) != value)
            setData(Name, role, value);
    };
    setRole(roles::layer::identifier, state.identifier);
    setRole(roles::layer::parentIdentifier, state.parentIdentifier);
    setRole(roles::layer::inheritedMuted, state.inheritedMuted);

    const auto setIcon = [this](int column, const QIcon& icon) {
        if (this->icon(column).cacheKey() != icon.cacheKey())
            this->setIcon(column, icon);
    };
    const auto setToolTip = [this](int column, const QString& text) {
        if (toolTip(column) != text)
            this->setToolTip(column, text);
    };

    setIcon(Name, style()->icon(Style::IconRole::Layer));
    setToolTip(Name, state.tooltip);

    Qt::ItemFlags itemFlags = flags();
    if (!state.parentIdentifier.isEmpty() && state.canReorder)
        itemFlags |= Qt::ItemIsDragEnabled | Qt::ItemIsDropEnabled;
    else
        itemFlags &= ~(Qt::ItemIsDragEnabled | Qt::ItemIsDropEnabled);
    if (flags() != itemFlags)
        setFlags(itemFlags);

    if (state.root) {
        setIcon(Visibility, QIcon());
        setIcon(Opinions, QIcon());
        setIcon(Permission, QIcon());
        setToolTip(Visibility, QString());
        setToolTip(Opinions, QString());
        setToolTip(Permission, QString());
        return;
    }

    setIcon(Visibility, style()->icon(state.muted ? Style::IconRole::Hidden : Style::IconRole::Visible));
    QString visibilityTooltip;
    if (state.inheritedMuted)
        visibilityTooltip = tr("Layer is muted by an ancestor layer.");
    else if (!state.canMute)
        visibilityTooltip = tr("The active edit layer cannot be muted.");
    else if (state.directlyMuted)
        visibilityTooltip = tr("Layer is muted. Click to unmute.");
    else
        visibilityTooltip = tr("Layer is contributing. Click to mute.");
    setToolTip(Visibility, visibilityTooltip);

    if (state.primSpecCount > 0) {
        setIcon(Opinions, style()->icon(Style::IconRole::Override));
        setToolTip(Opinions, tr("Contains authored opinions on %1 prim spec(s).").arg(state.primSpecCount));
    }
    else {
        setIcon(Opinions, QIcon());
        setToolTip(Opinions, tr("No authored prim opinions in this layer."));
    }

    setIcon(Permission, style()->icon(state.editable ? Style::IconRole::Unlock : Style::IconRole::Lock));
    QString permissionTooltip = state.editable ? tr("Layer is editable. Click to lock it.")
                                               : tr("Layer is locked. Click to allow editing.");
    if (state.inheritedMuted)
        permissionTooltip += tr("\nPermission changes are unavailable while an ancestor layer is muted.");
    setToolTip(Permission, permissionTooltip);
}

QString
LayerItem::identifier() const
{
    return p->d.state.identifier;
}

QString
LayerItem::parentIdentifier() const
{
    return p->d.state.parentIdentifier;
}

bool
LayerItem::isInheritedMuted() const
{
    return p->d.state.inheritedMuted;
}

void
LayerItem::setIdentifier(const QString& identifier)
{
    if (p->d.state.identifier == identifier)
        return;

    p->d.state.identifier = identifier;
    setData(Name, roles::layer::identifier, identifier);
}

int
LayerItem::countPrimSpecs(const SdfPrimSpecHandle& spec)
{
    if (!spec)
        return 0;

    int count = 1;
    for (const SdfPrimSpecHandle& child : spec->GetNameChildren())
        count += countPrimSpecs(child);
    return count;
}

void
LayerItem::appendComposedPrimPaths(const SdfPrimSpecHandle& spec, const UsdStageRefPtr& stage, QList<SdfPath>& paths)
{
    if (!spec || !stage)
        return;

    const SdfPath path = spec->GetPath();
    if (path.IsPrimPath() && stage->GetPrimAtPath(path).IsValid())
        paths.append(path);
    for (const SdfPrimSpecHandle& child : spec->GetNameChildren())
        appendComposedPrimPaths(child, stage, paths);
}

TreeItem::ItemStates
LayerItem::itemStates() const
{
    return p->d.state.muted ? ReadOnly : Visible;
}

}  // namespace stageviz
