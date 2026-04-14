// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#include "stagetree.h"
#include "application.h"
#include "command.h"
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
#include <QClipboard>
#include <QContextMenuEvent>
#include <QDrag>
#include <QFileInfo>
#include <QHeaderView>
#include <QKeyEvent>
#include <QMenu>
#include <QMimeData>
#include <QPainter>
#include <QPointer>
#include <QStyledItemDelegate>
#include <QTimer>
#include <functional>
#include <pxr/usd/sdf/payload.h>
#include <pxr/usd/sdf/primSpec.h>
#include <pxr/usd/sdf/variantSetSpec.h>
#include <pxr/usd/sdf/variantSpec.h>
#include <pxr/usd/usd/prim.h>

PXR_NAMESPACE_USING_DIRECTIVE

namespace stageviz {

class StageTreePrivate : public QObject, public SignalGuard {
public:
    StageTreePrivate();
    void init();
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

public Q_SLOTS:
    void itemSelectionChanged();
    void checkStateChanged(PrimItem* item);
    void nameChanged(PrimItem* item);

public:
    void updatePrim(const SdfPath& path);
    void invalidatePrim(const SdfPath& path);
    void invalidatePrimImpl(const SdfPath& path, bool refreshParent);
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
        QTreeWidgetItem* root = d.tree->topLevelItem(0);

        std::function<void(QTreeWidgetItem*, int)> expandNode = [&](QTreeWidgetItem* item, int depthValue) {
            item->setExpanded(depthValue < targetDepth);
            for (int i = 0; i < item->childCount(); ++i)
                expandNode(item->child(i), depthValue + 1);
        };
        expandNode(root, 0);
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
            parent->setExpanded(parentDepthValue < targetDepth);
            parent = parent->parent();
            parentDepthValue--;
        }

