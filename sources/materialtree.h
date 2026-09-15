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
    /**
     * @brief Creates the material-parameter editors.
     */
    explicit MaterialTree(QWidget* parent = nullptr);
    /**
     * @brief Releases the editors and private state.
     */
    virtual ~MaterialTree();

    /**
     * @brief Displays parameters shared by the selected material descriptions.
     */
    void setMaterials(const QList<MaterialEntry>& materials);
    /**
     * @brief Removes the current material editors.
     */
    void clearMaterials();

Q_SIGNALS:
    /**
     * @brief Emitted for an uncommitted scalar preview; does not itself author USD.
     */
    void floatPreviewChanged(const QString& parameter, double value);
    /**
     * @brief Emitted for an uncommitted color preview; does not itself author USD.
     */
    void colorPreviewChanged(const QString& parameter, const QColor& value);

    /**
     * @brief Requests committing a scalar parameter to the selected materials.
     */
    void floatChanged(const QString& parameter, double value);
    /**
     * @brief Requests committing a color parameter to the selected materials.
     */
    void colorChanged(const QString& parameter, const QColor& value);

private:
    QScopedPointer<MaterialTreePrivate> p;
};

}  // namespace stageviz
