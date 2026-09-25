// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#include "materialbrowser.h"
#include "application.h"
#include "command.h"
#include "commandstack.h"
#include "materialitem.h"
#include "messagedialog.h"
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
#include <QFileDialog>
#include <QFileInfo>
#include <QHeaderView>
#include <QInputDevice>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMimeData>
#include <QMouseEvent>
#include <QNativeGestureEvent>
#include <QPainter>
#include <QPointer>
#include <QScrollBar>
#include <QSet>
#include <QStackedWidget>
#include <QStyle>
#include <QStyleOptionViewItem>
#include <QStyledItemDelegate>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>
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
    void panView(QAbstractItemView* view, const QPoint& delta);
    void updateViewSizes();
    void showViewMenu();
    void showContextMenu(QAbstractItemView* view, const QPoint& position);
    void assignMaterial(const MaterialEntry& material);
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

    class MaterialBrowserIconDelegate : public MaterialBrowserItemDelegate {
    public:
        explicit MaterialBrowserIconDelegate(QObject* parent = nullptr)
            : MaterialBrowserItemDelegate(parent)
        {}

        QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override
        {
            const auto* view = qobject_cast<const QListView*>(parent());
            if (view && view->gridSize().isValid())
                return view->gridSize();

            return MaterialBrowserItemDelegate::sizeHint(option, index);
        }

        void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override
        {
            if (!painter)
                return;

            QStyleOptionViewItem opt(option);
            initStyleOption(&opt, index);

            const QWidget* widget = opt.widget;
            const QStyle* style = widget ? widget->style() : QApplication::style();
            const auto* view = qobject_cast<const QListView*>(parent());

            const QSize gridSize = (view && view->gridSize().isValid()) ? view->gridSize() : opt.rect.size();
            const QSize iconSize = (view && view->iconSize().isValid()) ? view->iconSize() : opt.decorationSize;

            // Keep card geometry stable when the viewport clips during splitter resize.
            const QRect fullRect(opt.rect.topLeft(), gridSize);

            painter->save();

            QStyleOptionViewItem backgroundOpt(opt);
            backgroundOpt.rect = fullRect;
            backgroundOpt.text.clear();
            backgroundOpt.icon = QIcon();
            style->drawPrimitive(QStyle::PE_PanelItemViewItem, &backgroundOpt, painter, widget);

            const int padding = 8;
            const int textHeight = opt.fontMetrics.height() + 4;

            const QRect iconRect(fullRect.left() + (fullRect.width() - iconSize.width()) / 2, fullRect.top() + padding,
                                 iconSize.width(), iconSize.height());

            const QRect textRect(fullRect.left() + padding, fullRect.top() + padding + iconSize.height() + padding,
                                 fullRect.width() - padding * 2, textHeight);

            if (!opt.icon.isNull()) {
                QIcon::Mode mode = QIcon::Normal;
                if (!(opt.state & QStyle::State_Enabled))
                    mode = QIcon::Disabled;
                else if (opt.state & QStyle::State_Selected)
                    mode = QIcon::Selected;

                opt.icon.paint(painter, iconRect, Qt::AlignCenter, mode, QIcon::Off);
            }

            painter->setFont(opt.font);

            QPalette::ColorGroup group = (opt.state & QStyle::State_Enabled) ? QPalette::Normal : QPalette::Disabled;
            if (!(opt.state & QStyle::State_Active))
                group = QPalette::Inactive;

            const QPalette::ColorRole role = (opt.state & QStyle::State_Selected) ? QPalette::HighlightedText
                                                                                  : QPalette::Text;
            painter->setPen(opt.palette.color(group, role));

            const QString text = opt.fontMetrics.elidedText(opt.text, Qt::ElideRight, textRect.width());
            painter->drawText(textRect, Qt::AlignCenter | Qt::TextSingleLine, text);

            painter->restore();
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
        QPoint panLastPosition;
        QList<int> dragSelectionRows;
        Qt::KeyboardModifiers dragModifiers = Qt::NoModifier;
        int swatchSize = 128;
        int swatchMinimum = 64;
        int swatchMaximum = 512;
        int swatchPadding = 8;
        int dragSourceRow = -1;
        bool materialDragActive = false;
        bool dragPressPending = false;
        bool panning = false;
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
        // Dragging is handled manually in eventFilter(); keep the view drag state disabled.
        browser->setDragEnabled(false);
        browser->setDragDropMode(QAbstractItemView::NoDragDrop);
        browser->setDefaultDropAction(Qt::CopyAction);
        browser->setEditTriggers(QAbstractItemView::NoEditTriggers);
        browser->installEventFilter(d.browser.data());
        browser->viewport()->installEventFilter(d.browser.data());
    }

    d.ui->icons->setItemDelegate(new MaterialBrowserIconDelegate(d.ui->icons));
    d.ui->list->setItemDelegate(new MaterialBrowserItemDelegate(d.ui->list));
    d.ui->details->setItemDelegate(new MaterialBrowserItemDelegate(d.ui->details));

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

    connect(d.visibleTimer, &QTimer::timeout, d.browser.data(), [this]() { d.browser->refreshVisibleSwatches(); });
    connect(d.ui->filter, &QLineEdit::textChanged, d.browser.data(), [this](const QString& text) {
        applyFilter(text);
        d.ui->clear->setEnabled(!text.isEmpty());
        d.visibleTimer->start();
    });
    connect(d.ui->clear, &QToolButton::clicked, d.ui->filter, &QLineEdit::clear);
    connect(d.ui->view, &QToolButton::clicked, d.browser.data(), [this]() { showViewMenu(); });
    connect(d.ui->view, &QWidget::customContextMenuRequested, d.browser.data(),
            [this](const QPoint&) { showViewMenu(); });
    connect(d.iconView, &QAction::triggered, d.browser.data(),
            [this]() { d.browser->setViewMode(MaterialBrowser::Icons); });
    connect(d.listView, &QAction::triggered, d.browser.data(),
            [this]() { d.browser->setViewMode(MaterialBrowser::List); });
    connect(d.detailView, &QAction::triggered, d.browser.data(),
            [this]() { d.browser->setViewMode(MaterialBrowser::Details); });
    // Icon double-click distinguishes rename-on-name from activate-on-swatch.
    connect(d.ui->list, &QAbstractItemView::doubleClicked, d.browser.data(), [this](const QModelIndex& index) {
        if (index.row() >= 0 && index.row() < d.entries.size())
            Q_EMIT d.browser->materialActivated(d.entries[index.row()].materialPath);
    });
    connect(d.ui->details, &QAbstractItemView::doubleClicked, d.browser.data(), [this](const QModelIndex& index) {
        if (index.row() >= 0 && index.row() < d.entries.size())
            Q_EMIT d.browser->materialActivated(d.entries[index.row()].materialPath);
    });
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

    // Empty-space context click keeps the existing controller-owned New menu.
    if (!index.isValid()) {
        Q_EMIT d.browser->newMaterialRequested(view->viewport()->mapToGlobal(position));
        return;
    }

    const MaterialEntry* contextEntry = d.browser->entry(index.row());
    if (!contextEntry)
        return;

    const MaterialEntry contextMaterial = *contextEntry;
    const bool hasSceneSelection = !session()->selectionList()->paths().isEmpty();
    const bool contextMaterialSelected = d.browser->selectedRows().contains(index.row());

    QMenu menu(view);

    QAction* assignAction = menu.addAction(tr("Assign"));
    assignAction->setEnabled(hasSceneSelection);

    menu.addSeparator();

    QAction* duplicateMaterial = menu.addAction(tr("Duplicate"));

    menu.addSeparator();

    QMenu* copy = menu.addMenu(tr("Copy"));
    QAction* copyName = copy->addAction(tr("Name"));
    QAction* copyPath = copy->addAction(tr("Path"));

    menu.addSeparator();

    QMenu* exportMenu = menu.addMenu(tr("Export"));
    QAction* exportMaterialX = exportMenu->addAction(tr("MaterialX File..."));
    exportMaterialX->setEnabled(contextMaterial.shaderId.startsWith(QStringLiteral("ND_")));

    menu.addSeparator();

    QMenu* newMenu = menu.addMenu(tr("New"));
    QAction* newPreviewSurface = newMenu->addAction(tr("USD Preview Surface"));
    QAction* newStandardSurface = newMenu->addAction(tr("MaterialX Standard Surface"));
    QAction* newOpenPBRSurface = newMenu->addAction(tr("MaterialX OpenPBR Surface"));
    newMenu->addSeparator();
    QAction* newMaterialXFile = newMenu->addAction(tr("MaterialX File..."));

    QAction* deleteMaterial = menu.addAction(tr("Delete"));
    // Delete still operates on browser selection, so an RMB click on another
    // material must never delete the previously selected material.
    deleteMaterial->setEnabled(contextMaterialSelected);

    QAction* action = menu.exec(view->viewport()->mapToGlobal(position));
    if (!action)
        return;

    if (action == assignAction) {
        assignMaterial(contextMaterial);
    }
    else if (action == duplicateMaterial) {
        // Duplicate only the RMB material and preserve the current scene/browser selection.
        session()->commandStack()->run(new Command(duplicatePaths({ contextMaterial.materialPath }, false)));
    }
    else if (action == copyName) {
        QApplication::clipboard()->setText(contextMaterial.name);
    }
    else if (action == copyPath) {
        QApplication::clipboard()->setText(QString::fromStdString(contextMaterial.materialPath.GetString()));
    }
    else if (action == exportMaterialX) {
        QString baseName = contextMaterial.name.trimmed();
        if (baseName.isEmpty())
            baseName = QStringLiteral("Material");

        QString filename = QFileDialog::getSaveFileName(d.browser.data(), tr("Export MaterialX File"),
                                                        baseName + QStringLiteral(".mtlx"),
                                                        tr("MaterialX Files (*.mtlx)"));

        if (!filename.isEmpty()) {
            if (!filename.endsWith(QStringLiteral(".mtlx"), Qt::CaseInsensitive))
                filename += QStringLiteral(".mtlx");

            QString error;
            bool success = false;
            {
                READ_LOCKER(locker, session()->stageLock(), "stageLock");
                const UsdStageRefPtr stage = session()->stageUnsafe();
                success = MaterialUtils::exportMaterialX(stage, contextMaterial.materialPath, filename, error);
            }

            if (!success) {
                MessageDialog::warning(d.browser.data(), tr("Export MaterialX File"),
                                       error.isEmpty() ? tr("Failed to export the material.") : error);
            }
        }
    }
    else if (action == newPreviewSurface) {
        Q_EMIT d.browser->createMaterialRequested(QStringLiteral("UsdPreviewSurface"));
    }
    else if (action == newStandardSurface) {
        Q_EMIT d.browser->createMaterialRequested(QStringLiteral("MaterialXStandardSurface"));
    }
    else if (action == newOpenPBRSurface) {
        Q_EMIT d.browser->createMaterialRequested(QStringLiteral("MaterialXOpenPBRSurface"));
    }
    else if (action == newMaterialXFile) {
        Q_EMIT d.browser->createMaterialRequested(QStringLiteral("MaterialXFile"));
    }
    else if (action == deleteMaterial) {
        Q_EMIT d.browser->deleteRequested();
    }
}


