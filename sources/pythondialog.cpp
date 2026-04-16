// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#include "pythondialog.h"
#include "application.h"
#include "mime.h"
#include "pythoninterpreter.h"
#include "qtutils.h"
#include "session.h"
#include "settings.h"
#include "shelfwidget.h"
#include "style.h"
#include <QAction>
#include <QApplication>
#include <QDrag>
#include <QDropEvent>
#include <QFileDialog>
#include <QJsonDocument>
#include <QKeyEvent>
#include <QLineEdit>
#include <QListWidgetItem>
#include <QMenu>
#include <QMimeData>
#include <QMouseEvent>
#include <QPlainTextEdit>
#include <QPointer>
#include <QSaveFile>
#include <QShortcut>
#include <QStyle>
#include <QTabBar>
#include <QTextCursor>
#include <QTextDocument>
#include <QToolButton>
#include <QVariantMap>

// generated files
#include "ui_pythondialog.h"

namespace stageviz {

class PythonDialogPrivate : public QObject {
public:
    void init();
    bool eventFilter(QObject* object, QEvent* event) override;
    void executeCode(const QString& code);
    void createDefaultTabIfNeeded();
    int createShelfTab(const QString& name, const QVariantList& scripts = {});
    ShelfWidget* currentShelf() const;
    ShelfWidget* shelfAt(int index) const;
    void beginTabRename(int index);
    void commitTabRename();
    void cancelTabRename();
    void removeTab(int index);
    void exportTab(int index);
    void loadShelves();
    void saveShelves() const;
    void startScriptDrag(QPlainTextEdit* edit);
    void updateClearButton();
    bool isPointInSelection(QPlainTextEdit* edit, const QPoint& pos) const;

