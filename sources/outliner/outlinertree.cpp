// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#include "outlinertree.h"
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
#include <QItemSelectionModel>
#include <QKeyEvent>
#include <QMap>
#include <QMimeData>
#include <QPainter>
#include <QPointer>
#include <QStringList>
#include <QStyledItemDelegate>
#include <QTimer>
#include <algorithm>
#include <functional>
#include <pxr/usd/sdf/primSpec.h>
#include <pxr/usd/usd/prim.h>
#include <pxr/usd/usdGeom/tokens.h>

PXR_NAMESPACE_USING_DIRECTIVE

namespace stageviz {

class OutlinerTreePrivate : public QObject, public SignalGuard {
public:
    OutlinerTreePrivate();
    void init();
    void initContext();
    void initTree();
    void close();
    void collapse();
    void expand();
    void reveal(const SdfPath& path);
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
    void rebuildOverrideCache();
    void updateOverrideCache(const QSet<SdfPath>& primPaths);
    void updateOverrideCacheForNamespaceChanges(const QList<QPair<SdfPath, SdfPath>>& pathRemaps,
                                                const QList<SdfPath>& removedPaths);
    void applyOverrideState(PrimItem* item);
    bool isTransformPropertyPath(const SdfPath& path) const;
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
    void indexItem(PrimItem* item);
    void unindexSubtree(PrimItem* item);
    void deleteItem(PrimItem* item);
    void deleteChildren(PrimItem* parent);
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
    class OutlinerTreeItemDelegate : public TreeWidget::ItemDelegate {
    public:
        explicit OutlinerTreeItemDelegate(QObject* parent = nullptr)
            : TreeWidget::ItemDelegate(parent)
        {}

    protected:
        bool eventFilter(QObject* editor, QEvent* event) override
        {
            if (editor && event && event->type() == QEvent::KeyPress) {
                auto* keyEvent = static_cast<QKeyEvent*>(event);
                if (keyEvent->key() == Qt::Key_Tab && keyEvent->modifiers() == Qt::NoModifier) {
                    QWidget* widget = qobject_cast<QWidget*>(editor);
                    Q_EMIT commitData(widget);
                    Q_EMIT closeEditor(widget, QAbstractItemDelegate::NoHint);
                    keyEvent->accept();
                    return true;
                }
            }
            return TreeWidget::ItemDelegate::eventFilter(editor, event);
        }
    };

    class OutlinerTreeTabFilter : public QObject {
    public:
        explicit OutlinerTreeTabFilter(OutlinerTree* tree)
            : QObject(tree)
            , m_tree(tree)
        {}

    protected:
        bool eventFilter(QObject* watched, QEvent* event) override
        {
            if (!m_tree || watched != m_tree || !event || event->type() != QEvent::KeyPress)
                return QObject::eventFilter(watched, event);

            auto* keyEvent = static_cast<QKeyEvent*>(event);
            if (keyEvent->key() != Qt::Key_Tab || keyEvent->modifiers() != Qt::NoModifier)
                return QObject::eventFilter(watched, event);

            QTreeWidgetItem* item = nullptr;

            const QList<QTreeWidgetItem*> selected = m_tree->selectedItems();
            if (!selected.isEmpty())
                item = selected.first();
            else
                item = m_tree->currentItem();

            if (item && (item->flags() & Qt::ItemIsEditable)) {
                m_tree->setCurrentItem(item, PrimItem::Name, QItemSelectionModel::NoUpdate);
                m_tree->editItem(item, PrimItem::Name);
                keyEvent->accept();
                return true;
            }
            return QObject::eventFilter(watched, event);
        }

    private:
        QPointer<OutlinerTree> m_tree;
    };
    struct Data {
        QPoint dragPressPosition;
        bool dragPressCandidate = false;
        int pending = 0;
        mutable int cachedMaxDepth = -1;
        bool payloadEnabled = false;
        QString filter;
        QList<SdfPath> loadPaths;
        QList<SdfPath> unloadPaths;
        QList<SdfPath> maskPaths;
        QSet<SdfPath> directOverridePaths;
        QSet<SdfPath> descendantOverridePaths;
        QHash<QString, PrimItem*> itemByPath;
        UsdStageRefPtr stage;
        QPointer<ViewContext> context;
        QPointer<OutlinerTree> tree;
    };
    Data d;
};

OutlinerTreePrivate::OutlinerTreePrivate() {}

void
OutlinerTreePrivate::init()
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
    d.tree->setColumnCount(3);
    d.tree->setColumnSelectable(PrimItem::Name, true);
    d.tree->setColumnSelectable(PrimItem::Override, false);
    d.tree->setColumnSelectable(PrimItem::Visibility, false);
    d.tree->setSelectionMode(QAbstractItemView::ExtendedSelection);
    d.tree->setSelectionBehavior(QAbstractItemView::SelectRows);

    connect(d.tree.data(), &OutlinerTree::itemSelectionChanged, this, &OutlinerTreePrivate::itemSelectionChanged);
    connect(d.tree.data(), &OutlinerTree::itemChanged, this, [this](QTreeWidgetItem* item, int column) {
        if (column == PrimItem::Name) {
            PrimItem* primItem = static_cast<PrimItem*>(item);
            checkStateChanged(primItem);
            nameChanged(primItem);
        }
    });
}

void
OutlinerTreePrivate::initContext()
{
    if (!d.context)
        return;

    SelectionList* list = selectionList();
    if (!list)
        return;

    connect(list, &SelectionList::selectionChanged, this, &OutlinerTreePrivate::updateSelection);
    updateSelection(list->paths());
}

SelectionList*
OutlinerTreePrivate::selectionList() const
{
    return d.context ? d.context->selectionList() : nullptr;
}

void
OutlinerTreePrivate::itemCheckState(QTreeWidgetItem* item, bool checkable, bool recursive)
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
OutlinerTreePrivate::treeCheckState(QTreeWidgetItem* item)
{
    itemCheckState(item, true, true);
}

void
OutlinerTreePrivate::initTree()
{
    const int topLevelCount = d.tree->topLevelItemCount();
    for (int i = 0; i < topLevelCount; ++i)
        d.tree->expandItem(d.tree->topLevelItem(i));
}

void
OutlinerTreePrivate::clearDropIndicator()
{
    if (!d.tree)
        return;

    d.tree->setProperty(mime::dropItemPtrProperty, QVariant::fromValue<qulonglong>(0));
    d.tree->setProperty(mime::dropModeProperty, DropNone);

    if (d.tree->updatesEnabled())
        d.tree->update();
}

