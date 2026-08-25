// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#include "stagetree.h"
#include "application.h"
#include "command.h"
#include "contextmenu.h"
#include "mime.h"
#include "primitem.h"
#include "qtutils.h"
#include "selectionlist.h"
#include "signalguard.h"
#include "style.h"
#include "tracelocks.h"
#include "usdutils.h"
#include "viewcontext.h"
#include <QApplication>
#include <QContextMenuEvent>
#include <QDrag>
#include <QHeaderView>
#include <QKeyEvent>
#include <QMimeData>
#include <QPainter>
#include <QPointer>
#include <QStyledItemDelegate>
#include <QTimer>
#include <functional>
#include <pxr/usd/usd/prim.h>

PXR_NAMESPACE_USING_DIRECTIVE

namespace stageviz {

class StageTreePrivate : public QObject, public SignalGuard {
public:
    StageTreePrivate();
    void init();
    void initContext();
    void initTree();
    void close();
    void collapse();
    void expand();
    void expandDepth(int targetDepth, const SdfPath& path);
    int maxDepth(const SdfPath& path) const;
    int depth(const SdfPath& path) const;
    void toggleVisible(PrimItem* item);
    void updateFilter();
    void itemCheckState(QTreeWidgetItem* item, bool checkable, bool recursive = false);
    void treeCheckState(QTreeWidgetItem* item);
    void contextMenuEvent(QContextMenuEvent* event);
    void updateStage(UsdStageRefPtr stage);
    void updatePrims(const NoticeBatch& batch);
    void updateSelection(const QList<SdfPath>& paths);
    SelectionList* selectionList() const;

public Q_SLOTS:
    void itemSelectionChanged();
    void checkStateChanged(PrimItem* item);
    void nameChanged(PrimItem* item);

public:
    void updatePrim(const SdfPath& path);
    void invalidatePrim(const SdfPath& path);
    void invalidateSubtree(PrimItem* item, const UsdPrim& prim);
    void invalidateChildren(PrimItem* parentItem, const UsdPrim& prim);
    bool remapSubtreePaths(const SdfPath& fromPath, const SdfPath& toPath);
    void refreshParentBranch(const SdfPath& path);
    PrimItem* addItem(PrimItem* parent, const SdfPath& parentPath);
    void addChildren(PrimItem* parent, const SdfPath& parentPath);
    int parentDepth(const SdfPath& path) const;
    PrimItem* itemFromPath(const SdfPath& path) const;
    void clearDropIndicator();
    void setDropIndicator(QTreeWidgetItem* item, int mode);
    bool maskContainsCurrentSelection(const QList<SdfPath>& maskPaths, const QList<SdfPath>& selectedPaths) const;
    bool isRenameOrReparentSource(UsdNotice::ObjectsChanged::PrimResyncType type) const;
    bool isRenameOrReparentDestination(UsdNotice::ObjectsChanged::PrimResyncType type) const;
    bool isAncestorOrSelf(const SdfPath& ancestor, const SdfPath& path) const;
    void syncDirectChildrenOnly(PrimItem* parentItem, const UsdPrim& parentPrim);

public:
    enum DropMode { DropNone = 0, DropAboveItem = 1, DropOnItem = 2, DropBelowItem = 3 };
    struct Data {
        int pending = 0;
        bool payloadEnabled = false;
        QString filter;
        QList<SdfPath> loadPaths;
        QList<SdfPath> unloadPaths;
        QList<SdfPath> maskPaths;
        UsdStageRefPtr stage;
        QPointer<ViewContext> context;
        QPointer<StageTree> tree;
    };
    Data d;
};

StageTreePrivate::StageTreePrivate() {}

void
StageTreePrivate::init()
{
    attach(d.tree);
    attach(d.tree->selectionModel());

    const int size = style()->iconSize(Style::UIScale::Small);
    d.tree->setIconSize(QSize(size, size));

    d.tree->setDragEnabled(true);
    d.tree->setAcceptDrops(true);
    d.tree->viewport()->setAcceptDrops(true);
    d.tree->setDropIndicatorShown(false);
    d.tree->setDragDropMode(QAbstractItemView::DragDrop);
    d.tree->setDefaultDropAction(Qt::MoveAction);
    d.tree->setColumnSelectable(PrimItem::Name, true);
    d.tree->setColumnSelectable(PrimItem::Visibility, false);
    d.tree->setSelectionMode(QAbstractItemView::ExtendedSelection);
    d.tree->setSelectionBehavior(QAbstractItemView::SelectRows);

    connect(d.tree.data(), &StageTree::itemSelectionChanged, this, &StageTreePrivate::itemSelectionChanged);
    connect(d.tree.data(), &StageTree::itemChanged, this, [this](QTreeWidgetItem* item, int column) {
        if (column == PrimItem::Name) {
            PrimItem* primItem = static_cast<PrimItem*>(item);
            checkStateChanged(primItem);
            nameChanged(primItem);
        }
    });
}

void
StageTreePrivate::initContext()
{
    if (!d.context)
        return;

    SelectionList* list = selectionList();
    if (!list)
        return;

    connect(list, &SelectionList::selectionChanged, this, &StageTreePrivate::updateSelection);
    updateSelection(list->paths());
}

SelectionList*
StageTreePrivate::selectionList() const
{
    return d.context ? d.context->selectionList() : nullptr;
}

void
StageTreePrivate::itemCheckState(QTreeWidgetItem* item, bool checkable, bool recursive)
{
    Qt::ItemFlags flags = item->flags();
    if (checkable) {
        flags |= Qt::ItemIsUserCheckable | Qt::ItemIsEnabled;
        flags |= Qt::ItemIsAutoTristate;
        item->setFlags(flags);
        if (item->data(0, Qt::CheckStateRole).isNull())
            item->setCheckState(0, Qt::Unchecked);
    }
    else {
        flags &= ~(Qt::ItemIsUserCheckable | Qt::ItemIsAutoTristate);
        item->setFlags(flags);
        item->setData(0, Qt::CheckStateRole, QVariant());
    }

    if (recursive) {
        for (int i = 0; i < item->childCount(); ++i)
            itemCheckState(item->child(i), checkable, recursive);
    }
}

void
StageTreePrivate::treeCheckState(QTreeWidgetItem* item)
{
    itemCheckState(item, true, true);
}

void
StageTreePrivate::initTree()
{
    const int topLevelCount = d.tree->topLevelItemCount();
    for (int i = 0; i < topLevelCount; ++i)
        d.tree->expandItem(d.tree->topLevelItem(i));
}

void
StageTreePrivate::clearDropIndicator()
{
    if (!d.tree)
        return;

    d.tree->setProperty(mime::dropItemPtrProperty, QVariant::fromValue<qulonglong>(0));
    d.tree->setProperty(mime::dropModeProperty, DropNone);

    if (d.tree->updatesEnabled())
        d.tree->update();
}

void
StageTreePrivate::setDropIndicator(QTreeWidgetItem* item, int mode)
{
    if (!d.tree)
        return;

    const qulonglong ptr = reinterpret_cast<qulonglong>(item);
    d.tree->setProperty(mime::dropItemPtrProperty, QVariant::fromValue(ptr));
    d.tree->setProperty(mime::dropModeProperty, mode);

    if (d.tree->updatesEnabled())
        d.tree->update();
}

bool
StageTreePrivate::maskContainsCurrentSelection(const QList<SdfPath>& maskPaths,
                                               const QList<SdfPath>& selectedPaths) const
{
    if (maskPaths.isEmpty() || selectedPaths.isEmpty())
        return false;

    for (const SdfPath& selectedPath : selectedPaths) {
        bool found = false;
        for (const SdfPath& maskPath : maskPaths) {
            if (selectedPath == maskPath) {
                found = true;
                break;
            }
        }
        if (!found)
            return false;
    }

    return true;
}

bool
StageTreePrivate::isRenameOrReparentSource(UsdNotice::ObjectsChanged::PrimResyncType type) const
{
    using Type = UsdNotice::ObjectsChanged::PrimResyncType;
    switch (type) {
    case Type::RenameSource:
    case Type::ReparentSource:
    case Type::RenameAndReparentSource: return true;
    default: return false;
    }
}

bool
StageTreePrivate::isRenameOrReparentDestination(UsdNotice::ObjectsChanged::PrimResyncType type) const
{
    using Type = UsdNotice::ObjectsChanged::PrimResyncType;
    switch (type) {
    case Type::RenameDestination:
    case Type::ReparentDestination:
    case Type::RenameAndReparentDestination: return true;
    default: return false;
    }
}

bool
StageTreePrivate::isAncestorOrSelf(const SdfPath& ancestor, const SdfPath& path) const
{
    if (ancestor.IsEmpty() || path.IsEmpty())
        return false;
    return ancestor == path || path.HasPrefix(ancestor);
}

void
StageTreePrivate::close()
{
    QSignalBlocker blocker(d.tree);
    d.stage = nullptr;
    d.tree->clear();
    clearDropIndicator();
}

void
StageTreePrivate::collapse()
{
    std::function<void(QTreeWidgetItem*)> collapseItems = [&](QTreeWidgetItem* item) {
        item->setExpanded(false);
        for (int i = 0; i < item->childCount(); ++i)
            collapseItems(item->child(i));
    };

    for (int i = 0; i < d.tree->topLevelItemCount(); ++i)
        collapseItems(d.tree->topLevelItem(i));

    initTree();
}

void
StageTreePrivate::expand()
{
    const QList<QTreeWidgetItem*> selected = d.tree->selectedItems();
    for (QTreeWidgetItem* item : selected) {
        QTreeWidgetItem* parent = item->parent();
        while (parent) {
            parent->setExpanded(true);
            parent = parent->parent();
        }
    }

    if (!selected.isEmpty()) {
        QTreeWidgetItem* item = selected.first();
        const QRect itemRect = d.tree->visualItemRect(item);
        const QRect viewRect = d.tree->viewport()->rect();
        if (!viewRect.intersects(itemRect))
            d.tree->scrollToItem(item, QAbstractItemView::PositionAtCenter);
    }
}

void
StageTreePrivate::expandDepth(int targetDepth, const SdfPath& path)
{
    if (d.tree->topLevelItemCount() == 0)
        return;

    d.tree->setUpdatesEnabled(false);

    if (path.IsEmpty()) {
        std::function<void(QTreeWidgetItem*, int)> expandNode = [&](QTreeWidgetItem* item, int depthValue) {
            item->setExpanded(depthValue <= targetDepth);
            for (int i = 0; i < item->childCount(); ++i)
                expandNode(item->child(i), depthValue + 1);
        };
        for (int i = 0; i < d.tree->topLevelItemCount(); ++i) {
            expandNode(d.tree->topLevelItem(i), 0);
        }
    }
    else {
        QTreeWidgetItem* item = itemFromPath(path);
        if (!item) {
            d.tree->setUpdatesEnabled(true);
            return;
        }

        const int selectedDepth = depth(path);
        QTreeWidgetItem* parent = item->parent();
        int parentDepthValue = selectedDepth - 1;
        while (parent) {
            parent->setExpanded(parentDepthValue <= targetDepth);
            parent = parent->parent();
            parentDepthValue--;
        }

        std::function<void(QTreeWidgetItem*, int)> expandNode = [&](QTreeWidgetItem* node, int depthValue) {
            node->setExpanded(depthValue <= targetDepth);
            for (int i = 0; i < node->childCount(); ++i)
                expandNode(node->child(i), depthValue + 1);
        };
        expandNode(item, selectedDepth);
    }

    d.tree->setUpdatesEnabled(true);
}

int
StageTreePrivate::maxDepth(const SdfPath& path) const
{
    if (d.tree->topLevelItemCount() == 0)
        return 0;

    std::function<int(QTreeWidgetItem*, int)> subtreeDepth = [&](QTreeWidgetItem* item, int currentDepth) {
        int max = currentDepth;
        for (int i = 0; i < item->childCount(); ++i)
            max = std::max(max, subtreeDepth(item->child(i), currentDepth + 1));
        return max;
    };

    if (path.IsEmpty()) {
        int max = 0;
        for (int i = 0; i < d.tree->topLevelItemCount(); ++i)
            max = std::max(max, subtreeDepth(d.tree->topLevelItem(i), 0));
        return max;
    }

    QTreeWidgetItem* item = itemFromPath(path);
    if (!item) {
        int max = 0;
        for (int i = 0; i < d.tree->topLevelItemCount(); ++i)
            max = std::max(max, subtreeDepth(d.tree->topLevelItem(i), 0));
        return max;
    }

    const int parentDepthValue = depth(path);
    const int childDepth = subtreeDepth(item, 0);
    return parentDepthValue + childDepth;
}

int
StageTreePrivate::depth(const SdfPath& path) const
{
    if (path.IsEmpty())
        return 0;

    QTreeWidgetItem* item = itemFromPath(path);
    if (!item)
        return 0;

    int depthValue = 0;
    while (item->parent()) {
        depthValue++;
        item = item->parent();
    }
    return depthValue;
}

PrimItem*
StageTreePrivate::addItem(PrimItem* parent, const SdfPath& path)
{
    UsdStageRefPtr stage;
    bool isPayload = false;
    bool isLoaded = false;

    {
        READ_LOCKER(locker, d.context->stageLock(), "stageLock");
        stage = d.stage;
        if (!stage)
            return nullptr;

        isPayload = d.payloadEnabled && stage::isPayload(stage, path);
        if (isPayload)
            isLoaded = stage->GetPrimAtPath(path).IsLoaded();
    }

    PrimItem* item = new PrimItem(parent, stage, path);
    item->invalidate();

    Qt::ItemFlags flags = item->flags();
    flags |= Qt::ItemIsDragEnabled | Qt::ItemIsDropEnabled;
    item->setFlags(flags);

    itemCheckState(item, d.payloadEnabled);
    parent->addChild(item);

    if (isPayload) {
        item->setCheckState(0, isLoaded ? Qt::Checked : Qt::Unchecked);
        return item;
    }

    addChildren(item, path);
    return item;
}

void
StageTreePrivate::addChildren(PrimItem* parent, const SdfPath& path)
{
    QList<SdfPath> childPaths;
    {
        READ_LOCKER(locker, d.context->stageLock(), "stageLock");
        if (!d.stage)
            return;

        const UsdPrim prim = d.stage->GetPrimAtPath(path);
        if (!prim)
            return;

        for (const UsdPrim& child : prim.GetAllChildren())
            childPaths.append(child.GetPath());
    }

    for (const SdfPath& childPath : childPaths)
        addItem(parent, childPath);
}

void
StageTreePrivate::toggleVisible(PrimItem* item)
{
    const SdfPath path(QStringToString(item->data(0, PrimItem::Path).toString()));
    bool visible = false;

    {
        READ_LOCKER(locker, d.context->stageLock(), "stageLock");
        if (!d.stage)
            return;
        visible = stage::isVisible(d.stage, path);
    }

    if (visible)
        d.context->run(new Command(hidePaths(QList<SdfPath> { path }, false)));
    else
        d.context->run(new Command(showPaths(QList<SdfPath> { path }, false)));
}

void
StageTreePrivate::updateFilter()
{
    std::function<bool(QTreeWidgetItem*)> matchFilter = [&](QTreeWidgetItem* item) -> bool {
        bool matches = false;
        for (int col = 0; col < d.tree->columnCount(); ++col) {
            if (item->text(col).contains(d.filter, Qt::CaseInsensitive)) {
                matches = true;
                break;
            }
        }

        bool childMatches = false;
        for (int i = 0; i < item->childCount(); ++i) {
            if (matchFilter(item->child(i)))
                childMatches = true;
        }

        const bool visible = matches || childMatches;
        item->setHidden(!visible);
        return visible;
    };
    for (int i = 0; i < d.tree->topLevelItemCount(); ++i)
        matchFilter(d.tree->topLevelItem(i));
}

void
StageTreePrivate::itemSelectionChanged()
{
    QList<SdfPath> paths;
    for (QTreeWidgetItem* baseItem : d.tree->selectedItems()) {
        PrimItem* item = static_cast<PrimItem*>(baseItem);
        const QString pathString = item->data(0, PrimItem::Path).toString();
        if (!pathString.isEmpty())
            paths.append(SdfPath(QStringToString(pathString)));
    }
    d.context->run(new Command(selectPaths(paths)));
}

void
StageTreePrivate::checkStateChanged(PrimItem* item)
{
    PrimItem* primItem = static_cast<PrimItem*>(item);
    const QString pathString = primItem->data(0, PrimItem::Path).toString();
    if (pathString.isEmpty())
        return;

    const SdfPath path(QStringToString(pathString));

    bool hasPayload = false;
    bool isLoaded = false;
    {
        READ_LOCKER(locker, d.context->stageLock(), "stageLock");
        if (!d.stage)
            return;

        const UsdPrim prim = d.stage->GetPrimAtPath(path);
        if (!prim)
            return;

        if (!stage::isPayload(d.stage, path))
            return;

        hasPayload = true;
        isLoaded = prim.IsLoaded();
    }

    if (!hasPayload)
        return;

    const Qt::CheckState state = item->checkState(PrimItem::Name);
    if ((state == Qt::Checked && isLoaded) || (state == Qt::Unchecked && !isLoaded))
        return;

    if (state == Qt::Checked)
        d.loadPaths.append(path);
    else if (state == Qt::Unchecked)
        d.unloadPaths.append(path);

    d.pending++;
    QTimer::singleShot(0, d.tree, [this]() {
        if (--d.pending <= 0) {
            if (!d.loadPaths.isEmpty()) {
                d.context->run(new Command(loadPayloads(d.loadPaths)));
                d.loadPaths.clear();
            }
            if (!d.unloadPaths.isEmpty()) {
                d.context->run(new Command(unloadPayloads(d.unloadPaths)));
                d.unloadPaths.clear();
            }
            d.pending = 0;
        }
    });
}

void
StageTreePrivate::nameChanged(PrimItem* item)
{
    const SdfPath oldPath(QStringToString(item->data(0, PrimItem::Path).toString()));
    if (oldPath.IsEmpty())
        return;

    const QString newName = item->data(PrimItem::Name, PrimItem::EditName).toString().trimmed();
    if (newName.isEmpty())
        return;

    const QString oldName = qt::StringToQString(oldPath.GetName());
    if (newName == oldName) {
        item->setData(PrimItem::Name, PrimItem::EditName, QString());
        return;
    }

    d.context->run(new Command(renamePath(oldPath, newName)));
}

void
StageTreePrivate::contextMenuEvent(QContextMenuEvent* event)
{
    if (!d.tree || !event || !d.context)
        return;

    SelectionList* list = selectionList();
    if (!list)
        return;

    QTreeWidgetItem* clickedItem = d.tree->itemAt(event->pos());
    QList<SdfPath> paths = list->paths();

    if (clickedItem && !clickedItem->isSelected()) {
        auto* item = static_cast<PrimItem*>(clickedItem);
        const QString pathString = item->data(0, PrimItem::Path).toString();
        const SdfPath clickedPath = pathString.isEmpty() ? SdfPath() : SdfPath(qt::QStringToString(pathString));

        if (!clickedPath.IsEmpty()) {
            paths = { clickedPath };

            {
                SignalGuard::Scope guard(this);
                d.tree->clearSelection();
                clickedItem->setSelected(true);
                d.tree->setCurrentItem(clickedItem);
            }

            d.context->run(new Command(selectPaths(paths)));
        }
    }

    SdfPath createParentPath = SdfPath::AbsoluteRootPath();
    if (clickedItem) {
        if (paths.size() == 1)
            createParentPath = paths.first();
        else if (paths.size() > 1)
            createParentPath = paths.first().GetParentPath();
    }

    ContextMenu::exec(d.tree, d.context, d.stage, d.tree->mapToGlobal(event->pos()), paths, d.maskPaths,
                      createParentPath, d.payloadEnabled);
}

void
StageTreePrivate::updateStage(UsdStageRefPtr stage)
{
    SignalGuard::Scope guard(this);
    close();
    d.stage = stage;
    if (!stage)
        return;

    UsdPrim prim;
    {
        READ_LOCKER(locker, d.context->stageLock(), "stageLock");
        prim = stage->GetPseudoRoot();
    }

    PrimItem* rootItem = new PrimItem(d.tree.data(), stage, prim.GetPath());

    Qt::ItemFlags flags = rootItem->flags();
    flags |= Qt::ItemIsDropEnabled;
    rootItem->setFlags(flags);

    itemCheckState(rootItem, false, false);
    addChildren(rootItem, prim.GetPath());
    initTree();

    if (d.payloadEnabled)
        treeCheckState(rootItem);

    if (SelectionList* list = selectionList())
        updateSelection(list->paths());
}

void
StageTree::updateEditLayer()
{
    std::function<void(QTreeWidgetItem*)> invalidate = [&](QTreeWidgetItem* baseItem) {
        if (!baseItem)
            return;

        if (auto* item = dynamic_cast<PrimItem*>(baseItem))
            item->invalidate();

        for (int i = 0; i < baseItem->childCount(); ++i)
            invalidate(baseItem->child(i));
    };

    for (int i = 0; i < topLevelItemCount(); ++i)
        invalidate(topLevelItem(i));

    viewport()->update();
}

void
StageTreePrivate::syncDirectChildrenOnly(PrimItem* parentItem, const UsdPrim& parentPrim)
{
    if (!parentItem || !parentPrim)
        return;

    bool isPayload = false;
    bool isLoaded = false;

    {
        READ_LOCKER(locker, d.context->stageLock(), "stageLock");
        if (!d.stage)
            return;

        if (d.payloadEnabled) {
            isPayload = stage::isPayload(d.stage, parentPrim.GetPath());
            isLoaded = parentPrim.IsLoaded();
        }
    }

    if (isPayload) {
        parentItem->invalidate();
        itemCheckState(parentItem, true, false);

        const Qt::CheckState want = isLoaded ? Qt::Checked : Qt::Unchecked;
        if (parentItem->checkState(PrimItem::Name) != want)
            parentItem->setCheckState(PrimItem::Name, want);

        while (parentItem->childCount() > 0)
            delete parentItem->child(0);

        return;
    }

    QList<SdfPath> ordered;
    QSet<QString> stageSet;

    {
        READ_LOCKER(locker, d.context->stageLock(), "stageLock");
        if (!d.stage)
            return;

        for (const UsdPrim& childPrim : parentPrim.GetAllChildren()) {
            const SdfPath childPath = childPrim.GetPath();
            ordered.append(childPath);
            stageSet.insert(qt::SdfPathToQString(childPath));
        }
    }

    QHash<QString, PrimItem*> existing;
    existing.reserve(parentItem->childCount());

    for (int i = 0; i < parentItem->childCount(); ++i) {
        auto* child = static_cast<PrimItem*>(parentItem->child(i));
        if (!child)
            continue;
        existing.insert(child->data(0, PrimItem::Path).toString(), child);
    }

    for (auto it = existing.begin(); it != existing.end(); ++it) {
        if (!stageSet.contains(it.key()))
            delete it.value();
    }

    existing.clear();
    existing.reserve(parentItem->childCount());

    for (int i = 0; i < parentItem->childCount(); ++i) {
        auto* child = static_cast<PrimItem*>(parentItem->child(i));
        if (!child)
            continue;
        existing.insert(child->data(0, PrimItem::Path).toString(), child);
    }

    for (const SdfPath& childPath : ordered) {
        const QString key = qt::SdfPathToQString(childPath);
        if (!existing.contains(key)) {
            PrimItem* item = addItem(parentItem, childPath);
            if (item)
                item->invalidate();
        }
    }

    existing.clear();
    existing.reserve(parentItem->childCount());

    for (int i = 0; i < parentItem->childCount(); ++i) {
        auto* child = static_cast<PrimItem*>(parentItem->child(i));
        if (!child)
            continue;
        existing.insert(child->data(0, PrimItem::Path).toString(), child);
    }

    for (int i = 0; i < ordered.size(); ++i) {
        const QString key = qt::SdfPathToQString(ordered[i]);
        PrimItem* childItem = existing.value(key, nullptr);
        if (!childItem)
            continue;

        if (parentItem->child(i) != childItem) {
            parentItem->removeChild(childItem);
            parentItem->insertChild(i, childItem);
        }
    }

    for (int i = 0; i < parentItem->childCount(); ++i) {
        auto* childItem = static_cast<PrimItem*>(parentItem->child(i));
        if (!childItem)
            continue;

        childItem->invalidate();

        const QString childPathString = childItem->data(0, PrimItem::Path).toString();
        if (childPathString.isEmpty())
            continue;

        const SdfPath childPath(QStringToString(childPathString));

        bool isPayload = false;
        bool isLoaded = false;
        {
            READ_LOCKER(locker, d.context->stageLock(), "stageLock");
            if (!d.stage)
                return;

            const UsdPrim childPrim = d.stage->GetPrimAtPath(childPath);
            if (!childPrim)
                continue;

            if (d.payloadEnabled) {
                isPayload = stage::isPayload(d.stage, childPath);
                isLoaded = childPrim.IsLoaded();
            }
        }

        itemCheckState(childItem, d.payloadEnabled, false);

        if (isPayload) {
            const Qt::CheckState want = isLoaded ? Qt::Checked : Qt::Unchecked;
            if (childItem->checkState(0) != want)
                childItem->setCheckState(0, want);
        }
    }

    parentItem->invalidate();
}

void
StageTreePrivate::updatePrims(const NoticeBatch& batch)
{
    SignalGuard::Scope guard(this);
    if (!d.stage || !d.tree || batch.entries.isEmpty())
        return;

    d.tree->setUpdatesEnabled(false);

    QSet<SdfPath> handledPaths;
    QSet<SdfPath> handledParents;

    bool hasSpecificPath = false;
    for (const NoticeEntry& entry : batch.entries) {
        if (!entry.path.IsEmpty() && entry.path != SdfPath::AbsoluteRootPath()) {
            hasSpecificPath = true;
            break;
        }
    }

    for (const NoticeEntry& entry : batch.entries) {
        if (entry.path.IsEmpty())
            continue;

        if (!isRenameOrReparentDestination(entry.primResyncType))
            continue;

        const SdfPath oldPath = entry.associatedPath;
        const SdfPath newPath = entry.path;

        if (oldPath.IsEmpty() || newPath.IsEmpty())
            continue;

        const bool remapped = remapSubtreePaths(oldPath, newPath);
        if (!remapped)
            continue;

        handledPaths.insert(oldPath);
        handledPaths.insert(newPath);

        const SdfPath oldParent = oldPath.GetParentPath();
        const SdfPath newParent = newPath.GetParentPath();

        if (!oldParent.IsEmpty())
            handledParents.insert(oldParent);

        if (!newParent.IsEmpty())
            handledParents.insert(newParent);

        PrimItem* renamedItem = itemFromPath(newPath);
        if (renamedItem)
            renamedItem->invalidate();
    }

    for (const SdfPath& parentPath : handledParents) {
        PrimItem* parentItem = itemFromPath(parentPath);
        if (!parentItem)
            continue;

        UsdPrim parentPrim;
        {
            READ_LOCKER(locker, d.context->stageLock(), "stageLock");
            if (!d.stage)
                continue;

            parentPrim = parentPath == SdfPath::AbsoluteRootPath() ? d.stage->GetPseudoRoot()
                                                                   : d.stage->GetPrimAtPath(parentPath);
        }

        if (!parentPrim)
            continue;

        syncDirectChildrenOnly(parentItem, parentPrim);
    }

    for (const NoticeEntry& entry : batch.entries) {
        if (entry.path.IsEmpty())
            continue;

        if (hasSpecificPath && entry.path == SdfPath::AbsoluteRootPath())
            continue;

        if (handledPaths.contains(entry.path))
            continue;

        if (isRenameOrReparentSource(entry.primResyncType))
            continue;

        bool coveredByHandledParent = false;
        for (const SdfPath& parentPath : handledParents) {
            if (entry.path == parentPath || isAncestorOrSelf(parentPath, entry.path)) {
                coveredByHandledParent = true;
                break;
            }
        }

        if (coveredByHandledParent)
            continue;

        if (entry.changedInfoOnly) {
            updatePrim(entry.path);
            continue;
        }

        if (entry.resolvedAssetPathsResynced) {
            invalidatePrim(entry.path);
            continue;
        }

        switch (entry.primResyncType) {
        case UsdNotice::ObjectsChanged::PrimResyncType::RenameSource:
        case UsdNotice::ObjectsChanged::PrimResyncType::ReparentSource:
        case UsdNotice::ObjectsChanged::PrimResyncType::RenameAndReparentSource:
        case UsdNotice::ObjectsChanged::PrimResyncType::RenameDestination:
        case UsdNotice::ObjectsChanged::PrimResyncType::ReparentDestination:
        case UsdNotice::ObjectsChanged::PrimResyncType::RenameAndReparentDestination: break;

        case UsdNotice::ObjectsChanged::PrimResyncType::Delete:
            invalidatePrim(entry.path);
            refreshParentBranch(entry.path);
            break;

        case UsdNotice::ObjectsChanged::PrimResyncType::UnchangedPrimStack:
            updatePrim(entry.path);
            refreshParentBranch(entry.path);
            break;

        case UsdNotice::ObjectsChanged::PrimResyncType::Other:
        case UsdNotice::ObjectsChanged::PrimResyncType::Invalid:
        default: invalidatePrim(entry.path); break;
        }
    }

    d.tree->setUpdatesEnabled(true);
    d.tree->update();
}

void
StageTreePrivate::updatePrim(const SdfPath& path)
{
    const SdfPath primPath = path.IsPropertyPath() ? path.GetPrimPath() : path;
    PrimItem* primItem = itemFromPath(primPath);
    if (!primItem)
        return;

    UsdPrim prim;
    bool isPayload = false;

    {
        READ_LOCKER(locker, d.context->stageLock(), "stageLock");
        if (!d.stage)
            return;

        prim = d.stage->GetPrimAtPath(primPath);
        if (!prim)
            return;

        if (d.payloadEnabled)
            isPayload = stage::isPayload(d.stage, primPath);
    }

    primItem->invalidate();
    itemCheckState(primItem, d.payloadEnabled, false);

    if (isPayload) {
        const Qt::CheckState want = prim.IsLoaded() ? Qt::Checked : Qt::Unchecked;
        if (primItem->checkState(0) != want)
            primItem->setCheckState(0, want);

        while (primItem->childCount() > 0)
            delete primItem->child(0);
    }
}

void
StageTreePrivate::invalidateSubtree(PrimItem* item, const UsdPrim& prim)
{
    if (!item || !prim)
        return;

    const SdfPath primPath = prim.GetPath();

    bool isPayload = false;
    {
        READ_LOCKER(locker, d.context->stageLock(), "stageLock");
        if (d.stage && d.payloadEnabled)
            isPayload = stage::isPayload(d.stage, primPath);
    }

    item->invalidate();
    itemCheckState(item, d.payloadEnabled, false);

    if (isPayload) {
        const Qt::CheckState want = prim.IsLoaded() ? Qt::Checked : Qt::Unchecked;
        if (item->checkState(0) != want)
            item->setCheckState(0, want);

        while (item->childCount() > 0)
            delete item->child(0);

        return;
    }

    invalidateChildren(item, prim);
}

void
StageTreePrivate::invalidatePrim(const SdfPath& path)
{
    const SdfPath primPath = path.IsPropertyPath() ? path.GetPrimPath() : path;
    const SdfPath parentPath = primPath.GetParentPath();

    UsdPrim prim;
    PrimItem* primItem = itemFromPath(primPath);

    {
        READ_LOCKER(locker, d.context->stageLock(), "stageLock");
        if (!d.stage)
            return;
        prim = d.stage->GetPrimAtPath(primPath);
    }

    if (!prim) {
        PrimItem* parentItem = itemFromPath(parentPath);

        if (primItem)
            delete primItem;

        if (parentItem) {
            UsdPrim parentPrim;
            {
                READ_LOCKER(locker, d.context->stageLock(), "stageLock");
                if (d.stage)
                    parentPrim = d.stage->GetPrimAtPath(parentPath);
            }
            if (parentPrim) {
                invalidateChildren(parentItem, parentPrim);
                parentItem->invalidate();
            }
        }
        return;
    }

    if (!primItem) {
        PrimItem* parentItem = itemFromPath(parentPath);
        if (!parentItem)
            return;

        UsdPrim parentPrim;
        bool parentIsPayload = false;
        bool parentIsLoaded = false;

        {
            READ_LOCKER(locker, d.context->stageLock(), "stageLock");
            if (!d.stage)
                return;

            parentPrim = parentPath == SdfPath::AbsoluteRootPath() ? d.stage->GetPseudoRoot()
                                                                   : d.stage->GetPrimAtPath(parentPath);

            if (d.payloadEnabled && parentPrim) {
                parentIsPayload = stage::isPayload(d.stage, parentPath);
                parentIsLoaded = parentPrim.IsLoaded();
            }
        }

        if (parentIsPayload) {
            parentItem->invalidate();
            itemCheckState(parentItem, true, false);

            const Qt::CheckState want = parentIsLoaded ? Qt::Checked : Qt::Unchecked;
            if (parentItem->checkState(PrimItem::Name) != want)
                parentItem->setCheckState(PrimItem::Name, want);

            while (parentItem->childCount() > 0)
                delete parentItem->child(0);

            return;
        }

        primItem = addItem(parentItem, primPath);
        if (!primItem)
            return;

        invalidateChildren(parentItem, prim.GetParent());
        parentItem->invalidate();
    }

    invalidateSubtree(primItem, prim);
}

void
StageTreePrivate::invalidateChildren(PrimItem* parentItem, const UsdPrim& prim)
{
    if (!parentItem || !prim)
        return;

    bool isPayload = false;
    bool isLoaded = false;

    {
        READ_LOCKER(locker, d.context->stageLock(), "stageLock");
        if (!d.stage)
            return;

        if (d.payloadEnabled) {
            isPayload = stage::isPayload(d.stage, prim.GetPath());
            isLoaded = prim.IsLoaded();
        }
    }

    if (isPayload) {
        parentItem->invalidate();
        itemCheckState(parentItem, true, false);

        const Qt::CheckState want = isLoaded ? Qt::Checked : Qt::Unchecked;
        if (parentItem->checkState(PrimItem::Name) != want)
            parentItem->setCheckState(PrimItem::Name, want);

        while (parentItem->childCount() > 0)
            delete parentItem->child(0);

        return;
    }

    QHash<QString, PrimItem*> existing;
    existing.reserve(parentItem->childCount());

    for (int i = 0; i < parentItem->childCount(); ++i) {
        auto* child = static_cast<PrimItem*>(parentItem->child(i));
        if (!child)
            continue;
        existing.insert(child->data(0, PrimItem::Path).toString(), child);
    }

    QList<SdfPath> ordered;
    QSet<QString> stageSet;

    {
        READ_LOCKER(locker, d.context->stageLock(), "stageLock");
        for (const UsdPrim& childPrim : prim.GetAllChildren()) {
            const SdfPath childPath = childPrim.GetPath();
            const QString key = qt::StringToQString(childPath.GetString());
            ordered.append(childPath);
            stageSet.insert(key);
        }
    }

    for (auto it = existing.begin(); it != existing.end(); ++it) {
        if (!stageSet.contains(it.key()))
            delete it.value();
    }

    existing.clear();
    existing.reserve(parentItem->childCount());

    for (int i = 0; i < parentItem->childCount(); ++i) {
        auto* child = static_cast<PrimItem*>(parentItem->child(i));
        if (!child)
            continue;
        existing.insert(child->data(0, PrimItem::Path).toString(), child);
    }

    for (const SdfPath& childPath : ordered) {
        const QString key = qt::StringToQString(childPath.GetString());
        if (!existing.contains(key))
            addItem(parentItem, childPath);
    }

    existing.clear();
    existing.reserve(parentItem->childCount());

    for (int i = 0; i < parentItem->childCount(); ++i) {
        auto* child = static_cast<PrimItem*>(parentItem->child(i));
        if (!child)
            continue;
        existing.insert(child->data(0, PrimItem::Path).toString(), child);
    }

    bool orderMatches = (parentItem->childCount() == ordered.size());
    if (orderMatches) {
        for (int i = 0; i < ordered.size(); ++i) {
            auto* child = static_cast<PrimItem*>(parentItem->child(i));
            const SdfPath treePath = child ? SdfPath(QStringToString(child->data(0, PrimItem::Path).toString()))
                                           : SdfPath();

            if (!child || treePath != ordered[i]) {
                orderMatches = false;
                break;
            }
        }
    }

    if (!orderMatches) {
        for (int i = 0; i < ordered.size(); ++i) {
            const QString key = qt::StringToQString(ordered[i].GetString());
            PrimItem* childItem = existing.value(key, nullptr);
            if (!childItem)
                continue;

            if (parentItem->child(i) != childItem) {
                parentItem->removeChild(childItem);
                parentItem->insertChild(i, childItem);
            }
        }
    }

    for (int i = 0; i < parentItem->childCount(); ++i) {
        auto* childItem = static_cast<PrimItem*>(parentItem->child(i));
        if (!childItem)
            continue;

        const QString childPathString = childItem->data(0, PrimItem::Path).toString();
        if (childPathString.isEmpty())
            continue;

        const SdfPath childPath(QStringToString(childPathString));

        UsdPrim childPrim;
        {
            READ_LOCKER(locker, d.context->stageLock(), "stageLock");
            if (!d.stage)
                return;
            childPrim = d.stage->GetPrimAtPath(childPath);
        }

        if (!childPrim) {
            delete childItem;
            --i;
            continue;
        }

        invalidateSubtree(childItem, childPrim);
    }
}

bool
StageTreePrivate::remapSubtreePaths(const SdfPath& fromPath, const SdfPath& toPath)
{
    PrimItem* rootItem = itemFromPath(fromPath);
    if (!rootItem)
        return false;

    std::function<void(PrimItem*)> remap = [&](PrimItem* item) {
        if (!item)
            return;

        const SdfPath oldItemPath = item->path();
        if (!oldItemPath.IsEmpty() && (oldItemPath == fromPath || oldItemPath.HasPrefix(fromPath))) {
            const SdfPath newItemPath = oldItemPath.ReplacePrefix(fromPath, toPath);
            item->setPath(newItemPath);
        }

        item->invalidate();

        for (int i = 0; i < item->childCount(); ++i)
            remap(static_cast<PrimItem*>(item->child(i)));
    };

    remap(rootItem);
    return true;
}

void
StageTreePrivate::refreshParentBranch(const SdfPath& path)
{
    const SdfPath primPath = path.IsPropertyPath() ? path.GetPrimPath() : path;
    const SdfPath parentPath = primPath.GetParentPath();

    if (parentPath.IsEmpty())
        return;

    PrimItem* parentItem = itemFromPath(parentPath);
    if (!parentItem)
        return;

    UsdPrim parentPrim;
    {
        READ_LOCKER(locker, d.context->stageLock(), "stageLock");
        if (!d.stage)
            return;

        parentPrim = parentPath == SdfPath::AbsoluteRootPath() ? d.stage->GetPseudoRoot()
                                                               : d.stage->GetPrimAtPath(parentPath);
    }

    if (!parentPrim)
        return;

    syncDirectChildrenOnly(parentItem, parentPrim);
    parentItem->invalidate();
}

void
StageTreePrivate::updateSelection(const QList<SdfPath>& paths)
{
    SignalGuard::Scope guard(this);
    const QSet<SdfPath> selectedSet(paths.begin(), paths.end());

    std::function<void(QTreeWidgetItem*)> selectItems = [&](QTreeWidgetItem* baseItem) {
        PrimItem* primItem = static_cast<PrimItem*>(baseItem);
        const QString pathString = primItem->data(0, PrimItem::Path).toString();
        if (!pathString.isEmpty()) {
            const SdfPath itemPath(QStringToString(pathString));
            bool isSelected = selectedSet.contains(itemPath);

            if (!isSelected && d.payloadEnabled && primItem->childCount() == 0) {
                for (const SdfPath& path : selectedSet) {
                    if (path.HasPrefix(itemPath) && path != itemPath) {
                        isSelected = true;
                        break;
                    }
                }
            }

            primItem->setSelected(isSelected);
        }

        for (int i = 0; i < primItem->childCount(); ++i)
            selectItems(primItem->child(i));
    };

    for (int i = 0; i < d.tree->topLevelItemCount(); ++i)
        selectItems(d.tree->topLevelItem(i));

    d.tree->update();
}

PrimItem*
StageTreePrivate::itemFromPath(const SdfPath& path) const
{
    std::function<PrimItem*(PrimItem*)> find = [&](PrimItem* item) -> PrimItem* {
        if (!item)
            return nullptr;

        if (item->path() == path)
            return item;

        for (int i = 0; i < item->childCount(); ++i) {
            if (PrimItem* found = find(static_cast<PrimItem*>(item->child(i))))
                return found;
        }

        return nullptr;
    };

    for (int i = 0; i < d.tree->topLevelItemCount(); ++i) {
        if (PrimItem* found = find(static_cast<PrimItem*>(d.tree->topLevelItem(i))))
            return found;
    }

    return nullptr;
}

StageTree::StageTree(QWidget* parent)
    : TreeWidget(parent)
    , p(new StageTreePrivate())
{
    p->d.tree = this;
    p->init();
}

StageTree::~StageTree() = default;

ViewContext*
StageTree::context() const
{
    return p->d.context;
}

void
StageTree::setContext(ViewContext* context)
{
    if (p->d.context == context)
        return;

    if (p->d.context && p->d.context->selectionList())
        disconnect(p->d.context->selectionList(), nullptr, p.data(), nullptr);

    p->d.context = context;
    p->initContext();
}

void
StageTree::close()
{
    p->close();
}

void
StageTree::collapse()
{
    p->collapse();
}

void
StageTree::expand()
{
    p->expand();
}

void
StageTree::expandDepth(int targetDepth, const SdfPath& path)
{
    p->expandDepth(targetDepth, path);
}

int
StageTree::maxDepth(const SdfPath& path) const
{
    return p->maxDepth(path);
}

int
StageTree::depth(const SdfPath& path) const
{
    return p->depth(path);
}

QString
StageTree::filter() const
{
    return p->d.filter;
}

void
StageTree::setFilter(const QString& filter)
{
    if (filter != p->d.filter) {
        p->d.filter = filter;
        p->updateFilter();
    }
}

bool
StageTree::payloadEnabled() const
{
    return p->d.payloadEnabled;
}

void
StageTree::setPayloadEnabled(bool enabled)
{
    if (p->d.payloadEnabled != enabled)
        p->d.payloadEnabled = enabled;
}

void
StageTree::updateMask(const QList<SdfPath>& paths)
{
    if (p->d.maskPaths != paths)
        p->d.maskPaths = paths;
}

void
StageTree::updateStage(UsdStageRefPtr stage)
{
    p->updateStage(stage);
}

void
StageTree::updatePrims(const NoticeBatch& batch)
{
    p->updatePrims(batch);
}

void
StageTree::contextMenuEvent(QContextMenuEvent* event)
{
    p->contextMenuEvent(event);
}

void
StageTree::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_A && (event->modifiers() & Qt::ControlModifier)) {
        for (int i = 0; i < topLevelItemCount(); ++i)
            topLevelItem(i)->setSelected(true);
    }
    QTreeWidget::keyPressEvent(event);
}

