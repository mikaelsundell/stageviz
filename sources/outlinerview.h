// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#pragma once

#include "stageviz.h"
#include <QTreeWidget>
#include <pxr/usd/usd/stage.h>

PXR_NAMESPACE_USING_DIRECTIVE

namespace stageviz {

class OutlinerViewPrivate;

/**
 * @class OutlinerView
 * @brief Widget providing a hierarchical view of USD prims.
 *
 * Displays the prim hierarchy of the current USD stage and allows
 * users to navigate and inspect the scene structure. The view is
 * connected to a Session for stage data and a SelectionList
 * to synchronize prim selections with other viewer components.
 */
class OutlinerView : public QWidget {
    Q_OBJECT
public:
    /**
     * @brief Constructs the outliner view widget.
     *
     * @param parent Optional parent widget.
     */
    OutlinerView(QWidget* parent = nullptr);

    /**
     * @brief Destroys the OutlinerView instance.
     */
    virtual ~OutlinerView();

    /** @name Tree Control */
    ///@{

    /**
     * @brief Collapses all nodes in the outliner.
     */
    void collapse();

    /**
     * @brief Expands all nodes in the outliner.
     */
    void expand();

    /**
     * @brief Returns whether follow is enabled for outliner.
     */
    bool followEnabled();

    /**
     * @brief Enables or disables follow in the outliner
     *
     * @param enabled Follow state.
     */
    void enableFollow(bool enable);

    ///@}

private:
    QScopedPointer<OutlinerViewPrivate> p;
};

}  // namespace stageviz