    void showEditorContextMenu(const QPoint& pos);
    bool findText(const QString& text, QTextDocument::FindFlags flags = {});
    void findNext();
    void findPrevious();
    void focusFind();
    QString searchText() const;
    void resetSearchStart(QTextDocument::FindFlags flags);

public Q_SLOTS:
    void run();
    void clear();
    void stageChanged(UsdStageRefPtr stage, Session::LoadPolicy policy, Session::StageStatus status);
    void showLogContextMenu(const QPoint& pos);
    void showTabContextMenu(const QPoint& pos);
    void newTab();

public:
    struct Data {
        QPoint dragStartPos;
        QPointer<QLineEdit> tabRenameEditor;
        int tabRenameIndex = -1;
        bool dragCandidate = false;
        QPointer<QPlainTextEdit> dragSourceEdit;
        QString lastSearch;
        QScopedPointer<Ui_PythonDialog> ui;
        QPointer<PythonDialog> dialog;
    };
    Data d;
};

void
PythonDialogPrivate::init()
{
    d.ui.reset(new Ui_PythonDialog());
    d.ui->setupUi(d.dialog.data());
    d.ui->run->setIcon(style()->icon(Style::IconRole::Run));
    d.ui->clear->setIcon(style()->icon(Style::IconRole::Clear));
    d.ui->next->setIcon(style()->icon(Style::IconRole::Right));
    d.ui->previous->setIcon(style()->icon(Style::IconRole::Left));

    d.ui->log->setReadOnly(true);
    d.ui->log->setContextMenuPolicy(Qt::CustomContextMenu);
    d.ui->log->setAcceptDrops(false);
    d.ui->log->installEventFilter(this);

    d.ui->editor->setAcceptDrops(true);
    d.ui->editor->setContextMenuPolicy(Qt::CustomContextMenu);
    d.ui->editor->viewport()->installEventFilter(this);

    d.ui->find->installEventFilter(this);

    d.ui->tabWidget->setTabsClosable(false);
    d.ui->tabWidget->setMovable(true);
    d.ui->tabWidget->tabBar()->setContextMenuPolicy(Qt::CustomContextMenu);
    d.ui->tabWidget->tabBar()->installEventFilter(this);
    d.ui->tabWidget->setUsesScrollButtons(true);
    d.ui->tabWidget->setElideMode(Qt::ElideRight);
    if (QTabBar* bar = d.ui->tabWidget->tabBar()) {
        bar->setExpanding(false);
        bar->setUsesScrollButtons(true);
        bar->setElideMode(Qt::ElideRight);
        bar->setMinimumWidth(0);
        bar->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    }

    // connect
    QObject::connect(d.ui->run, &QToolButton::clicked, this, &PythonDialogPrivate::run);
    QObject::connect(d.ui->clear, &QToolButton::clicked, this, &PythonDialogPrivate::clear);
    QObject::connect(d.ui->next, &QToolButton::clicked, this, &PythonDialogPrivate::findNext);
    QObject::connect(d.ui->previous, &QToolButton::clicked, this, &PythonDialogPrivate::findPrevious);

    QObject::connect(d.ui->editor->document(), &QTextDocument::contentsChanged, this,
                     &PythonDialogPrivate::updateClearButton);

    QObject::connect(d.ui->log, &QWidget::customContextMenuRequested, this, &PythonDialogPrivate::showLogContextMenu);
    QObject::connect(d.ui->editor, &QWidget::customContextMenuRequested, this,
                     &PythonDialogPrivate::showEditorContextMenu);

    QObject::connect(d.ui->tabWidget->tabBar(), &QWidget::customContextMenuRequested, this,
                     &PythonDialogPrivate::showTabContextMenu);
    QObject::connect(d.ui->tabWidget->tabBar(), &QTabBar::tabMoved, this, [this](int, int) { saveShelves(); });

    QObject::connect(d.ui->find, &QLineEdit::textChanged, this, [this](const QString&) { d.lastSearch.clear(); });

    QObject::connect(d.ui->find, &QLineEdit::returnPressed, this, [this]() {
        if (QApplication::keyboardModifiers().testFlag(Qt::ShiftModifier))
            findPrevious();
        else
            findNext();
    });

    QObject::connect(session(), &Session::stageChanged, this, &PythonDialogPrivate::stageChanged);

    auto* find = new QShortcut(QKeySequence::Find, d.dialog.data());
    QObject::connect(find, &QShortcut::activated, this, [this]() { focusFind(); });

    loadShelves();
    createDefaultTabIfNeeded();
    updateClearButton();
}

bool
PythonDialogPrivate::eventFilter(QObject* object, QEvent* event)
{
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
            d.ui->editor->setFocus();
            return true;
        }
    }

    if (object == d.ui->tabWidget->tabBar()) {
        QTabBar* tabBar = d.ui->tabWidget->tabBar();
        if (event->type() == QEvent::MouseButtonDblClick) {
            auto* mouseEvent = static_cast<QMouseEvent*>(event);
            const int index = tabBar->tabAt(mouseEvent->pos());
            if (index >= 0) {
                beginTabRename(index);
                return true;
            }
        }
    }

    if (object == d.ui->editor->viewport()) {
        QPlainTextEdit* edit = d.ui->editor;
        if (!edit)
            return QObject::eventFilter(object, event);

        switch (event->type()) {
        case QEvent::MouseButtonPress: {
            auto* mouseEvent = static_cast<QMouseEvent*>(event);
            if (mouseEvent->button() == Qt::LeftButton) {
                d.dragStartPos = mouseEvent->pos();
                d.dragCandidate = false;
                d.dragSourceEdit = nullptr;
                if (isPointInSelection(edit, mouseEvent->pos())) {
                    d.dragCandidate = true;
                    d.dragSourceEdit = edit;
                }
            }
            break;
        }

        case QEvent::MouseMove: {
            auto* mouseEvent = static_cast<QMouseEvent*>(event);
            if (!(mouseEvent->buttons() & Qt::LeftButton))
                break;
            if (!d.dragCandidate || d.dragSourceEdit != edit)
                break;
            if ((mouseEvent->pos() - d.dragStartPos).manhattanLength() < QApplication::startDragDistance())
                break;
            if (!edit->textCursor().hasSelection())
                break;

            startScriptDrag(edit);
            d.dragCandidate = false;
            d.dragSourceEdit = nullptr;
            return true;
        }

        case QEvent::MouseButtonRelease:
            d.dragCandidate = false;
            d.dragSourceEdit = nullptr;
            break;

        case QEvent::DragEnter: {
            auto* dragEvent = static_cast<QDragEnterEvent*>(event);
            if (dragEvent->mimeData()->hasFormat(mime::script) || dragEvent->mimeData()->hasText()) {
                dragEvent->acceptProposedAction();
                return true;
            }
            break;
        }

        case QEvent::DragMove: {
            auto* dragEvent = static_cast<QDragMoveEvent*>(event);
            if (dragEvent->mimeData()->hasFormat(mime::script) || dragEvent->mimeData()->hasText()) {
                dragEvent->acceptProposedAction();
                return true;
            }
            break;
        }

        case QEvent::Drop: {
            auto* dropEvent = static_cast<QDropEvent*>(event);
            if (dropEvent->mimeData()->hasFormat(mime::script) || dropEvent->mimeData()->hasText()) {
                QString code;
                if (dropEvent->mimeData()->hasFormat(mime::script))
                    code = QString::fromUtf8(dropEvent->mimeData()->data(mime::script));
                else
                    code = dropEvent->mimeData()->text();

                code = qt::normalizeNewlines(code).trimmed();
                if (!code.isEmpty()) {
                    QTextCursor cursor = d.ui->editor->cursorForPosition(dropEvent->position().toPoint());
                    d.ui->editor->setTextCursor(cursor);
                    d.ui->editor->insertPlainText(code);
                    dropEvent->acceptProposedAction();
                    updateClearButton();
                    return true;
                }
            }
            break;
        }

        default: break;
        }
    }

    return QObject::eventFilter(object, event);
}

