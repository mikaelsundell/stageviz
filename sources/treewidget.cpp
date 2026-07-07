// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#include "treewidget.h"
#include "application.h"
#include "mime.h"
#include "style.h"
#include "treeitem.h"
#include <QDebug>
#include <QEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPersistentModelIndex>
#include <QPointer>
#include <QStyledItemDelegate>

namespace stageviz {

class TreeWidgetPrivate : public QObject {
    Q_OBJECT
public:
    TreeWidgetPrivate();
    void init();
    bool eventFilter(QObject* watched, QEvent* event);
    void clearHit();
    bool hasSelectedChildren(QTreeWidgetItem* item) const;
    int visualRowIndex(const QModelIndex& index) const;
    QModelIndex indexAtPosition(const QPoint& pos) const;
    QRect branchRect(const QRect& rect, const QModelIndex& index) const;
    QRect branchHitRect(const QRect& rect, const QModelIndex& index) const;
    bool hitBranch(const QPoint& pos, QModelIndex* outIndex = nullptr) const;
    bool hitCheckbox(const QPoint& pos, QModelIndex* outIndex = nullptr) const;
    bool hitSelectableContent(const QPoint& pos, QModelIndex* outIndex = nullptr) const;
    void drawBranches(QPainter* painter, const QRect& rect, const QModelIndex& index) const;
    void drawColumn(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const;
    QRect drawRow(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const;

public:
    class ItemDelegate : public QStyledItemDelegate {
    public:
        struct Layout {
            QRect contentRect;
            QRect checkRect;
            QRect checkHitRect;
            QRect iconRect;
            QRect iconHitRect;
            QRect textRect;
            bool isCheckable = false;
            bool hasDecoration = false;
        };

        ItemDelegate(QObject* parent = nullptr)
            : QStyledItemDelegate(parent)
        {}

        Layout layout(const QStyleOptionViewItem& option, const QModelIndex& index) const
        {
            QStyleOptionViewItem opt(option);
            initStyleOption(&opt, index);

            const int leftMargin = 8;
            const int rightMargin = 8;
            const int iconSpacing = 6;
            const int textSpacing = 8;
            const int iconSize = style()->iconSize(Style::UIScale::Small);
            const int xOffset = 10;
            const int yOffset = 2;

            Layout l;
            l.contentRect = opt.rect.adjusted(leftMargin, 0, -rightMargin, 0);
            l.hasDecoration = !opt.icon.isNull();

            if (index.column() == 0) {
                int x = l.contentRect.left() + xOffset;

                l.isCheckable = (index.flags() & Qt::ItemIsUserCheckable) && index.data(Qt::CheckStateRole).isValid();

                if (l.isCheckable) {
                    l.checkRect = QRect(x, l.contentRect.center().y() - iconSize / 2 + yOffset, iconSize, iconSize);
                    l.checkHitRect = l.checkRect;
                    x = l.checkRect.right() + 1 + iconSpacing;
                }

                if (l.hasDecoration) {
                    l.iconRect = QRect(x, l.contentRect.center().y() - iconSize / 2 + yOffset, iconSize, iconSize);
                    l.iconHitRect = l.iconRect;
                    x = l.iconRect.right() + 1 + textSpacing;
                }

                l.textRect = l.contentRect;
                l.textRect.setLeft(x);
                return l;
            }

            const bool hasText = !opt.text.isEmpty();

            if (l.hasDecoration && hasText) {
                int x = l.contentRect.left();

                l.iconRect = QRect(x, l.contentRect.center().y() - iconSize / 2 + yOffset, iconSize, iconSize);
                l.iconHitRect = l.iconRect;

                l.textRect = l.contentRect;
                l.textRect.setLeft(l.iconRect.right() + 1 + textSpacing);
            }
            else if (l.hasDecoration) {
                const int x = l.contentRect.center().x() - iconSize / 2;
                const int y = l.contentRect.center().y() - iconSize / 2 + yOffset;

                l.iconRect = QRect(x, y, iconSize, iconSize);
                l.iconHitRect = l.iconRect;
                l.textRect = QRect();
            }
            else {
                l.textRect = l.contentRect;
            }

            return l;
        }

        bool hitCheckbox(const QStyleOptionViewItem& option, const QModelIndex& index, const QPoint& pos) const
        {
            const Layout l = layout(option, index);
            return l.isCheckable && l.checkHitRect.contains(pos);
        }

        bool editorEvent(QEvent* event, QAbstractItemModel* model, const QStyleOptionViewItem& option,
                         const QModelIndex& index) override
        {
            Q_UNUSED(model);

            const bool isCheckable = index.column() == 0 && (index.flags() & Qt::ItemIsUserCheckable)
                                     && index.data(Qt::CheckStateRole).isValid();

            if (!isCheckable)
                return QStyledItemDelegate::editorEvent(event, model, option, index);

            if (event->type() == QEvent::MouseButtonPress || event->type() == QEvent::MouseButtonRelease) {
                auto* mouse = static_cast<QMouseEvent*>(event);
                if (mouse->button() == Qt::LeftButton && hitCheckbox(option, index, mouse->pos()))
                    return true;
                return false;
            }

            if (event->type() == QEvent::MouseButtonDblClick) {
                auto* mouse = static_cast<QMouseEvent*>(event);
                const Layout l = layout(option, index);
                if (!l.textRect.contains(mouse->pos()))
                    return true;
            }

            return QStyledItemDelegate::editorEvent(event, model, option, index);
        }

        void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override
        {
            QStyleOptionViewItem opt(option);
            initStyleOption(&opt, index);
            opt.features &= ~QStyleOptionViewItem::HasCheckIndicator;
            opt.backgroundBrush = Qt::NoBrush;
            opt.state &= ~QStyle::State_HasFocus;

            const Layout l = layout(option, index);
            const int iconSize = style()->iconSize(Style::UIScale::Small);

            painter->save();

            if (index.column() == 0 && l.isCheckable) {
                painter->setBrush(style()->color(Style::ColorRole::BaseAlt));
                painter->setPen(style()->color(Style::ColorRole::BorderAlt));
                painter->drawRect(l.checkRect.adjusted(0, 0, -1, -1));

                const Qt::CheckState state = static_cast<Qt::CheckState>(index.data(Qt::CheckStateRole).toInt());
                if (state == Qt::Checked) {
                    const QIcon checked = style()->icon(Style::IconRole::Checked, Style::UIScale::Small);
                    painter->drawPixmap(l.checkRect.topLeft(), checked.pixmap(iconSize, iconSize));
                }
                else if (state == Qt::PartiallyChecked) {
                    const QIcon partial = style()->icon(Style::IconRole::PartiallyChecked, Style::UIScale::Small);
                    painter->drawPixmap(l.checkRect.topLeft(), partial.pixmap(iconSize, iconSize));
                }
            }

            if (!l.iconRect.isNull()) {
                opt.icon.paint(painter, l.iconRect, Qt::AlignCenter,
                               (opt.state & QStyle::State_Enabled) ? QIcon::Normal : QIcon::Disabled);
            }

            if (!l.textRect.isNull() && !opt.text.isEmpty()) {
                painter->setPen(opt.palette.color(QPalette::Text));
                painter->setFont(opt.font);
                painter->drawText(l.textRect, opt.displayAlignment | Qt::AlignVCenter,
                                  opt.fontMetrics.elidedText(opt.text, Qt::ElideRight, l.textRect.width()));
            }

#ifdef STAGEVIZ_DEBUG_TREE_HITREGIONS
            {
                painter->setBrush(Qt::NoBrush);

                if (!l.checkRect.isNull()) {
                    QPen checkRectPen(Qt::yellow);
                    checkRectPen.setWidth(1);
                    painter->setPen(checkRectPen);
                    painter->drawRect(l.checkRect.adjusted(0, 0, -1, -1));
                }

                if (!l.checkHitRect.isNull()) {
                    QPen checkHitPen(Qt::cyan);
                    checkHitPen.setWidth(1);
                    painter->setPen(checkHitPen);
                    painter->drawRect(l.checkHitRect.adjusted(0, 0, -1, -1));
                }

                if (!l.iconRect.isNull()) {
                    QPen iconRectPen(Qt::red);
                    iconRectPen.setWidth(1);
                    painter->setPen(iconRectPen);
                    painter->drawRect(l.iconRect.adjusted(0, 0, -1, -1));
                }

                if (!l.iconHitRect.isNull()) {
                    QPen iconHitPen(Qt::magenta);
                    iconHitPen.setWidth(1);
                    painter->setPen(iconHitPen);
                    painter->drawRect(l.iconHitRect.adjusted(0, 0, -1, -1));
                }

                QPen textPen(Qt::green);
                textPen.setWidth(1);
                painter->setPen(textPen);
                painter->drawRect(l.textRect.adjusted(0, 0, -1, -1));
            }
#endif

            painter->restore();
        }
    };

    enum DropMode { DropNone = 0, DropAboveItem = 1, DropOnItem = 2, DropBelowItem = 3 };

    struct Data {
        bool suppressSelection = false;
        bool branchExpanded = false;
        QSet<int> selectableColumns { 0 };
        QPersistentModelIndex branchIndex;
        QPersistentModelIndex checkboxIndex;
        QPointer<ItemDelegate> delegate;
        QPointer<TreeWidget> tree;
    };

    Data d;
};

TreeWidgetPrivate::TreeWidgetPrivate() {}

void
TreeWidgetPrivate::init()
{
    d.delegate = new ItemDelegate(d.tree.data());
    d.tree->setItemDelegate(d.delegate);
    d.tree->viewport()->installEventFilter(this);
}

bool
TreeWidgetPrivate::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == d.tree->viewport()) {
        switch (event->type()) {
        case QEvent::MouseButtonPress: {
            auto* mouse = static_cast<QMouseEvent*>(event);
            if (mouse->button() != Qt::LeftButton)
                break;

            if (!d.tree->itemAt(mouse->pos())) {
                d.tree->clearSelection();
                Q_EMIT d.tree->itemSelectionChanged();
                clearHit();
                return true;
            }

            QModelIndex index;
            if (hitBranch(mouse->pos(), &index)) {
                d.suppressSelection = true;
                d.branchIndex = index;
                d.branchExpanded = d.tree->isExpanded(index);
                d.checkboxIndex = QModelIndex();
                return true;
            }

            if (hitCheckbox(mouse->pos(), &index)) {
                d.suppressSelection = true;
                d.checkboxIndex = index;
                d.branchIndex = QModelIndex();
                d.branchExpanded = false;
                return true;
            }

            clearHit();
            break;
        }

        case QEvent::MouseButtonRelease: {
            auto* mouse = static_cast<QMouseEvent*>(event);
            if (mouse->button() != Qt::LeftButton)
                break;

            QModelIndex index;
            if (d.branchIndex.isValid()) {
                const bool releaseOnBranch = hitBranch(mouse->pos(), &index);
                const bool sameBranch = releaseOnBranch && index == d.branchIndex;
                if (sameBranch && d.tree->model()->hasChildren(index)) {
                    const bool targetExpanded = !d.branchExpanded;
                    d.tree->setExpanded(index, targetExpanded);
                }

                clearHit();
                return true;
            }

            if (d.checkboxIndex.isValid()) {
                const bool releaseOnCheckbox = hitCheckbox(mouse->pos(), &index);
                const bool sameCheckbox = releaseOnCheckbox && index == d.checkboxIndex;

                if (sameCheckbox) {
                    Qt::CheckState state = static_cast<Qt::CheckState>(index.data(Qt::CheckStateRole).toInt());
                    switch (state) {
                    case Qt::Unchecked: state = Qt::Checked; break;
                    case Qt::Checked:
                    case Qt::PartiallyChecked: state = Qt::Unchecked; break;
                    }

                    d.tree->model()->setData(index, state, Qt::CheckStateRole);
                }

                clearHit();
                return true;
            }

            break;
        }

        case QEvent::MouseButtonDblClick: {
            auto* mouse = static_cast<QMouseEvent*>(event);
            if (mouse->button() != Qt::LeftButton)
                break;

            if (hitBranch(mouse->pos()))
                return true;

            if (hitCheckbox(mouse->pos()))
                return true;

            break;
        }

        default: break;
        }
    }