void
OutlinerTreePrivate::setDropIndicator(QTreeWidgetItem* item, int mode)
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
OutlinerTreePrivate::maskContainsCurrentSelection(const QList<SdfPath>& maskPaths,
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
OutlinerTreePrivate::isRenameOrReparentSource(UsdNotice::ObjectsChanged::PrimResyncType type) const
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
OutlinerTreePrivate::isRenameOrReparentDestination(UsdNotice::ObjectsChanged::PrimResyncType type) const
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
OutlinerTreePrivate::isAncestorOrSelf(const SdfPath& ancestor, const SdfPath& path) const
{
    if (ancestor.IsEmpty() || path.IsEmpty())
        return false;
    return ancestor == path || path.HasPrefix(ancestor);
}

void
OutlinerTreePrivate::applyOverrideState(PrimItem* item)
{
    if (!item)
        return;

    const SdfPath path = item->path();
    item->setOverrideState(d.directOverridePaths.contains(path), d.descendantOverridePaths.contains(path));
}

bool
OutlinerTreePrivate::isTransformPropertyPath(const SdfPath& path) const
{
    if (!path.IsPropertyPath())
        return false;

    const std::string name = path.GetNameToken().GetString();
    return name == "xformOpOrder" || name.rfind("xformOp:", 0) == 0;
}

void
OutlinerTreePrivate::updateOverrideCache(const QSet<SdfPath>& primPaths)
{
    if (primPaths.isEmpty() || !d.stage)
        return;

    QSet<SdfPath> affectedPaths;

    for (const SdfPath& inputPath : primPaths) {
        const SdfPath primPath = inputPath.IsPropertyPath() ? inputPath.GetPrimPath() : inputPath;
        if (primPath.IsEmpty())
            continue;

        SdfPath path = primPath;
        while (!path.IsEmpty()) {
            affectedPaths.insert(path);
            if (path == SdfPath::AbsoluteRootPath())
                break;
            path = path.GetParentPath();
        }
    }

    {
        READ_LOCKER(locker, d.context->stageLock(), "stageLock");

        if (!d.stage)
            return;

        const SdfLayerHandle editLayer = d.stage->GetEditTarget().GetLayer();
        if (!editLayer)
            return;

        for (const SdfPath& inputPath : primPaths) {
            const SdfPath primPath = inputPath.IsPropertyPath() ? inputPath.GetPrimPath() : inputPath;
            if (primPath.IsEmpty() || primPath == SdfPath::AbsoluteRootPath())
                continue;

            const SdfPrimSpecHandle primSpec = editLayer->GetPrimAtPath(primPath);
            const bool direct = primSpec && primSpec->GetSpecifier() == SdfSpecifierOver
                                && !primSpec->GetProperties().empty();

            if (direct)
                d.directOverridePaths.insert(primPath);
            else
                d.directOverridePaths.remove(primPath);
        }
    }

    // Only ancestor state of the changed prims can have changed. Recompute
    // those paths from the direct-override cache instead of rescanning the
    // edit layer and every tree item.
    for (const SdfPath& path : affectedPaths)
        d.descendantOverridePaths.remove(path);

    for (const SdfPath& directPath : d.directOverridePaths) {
        SdfPath ancestorPath = directPath.GetParentPath();
        while (!ancestorPath.IsEmpty()) {
            if (affectedPaths.contains(ancestorPath))
                d.descendantOverridePaths.insert(ancestorPath);

            if (ancestorPath == SdfPath::AbsoluteRootPath())
                break;

            ancestorPath = ancestorPath.GetParentPath();
        }
    }

    for (const SdfPath& path : affectedPaths) {
        if (PrimItem* item = itemFromPath(path))
            applyOverrideState(item);
    }
}

void
OutlinerTreePrivate::updateOverrideCacheForNamespaceChanges(const QList<QPair<SdfPath, SdfPath>>& pathRemaps,
                                                            const QList<SdfPath>& removedPaths)
{
    QList<QPair<SdfPath, SdfPath>> effectiveRemaps = pathRemaps;
    std::sort(effectiveRemaps.begin(), effectiveRemaps.end(), [](const auto& lhs, const auto& rhs) {
        return lhs.first.GetPathElementCount() < rhs.first.GetPathElementCount();
    });
    for (qsizetype i = effectiveRemaps.size() - 1; i >= 0; --i) {
        for (qsizetype j = 0; j < i; ++j) {
            if (effectiveRemaps[i].first.HasPrefix(effectiveRemaps[j].first)) {
                effectiveRemaps.removeAt(i);
                break;
            }
        }
    }
    QList<SdfPath> effectiveRemovedPaths = removedPaths;
    std::sort(effectiveRemovedPaths.begin(), effectiveRemovedPaths.end(), [](const SdfPath& lhs, const SdfPath& rhs) {
        return lhs.GetPathElementCount() < rhs.GetPathElementCount();
    });
    for (qsizetype i = effectiveRemovedPaths.size() - 1; i >= 0; --i) {
        for (qsizetype j = 0; j < i; ++j) {
            if (effectiveRemovedPaths[i].HasPrefix(effectiveRemovedPaths[j])) {
                effectiveRemovedPaths.removeAt(i);
                break;
            }
        }
    }

    QSet<SdfPath> affectedPaths;
    auto addAncestors = [&affectedPaths](SdfPath path) {
        while (!path.IsEmpty()) {
            affectedPaths.insert(path);
            if (path == SdfPath::AbsoluteRootPath())
                break;
            path = path.GetParentPath();
        }
    };

    for (const auto& remap : effectiveRemaps) {
        addAncestors(remap.first);
        addAncestors(remap.second);
    }
    for (const SdfPath& path : effectiveRemovedPaths)
        addAncestors(path);

    auto updatePaths = [&](QSet<SdfPath>& paths) {
        QList<SdfPath> removed;
        QList<SdfPath> added;
        for (const SdfPath& path : paths) {
            bool isRemoved = false;
            for (const SdfPath& removedPath : effectiveRemovedPaths) {
                if (path == removedPath || path.HasPrefix(removedPath)) {
                    isRemoved = true;
                    break;
                }
            }
            if (isRemoved) {
                removed.append(path);
                continue;
            }

            for (const auto& remap : effectiveRemaps) {
                if (path == remap.first || path.HasPrefix(remap.first)) {
                    removed.append(path);
                    added.append(path.ReplacePrefix(remap.first, remap.second));
                    break;
                }
            }
        }

        for (const SdfPath& path : removed)
            paths.remove(path);
        for (const SdfPath& path : added)
            paths.insert(path);
    };

    updatePaths(d.directOverridePaths);
    updatePaths(d.descendantOverridePaths);

    // Moving or deleting a subtree changes only its old and new ancestor
    // chains. Recompute those flags against the updated direct-override set.
    for (const SdfPath& path : affectedPaths)
        d.descendantOverridePaths.remove(path);

    for (const SdfPath& directPath : d.directOverridePaths) {
        SdfPath ancestorPath = directPath.GetParentPath();
        while (!ancestorPath.IsEmpty()) {
            if (affectedPaths.contains(ancestorPath))
                d.descendantOverridePaths.insert(ancestorPath);

            if (ancestorPath == SdfPath::AbsoluteRootPath())
                break;

            ancestorPath = ancestorPath.GetParentPath();
        }
    }

    for (const SdfPath& path : affectedPaths) {
        if (PrimItem* item = itemFromPath(path))
            applyOverrideState(item);
    }

    // Namespace edits retain the existing tree items. Override state inside a
    // moved subtree is unchanged; only its roots and old/new ancestor chains
    // need to be refreshed.
}

void
OutlinerTreePrivate::rebuildOverrideCache()
{
    d.directOverridePaths.clear();
    d.descendantOverridePaths.clear();

    {
        READ_LOCKER(locker, d.context->stageLock(), "stageLock");

        if (!d.stage)
            return;

        const SdfLayerHandle editLayer = d.stage->GetEditTarget().GetLayer();
        if (!editLayer)
            return;

        std::function<void(const SdfPrimSpecHandle&)> collect = [&](const SdfPrimSpecHandle& primSpec) {
            if (!primSpec)
                return;

            if (primSpec->GetSpecifier() == SdfSpecifierOver && !primSpec->GetProperties().empty()) {
                const SdfPath directPath = primSpec->GetPath();
                d.directOverridePaths.insert(directPath);

                SdfPath ancestorPath = directPath.GetParentPath();
                while (!ancestorPath.IsEmpty()) {
                    d.descendantOverridePaths.insert(ancestorPath);

                    if (ancestorPath == SdfPath::AbsoluteRootPath())
                        break;

                    ancestorPath = ancestorPath.GetParentPath();
                }
            }

            for (const SdfPrimSpecHandle& child : primSpec->GetNameChildren())
                collect(child);
        };

        for (const SdfPrimSpecHandle& rootPrim : editLayer->GetRootPrims())
            collect(rootPrim);
    }

    for (auto it = d.itemByPath.begin(); it != d.itemByPath.end(); ++it)
        applyOverrideState(it.value());
}

void
OutlinerTreePrivate::close()
{
    QSignalBlocker blocker(d.tree);
    d.pending = 0;
    d.cachedMaxDepth = -1;
    d.loadPaths.clear();
    d.unloadPaths.clear();
    d.directOverridePaths.clear();
    d.descendantOverridePaths.clear();
    d.stage = nullptr;
    d.itemByPath.clear();
    d.tree->clear();
    clearDropIndicator();
}

void
OutlinerTreePrivate::collapse()
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
OutlinerTreePrivate::expand()
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
OutlinerTreePrivate::reveal(const SdfPath& path)
{
    if (path.IsEmpty() || !d.tree)
        return;

    PrimItem* item = itemFromPath(path);

    // Payload mode deliberately omits payload descendants from the tree. In
    // that case reveal the nearest visible ancestor available in this view.
    if (!item && d.payloadEnabled) {
        SdfPath ancestorPath = path.GetParentPath();
        while (!ancestorPath.IsEmpty()) {
            item = itemFromPath(ancestorPath);
            if (item)
                break;
            if (ancestorPath == SdfPath::AbsoluteRootPath())
                break;
            ancestorPath = ancestorPath.GetParentPath();
        }
    }

    if (!item)
        return;

    QTreeWidgetItem* parent = item->parent();
    while (parent) {
        parent->setExpanded(true);
        parent = parent->parent();
    }

    // currentItem is navigation state only; do not disturb the semantic or Qt
    // multi-selection while following the first selected path.
    d.tree->setCurrentItem(item, PrimItem::Name, QItemSelectionModel::NoUpdate);

    const QRect itemRect = d.tree->visualItemRect(item);
    const QRect viewRect = d.tree->viewport()->rect();
    if (!viewRect.intersects(itemRect))
        d.tree->scrollToItem(item, QAbstractItemView::PositionAtCenter);
}

void
OutlinerTreePrivate::expandDepth(int targetDepth, const SdfPath& path)
{
    if (d.tree->topLevelItemCount() == 0)
        return;

    const bool updatesEnabled = d.tree->updatesEnabled();
    d.tree->setUpdatesEnabled(false);
    const auto applyDepth = [targetDepth](auto&& self, QTreeWidgetItem* item, int depthValue) -> void {
        const bool shouldExpand = depthValue <= targetDepth;
        if (item->isExpanded() != shouldExpand)
            item->setExpanded(shouldExpand);

        // Descendants under a collapsed item cannot affect the visible tree.
        // Leave their expansion state alone and update them if this branch is
        // opened by a later slider value.
        if (!shouldExpand)
            return;

        for (int i = 0; i < item->childCount(); ++i)
            self(self, item->child(i), depthValue + 1);
    };

    if (path.IsEmpty()) {
        for (int i = 0; i < d.tree->topLevelItemCount(); ++i) {
            applyDepth(applyDepth, d.tree->topLevelItem(i), 0);
        }
    }
    else {
        QTreeWidgetItem* item = itemFromPath(path);
        if (!item) {
            d.tree->setUpdatesEnabled(updatesEnabled);
            return;
        }

        const int selectedDepth = depth(path);
        QTreeWidgetItem* parent = item->parent();
        int parentDepthValue = selectedDepth - 1;
        while (parent) {
            const bool shouldExpand = parentDepthValue <= targetDepth;
            if (parent->isExpanded() != shouldExpand)
                parent->setExpanded(shouldExpand);
            parent = parent->parent();
            parentDepthValue--;
        }

        applyDepth(applyDepth, item, selectedDepth);
    }

    d.tree->setUpdatesEnabled(updatesEnabled);
}

int
OutlinerTreePrivate::maxDepth(const SdfPath& path) const
{
    if (d.tree->topLevelItemCount() == 0)
        return 0;

    if (path.IsEmpty() && d.cachedMaxDepth >= 0)
        return d.cachedMaxDepth;

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
        d.cachedMaxDepth = max;
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
OutlinerTreePrivate::depth(const SdfPath& path) const
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

void
OutlinerTreePrivate::indexItem(PrimItem* item)
{
    if (!item)
        return;

    const SdfPath path = item->path();
    if (path.IsEmpty())
        return;

    d.itemByPath.insert(qt::SdfPathToQString(path), item);
}

void
OutlinerTreePrivate::unindexSubtree(PrimItem* item)
{
    if (!item)
        return;

    for (int i = 0; i < item->childCount(); ++i)
        unindexSubtree(static_cast<PrimItem*>(item->child(i)));

    const SdfPath path = item->path();
    if (path.IsEmpty())
        return;

    const QString key = qt::SdfPathToQString(path);
    auto it = d.itemByPath.find(key);
    if (it != d.itemByPath.end() && it.value() == item)
        d.itemByPath.erase(it);
}

void
OutlinerTreePrivate::deleteItem(PrimItem* item)
{
    if (!item)
        return;

    unindexSubtree(item);
    delete item;
}

void
OutlinerTreePrivate::deleteChildren(PrimItem* parent)
{
    if (!parent)
        return;

    while (parent->childCount() > 0)
        deleteItem(static_cast<PrimItem*>(parent->child(0)));
}

PrimItem*
OutlinerTreePrivate::addItem(PrimItem* parent, const SdfPath& path)
{
    UsdStageRefPtr stage;
    bool isPayload = false;
    bool isLoaded = false;

    {
        READ_LOCKER(locker, d.context->stageLock(), "stageLock");
        stage = d.stage;
        if (!stage)
            return nullptr;

        isPayload = stage::isPayload(stage, path);
        if (isPayload && d.payloadEnabled)
            isLoaded = stage->GetPrimAtPath(path).IsLoaded();
    }

    PrimItem* item = new PrimItem(parent, stage, path);
    indexItem(item);
    applyOverrideState(item);
    item->invalidate();

    Qt::ItemFlags flags = item->flags();
    flags |= Qt::ItemIsDragEnabled | Qt::ItemIsDropEnabled;
    if (stage->GetPrimAtPath(path).IsInstanceProxy())
        flags &= ~(Qt::ItemIsDragEnabled | Qt::ItemIsDropEnabled | Qt::ItemIsEditable | Qt::ItemIsUserCheckable);
    item->setFlags(flags);

    itemCheckState(item, d.payloadEnabled && !stage->GetPrimAtPath(path).IsInstanceProxy());
    parent->addChild(item);

    if (isPayload && d.payloadEnabled) {
        item->setCheckState(0, isLoaded ? Qt::Checked : Qt::Unchecked);
        return item;
    }

    addChildren(item, path);
    return item;
}

void
OutlinerTreePrivate::addChildren(PrimItem* parent, const SdfPath& path)
{
    QList<SdfPath> childPaths;
    {
        READ_LOCKER(locker, d.context->stageLock(), "stageLock");
        if (!d.stage)
            return;

        const UsdPrim prim = d.stage->GetPrimAtPath(path);
        if (!prim)
            return;

        for (const UsdPrim& child : prim.GetFilteredChildren(UsdTraverseInstanceProxies(UsdPrimAllPrimsPredicate)))
            childPaths.append(child.GetPath());
    }

    for (const SdfPath& childPath : childPaths)
        addItem(parent, childPath);
}

void
OutlinerTreePrivate::toggleVisible(PrimItem* item)
{
    const SdfPath path(QStringToString(item->data(0, PrimItem::Path).toString()));
    bool visible = false;

    {
        READ_LOCKER(locker, d.context->stageLock(), "stageLock");
        if (!d.stage)
            return;
        visible = stage::isVisible(d.stage, path);
    }

    if (d.stage->GetPrimAtPath(path).IsInstanceProxy())
        return;

    if (visible)
        d.context->execute(new Command(hidePaths(QList<SdfPath> { path }, false)));
    else
        d.context->execute(new Command(showPaths(QList<SdfPath> { path }, false)));
}

void
OutlinerTreePrivate::updateFilter()
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
OutlinerTreePrivate::itemSelectionChanged()
{
    QList<SdfPath> paths;
    for (QTreeWidgetItem* baseItem : d.tree->selectedItems()) {
        PrimItem* item = static_cast<PrimItem*>(baseItem);
        const QString pathString = item->data(0, PrimItem::Path).toString();
        if (!pathString.isEmpty())
            paths.append(SdfPath(QStringToString(pathString)));
    }

    SelectionList* list = selectionList();
    if (!list)
        return;

    if (paths == list->paths())
        return;

    d.context->execute(new Command(selectPaths(paths)));
}

void
OutlinerTreePrivate::checkStateChanged(PrimItem* item)
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
                d.context->execute(new Command(loadPayloads(d.loadPaths)));
                d.loadPaths.clear();
            }
            if (!d.unloadPaths.isEmpty()) {
                d.context->execute(new Command(unloadPayloads(d.unloadPaths)));
                d.unloadPaths.clear();
            }
            d.pending = 0;
        }
    });
}