void
PythonDialogPrivate::executeCode(const QString& code)
{
    auto* interpreter = pythonInterpreter();
    const QString trimmed = code.trimmed();
    if (trimmed.isEmpty())
        return;

    d.ui->log->appendPlainText(">>> " + trimmed);

    const QString result = interpreter->executeScript(trimmed);
    if (!result.isEmpty())
        d.ui->log->appendPlainText(result);

    d.ui->log->appendPlainText("");
}

void
PythonDialogPrivate::createDefaultTabIfNeeded()
{
    if (d.ui->tabWidget->count() == 0)
        createShelfTab(tr("Default"));
}

int
PythonDialogPrivate::createShelfTab(const QString& name, const QVariantList& scripts)
{
    auto* shelf = new ShelfWidget(d.ui->tabWidget);
    shelf->fromVariantList(scripts);

    QObject::connect(shelf, &ShelfWidget::itemActivated, this, [this](const QString& code) { executeCode(code); });
    QObject::connect(shelf, &ShelfWidget::itemContextMenuRequested, this,
                     [this, shelf](const QPoint& pos, QListWidgetItem* item) {
                         QMenu menu(shelf);
                         QAction* loadAction = nullptr;
                         QAction* renameAction = nullptr;
                         QAction* exportAction = nullptr;
                         QAction* removeAction = nullptr;

                         if (item) {
                             loadAction = menu.addAction(tr("Load"));
                             renameAction = menu.addAction(tr("Rename"));
                             exportAction = menu.addAction(tr("Export"));
                             removeAction = menu.addAction(tr("Remove"));
                         }
                         else {
                             QAction* clearAction = menu.addAction(tr("Clear Shelf"));
                             clearAction->setEnabled(shelf->count() > 0);
                             QObject::connect(clearAction, &QAction::triggered, this, [this, shelf]() {
                                 shelf->clear();
                                 saveShelves();
                             });
                         }

                         QAction* chosen = menu.exec(shelf->mapToGlobal(pos));
                         if (!chosen || !item)
                             return;

                         const QString code = item->data(Qt::UserRole).toString();

                         if (chosen == loadAction) {
                             d.ui->editor->setPlainText(code);
                             updateClearButton();
                         }
                         else if (chosen == renameAction) {
                             shelf->editScript(item);
                         }
                         else if (chosen == exportAction) {
                             const QString fileName
                                 = QFileDialog::getSaveFileName(d.dialog.data(), tr("Export Script"),
                                                                item->text() + QStringLiteral(".py"),
                                                                tr("Python Files (*.py);;Text Files (*.txt)"));
                             if (!fileName.isEmpty()) {
                                 QSaveFile file(fileName);
                                 if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
                                     file.write(code.toUtf8());
                                     file.commit();
                                 }
                             }
                         }
                         else if (chosen == removeAction) {
                             shelf->removeScript(item);
                             saveShelves();
                         }
                     });

    QObject::connect(shelf, &ShelfWidget::changed, this, [this]() { saveShelves(); });

    const int index = d.ui->tabWidget->addTab(shelf, name.trimmed().isEmpty() ? tr("Default") : name);
    d.ui->tabWidget->setCurrentIndex(index);
    return index;
}

