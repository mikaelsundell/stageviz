// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#pragma once

#include "materialutils.h"
#include "stageviz.h"
#include "treewidget.h"

class QColor;

namespace stageviz {

class MaterialTreePrivate;

/**
 * @class MaterialTree
 * @brief Property tree used to edit one or more selected materials.
 *
 * Slider movement emits preview signals so swatches can update without
 * authoring USD. Commit signals are emitted only when the slider is released
 * or a numeric editor finishes editing.
 */
class MaterialTree : public TreeWidget {
    Q_OBJECT
public:
    explicit MaterialTree(QWidget* parent = nullptr);
    virtual ~MaterialTree();

    void setMaterials(const QList<MaterialEntry>& materials);
    void clearMaterials();

Q_SIGNALS:
    void floatPreviewChanged(const QString& parameter, double value);
    void colorPreviewChanged(const QString& parameter, const QColor& value);

    void floatChanged(const QString& parameter, double value);
    void colorChanged(const QString& parameter, const QColor& value);

private:
    QScopedPointer<MaterialTreePrivate> p;
};

}  // namespace stageviz