void
OutlinerTreePrivate::nameChanged(PrimItem* item)
{
    if (!item)
        return;

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

    d.context->execute(new Command(renamePath(oldPath, newName)));

    if (itemFromPath(oldPath))
        item->setData(PrimItem::Name, PrimItem::EditName, QString());
}

void
OutlinerTreePrivate::contextMenuEvent(QContextMenuEvent* event)
{
    if (!d.tree || !event || !d.context)
        return;

    SelectionList* list = selectionList();
    if (!list)
        return;

    QTreeWidgetItem* clickedItem = d.tree->itemAt(event->pos());
    QList<SdfPath> paths = list->paths();

    if (clickedItem && !d.tree->isItemSelected(clickedItem)) {
        auto* item = static_cast<PrimItem*>(clickedItem);
        const QString pathString = item->data(0, PrimItem::Path).toString();
        const SdfPath clickedPath = pathString.isEmpty() ? SdfPath() : SdfPath(qt::QStringToString(pathString));

        if (!clickedPath.IsEmpty()) {
            paths = { clickedPath };

            {
                SignalGuard::Scope guard(this);
                d.tree->setCurrentItem(clickedItem, PrimItem::Name, QItemSelectionModel::NoUpdate);
            }

            d.context->execute(new Command(selectPaths(paths)));
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
OutlinerTreePrivate::updateStage(UsdStageRefPtr stage)
{
    SignalGuard::Scope guard(this);

    QList<SdfPath> expandedPaths;
    SdfPath currentPath;

    // Save As replaces the live UsdStage with a newly opened stage while the
    // logical document and prim paths remain the same. Preserve expansion and
    // current-item state across a direct non-null -> non-null stage replacement.
    if (d.stage && stage) {
        if (QTreeWidgetItem* currentItem = d.tree->currentItem()) {
            if (auto* item = dynamic_cast<PrimItem*>(currentItem))
                currentPath = item->path();
        }

        std::function<void(QTreeWidgetItem*)> captureExpanded = [&](QTreeWidgetItem* baseItem) {
            if (!baseItem)
                return;

            auto* item = static_cast<PrimItem*>(baseItem);
            if (item->isExpanded()) {
                const SdfPath path = item->path();
                if (!path.IsEmpty())
                    expandedPaths.append(path);
            }

            for (int i = 0; i < baseItem->childCount(); ++i)
                captureExpanded(baseItem->child(i));
        };

        for (int i = 0; i < d.tree->topLevelItemCount(); ++i)
            captureExpanded(d.tree->topLevelItem(i));
    }

    close();
    d.stage = stage;

    if (!stage)
        return;

    rebuildOverrideCache();

    UsdPrim prim;
    {
        READ_LOCKER(locker, d.context->stageLock(), "stageLock");
        prim = stage->GetPseudoRoot();
    }

    PrimItem* rootItem = new PrimItem(d.tree.data(), stage, prim.GetPath());
    indexItem(rootItem);
    applyOverrideState(rootItem);

    Qt::ItemFlags flags = rootItem->flags();
    flags |= Qt::ItemIsDropEnabled;
    rootItem->setFlags(flags);

    itemCheckState(rootItem, false, false);
    addChildren(rootItem, prim.GetPath());
    initTree();

    if (d.payloadEnabled)
        treeCheckState(rootItem);

    for (const SdfPath& path : expandedPaths) {
        if (PrimItem* item = itemFromPath(path))
            item->setExpanded(true);
    }

    SelectionList* list = selectionList();
    const QList<SdfPath> selectionPaths = list ? list->paths() : QList<SdfPath>();

    updateSelection(selectionPaths);

    // currentItem is UI navigation state. Restore it only if the same row still
    // exists after the replacement stage is rebuilt, and never recreate current
    // state when semantic selection is empty.
    if (selectionPaths.isEmpty()) {
        d.tree->setCurrentItem(nullptr);
    }
    else if (!currentPath.IsEmpty()) {
        if (PrimItem* item = itemFromPath(currentPath)) {
            if (d.tree->isItemSelected(item))
                d.tree->setCurrentItem(item, PrimItem::Name, QItemSelectionModel::NoUpdate);
        }
    }
}

void
OutlinerTree::updateEditLayer()
{
    p->rebuildOverrideCache();

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
OutlinerTreePrivate::syncDirectChildrenOnly(PrimItem* parentItem, const UsdPrim& parentPrim)
{
    if (!parentItem || !parentPrim)
        return;

    bool isPayload = false;
    bool isLoaded = false;

    {
        READ_LOCKER(locker, d.context->stageLock(), "stageLock");
        if (!d.stage)
            return;

        isPayload = stage::isPayload(d.stage, parentPrim.GetPath());
        if (isPayload && d.payloadEnabled)
            isLoaded = parentPrim.IsLoaded();
    }

    if (isPayload && d.payloadEnabled) {
        parentItem->invalidate();
        itemCheckState(parentItem, true, false);

        const Qt::CheckState want = isLoaded ? Qt::Checked : Qt::Unchecked;
        if (parentItem->checkState(PrimItem::Name) != want)
            parentItem->setCheckState(PrimItem::Name, want);

        deleteChildren(parentItem);

        return;
    }

    QList<SdfPath> ordered;
    QSet<QString> stageSet;

    {
        READ_LOCKER(locker, d.context->stageLock(), "stageLock");
        if (!d.stage)
            return;

        for (const UsdPrim& childPrim :
             parentPrim.GetFilteredChildren(UsdTraverseInstanceProxies(UsdPrimAllPrimsPredicate))) {
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

    QList<PrimItem*> staleItems;
    {
        READ_LOCKER(locker, d.context->stageLock(), "stageLock");
        if (!d.stage)
            return;

        for (auto it = existing.begin(); it != existing.end(); ++it) {
            if (stageSet.contains(it.key()))
                continue;

            const SdfPath existingPath(QStringToString(it.key()));
            if (!d.stage->GetPrimAtPath(existingPath))
                staleItems.append(it.value());
        }
    }

    for (PrimItem* item : staleItems)
        deleteItem(item);

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

            isPayload = stage::isPayload(d.stage, childPath);
            if (isPayload && d.payloadEnabled)
                isLoaded = childPrim.IsLoaded();
        }

        itemCheckState(childItem, d.payloadEnabled, false);

        if (isPayload && d.payloadEnabled) {
            const Qt::CheckState want = isLoaded ? Qt::Checked : Qt::Unchecked;
            if (childItem->checkState(0) != want)
                childItem->setCheckState(0, want);
        }
    }

    parentItem->invalidate();
}

void
OutlinerTreePrivate::updatePrims(const NoticeBatch& batch)
{
    SignalGuard::Scope guard(this);

    if (!d.stage || !d.tree || batch.entries.isEmpty())
        return;

    d.cachedMaxDepth = -1;

    // Bulk deletes need one reconciliation per surviving parent, not one
    // recursive subtree refresh for every removed prim.
    if (batch.entries.size() >= 256) {
        QSet<SdfPath> deletedPaths;
        bool deleteOnly = true;
        for (const NoticeEntry& entry : batch.entries) {
            if (entry.path.IsEmpty())
                continue;
            if (entry.path == SdfPath::AbsoluteRootPath())
                continue;
            if (!entry.path.IsPrimPath() || entry.primResyncType != UsdNotice::ObjectsChanged::PrimResyncType::Delete
                || !entry.associatedPath.IsEmpty() || entry.resolvedAssetPathsResynced) {
                deleteOnly = false;
                break;
            }
            deletedPaths.insert(entry.path);
        }

        if (deleteOnly && !deletedPaths.isEmpty()) {
            // Verify that every notice refers to a genuinely absent prim.
            // Otherwise use the normal conservative resync handling.
            {
                READ_LOCKER(locker, d.context->stageLock(), "stageLock");
                if (!d.stage)
                    return;
                for (const SdfPath& path : deletedPaths) {
                    if (d.stage->GetPrimAtPath(path)) {
                        deleteOnly = false;
                        break;
                    }
                }
            }
        }

        if (deleteOnly && !deletedPaths.isEmpty()) {
            QSet<SdfPath> parents;
            QList<PrimItem*> roots;
            roots.reserve(deletedPaths.size());
            for (const SdfPath& path : deletedPaths) {
                const SdfPath parentPath = path.GetParentPath();
                if (!parentPath.IsEmpty())
                    parents.insert(parentPath);
                // A deleted ancestor owns its Qt descendants. Avoid deleting
                // the same subtree twice, including when notice paths overlap.
                bool hasDeletedAncestor = false;
                SdfPath ancestor = parentPath;
                while (!ancestor.IsEmpty()) {
                    if (deletedPaths.contains(ancestor)) {
                        hasDeletedAncestor = true;
                        break;
                    }
                    ancestor = ancestor.GetParentPath();
                }
                if (!hasDeletedAncestor) {
                    if (PrimItem* item = itemFromPath(path))
                        roots.append(item);
                }
            }

            const bool updatesEnabled = d.tree->updatesEnabled();
            d.tree->setUpdatesEnabled(false);
            for (PrimItem* item : roots)
                deleteItem(item);

            // Direct children of surviving parents are already correct after
            // removing their deleted Qt subtrees. Refresh parent rows only;
            // avoid recursive invalidateChildren and per-notice reconciliation.
            for (const SdfPath& path : parents) {
                if (deletedPaths.contains(path))
                    continue;
                if (PrimItem* item = itemFromPath(path))
                    item->invalidate();
            }

            const QList<SdfPath> removedRoots = deletedPaths.values();
            updateOverrideCacheForNamespaceChanges({}, removedRoots);
            SelectionList* list = selectionList();
            updateSelection(list ? list->paths() : QList<SdfPath>());
            if (!list || list->paths().isEmpty())
                d.tree->setCurrentItem(nullptr);
            if (!d.filter.isEmpty())
                updateFilter();
            d.tree->setUpdatesEnabled(updatesEnabled);
            if (updatesEnabled)
                d.tree->viewport()->update();
            return;
        }
    }

    bool propertyFastPathBatch = true;
    QSet<SdfPath> changedPropertyPrimPaths;

    for (const NoticeEntry& entry : batch.entries) {
        const bool ordinaryPropertyInfo = entry.changedInfoOnly && entry.path.IsPropertyPath()
                                          && !entry.resolvedAssetPathsResynced
                                          && entry.primResyncType == UsdNotice::ObjectsChanged::PrimResyncType::Invalid;

        // First-time xform authoring can arrive from USD as property resyncs
        // rather than info-only changes (for example when xformOpOrder and an
        // xformOp are first authored). These notices still cannot change the
        // OutlinerTree namespace because the notice path itself is a property.
        // Treat them like ordinary transform-property edits, but keep asset and
        // namespace/delete resyncs on the conservative path.
        const bool transformPropertyResync = entry.path.IsPropertyPath() && isTransformPropertyPath(entry.path)
                                             && !entry.resolvedAssetPathsResynced
                                             && entry.primResyncType
                                                    != UsdNotice::ObjectsChanged::PrimResyncType::Delete
                                             && !isRenameOrReparentSource(entry.primResyncType)
                                             && !isRenameOrReparentDestination(entry.primResyncType);

        if (!(ordinaryPropertyInfo || transformPropertyResync)) {
            propertyFastPathBatch = false;
            break;
        }

        changedPropertyPrimPaths.insert(entry.path.GetPrimPath());
    }

    struct PathRemap {
        SdfPath from;
        SdfPath to;
    };

    QList<PathRemap> pathRemaps;

    for (const NoticeEntry& entry : batch.entries) {
        if (entry.path.IsEmpty() || entry.associatedPath.IsEmpty())
            continue;

        if (!isRenameOrReparentDestination(entry.primResyncType))
            continue;

        pathRemaps.append({ entry.associatedPath, entry.path });
    }
    bool namespaceOnlyBatch = !pathRemaps.isEmpty();
    for (const NoticeEntry& entry : batch.entries) {
        if (entry.path.IsEmpty() || entry.path == SdfPath::AbsoluteRootPath())
            continue;

        if ((isRenameOrReparentSource(entry.primResyncType) || isRenameOrReparentDestination(entry.primResyncType))
            && !entry.associatedPath.IsEmpty())
            continue;

        namespaceOnlyBatch = false;
        break;
    }

    auto remapPath = [&](SdfPath path) {
        bool changed = true;
        int guardCount = 0;

        while (changed && guardCount++ < pathRemaps.size() + 1) {
            changed = false;

            for (const PathRemap& remap : pathRemaps) {
                if (path == remap.from || path.HasPrefix(remap.from)) {
                    path = path.ReplacePrefix(remap.from, remap.to);
                    changed = true;
                    break;
                }
            }
        }

        return path;
    };

    // A batch often contains many changed siblings. The USD stage already
    // reflects the whole batch, so reconciling their shared parent repeatedly
    // only repeats the same direct-child scan and Qt row updates.
    QSet<SdfPath> refreshedParents;
    auto refreshParentBranchOnce = [&](const SdfPath& path) {
        const SdfPath primPath = path.IsPropertyPath() ? path.GetPrimPath() : path;
        const SdfPath parentPath = primPath.GetParentPath();
        if (parentPath.IsEmpty() || refreshedParents.contains(parentPath))
            return;
        refreshedParents.insert(parentPath);
        refreshParentBranch(path);
    };

    // Preserve only UI state that can actually be disturbed by a namespace
    // edit. Selection itself is owned by SelectionList and is synchronized
    // from that authoritative state after the tree update.

    SdfPath currentPath;
    if (QTreeWidgetItem* currentItem = d.tree->currentItem()) {
        PrimItem* item = static_cast<PrimItem*>(currentItem);
        if (item)
            currentPath = item->path();
    }

    struct AffectedState {
        SdfPath path;
        bool expanded = false;
    };

    QList<AffectedState> affectedStates;
    QSet<SdfPath> affectedStatePaths;

    auto captureExpandedState = [&](const SdfPath& path) {
        if (path.IsEmpty() || affectedStatePaths.contains(path))
            return;

        PrimItem* item = itemFromPath(path);
        if (!item)
            return;

        affectedStatePaths.insert(path);
        affectedStates.append({ path, item->isExpanded() });
    };

    // The edited subtree roots and their source/destination parents are the
    // only rows whose placement/order can change. Capture their expansion
    // state explicitly; descendants keep state because the same Qt subtree
    // objects are retained and remapped in place.
    for (const NoticeEntry& entry : batch.entries) {
        if (entry.path.IsEmpty())
            continue;

        if (isRenameOrReparentDestination(entry.primResyncType) && !entry.associatedPath.IsEmpty()) {
            const SdfPath oldPath = entry.associatedPath;
            const SdfPath newPath = entry.path;

            captureExpandedState(oldPath);
            captureExpandedState(oldPath.GetParentPath());
            captureExpandedState(newPath.GetParentPath());
        }
        else if (isRenameOrReparentSource(entry.primResyncType)) {
            captureExpandedState(entry.path);
            captureExpandedState(entry.path.GetParentPath());
        }
    }

    d.tree->setUpdatesEnabled(false);

    auto rebuildPath = [&](const SdfPath& path) {
        if (path.IsEmpty())
            return;

        QList<SdfPath> instancePaths;
        {
            READ_LOCKER(locker, d.context->stageLock(), "stageLock");
            if (d.stage) {
                UsdPrim prim = d.stage->GetPrimAtPath(path);
                if (prim && prim.IsInPrototype()) {
                    while (prim && !prim.IsPrototype())
                        prim = prim.GetParent();
                    if (prim && prim.IsPrototype()) {
                        const SdfPath prototypePath = prim.GetPath();
                        const SdfPath relativePath = path.MakeRelativePath(prototypePath);
                        for (const UsdPrim& instance : prim.GetInstances()) {
                            if (instance && instance.IsInstance())
                                instancePaths.append(instance.GetPath().AppendPath(relativePath));
                        }
                    }
                }
            }
        }

        // USD reports edits to instanceable source prims at prototype paths.
        // Those paths have no Outliner rows, so reconcile the corresponding
        // scene instance paths to expose newly composed children.
        if (!instancePaths.isEmpty()) {
            for (const SdfPath& instancePath : instancePaths)
                refreshParentBranchOnce(instancePath);
            return;
        }

        PrimItem* item = itemFromPath(path);
        UsdPrim prim;

        {
            READ_LOCKER(locker, d.context->stageLock(), "stageLock");

            if (d.stage) {
                prim = path == SdfPath::AbsoluteRootPath() ? d.stage->GetPseudoRoot() : d.stage->GetPrimAtPath(path);
            }
        }

        if (item && prim)
            invalidateSubtree(item, prim);

        refreshParentBranchOnce(path);
    };

    QSet<SdfPath> handledPaths;
    QSet<SdfPath> handledParents;

    bool hasSpecificPath = false;

    for (const NoticeEntry& entry : batch.entries) {
        if (!entry.path.IsEmpty() && entry.path != SdfPath::AbsoluteRootPath()) {
            hasSpecificPath = true;
            break;
        }
    }

    // Namespace destinations first. Rename keeps the same Qt parent; reparent
    // physically moves the existing Qt subtree before remapping paths.
    for (const NoticeEntry& entry : batch.entries) {
        if (entry.path.IsEmpty() || entry.associatedPath.IsEmpty())
            continue;

        if (!isRenameOrReparentDestination(entry.primResyncType))
            continue;

        const SdfPath oldPath = entry.associatedPath;
        const SdfPath newPath = entry.path;

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
    }

    // Reconcile ordering and direct children after in-place remapping.
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

        if (parentPrim)
            syncDirectChildrenOnly(parentItem, parentPrim);
    }

    // Everything not represented by a usable namespace pair follows the
    // conservative ordinary refresh path.
    for (const NoticeEntry& entry : batch.entries) {
        if (entry.path.IsEmpty())
            continue;

        if (hasSpecificPath && entry.path == SdfPath::AbsoluteRootPath()) {
            continue;
        }

        if (handledPaths.contains(entry.path))
            continue;

        if (isRenameOrReparentSource(entry.primResyncType) && !entry.associatedPath.IsEmpty()
            && handledPaths.contains(entry.associatedPath)) {
            continue;
        }

        bool coveredByHandledParent = false;

        for (const SdfPath& parentPath : handledParents) {
            if (entry.path == parentPath || isAncestorOrSelf(parentPath, entry.path)) {
                coveredByHandledParent = true;
                break;
            }
        }

        if (coveredByHandledParent)
            continue;

        // Transform-property edits do not change anything displayed by the
        // OutlinerTree row itself. This also covers first-time xform property
        // authoring, which USD may report as a property resync rather than an
        // info-only notice.
        if (propertyFastPathBatch && isTransformPropertyPath(entry.path))
            continue;

        if (entry.changedInfoOnly) {
            if (entry.path.IsPropertyPath() && entry.path.GetNameToken() == UsdGeomTokens->visibility) {
                if (PrimItem* item = itemFromPath(entry.path.GetPrimPath()))
                    item->invalidate();
            }
            else {
                updatePrim(entry.path);
            }
            continue;
        }

        if (entry.resolvedAssetPathsResynced) {
            invalidatePrim(entry.path);
            continue;
        }

        switch (entry.primResyncType) {
        case UsdNotice::ObjectsChanged::PrimResyncType::Delete:
            invalidatePrim(entry.path);
            refreshParentBranchOnce(entry.path);
            break;

        case UsdNotice::ObjectsChanged::PrimResyncType::UnchangedPrimStack:
            updatePrim(entry.path);
            refreshParentBranchOnce(entry.path);
            break;

        case UsdNotice::ObjectsChanged::PrimResyncType::RenameSource:
        case UsdNotice::ObjectsChanged::PrimResyncType::RenameDestination:
        case UsdNotice::ObjectsChanged::PrimResyncType::ReparentSource:
        case UsdNotice::ObjectsChanged::PrimResyncType::ReparentDestination:
        case UsdNotice::ObjectsChanged::PrimResyncType::RenameAndReparentSource:
        case UsdNotice::ObjectsChanged::PrimResyncType::RenameAndReparentDestination: rebuildPath(entry.path); break;

        case UsdNotice::ObjectsChanged::PrimResyncType::Other:
        case UsdNotice::ObjectsChanged::PrimResyncType::Invalid:
        default: rebuildPath(entry.path); break;
        }
    }

    // Restore only explicitly affected expansion states. Unrelated rows were
    // never touched and therefore retain their existing Qt state.
    for (const AffectedState& state : affectedStates) {
        const SdfPath finalPath = remapPath(state.path);

        if (PrimItem* item = itemFromPath(finalPath))
            item->setExpanded(state.expanded);
    }

    // Property-only changes cannot alter tree structure. Update the override
    // cache only for changed prims and their ancestors. Structural/resync
    // batches keep the conservative full rebuild.
    if (propertyFastPathBatch)
        updateOverrideCache(changedPropertyPrimPaths);
    else if (namespaceOnlyBatch) {
        QList<QPair<SdfPath, SdfPath>> cachePathRemaps;
        cachePathRemaps.reserve(pathRemaps.size());
        for (const PathRemap& remap : pathRemaps)
            cachePathRemaps.append({ remap.from, remap.to });
        updateOverrideCacheForNamespaceChanges(cachePathRemaps, {});
    }
    else
        rebuildOverrideCache();


    // Property-only batches cannot add/remove/remap OutlinerTree rows and cannot
    // change SelectionList. Keep the existing Qt selection untouched. This is
    // especially important for multi-prim gizmo edits where clearSelection()
    // plus reselecting hundreds of rows is pure release-time overhead.
    if (!propertyFastPathBatch) {
        // SelectionList is the sole source of truth for selection. Never restore
        // selection from cached Qt item state: payload/namespace notices can arrive
        // after the semantic selection has already been cleared.
        SelectionList* list = selectionList();
        const QList<SdfPath> selectionPaths = list ? list->paths() : QList<SdfPath>();

        updateSelection(selectionPaths);

        // currentItem is UI navigation state only. Preserve it through namespace
        // edits when it still corresponds to the semantic selection, but never let
        // restoring currentItem create a Qt selection after SelectionList cleared.
        if (selectionPaths.isEmpty()) {
            d.tree->setCurrentItem(nullptr);
        }
        else if (!currentPath.IsEmpty()) {
            const SdfPath finalCurrentPath = remapPath(currentPath);

            if (PrimItem* currentItem = itemFromPath(finalCurrentPath)) {
                if (d.tree->isItemSelected(currentItem))
                    d.tree->setCurrentItem(currentItem, PrimItem::Name, QItemSelectionModel::NoUpdate);
            }
        }
    }

    d.tree->setUpdatesEnabled(true);
    d.tree->viewport()->update();
}

void
OutlinerTreePrivate::updatePrim(const SdfPath& path)
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

        isPayload = stage::isPayload(d.stage, primPath);
    }

    primItem->invalidate();
    itemCheckState(primItem, d.payloadEnabled, false);

    if (isPayload && d.payloadEnabled) {
        const Qt::CheckState want = prim.IsLoaded() ? Qt::Checked : Qt::Unchecked;
        if (primItem->checkState(0) != want)
            primItem->setCheckState(0, want);

        deleteChildren(primItem);
    }
}

void
OutlinerTreePrivate::invalidateSubtree(PrimItem* item, const UsdPrim& prim)
{
    if (!item || !prim)
        return;

    const SdfPath primPath = prim.GetPath();

    bool isPayload = false;
    {
        READ_LOCKER(locker, d.context->stageLock(), "stageLock");
        if (d.stage)
            isPayload = stage::isPayload(d.stage, primPath);
    }

    item->invalidate();
    itemCheckState(item, d.payloadEnabled, false);

    if (isPayload && d.payloadEnabled) {
        const Qt::CheckState want = prim.IsLoaded() ? Qt::Checked : Qt::Unchecked;
        if (item->checkState(0) != want)
            item->setCheckState(0, want);

        deleteChildren(item);

        return;
    }

    invalidateChildren(item, prim);
}

void
OutlinerTreePrivate::invalidatePrim(const SdfPath& path)
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
            deleteItem(primItem);

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

            if (parentPrim) {
                parentIsPayload = stage::isPayload(d.stage, parentPath);
                if (parentIsPayload && d.payloadEnabled)
                    parentIsLoaded = parentPrim.IsLoaded();
            }
        }

        if (parentIsPayload && d.payloadEnabled) {
            parentItem->invalidate();
            itemCheckState(parentItem, true, false);

            const Qt::CheckState want = parentIsLoaded ? Qt::Checked : Qt::Unchecked;
            if (parentItem->checkState(PrimItem::Name) != want)
                parentItem->setCheckState(PrimItem::Name, want);

            deleteChildren(parentItem);

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
OutlinerTreePrivate::invalidateChildren(PrimItem* parentItem, const UsdPrim& prim)
{
    if (!parentItem || !prim)
        return;

    bool isPayload = false;
    bool isLoaded = false;

    {
        READ_LOCKER(locker, d.context->stageLock(), "stageLock");
        if (!d.stage)
            return;

        isPayload = stage::isPayload(d.stage, prim.GetPath());
        if (isPayload && d.payloadEnabled)
            isLoaded = prim.IsLoaded();
    }

    if (isPayload && d.payloadEnabled) {
        parentItem->invalidate();
        itemCheckState(parentItem, true, false);

        const Qt::CheckState want = isLoaded ? Qt::Checked : Qt::Unchecked;
        if (parentItem->checkState(PrimItem::Name) != want)
            parentItem->setCheckState(PrimItem::Name, want);

        deleteChildren(parentItem);

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
        for (const UsdPrim& childPrim :
             prim.GetFilteredChildren(UsdTraverseInstanceProxies(UsdPrimAllPrimsPredicate))) {
            const SdfPath childPath = childPrim.GetPath();
            const QString key = qt::StringToQString(childPath.GetString());
            ordered.append(childPath);
            stageSet.insert(key);
        }
    }

    QList<PrimItem*> staleItems;
    {
        READ_LOCKER(locker, d.context->stageLock(), "stageLock");
        if (!d.stage)
            return;

        for (auto it = existing.begin(); it != existing.end(); ++it) {
            if (stageSet.contains(it.key()))
                continue;

            const SdfPath existingPath(QStringToString(it.key()));
            if (!d.stage->GetPrimAtPath(existingPath))
                staleItems.append(it.value());
        }
    }

    for (PrimItem* item : staleItems)
        deleteItem(item);

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
            deleteItem(childItem);
            --i;
            continue;
        }

        invalidateSubtree(childItem, childPrim);
    }
}