ShelfWidget*
PythonDialogPrivate::currentShelf() const
{
    return qobject_cast<ShelfWidget*>(d.ui->tabWidget->currentWidget());
}

ShelfWidget*
PythonDialogPrivate::shelfAt(int index) const
{
    return qobject_cast<ShelfWidget*>(d.ui->tabWidget->widget(index));
}

void
PythonDialogPrivate::beginTabRename(int index)
{
    cancelTabRename();

    QTabBar* tabBar = d.ui->tabWidget->tabBar();
    if (index < 0 || index >= tabBar->count())
        return;

    d.tabRenameIndex = index;
    d.tabRenameEditor = new QLineEdit(tabBar);
    d.tabRenameEditor->setText(tabBar->tabText(index));
    d.tabRenameEditor->setFrame(false);
    d.tabRenameEditor->setAlignment(Qt::AlignCenter);
    d.tabRenameEditor->setGeometry(tabBar->tabRect(index).adjusted(2, 2, -2, -2));
    d.tabRenameEditor->selectAll();
    d.tabRenameEditor->show();
    d.tabRenameEditor->setFocus();

    QObject::connect(d.tabRenameEditor, &QLineEdit::editingFinished, this, &PythonDialogPrivate::commitTabRename);
}

void
PythonDialogPrivate::commitTabRename()
{
    if (!d.tabRenameEditor)
        return;

    const QString name = d.tabRenameEditor->text().trimmed();
    if (d.tabRenameIndex >= 0 && d.tabRenameIndex < d.ui->tabWidget->count())
        d.ui->tabWidget->setTabText(d.tabRenameIndex, name.isEmpty() ? tr("Default") : name);

    d.tabRenameEditor->deleteLater();
    d.tabRenameEditor = nullptr;
    d.tabRenameIndex = -1;
    saveShelves();
}

void
PythonDialogPrivate::cancelTabRename()
{
    if (!d.tabRenameEditor)
        return;

    d.tabRenameEditor->deleteLater();
    d.tabRenameEditor = nullptr;
    d.tabRenameIndex = -1;
}

void
PythonDialogPrivate::removeTab(int index)
{
    if (index < 0 || index >= d.ui->tabWidget->count())
        return;

    QWidget* widget = d.ui->tabWidget->widget(index);
    d.ui->tabWidget->removeTab(index);

    if (widget)
        widget->deleteLater();

    createDefaultTabIfNeeded();
    saveShelves();
}