void
StageTree::mousePressEvent(QMouseEvent* event)
{
    QTreeWidgetItem* item = itemAt(event->pos());
    if (item) {
        const int column = columnAt(event->pos().x());
        if (column == PrimItem::Visibility) {
            p->toggleVisible(static_cast<PrimItem*>(item));
            event->accept();
            return;
        }
    }
    QTreeWidget::mousePressEvent(event);
}

void
StageTree::startDrag(Qt::DropActions supportedActions)
{
    Q_UNUSED(supportedActions);

    QList<SdfPath> paths;
    QStringList pathStrings;

    for (QTreeWidgetItem* baseItem : selectedItems()) {
        auto* item = static_cast<PrimItem*>(baseItem);
        if (!item)
            continue;

        const QString pathString = item->data(0, PrimItem::Path).toString();
        if (pathString.isEmpty())
            continue;

        paths.append(SdfPath(QStringToString(pathString)));
        pathStrings.append(pathString);
    }

    paths = path::topLevelPaths(paths);

    pathStrings.clear();
    for (const SdfPath& path : paths)
        pathStrings.append(qt::SdfPathToQString(path));

    if (pathStrings.isEmpty())
        return;

    auto* mime = new QMimeData();
    mime->setData(mime::primPath, pathStrings.join('\n').toUtf8());

    auto* drag = new QDrag(this);
    drag->setMimeData(mime);
    drag->exec(Qt::MoveAction);
}

