// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#pragma once

#include "stageviz.h"
#include <QList>
#include <QPoint>
#include <pxr/usd/sdf/path.h>
#include <pxr/usd/usd/stage.h>

PXR_NAMESPACE_USING_DIRECTIVE

class QWidget;

namespace stageviz {

class ViewContext;

/**
 * @class ContextMenu
 * @brief Shared prim context menu for stage and viewport widgets.
 *
 * Builds and executes the common USD prim context menu used by widgets that
 * operate on the shared stage selection.
 */
class ContextMenu {
public:
    /**
     * @brief Shows the prim context menu.
     *
     * @param parent Parent widget for the menu.
     * @param context View context used for stage locking and command execution.
     * @param stage Stage containing the selected prims.
     * @param globalPos Global screen position where the menu is shown.
     * @param paths Selected prim paths.
     * @param maskPaths Current isolation mask.
     * @param createParentPath Parent path used by the New xform action.
     * @param payloadEnabled Whether payload-oriented selection behavior is active.
     */
    static void exec(QWidget* parent, ViewContext* context, UsdStageRefPtr usdStage, const QPoint& globalPos,
                     const QList<SdfPath>& paths, const QList<SdfPath>& maskPaths, const SdfPath& createParentPath,
                     bool payloadEnabled = false);
};

}  // namespace stageviz
