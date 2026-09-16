// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#include "materialbrowser.h"
#include "application.h"
#include "command.h"
#include "commandstack.h"
#include "materialitem.h"
#include "mime.h"
#include "selectionlist.h"
#include "settings.h"
#include "style.h"
#include "tracelocks.h"
#include <QAbstractItemView>
#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QCursor>
#include <QDrag>
#include <QHeaderView>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMimeData>
#include <QMouseEvent>
#include <QPointer>
#include <QSet>
#include <QSlider>
#include <QStackedWidget>
#include <QStyledItemDelegate>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>
#include <algorithm>
#include <pxr/usd/usd/primRange.h>
#include <pxr/usd/usdGeom/gprim.h>

// generated files
#include "ui_materialbrowser.h"

namespace stageviz {

class MaterialBrowserPrivate : public QObject {
public:
    void init();
    void rebuild();
    void insertRow(int row);
    void updateRow(int row);
    void updateSourceRows();
    void applyFilter(const QString& text);
    void syncSelection(QAbstractItemView* source);
    void updateViewSizes();
    void showViewMenu();
    void showContextMenu(QAbstractItemView* view, const QPoint& position);
    void assignSelectedMaterial();
    void beginRename(QAbstractItemView* view, int row = -1);
    void commitRename(int row, const QString& name);
    QAbstractItemView* currentView() const;
    QImage placeholderImage() const;

public:
    class MaterialBrowserItemDelegate : public QStyledItemDelegate {
    public:
        explicit MaterialBrowserItemDelegate(QObject* parent = nullptr)
            : QStyledItemDelegate(parent)
        {}