void
StageTree::dragEnterEvent(QDragEnterEvent* event)
{
    if (!event->mimeData()->hasFormat(mime::primPath)) {
        p->clearDropIndicator();
        event->ignore();
        return;
    }
    event->acceptProposedAction();
}

void
StageTree::dragMoveEvent(QDragMoveEvent* event)
{
    if (!event->mimeData()->hasFormat(mime::primPath)) {
        p->clearDropIndicator();
        event->ignore();
        return;
    }

    QTreeWidgetItem* target = itemAt(event->position().toPoint());
    if (!target) {
        p->clearDropIndicator();
        event->ignore();
        return;
    }

    auto* primTarget = static_cast<PrimItem*>(target);
    const QString fromPathString = QString::fromUtf8(event->mimeData()->data(mime::primPath));
    const QString targetPathString = primTarget->data(0, PrimItem::Path).toString();

    if (fromPathString.isEmpty() || targetPathString.isEmpty()) {
        p->clearDropIndicator();
        event->ignore();
        return;
    }

    const SdfPath fromPath(QStringToString(fromPathString));
    const SdfPath targetPath(QStringToString(targetPathString));

    if (fromPath == targetPath || targetPath.HasPrefix(fromPath)) {
        p->clearDropIndicator();
        event->ignore();
        return;
    }

    const QRect rect = visualItemRect(target);
    const int y = event->position().toPoint().y() - rect.top();
    const int margin = std::max(4, rect.height() / 4);

    int mode = StageTreePrivate::DropOnItem;
    if (y < margin)
        mode = StageTreePrivate::DropAboveItem;
    else if (y > rect.height() - margin)
        mode = StageTreePrivate::DropBelowItem;

    p->setDropIndicator(target, mode);
    event->acceptProposedAction();
}

