// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#pragma once

#include <QListWidget>
#include <QScopedPointer>

namespace stageviz {

class ShelfListPrivate;

/**
 * @brief List widget used by the shelf view for drag-and-drop script items.
 *
 * ShelfList is a QListWidget specialization that tracks the pressed item index
 * and provides custom drag-and-drop handling for shelf entries. It is intended
 * to support internal item moves as well as custom mime export/import behavior
 * for shelf content.
 */
class ShelfList : public QListWidget {
    Q_OBJECT
public:
    /**
     * @brief Constructs an empty shelf list.
     * @param parent Optional parent widget.
     */
    ShelfList(QWidget* parent = nullptr);

    /**
     * @brief Destroys the ShelfList instance.
     */
    ~ShelfList();

    /**
     * @brief Returns the index that was last pressed with the mouse.
     *
     * This can be used during drag initiation and hover handling to identify
     * the original item under interaction.
     *
     * @return The last pressed model index, or an invalid index if none.
     */
    QModelIndex pressedIndex() const;

protected:
    /**
     * @brief Returns the supported mime types for dragged shelf items.
     */
    QStringList mimeTypes() const override;

    /**
     * @brief Creates mime data for a set of dragged shelf items.
     * @param items The items being dragged.
     * @return Newly allocated mime data owned by the caller.
     */
    QMimeData* mimeData(const QList<QListWidgetItem*>& items) const override;

    /**
     * @brief Returns the supported drop actions for the shelf list.
     */
    Qt::DropActions supportedDropActions() const override;

    /**
     * @brief Handles drag enter events for custom shelf mime data.
     * @param event The incoming drag enter event.
     */
    void dragEnterEvent(QDragEnterEvent* event) override;

    /**
     * @brief Handles drag move events while dragging over the shelf.
     * @param event The incoming drag move event.
     */
    void dragMoveEvent(QDragMoveEvent* event) override;

    /**
     * @brief Handles drop events for internal moves or imported shelf items.
     * @param event The incoming drop event.
     */
    void dropEvent(QDropEvent* event) override;

    /**
     * @brief Records mouse press state for drag handling.
     * @param event The mouse press event.
     */
    void mousePressEvent(QMouseEvent* event) override;

    /**
     * @brief Starts or updates drag interaction when the mouse moves.
     * @param event The mouse move event.
     */
    void mouseMoveEvent(QMouseEvent* event) override;

    /**
     * @brief Clears press and drag state when the mouse is released.
     * @param event The mouse release event.
     */
    void mouseReleaseEvent(QMouseEvent* event) override;

    /**
     * @brief Clears hover-related state when the cursor leaves the widget.
     * @param event The leave event.
     */
    void leaveEvent(QEvent* event) override;

private:
    Q_DISABLE_COPY_MOVE(ShelfList)
    QScopedPointer<ShelfListPrivate> p;
};

}  // namespace stageviz
