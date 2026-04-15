// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#pragma once

#include "stageviz.h"

#include <QStyleOptionViewItem>
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
     * @brief Creates the tree widget.
     * @param parent Parent widget.
     */
    explicit TreeWidget(QWidget* parent = nullptr);

    /**
     * @brief Destroys the tree widget.
     */
    ~TreeWidget() override;

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
     * @brief Handles mouse press events.
     * @param event Mouse event.
     */
    void mousePressEvent(QMouseEvent* event) override;

    /**
     * @brief Handles mouse release events.
     * @param event Mouse event.
     */
    void mouseReleaseEvent(QMouseEvent* event) override;

    /**
     * @brief Handles mouse double click events.
     * @param event Mouse event.
     */
    void mouseDoubleClickEvent(QMouseEvent* event) override;

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
     * @brief Draws custom branch indicators for a row.
     * @param painter Painter used for drawing.
     * @param rect Qt branch rect.
     * @param index Row model index.
     */
    void drawBranches(QPainter* painter, const QRect& rect, const QModelIndex& index) const override;

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
