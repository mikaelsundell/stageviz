// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#pragma once

#include "treewidget.h"
#include <QSet>
#include <QString>
#include <memory>

namespace stageviz {

class IndexTreePrivate;

/**
 * @class IndexTree
 * @brief TreeWidget with optimized selection for large hierarchies.
 *
 * Maintains selection outside Qt's QItemSelectionModel using item pointers
 * or path identifiers. Selected items and ancestors are highlighted by
 * TreeWidget without synchronizing the native selection model.
 *
 * Selection methods hide the corresponding QTreeWidget methods where they
 * are not virtual. Use these methods to access the active selection state.
 */
class IndexTree : public TreeWidget {
    Q_OBJECT
public:
    /**
     * @brief Creates the indexed tree widget.
     * @param parent Parent widget.
     */
    explicit IndexTree(QWidget* parent = nullptr);

    /**
     * @brief Destroys the indexed tree widget.
     */
    ~IndexTree() override;

    /**
     * @brief Sets external selection using item path identifiers.
     *
     * Selected rows and their ancestors are highlighted without updating
     * Qt's native selection model.
     *
     * @param selected Identifiers of selected items.
     * @param ancestors Identifiers of ancestors containing selected descendants.
     * @param pathRole Data role containing each identifier in column zero.
     * @note The caller must keep identifiers synchronized with the tree.
     */
    void setSelection(const QSet<QString>& selected, const QSet<QString>& ancestors, int pathRole);

    /**
     * @brief Sets external selection using tree item pointers.
     *
     * Avoids resolving paths during row painting and replaces any previous
     * path-based selection.
     *
     * @param selected Items to highlight as selected.
     * @param ancestors Items to highlight as ancestors of selected descendants.
     * @warning Pointers are non-owning. Clear or replace the selection before
     *          deleting or rebuilding referenced items.
     */
    void setSelectionItems(const QSet<QTreeWidgetItem*>& selected, const QSet<QTreeWidgetItem*>& ancestors);

    /**
     * @brief Clears native and externally managed selection state.
     *
     * Also removes external ancestor highlighting.
     * @note Hides QTreeWidget::clearSelection().
     */
    void clearSelection();

    /**
     * @brief Returns selected items from the active selection model.
     * @return Non-owning item pointers, in unspecified order.
     * @note Resolving path-based selection requires traversing the tree.
     * @note Hides QTreeWidget::selectedItems().
     */
    QList<QTreeWidgetItem*> selectedItems() const;

    /**
     * @brief Reports whether an item is selected in the active model.
     * @param item Item to query; nullptr returns false.
     * @return True if the item is selected.
     */
    bool isItemSelected(const QTreeWidgetItem* item) const override;

    /**
     * @brief Reports whether external selection is active.
     * @return True when selection is managed outside Qt.
     */
    bool hasExternalSelection() const override;

    /**
     * @brief Returns the number of selected items or identifiers.
     * @return Selection count in the active model.
     * @note Path identifiers need not correspond to instantiated tree items.
     */
    qsizetype selectedItemCount() const;

    /**
     * @brief Changes an item's selection using the active model.
     * @param item Item to modify; nullptr or non-selectable items are ignored.
     * @param selected True to select, false to deselect.
     * @note Editing path-based selection converts it to pointer-based selection.
     */
    void setItemSelected(QTreeWidgetItem* item, bool selected);

    /**
     * @brief Selects all selectable items using the active selection model.
     * @note External selection traverses the tree once.
     */
    void selectAll() override;

    /**
     * @brief Reports whether an item contains selected descendants.
     * @param item Item to query; nullptr returns false.
     * @return True if the item has a selected descendant.
     */
    bool hasSelectedDescendants(const QTreeWidgetItem* item) const override;

protected:
    /**
     * @brief Handles keyboard navigation using the active selection model.
     *
     * Supports optimized arrow-key selection without updating Qt's native
     * selection model.
     *
     * @param event Incoming key event.
     */
    void keyPressEvent(QKeyEvent* event) override;

    /**
     * @brief Handles mouse selection using the active selection model.
     *
     * Supports modifier-based selection while preserving native Qt handling
     * when external selection is not active.
     *
     * @param event Incoming mouse event.
     */
    void mousePressEvent(QMouseEvent* event) override;

    /** @brief Collapses an existing multi-selection on click release, not drag press. */
    void mouseReleaseEvent(QMouseEvent* event) override;

    /**
     * @brief Returns the selection command for a given event.
     *
     * Prevents Qt from modifying native selection during externally managed
     * mouse interactions.
     *
     * @param index Model index under consideration.
     * @param event Event driving the selection query.
     * @return Selection flags to apply.
     */
    QItemSelectionModel::SelectionFlags selectionCommand(const QModelIndex& index,
                                                         const QEvent* event = nullptr) const override;

private:
    std::unique_ptr<IndexTreePrivate> p;
};

}  // namespace stageviz
