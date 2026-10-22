// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#include "indextree.h"
#include <QApplication>
#include <QEvent>
#include <QItemSelectionModel>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QSignalBlocker>
#include <QTreeWidgetItemIterator>

namespace stageviz {
class IndexTreePrivate {
public:
    void clear() noexcept
    {
        d.selectedPaths.clear();
        d.ancestorPaths.clear();
        d.selectedItemPointers.clear();
        d.ancestorItemPointers.clear();
        d.keyboardRangeAnchor = nullptr;
        d.itemSelectionActive = false;
        d.selectionPathRole = -1;
    }
    struct Data {
        QSet<QString> selectedPaths;
        QSet<QString> ancestorPaths;
        int selectionPathRole = -1;
        bool itemSelectionActive = false;
        QTreeWidgetItem* keyboardRangeAnchor = nullptr;
        QTreeWidgetItem* pendingClickItem = nullptr;
        QPoint pendingClickPosition;
        QSet<QTreeWidgetItem*> selectedItemPointers;
        QSet<QTreeWidgetItem*> ancestorItemPointers;
    };
    Data d;
};

IndexTree::IndexTree(QWidget* parent)
    : TreeWidget(parent)
    , p(new IndexTreePrivate)
{
    connect(model(), &QAbstractItemModel::rowsAboutToBeRemoved, this, [this]() { p->clear(); });
    connect(model(), &QAbstractItemModel::modelAboutToBeReset, this, [this]() { p->clear(); });
}

IndexTree::~IndexTree()
{
    if (model())
        QObject::disconnect(model(), nullptr, this, nullptr);
    p->clear();
}

QItemSelectionModel::SelectionFlags
IndexTree::selectionCommand(const QModelIndex& index, const QEvent* event) const
{
    if (p->d.itemSelectionActive && event
        && (event->type() == QEvent::MouseButtonPress || event->type() == QEvent::MouseButtonRelease
            || event->type() == QEvent::MouseButtonDblClick || event->type() == QEvent::MouseMove))
        return QItemSelectionModel::NoUpdate;
    return TreeWidget::selectionCommand(index, event);
}

void
IndexTree::keyPressEvent(QKeyEvent* event)
{
    if (!p->d.itemSelectionActive || (event->key() != Qt::Key_Up && event->key() != Qt::Key_Down)) {
        p->d.keyboardRangeAnchor = nullptr;
        QTreeWidget::keyPressEvent(event);
        return;
    }

    QTreeWidgetItem* current = currentItem();
    if (!current && !p->d.selectedItemPointers.isEmpty())
        current = *p->d.selectedItemPointers.constBegin();
    if (!current) {
        QTreeWidget::keyPressEvent(event);
        return;
    }

    const bool extend = event->modifiers() & Qt::ShiftModifier;
    if (extend) {
        if (!p->d.keyboardRangeAnchor)
            p->d.keyboardRangeAnchor = current;
    }
    else {
        p->d.keyboardRangeAnchor = nullptr;
    }

    QTreeWidgetItem* target = event->key() == Qt::Key_Down ? itemBelow(current) : itemAbove(current);
    if (!target) {
        event->accept();
        return;
    }

    QSet<QTreeWidgetItem*> next;
    if (extend) {
        // Keep the keyboard range anchored while currentItem() moves along it.
        QTreeWidgetItem* rangeStart = p->d.keyboardRangeAnchor;
        for (QTreeWidgetItem* item = rangeStart; item; item = itemBelow(item)) {
            next.insert(item);
            if (item == target)
                break;
        }
        if (!next.contains(target)) {
            next.clear();
            for (QTreeWidgetItem* item = rangeStart; item; item = itemAbove(item)) {
                next.insert(item);
                if (item == target)
                    break;
            }
        }
    }
    else {
        next.insert(target);
    }

    setCurrentItem(target, 0, QItemSelectionModel::NoUpdate);
    scrollToItem(target);
    if (next != p->d.selectedItemPointers) {
        QSet<QTreeWidgetItem*> ancestors;
        for (QTreeWidgetItem* item : next)
            for (QTreeWidgetItem* parent = item->parent(); parent; parent = parent->parent())
                ancestors.insert(parent);
        setSelectionItems(next, ancestors);
        Q_EMIT itemSelectionChanged();
    }
    event->accept();
}

void
IndexTree::mousePressEvent(QMouseEvent* event)
{
    p->d.keyboardRangeAnchor = nullptr;
    if (!p->d.itemSelectionActive || event->button() != Qt::LeftButton) {
        QTreeWidget::mousePressEvent(event);
        return;
    }

    p->d.pendingClickItem = nullptr;

    // Qt may change currentItem() while handling the press. Preserve the
    // existing range anchor before passing the event to Qt.
    QTreeWidgetItem* rangeAnchor = currentItem();
    // Let Qt maintain its pressed/current index, drag state and edit triggers.
    // selectionCommand() prevents Qt from changing native selection.
    QTreeWidget::mousePressEvent(event);

    QTreeWidgetItem* clicked = itemAt(event->pos());
    if (clicked && !columnSelectable(columnAt(event->pos().x()))) {
        // Do not let Qt change native selection for non-selectable content.
        event->accept();
        return;
    }

    QSet<QTreeWidgetItem*> next = p->d.selectedItemPointers;
    const auto modifiers = event->modifiers();
    const bool additive = modifiers & (Qt::ControlModifier | Qt::MetaModifier);
    if (!clicked) {
        if (!additive && !(modifiers & Qt::ShiftModifier))
            next.clear();
    }
    else if (modifiers & Qt::ShiftModifier) {
        QTreeWidgetItem* anchor = rangeAnchor;
        if (!anchor)
            anchor = clicked;
        if (!additive)
            next.clear();
        QList<QTreeWidgetItem*> range;
        for (auto* row = anchor; row; row = itemBelow(row)) {
            range.append(row);
            if (row == clicked)
                break;
        }
        if (range.isEmpty() || range.last() != clicked) {
            range.clear();
            for (auto* row = anchor; row; row = itemAbove(row)) {
                range.append(row);
                if (row == clicked)
                    break;
            }
        }
        for (auto* row : range)
            next.insert(row);
        // Keep the anchor stable across repeated Shift-clicks.
        setCurrentItem(anchor, 0, QItemSelectionModel::NoUpdate);
    }
    else if (additive) {
        if (next.contains(clicked))
            next.remove(clicked);
        else
            next.insert(clicked);
        setCurrentItem(clicked, 0, QItemSelectionModel::NoUpdate);
    }
    else {
        if (p->d.selectedItemPointers.contains(clicked) && p->d.selectedItemPointers.size() > 1) {
            // Keep the multi-selection intact until we know whether this is a
            // click or a drag. A simple click collapses it on mouse release.
            p->d.pendingClickItem = clicked;
            p->d.pendingClickPosition = event->pos();
        }
        else {
            next = { clicked };
        }
        setCurrentItem(clicked, 0, QItemSelectionModel::NoUpdate);
    }

    if (next != p->d.selectedItemPointers) {
        QSet<QTreeWidgetItem*> ancestors;
        for (auto* row : next) {
            for (auto* parent = row->parent(); parent && !ancestors.contains(parent); parent = parent->parent())
                ancestors.insert(parent);
        }
        setSelectionItems(next, ancestors);
        // OutlinerTree already listens to this standard QTreeWidget signal.
        Q_EMIT itemSelectionChanged();
    }
    event->accept();
}

void
IndexTree::mouseReleaseEvent(QMouseEvent* event)
{
    QTreeWidgetItem* pending = p->d.pendingClickItem;
    const QPoint pressPosition = p->d.pendingClickPosition;
    p->d.pendingClickItem = nullptr;

    QTreeWidget::mouseReleaseEvent(event);

    if (!pending || event->button() != Qt::LeftButton || !p->d.itemSelectionActive)
        return;
    if ((event->pos() - pressPosition).manhattanLength() >= QApplication::startDragDistance())
        return;
    if (itemAt(event->pos()) != pending || !p->d.selectedItemPointers.contains(pending))
        return;

    QSet<QTreeWidgetItem*> next { pending };
    QSet<QTreeWidgetItem*> ancestors;
    for (QTreeWidgetItem* parent = pending->parent(); parent; parent = parent->parent())
        ancestors.insert(parent);
    setSelectionItems(next, ancestors);
    Q_EMIT itemSelectionChanged();
}

void
IndexTree::setSelectionItems(const QSet<QTreeWidgetItem*>& selected, const QSet<QTreeWidgetItem*>& ancestors)
{
    // External selection is authoritative. Remove stale native highlights
    // without emitting an intermediate empty selection to derived widgets.
    if (QItemSelectionModel* model = selectionModel()) {
        // Qt may still own an old selection from before pointer mode was
        // activated. Clear it only when needed, not on every USD update.
        if (model->hasSelection()) {
            const QSignalBlocker blocker(model);
            QTreeWidget::clearSelection();
        }
    }

    p->d.selectedItemPointers = selected;
    p->d.ancestorItemPointers = ancestors;
    p->d.itemSelectionActive = true;
    p->d.selectedPaths.clear();
    p->d.ancestorPaths.clear();
    p->d.selectionPathRole = -1;
    viewport()->update();
}

void
IndexTree::setSelection(const QSet<QString>& selected, const QSet<QString>& ancestors, int pathRole)
{
    p->d.keyboardRangeAnchor = nullptr;
    if (QItemSelectionModel* model = selectionModel()) {
        // Qt may still own an old selection from before pointer mode was
        // activated. Clear it only when needed, not on every USD update.
        if (model->hasSelection()) {
            const QSignalBlocker blocker(model);
            QTreeWidget::clearSelection();
        }
    }

    p->d.selectedItemPointers.clear();
    p->d.ancestorItemPointers.clear();
    p->d.itemSelectionActive = false;
    p->d.selectedPaths = selected;
    p->d.ancestorPaths = ancestors;
    p->d.selectionPathRole = pathRole;
    viewport()->update();
}

void
IndexTree::clearSelection()
{
    p->d.keyboardRangeAnchor = nullptr;
    QTreeWidget::clearSelection();
    p->d.selectedPaths.clear();
    p->d.ancestorPaths.clear();
    p->d.selectedItemPointers.clear();
    p->d.ancestorItemPointers.clear();
    p->d.itemSelectionActive = false;
    p->d.selectionPathRole = -1;
    viewport()->update();
}

QList<QTreeWidgetItem*>
IndexTree::selectedItems() const
{
    if (p->d.itemSelectionActive) {
        QList<QTreeWidgetItem*> result;
        result.reserve(p->d.selectedItemPointers.size());
        for (QTreeWidgetItem* item : p->d.selectedItemPointers)
            result.append(item);
        return result;
    }
    if (p->d.selectionPathRole >= 0) {
        QList<QTreeWidgetItem*> result;
        // Path-backed mode is intended for external highlighting. Resolving
        // pointers is intentionally on demand, never during row painting.
        QTreeWidgetItemIterator it(const_cast<IndexTree*>(this));
        while (*it) {
            if (isItemSelected(*it))
                result.append(*it);
            ++it;
        }
        return result;
    }
    return QTreeWidget::selectedItems();
}

bool
IndexTree::hasExternalSelection() const
{
    return p->d.itemSelectionActive || p->d.selectionPathRole >= 0;
}

bool
IndexTree::isItemSelected(const QTreeWidgetItem* item) const
{
    if (!item)
        return false;
    if (p->d.itemSelectionActive)
        return p->d.selectedItemPointers.contains(const_cast<QTreeWidgetItem*>(item));
    if (p->d.selectionPathRole >= 0)
        return p->d.selectedPaths.contains(item->data(0, p->d.selectionPathRole).toString());
    return TreeWidget::isItemSelected(item);
}

qsizetype
IndexTree::selectedItemCount() const
{
    if (p->d.itemSelectionActive)
        return p->d.selectedItemPointers.size();
    if (p->d.selectionPathRole >= 0)
        return p->d.selectedPaths.size();
    return QTreeWidget::selectedItems().size();
}

void
IndexTree::setItemSelected(QTreeWidgetItem* item, bool selected)
{
    p->d.keyboardRangeAnchor = nullptr;
    if (!item || !(item->flags() & Qt::ItemIsSelectable))
        return;

    if (!p->d.itemSelectionActive && p->d.selectionPathRole < 0) {
        item->setSelected(selected);
        return;
    }

    QSet<QTreeWidgetItem*> next = p->d.selectedItemPointers;
    if (!p->d.itemSelectionActive) {
        // Convert path-backed selection only when an explicit edit is requested.
        for (QTreeWidgetItem* existing : selectedItems())
            next.insert(existing);
    }
    if (selected)
        next.insert(item);
    else
        next.remove(item);

    if (p->d.itemSelectionActive && next == p->d.selectedItemPointers)
        return;

    QSet<QTreeWidgetItem*> ancestors;
    for (QTreeWidgetItem* entry : next) {
        for (QTreeWidgetItem* parent = entry->parent(); parent; parent = parent->parent()) {
            if (ancestors.contains(parent))
                break;
            ancestors.insert(parent);
        }
    }
    setSelectionItems(next, ancestors);
    Q_EMIT itemSelectionChanged();
}

void
IndexTree::selectAll()
{
    p->d.keyboardRangeAnchor = nullptr;
    if (!p->d.itemSelectionActive && p->d.selectionPathRole < 0) {
        QTreeWidget::selectAll();
        return;
    }

    QSet<QTreeWidgetItem*> next;
    QSet<QTreeWidgetItem*> ancestors;
    QTreeWidgetItemIterator it(this);
    while (*it) {
        QTreeWidgetItem* item = *it;
        if (item->flags() & Qt::ItemIsSelectable) {
            next.insert(item);
            for (QTreeWidgetItem* parent = item->parent(); parent; parent = parent->parent())
                ancestors.insert(parent);
        }
        ++it;
    }
    if (p->d.itemSelectionActive && next == p->d.selectedItemPointers)
        return;
    setSelectionItems(next, ancestors);
    Q_EMIT itemSelectionChanged();
}

bool
IndexTree::hasSelectedDescendants(const QTreeWidgetItem* item) const
{
    if (!item)
        return false;
    if (p->d.itemSelectionActive)
        return p->d.ancestorItemPointers.contains(const_cast<QTreeWidgetItem*>(item));
    if (p->d.selectionPathRole >= 0)
        return p->d.ancestorPaths.contains(item->data(0, p->d.selectionPathRole).toString());
    return TreeWidget::hasSelectedDescendants(item);
}

}  // namespace stageviz
