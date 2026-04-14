// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#include "propertytree.h"
#include "notice.h"
#include "propertyitem.h"
#include "qtutils.h"
#include "selectionlist.h"
#include "signalguard.h"
#include "viewcontext.h"
#include <QFileInfo>
#include <QHeaderView>
#include <QKeyEvent>
#include <QPointer>
#include <pxr/base/gf/bbox3d.h>
#include <pxr/base/gf/matrix4d.h>
#include <pxr/base/gf/range3d.h>
#include <pxr/base/gf/vec3d.h>
#include <pxr/base/tf/token.h>
#include <pxr/base/vt/value.h>
#include <pxr/usd/usd/attribute.h>
#include <pxr/usd/usd/modelAPI.h>
#include <pxr/usd/usd/prim.h>
#include <pxr/usd/usd/stage.h>
#include <pxr/usd/usdGeom/bboxCache.h>
#include <pxr/usd/usdGeom/imageable.h>
#include <pxr/usd/usdGeom/metrics.h>
#include <pxr/usd/usdGeom/tokens.h>
#include <pxr/usd/usdGeom/xformable.h>

PXR_NAMESPACE_USING_DIRECTIVE

namespace stageviz {
class PropertyTreePrivate : public QObject, public SignalGuard {
public:
    void init();
    void close();
    void updateStage(UsdStageRefPtr stage);
    void updatePrims(const NoticeBatch& batch);
    void updateSelection(const QList<SdfPath>& paths);

public:
    std::string matrixString(const GfMatrix4d& matrix);