bool
OutlinerTreePrivate::remapSubtreePaths(const SdfPath& fromPath, const SdfPath& toPath)
{
    PrimItem* rootItem = itemFromPath(fromPath);
    if (!rootItem)
        return false;

    const SdfPath oldParentPath = fromPath.GetParentPath();
    const SdfPath newParentPath = toPath.GetParentPath();

    if (oldParentPath != newParentPath) {
        PrimItem* newParentItem = itemFromPath(newParentPath);
        if (!newParentItem)
            return false;

        QTreeWidgetItem* oldParentItem = rootItem->parent();
        if (oldParentItem) {
            const int index = oldParentItem->indexOfChild(rootItem);
            if (index < 0)
                return false;

            QTreeWidgetItem* taken = oldParentItem->takeChild(index);
            if (taken != rootItem)
                return false;
        }
        else {
            const int index = d.tree->indexOfTopLevelItem(rootItem);
            if (index < 0)
                return false;

            QTreeWidgetItem* taken = d.tree->takeTopLevelItem(index);
            if (taken != rootItem)
                return false;
        }

        newParentItem->addChild(rootItem);
    }

    std::function<void(PrimItem*)> remap = [&](PrimItem* item) {
        if (!item)
            return;

        const SdfPath oldItemPath = item->path();
        if (!oldItemPath.IsEmpty() && (oldItemPath == fromPath || oldItemPath.HasPrefix(fromPath))) {
            const SdfPath newItemPath = oldItemPath.ReplacePrefix(fromPath, toPath);
            const QString oldKey = qt::SdfPathToQString(oldItemPath);
            const QString newKey = qt::SdfPathToQString(newItemPath);

            auto oldIt = d.itemByPath.find(oldKey);
            if (oldIt != d.itemByPath.end() && oldIt.value() == item)
                d.itemByPath.erase(oldIt);

            item->setPath(newItemPath);
            d.itemByPath.insert(newKey, item);
        }

        item->invalidate();

        for (int i = 0; i < item->childCount(); ++i)
            remap(static_cast<PrimItem*>(item->child(i)));
    };

    remap(rootItem);
    return true;
}

