// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#pragma once

#include "stageviz.h"
#include <QDialog>

namespace stageviz {

class PropertyDialogPrivate;

/**
 * @class PropertyDialog
 * @brief Dialog for inspecting properties of the current USD selection.
 *
 * Owns a PropertyTree and synchronizes it with the active Session stage,
 * prim changes, and selection. The property presentation and behavior remain
 * implemented by PropertyTree; this dialog only provides standalone window
 * ownership and session wiring.
 */
class PropertyDialog : public QDialog {
    Q_OBJECT
public:
    /**
     * @brief Constructs the property editor dialog.
     *
     * @param parent Optional parent widget.
     */
    PropertyDialog(QWidget* parent = nullptr);

    /** @brief Destroys the PropertyDialog instance. */
    virtual ~PropertyDialog();

private:
    Q_DISABLE_COPY_MOVE(PropertyDialog)
    QScopedPointer<PropertyDialogPrivate> p;
};

}  // namespace stageviz