void
MaterialBrowserPrivate::assignMaterial(const MaterialEntry& material)
{
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

    session()->commandStack()->run(new Command(bindMaterial(paths, material.materialPath)));
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

    // Ignore transient/duplicate itemChanged values while an editor closes.
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

    // Keep views unchanged until the stage notice returns the sanitized/unique name.
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

    // Keep the full-resolution render in QIcon so Qt can choose the proper high-DPI representation.
    QIcon icon;
    if (!image.isNull())
        icon = QIcon(QPixmap::fromImage(image));

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
MaterialBrowserPrivate::panView(QAbstractItemView* view, const QPoint& delta)
{
    if (!view || delta.isNull())
        return;

    // Content follows the fingers, matching the graph/viewport pan convention.
    if (QScrollBar* horizontal = view->horizontalScrollBar())
        horizontal->setValue(horizontal->value() - delta.x());
    if (QScrollBar* vertical = view->verticalScrollBar())
        vertical->setValue(vertical->value() - delta.y());
}

void
MaterialBrowserPrivate::updateViewSizes()
{
    const int size = std::clamp(d.swatchSize, d.swatchMinimum, d.swatchMaximum);
    d.swatchSize = size;

    const int nameHeight = d.ui->icons->fontMetrics().height() + 4;
    const int bottomPadding = 6;
    const QSize gridSize(size + d.swatchPadding * 2, size + nameHeight + d.swatchPadding * 2 + bottomPadding);

    d.ui->icons->setIconSize(QSize(size, size));
    d.ui->icons->setGridSize(gridSize);

    for (int row = 0; row < d.ui->icons->count(); ++row) {
        QListWidgetItem* item = d.ui->icons->item(row);
        if (!item)
            continue;

        item->setSizeHint(gridSize);

        const QImage image = d.swatches.value(row);
        if (!image.isNull()) {
            // Reuse the full-resolution render for high-DPI resizing.
            item->setIcon(QIcon(QPixmap::fromImage(image)));
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
            // Recreated materials must not inherit a swatch solely from a reused USD path.
            p->d.swatches[row] = p->placeholderImage();
            p->d.swatchValid[row] = false;
        }
    }

    if (appendOnly && orderedEntries.size() > previousCount) {
        p->d.syncingSelection = true;

        for (int row = previousCount; row < orderedEntries.size(); ++row)
            p->insertRow(row);

        p->updateSourceRows();
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
    const int clamped = std::clamp(size, p->d.swatchMinimum, p->d.swatchMaximum);
    if (clamped == p->d.swatchSize)
        return;

    p->d.swatchSize = clamped;
    p->updateViewSizes();
    p->d.visibleTimer->start();
}

int
MaterialBrowser::swatchSize() const
{
    return p->d.swatchSize;
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

    // Keep the current image visible while its replacement renders.
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
    const bool iconView = object == p->d.ui->icons || object == p->d.ui->icons->viewport();

    // Match graph/viewport controls: pinch or wheel zoom, trackpad pan, Shift+trackpad zoom, middle-drag pan.
#ifdef Q_OS_MAC
    if (iconView && event && event->type() == QEvent::NativeGesture) {
        auto* gesture = static_cast<QNativeGestureEvent*>(event);
        if (gesture->gestureType() == Qt::ZoomNativeGesture) {
            const qreal delta = std::clamp<qreal>(gesture->value(), -0.5, 0.5);
            const int target = qRound(static_cast<qreal>(p->d.swatchSize) * std::exp(delta));
            setSwatchSize(target);
            event->accept();
            return true;
        }
    }
#endif

    if (iconView && event && event->type() == QEvent::Wheel) {
        auto* wheel = static_cast<QWheelEvent*>(event);
        const QPoint pixelDelta = wheel->pixelDelta();
        const QPoint angleDelta = wheel->angleDelta();
        const QPointingDevice* device = wheel->pointingDevice();
        const bool isTrackpad = device && device->type() == QInputDevice::DeviceType::TouchPad;

        if (isTrackpad && !pixelDelta.isNull() && !(wheel->modifiers() & Qt::ShiftModifier)) {
            p->panView(p->d.ui->icons, pixelDelta);
            p->d.visibleTimer->start();
            wheel->accept();
            return true;
        }

        QPoint zoomDelta = angleDelta;
        qreal divisor = 1000.0;
        if ((isTrackpad || zoomDelta.isNull()) && !pixelDelta.isNull()) {
            zoomDelta = pixelDelta;
            divisor = 300.0;
        }

        if (!zoomDelta.isNull()) {
            const qreal delta = std::clamp<qreal>(static_cast<qreal>(zoomDelta.y()) / divisor, -0.5, 0.5);
            const int target = qRound(static_cast<qreal>(p->d.swatchSize) * std::exp(delta));
            setSwatchSize(target);
            wheel->accept();
            return true;
        }
    }

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

    if (iconView && browserViewport && event->type() == QEvent::MouseButtonDblClick) {
        auto* mouse = static_cast<QMouseEvent*>(event);
        if (mouse->button() == Qt::LeftButton && object == p->d.ui->icons->viewport()) {
            const QPoint position = mouse->position().toPoint();
            const QModelIndex index = p->d.ui->icons->indexAt(position);
            if (index.isValid() && index.row() >= 0 && index.row() < p->d.entries.size()) {
                const QRect itemRect = p->d.ui->icons->visualRect(index);
                const int iconHeight = p->d.ui->icons->iconSize().height();
                const int padding = p->d.swatchPadding;
                const int nameTop = itemRect.top() + padding + iconHeight;

                if (position.y() >= nameTop) {
                    p->beginRename(p->d.ui->icons, index.row());
                }
                else {
                    Q_EMIT materialActivated(p->d.entries[index.row()].materialPath);
                }

                mouse->accept();
                return true;
            }
        }
    }

    if (iconView && browserViewport && event->type() == QEvent::MouseButtonPress) {
        auto* mouse = static_cast<QMouseEvent*>(event);
        if (mouse->button() == Qt::MiddleButton) {
            p->d.panning = true;
            p->d.panLastPosition = mouse->position().toPoint();
            if (QWidget* viewport = qobject_cast<QWidget*>(object))
                viewport->setCursor(Qt::ClosedHandCursor);
            mouse->accept();
            return true;
        }
    }

    if (p->d.panning && iconView && browserViewport && event->type() == QEvent::MouseMove) {
        auto* mouse = static_cast<QMouseEvent*>(event);
        if (!(mouse->buttons() & Qt::MiddleButton)) {
            p->d.panning = false;
            p->d.panLastPosition = QPoint();
            if (QWidget* viewport = qobject_cast<QWidget*>(object))
                viewport->unsetCursor();
        }
        else {
            const QPoint position = mouse->position().toPoint();
            const QPoint delta = position - p->d.panLastPosition;
            p->d.panLastPosition = position;
            p->panView(p->d.ui->icons, delta);
            p->d.visibleTimer->start();
            mouse->accept();
            return true;
        }
    }

    if (p->d.panning && iconView && browserViewport && event->type() == QEvent::MouseButtonRelease) {
        auto* mouse = static_cast<QMouseEvent*>(event);
        if (mouse->button() == Qt::MiddleButton) {
            p->d.panning = false;
            p->d.panLastPosition = QPoint();
            if (QWidget* viewport = qobject_cast<QWidget*>(object))
                viewport->unsetCursor();
            mouse->accept();
            return true;
        }
    }

    if (browserViewport && event->type() == QEvent::MouseButtonPress) {
        auto* mouse = static_cast<QMouseEvent*>(event);

        // QAbstractItemView normally changes the current/selected row on a
        // right-button press before the ContextMenu event is delivered. Handle
        // the mouse context menu here and consume the press so RMB never changes
        // browser selection. The material under the cursor is passed directly to
        // showContextMenu(), so Assign still operates on that material.
        if (mouse->button() == Qt::RightButton) {
            QAbstractItemView* view = nullptr;
            if (object == p->d.ui->icons->viewport())
                view = p->d.ui->icons;
            else if (object == p->d.ui->list->viewport())
                view = p->d.ui->list;
            else if (object == p->d.ui->details->viewport())
                view = p->d.ui->details;

            if (view) {
                p->showContextMenu(view, mouse->position().toPoint());
                mouse->accept();
                return true;
            }
        }

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
            p->d.dragSelectionRows = selectedRows();
            p->d.dragModifiers = mouse->modifiers();
            p->d.materialDragActive = false;
            p->d.dragPressPending = index.isValid();

            // Delay item selection until release. This lets a material that is
            // not currently selected be dragged without changing the browser
            // selection first. Once movement passes the drag threshold, the
            // pressed row becomes only the drag source, not the selected row.
            if (p->d.dragPressPending) {
                mouse->accept();
                return true;
            }
        }
    }
    else if (browserViewport && event->type() == QEvent::MouseMove) {
        auto* mouse = static_cast<QMouseEvent*>(event);

        if (p->d.materialDragActive)
            return true;

        // A pending press becomes a drag only after the normal Qt threshold.
        // The pressed row is the drag source even when another material remains selected.
        if ((mouse->buttons() & Qt::LeftButton) && p->d.dragPressPending && p->d.dragSourceRow >= 0
            && (mouse->position().toPoint() - p->d.dragStartPosition).manhattanLength()
                   >= QApplication::startDragDistance()) {
            if (const MaterialEntry* value = entry(p->d.dragSourceRow)) {
                p->d.materialDragActive = true;
                p->d.dragPressPending = false;

                auto* mime = new QMimeData();
                mime->setData(mime::material, QByteArray::fromStdString(value->materialPath.GetString()));

                auto* drag = new QDrag(this);
                drag->setMimeData(mime);


                drag->exec(Qt::CopyAction, Qt::CopyAction);

                p->d.materialDragActive = false;
                p->d.dragSourceRow = -1;
                p->d.dragStartPosition = QPoint();
                p->d.dragSelectionRows.clear();
                p->d.dragModifiers = Qt::NoModifier;
                return true;
            }
        }

        if (p->d.dragPressPending) {
            mouse->accept();
            return true;
        }
    }
    else if (browserViewport && event->type() == QEvent::MouseButtonRelease) {
        auto* mouse = static_cast<QMouseEvent*>(event);

        if (mouse->button() == Qt::LeftButton && p->d.dragPressPending && p->d.dragSourceRow >= 0) {
            QList<int> rows = p->d.dragSelectionRows;
            const int row = p->d.dragSourceRow;
            const Qt::KeyboardModifiers modifiers = p->d.dragModifiers;

            if (modifiers & (Qt::ControlModifier | Qt::MetaModifier)) {
                if (rows.contains(row))
                    rows.removeAll(row);
                else
                    rows.append(row);
                p->d.browser->selectRows(rows);
            }
            else if (modifiers & Qt::ShiftModifier) {
                int anchor = row;
                if (!rows.isEmpty())
                    anchor = rows.last();

                QList<int> range;
                const int first = std::min(anchor, row);
                const int last = std::max(anchor, row);
                for (int current = first; current <= last; ++current)
                    range.append(current);
                p->d.browser->selectRows(range);
            }
            else {
                p->d.browser->selectRow(row);
            }

            mouse->accept();
        }

        p->d.materialDragActive = false;
        p->d.dragPressPending = false;
        p->d.dragSourceRow = -1;
        p->d.dragStartPosition = QPoint();
        p->d.dragSelectionRows.clear();
        p->d.dragModifiers = Qt::NoModifier;
    }

    if (browserViewport && event->type() == QEvent::ContextMenu) {
        auto* contextEvent = static_cast<QContextMenuEvent*>(event);

        // Mouse-triggered context menus are already handled on RMB press above
        // so the item view never gets a chance to alter selection. Keep this
        // path for keyboard-triggered context menus only.
        if (contextEvent->reason() == QContextMenuEvent::Mouse) {
            contextEvent->accept();
            return true;
        }

        QAbstractItemView* view = nullptr;
        if (object == p->d.ui->icons->viewport())
            view = p->d.ui->icons;
        else if (object == p->d.ui->list->viewport())
            view = p->d.ui->list;
        else if (object == p->d.ui->details->viewport())
            view = p->d.ui->details;

        if (view) {
            QPoint position;
            const QModelIndex current = view->currentIndex();
            if (current.isValid())
                position = view->visualRect(current).center();
            else
                position = view->viewport()->rect().center();

            p->showContextMenu(view, position);
            contextEvent->accept();
            return true;
        }
    }

    if (browserViewport
        && (event->type() == QEvent::Paint || event->type() == QEvent::Resize || event->type() == QEvent::Wheel
            || event->type() == QEvent::Show)) {
        p->d.visibleTimer->start();
    }

    return QWidget::eventFilter(object, event);
}

}  // namespace stageviz
