// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#pragma once

#include "stageviz.h"
#include <QStyleOptionViewItem>
#include <QStyledItemDelegate>
#include <QTreeWidget>
#include <memory>

namespace stageviz {

class TreeWidgetPrivate;

/**
 * @class TreeWidget
 * @brief QTreeWidget with custom row interaction and painting.
 *
 * Provides custom hit handling for branch toggles, checkboxes, and
 * selectable content regions, along with custom branch and row drawing.
 */
class TreeWidget : public QTreeWidget {
    Q_OBJECT
public:
    /**
     * @class ItemDelegate
     * @brief Shared StageViz tree item layout and painting delegate.
     *
     * Specialized tree delegates should derive from this class when they
     * need custom editors while preserving the standard StageViz tree
     * geometry, hit regions, icons, and text painting.
     */
    class ItemDelegate : public QStyledItemDelegate {
    public:
        /**
         * @struct Layout
         * @brief Geometry and visibility flags shared by painting and hit testing, in view coordinates.
         */
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

        /**
         * @brief Creates the shared tree delegate.
         */
        explicit ItemDelegate(QObject* parent = nullptr);
        /**
         * @brief Releases the delegate.
         */
        ~ItemDelegate() override;

        /**
         * @brief Computes the content, checkbox, decoration, and text rectangles for a row.
         */
        Layout layout(const QStyleOptionViewItem& option, const QModelIndex& index) const;
        /**
         * @brief Reports whether pos lies inside the row's checkbox interaction region.
         */
        bool hitCheckbox(const QStyleOptionViewItem& option, const QModelIndex& index, const QPoint& pos) const;

        /**
         * @brief Handles checkbox editing using the shared row hit regions.
         */
        bool editorEvent(QEvent* event, QAbstractItemModel* model, const QStyleOptionViewItem& option,
                         const QModelIndex& index) override;

        /**
         * @brief Paints the row using the shared layout and item states.
         */
        void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override;
    };

    /**
     * @brief Creates the tree widget.
     * @param parent Parent widget.
     */
    explicit TreeWidget(QWidget* parent = nullptr);

    /**
     * @brief Destroys the tree widget.
     */
    ~TreeWidget() override;

    /**
     * @brief Enables or disables row selection from a column.
     *
     * When enabled, clicking the column's selectable content (text or
     * decoration) selects the corresponding row. Disabled columns do not
     * participate in row selection.
     *
     * @param column Column index.
     * @param selectable True to enable selection, false to disable it.
     */
    void setColumnSelectable(int column, bool selectable);

    /**
     * @brief Returns whether a column participates in row selection.
     *
     * @param column Column index.
     * @return True if the column's selectable content may select the row.
     */
    bool columnSelectable(int column) const;

    /**
     * @brief Builds a view option for a specific model index.
     * @param index Model index to describe.
     * @return Initialized style option for the index.
     */
    QStyleOptionViewItem itemViewOption(const QModelIndex& index) const;

protected:
    /**
     * @brief Handles viewport-level events.
     *
     * Used for interaction state such as drag feedback cleanup.
     *
     * @param event Incoming event.
     * @return True if the event was handled.
     */
    bool viewportEvent(QEvent* event) override;

    /**
     * @brief Returns the selection command for a given event.
     *
     * Restricts selection updates to intended interactive regions such as
     * item text or decoration, while ignoring branch and checkbox clicks.
     *
     * @param index Model index under consideration.
     * @param event Event driving the selection query.
     * @return Selection flags to apply.
     */
    QItemSelectionModel::SelectionFlags selectionCommand(const QModelIndex& index,
                                                         const QEvent* event = nullptr) const override;

    /**
     * @brief Draws a custom row background and debug overlays.
     * @param painter Painter used for drawing.
     * @param option Style option for the row.
     * @param index Row model index.
     */
    void drawRow(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override;

private:
    std::unique_ptr<TreeWidgetPrivate> p;
};

}  // namespace stageviz