    return QObject::eventFilter(watched, event);
}

void
TreeWidgetPrivate::clearHit()
{
    d.suppressSelection = false;
    d.branchIndex = QModelIndex();
    d.branchExpanded = false;
    d.checkboxIndex = QModelIndex();
}

bool
TreeWidgetPrivate::hasSelectedChildren(QTreeWidgetItem* item) const
{
    for (int i = 0; i < item->childCount(); ++i) {
        QTreeWidgetItem* child = item->child(i);
        if (child->isSelected() || hasSelectedChildren(child))
            return true;
    }
    return false;
}

int
TreeWidgetPrivate::visualRowIndex(const QModelIndex& index) const
{
    int row = 0;
    QModelIndex current = index.siblingAtColumn(0);
    while (true) {
        QModelIndex above = d.tree->indexAbove(current);
        if (!above.isValid())
            break;

        current = above;
        ++row;
    }
    return row;
}

QModelIndex
TreeWidgetPrivate::indexAtPosition(const QPoint& pos) const
{
    QModelIndex index = d.tree->indexAt(pos);
    if (index.isValid())
        return index;

    index = d.tree->indexAt(QPoint(d.tree->indentation() + 20, pos.y()));
    if (index.isValid())
        return index;

    return d.tree->indexAt(QPoint(d.tree->viewport()->width() - 1, pos.y()));
}

QRect
TreeWidgetPrivate::branchRect(const QRect& rect, const QModelIndex& index) const
{
    Q_UNUSED(index);
    const int size = style()->iconSize(Style::UIScale::Small);
    const int x = 6;
    const int y = rect.center().y() - size / 2 + 1;
    return QRect(x, y, size, size);
}

QRect
TreeWidgetPrivate::branchHitRect(const QRect& rect, const QModelIndex& index) const
{
    const QRect r = branchRect(rect, index);
    const int padLeft = 2;
    const int padRight = 2;
    const int padTop = 2;
    const int padBottom = 2;
    return r.adjusted(-padLeft, -padTop, padRight, padBottom);
}

bool
TreeWidgetPrivate::hitBranch(const QPoint& pos, QModelIndex* outIndex) const
{
    QModelIndex index = indexAtPosition(pos);
    if (!index.isValid())
        return false;

    index = index.siblingAtColumn(0);

    const QRect vr = d.tree->visualRect(index);
    const QRect br = branchHitRect(vr, index);

    if (!br.contains(pos))
        return false;

    if (outIndex)
        *outIndex = index;

    return true;
}

bool
TreeWidgetPrivate::hitCheckbox(const QPoint& pos, QModelIndex* outIndex) const
{
    QModelIndex index = indexAtPosition(pos);
    if (!index.isValid() || !d.delegate)
        return false;

    index = index.siblingAtColumn(0);

    const QRect vr = d.tree->visualRect(index);
    const QRect br = branchHitRect(vr, index);
    if (br.contains(pos))
        return false;

    const QStyleOptionViewItem opt = d.tree->itemViewOption(index);
    if (!d.delegate->hitCheckbox(opt, index, pos))
        return false;

    if (outIndex)
        *outIndex = index;

    return true;
}

bool
TreeWidgetPrivate::hitSelectableContent(const QPoint& pos, QModelIndex* outIndex) const
{
    const QModelIndex index = indexAtPosition(pos);
    if (!index.isValid() || !d.delegate)
        return false;

    if (index.column() == 0) {
        const QRect vr = d.tree->visualRect(index);
        const QRect br = branchHitRect(vr, index);
        if (br.contains(pos))
            return false;
    }

    const QStyleOptionViewItem opt = d.tree->itemViewOption(index);
    const auto layout = d.delegate->layout(opt, index);

    if (layout.isCheckable && layout.checkHitRect.contains(pos))
        return false;

    const bool selectableColumn = d.selectableColumns.contains(index.column());

    const bool inIcon = selectableColumn && !layout.iconHitRect.isNull() && layout.iconHitRect.contains(pos);
    const bool inText = selectableColumn && !layout.textRect.isNull() && layout.textRect.contains(pos);

    if (!inIcon && !inText)
        return false;

    if (outIndex)
        *outIndex = index;

    return true;
}

void
TreeWidgetPrivate::drawBranches(QPainter* painter, const QRect& rect, const QModelIndex& index) const
{
    Q_UNUSED(rect);

    const bool hasChildren = d.tree->model()->hasChildren(index);
    if (!hasChildren)
        return;

    const bool expanded = d.tree->isExpanded(index);
    const QRect vr = d.tree->visualRect(index);
    const QRect r = branchRect(vr, index);
    const QRect hr = branchHitRect(vr, index);

    const QIcon icon = expanded ? app()->style()->icon(Style::IconRole::BranchOpen, Style::UIScale::Small)
                                : app()->style()->icon(Style::IconRole::BranchClosed, Style::UIScale::Small);

    icon.paint(painter, r, Qt::AlignCenter);

#ifdef STAGEVIZ_DEBUG_TREE_HITREGIONS
    painter->save();
    painter->setBrush(Qt::NoBrush);

    QPen branchRectPen(Qt::blue);
    branchRectPen.setWidth(1);
    painter->setPen(branchRectPen);
    painter->drawRect(r.adjusted(0, 0, -1, -1));

    QPen branchHitPen(Qt::cyan);
    branchHitPen.setWidth(1);
    painter->setPen(branchHitPen);
    painter->drawRect(hr.adjusted(0, 0, -1, -1));

    painter->restore();
#endif
}

void
TreeWidgetPrivate::drawColumn(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const
{
    QStyleOptionViewItem opt(option);
    opt.rect = d.tree->visualRect(index);
    opt.features &= ~QStyleOptionViewItem::Alternate;
    opt.backgroundBrush = Qt::NoBrush;
    opt.state &= ~QStyle::State_HasFocus;

    if (d.tree->selectionModel() && d.tree->selectionModel()->isSelected(index))
        opt.state |= QStyle::State_Selected;
    else
        opt.state &= ~QStyle::State_Selected;

    if (QAbstractItemDelegate* delegate = d.tree->itemDelegateForIndex(index))
        delegate->paint(painter, opt, index);
}

QRect
TreeWidgetPrivate::drawRow(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const
{
    QStyleOptionViewItem opt(option);
    opt.features &= ~QStyleOptionViewItem::Alternate;
    opt.backgroundBrush = Qt::NoBrush;

    QRect rowRect = d.tree->visualRect(index.siblingAtColumn(0));
    rowRect.setLeft(0);
    rowRect.setRight(d.tree->viewport()->width());

    QTreeWidgetItem* item = d.tree->itemFromIndex(index);

    const bool alternatingRow = visualRowIndex(index) % 2 == 1;
    const QColor bg = alternatingRow ? app()->style()->color(Style::ColorRole::ItemAlt)
                                     : app()->style()->color(Style::ColorRole::Item);

    const QBrush background = item ? item->background(0) : QBrush();
    if (background.style() != Qt::NoBrush && background.color().isValid())
        painter->fillRect(rowRect, background);
    else
        painter->fillRect(rowRect, bg);

    const bool selected = d.tree->selectionModel() && d.tree->selectionModel()->isSelected(index);

    if (selected) {
        painter->fillRect(rowRect, app()->style()->color(Style::ColorRole::Highlight));
    }
    else if (item && hasSelectedChildren(item)) {
        painter->fillRect(rowRect, app()->style()->color(Style::ColorRole::HighlightAlt));
    }

    const qulonglong dropItemPtr = d.tree->property(mime::dropItemPtrProperty).toULongLong();
    const int dropMode = d.tree->property(mime::dropModeProperty).toInt();

    if (item && dropItemPtr != 0 && reinterpret_cast<qulonglong>(item) == dropItemPtr) {
        painter->save();

        QPen pen(app()->style()->color(Style::ColorRole::Highlight));
        pen.setWidth(2);

        painter->setPen(pen);
        painter->setBrush(Qt::NoBrush);

        switch (dropMode) {
        case TreeWidgetPrivate::DropAboveItem:
            painter->drawLine(rowRect.left(), rowRect.top(), rowRect.right(), rowRect.top());
            break;
        case TreeWidgetPrivate::DropBelowItem:
            painter->drawLine(rowRect.left(), rowRect.bottom(), rowRect.right(), rowRect.bottom());
            break;
        case TreeWidgetPrivate::DropOnItem: painter->drawRect(rowRect.adjusted(1, 1, -2, -2)); break;
        default: break;
        }

        painter->restore();
    }

    return rowRect;
}

TreeWidget::TreeWidget(QWidget* parent)
    : QTreeWidget(parent)
    , p(new TreeWidgetPrivate())
{
    p->d.tree = this;
    p->init();
    setIndentation(12);
}

TreeWidget::~TreeWidget() = default;

void
TreeWidget::setColumnSelectable(int column, bool selectable)
{
    if (selectable)
        p->d.selectableColumns.insert(column);
    else
        p->d.selectableColumns.remove(column);
}

bool
TreeWidget::columnSelectable(int column) const
{
    return p->d.selectableColumns.contains(column);
}

QStyleOptionViewItem
TreeWidget::itemViewOption(const QModelIndex& index) const
{
    QStyleOptionViewItem opt;
    initViewItemOption(&opt);
    opt.rect = visualRect(index);
    return opt;
}

bool
TreeWidget::viewportEvent(QEvent* event)
{
    if (event->type() == QEvent::DragLeave) {
        setProperty(mime::dropItemPtrProperty, QVariant::fromValue<qulonglong>(0));
        setProperty(mime::dropModeProperty, TreeWidgetPrivate::DropNone);
        viewport()->update();
    }

    return QTreeWidget::viewportEvent(event);
}

QItemSelectionModel::SelectionFlags
TreeWidget::selectionCommand(const QModelIndex& index, const QEvent* event) const
{
    if (p->d.suppressSelection)
        return QItemSelectionModel::NoUpdate;

    if (!event)
        return QTreeWidget::selectionCommand(index, event);

    switch (event->type()) {
    case QEvent::MouseButtonPress:
    case QEvent::MouseButtonRelease:
    case QEvent::MouseButtonDblClick:
    case QEvent::MouseMove: {
        const auto* mouse = static_cast<const QMouseEvent*>(event);

        if (p->hitBranch(mouse->pos()))
            return QItemSelectionModel::NoUpdate;

        if (p->hitCheckbox(mouse->pos()))
            return QItemSelectionModel::NoUpdate;

        if (mouse->modifiers() & (Qt::ShiftModifier | Qt::ControlModifier | Qt::MetaModifier))
            return QTreeWidget::selectionCommand(index, event);

        QModelIndex contentIndex;
        if (p->hitSelectableContent(mouse->pos(), &contentIndex))
            return QTreeWidget::selectionCommand(index, event);

        return QItemSelectionModel::NoUpdate;
    }

    default:
        break;
    }

    return QTreeWidget::selectionCommand(index, event);
}

void
TreeWidget::drawRow(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const
{
    const QRect rowRect = p->drawRow(painter, option, index);

    p->drawBranches(painter, rowRect, index);

    for (int column = 0; column < model()->columnCount(index.parent()); ++column) {
        const QModelIndex columnIndex = index.sibling(index.row(), column);
        if (!columnIndex.isValid())
            continue;

        QStyleOptionViewItem itemOpt;
        initViewItemOption(&itemOpt);
        itemOpt.rect = visualRect(columnIndex);

        p->drawColumn(painter, itemOpt, columnIndex);
    }
}

}  // namespace stageviz

#include "treewidget.moc"
