// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#include "progressdialog.h"
#include <QPointer>

// generated files
#include "ui_progressdialog.h"

namespace stageviz {

class ProgressDialogPrivate : public QObject {
public:
    void init();

    struct Data {
        QScopedPointer<Ui_ProgressDialog> ui;
        QPointer<ProgressDialog> dialog;
    };
    Data d;
};

void
ProgressDialogPrivate::init()
{
    d.ui.reset(new Ui_ProgressDialog());
    d.ui->setupUi(d.dialog.data());
}

ProgressDialog::ProgressDialog(QWidget* parent)
    : QDialog(parent)
    , p(new ProgressDialogPrivate())
{
    p->d.dialog = this;
    p->init();
}

ProgressDialog::~ProgressDialog() = default;

}  // namespace stageviz
