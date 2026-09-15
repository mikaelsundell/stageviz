// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#pragma once

#include "stageviz.h"
#include <QDialog>
#include <QScopedPointer>

namespace stageviz {

class MaterialDialogPrivate;

/**
 * @class MaterialDialog
 * @brief Native Stageviz material browser and editor.
 *
 * MaterialDialog coordinates MaterialBrowser, MaterialTree, MaterialRenderer,
 * the current Stageviz Session, and the command stack. Presentation and swatch
 * rendering details remain delegated to the corresponding classes.
 */
class MaterialDialog : public QDialog {
    Q_OBJECT
public:
    /**
     * @brief Creates the material browser/editor and its controllers.
     */
    explicit MaterialDialog(QWidget* parent = nullptr);
    /**
     * @brief Releases the dialog and its owned controllers.
     */
    virtual ~MaterialDialog();

protected:
    /**
     * @brief Refreshes material presentation when the dialog becomes visible.
     */
    void showEvent(QShowEvent* event) override;

private:
    QScopedPointer<MaterialDialogPrivate> p;
};

}  // namespace stageviz
