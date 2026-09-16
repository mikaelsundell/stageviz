// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#pragma once

#include "materialutils.h"
#include "stageviz.h"
#include <QWidget>

namespace stageviz {

class MaterialBrowserPrivate;

/**
 * @class MaterialBrowser
 * @brief Material browser with icon, list, and detail presentation modes.
 *
 * MaterialBrowser owns its complete user interface through materialbrowser.ui.
 * Callers use this interface instead of accessing the internal Qt widgets.
 */
class MaterialBrowser : public QWidget {
    Q_OBJECT
public:
    /**
     * @brief Selects icon, list, or detail presentation; all modes share the same entries.
     */
    enum ViewMode { Icons = 0, List, Details };

    /**
     * @brief Creates the browser and its owned child widgets.
     */
    explicit MaterialBrowser(QWidget* parent = nullptr);
    /**
     * @brief Releases the browser's presentation state.
     */
    virtual ~MaterialBrowser();

    /**
     * @brief Replaces the browser entries and updates their presentation.
     */
    void setEntries(const QList<MaterialEntry>& entries);
    /**
     * @brief Returns borrowed entry storage, valid until the entries are modified.
     */
    const QList<MaterialEntry>& entries() const;
    /**
     * @brief Returns a borrowed entry, or nullptr for an invalid row; mutations may invalidate it.
     */
    const MaterialEntry* entry(int row) const;

    /**
     * @brief Returns the source row for a material path, or -1 if absent.
     */
    int rowForMaterialPath(const SdfPath& path) const;
    /**
     * @brief Updates the matching entry's material path without authoring USD.
     */
    void remapEntryPath(const SdfPath& oldPath, const SdfPath& newPath);
    /**
     * @brief Replaces and refreshes one row; returns false for an invalid row.
     */
    bool updateEntry(int row, const MaterialEntry& entry);

    /**
     * @brief Returns selected source-entry indices, independent of the active presentation.
     */
    QList<int> selectedRows() const;
    /**
     * @brief Returns copies of the selected material descriptions.
     */
    QList<MaterialEntry> selectedEntries() const;

    /**
     * @brief Selects a source-entry row in the active presentation.
     */
    void selectRow(int row);
    /**
     * @brief Updates the active presentation to select the supplied source-entry rows.
     */
    void selectRows(const QList<int>& rows);

    /**
     * @brief Switches presentation while retaining the browser's entries.
     */
    void setViewMode(ViewMode mode);
    /**
     * @brief Returns the active presentation mode.
     */
    ViewMode viewMode() const;

    /**
     * @brief Sets the requested preview size in widget pixels.
     */
    void setSwatchSize(int size);
    /**
     * @brief Returns the configured preview size in widget pixels.
     */
    int swatchSize() const;

    /**
     * @brief Applies a text filter to the displayed entries.
     */
    void setFilter(const QString& filter);
    /**
     * @brief Returns the current filter text.
     */
    QString filter() const;

    /**
     * @brief Stores a rendered preview for a source row and refreshes its presentation.
     */
    void setSwatch(int row, const QImage& image);

    /**
     * @brief Marks a swatch stale without removing its current image.
     *
     * The old image remains visible until MaterialRenderer returns the new one.
     * This prevents the material browser from blinking during edits.
     */
    void invalidateSwatch(int row);

    /**
     * @brief Returns the cached preview for a source row.
     */
    QImage swatch(int row) const;
    /**
     * @brief Requests missing or stale previews needed by the current presentation.
     */
    void refreshVisibleSwatches();

Q_SIGNALS:
    /**
     * @brief Emitted when the browser selection changes.
     */
    void selectionChanged();
    /**
     * @brief Requests rendering for the supplied source-entry row.
     */
    void swatchRequested(int row);
    /**
     * @brief Legacy assignment request signal.
     *
     * Context-menu Assign is handled directly by MaterialBrowser and does not
     * emit this signal. It is retained for source compatibility with existing
     * controller connections.
     */
    void assignRequested();
    /**
     * @brief Requests creation of a material through the controller.
     */
    void newMaterialRequested();
    /**
     * @brief Requests deletion of the selected materials.
     */
    void deleteRequested();
    /**
     * @brief Requests renaming the material at path to the supplied name.
     */
    void renameRequested(const SdfPath& path, const QString& name);

protected:
    /**
     * @brief Handles browser interactions on its child widgets.
     */
    bool eventFilter(QObject* object, QEvent* event) override;

private:
    QScopedPointer<MaterialBrowserPrivate> p;
};

}  // namespace stageviz
