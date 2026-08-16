// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#pragma once

#include "stageviz.h"
#include <QDialog>
#include <QScopedPointer>

namespace stageviz {

class ProgressDialogPrivate;

/**
 * @class ProgressDialog
 * @brief Tool dialog hosting the application progress view.
 */
class ProgressDialog : public QDialog {
    Q_OBJECT
public:
    explicit ProgressDialog(QWidget* parent = nullptr);
    ~ProgressDialog() override;

private:
    Q_DISABLE_COPY_MOVE(ProgressDialog)
    QScopedPointer<ProgressDialogPrivate> p;
};

}  // namespace stageviz
