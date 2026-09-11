// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#pragma once

#include "stageviz.h"
#include "treeitem.h"
#include <QImage>
#include <pxr/usd/sdf/path.h>

PXR_NAMESPACE_USING_DIRECTIVE

namespace stageviz {

class MaterialItemPrivate;

/**
 * @class MaterialItem
 * @brief Tree item representing one material in MaterialBrowser detail mode.
 *
 * The item stores lightweight presentation metadata only. USD reads, writes,
 * rendering, and commands remain owned by the corresponding controller classes.
 */
class MaterialItem : public TreeItem {
public:
    enum Column { Name = 0, Type, Path };

    MaterialItem(QTreeWidget* parent);
    MaterialItem(QTreeWidgetItem* parent);
    virtual ~MaterialItem();

    TreeItem::ItemStates itemStates() const;

    int sourceRow() const;
    void setSourceRow(int row);

    SdfPath materialPath() const;
    void setMaterialPath(const SdfPath& path);

    SdfPath shaderPath() const;
    void setShaderPath(const SdfPath& path);

    QString shaderId() const;
    void setShaderId(const QString& shaderId);

    QImage swatch() const;
    void setSwatch(const QImage& image);

private:
    QScopedPointer<MaterialItemPrivate> p;
};

}  // namespace stageviz
