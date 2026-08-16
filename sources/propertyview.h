// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#pragma once

#include "stageviz.h"
#include <QScopedPointer>
#include <QWidget>
#include <pxr/usd/usd/stage.h>

PXR_NAMESPACE_USING_DIRECTIVE

namespace stageviz {

class PropertyViewPrivate;

/**
 * @class PropertyView
 * @brief Main-window view for inspecting and editing the current USD selection.
 *
 * Owns a PropertyTree and synchronizes it with the active Session stage,
 * prim changes, and selection. Property editing behavior remains implemented
 * by PropertyTree; this class provides embedded main-window ownership and
 * session wiring.
 */
class PropertyView : public QWidget {
    Q_OBJECT
public:
    explicit PropertyView(QWidget* parent = nullptr);
    ~PropertyView() override;

private:
    Q_DISABLE_COPY_MOVE(PropertyView)
    QScopedPointer<PropertyViewPrivate> p;
};

}  // namespace stageviz
