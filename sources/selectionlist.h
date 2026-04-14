// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#pragma once

#include <QList>
#include <QObject>
#include <QScopedPointer>
#include <pxr/usd/sdf/path.h>
#include <pxr/usd/usd/stage.h>

PXR_NAMESPACE_USING_DIRECTIVE

namespace stageviz {

class SelectionListPrivate;

/**
 * @class SelectionList
 * @brief Maintains the current USD prim selection.
 *
 * Provides a centralized model for tracking selected prim paths
 * within a USD stage. The model supports adding, removing, and
 * toggling selections and emits signals when the selection state
 * changes.
 *
 * This class is typically shared between viewer components such
 * as the stage tree, viewport, and property panels.
 */
class SelectionList : public QObject {
    Q_OBJECT
public:
    /**
     * @brief Constructs a SelectionList.
     *
     * @param parent Optional parent object.
     */
    SelectionList(QObject* parent = nullptr);
    /**
     * @brief Destroys the SelectionList instance.
     */
    ~SelectionList();

    /** @name Selection Queries */
    ///@{

    /**
     * @brief Returns whether the specified path is selected.
     *
     * @param path Prim path to query.
     */
    bool isSelected(const SdfPath& path) const;

    /**
     * @brief Returns the list of selected prim paths.
     */
    QList<SdfPath> paths() const;

    /**
     * @brief Returns whether the selection is empty.
     */
    bool isEmpty() const;

    /**
     * @brief Returns whether the selection state is valid.
     */
    bool isValid() const;

    ///@}

    /** @name Selection Modification */
    ///@{

    /**
     * @brief Adds prim paths to the selection.
     *
     * @param path Paths to add.
     */
    void addPaths(const QList<SdfPath>& path);

    /**
     * @brief Removes prim paths from the selection.
     *
     * @param paths Paths to remove.
     */
    void removePaths(const QList<SdfPath>& paths);

    /**
     * @brief Toggles the selection state of the specified paths.
     *
     * Selected paths become unselected and vice versa.
     *
     * @param paths Paths to toggle.
     */
    void togglePaths(const QList<SdfPath>& paths);

    /**
     * @brief Replaces the current selection with the specified paths.
     *
     * @param paths New selection paths.
     */
    void updatePaths(const QList<SdfPath>& paths);

    /**
     * @brief Clears the current selection.
     */
    void clear();

    ///@}

Q_SIGNALS:

    /**
     * @brief Emitted when the selection changes.
     *
     * @param paths Current selection paths.
     */
    void selectionChanged(const QList<SdfPath>& paths);

private:
    QScopedPointer<SelectionListPrivate> p;
};

}  // namespace stageviz