void
PythonDialogPrivate::exportTab(int index)
{
    ShelfWidget* shelf = shelfAt(index);
    if (!shelf)
        return;

    QVariantMap tabData;
    tabData.insert("name", d.ui->tabWidget->tabText(index));
    tabData.insert("scripts", shelf->toVariantList());

    const QString defaultName = d.ui->tabWidget->tabText(index).trimmed().isEmpty()
                                    ? QStringLiteral("shelf.json")
                                    : d.ui->tabWidget->tabText(index).trimmed() + QStringLiteral(".json");

    const QString fileName = QFileDialog::getSaveFileName(d.dialog.data(), tr("Export Shelf"), defaultName,
                                                          tr("JSON Files (*.json)"));
    if (fileName.isEmpty())
        return;

    QSaveFile file(fileName);
    if (!file.open(QIODevice::WriteOnly))
        return;

    const QJsonDocument json = QJsonDocument::fromVariant(tabData);
    file.write(json.toJson(QJsonDocument::Indented));
    file.commit();
}

void
PythonDialogPrivate::loadShelves()
{
    d.ui->tabWidget->clear();

    const QVariant value = settings()->value("python/shelves");
    const QVariantList tabs = value.toList();
    for (const QVariant& tabValue : tabs) {
        const QVariantMap tabMap = tabValue.toMap();
        const QString name = tabMap.value("name").toString();
        const QVariantList scripts = tabMap.value("scripts").toList();
        createShelfTab(name, scripts);
    }
}

void
PythonDialogPrivate::saveShelves() const
{
    QVariantList tabs;
    for (int i = 0; i < d.ui->tabWidget->count(); ++i) {
        const ShelfWidget* shelf = shelfAt(i);
        if (!shelf)
            continue;

        QVariantMap tabMap;
        tabMap.insert("name", d.ui->tabWidget->tabText(i));
        tabMap.insert("scripts", shelf->toVariantList());
        tabs.append(tabMap);
    }

    settings()->setValue("python/shelves", tabs);
}

void
PythonDialogPrivate::startScriptDrag(QPlainTextEdit* edit)
{
    QString code = qt::normalizeNewlines(edit->textCursor().selectedText()).trimmed();
    if (code.isEmpty())
        return;

    auto* mimeData = new QMimeData();
    mimeData->setData(mime::script, code.toUtf8());
    mimeData->setText(code);

    auto* drag = new QDrag(edit);
    drag->setMimeData(mimeData);
    drag->setPixmap(style()->icon(Style::IconRole::Code));
    drag->exec(Qt::CopyAction);
}

void
PythonDialogPrivate::updateClearButton()
{
    const bool enabled = d.ui->editor->isEnabled() && !d.ui->editor->document()->isEmpty();
    d.ui->clear->setEnabled(enabled);
}

bool
PythonDialogPrivate::isPointInSelection(QPlainTextEdit* edit, const QPoint& pos) const
{
    if (!edit)
        return false;

    QTextCursor selection = edit->textCursor();
    if (!selection.hasSelection())
        return false;

    const int selStart = selection.selectionStart();
    const int selEnd = selection.selectionEnd();

    QTextCursor hit = edit->cursorForPosition(pos);
    const int hitPos = hit.position();

    return hitPos >= selStart && hitPos < selEnd;
}

QString
PythonDialogPrivate::searchText() const
{
    return d.ui->find->text().trimmed();
}

void
PythonDialogPrivate::resetSearchStart(QTextDocument::FindFlags flags)
{
    if (!d.ui || !d.ui->editor)
        return;

    QTextCursor cursor = d.ui->editor->textCursor();
    cursor.movePosition(flags.testFlag(QTextDocument::FindBackward) ? QTextCursor::End : QTextCursor::Start);
    d.ui->editor->setTextCursor(cursor);
}

void
PythonDialogPrivate::focusFind()
{
    d.ui->find->setFocus();
    d.ui->find->selectAll();
}

bool
PythonDialogPrivate::findText(const QString& text, QTextDocument::FindFlags flags)
{
    if (text.isEmpty())
        return false;

    if (d.lastSearch != text) {
        d.lastSearch = text;
        resetSearchStart(flags);
    }

    if (d.ui->editor->find(text, flags))
        return true;

    resetSearchStart(flags);
    return d.ui->editor->find(text, flags);
}

void
PythonDialogPrivate::findNext()
{
    const QString text = searchText();
    if (text.isEmpty())
        return;

    findText(text);
}