void
OutlinerTreePrivate::refreshParentBranch(const SdfPath& path)
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
OutlinerTreePrivate::updateSelection(const QList<SdfPath>& paths)
{
    SignalGuard::Scope guard(this);
    QSet<PrimItem*> targetItems;
    targetItems.reserve(paths.size());

    {
        for (const SdfPath& path : paths) {
            if (path.IsEmpty())
                continue;

            if (PrimItem* item = itemFromPath(path)) {
                targetItems.insert(item);
                continue;
            }

            if (!d.payloadEnabled)
                continue;

            SdfPath ancestorPath = path.GetParentPath();

            while (!ancestorPath.IsEmpty()) {
                PrimItem* ancestorItem = itemFromPath(ancestorPath);

                if (ancestorItem) {
                    if (ancestorItem->childCount() == 0)
                        targetItems.insert(ancestorItem);

                    break;
                }

                if (ancestorPath == SdfPath::AbsoluteRootPath())
                    break;

                ancestorPath = ancestorPath.GetParentPath();
            }
        }
    }
    // Pointer-backed virtual selection avoids constructing thousands of QString paths.
    // TreeWidget invalidates this cache before structural changes can delete items.
    QSet<QTreeWidgetItem*> selectedItems;
    QSet<QTreeWidgetItem*> ancestorItems;
    {
        selectedItems.reserve(targetItems.size());
        ancestorItems.reserve(targetItems.size());
        {
            for (PrimItem* item : targetItems) {
                selectedItems.insert(item);
                for (QTreeWidgetItem* parent = item->parent(); parent; parent = parent->parent()) {
                    if (ancestorItems.contains(parent))
                        break;
                    ancestorItems.insert(parent);
                }
            }
        }
    }
    {
        if (QItemSelectionModel* model = d.tree->selectionModel())
            model->clearSelection();
    }
    {
        d.tree->setSelectionItems(selectedItems, ancestorItems);
    }
    if (paths.isEmpty())
        d.tree->setCurrentItem(nullptr);
    d.tree->viewport()->update();
}

