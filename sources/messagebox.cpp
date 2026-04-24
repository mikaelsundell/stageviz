// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#include "messagebox.h"
#include <QPointer>

// generated files
#include "ui_messagebox.h"

namespace stageviz {
class MessageBoxPrivate : public QObject {
    Q_OBJECT
public:
    MessageBoxPrivate();
    void init();
    bool exec();

public:
    struct Data {
        QString title;
        QString url;
        QString text;
        QString details;
        QString acceptText;
        QString rejectText;
        int iconSize;
        bool showIcon;
        bool showReject;
        QPointer<MessageBox> dialog;
        QScopedPointer<Ui_MessageBox> ui;
    };
    Data d;
};

MessageBoxPrivate::MessageBoxPrivate()
{
    d.iconSize = 128;
    d.showIcon = true;
    d.showReject = false;
}

void
MessageBoxPrivate::init()
{
    // ui
    d.ui.reset(new Ui_MessageBox());
    d.ui->setupUi(d.dialog.data());
    // connect
    connect(d.ui->accept, &QPushButton::clicked, this, [this]() { d.dialog->done(QDialog::Accepted); });
    connect(d.ui->reject, &QPushButton::clicked, this, [this]() { d.dialog->done(QDialog::Rejected); });
}

bool
MessageBoxPrivate::exec()
{
    d.dialog->setWindowTitle(d.title);

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
    d.dialog->setMaximumHeight(d.dialog->sizeHint().height());

    return d.dialog->exec() == QDialog::Accepted;
}

MessageBox::MessageBox(QWidget* parent)
    : QDialog(parent)
    , p(new MessageBoxPrivate())
{
    p->d.dialog = this;
    p->init();
}

MessageBox::~MessageBox() {}

bool
MessageBox::information(QWidget* parent, const QString& title, const QString& text)
{
    MessageBox box(parent);
    box.p->d.title = title;
    box.p->d.text = text;
    box.p->d.acceptText = tr("Close");
    box.p->d.iconSize = 64;
    box.p->d.showReject = false;
    return box.p->exec();
}

bool
MessageBox::warning(QWidget* parent, const QString& title, const QString& text)
{
    MessageBox box(parent);
    box.p->d.title = title;
    box.p->d.text = text;
    box.p->d.acceptText = tr("Close");
    box.p->d.iconSize = 64;
    box.p->d.showReject = false;
    return box.p->exec();
}

bool
MessageBox::question(QWidget* parent, const QString& title, const QString& text)
{
    MessageBox box(parent);
    box.p->d.title = title;
    box.p->d.text = text;
    box.p->d.acceptText = tr("Yes");
    box.p->d.rejectText = tr("No");
    box.p->d.iconSize = 64;
    box.p->d.showReject = true;
    return box.p->exec();
}

bool
MessageBox::about(QWidget* parent, const QString& title, const QString& heading, const QString& details,
                  const QString& url)
{
    MessageBox box(parent);
    box.p->d.title = title;
    box.p->d.url = url;
    box.p->d.text = heading;
    box.p->d.details = details;
    box.p->d.acceptText = tr("Close");
    box.p->d.showReject = false;
    return box.p->exec();
}

bool
MessageBox::update(QWidget* parent, const QString& title, const QString& heading, const QString& details,
                   const QString& url)
{
    MessageBox box(parent);
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

#include "messagebox.moc"