void
PythonDialogPrivate::findPrevious()
{
    const QString text = searchText();
    if (text.isEmpty())
        return;

    findText(text, QTextDocument::FindBackward);
}

void
PythonDialogPrivate::stageChanged(UsdStageRefPtr stage, Session::LoadPolicy policy, Session::StageStatus status)
{
    Q_UNUSED(stage);
    Q_UNUSED(policy);
    const bool enabled = (status == Session::StageStatus::Loaded);
    d.ui->run->setEnabled(enabled);
    d.ui->editor->setEnabled(enabled);
    d.ui->log->setEnabled(enabled);
    d.ui->tabWidget->setEnabled(enabled);
    d.ui->find->setEnabled(enabled);
    d.ui->next->setEnabled(enabled);
    d.ui->previous->setEnabled(enabled);
    updateClearButton();
}

void
PythonDialogPrivate::showLogContextMenu(const QPoint& pos)
{
    QMenu* menu = d.ui->log->createStandardContextMenu();
    menu->addSeparator();

    QAction* clearAction = menu->addAction(style()->icon(Style::IconRole::Clear), tr("Clear"));
    clearAction->setEnabled(!d.ui->log->toPlainText().isEmpty());
    QObject::connect(clearAction, &QAction::triggered, this, [this]() { d.ui->log->clear(); });

    menu->exec(d.ui->log->viewport()->mapToGlobal(pos));
    delete menu;
}

void
PythonDialogPrivate::showEditorContextMenu(const QPoint& pos)
{
    QMenu* menu = d.ui->editor->createStandardContextMenu();
    menu->addSeparator();

    QAction* findAction = menu->addAction(tr("Find"));
    QObject::connect(findAction, &QAction::triggered, this, [this]() { focusFind(); });

    QAction* findNextAction = menu->addAction(tr("Find Next"));
    QObject::connect(findNextAction, &QAction::triggered, this, [this]() { findNext(); });

    QAction* findPreviousAction = menu->addAction(tr("Find Previous"));
    QObject::connect(findPreviousAction, &QAction::triggered, this, [this]() { findPrevious(); });

    menu->exec(d.ui->editor->viewport()->mapToGlobal(pos));
    delete menu;
}

void
PythonDialogPrivate::showTabContextMenu(const QPoint& pos)
{
    QTabBar* tabBar = d.ui->tabWidget->tabBar();
    const int index = tabBar->tabAt(pos);

    QMenu menu(tabBar);
    QAction* newAction = menu.addAction(tr("New Shelf"));
    QAction* renameAction = menu.addAction(tr("Rename"));
    QAction* exportAction = menu.addAction(tr("Export"));
    QAction* removeAction = menu.addAction(tr("Remove"));

    renameAction->setEnabled(index >= 0);
    exportAction->setEnabled(index >= 0);
    removeAction->setEnabled(index >= 0 && d.ui->tabWidget->count() > 1);

    QAction* chosen = menu.exec(tabBar->mapToGlobal(pos));
    if (!chosen)
        return;

    if (chosen == newAction) {
        newTab();
    }
    else if (chosen == renameAction) {
        beginTabRename(index);
    }
    else if (chosen == exportAction) {
        exportTab(index);
    }
    else if (chosen == removeAction) {
        removeTab(index);
    }
}

void
PythonDialogPrivate::newTab()
{
    const int index = createShelfTab(tr("Shelf"));
    saveShelves();
    beginTabRename(index);
}

void
PythonDialogPrivate::run()
{
    executeCode(d.ui->editor->toPlainText());
}

void
PythonDialogPrivate::clear()
{
    d.ui->editor->clear();
    updateClearButton();
}

PythonDialog::PythonDialog(QWidget* parent)
    : QDialog(parent)
    , p(new PythonDialogPrivate())
{
    p->d.dialog = this;
    p->init();
}

PythonDialog::~PythonDialog()
{
    p->saveShelves();
}

}  // namespace stageviz
