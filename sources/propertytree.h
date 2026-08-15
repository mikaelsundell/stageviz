// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#pragma once

#include "notice.h"
#include "stageviz.h"
#include "treewidget.h"
#include <QTreeWidget>
#include <pxr/usd/usd/stage.h>

PXR_NAMESPACE_USING_DIRECTIVE

namespace stageviz {

class PropertyTreePrivate;
class ViewContext;

/**
 * @class PropertyTree
 * @brief Editable property view for the currently selected USD prim.
 *
 * Scalar attributes are edited directly in the Value column. Array attributes
 * expand to indexed elements; large arrays are split into lazily populated
 * chunks so mesh vertex data can be edited without creating every row at once.
 *
 * All authored changes are submitted through CommandStack for undo/redo.
 */
class PropertyTree : public TreeWidget {
    Q_OBJECT
public:
    PropertyTree(QWidget* parent = nullptr);
    virtual ~PropertyTree();

    ViewContext* context() const;
    void setContext(ViewContext* context);

    void close();

    void updateStage(UsdStageRefPtr stage);
    void updatePrims(const NoticeBatch& batch);
    void updateSelection(const QList<SdfPath>& paths);

private:
    QScopedPointer<PropertyTreePrivate> p;
};

}  // namespace stageviz
