// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#include "layertree.h"
#include "application.h"
#include "command.h"
#include "commandstack.h"
#include "layeritem.h"
#include "mime.h"
#include "selectionlist.h"
#include "session.h"
#include "style.h"
#include "tracelocks.h"
#include <QContextMenuEvent>
#include <QDebug>
#include <QDrag>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QHash>
#include <QHeaderView>
#include <QMenu>
#include <QMimeData>
#include <QMouseEvent>
#include <QSet>
#include <QTimer>
#include <QTreeWidgetItem>
#include <QTreeWidgetItemIterator>
#include <algorithm>
#include <functional>
#include <pxr/usd/sdf/layer.h>
#include <pxr/usd/usd/stage.h>

PXR_NAMESPACE_USING_DIRECTIVE

namespace stageviz {

namespace {
    LayerItem* layerItem(QTreeWidgetItem* item) { return static_cast<LayerItem*>(item); }

    const LayerItem* layerItem(const QTreeWidgetItem* item) { return static_cast<const LayerItem*>(item); }

}  // namespace

class LayerTreePrivate : public QObject {
public:
    explicit LayerTreePrivate(LayerTree* tree)
        : d(tree)
    {
        if (session()) {
            connect(session(), &Session::stageChanged, this,
                    [this](UsdStageRefPtr stage, Session::LoadPolicy, Session::StageStatus) {
                        rootChanged = false;
                        const QString rootIdentifier = stage && stage->GetRootLayer() ? QString::fromStdString(
                                                           stage->GetRootLayer()->GetIdentifier())
                                                                                      : QString();
                        if (!rootIdentifier.isEmpty() && !retainedRootIdentifier.isEmpty()
                            && rootIdentifier != retainedRootIdentifier && d) {
                            // Saving an anonymous root replaces the stage with a
                            // file-backed stage. Keep the root item itself so Qt
                            // retains its expanded/current state, just as the
                            // StageTree retains its prim rows across stage swaps.
                            QTreeWidgetItemIterator itemIt(d);
                            while (*itemIt) {
                                LayerItem* item = layerItem(*itemIt);
                                if (item->identifier() == retainedRootIdentifier) {
                                    item->setIdentifier(rootIdentifier);
                                    break;
                                }
                                ++itemIt;
                            }
                        }
                        if (rootIdentifier != retainedRootIdentifier)
                            retainedLayerHandles.clear();
                        retainedRootIdentifier = rootIdentifier;
                        refresh();
                    });
            connect(session(), &Session::editLayerChanged, this, [this](SdfLayerHandle) { refresh(); });
            connect(session(), &Session::layerMutingChanged, this,
                    [this](const QStringList&, const QStringList&) { refresh(); });
            connect(session(), &Session::primsChanged, this, [this](const NoticeBatch& batch) {
                if (!batch.entries.empty()) {
                    bool editTargetIsRoot = false;
                    {
                        READ_LOCKER(locker, session()->stageLock(), "stageLock");
                        const UsdStageRefPtr stage = session()->stageUnsafe();
                        editTargetIsRoot = stage && stage->GetEditTarget().GetLayer() == stage->GetRootLayer();
                    }
                    if (editTargetIsRoot) {
                        rootChanged = true;
                    }
                }
                refresh();
            });
            connect(session()->commandStack(), &CommandStack::changed, this, [this]() { refresh(); });
        }
        refresh();
    }