PrimItem*
OutlinerTreePrivate::itemFromPath(const SdfPath& path) const
{
    if (path.IsEmpty())
        return nullptr;

    return d.itemByPath.value(qt::SdfPathToQString(path), nullptr);
}

OutlinerTree::OutlinerTree(QWidget* parent)
    : IndexTree(parent)
    , p(new OutlinerTreePrivate())
{
    p->d.tree = this;
    p->init();
    setItemDelegate(new OutlinerTreePrivate::OutlinerTreeItemDelegate(this));
    auto* tabFilter = new OutlinerTreePrivate::OutlinerTreeTabFilter(this);
    installEventFilter(tabFilter);
}

OutlinerTree::~OutlinerTree() = default;

ViewContext*
OutlinerTree::context() const
{
    return p->d.context;
}

void
OutlinerTree::setContext(ViewContext* context)
{
    if (p->d.context == context)
        return;

    if (p->d.context && p->d.context->selectionList())
        disconnect(p->d.context->selectionList(), nullptr, p.data(), nullptr);

    p->d.context = context;
    p->initContext();
}

void
OutlinerTree::close()
{
    p->close();
}

void
OutlinerTree::collapse()
{
    p->collapse();
}

void
OutlinerTree::expand()
{
    p->expand();
}

void
OutlinerTree::reveal(const SdfPath& path)
{
    p->reveal(path);
}