    struct Data {
        UsdStageRefPtr stage;
        SdfPath path;
        QPointer<ViewContext> context;
        QPointer<PropertyTree> tree;
    };
    Data d;
};

void
PropertyTreePrivate::init()
{
    attach(d.tree);
}

void
PropertyTreePrivate::close()
{
    d.stage = nullptr;
    d.path = SdfPath();
    d.context = nullptr;
    d.tree->clear();
}

void
PropertyTreePrivate::updateStage(UsdStageRefPtr stage)
{
    SignalGuard::Scope guard(this);

    close();
    d.stage = stage;
    if (!stage)
        return;

    PropertyItem* stageItem = new PropertyItem(d.tree.data());
    stageItem->setText(PropertyItem::Name, "Stage");
    d.tree->addTopLevelItem(stageItem);
    stageItem->setExpanded(true);

    auto addChild = [&](const QString& name, const QString& value) {
        PropertyItem* item = new PropertyItem(stageItem);
        item->setText(PropertyItem::Name, name);
        item->setText(PropertyItem::Value, value);
    };

    addChild("metersPerUnit", QString::number(UsdGeomGetStageMetersPerUnit(stage)));
    addChild("upAxis", StringToQString(UsdGeomGetStageUpAxis(stage).GetString()));
    addChild("timeCodesPerSecond", QString::number(stage->GetTimeCodesPerSecond()));
    addChild("startTimeCode", QString::number(stage->GetStartTimeCode()));
    addChild("endTimeCode", QString::number(stage->GetEndTimeCode()));

    const std::string comment = stage->GetRootLayer()->GetComment();
    if (!comment.empty())
        addChild("comment", StringToQString(comment));

    const std::string filePath = stage->GetRootLayer()->GetRealPath();
    addChild("filePath", QFileInfo(StringToQString(filePath)).fileName());
}

void
PropertyTreePrivate::updatePrims(const NoticeBatch& batch)
{
    SignalGuard::Scope guard(this);

    if (d.path.IsEmpty() || batch.entries.isEmpty())
        return;

    for (const NoticeEntry& entry : batch.entries) {
        if (entry.path.IsEmpty())
            continue;

        const SdfPath entryPath = entry.path.IsPropertyPath() ? entry.path.GetPrimPath() : entry.path;

        if (entry.changedInfoOnly) {
            if (entryPath == d.path) {
                updateSelection({ d.path });
                return;
            }
            continue;
        }

        if (entry.resolvedAssetPathsResynced) {
            if (d.path == entryPath || d.path.HasPrefix(entryPath)) {
                updateSelection({ d.path });
                return;
            }
            continue;
        }

        switch (entry.primResyncType) {
        case UsdNotice::ObjectsChanged::PrimResyncType::RenameDestination:
        case UsdNotice::ObjectsChanged::PrimResyncType::ReparentDestination:
        case UsdNotice::ObjectsChanged::PrimResyncType::RenameAndReparentDestination:
            if (!entry.associatedPath.IsEmpty() && d.path == entry.associatedPath) {
                updateSelection({ entryPath });
                return;
            }
            if (d.path == entryPath || d.path.HasPrefix(entryPath)) {
                updateSelection({ d.path });
                return;
            }
            break;

        case UsdNotice::ObjectsChanged::PrimResyncType::RenameSource:
        case UsdNotice::ObjectsChanged::PrimResyncType::ReparentSource:
        case UsdNotice::ObjectsChanged::PrimResyncType::RenameAndReparentSource:
            if (d.path == entryPath || d.path.HasPrefix(entryPath)) {
                if (!entry.associatedPath.IsEmpty()) {
                    updateSelection({ entry.associatedPath });
                    return;
                }
                updateSelection({});
                return;
            }
            break;

        case UsdNotice::ObjectsChanged::PrimResyncType::Delete:
            if (d.path == entryPath || d.path.HasPrefix(entryPath)) {
                updateSelection({});
                return;
            }
            break;

        case UsdNotice::ObjectsChanged::PrimResyncType::UnchangedPrimStack:
        case UsdNotice::ObjectsChanged::PrimResyncType::Other:
        case UsdNotice::ObjectsChanged::PrimResyncType::Invalid:
        default:
            if (d.path == entryPath || d.path.HasPrefix(entryPath)) {
                updateSelection({ d.path });
                return;
            }
            break;
        }
    }
}

void
PropertyTreePrivate::updateSelection(const QList<SdfPath>& paths)
{
    SignalGuard::Scope guard(this);
    d.tree->clear();

    if (!paths.isEmpty()) {
        if (paths.size() > 1) {
            PropertyItem* multiItem = new PropertyItem(d.tree.data());
            multiItem->setText(PropertyItem::Name, "[Multiple selection]");
            d.tree->addTopLevelItem(multiItem);
            multiItem->setExpanded(true);
            d.path = SdfPath();
            return;
        }

        const SdfPath path = paths.first();
        if (!d.stage)
            return;

        const UsdPrim prim = d.stage->GetPrimAtPath(path);
        if (!prim)
            return;

        PropertyItem* primItem = new PropertyItem(d.tree.data());
        primItem->setText(PropertyItem::Name, StringToQString(path.GetString()));
        primItem->setExpanded(true);
        d.tree->addTopLevelItem(primItem);

        auto addChild = [&](const QString& name, const QString& value) {
            PropertyItem* item = new PropertyItem(primItem);
            item->setText(PropertyItem::Name, name);
            item->setText(PropertyItem::Value, value);
        };

        for (const UsdAttribute& attr : prim.GetAttributes()) {
            const std::string name = attr.GetName().GetString();
            VtValue value;
            if (attr.Get(&value))
                addChild(StringToQString(name), StringToQString(value.GetTypeName()));
        }

        d.tree->expandAll();
        d.path = path;
        return;
    }

    d.path = SdfPath();
    if (d.stage)
        updateStage(d.stage);
}

std::string
PropertyTreePrivate::matrixString(const GfMatrix4d& m)
{
    std::ostringstream ss;
    ss << "(";
    for (int r = 0; r < 4; ++r) {
        ss << "(";
        for (int c = 0; c < 4; ++c) {
            ss << m[r][c];
            if (c < 3)
                ss << ", ";
        }
        ss << ")";
        if (r < 3)
            ss << ", ";
    }
    ss << ")";
    return ss.str();
}

PropertyTree::PropertyTree(QWidget* parent)
    : TreeWidget(parent)
    , p(new PropertyTreePrivate())
{
    p->d.tree = this;
    p->init();
}

PropertyTree::~PropertyTree() = default;

ViewContext*
PropertyTree::context() const
{
    return p->d.context;
}

void
PropertyTree::setContext(ViewContext* context)
{
    if (p->d.context != context)
        p->d.context = context;
}

void
PropertyTree::close()
{
    p->close();
}

void
PropertyTree::updateStage(UsdStageRefPtr stage)
{
    p->updateStage(stage);
}

void
PropertyTree::updatePrims(const NoticeBatch& batch)
{
    p->updatePrims(batch);
}

void
PropertyTree::updateSelection(const QList<SdfPath>& paths)
{
    p->updateSelection(paths);
}

}  // namespace stageviz