    void refresh()
    {
        if (!d || !session()) {
            return;
        }

        refreshing = true;
        if (!headerConfigured || d->columnCount() != 4 || !d->headerItem()
            || d->headerItem()->text(LayerItem::Name) != tr("Name")) {
            d->setColumnCount(4);
            d->setHeaderLabels({ tr("Name"), QString(), QString(), QString() });
            d->setHeaderHidden(false);
            d->header()->setStretchLastSection(false);
            d->header()->setSectionResizeMode(LayerItem::Name, QHeaderView::Stretch);
            d->header()->setSectionResizeMode(LayerItem::Visibility, QHeaderView::Fixed);
            d->header()->setSectionResizeMode(LayerItem::Opinions, QHeaderView::Fixed);
            d->header()->setSectionResizeMode(LayerItem::Permission, QHeaderView::Fixed);
            d->setColumnWidth(LayerItem::Visibility, 28);
            d->setColumnWidth(LayerItem::Opinions, 28);
            d->setColumnWidth(LayerItem::Permission, 28);
            headerConfigured = true;
        }

        std::vector<SdfLayerHandle> layers;
        SdfLayerHandle editLayer;
        SdfLayerHandle rootLayer;
        std::vector<std::string> mutedLayers;
        QSet<QString> directlyMutedIdentifiers;
        QSet<QString> layerIdentifiers;
        QSet<QString> activeLayerIdentifiers;
        struct LayerInfo {
            SdfLayerHandle layer;
            QString identifier;
            QString name;
            QString tooltip;
            bool dirty = false;
            bool saveable = false;
            bool editable = true;
            bool muted = false;
            bool directlyMuted = false;
            bool canMute = false;
            QString parentIdentifier;
            int siblingIndex = -1;
            bool canReorder = false;
            int primSpecCount = 0;
        };
        std::vector<LayerInfo> layerInfo;
        {
            READ_LOCKER(locker, session()->stageLock(), "stageLock");
            const UsdStageRefPtr stage = session()->stageUnsafe();
            if (stage) {
                layers = stage->GetLayerStack(false);
                editLayer = stage->GetEditTarget().GetLayer();
                rootLayer = stage->GetRootLayer();
                if (rootLayer && std::find(layers.begin(), layers.end(), rootLayer) == layers.end()) {
                    qWarning() << "[LayerTree] root layer missing from layer stack; adding it explicitly:"
                               << QString::fromStdString(rootLayer->GetIdentifier());
                    layers.insert(layers.begin(), rootLayer);
                }
                mutedLayers = stage->GetMutedLayers();
                for (const std::string& identifier : mutedLayers)
                    directlyMutedIdentifiers.insert(QString::fromStdString(identifier));

                for (const SdfLayerHandle& layer : layers) {
                    if (!layer)
                        continue;
                    const QString identifier = QString::fromStdString(layer->GetIdentifier());
                    layerIdentifiers.insert(identifier);
                    activeLayerIdentifiers.insert(identifier);
                    retainedLayerHandles.insert(identifier, SdfLayerRefPtr(layer));
                }

                QSet<QString> expandedMutedSubtree;
                std::function<void(const SdfLayerHandle&)> appendMutedSubtree = [&](const SdfLayerHandle& layer) {
                    if (!layer)
                        return;
                    const QString identifier = QString::fromStdString(layer->GetIdentifier());
                    if (expandedMutedSubtree.contains(identifier))
                        return;
                    expandedMutedSubtree.insert(identifier);
                    retainedLayerHandles.insert(identifier, SdfLayerRefPtr(layer));
                    if (!layerIdentifiers.contains(identifier)) {
                        layers.push_back(layer);
                        layerIdentifiers.insert(identifier);
                    }
                    for (const std::string& subLayerPath : layer->GetSubLayerPaths())
                        appendMutedSubtree(SdfLayer::FindRelativeToLayer(layer, subLayerPath));
                };
                for (const std::string& identifier : mutedLayers) {
                    const QString layerIdentifier = QString::fromStdString(identifier);
                    SdfLayerRefPtr mutedReference = retainedLayerHandles.value(layerIdentifier);
                    if (!mutedReference)
                        mutedReference = SdfLayer::Find(identifier);
                    if (!mutedReference)
                        mutedReference = SdfLayer::FindOrOpen(identifier);
                    appendMutedSubtree(mutedReference);
                }

                for (const SdfLayerHandle& layer : layers) {
                    if (!layer)
                        continue;

                    LayerInfo info;
                    info.layer = layer;
                    info.identifier = QString::fromStdString(layer->GetIdentifier());
                    info.name = QString::fromStdString(layer->GetDisplayName());
                    if (info.name.isEmpty())
                        info.name = info.identifier;
                    info.dirty = layer->IsDirty();
                    info.saveable = layer->PermissionToSave() && !layer->IsAnonymous();
                    info.editable = layer->PermissionToEdit();
                    info.directlyMuted = directlyMutedIdentifiers.contains(info.identifier);
                    info.muted = info.directlyMuted;
                    info.canMute = layer != rootLayer
                                   && (info.directlyMuted || (stage->HasLocalLayer(layer) && layer != editLayer));
                    for (const SdfPrimSpecHandle& rootPrim : layer->GetRootPrims())
                        info.primSpecCount += LayerItem::countPrimSpecs(rootPrim);
                    info.tooltip = info.identifier;
                    if (layer->IsAnonymous())
                        info.tooltip += tr("\nAnonymous layer");
                    else if (!info.saveable)
                        info.tooltip += tr("\nRead-only layer");
                    if (info.dirty)
                        info.tooltip += tr("\nHas unsaved changes");
                    layerInfo.push_back(info);
                }

                for (LayerInfo& info : layerInfo) {
                    if (info.layer == rootLayer)
                        continue;
                    SdfLayerHandle parent;
                    bool ambiguousParent = false;
                    for (const LayerInfo& candidate : layerInfo) {
                        if (candidate.layer == info.layer)
                            continue;
                        const std::vector<std::string>& subLayerPaths = candidate.layer->GetSubLayerPaths();
                        for (int siblingIndex = 0; siblingIndex < int(subLayerPaths.size()); ++siblingIndex) {
                            const std::string& subLayerPath = subLayerPaths[siblingIndex];
                            const SdfLayerHandle child = SdfLayer::FindRelativeToLayer(candidate.layer, subLayerPath);
                            if (child == info.layer) {
                                if (parent && parent != candidate.layer) {
                                    ambiguousParent = true;
                                    break;
                                }
                                parent = candidate.layer;
                                info.siblingIndex = siblingIndex;
                            }
                        }
                        if (ambiguousParent)
                            break;
                    }
                    if (parent && !ambiguousParent) {
                        info.parentIdentifier = QString::fromStdString(parent->GetIdentifier());
                        info.canReorder = stage->HasLocalLayer(parent) && parent->PermissionToEdit();
                        if (info.canReorder)
                            info.tooltip += tr("\nDrag above or below a sibling to change layer strength.");
                    }
                }

                for (LayerInfo& info : layerInfo) {
                    QString ancestorIdentifier = info.parentIdentifier;
                    QSet<QString> visited;
                    while (!ancestorIdentifier.isEmpty() && !visited.contains(ancestorIdentifier)) {
                        visited.insert(ancestorIdentifier);
                        if (directlyMutedIdentifiers.contains(ancestorIdentifier)
                            && !activeLayerIdentifiers.contains(info.identifier)) {
                            info.muted = true;
                            break;
                        }
                        const auto ancestor = std::find_if(layerInfo.begin(), layerInfo.end(),
                                                           [&](const LayerInfo& candidate) {
                                                               return candidate.identifier == ancestorIdentifier;
                                                           });
                        if (ancestor == layerInfo.end())
                            break;
                        ancestorIdentifier = ancestor->parentIdentifier;
                    }
                    if (info.muted && !info.directlyMuted) {
                        info.canMute = false;
                        info.tooltip += tr("\nMuted because an ancestor layer is muted");
                    }
                }
            }
            else {}
        }

        // Keep existing items when their layer still exists. Reusing the item
        // preserves its selection and expansion state across layer notices.
        const QString previousCurrentIdentifier = currentIdentifier();
        QHash<QString, LayerItem*> items;
        QHash<QString, bool> expanded;
        QTreeWidgetItemIterator existing(d);
        while (*existing) {
            LayerItem* item = layerItem(*existing);
            const QString identifier = item->identifier();
            if (!identifier.isEmpty()) {
                items.insert(identifier, item);
                expanded.insert(identifier, item->isExpanded());
            }
            ++existing;
        }

        LayerItem* editItem = nullptr;
        for (const LayerInfo& info : layerInfo) {
            QString name = info.name;
            if (info.layer == rootLayer && session()->filename().isEmpty())
                name = tr("Untitled");
            const bool showDirty = info.dirty
                                   && (info.layer != rootLayer || !session()->filename().isEmpty() || rootChanged);
            if (showDirty)
                name.prepend(QLatin1Char('*'));
            if (info.layer == rootLayer)
                name += tr("  (Root)");


            LayerItem* item = items.value(info.identifier, nullptr);
            const bool isNew = item == nullptr;
            if (isNew) {
                item = new LayerItem(d);
                items.insert(info.identifier, item);
            }
            LayerItem::State itemState;
            itemState.identifier = info.identifier;
            itemState.parentIdentifier = info.parentIdentifier;
            itemState.name = name;
            itemState.tooltip = info.tooltip;
            itemState.primSpecCount = info.primSpecCount;
            itemState.root = info.layer == rootLayer;
            itemState.editTarget = info.layer == editLayer;
            itemState.muted = info.muted;
            itemState.directlyMuted = info.directlyMuted;
            itemState.inheritedMuted = info.muted && !info.directlyMuted;
            itemState.canMute = info.canMute;
            itemState.editable = info.editable;
            itemState.canReorder = info.canReorder;
            item->setState(itemState);
            if (info.layer == editLayer)
                editItem = item;
        }

        // Move only rows whose parent changed, retaining the QTreeWidgetItem
        // instances for unaffected layers.
        for (const LayerInfo& info : layerInfo) {
            QTreeWidgetItem* item = items.value(info.identifier, nullptr);
            QTreeWidgetItem* parent = items.value(info.parentIdentifier, nullptr);
            if (parent == item)
                parent = nullptr;

            QTreeWidgetItem* oldParent = item->parent();
            const bool attachedToTree = item->treeWidget() == d;
            if (oldParent == parent && attachedToTree)
                continue;

            if (oldParent) {
                const int index = oldParent->indexOfChild(item);
                if (index >= 0)
                    oldParent->takeChild(index);
            }
            else if (attachedToTree) {
                const int index = d->indexOfTopLevelItem(item);
                if (index >= 0)
                    d->takeTopLevelItem(index);
            }

            if (parent)
                parent->addChild(item);
            else
                d->addTopLevelItem(item);
        }

        // Match USD sibling order without rebuilding the tree.
        auto reorderChildren = [](QTreeWidgetItem* parent, const QList<QTreeWidgetItem*>& desired) {
            for (int targetIndex = 0; targetIndex < desired.size(); ++targetIndex) {
                QTreeWidgetItem* wanted = desired[targetIndex];
                if (parent->child(targetIndex) == wanted)
                    continue;
                const int currentIndex = parent->indexOfChild(wanted);
                if (currentIndex < 0)
                    continue;
                parent->insertChild(targetIndex, parent->takeChild(currentIndex));
            }
        };
        QHash<QString, QList<QPair<int, QTreeWidgetItem*>>> desiredChildren;
        QList<QTreeWidgetItem*> desiredTopLevel;
        for (const LayerInfo& info : layerInfo) {
            QTreeWidgetItem* item = items.value(info.identifier, nullptr);
            if (info.parentIdentifier.isEmpty() || !items.contains(info.parentIdentifier))
                desiredTopLevel.append(item);
            else
                desiredChildren[info.parentIdentifier].append(qMakePair(info.siblingIndex, item));
        }
        for (auto it = desiredChildren.cbegin(); it != desiredChildren.cend(); ++it) {
            QList<QPair<int, QTreeWidgetItem*>> orderedChildren = it.value();
            std::stable_sort(orderedChildren.begin(), orderedChildren.end(),
                             [](const auto& lhs, const auto& rhs) { return lhs.first < rhs.first; });
            QList<QTreeWidgetItem*> desired;
            desired.reserve(orderedChildren.size());
            for (const auto& child : orderedChildren)
                desired.append(child.second);
            reorderChildren(items.value(it.key()), desired);
        }
        for (int targetIndex = 0; targetIndex < desiredTopLevel.size(); ++targetIndex) {
            QTreeWidgetItem* wanted = desiredTopLevel[targetIndex];
            if (d->topLevelItem(targetIndex) == wanted)
                continue;
            const int currentIndex = d->indexOfTopLevelItem(wanted);
            if (currentIndex >= 0)
                d->insertTopLevelItem(targetIndex, d->takeTopLevelItem(currentIndex));
        }

        // Delete rows that no longer exist after surviving descendants have
        // been moved to their new parents.
        QSet<QString> activeIdentifiers;
        for (const LayerInfo& info : layerInfo)
            activeIdentifiers.insert(info.identifier);
        std::function<void(QTreeWidgetItem*)> removeStaleRows = [&](QTreeWidgetItem* item) {
            for (int childIndex = item->childCount() - 1; childIndex >= 0; --childIndex) {
                QTreeWidgetItem* child = item->child(childIndex);
                removeStaleRows(child);
                if (!activeIdentifiers.contains(layerItem(child)->identifier())) {
                    const QString identifier = layerItem(child)->identifier();
                    delete item->takeChild(childIndex);
                }
            }
        };
        for (int index = d->topLevelItemCount() - 1; index >= 0; --index) {
            QTreeWidgetItem* item = d->topLevelItem(index);
            removeStaleRows(item);
            const QString identifier = layerItem(item)->identifier();
            if (!activeIdentifiers.contains(identifier)) {
                delete d->takeTopLevelItem(index);
            }
        }

        // A moved row keeps its expanded flag in Qt, but restore it explicitly
        // for robustness when the tree implementation reparents an item.
        for (auto it = expanded.cbegin(); it != expanded.cend(); ++it) {
            if (activeIdentifiers.contains(it.key())) {
                QTreeWidgetItem* item = items.value(it.key(), nullptr);
                if (item && item->isExpanded() != it.value())
                    item->setExpanded(it.value());
            }
        }

        if (rootLayer) {
            const QString rootIdentifier = QString::fromStdString(rootLayer->GetIdentifier());
            QTreeWidgetItem* rootItem = items.value(rootIdentifier, nullptr);
            if (rootItem && !rootItem->isExpanded()) {
                rootItem->setExpanded(true);
            }
            // Reapply after Qt finishes processing item insertions/reparenting.
            // This also covers a tree first populated while its dialog is hidden.
            QTimer::singleShot(0, d, [tree = d, rootIdentifier]() {
                QTreeWidgetItemIterator itemIt(tree);
                while (*itemIt) {
                    LayerItem* item = layerItem(*itemIt);
                    if (item->identifier() == rootIdentifier) {
                        if (!item->isExpanded()) {
                            item->setExpanded(true);
                        }
                        return;
                    }
                    ++itemIt;
                }
            });
        }

        QTreeWidgetItem* previousCurrent = activeIdentifiers.contains(previousCurrentIdentifier)
                                               ? items.value(previousCurrentIdentifier, nullptr)
                                               : nullptr;
        if (previousCurrent && previousCurrent->treeWidget() == d) {
            if (d->currentItem() != previousCurrent)
                d->setCurrentItem(previousCurrent);
        }
        else if (editItem)
            d->setCurrentItem(editItem);
        d->setEnabled(!layers.empty());
        refreshing = false;
    }

