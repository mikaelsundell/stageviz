// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#pragma once

#include "stageviz.h"

#include <QListWidgetItem>
#include <QScopedPointer>
#include <QVariant>
#include <QWidget>

namespace stageviz {

class ShelfWidgetPrivate;

/**
 * @brief A small shelf view for storing reusable Python script snippets.
 *
 * ShelfWidget presents scripts as icon items with a user-visible title and the
 * full script source stored internally. Items can be activated, renamed,
 * removed, serialized, and restored.
 *
 * Typical usage:
 * - add scripts dropped from the editor or log
 * - activate a script to load and/or run it in the Python view
 * - expose a context menu for rename, export, or removal
 * - persist shelf contents through QVariant-based serialization
 */
class ShelfWidget : public QWidget {
    Q_OBJECT
public:
    /**
     * @brief Construct an empty shelf widget.
     * @param parent Optional parent widget.
     */
    ShelfWidget(QWidget* parent = nullptr);

    /**
     * @brief Destroy the shelf widget.
     */
    ~ShelfWidget() override;

    /**
     * @brief Add a script item to the shelf.
     *
     * If @p name is empty, a title is derived from the script contents.
     *
     * @param code Script source code.
     * @param name Optional display name for the shelf item.
     */
    void addScript(const QString& code, const QString& name = QString());

    /**
     * @brief Begin inline editing of an existing shelf item title.
     * @param item The item to rename.
     */
    void editScript(QListWidgetItem* item);

    /**
     * @brief Remove a script item from the shelf.
     * @param item The item to remove.
     */
    void removeScript(QListWidgetItem* item);

    /**
     * @brief Remove all script items from the shelf.
     */
    void clear();

    /**
     * @brief Return the number of script items currently in the shelf.
     * @return Item count.
     */
    int count() const;

    /**
     * @brief Serialize the shelf contents to a QVariant list.
     *
     * Each entry contains at least the item display name and script code.
     *
     * @return Serialized shelf data suitable for settings persistence.
     */
    QVariantList toVariantList() const;

    /**
     * @brief Restore shelf contents from serialized data.
     *
     * Existing items are cleared before new items are created.
     *
     * @param scripts Serialized shelf data previously produced by toVariantList().
     */
    void fromVariantList(const QVariantList& scripts);

    /**
      * @brief Return the preferred size of the shelf widget.
      *
      * The shelf reports a compact default height suitable for a single row of
      * shelf items, while still allowing parent layouts or splitters to resize it.
      *
      * @return Preferred widget size.
      */
    QSize sizeHint() const override;

    /**
      * @brief Return the minimum useful size of the shelf widget.
      *
      * This ensures the shelf remains usable when compressed, without forcing an
      * overly large page height in parent layouts.
      *
      * @return Minimum recommended widget size.
      */
    QSize minimumSizeHint() const override;

Q_SIGNALS:
    /**
     * @brief Emitted when a shelf item is activated.
     * @param code The full script source associated with the activated item.
     */
    void itemActivated(const QString& code);

    /**
     * @brief Emitted when the shelf requests a context menu for an item or empty space.
     * @param pos Position in shelf-local coordinates.
     * @param item The item under the cursor, or nullptr if none.
     */
    void itemContextMenuRequested(const QPoint& pos, QListWidgetItem* item);

    /**
     * @brief Emitted whenever the shelf contents or item names change.
     */
    void changed();

protected:
    /**
     * @brief Event filter override for internal interaction handling.
     * @param object Filtered object.
     * @param event Incoming event.
     * @return True if the event was handled, otherwise false.
     */
    bool eventFilter(QObject* object, QEvent* event) override;

private:
    QScopedPointer<ShelfWidgetPrivate> p;
};

}  // namespace stageviz
