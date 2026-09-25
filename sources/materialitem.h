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
    /**
     * @brief Identifies the name, shader-type, and USD-path columns.
     */
    enum Column { Name = 0, Type, Path };

    /**
     * @brief Creates an item owned by the supplied tree.
     */
    MaterialItem(QTreeWidget* parent);
    /**
     * @brief Creates an item owned by the supplied parent item.
     */
    MaterialItem(QTreeWidgetItem* parent);
    /**
     * @brief Releases the item's presentation metadata.
     */
    virtual ~MaterialItem();

    /**
     * @brief Returns the item state used by the shared tree delegate.
     */
    TreeItem::ItemStates itemStates() const;

    /**
     * @brief Returns this item's index in the browser's source entries.
     */
    int sourceRow() const;
    
    /**
     * @brief Stores the browser source-entry index.
     */
    void setSourceRow(int row);

    /**
     * @brief Returns the represented material's USD path.
     */
    SdfPath materialPath() const;
    /**
     * @brief Updates the represented material path; does not edit USD.
     */
    void setMaterialPath(const SdfPath& path);

    /**
     * @brief Returns the surface shader's USD path.
     */
    SdfPath shaderPath() const;
    
    /**
     * @brief Stores the surface shader path.
     */
    void setShaderPath(const SdfPath& path);

    /**
     * @brief Returns the surface shader identifier.
     */
    QString shaderId() const;
    
    /**
     * @brief Stores the shader identifier used for presentation.
     */
    void setShaderId(const QString& shaderId);

    /**
     * @brief Returns the cached preview image.
     */
    QImage swatch() const;
    
    /**
     * @brief Updates the item's preview image.
     */
    void setSwatch(const QImage& image);

private:
    QScopedPointer<MaterialItemPrivate> p;
};

}  // namespace stageviz
