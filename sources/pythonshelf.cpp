// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#include "pythonshelf.h"
#include "application.h"
#include "messagedialog.h"
#include "pythoninterpreter.h"
#include "roles.h"
#include "settings.h"
#include "shelfwidget.h"
#include <QAction>
#include <QDebug>
#include <QFileDialog>
#include <QJsonDocument>
#include <QLineEdit>
#include <QListWidgetItem>
#include <QMenu>
#include <QPointer>
#include <QSaveFile>
#include <QTabBar>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QVariantMap>

namespace stageviz {

class PythonShelfPrivate : public QObject {
    Q_OBJECT
public:
    void init();
    void executeCode(const QString& code);
    void createDefaultTabIfNeeded();
    int createShelfTab(const QString& name, const QVariantList& scripts = {});
    ShelfWidget* shelfAt(int index) const;
    void beginTabRename(int index);
    void commitTabRename();
    void cancelTabRename();
    void removeTab(int index);
    void exportTab(int index);
    void loadShelves();
    void saveShelves() const;

public Q_SLOTS:
    void showTabContextMenu(const QPoint& pos);
    void newTab();

public:
    struct Data {
        QPointer<QLineEdit> tabRenameEditor;
        int tabRenameIndex = -1;
        QPointer<QTabWidget> tabs;
        QPointer<PythonShelf> shelf;
    };
    Data d;
};

void
PythonShelfPrivate::init()
{
    auto* layout = new QVBoxLayout(d.shelf.data());
    layout->setSpacing(0);
    layout->setContentsMargins(0, 0, 0, 0);

    d.shelf->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    d.tabs = new QTabWidget(d.shelf.data());
    d.tabs->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    d.tabs->setTabsClosable(false);
    d.tabs->setMovable(true);
    d.tabs->setUsesScrollButtons(true);
    d.tabs->setElideMode(Qt::ElideNone);
    layout->addWidget(d.tabs);

    if (QTabBar* bar = d.tabs->tabBar()) {
        bar->setContextMenuPolicy(Qt::CustomContextMenu);
        bar->setExpanding(false);
        bar->setUsesScrollButtons(true);
        bar->setElideMode(Qt::ElideNone);
        bar->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

        QObject::connect(bar, &QWidget::customContextMenuRequested, this, &PythonShelfPrivate::showTabContextMenu);
        QObject::connect(bar, &QTabBar::tabMoved, this, [this](int, int) { saveShelves(); });
        QObject::connect(bar, &QTabBar::tabBarDoubleClicked, this, [this](int index) {
            if (index >= 0)
                beginTabRename(index);
        });
    }

    loadShelves();
    createDefaultTabIfNeeded();
}

void
PythonShelfPrivate::executeCode(const QString& code)
{
    const QString trimmed = code.trimmed();
    if (trimmed.isEmpty())
        return;

    const QString result = pythonInterpreter()->executeScript(trimmed);
    if (!result.isEmpty())
        qInfo().noquote() << result;
}

void
PythonShelfPrivate::createDefaultTabIfNeeded()
{
    if (d.tabs && d.tabs->count() == 0)
        createShelfTab(tr("Shelf"));
}

int
PythonShelfPrivate::createShelfTab(const QString& name, const QVariantList& scripts)
{
    if (!d.tabs)
        return -1;

    auto* shelf = new ShelfWidget(d.tabs.data());
    shelf->fromVariantList(scripts);

    QObject::connect(shelf, &ShelfWidget::itemActivated, this, [this](const QString& code) { executeCode(code); });
    QObject::connect(shelf, &ShelfWidget::itemContextMenuRequested, this,
                     [this, shelf](const QPoint& pos, QListWidgetItem* item) {
                         QMenu menu(shelf);
                         QAction* renameAction = nullptr;
                         QAction* exportAction = nullptr;
                         QAction* removeAction = nullptr;

                         if (item) {
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

                         if (chosen == renameAction) {
                             shelf->editScript(item);
                         }
                         else if (chosen == exportAction) {
                             const QString fileName
                                 = QFileDialog::getSaveFileName(d.shelf.data(), tr("Export Script"),
                                                                item->data(roles::shelf::scriptName).toString()
                                                                    + QStringLiteral(".py"),
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

    const int index = d.tabs->addTab(shelf, name.trimmed().isEmpty() ? tr("Shelf") : name);
    d.tabs->setCurrentIndex(index);
    return index;
}

ShelfWidget*
PythonShelfPrivate::shelfAt(int index) const
{
    return d.tabs ? qobject_cast<ShelfWidget*>(d.tabs->widget(index)) : nullptr;
}

void
PythonShelfPrivate::beginTabRename(int index)
{
    cancelTabRename();

    if (!d.tabs)
        return;

    QTabBar* tabBar = d.tabs->tabBar();
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

    QObject::connect(d.tabRenameEditor, &QLineEdit::editingFinished, this, &PythonShelfPrivate::commitTabRename);
}

void
PythonShelfPrivate::commitTabRename()
{
    if (!d.tabRenameEditor || !d.tabs)
        return;

    const QString name = d.tabRenameEditor->text().trimmed();
    if (d.tabRenameIndex >= 0 && d.tabRenameIndex < d.tabs->count())
        d.tabs->setTabText(d.tabRenameIndex, name.isEmpty() ? tr("Shelf") : name);

    d.tabRenameEditor->deleteLater();
    d.tabRenameEditor = nullptr;
    d.tabRenameIndex = -1;
    saveShelves();
}

void
PythonShelfPrivate::cancelTabRename()
{
    if (!d.tabRenameEditor)
        return;

    d.tabRenameEditor->deleteLater();
    d.tabRenameEditor = nullptr;
    d.tabRenameIndex = -1;
}

void
PythonShelfPrivate::removeTab(int index)
{
    if (!d.tabs || index < 0 || index >= d.tabs->count())
        return;

    const QString shelfName = d.tabs->tabText(index).trimmed();
    const QString displayName = shelfName.isEmpty() ? tr("Shelf") : shelfName;

    if (!MessageDialog::question(d.shelf.data(), tr("Remove Shelf"),
                                 tr("Remove the shelf \"%1\" and all scripts it contains?").arg(displayName)))
        return;

    QWidget* widget = d.tabs->widget(index);
    d.tabs->removeTab(index);

    if (widget)
        widget->deleteLater();

    createDefaultTabIfNeeded();
    saveShelves();
}

void
PythonShelfPrivate::exportTab(int index)
{
    if (!d.tabs)
        return;

    ShelfWidget* shelf = shelfAt(index);
    if (!shelf)
        return;

    QVariantMap tabData;
    tabData.insert("name", d.tabs->tabText(index));
    tabData.insert("scripts", shelf->toVariantList());

    const QString tabName = d.tabs->tabText(index).trimmed();
    const QString defaultName = tabName.isEmpty() ? QStringLiteral("shelf.json") : tabName + QStringLiteral(".json");

    const QString fileName = QFileDialog::getSaveFileName(d.shelf.data(), tr("Export Shelf"), defaultName,
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
PythonShelfPrivate::loadShelves()
{
    if (!d.tabs)
        return;

    d.tabs->clear();

    const QVariantList tabs = settings()->value("python/shelves").toList();
    for (const QVariant& tabValue : tabs) {
        const QVariantMap tabMap = tabValue.toMap();
        createShelfTab(tabMap.value("name").toString(), tabMap.value("scripts").toList());
    }
}

void
PythonShelfPrivate::saveShelves() const
{
    if (!d.tabs)
        return;

    QVariantList tabs;
    for (int i = 0; i < d.tabs->count(); ++i) {
        const ShelfWidget* shelf = shelfAt(i);
        if (!shelf)
            continue;

        QVariantMap tabMap;
        tabMap.insert("name", d.tabs->tabText(i));
        tabMap.insert("scripts", shelf->toVariantList());
        tabs.append(tabMap);
    }

    settings()->setValue("python/shelves", tabs);
}

void
PythonShelfPrivate::showTabContextMenu(const QPoint& pos)
{
    if (!d.tabs)
        return;

    QTabBar* tabBar = d.tabs->tabBar();
    const int index = tabBar->tabAt(pos);

    QMenu menu(tabBar);
    QAction* newAction = menu.addAction(tr("New Shelf"));
    QAction* renameAction = menu.addAction(tr("Rename"));
    QAction* exportAction = menu.addAction(tr("Export"));
    QAction* removeAction = menu.addAction(tr("Remove"));

    renameAction->setEnabled(index >= 0);
    exportAction->setEnabled(index >= 0);
    removeAction->setEnabled(index >= 0 && d.tabs->count() > 1);

    QAction* chosen = menu.exec(tabBar->mapToGlobal(pos));
    if (!chosen)
        return;

    if (chosen == newAction)
        newTab();
    else if (chosen == renameAction)
        beginTabRename(index);
    else if (chosen == exportAction)
        exportTab(index);
    else if (chosen == removeAction)
        removeTab(index);
}

void
PythonShelfPrivate::newTab()
{
    const int index = createShelfTab(tr("Shelf"));
    saveShelves();
    beginTabRename(index);
}

PythonShelf::PythonShelf(QWidget* parent)
    : QWidget(parent)
    , p(new PythonShelfPrivate())
{
    p->d.shelf = this;
    p->init();
}

PythonShelf::~PythonShelf() { p->saveShelves(); }

QSize
PythonShelf::sizeHint() const
{
    int tabHeight = 0;
    int shelfHeight = 0;
    int frameHeight = 0;

    if (p->d.tabs) {
        if (QTabBar* bar = p->d.tabs->tabBar())
            tabHeight = bar->sizeHint().height();

        if (ShelfWidget* shelf = p->shelfAt(p->d.tabs->currentIndex()))
            shelfHeight = shelf->sizeHint().height();

        frameHeight = p->d.tabs->style()->pixelMetric(QStyle::PM_DefaultFrameWidth) * 2;
    }

    return QSize(QWidget::sizeHint().width(), tabHeight + shelfHeight + frameHeight);
}

QSize
PythonShelf::minimumSizeHint() const
{
    return sizeHint();
}

}  // namespace stageviz

#include "pythonshelf.moc"