void
OutlinerTree::expandDepth(int targetDepth, const SdfPath& path)
{
    p->expandDepth(targetDepth, path);
}

int
OutlinerTree::maxDepth(const SdfPath& path) const
{
    return p->maxDepth(path);
}

int
OutlinerTree::depth(const SdfPath& path) const
{
    return p->depth(path);
}

QString
OutlinerTree::filter() const
{
    return p->d.filter;
}

void
OutlinerTree::setFilter(const QString& filter)
{
    if (filter != p->d.filter) {
        p->d.filter = filter;
        p->updateFilter();
    }
}

bool
OutlinerTree::payloadEnabled() const
{
    return p->d.payloadEnabled;
}

void
OutlinerTree::setPayloadEnabled(bool enabled)
{
    if (p->d.payloadEnabled != enabled)
        p->d.payloadEnabled = enabled;
}

void
OutlinerTree::updateMask(const QList<SdfPath>& paths)
{
    if (p->d.maskPaths != paths)
        p->d.maskPaths = paths;
}

void
OutlinerTree::updateStage(UsdStageRefPtr stage)
{
    p->updateStage(stage);
}

void
OutlinerTree::updatePrims(const NoticeBatch& batch)
{
    p->updatePrims(batch);
}

void
OutlinerTree::contextMenuEvent(QContextMenuEvent* event)
{
    p->contextMenuEvent(event);
}

