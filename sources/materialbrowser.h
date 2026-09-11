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
    enum ViewMode { Icons = 0, List, Details };

    explicit MaterialBrowser(QWidget* parent = nullptr);
    virtual ~MaterialBrowser();

    void setEntries(const QList<MaterialEntry>& entries);
    const QList<MaterialEntry>& entries() const;
    const MaterialEntry* entry(int row) const;

    int rowForMaterialPath(const SdfPath& path) const;
    bool updateEntry(int row, const MaterialEntry& entry);

    QList<int> selectedRows() const;
    QList<MaterialEntry> selectedEntries() const;
    void selectRow(int row);

    void setViewMode(ViewMode mode);
    ViewMode viewMode() const;

    void setSwatchSize(int size);
    int swatchSize() const;

    void setFilter(const QString& filter);
    QString filter() const;

    void setSwatch(int row, const QImage& image);

    /**
     * @brief Marks a swatch stale without removing its current image.
     *
     * The old image remains visible until MaterialRenderer returns the new one.
     * This prevents the material browser from blinking during edits.
     */
    void invalidateSwatch(int row);

    QImage swatch(int row) const;
    void refreshVisibleSwatches();

Q_SIGNALS:
    void selectionChanged();
    void swatchRequested(int row);

protected:
    bool eventFilter(QObject* object, QEvent* event) override;

private:
    QScopedPointer<MaterialBrowserPrivate> p;
};

}  // namespace stageviz