    protected:
        bool eventFilter(QObject* editor, QEvent* event) override
        {
            if (editor && event && event->type() == QEvent::KeyPress) {
                auto* keyEvent = static_cast<QKeyEvent*>(event);
                if (keyEvent->key() == Qt::Key_Tab && keyEvent->modifiers() == Qt::NoModifier) {
                    QWidget* widget = qobject_cast<QWidget*>(editor);
                    Q_EMIT commitData(widget);
                    Q_EMIT closeEditor(widget, QAbstractItemDelegate::NoHint);
                    keyEvent->accept();
                    return true;
                }
            }

            return QStyledItemDelegate::eventFilter(editor, event);
        }
    };
    struct Data {
        QPointer<MaterialBrowser> browser;
        QScopedPointer<Ui_MaterialBrowser> ui;
        QList<MaterialEntry> entries;
        QVector<QImage> swatches;
        QVector<bool> swatchValid;
        QSet<int> requestedRows;
        QPointer<QAction> iconView;
        QPointer<QAction> listView;
        QPointer<QAction> detailView;
        QTimer* visibleTimer = nullptr;
        QPoint dragStartPosition;
        int swatchSize = 128;
        int swatchMinimum = 64;
        int swatchMaximum = 256;
        int swatchPadding = 8;
        int dragSourceRow = -1;
        bool materialDragActive = false;
        bool syncingSelection = false;
        bool updatingRows = false;
        MaterialBrowser::ViewMode mode = MaterialBrowser::Icons;
    };
    Data d;
};

void
MaterialBrowserPrivate::init()
{
    d.ui.reset(new Ui_MaterialBrowser());
    d.ui->setupUi(d.browser.data());

    for (QAbstractItemView* browser :
         { static_cast<QAbstractItemView*>(d.ui->icons), static_cast<QAbstractItemView*>(d.ui->list),
           static_cast<QAbstractItemView*>(d.ui->details) }) {
        browser->setSelectionMode(QAbstractItemView::ExtendedSelection);
        browser->setSelectionBehavior(QAbstractItemView::SelectRows);
        // MaterialBrowser starts its own drag from eventFilter(). Keep the
        // QAbstractItemView drag state disabled so it cannot fall back to
        // rubber-band selection when the cursor returns after a material drag.
        browser->setDragEnabled(false);
        browser->setDragDropMode(QAbstractItemView::NoDragDrop);
        browser->setDefaultDropAction(Qt::CopyAction);
        browser->setEditTriggers(QAbstractItemView::NoEditTriggers);
        browser->setItemDelegate(new MaterialBrowserItemDelegate(browser));
        browser->installEventFilter(d.browser.data());
        browser->viewport()->installEventFilter(d.browser.data());
    }

    d.ui->icons->setViewMode(QListView::IconMode);
    d.ui->icons->setMovement(QListView::Static);
    d.ui->icons->setResizeMode(QListView::Adjust);
    d.ui->icons->setWrapping(true);
    d.ui->icons->setSpacing(8);
    d.ui->icons->setUniformItemSizes(true);
    d.ui->icons->setWordWrap(false);
    d.ui->icons->setTextElideMode(Qt::ElideRight);

    d.ui->list->setViewMode(QListView::ListMode);
    d.ui->list->setUniformItemSizes(true);
    d.ui->details->setRootIsDecorated(false);
    d.ui->details->setItemsExpandable(false);
    d.ui->details->setIndentation(0);
    d.ui->details->setSortingEnabled(false);

    QHeaderView* header = d.ui->details->header();
    header->setStretchLastSection(false);
    header->setSectionsMovable(true);
    header->setSectionsClickable(true);
    header->setMinimumSectionSize(40);
    header->setSectionResizeMode(MaterialItem::Name, QHeaderView::Interactive);
    header->setSectionResizeMode(MaterialItem::Type, QHeaderView::Interactive);
    header->setSectionResizeMode(MaterialItem::Path, QHeaderView::Stretch);
    header->resizeSection(MaterialItem::Name, 180);
    header->resizeSection(MaterialItem::Type, 220);

    d.ui->clear->setIcon(style()->icon(Style::IconRole::Clear));
    d.ui->view->setIcon(style()->icon(Style::IconRole::List));
    d.ui->view->setText(QString());
    d.ui->view->setContextMenuPolicy(Qt::CustomContextMenu);

    d.iconView = new QAction("Icons", d.browser.data());
    d.listView = new QAction("List", d.browser.data());
    d.detailView = new QAction("Details", d.browser.data());

    for (QAction* action : { d.iconView.data(), d.listView.data(), d.detailView.data() })
        action->setCheckable(true);

    QActionGroup* actions = new QActionGroup(d.browser.data());
    actions->setExclusive(true);
    actions->addAction(d.iconView);
    actions->addAction(d.listView);
    actions->addAction(d.detailView);
    d.iconView->setChecked(true);

    d.visibleTimer = new QTimer(d.browser.data());
    d.visibleTimer->setSingleShot(true);
    d.visibleTimer->setInterval(80);

    d.ui->swatchSize->setRange(d.swatchMinimum, d.swatchMaximum);
    d.ui->swatchSize->setSingleStep(8);
    d.ui->swatchSize->setValue(d.swatchSize);

    // connect
    connect(d.visibleTimer, &QTimer::timeout, d.browser.data(), [this]() { d.browser->refreshVisibleSwatches(); });
    connect(d.ui->filter, &QLineEdit::textChanged, d.browser.data(), [this](const QString& text) {
        applyFilter(text);
        d.ui->clear->setEnabled(!text.isEmpty());
        d.visibleTimer->start();
    });
    connect(d.ui->clear, &QToolButton::clicked, d.ui->filter, &QLineEdit::clear);
    connect(d.ui->swatchSize, &QSlider::valueChanged, d.browser.data(), [this](int) {
        updateViewSizes();
        d.visibleTimer->start();
    });
    connect(d.ui->view, &QToolButton::clicked, d.browser.data(), [this]() { showViewMenu(); });
    connect(d.ui->view, &QWidget::customContextMenuRequested, d.browser.data(),
            [this](const QPoint&) { showViewMenu(); });
    connect(d.iconView, &QAction::triggered, d.browser.data(),
            [this]() { d.browser->setViewMode(MaterialBrowser::Icons); });
    connect(d.listView, &QAction::triggered, d.browser.data(),
            [this]() { d.browser->setViewMode(MaterialBrowser::List); });
    connect(d.detailView, &QAction::triggered, d.browser.data(),
            [this]() { d.browser->setViewMode(MaterialBrowser::Details); });
    connect(d.ui->icons, &QAbstractItemView::doubleClicked, d.browser.data(),
            [this](const QModelIndex& index) { beginRename(d.ui->icons, index.row()); });
    connect(d.ui->list, &QAbstractItemView::doubleClicked, d.browser.data(),
            [this](const QModelIndex& index) { beginRename(d.ui->list, index.row()); });
    connect(d.ui->details, &QAbstractItemView::doubleClicked, d.browser.data(),
            [this](const QModelIndex& index) { beginRename(d.ui->details, index.row()); });
    connect(d.ui->icons, &QListWidget::itemChanged, d.browser.data(), [this](QListWidgetItem* item) {
        if (item)
            commitRename(item->data(Qt::UserRole).toInt(), item->text());
    });
    connect(d.ui->list, &QListWidget::itemChanged, d.browser.data(), [this](QListWidgetItem* item) {
        if (item)
            commitRename(item->data(Qt::UserRole).toInt(), item->text());
    });
    connect(d.ui->details, &QTreeWidget::itemChanged, d.browser.data(), [this](QTreeWidgetItem* baseItem, int column) {
        if (column != MaterialItem::Name)
            return;
        if (auto* item = dynamic_cast<MaterialItem*>(baseItem))
            commitRename(item->sourceRow(), item->text(MaterialItem::Name));
    });
    connect(d.ui->icons, &QListWidget::itemSelectionChanged, d.browser.data(),
            [this]() { syncSelection(d.ui->icons); });
    connect(d.ui->list, &QListWidget::itemSelectionChanged, d.browser.data(), [this]() { syncSelection(d.ui->list); });
    connect(d.ui->details, &QTreeWidget::itemSelectionChanged, d.browser.data(),
            [this]() { syncSelection(d.ui->details); });

    d.browser->setViewMode(MaterialBrowser::Icons);
    updateViewSizes();
}

void
MaterialBrowserPrivate::showViewMenu()
{
    QMenu menu(d.ui->view);
    menu.addAction(d.iconView);
    menu.addAction(d.listView);
    menu.addAction(d.detailView);
    menu.exec(d.ui->view->mapToGlobal(QPoint(0, d.ui->view->height())));
}

void
MaterialBrowserPrivate::showContextMenu(QAbstractItemView* view, const QPoint& position)
{
    if (!view)
        return;

    const QModelIndex index = view->indexAt(position);
    if (index.isValid() && !d.browser->selectedRows().contains(index.row()))
        d.browser->selectRow(index.row());

    const QList<MaterialEntry> materials = d.browser->selectedEntries();

    QMenu menu(view);

    QAction* assignMaterial = nullptr;
    QAction* copyName = nullptr;
    QAction* copyPath = nullptr;
    QAction* deleteMaterial = nullptr;

    if (index.isValid()) {
        assignMaterial = menu.addAction(tr("Assign"));
        assignMaterial->setEnabled(materials.size() == 1);

        menu.addSeparator();

        QMenu* copy = menu.addMenu(tr("Copy"));
        copyName = copy->addAction(tr("Name"));
        copyPath = copy->addAction(tr("Path"));

        menu.addSeparator();
    }

    QAction* newMaterial = menu.addAction(tr("New Material"));

    if (index.isValid())
        deleteMaterial = menu.addAction(tr("Delete"));

    QAction* action = menu.exec(view->viewport()->mapToGlobal(position));
    if (!action)
        return;

    if (action == assignMaterial) {
        assignSelectedMaterial();
    }
    else if (action == copyName) {
        QStringList values;
        for (const MaterialEntry& material : materials)
            values.append(material.name);
        QApplication::clipboard()->setText(values.join('\n'));
    }
    else if (action == copyPath) {
        QStringList values;
        for (const MaterialEntry& material : materials)
            values.append(QString::fromStdString(material.materialPath.GetString()));
        QApplication::clipboard()->setText(values.join('\n'));
    }
    else if (action == newMaterial) {
        Q_EMIT d.browser->newMaterialRequested();
    }
    else if (action == deleteMaterial) {
        Q_EMIT d.browser->deleteRequested();
    }
}


void
MaterialBrowserPrivate::assignSelectedMaterial()
{
    const QList<MaterialEntry> materials = d.browser->selectedEntries();
    if (materials.size() != 1)
        return;

    const QList<SdfPath> selectedPaths = session()->selectionList()->paths();
    if (selectedPaths.isEmpty())
        return;

    QList<SdfPath> paths;
    QSet<QString> seen;

    {
        READ_LOCKER(locker, session()->stageLock(), "stageLock");
        const UsdStageRefPtr stage = session()->stageUnsafe();
        if (!stage)
            return;

        auto appendDrawable = [&](const UsdPrim& prim) {
            if (!prim || !prim.IsValid() || prim.IsInstanceProxy())
                return;

            if (!prim.IsA<UsdGeomGprim>())
                return;

            const SdfPath path = prim.GetPath();
            const QString key = QString::fromStdString(path.GetString());

            if (seen.contains(key))
                return;

            seen.insert(key);
            paths.append(path);
        };

        for (const SdfPath& selectedPath : selectedPaths) {
            const SdfPath primPath = selectedPath.IsPropertyPath() ? selectedPath.GetPrimPath() : selectedPath;

            if (primPath.IsEmpty())
                continue;

            if (primPath == SdfPath::AbsoluteRootPath()) {
                for (const UsdPrim& prim : stage->Traverse())
                    appendDrawable(prim);
                continue;
            }

            const UsdPrim root = stage->GetPrimAtPath(primPath);
            if (!root || !root.IsValid())
                continue;

            for (const UsdPrim& prim : UsdPrimRange(root))
                appendDrawable(prim);
        }
    }

    if (paths.isEmpty())
        return;

    session()->commandStack()->run(new Command(bindMaterial(paths, materials.first().materialPath)));
}


void
MaterialBrowserPrivate::beginRename(QAbstractItemView* view, int row)
{
    if (!view)
        return;

    if (row < 0) {
        const QModelIndex current = view->currentIndex();
        if (current.isValid())
            row = current.row();
    }

    if (row < 0 || row >= d.entries.size())
        return;

    if (view == d.ui->icons) {
        if (QListWidgetItem* item = d.ui->icons->item(row)) {
            d.ui->icons->setCurrentItem(item);
            d.ui->icons->editItem(item);
        }
    }
    else if (view == d.ui->list) {
        if (QListWidgetItem* item = d.ui->list->item(row)) {
            d.ui->list->setCurrentItem(item);
            d.ui->list->editItem(item);
        }
    }
    else if (view == d.ui->details) {
        if (QTreeWidgetItem* item = d.ui->details->topLevelItem(row)) {
            d.ui->details->setCurrentItem(item, MaterialItem::Name);
            d.ui->details->editItem(item, MaterialItem::Name);
        }
    }
}

void
MaterialBrowserPrivate::commitRename(int row, const QString& name)
{
    if (d.updatingRows || row < 0 || row >= d.entries.size())
        return;

    const MaterialEntry& entry = d.entries[row];
    const QString trimmed = name.trimmed();

    // QAbstractItemDelegate may emit itemChanged more than once while closing
    // an editor. Never forward an empty transient value to the rename command.
    if (trimmed.isEmpty() || trimmed == entry.name) {
        d.updatingRows = true;
        if (QListWidgetItem* item = d.ui->icons->item(row))
            item->setText(entry.name);
        if (QListWidgetItem* item = d.ui->list->item(row))
            item->setText(entry.name);
        if (QTreeWidgetItem* item = d.ui->details->topLevelItem(row))
            item->setText(MaterialItem::Name, entry.name);
        d.updatingRows = false;
        return;
    }

    Q_EMIT d.browser->renameRequested(entry.materialPath, trimmed);

    // Keep all views on the authored value until the stage notice refreshes
    // the material list. This also lets the command perform sanitizing and
    // uniquing exactly like StageTree.
    d.updatingRows = true;
    if (QListWidgetItem* item = d.ui->icons->item(row))
        item->setText(entry.name);
    if (QListWidgetItem* item = d.ui->list->item(row))
        item->setText(entry.name);
    if (QTreeWidgetItem* item = d.ui->details->topLevelItem(row))
        item->setText(MaterialItem::Name, entry.name);
    d.updatingRows = false;
}

void
MaterialBrowserPrivate::insertRow(int row)
{
    if (row < 0 || row >= d.entries.size())
        return;

    const MaterialEntry& entry = d.entries[row];
    const QString path = QString::fromStdString(entry.materialPath.GetString());
    const QString type = MaterialUtils::shaderTypeLabel(entry.shaderId);

    auto* iconItem = new QListWidgetItem();
    iconItem->setFlags(iconItem->flags() | Qt::ItemIsDragEnabled | Qt::ItemIsEditable);
    d.ui->icons->insertItem(row, iconItem);

    auto* listItem = new QListWidgetItem();
    listItem->setFlags(listItem->flags() | Qt::ItemIsDragEnabled | Qt::ItemIsEditable);
    d.ui->list->insertItem(row, listItem);

    auto* detailItem = new MaterialItem(d.ui->details);
    detailItem->setFlags(detailItem->flags() | Qt::ItemIsEditable);
    const int appendedRow = d.ui->details->indexOfTopLevelItem(detailItem);
    if (appendedRow != row) {
        d.ui->details->takeTopLevelItem(appendedRow);
        d.ui->details->insertTopLevelItem(row, detailItem);
    }

    updateRow(row);
}

void
MaterialBrowserPrivate::updateSourceRows()
{
    for (int row = 0; row < d.entries.size(); ++row) {
        if (QListWidgetItem* item = d.ui->icons->item(row))
            item->setData(Qt::UserRole, row);
        if (QListWidgetItem* item = d.ui->list->item(row))
            item->setData(Qt::UserRole, row);
        if (auto* item = dynamic_cast<MaterialItem*>(d.ui->details->topLevelItem(row)))
            item->setSourceRow(row);
    }
}

void
MaterialBrowserPrivate::rebuild()
{
    d.syncingSelection = true;

    d.ui->icons->setUpdatesEnabled(false);
    d.ui->list->setUpdatesEnabled(false);
    d.ui->details->setUpdatesEnabled(false);

    d.ui->icons->clear();
    d.ui->list->clear();
    d.ui->details->clear();

    for (int row = 0; row < d.entries.size(); ++row) {
        auto* iconItem = new QListWidgetItem(d.ui->icons);
        iconItem->setFlags(iconItem->flags() | Qt::ItemIsDragEnabled | Qt::ItemIsEditable);

        auto* listItem = new QListWidgetItem(d.ui->list);
        listItem->setFlags(listItem->flags() | Qt::ItemIsDragEnabled | Qt::ItemIsEditable);

        auto* detailItem = new MaterialItem(d.ui->details);
        detailItem->setFlags(detailItem->flags() | Qt::ItemIsEditable);
        updateRow(row);
    }

    updateSourceRows();
    d.ui->count->setText(QString("%1 material(s)").arg(d.entries.size()));

    d.ui->icons->setUpdatesEnabled(true);
    d.ui->list->setUpdatesEnabled(true);
    d.ui->details->setUpdatesEnabled(true);

    d.syncingSelection = false;
    applyFilter(d.ui->filter->text());
    updateViewSizes();
}

void
MaterialBrowserPrivate::updateRow(int row)
{
    if (row < 0 || row >= d.entries.size())
        return;

    const bool wasUpdating = d.updatingRows;
    d.updatingRows = true;

    const MaterialEntry& entry = d.entries[row];
    const QString path = QString::fromStdString(entry.materialPath.GetString());
    const QString type = MaterialUtils::shaderTypeLabel(entry.shaderId);
    const QImage image = d.swatches.value(row);

    QIcon icon;
    if (!image.isNull()) {
        icon = QIcon(QPixmap::fromImage(image).scaled(d.swatchSize, d.swatchSize, Qt::KeepAspectRatio,
                                                      Qt::SmoothTransformation));
    }

    if (QListWidgetItem* item = d.ui->icons->item(row)) {
        item->setText(entry.name);
        item->setToolTip(QString("%1\n%2").arg(path, type));
        item->setIcon(icon);
    }

    if (QListWidgetItem* item = d.ui->list->item(row)) {
        item->setText(entry.name);
        item->setToolTip(QString("%1\n%2").arg(path, type));
        item->setIcon(icon);
    }

    if (auto* item = dynamic_cast<MaterialItem*>(d.ui->details->topLevelItem(row))) {
        item->setSourceRow(row);
        item->setMaterialPath(entry.materialPath);
        item->setShaderPath(entry.shaderPath);
        item->setShaderId(entry.shaderId);
        item->setSwatch(image);
        item->setText(MaterialItem::Name, entry.name);
        item->setText(MaterialItem::Type, type);
        item->setText(MaterialItem::Path, path);
        item->setToolTip(MaterialItem::Name, QString("%1\n%2").arg(path, type));
        item->setIcon(MaterialItem::Name, icon);
    }

    d.updatingRows = wasUpdating;
}

void
MaterialBrowserPrivate::applyFilter(const QString& text)
{
    const QString filter = text.trimmed();

    for (int row = 0; row < d.entries.size(); ++row) {
        const MaterialEntry& entry = d.entries[row];
        const QString path = QString::fromStdString(entry.materialPath.GetString());
        const QString type = MaterialUtils::shaderTypeLabel(entry.shaderId);
        const bool visible = filter.isEmpty() || entry.name.contains(filter, Qt::CaseInsensitive)
                             || type.contains(filter, Qt::CaseInsensitive)
                             || path.contains(filter, Qt::CaseInsensitive);

        if (QListWidgetItem* item = d.ui->icons->item(row))
            item->setHidden(!visible);
        if (QListWidgetItem* item = d.ui->list->item(row))
            item->setHidden(!visible);
        if (QTreeWidgetItem* item = d.ui->details->topLevelItem(row))
            item->setHidden(!visible);
    }
}

void
MaterialBrowserPrivate::syncSelection(QAbstractItemView* source)
{
    if (d.syncingSelection)
        return;

    QSet<int> selected;

    if (source == d.ui->icons) {
        for (QListWidgetItem* item : d.ui->icons->selectedItems())
            selected.insert(item->data(Qt::UserRole).toInt());
    }
    else if (source == d.ui->list) {
        for (QListWidgetItem* item : d.ui->list->selectedItems())
            selected.insert(item->data(Qt::UserRole).toInt());
    }
    else {
        for (QTreeWidgetItem* baseItem : d.ui->details->selectedItems()) {
            if (auto* item = dynamic_cast<MaterialItem*>(baseItem))
                selected.insert(item->sourceRow());
        }
    }

    d.syncingSelection = true;
    for (int row = 0; row < d.entries.size(); ++row) {
        const bool selectedRow = selected.contains(row);
        if (QListWidgetItem* item = d.ui->icons->item(row))
            item->setSelected(selectedRow);
        if (QListWidgetItem* item = d.ui->list->item(row))
            item->setSelected(selectedRow);
        if (QTreeWidgetItem* item = d.ui->details->topLevelItem(row))
            item->setSelected(selectedRow);
    }
    d.syncingSelection = false;

    Q_EMIT d.browser->selectionChanged();
}

void
MaterialBrowserPrivate::updateViewSizes()
{
    const int size = std::clamp(d.ui->swatchSize->value(), d.swatchMinimum, d.swatchMaximum);
    d.swatchSize = size;

    const int nameHeight = d.ui->icons->fontMetrics().height() + 4;
    const QSize gridSize(size + d.swatchPadding * 2, size + nameHeight + d.swatchPadding * 2);

    d.ui->icons->setIconSize(QSize(size, size));
    d.ui->icons->setGridSize(gridSize);

    for (int row = 0; row < d.ui->icons->count(); ++row) {
        QListWidgetItem* item = d.ui->icons->item(row);
        if (!item)
            continue;

        item->setSizeHint(gridSize);

        const QImage image = d.swatches.value(row);
        if (!image.isNull()) {
            item->setIcon(
                QIcon(QPixmap::fromImage(image).scaled(size, size, Qt::KeepAspectRatio, Qt::SmoothTransformation)));
        }
    }

    d.ui->list->setIconSize(QSize(std::min(size, 56), std::min(size, 56)));

    const int detailIconSize = std::min(size, 40);
    const int detailRowHeight = detailIconSize + 12;

    d.ui->details->setIconSize(QSize(detailIconSize, detailIconSize));

    for (int row = 0; row < d.ui->details->topLevelItemCount(); ++row) {
        if (QTreeWidgetItem* item = d.ui->details->topLevelItem(row))
            item->setSizeHint(MaterialItem::Name, QSize(0, detailRowHeight));
    }

    d.ui->icons->doItemsLayout();
}

QAbstractItemView*
MaterialBrowserPrivate::currentView() const
{
    if (d.mode == MaterialBrowser::Icons)
        return d.ui->icons;
    if (d.mode == MaterialBrowser::List)
        return d.ui->list;
    return d.ui->details;
}

QImage
MaterialBrowserPrivate::placeholderImage() const
{
    QImage image(d.swatchMaximum, d.swatchMaximum, QImage::Format_RGBA8888);
    image.fill(style()->color(Style::ColorRole::Render));
    return image;
}

MaterialBrowser::MaterialBrowser(QWidget* parent)
    : QWidget(parent)
    , p(new MaterialBrowserPrivate())
{
    p->d.browser = this;
    p->init();
}

MaterialBrowser::~MaterialBrowser() = default;

void
MaterialBrowser::setEntries(const QList<MaterialEntry>& entries)
{
    QSet<QString> selectedPaths;
    for (const MaterialEntry& entry : selectedEntries())
        selectedPaths.insert(QString::fromStdString(entry.materialPath.GetString()));

    QHash<QString, MaterialEntry> oldEntries;
    QHash<QString, QImage> oldImages;
    QHash<QString, bool> oldValid;
    for (int row = 0; row < p->d.entries.size(); ++row) {
        const QString path = QString::fromStdString(p->d.entries[row].materialPath.GetString());
        oldEntries.insert(path, p->d.entries[row]);
        oldImages.insert(path, p->d.swatches.value(row));
        oldValid.insert(path, p->d.swatchValid.value(row, false));
    }

    auto sameParameters = [](const MaterialParameters& a, const MaterialParameters& b) {
        return a.baseColor == b.baseColor && a.metalness == b.metalness && a.roughness == b.roughness
               && a.specular == b.specular && a.ior == b.ior && a.coat == b.coat && a.coatRoughness == b.coatRoughness
               && a.opacity == b.opacity && a.transmission == b.transmission
               && a.transmissionColor == b.transmissionColor;
    };

    auto sameMaterial = [&](const MaterialEntry& a, const MaterialEntry& b) {
        return a.materialPath == b.materialPath && a.shaderPath == b.shaderPath && a.shaderId == b.shaderId
               && sameParameters(a.parameters, b.parameters);
    };

    QList<MaterialEntry> orderedEntries = entries;
    const int previousCount = static_cast<int>(p->d.entries.size());
    bool appendOnly = orderedEntries.size() >= previousCount;

    if (appendOnly) {
        for (int row = 0; row < previousCount; ++row) {
            if (orderedEntries[row].materialPath != p->d.entries[row].materialPath) {
                appendOnly = false;
                break;
            }
        }
    }

    p->d.entries = orderedEntries;
    p->d.swatches.resize(orderedEntries.size());
    p->d.swatchValid.resize(orderedEntries.size());
    p->d.requestedRows.clear();

    for (int row = 0; row < orderedEntries.size(); ++row) {
        const MaterialEntry& entry = orderedEntries[row];
        const QString path = QString::fromStdString(entry.materialPath.GetString());
        const auto oldEntryIt = oldEntries.constFind(path);
        const auto imageIt = oldImages.constFind(path);

        const bool unchanged = oldEntryIt != oldEntries.cend() && sameMaterial(oldEntryIt.value(), entry);

        if (unchanged && imageIt != oldImages.cend() && !imageIt.value().isNull()) {
            p->d.swatches[row] = imageIt.value();
            p->d.swatchValid[row] = oldValid.value(path, false);
        }
        else {
            // A new or recreated material must never inherit a swatch solely
            // because it reused the same USD path. Show the placeholder and
            // request a fresh render from MaterialRenderer.
            p->d.swatches[row] = p->placeholderImage();
            p->d.swatchValid[row] = false;
        }
    }

    if (appendOnly && orderedEntries.size() > previousCount) {
        p->d.syncingSelection = true;

        for (int row = previousCount; row < orderedEntries.size(); ++row)
            p->insertRow(row);

        p->updateSourceRows();
        p->d.ui->count->setText(QString("%1 material(s)").arg(orderedEntries.size()));
        p->d.syncingSelection = false;
        p->applyFilter(p->d.ui->filter->text());
        p->updateViewSizes();
    }
    else {
        bool sameStructure = orderedEntries.size() == oldImages.size();
        if (sameStructure) {
            for (int row = 0; row < orderedEntries.size(); ++row) {
                if (!oldImages.contains(QString::fromStdString(orderedEntries[row].materialPath.GetString()))) {
                    sameStructure = false;
                    break;
                }
            }
        }

        if (sameStructure) {
            for (int row = 0; row < orderedEntries.size(); ++row)
                p->updateRow(row);
        }
        else {
            p->rebuild();
        }
    }

    p->d.syncingSelection = true;
    for (int row = 0; row < orderedEntries.size(); ++row) {
        const bool selected = selectedPaths.contains(
            QString::fromStdString(orderedEntries[row].materialPath.GetString()));

        if (QListWidgetItem* item = p->d.ui->icons->item(row))
            item->setSelected(selected);
        if (QListWidgetItem* item = p->d.ui->list->item(row))
            item->setSelected(selected);
        if (QTreeWidgetItem* item = p->d.ui->details->topLevelItem(row))
            item->setSelected(selected);
    }
    p->d.syncingSelection = false;

    Q_EMIT selectionChanged();
    refreshVisibleSwatches();
}

const QList<MaterialEntry>&
MaterialBrowser::entries() const
{
    return p->d.entries;
}

const MaterialEntry*
MaterialBrowser::entry(int row) const
{
    return row >= 0 && row < p->d.entries.size() ? &p->d.entries[row] : nullptr;
}

int
MaterialBrowser::rowForMaterialPath(const SdfPath& path) const
{
    for (int row = 0; row < p->d.entries.size(); ++row) {
        if (p->d.entries[row].materialPath == path)
            return row;
    }
    return -1;
}

void
MaterialBrowser::remapEntryPath(const SdfPath& oldPath, const SdfPath& newPath)
{
    if (oldPath.IsEmpty() || newPath.IsEmpty() || oldPath == newPath)
        return;

    const int row = rowForMaterialPath(oldPath);
    if (row < 0)
        return;

    p->d.entries[row].materialPath = newPath;
}

bool
MaterialBrowser::updateEntry(int row, const MaterialEntry& entry)
{
    if (row < 0 || row >= p->d.entries.size())
        return false;

    p->d.entries[row] = entry;
    p->updateRow(row);
    return true;
}

QList<int>
MaterialBrowser::selectedRows() const
{
    QSet<int> unique;
    QAbstractItemView* view = p->currentView();

    if (view == p->d.ui->icons) {
        for (QListWidgetItem* item : p->d.ui->icons->selectedItems())
            unique.insert(item->data(Qt::UserRole).toInt());
    }
    else if (view == p->d.ui->list) {
        for (QListWidgetItem* item : p->d.ui->list->selectedItems())
            unique.insert(item->data(Qt::UserRole).toInt());
    }
    else {
        for (QTreeWidgetItem* baseItem : p->d.ui->details->selectedItems()) {
            if (auto* item = dynamic_cast<MaterialItem*>(baseItem))
                unique.insert(item->sourceRow());
        }
    }

    QList<int> rows = unique.values();
    std::sort(rows.begin(), rows.end());
    return rows;
}

QList<MaterialEntry>
MaterialBrowser::selectedEntries() const
{
    QList<MaterialEntry> result;
    for (int row : selectedRows()) {
        if (const MaterialEntry* value = entry(row))
            result.append(*value);
    }
    return result;
}

void
MaterialBrowser::selectRow(int row)
{
    if (row < 0 || row >= p->d.entries.size())
        return;

    p->d.syncingSelection = true;
    p->d.ui->icons->clearSelection();
    p->d.ui->list->clearSelection();
    p->d.ui->details->clearSelection();

    if (QListWidgetItem* item = p->d.ui->icons->item(row))
        item->setSelected(true);
    if (QListWidgetItem* item = p->d.ui->list->item(row))
        item->setSelected(true);
    if (QTreeWidgetItem* item = p->d.ui->details->topLevelItem(row))
        item->setSelected(true);

    p->d.syncingSelection = false;

    if (p->d.mode == Icons)
        p->d.ui->icons->scrollToItem(p->d.ui->icons->item(row), QAbstractItemView::PositionAtCenter);
    else if (p->d.mode == List)
        p->d.ui->list->scrollToItem(p->d.ui->list->item(row), QAbstractItemView::PositionAtCenter);
    else
        p->d.ui->details->scrollToItem(p->d.ui->details->topLevelItem(row), QAbstractItemView::PositionAtCenter);

    Q_EMIT selectionChanged();
}

void
MaterialBrowser::selectRows(const QList<int>& rows)
{
    QSet<int> selected;
    for (int row : rows) {
        if (row >= 0 && row < p->d.entries.size())
            selected.insert(row);
    }

    p->d.syncingSelection = true;
    p->d.ui->icons->clearSelection();
    p->d.ui->list->clearSelection();
    p->d.ui->details->clearSelection();

    for (int row : selected) {
        if (QListWidgetItem* item = p->d.ui->icons->item(row))
            item->setSelected(true);
        if (QListWidgetItem* item = p->d.ui->list->item(row))
            item->setSelected(true);
        if (QTreeWidgetItem* item = p->d.ui->details->topLevelItem(row))
            item->setSelected(true);
    }

    p->d.syncingSelection = false;

    if (!selected.isEmpty()) {
        QList<int> ordered = selected.values();
        std::sort(ordered.begin(), ordered.end());
        const int row = ordered.first();

        if (p->d.mode == Icons)
            p->d.ui->icons->scrollToItem(p->d.ui->icons->item(row), QAbstractItemView::PositionAtCenter);
        else if (p->d.mode == List)
            p->d.ui->list->scrollToItem(p->d.ui->list->item(row), QAbstractItemView::PositionAtCenter);
        else
            p->d.ui->details->scrollToItem(p->d.ui->details->topLevelItem(row), QAbstractItemView::PositionAtCenter);
    }

    Q_EMIT selectionChanged();
}

void
MaterialBrowser::setViewMode(ViewMode mode)
{
    p->d.mode = mode;
    p->d.iconView->setChecked(mode == Icons);
    p->d.listView->setChecked(mode == List);
    p->d.detailView->setChecked(mode == Details);
    p->d.ui->stackedWidget->setCurrentIndex(mode == Icons ? 1 : (mode == List ? 2 : 0));
    p->d.ui->swatchSize->setEnabled(mode == Icons);
    refreshVisibleSwatches();
}

MaterialBrowser::ViewMode
MaterialBrowser::viewMode() const
{
    return p->d.mode;
}

void
MaterialBrowser::setSwatchSize(int size)
{
    p->d.ui->swatchSize->setValue(size);
}

int
MaterialBrowser::swatchSize() const
{
    return p->d.ui->swatchSize->value();
}

void
MaterialBrowser::setFilter(const QString& filter)
{
    p->d.ui->filter->setText(filter);
}

QString
MaterialBrowser::filter() const
{
    return p->d.ui->filter->text();
}

void
MaterialBrowser::setSwatch(int row, const QImage& image)
{
    if (row < 0 || row >= p->d.swatches.size())
        return;

    p->d.swatches[row] = image;
    p->d.swatchValid[row] = !image.isNull();
    p->d.requestedRows.remove(row);
    p->updateRow(row);
}

void
MaterialBrowser::invalidateSwatch(int row)
{
    if (row < 0 || row >= p->d.swatches.size())
        return;

    // Keep the current image visible while the new image renders.
    p->d.swatchValid[row] = false;
    p->d.requestedRows.remove(row);
}

QImage
MaterialBrowser::swatch(int row) const
{
    return row >= 0 && row < p->d.swatches.size() ? p->d.swatches[row] : QImage();
}

void
MaterialBrowser::refreshVisibleSwatches()
{
    QAbstractItemView* view = p->currentView();
    if (!view || !view->isVisible())
        return;

    const QRect viewportRect = view->viewport()->rect().adjusted(0, -view->viewport()->height(), 0,
                                                                 view->viewport()->height());

    for (int row = 0; row < p->d.entries.size(); ++row) {
        QModelIndex index;
        if (view == p->d.ui->icons)
            index = p->d.ui->icons->indexFromItem(p->d.ui->icons->item(row));
        else if (view == p->d.ui->list)
            index = p->d.ui->list->indexFromItem(p->d.ui->list->item(row));
        else
            index = p->d.ui->details->indexFromItem(p->d.ui->details->topLevelItem(row), 0);

        const QRect rect = view->visualRect(index);
        if (!rect.isValid() || !rect.intersects(viewportRect))
            continue;

        if (!p->d.swatchValid.value(row, false) && !p->d.requestedRows.contains(row)) {
            p->d.requestedRows.insert(row);
            Q_EMIT swatchRequested(row);
        }
    }
}

bool
MaterialBrowser::eventFilter(QObject* object, QEvent* event)
{
    const bool browserView = object == p->d.ui->icons || object == p->d.ui->list || object == p->d.ui->details;
    const bool browserViewport = object == p->d.ui->icons->viewport() || object == p->d.ui->list->viewport()
                                 || object == p->d.ui->details->viewport();

    if (browserView && event->type() == QEvent::ShortcutOverride) {
        auto* keyEvent = static_cast<QKeyEvent*>(event);
        if (keyEvent->matches(QKeySequence::SelectAll)) {
            keyEvent->accept();
            return true;
        }
    }

    if (browserView && event->type() == QEvent::KeyPress) {
        auto* keyEvent = static_cast<QKeyEvent*>(event);

        if (keyEvent->matches(QKeySequence::SelectAll)) {
            auto* view = static_cast<QAbstractItemView*>(object);
            view->selectAll();
            keyEvent->accept();
            return true;
        }

        if (keyEvent->key() == Qt::Key_Tab && keyEvent->modifiers() == Qt::NoModifier) {
            p->beginRename(static_cast<QAbstractItemView*>(object));
            keyEvent->accept();
            return true;
        }
    }

    if (browserViewport && event->type() == QEvent::ContextMenu) {
        auto* contextEvent = static_cast<QContextMenuEvent*>(event);

        QAbstractItemView* view = nullptr;
        if (object == p->d.ui->icons->viewport())
            view = p->d.ui->icons;
        else if (object == p->d.ui->list->viewport())
            view = p->d.ui->list;
        else if (object == p->d.ui->details->viewport())
            view = p->d.ui->details;

        if (view) {
            p->showContextMenu(view, contextEvent->pos());
            contextEvent->accept();
            return true;
        }
    }

    if (browserViewport && event->type() == QEvent::MouseButtonPress) {
        const auto* mouse = static_cast<QMouseEvent*>(event);
        if (mouse->button() == Qt::LeftButton) {
            const QPoint position = mouse->position().toPoint();
            QModelIndex index;

            if (object == p->d.ui->icons->viewport())
                index = p->d.ui->icons->indexAt(position);
            else if (object == p->d.ui->list->viewport())
                index = p->d.ui->list->indexAt(position);
            else if (object == p->d.ui->details->viewport())
                index = p->d.ui->details->indexAt(position);

            p->d.dragStartPosition = position;
            p->d.dragSourceRow = index.isValid() ? index.row() : -1;
            p->d.materialDragActive = false;
        }
    }
    else if (browserViewport && event->type() == QEvent::MouseMove) {
        const auto* mouse = static_cast<QMouseEvent*>(event);

        if (p->d.materialDragActive)
            return true;

        // A material drag may only begin when the original mouse press was on
        // a material item. If the press started in empty space, leave the event
        // entirely to QListView/QTreeView so rubber-band selection can work even
        // when the rectangle later crosses over a material swatch.
        if ((mouse->buttons() & Qt::LeftButton) && p->d.dragSourceRow >= 0
            && (mouse->position().toPoint() - p->d.dragStartPosition).manhattanLength()
                   >= QApplication::startDragDistance()) {
            const QList<int> rows = selectedRows();
            if (rows.size() == 1 && rows.first() == p->d.dragSourceRow) {
                if (const MaterialEntry* value = entry(p->d.dragSourceRow)) {
                    p->d.materialDragActive = true;

                    auto* mime = new QMimeData();
                    mime->setData(mime::material, QByteArray::fromStdString(value->materialPath.GetString()));

                    auto* drag = new QDrag(this);
                    drag->setMimeData(mime);

                    drag->exec(Qt::CopyAction);

                    // QDrag runs a nested event loop. Reset our drag candidate
                    // and synthesize the release that the item view may not have
                    // seen when the mouse was released outside this widget.
                    p->d.materialDragActive = false;
                    p->d.dragSourceRow = -1;
                    p->d.dragStartPosition = QPoint();

                    if (QWidget* viewport = qobject_cast<QWidget*>(object)) {
                        QMouseEvent releaseEvent(QEvent::MouseButtonRelease,
                                                 QPointF(viewport->mapFromGlobal(QCursor::pos())),
                                                 QPointF(QCursor::pos()), Qt::LeftButton, Qt::NoButton,
                                                 QApplication::keyboardModifiers());
                        QApplication::sendEvent(viewport, &releaseEvent);
                    }

                    return true;
                }
            }
        }
    }
    else if (browserViewport && event->type() == QEvent::MouseButtonRelease) {
        p->d.materialDragActive = false;
        p->d.dragSourceRow = -1;
        p->d.dragStartPosition = QPoint();
    }

    if (browserViewport
        && (event->type() == QEvent::Paint || event->type() == QEvent::Resize || event->type() == QEvent::Wheel
            || event->type() == QEvent::Show)) {
        p->d.visibleTimer->start();
    }

    return QWidget::eventFilter(object, event);
}

}  // namespace stageviz