        std::function<void(QTreeWidgetItem*, int)> expandNode = [&](QTreeWidgetItem* node, int depthValue) {
            node->setExpanded(depthValue < targetDepth);
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
    QTreeWidgetItem* root = nullptr;
    if (d.tree->topLevelItemCount() > 0)
        root = d.tree->topLevelItem(0);
    if (!root)
        return 0;

    std::function<int(QTreeWidgetItem*, int)> subtreeDepth = [&](QTreeWidgetItem* item, int currentDepth) {
        int max = currentDepth;
        for (int i = 0; i < item->childCount(); ++i)
            max = std::max(max, subtreeDepth(item->child(i), currentDepth + 1));
        return max;
    };

    if (path.IsEmpty())
        return subtreeDepth(root, 0);

    QTreeWidgetItem* item = itemFromPath(path);
    if (!item)
        return subtreeDepth(root, 0);

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
    QList<SdfPath> paths;
    QList<PrimItem*> selectedItems;
    selectedItems.reserve(d.tree->selectedItems().size());

    QStringList pathStrings;
    QStringList nameStrings;

    for (QTreeWidgetItem* selected : d.tree->selectedItems()) {
        PrimItem* primItem = static_cast<PrimItem*>(selected);
        const QString pathString = primItem->data(0, PrimItem::Path).toString();
        if (pathString.isEmpty())
            continue;

        const SdfPath path(qt::QStringToString(pathString));
        paths.append(path);
        selectedItems.append(primItem);
        pathStrings.append(pathString);
        nameStrings.append(qt::StringToQString(path.GetName()));
    }

    if (paths.isEmpty())
        return;

    const QList<SdfPath> topLevelPaths = path::topLevelPaths(paths);

    SdfPath createParentPath;
    if (!topLevelPaths.isEmpty())
        createParentPath = topLevelPaths.first();

    auto payloadPathsAtSelection = [&]() {
        QList<SdfPath> result;
        READ_LOCKER(locker, d.context->stageLock(), "stageLock");
        if (!d.stage)
            return result;
        return stage::payloadPaths(d.stage, topLevelPaths);
    };

    auto payloadClipboardText = [&]() {
        QStringList lines;

        READ_LOCKER(locker, d.context->stageLock(), "stageLock");
        if (!d.stage)
            return QString();

        for (const SdfPath& path : paths) {
            if (!stage::isPayload(d.stage, path))
                continue;

            const UsdPrim prim = d.stage->GetPrimAtPath(path);
            if (!prim || !prim.IsValid())
                continue;

            QStringList payloadFiles;
            QMap<QString, QString> variantPayloadFiles;

            const SdfPrimSpecHandleVector primStack = prim.GetPrimStack();
            for (const SdfPrimSpecHandle& spec : primStack) {
                if (!spec)
                    continue;

                auto appendPayloadFiles = [&](const SdfPayloadVector& items) {
                    for (const SdfPayload& payload : items) {
                        const QString assetPath = qt::StringToQString(payload.GetAssetPath());
                        if (assetPath.isEmpty())
                            continue;
                        payloadFiles.append(QFileInfo(assetPath).fileName());
                    }
                };

                appendPayloadFiles(spec->GetPayloadList().GetAppliedItems());

                const auto variantSets = spec->GetVariantSets();
                for (const auto& setIt : variantSets) {
                    const std::string& setName = setIt.first;
                    const SdfVariantSetSpecHandle& setSpec = setIt.second;
                    if (!setSpec)
                        continue;

                    const auto variants = setSpec->GetVariants();
                    for (const SdfVariantSpecHandle& variantSpec : variants) {
                        if (!variantSpec)
                            continue;

                        const std::string variantName = variantSpec->GetName();

                        const SdfPrimSpecHandle variantPrimSpec = variantSpec->GetPrimSpec();
                        if (!variantPrimSpec)
                            continue;

                        const SdfPayloadVector variantItems = variantPrimSpec->GetPayloadList().GetAppliedItems();

                        for (const SdfPayload& payload : variantItems) {
                            const QString assetPath = qt::StringToQString(payload.GetAssetPath());
                            if (assetPath.isEmpty())
                                continue;

                            const QString key = QString("%1=%2").arg(qt::StringToQString(setName),
                                                                     qt::StringToQString(variantName));
                            variantPayloadFiles[key] = QFileInfo(assetPath).fileName();
                        }
                    }
                }
            }

            payloadFiles.removeDuplicates();

            if (!variantPayloadFiles.isEmpty()) {
                for (auto it = variantPayloadFiles.begin(); it != variantPayloadFiles.end(); ++it)
                    lines.append(QString("%1:%2").arg(it.key(), it.value()));
            }
            else {
                for (const QString& file : payloadFiles)
                    lines.append(file);
            }
        }

        return lines.join('\n');
    };

    bool hasExactPayloadSelection = false;
    {
        READ_LOCKER(locker, d.context->stageLock(), "stageLock");
        if (d.stage) {
            for (const SdfPath& path : paths) {
                if (stage::isPayload(d.stage, path)) {
                    hasExactPayloadSelection = true;
                    break;
                }
            }
        }
    }

    const bool isolateChecked = maskContainsCurrentSelection(d.maskPaths, paths);

    bool canLoadSelected = false;
    bool canUnloadSelected = false;

    if (d.payloadEnabled) {
        for (PrimItem* item : selectedItems) {
            const Qt::CheckState state = item->checkState(PrimItem::Name);
            if (state == Qt::Unchecked || state == Qt::PartiallyChecked)
                canLoadSelected = true;
            if (state == Qt::Checked || state == Qt::PartiallyChecked)
                canUnloadSelected = true;
        }
    }
    else {
        const QList<SdfPath> exactPayloadPaths = payloadPathsAtSelection();
        canLoadSelected = !exactPayloadPaths.isEmpty();
        canUnloadSelected = !exactPayloadPaths.isEmpty();
    }

    bool canShowSelected = false;
    bool canHideSelected = false;
    {
        READ_LOCKER(locker, d.context->stageLock(), "stageLock");
        if (d.stage) {
            for (const SdfPath& path : paths) {
                const bool visible = stage::isVisible(d.stage, path);
                if (visible)
                    canHideSelected = true;
                else
                    canShowSelected = true;

                if (canShowSelected && canHideSelected)
                    break;
            }
        }
    }

    auto setSelectedCheckState = [&](Qt::CheckState state) {
        for (PrimItem* item : selectedItems)
            item->setCheckState(PrimItem::Name, state);
    };

    auto copyToClipboard = [](const QString& text) {
        if (QClipboard* clipboard = QGuiApplication::clipboard())
            clipboard->setText(text);
    };

    QMenu menu(d.tree.data());
    struct VariantSelection {
        QString setName;
        QString value;
    };

    QMenu* loadVariantMenu = menu.addMenu("Load Variant");
    QMap<QAction*, VariantSelection> variantActions;
    {
        READ_LOCKER(locker, d.context->stageLock(), "stageLock");
        if (!d.stage)
            return;

        const QMap<QString, QList<QString>> variantSets = stage::findVariantSets(d.stage, paths, false);
        for (auto it = variantSets.begin(); it != variantSets.end(); ++it) {
            const QString& setName = it.key();
            const QList<QString>& values = it.value();

            QMenu* setMenu = loadVariantMenu->addMenu(setName);
            for (const QString& value : values) {
                QAction* action = setMenu->addAction(value);
                variantActions[action] = { setName, value };
            }
        }

        if (variantActions.isEmpty())
            loadVariantMenu->setEnabled(false);
    }

    menu.addSeparator();

    QAction* loadSelected = menu.addAction("Load");
    QAction* unloadSelected = menu.addAction("Unload");
    loadSelected->setEnabled(canLoadSelected);
    unloadSelected->setEnabled(canUnloadSelected);

    menu.addSeparator();

    QMenu* showMenu = menu.addMenu("Show");
    QAction* showSelected = showMenu->addAction("Selected");
    QAction* showRecursive = showMenu->addAction("Recursive");
    showSelected->setEnabled(canShowSelected);
    showRecursive->setEnabled(canShowSelected);

    QMenu* hideMenu = menu.addMenu("Hide");
    QAction* hideSelected = hideMenu->addAction("Selected");
    QAction* hideRecursive = hideMenu->addAction("Recursive");
    hideSelected->setEnabled(canHideSelected);
    hideRecursive->setEnabled(canHideSelected);

    menu.addSeparator();

    QAction* isolateAction = menu.addAction("Isolate");
    isolateAction->setCheckable(true);
    isolateAction->setChecked(isolateChecked);

    menu.addSeparator();

    QMenu* copyMenu = menu.addMenu("Copy");
    QAction* copyPath = copyMenu->addAction("Path");
    QAction* copyName = copyMenu->addAction("Name");
    QAction* copyPayload = nullptr;
    QAction* copyPaths = nullptr;
    QAction* copyNames = nullptr;

    if (hasExactPayloadSelection)
        copyPayload = copyMenu->addAction("Payload");

    if (paths.size() > 1) {
        copyPaths = copyMenu->addAction("Paths");
        copyNames = copyMenu->addAction("Names");
    }

    menu.addSeparator();

    QAction* newXform = menu.addAction("New xform");
    QAction* deleteSelected = menu.addAction("Delete");

    QAction* chosen = menu.exec(d.tree->mapToGlobal(event->pos()));
    if (!chosen)
        return;

    if (chosen == copyPath) {
        copyToClipboard(pathStrings.first());
        return;
    }

    if (chosen == copyName) {
        copyToClipboard(nameStrings.first());
        return;
    }

    if (chosen == copyPayload) {
        const QString text = payloadClipboardText();
        if (!text.isEmpty())
            copyToClipboard(text);
        return;
    }

    if (chosen == copyPaths) {
        copyToClipboard(pathStrings.join('\n'));
        return;
    }

    if (chosen == copyNames) {
        copyToClipboard(nameStrings.join('\n'));
        return;
    }

    if (chosen == loadSelected) {
        if (d.payloadEnabled) {
            setSelectedCheckState(Qt::Checked);
        }
        else {
            const QList<SdfPath> payloadPaths = payloadPathsAtSelection();
            if (!payloadPaths.isEmpty())
                d.context->run(new Command(loadPayloads(payloadPaths)));
        }
        return;
    }

    if (chosen == unloadSelected) {
        if (d.payloadEnabled) {
            setSelectedCheckState(Qt::Unchecked);
        }
        else {
            const QList<SdfPath> payloadPaths = payloadPathsAtSelection();
            if (!payloadPaths.isEmpty())
                d.context->run(new Command(unloadPayloads(payloadPaths)));
        }
        return;
    }

    if (variantActions.contains(chosen)) {
        const QList<SdfPath> payloadPaths = payloadPathsAtSelection();
        if (!payloadPaths.isEmpty()) {
            const VariantSelection& sel = variantActions[chosen];
            d.context->run(new Command(loadPayloads(payloadPaths, sel.setName, sel.value)));
        }
        return;
    }

    if (chosen == newXform) {
        if (!createParentPath.IsEmpty())
            d.context->run(new Command(newXformPath(createParentPath, "Xform")));
        return;
    }

    if (chosen == showSelected)
        d.context->run(new Command(showPaths(paths, false)));
    else if (chosen == showRecursive)
        d.context->run(new Command(showPaths(paths, true)));
    else if (chosen == hideSelected)
        d.context->run(new Command(hidePaths(paths, false)));
    else if (chosen == hideRecursive)
        d.context->run(new Command(hidePaths(paths, true)));
    else if (chosen == isolateAction)
        d.context->run(new Command(isolatePaths(isolateAction->isChecked() ? paths : QList<SdfPath>())));
    else if (chosen == deleteSelected)
        d.context->run(new Command(deletePaths(paths)));
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
        itemCheckState(rootItem, true, true);
}

void
StageTreePrivate::syncDirectChildrenOnly(PrimItem* parentItem, const UsdPrim& parentPrim)
{
    if (!parentItem || !parentPrim)
        return;

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
        itemCheckState(childItem, d.payloadEnabled, false);

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

        if (!oldParent.IsEmpty() && oldParent != SdfPath::AbsoluteRootPath())
            handledParents.insert(oldParent);
        if (!newParent.IsEmpty() && newParent != SdfPath::AbsoluteRootPath())
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
            parentPrim = d.stage->GetPrimAtPath(parentPath);
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
StageTreePrivate::invalidatePrimImpl(const SdfPath& path, bool refreshParent)
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

        if (refreshParent && parentItem) {
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

        primItem = addItem(parentItem, primPath);
        if (!primItem)
            return;

        if (refreshParent) {
            invalidateChildren(parentItem, prim.GetParent());
            parentItem->invalidate();
        }
    }

    invalidateSubtree(primItem, prim);
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
    invalidatePrimImpl(path, true);
}

void
StageTreePrivate::invalidateChildren(PrimItem* parentItem, const UsdPrim& prim)
{
    if (!parentItem || !prim)
        return;

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
    if (parentPath.IsEmpty() || parentPath == SdfPath::AbsoluteRootPath())
        return;

    PrimItem* parentItem = itemFromPath(parentPath);
    if (!parentItem)
        return;

    UsdPrim parentPrim;
    {
        READ_LOCKER(locker, d.context->stageLock(), "stageLock");
        if (!d.stage)
            return;
        parentPrim = d.stage->GetPrimAtPath(parentPath);
    }

    if (!parentPrim)
        return;

    invalidateChildren(parentItem, parentPrim);
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
    if (p->d.context != context)
        p->d.context = context;
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
StageTree::updateSelection(const QList<SdfPath>& paths)
{
    p->updateSelection(paths);
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
    const int column = columnAt(event->pos().x());
    if (!item) {
        clearSelection();
        Q_EMIT itemSelectionChanged();
        return;
    }

    if (column == PrimItem::Vis) {
        p->toggleVisible(static_cast<PrimItem*>(item));
        event->accept();
        return;
    }

    QTreeWidget::mousePressEvent(event);
}

void
StageTree::startDrag(Qt::DropActions supportedActions)
{
    Q_UNUSED(supportedActions);

    auto* item = static_cast<PrimItem*>(currentItem());
    if (!item)
        return;

    const QString pathString = item->data(0, PrimItem::Path).toString();
    if (pathString.isEmpty())
        return;

    auto* mime = new QMimeData();
    mime->setData(mime::primPath, pathString.toUtf8());

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

    const QString fromPathString = QString::fromUtf8(event->mimeData()->data(mime::primPath));
    const QString targetPathString = targetItem->data(0, PrimItem::Path).toString();

    if (fromPathString.isEmpty() || targetPathString.isEmpty()) {
        p->clearDropIndicator();
        event->ignore();
        return;
    }

    const SdfPath fromPath(QStringToString(fromPathString));
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

    if (newParentPath.IsEmpty() || newParentPath == SdfPath::AbsoluteRootPath()) {
        event->ignore();
        return;
    }

    if (fromPath == newParentPath || newParentPath.HasPrefix(fromPath)) {
        event->ignore();
        return;
    }

    if (fromPath.GetParentPath() == newParentPath) {
        QTreeWidgetItem* sourceItem = nullptr;

        std::function<QTreeWidgetItem*(QTreeWidgetItem*)> findItem = [&](QTreeWidgetItem* item) -> QTreeWidgetItem* {
            if (!item)
                return nullptr;

            if (item->data(0, PrimItem::Path).toString() == fromPathString)
                return item;

            for (int i = 0; i < item->childCount(); ++i) {
                if (QTreeWidgetItem* found = findItem(item->child(i)))
                    return found;
            }
            return nullptr;
        };

        for (int i = 0; i < topLevelItemCount() && !sourceItem; ++i)
            sourceItem = findItem(topLevelItem(i));

        if (sourceItem) {
            QTreeWidgetItem* oldParentItem = sourceItem->parent();
            if (oldParentItem) {
                const int oldRow = oldParentItem->indexOfChild(sourceItem);
                if (oldRow >= 0 && oldRow < insertIndex)
                    --insertIndex;
            }
        }
    }

    if (context())
        context()->run(new Command(movePath(fromPath, newParentPath, insertIndex)));

    event->acceptProposedAction();
}

void
StageTree::mouseMoveEvent(QMouseEvent* event)
{
    QTreeWidget::mouseMoveEvent(event);
}

}  // namespace stageviz
