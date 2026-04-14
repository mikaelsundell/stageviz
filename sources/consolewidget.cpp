// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#include "consolewidget.h"
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

PXR_NAMESPACE_USING_DIRECTIVE

// generated files
#include "ui_consolewidget.h"

namespace stageviz {

class ConsoleWidgetPrivate : public QObject {
public:
    ConsoleWidgetPrivate();
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
        QScopedPointer<Ui_ConsoleWidget> ui;
        QPointer<ConsoleWidget> view;
        QString lastSearch;
    };
    Data d;
};

ConsoleWidgetPrivate::ConsoleWidgetPrivate() {}

void
ConsoleWidgetPrivate::init()
{
    d.ui.reset(new Ui_ConsoleWidget());
    d.ui->setupUi(d.view.data());
    d.ui->log->setReadOnly(true);
    d.ui->log->setPlainText(console()->text());
    d.ui->log->setContextMenuPolicy(Qt::CustomContextMenu);
    d.ui->previous->setIcon(style()->icon(Style::IconRole::Left));
    d.ui->next->setIcon(style()->icon(Style::IconRole::Right));
    d.view->installEventFilter(this);
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
    auto* find = new QShortcut(QKeySequence::Find, d.view.data());
    connect(find, &QShortcut::activated, this, [this]() { focusFind(); });
}

bool
ConsoleWidgetPrivate::eventFilter(QObject* object, QEvent* event)
{
    if (object == d.view) {
        switch (event->type()) {
        case QEvent::Show:
            if (d.view)
                Q_EMIT d.view->visibilityChanged(true);
            break;
        case QEvent::Hide:
            if (d.view)
                Q_EMIT d.view->visibilityChanged(false);
            break;
        default: break;
        }
    }
    if (object == d.ui->log) {
        if (event->type() == QEvent::ShortcutOverride) {
            auto* keyEvent = static_cast<QKeyEvent*>(event);

            if (keyEvent->matches(QKeySequence::SelectAll) ||
                keyEvent->matches(QKeySequence::Copy)) {
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
ConsoleWidgetPrivate::searchText() const
{
    return d.ui->find->text().trimmed();
}

void
ConsoleWidgetPrivate::resetSearchStart(QTextDocument::FindFlags flags)
{
    if (!d.ui || !d.ui->log)
        return;

    QTextCursor cursor = d.ui->log->textCursor();
    cursor.movePosition(flags.testFlag(QTextDocument::FindBackward) ? QTextCursor::End : QTextCursor::Start);
    d.ui->log->setTextCursor(cursor);
}

void
ConsoleWidgetPrivate::focusFind()
{
    if (!d.ui || !d.ui->find)
        return;

    d.ui->find->setFocus();
    d.ui->find->selectAll();
}

bool
ConsoleWidgetPrivate::findText(const QString& text, QTextDocument::FindFlags flags)
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
ConsoleWidgetPrivate::findNext()
{
    const QString text = searchText();
    if (text.isEmpty())
        return;

    findText(text);
}

void
ConsoleWidgetPrivate::findPrevious()
{
    const QString text = searchText();
    if (text.isEmpty())
        return;

    findText(text, QTextDocument::FindBackward);
}

void
ConsoleWidgetPrivate::showLogContextMenu(const QPoint& pos)
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
ConsoleWidgetPrivate::appendText(const QString& text)
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

ConsoleWidget::ConsoleWidget(QWidget* parent)
    : QWidget(parent)
    , p(new ConsoleWidgetPrivate())
{
    p->d.view = this;
    p->init();
}

ConsoleWidget::~ConsoleWidget() = default;

}  // namespace stageviz