void
StageTree::dropEvent(QDropEvent* event)
{
    if (!event->mimeData()->hasFormat(mime::primPath)) {
        p->clearDropIndicator();
        event->ignore();
        return;
    }

    QTreeWidgetItem* targetItem = itemAt(event->position().toPoint());
    if (!targetItem) {
        p->clearDropIndicator();
        event->ignore();
        return;
    }

    const QString fromPathText = QString::fromUtf8(event->mimeData()->data(mime::primPath));
    const QString targetPathString = targetItem->data(0, PrimItem::Path).toString();

    if (fromPathText.isEmpty() || targetPathString.isEmpty()) {
        p->clearDropIndicator();
        event->ignore();
        return;
    }

    QList<SdfPath> fromPaths;
    for (const QString& line : fromPathText.split('\n', Qt::SkipEmptyParts)) {
        const SdfPath path(QStringToString(line.trimmed()));
        if (!path.IsEmpty())
            fromPaths.append(path);
    }

    fromPaths = path::topLevelPaths(fromPaths);

    if (fromPaths.isEmpty()) {
        p->clearDropIndicator();
        event->ignore();
        return;
    }

    const SdfPath targetPath(QStringToString(targetPathString));
    const int mode = property(mime::dropModeProperty).toInt();

    SdfPath newParentPath;
    int insertIndex = -1;

    if (mode == StageTreePrivate::DropOnItem) {
        newParentPath = targetPath;
        insertIndex = targetItem->childCount();
    }
    else {
        newParentPath = targetPath.GetParentPath();

        QTreeWidgetItem* parentItem = targetItem->parent();
        if (!parentItem) {
            p->clearDropIndicator();
            event->ignore();
            return;
        }

        const int targetRow = parentItem->indexOfChild(targetItem);
        if (targetRow < 0) {
            p->clearDropIndicator();
            event->ignore();
            return;
        }

        insertIndex = (mode == StageTreePrivate::DropAboveItem) ? targetRow : (targetRow + 1);
    }

    p->clearDropIndicator();

    if (newParentPath.IsEmpty()) {
        event->ignore();
        return;
    }

    for (const SdfPath& fromPath : fromPaths) {
        if (fromPath == newParentPath || newParentPath.HasPrefix(fromPath)) {
            event->ignore();
            return;
        }
    }

    if (context())
        context()->run(new Command(movePath(fromPaths, newParentPath, insertIndex)));

    event->acceptProposedAction();
}

void
StageTree::mouseMoveEvent(QMouseEvent* event)
{
    QTreeWidget::mouseMoveEvent(event);
}

}  // namespace stageviz
