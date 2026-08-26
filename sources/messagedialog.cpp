// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#include "messagedialog.h"

#include <QPointer>

// generated files
#include "ui_messagedialog.h"

namespace stageviz {

class MessageDialogPrivate : public QObject {
    Q_OBJECT
public:
    MessageDialogPrivate();
    void init();
    bool exec();
    int execResult();

public:
    struct Data {
        QString title;
        QString type;
        QString url;
        QString text;
        QString details;
        QString acceptText;
        QString rejectText;
        int iconSize;
        bool showIcon;
        bool showReject;
        bool fixedHeight;
        QPointer<MessageDialog> dialog;
        QScopedPointer<Ui_MessageDialog> ui;
    };
    Data d;
};

MessageDialogPrivate::MessageDialogPrivate()
{
    d.iconSize = 128;
    d.showIcon = true;
    d.showReject = false;
    d.fixedHeight = true;
}

void
MessageDialogPrivate::init()
{
    // ui
    d.ui.reset(new Ui_MessageDialog());
    d.ui->setupUi(d.dialog.data());
    // connect
    connect(d.ui->accept, &QPushButton::clicked, this, [this]() { d.dialog->done(QDialog::Accepted); });
    connect(d.ui->reject, &QPushButton::clicked, this, [this]() { d.dialog->done(2); });
}

bool
MessageDialogPrivate::exec()
{
    return execResult() == QDialog::Accepted;
}

int
MessageDialogPrivate::execResult()
{
    d.dialog->setWindowTitle(d.type);
    d.ui->icon->setFixedSize(d.iconSize, d.iconSize);
    d.ui->icon->setScaledContents(true);

    if (d.showIcon) {
        d.ui->icon->show();
    }
    else {
        d.ui->icon->hide();
    }

    d.ui->title->setText(d.title);

    if (d.url.isEmpty()) {
        d.ui->url->hide();
    }
    else {
        d.ui->url->setText(QString("<a href='%1'>%1</a>").arg(d.url));
        d.ui->url->setOpenExternalLinks(true);
        d.ui->url->show();
    }

    if (d.text.isEmpty()) {
        d.ui->text->hide();
    }
    else {
        d.ui->text->setText(d.text);
        d.ui->text->show();
    }

    if (d.details.isEmpty()) {
        d.ui->details->hide();
    }
    else {
        d.ui->details->setText(d.details);
        d.ui->details->show();
    }

    d.ui->accept->setText(d.acceptText);

    if (d.showReject) {
        d.ui->reject->setText(d.rejectText);
        d.ui->reject->show();
    }
    else {
        d.ui->reject->hide();
    }

    d.dialog->adjustSize();

    if (d.fixedHeight) {
        d.dialog->setMaximumHeight(d.dialog->sizeHint().height());
    }
    else {
        d.dialog->setMaximumHeight(QWIDGETSIZE_MAX);
    }
    return d.dialog->exec();
}

MessageDialog::MessageDialog(QWidget* parent)
    : QDialog(parent)
    , p(new MessageDialogPrivate())
{
    p->d.dialog = this;
    p->init();
}

MessageDialog::~MessageDialog() {}

bool
MessageDialog::information(QWidget* parent, const QString& title, const QString& text)
{
    MessageDialog box(parent);
    box.p->d.type = "Information";
    box.p->d.title = title;
    box.p->d.text = text;
    box.p->d.acceptText = tr("Close");
    box.p->d.iconSize = 64;
    box.p->d.showReject = false;
    return box.p->exec();
}

bool
MessageDialog::warning(QWidget* parent, const QString& title, const QString& text)
{
    MessageDialog box(parent);
    box.p->d.type = "Warning";
    box.p->d.title = title;
    box.p->d.text = text;
    box.p->d.acceptText = tr("Close");
    box.p->d.showIcon = false;
    box.p->d.showReject = false;
    return box.p->exec();
}

bool
MessageDialog::question(QWidget* parent, const QString& title, const QString& text)
{
    MessageDialog box(parent);
    box.p->d.type = "Question";
    box.p->d.title = title;
    box.p->d.text = text;
    box.p->d.acceptText = tr("Yes");
    box.p->d.rejectText = tr("No");
    box.p->d.showIcon = false;
    box.p->d.showReject = true;
    return box.p->exec();
}

MessageDialog::SaveResult
MessageDialog::saveQuestion(QWidget* parent, const QString& title, const QString& text)
{
    MessageDialog box(parent);
    box.p->d.type = "Question";
    box.p->d.title = title;
    box.p->d.text = text;
    box.p->d.acceptText = tr("Save");
    box.p->d.rejectText = tr("Don't Save");
    box.p->d.showIcon = false;
    box.p->d.showReject = true;

    const int result = box.p->execResult();
    if (result == QDialog::Accepted)
        return SaveResult::Save;
    if (result == 2)
        return SaveResult::DontSave;
    return SaveResult::Cancel;
}

bool
MessageDialog::about(QWidget* parent, const QString& title, const QString& heading, const QString& details,
                     const QString& url)
{
    MessageDialog box(parent);
    box.p->d.type = title;
    box.p->d.title = title;
    box.p->d.url = url;
    box.p->d.text = heading;
    box.p->d.details = details;
    box.p->d.acceptText = tr("Close");
    box.p->d.showReject = false;
    box.p->d.fixedHeight = false;
    return box.p->exec();
}

bool
MessageDialog::update(QWidget* parent, const QString& title, const QString& heading, const QString& details,
                      const QString& url)
{
    MessageDialog box(parent);
    box.p->d.type = "Update";
    box.p->d.title = title;
    box.p->d.url = url;
    box.p->d.text = heading;
    box.p->d.details = details;
    box.p->d.acceptText = tr("Download");
    box.p->d.rejectText = tr("Skip");
    box.p->d.showReject = true;
    return box.p->exec();
}

}  // namespace stageviz

#include "messagedialog.moc"
