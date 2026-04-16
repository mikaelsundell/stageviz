// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#include "consoledialog.h"
#include "application.h"
#include "console.h"
#include "style.h"
#include <QAbstractButton>
#include <QApplication>
#include <QEvent>
#include <QKeyEvent>
#include <QMenu>
#include <QPointer>
#include <QScrollBar>
#include <QShortcut>
#include <QTextCursor>
#include <QTextDocument>

// generated files
#include "ui_consoledialog.h"

namespace stageviz {

class ConsoleDialogPrivate : public QObject {
public:
    ConsoleDialogPrivate();
    void init();
    bool eventFilter(QObject* object, QEvent* event) override;
    void appendText(const QString& text);
    void showLogContextMenu(const QPoint& pos);
    bool findText(const QString& text, QTextDocument::FindFlags flags = {});
    void findNext();
    void findPrevious();
    void focusFind();
    QString searchText() const;
    void resetSearchStart(QTextDocument::FindFlags flags);

public:
    struct Data {
        QString lastSearch;
        QScopedPointer<Ui_ConsoleDialog> ui;
        QPointer<ConsoleDialog> dialog;
    };
    Data d;
};

ConsoleDialogPrivate::ConsoleDialogPrivate() {}

void
ConsoleDialogPrivate::init()
{
    d.ui.reset(new Ui_ConsoleDialog());
    d.ui->setupUi(d.dialog.data());
    d.ui->log->setReadOnly(true);
    d.ui->log->setPlainText(console()->text());
    d.ui->log->setContextMenuPolicy(Qt::CustomContextMenu);
    d.ui->previous->setIcon(style()->icon(Style::IconRole::Left));
    d.ui->next->setIcon(style()->icon(Style::IconRole::Right));
    d.dialog->installEventFilter(this);
    d.ui->log->installEventFilter(this);
    d.ui->find->installEventFilter(this);
    // connect
    connect(d.ui->log, &QWidget::customContextMenuRequested, this,
            [this](const QPoint& pos) { showLogContextMenu(pos); });

    connect(console(), &Console::textAppended, this, [this](const QString& text) { appendText(text); });

    connect(d.ui->find, &QLineEdit::textChanged, this, [this](const QString&) { d.lastSearch.clear(); });

    connect(d.ui->find, &QLineEdit::returnPressed, this, [this]() {
        if (QApplication::keyboardModifiers().testFlag(Qt::ShiftModifier))
            findPrevious();
        else
            findNext();
    });
    connect(d.ui->next, &QAbstractButton::clicked, this, [this]() { findNext(); });
    connect(d.ui->previous, &QAbstractButton::clicked, this, [this]() { findPrevious(); });
    auto* find = new QShortcut(QKeySequence::Find, d.dialog.data());
    connect(find, &QShortcut::activated, this, [this]() { focusFind(); });
}

bool
ConsoleDialogPrivate::eventFilter(QObject* object, QEvent* event)
{
    if (object == d.dialog) {
        switch (event->type()) {
        case QEvent::Show:
            if (d.dialog)
                Q_EMIT d.dialog->visibilityChanged(true);
            break;
        case QEvent::Hide:
            if (d.dialog)
                Q_EMIT d.dialog->visibilityChanged(false);
            break;
        default: break;
        }
    }
    if (object == d.ui->log) {
        if (event->type() == QEvent::ShortcutOverride) {
            auto* keyEvent = static_cast<QKeyEvent*>(event);

            if (keyEvent->matches(QKeySequence::SelectAll) || keyEvent->matches(QKeySequence::Copy)) {
                event->accept();
                return true;
            }
        }
        if (event->type() == QEvent::KeyPress) {
            auto* keyEvent = static_cast<QKeyEvent*>(event);

            if (keyEvent->matches(QKeySequence::SelectAll)) {
                d.ui->log->selectAll();
                return true;
            }

            if (keyEvent->matches(QKeySequence::Copy)) {
                d.ui->log->copy();
                return true;
            }
        }
    }
    if (object == d.ui->find && event->type() == QEvent::KeyPress) {
        auto* keyEvent = static_cast<QKeyEvent*>(event);
        if (keyEvent->key() == Qt::Key_Escape) {
            d.ui->find->clearFocus();
            d.ui->log->setFocus();
            return true;
        }
    }

    return QObject::eventFilter(object, event);
}

QString
ConsoleDialogPrivate::searchText() const
{
    return d.ui->find->text().trimmed();
}

void
ConsoleDialogPrivate::resetSearchStart(QTextDocument::FindFlags flags)
{
    if (!d.ui || !d.ui->log)
        return;

    QTextCursor cursor = d.ui->log->textCursor();
    cursor.movePosition(flags.testFlag(QTextDocument::FindBackward) ? QTextCursor::End : QTextCursor::Start);
    d.ui->log->setTextCursor(cursor);
}

void
ConsoleDialogPrivate::focusFind()
{
    if (!d.ui || !d.ui->find)
        return;

    d.ui->find->setFocus();
    d.ui->find->selectAll();
}

bool
ConsoleDialogPrivate::findText(const QString& text, QTextDocument::FindFlags flags)
{
    if (text.isEmpty())
        return false;
    if (d.lastSearch != text) {
        d.lastSearch = text;
        resetSearchStart(flags);
    }
    if (d.ui->log->find(text, flags))
        return true;
    resetSearchStart(flags);
    return d.ui->log->find(text, flags);
}

void
ConsoleDialogPrivate::findNext()
{
    const QString text = searchText();
    if (text.isEmpty())
        return;

    findText(text);
}

void
ConsoleDialogPrivate::findPrevious()
{
    const QString text = searchText();
    if (text.isEmpty())
        return;

    findText(text, QTextDocument::FindBackward);
}

void
ConsoleDialogPrivate::showLogContextMenu(const QPoint& pos)
{
    QMenu* menu = d.ui->log->createStandardContextMenu();
    menu->addSeparator();

    QAction* findAction = menu->addAction("Find");
    connect(findAction, &QAction::triggered, this, [this]() { focusFind(); });

    QAction* findNextAction = menu->addAction("Find Next");
    connect(findNextAction, &QAction::triggered, this, [this]() { findNext(); });

    QAction* findPreviousAction = menu->addAction("Find Previous");
    connect(findPreviousAction, &QAction::triggered, this, [this]() { findPrevious(); });

    menu->addSeparator();

    QAction* clearAction = menu->addAction("Clear");
    connect(clearAction, &QAction::triggered, this, [this]() { d.ui->log->clear(); });

    menu->exec(d.ui->log->mapToGlobal(pos));
    delete menu;
}

void
ConsoleDialogPrivate::appendText(const QString& text)
{
    if (!d.ui || !d.ui->log)
        return;

    QScrollBar* scrollBar = d.ui->log->verticalScrollBar();
    const bool atBottom = !scrollBar || scrollBar->value() == scrollBar->maximum();

    const QTextCursor currentCursor = d.ui->log->textCursor();
    const int originalPosition = currentCursor.position();
    const int originalAnchor = currentCursor.anchor();

    QTextCursor endCursor = d.ui->log->textCursor();
    endCursor.movePosition(QTextCursor::End);
    d.ui->log->setTextCursor(endCursor);
    d.ui->log->insertPlainText(text);

    const bool searching = d.ui->find && !searchText().isEmpty() && d.ui->find->hasFocus();

    if (searching) {
        QTextCursor restoreCursor = d.ui->log->textCursor();
        restoreCursor.setPosition(originalAnchor);
        restoreCursor.setPosition(originalPosition, QTextCursor::KeepAnchor);
        d.ui->log->setTextCursor(restoreCursor);
    }
    else if (atBottom) {
        d.ui->log->moveCursor(QTextCursor::End);
    }
}

ConsoleDialog::ConsoleDialog(QWidget* parent)
    : QDialog(parent)
    , p(new ConsoleDialogPrivate())
{
    p->d.dialog = this;
    p->init();
}

ConsoleDialog::~ConsoleDialog() = default;

}  // namespace stageviz