    QTreeWidgetItem* currentItem() const { return d ? d->currentItem() : nullptr; }

    void setDropIndicator(QTreeWidgetItem* item, int mode)
    {
        if (!d)
            return;
        const qulonglong newPointer = reinterpret_cast<qulonglong>(item);
        d->setProperty(mime::dropItemPtrProperty, QVariant::fromValue<qulonglong>(newPointer));
        d->setProperty(mime::dropModeProperty, mode);
        d->viewport()->update();
    }

    void clearDropIndicator()
    {
        if (!d)
            return;
        d->setProperty(mime::dropItemPtrProperty, QVariant::fromValue<qulonglong>(0));
        d->setProperty(mime::dropModeProperty, 0);
        d->viewport()->update();
    }

    QString currentIdentifier() const
    {
        const QTreeWidgetItem* item = currentItem();
        return item ? layerItem(item)->identifier() : QString();
    }

    LayerTree* d = nullptr;
    QHash<QString, SdfLayerRefPtr> retainedLayerHandles;
    QString retainedRootIdentifier;
    bool refreshing = false;
    bool rootChanged = false;
    bool headerConfigured = false;
};

LayerTree::LayerTree(QWidget* parent)
    : TreeWidget(parent)
    , p(new LayerTreePrivate(this))
{
    setContextMenuPolicy(Qt::DefaultContextMenu);
    setSelectionMode(QAbstractItemView::SingleSelection);
    setDragEnabled(true);
    setAcceptDrops(true);
    setDropIndicatorShown(false);
    setDragDropMode(QAbstractItemView::DragDrop);
    connect(this, &QTreeWidget::itemCollapsed, this, [this](QTreeWidgetItem* item) {
        if (item && !item->parent()) {
            const LayerItem* rootItem = layerItem(item);
            const QString rootIdentifier = rootItem->identifier();
            item->setExpanded(true);
            QTimer::singleShot(0, this, [this, rootIdentifier]() {
                QTreeWidgetItemIterator itemIt(this);
                while (*itemIt) {
                    LayerItem* currentRoot = layerItem(*itemIt);
                    if (currentRoot->identifier() == rootIdentifier) {
                        if (!currentRoot->isExpanded()) {
                            currentRoot->setExpanded(true);
                        }
                        return;
                    }
                    ++itemIt;
                }
            });
        }
    });
}

LayerTree::~LayerTree() = default;

void
LayerTree::refresh()
{
    p->refresh();
}

void
LayerTree::selectCurrentLayer()
{
    if (!session()) {
        return;
    }

    const QString identifier = p->currentIdentifier();
    if (identifier.isEmpty()) {
        return;
    }

    QList<SdfPath> paths;
    QString layerName;
    {
        READ_LOCKER(locker, session()->stageLock(), "stageLock");
        const UsdStageRefPtr stage = session()->stageUnsafe();
        if (!stage)
            return;

        SdfLayerHandle layer;
        for (const SdfLayerHandle& candidate : stage->GetLayerStack(false)) {
            if (candidate && QString::fromStdString(candidate->GetIdentifier()) == identifier) {
                layer = candidate;
                break;
            }
        }
        if (!layer)
            layer = SdfLayer::Find(identifier.toStdString());
        if (!layer)
            return;

        layerName = QString::fromStdString(layer->GetDisplayName());
        for (const SdfPrimSpecHandle& rootPrim : layer->GetRootPrims())
            LayerItem::appendComposedPrimPaths(rootPrim, stage, paths);
    }

    if (paths.isEmpty()) {
        session()->notifyStatus(Session::Notify::Status::Warning,
                                tr("Layer %1 has no composed prim paths to select.").arg(layerName));
        return;
    }
    session()->selectionList()->updatePaths(paths);
    session()->notifyStatus(Session::Notify::Status::Success,
                            tr("Selected %1 prim path(s) from layer %2.").arg(paths.size()).arg(layerName));
}

void
LayerTree::startDrag(Qt::DropActions supportedActions)
{
    Q_UNUSED(supportedActions);
    QTreeWidgetItem* item = currentItem();
    if (!item)
        return;
    const LayerItem* currentLayer = layerItem(item);
    const QString identifier = currentLayer->identifier();
    const QString parentIdentifier = currentLayer->parentIdentifier();
    if (identifier.isEmpty() || parentIdentifier.isEmpty()) {
        return;
    }

    auto* mimeData = new QMimeData();
    mimeData->setData(mime::layer, (parentIdentifier + QLatin1Char('\n') + identifier).toUtf8());
    auto* drag = new QDrag(this);
    drag->setMimeData(mimeData);
    drag->exec(Qt::MoveAction);
}

void
LayerTree::dragEnterEvent(QDragEnterEvent* event)
{
    if (event && event->mimeData()->hasFormat(mime::layer)) {
        p->clearDropIndicator();
        event->acceptProposedAction();
    }
    else if (event) {
        p->clearDropIndicator();
        event->ignore();
    }
}

void
LayerTree::dragMoveEvent(QDragMoveEvent* event)
{
    if (!event || !event->mimeData()->hasFormat(mime::layer)) {
        if (event)
            event->ignore();
        p->clearDropIndicator();
        return;
    }
    const QStringList payload = QString::fromUtf8(event->mimeData()->data(mime::layer)).split(QLatin1Char('\n'));
    QTreeWidgetItem* target = itemAt(event->position().toPoint());
    const bool validTarget = payload.size() == 2 && target && layerItem(target)->parentIdentifier() == payload[0]
                             && layerItem(target)->identifier() != payload[1];
    if (validTarget) {
        const QRect rowRect = visualItemRect(target);
        const int y = event->position().toPoint().y() - rowRect.top();
        const int margin = std::max(4, rowRect.height() / 4);
        const int mode = y < margin || (y <= rowRect.height() - margin && y < rowRect.height() / 2) ? 1 : 3;
        p->setDropIndicator(target, mode);
        event->acceptProposedAction();
    }
    else {
        p->clearDropIndicator();
        event->ignore();
    }
}

void
LayerTree::dropEvent(QDropEvent* event)
{
    if (!event || !event->mimeData()->hasFormat(mime::layer) || !session()) {
        if (event)
            event->ignore();
        p->clearDropIndicator();
        return;
    }
    const QStringList payload = QString::fromUtf8(event->mimeData()->data(mime::layer)).split(QLatin1Char('\n'));
    QTreeWidgetItem* target = itemAt(event->position().toPoint());
    if (payload.size() != 2 || !target || layerItem(target)->parentIdentifier() != payload[0]) {
        p->clearDropIndicator();
        event->ignore();
        return;
    }

    const QString targetIdentifier = layerItem(target)->identifier();
    if (targetIdentifier == payload[1]) {
        p->clearDropIndicator();
        event->ignore();
        return;
    }
    const bool afterTarget = property(mime::dropModeProperty).toInt() == 3;
    p->clearDropIndicator();
    session()->commandStack()->execute(
        new Command(reorderSublayers(payload[0], payload[1], targetIdentifier, afterTarget)));
    refresh();
    event->acceptProposedAction();
}

void
LayerTree::contextMenuEvent(QContextMenuEvent* event)
{
    if (!event)
        return;

    QTreeWidgetItem* item = itemAt(event->pos());
    if (item)
        setCurrentItem(item);

    QMenu menu(this);
    QAction* selectAction = menu.addAction(stageviz::style()->icon(Style::IconRole::Select), tr("Select"));
    menu.addSeparator();

    const QString identifier = p->currentIdentifier();
    SdfLayerHandle layer;
    bool muted = false;
    bool editTarget = false;
    bool rootLayer = false;
    bool permissionToEdit = true;
    bool canRemove = false;
    bool canMute = false;
    bool localLayer = false;
    const bool inheritedMuted = item && layerItem(item)->isInheritedMuted();
    if (!identifier.isEmpty() && session()) {
        READ_LOCKER(locker, session()->stageLock(), "stageLock");
        const UsdStageRefPtr stage = session()->stageUnsafe();
        if (stage) {
            layer = p->retainedLayerHandles.value(identifier);
            for (const SdfLayerHandle& candidate : stage->GetLayerStack(false)) {
                if (candidate && QString::fromStdString(candidate->GetIdentifier()) == identifier) {
                    layer = candidate;
                    break;
                }
            }
            const std::vector<std::string> mutedLayers = stage->GetMutedLayers();
            muted = std::find(mutedLayers.begin(), mutedLayers.end(), identifier.toStdString()) != mutedLayers.end();
            if (!layer && muted)
                layer = SdfLayer::Find(identifier.toStdString());
            if (layer) {
                editTarget = stage->GetEditTarget().GetLayer() == layer;
                rootLayer = stage->GetRootLayer() == layer;
                localLayer = stage->HasLocalLayer(layer);
                canMute = !rootLayer && (muted || (localLayer && !editTarget));
                permissionToEdit = layer->PermissionToEdit();
                if (!rootLayer && !editTarget && !muted && localLayer) {
                    int referenceCount = 0;
                    bool parentEditable = false;
                    for (const SdfLayerHandle& parent : stage->GetLayerStack(false)) {
                        if (!parent)
                            continue;
                        for (const std::string& subLayerPath : parent->GetSubLayerPaths()) {
                            if (SdfLayer::FindRelativeToLayer(parent, subLayerPath) == layer) {
                                ++referenceCount;
                                parentEditable = parent->PermissionToEdit();
                            }
                        }
                    }
                    canRemove = referenceCount == 1 && parentEditable;
                }
            }
        }
    }

    QAction* setEditAction = menu.addAction(tr("Set as Edit Layer"));
    selectAction->setEnabled(layer);
    setEditAction->setEnabled(layer && localLayer && !editTarget && !muted);
    QAction* saveAction = menu.addAction(tr("Save Layer"));
    saveAction->setEnabled(layer && localLayer && !muted && layer->IsDirty() && !layer->IsAnonymous()
                           && layer->PermissionToSave());
    QAction* removeAction = menu.addAction(tr("Remove Layer"));
    removeAction->setEnabled(canRemove);
    removeAction->setToolTip(tr("Remove this layer from the stage. The USD file stays on disk."));
    QAction* muteAction = menu.addAction(muted ? tr("Unmute Layer") : tr("Mute Layer"));
    muteAction->setEnabled(layer && canMute);
    QAction* lockAction = menu.addAction(permissionToEdit ? tr("Lock Layer") : tr("Unlock Layer"));
    lockAction->setEnabled(layer && !rootLayer && !inheritedMuted);
    if (inheritedMuted)
        lockAction->setToolTip(tr("Permission changes are unavailable while an ancestor layer is muted."));
    const QAction* selected = menu.exec(event->globalPos());
    if (!selected)
        return;

    if (selected == selectAction) {
        selectCurrentLayer();
    }
    else if (selected == setEditAction && !identifier.isEmpty()) {
        session()->commandStack()->execute(new Command(setEditLayer(identifier)));
    }
    else if (selected == saveAction && layer) {
        if (session()->saveLayer(identifier)) {
            session()->notifyStatus(Session::Notify::Status::Success,
                                    tr("Saved layer %1").arg(QString::fromStdString(layer->GetDisplayName())));
            refresh();
            Q_EMIT layerSaved();
        }
    }
    else if (selected == removeAction && !identifier.isEmpty()) {
        session()->commandStack()->execute(new Command(removeLayer(identifier)));
    }
    else if (selected == muteAction && layer) {
        session()->commandStack()->execute(new Command(setLayerMuted(identifier, !muted)));
    }
    else if (selected == lockAction && layer) {
        session()->commandStack()->execute(new Command(setLayerPermissionToEdit(identifier, !permissionToEdit)));
    }
    event->accept();
}

void
LayerTree::mousePressEvent(QMouseEvent* event)
{
    if (event && event->button() == Qt::LeftButton && columnAt(event->pos().x()) == LayerItem::Visibility) {
        QTreeWidgetItem* item = itemAt(event->pos());
        if (item) {
            const QString identifier = layerItem(item)->identifier();
            if (identifier.isEmpty() || !session()) {
                event->accept();
                return;
            }

            bool muted = false;
            bool allowed = false;
            SdfLayerHandle clickedLayer;
            {
                READ_LOCKER(locker, session()->stageLock(), "stageLock");
                const UsdStageRefPtr stage = session()->stageUnsafe();
                if (stage) {
                    clickedLayer = p->retainedLayerHandles.value(identifier);
                    if (!clickedLayer)
                        clickedLayer = SdfLayer::Find(identifier.toStdString());
                    const std::vector<std::string> mutedLayers = stage->GetMutedLayers();
                    muted = std::find(mutedLayers.begin(), mutedLayers.end(), identifier.toStdString())
                            != mutedLayers.end();
                    allowed = clickedLayer && clickedLayer != stage->GetRootLayer()
                              && (muted
                                  || (stage->HasLocalLayer(clickedLayer)
                                      && clickedLayer != stage->GetEditTarget().GetLayer()));
                }
            }
            if (allowed)
                session()->commandStack()->execute(new Command(setLayerMuted(identifier, !muted)));
            event->accept();
            return;
        }
    }
    else if (event && event->button() == Qt::LeftButton && columnAt(event->pos().x()) == LayerItem::Permission) {
        QTreeWidgetItem* item = itemAt(event->pos());
        if (item) {
            const QString identifier = layerItem(item)->identifier();
            if (!identifier.isEmpty() && session()) {
                bool canToggle = false;
                bool editable = true;
                {
                    READ_LOCKER(locker, session()->stageLock(), "stageLock");
                    SdfLayerHandle layer = p->retainedLayerHandles.value(identifier);
                    if (!layer)
                        layer = SdfLayer::Find(identifier.toStdString());
                    const UsdStageRefPtr stage = session()->stageUnsafe();
                    const bool inheritedMuted = layerItem(item)->isInheritedMuted();
                    if (layer && (!stage || layer != stage->GetRootLayer()) && !inheritedMuted) {
                        editable = layer->PermissionToEdit();
                        canToggle = true;
                    }
                }
                if (canToggle)
                    session()->commandStack()->execute(new Command(setLayerPermissionToEdit(identifier, !editable)));
            }
            event->accept();
            return;
        }
    }
    TreeWidget::mousePressEvent(event);
}

}  // namespace stageviz