void
OutlinerTree::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_A && event->modifiers() == Qt::ControlModifier) {
        selectAll();
        event->accept();
        return;
    }
    IndexTree::keyPressEvent(event);
}

void
OutlinerTree::mousePressEvent(QMouseEvent* event)
{
    p->d.dragPressCandidate = false;
    QTreeWidgetItem* item = itemAt(event->pos());
    if (item) {
        const int column = columnAt(event->pos().x());
        if (column == PrimItem::Visibility) {
            auto* primItem = static_cast<PrimItem*>(item);
            if (primItem->supportsVisibility())
                p->toggleVisible(primItem);
            event->accept();
            return;
        }
    }

    IndexTree::mousePressEvent(event);

    // Pointer-backed selection does not populate QItemSelectionModel. Qt's
    // default drag detection depends on native selection, so remember the
    // pressed row and start its drag explicitly after the drag threshold.
    if (event->button() == Qt::LeftButton && item && columnAt(event->pos().x()) == PrimItem::Name
        && selectedItems().contains(item)) {
        p->d.dragPressPosition = event->pos();
        p->d.dragPressCandidate = true;
    }
}

void
OutlinerTree::mouseReleaseEvent(QMouseEvent* event)
{
    p->d.dragPressCandidate = false;
    IndexTree::mouseReleaseEvent(event);
}

void
OutlinerTree::startDrag(Qt::DropActions supportedActions)
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
OutlinerTree::dragEnterEvent(QDragEnterEvent* event)
{
    if (!event->mimeData()->hasFormat(mime::primPath)) {
        p->clearDropIndicator();
        event->ignore();
        return;
    }
    event->acceptProposedAction();
}

void
OutlinerTree::dragMoveEvent(QDragMoveEvent* event)
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

    int mode = OutlinerTreePrivate::DropOnItem;
    if (y < margin)
        mode = OutlinerTreePrivate::DropAboveItem;
    else if (y > rect.height() - margin)
        mode = OutlinerTreePrivate::DropBelowItem;

    p->setDropIndicator(target, mode);
    event->acceptProposedAction();
}

void
OutlinerTree::dropEvent(QDropEvent* event)
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

    if (mode == OutlinerTreePrivate::DropOnItem) {
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

        insertIndex = (mode == OutlinerTreePrivate::DropAboveItem) ? targetRow : (targetRow + 1);
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
        context()->execute(new Command(movePath(fromPaths, newParentPath, insertIndex)));

    event->acceptProposedAction();
}

void
OutlinerTree::mouseMoveEvent(QMouseEvent* event)
{
    if (p->d.dragPressCandidate) {
        if (!(event->buttons() & Qt::LeftButton)) {
            p->d.dragPressCandidate = false;
        }
        else if ((event->pos() - p->d.dragPressPosition).manhattanLength() >= QApplication::startDragDistance()) {
            p->d.dragPressCandidate = false;
            startDrag(Qt::MoveAction);
            event->accept();
            return;
        }
    }
    TreeWidget::mouseMoveEvent(event);
}

}  // namespace stageviz
