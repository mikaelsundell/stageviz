// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#include "outlinerview.h"
#include "application.h"
#include "notice.h"
#include "outlinertree.h"
#include "perfscope.h"
#include "primitem.h"
#include "qtutils.h"
#include "signalguard.h"
#include "tracelocks.h"
#include "ui.h"
#include "viewcontext.h"
#include <QAction>
#include <QMenu>
#include <QPointer>
#include <QSet>
#include <QSignalBlocker>
#include <QTimer>
#include <algorithm>
#include <pxr/usd/usd/prim.h>
#include <pxr/usd/usd/primRange.h>

PXR_NAMESPACE_USING_DIRECTIVE

// generated files
#include "ui_outlinerview.h"

namespace stageviz {

class OutlinerViewPrivate : public QObject, public SignalGuard {
public:
    OutlinerViewPrivate();
    void init();
    OutlinerTree* outlinerTree();
    bool eventFilter(QObject* obj, QEvent* event);

public Q_SLOTS:
    void clearFilter();
    void clearDepth();
    void collapse();
    void expand();
    void follow(bool enabled);
    void showDisplayMenu();
    void updateDisplayButton();
    void filterChanged(const QString& filter);
    void colorsChanged();
    void editLayerChanged(SdfLayerHandle layer);
    void maskChanged(const QList<SdfPath>& paths);
    void primsChanged(const NoticeBatch& batch);
    void selectionChanged(const QList<SdfPath>& paths);
    void stageChanged(UsdStageRefPtr stage, Session::LoadPolicy policy, Session::StageStatus status);
    void depthChanged(int value);

public:
    void updateDepth(const SdfPath& path = SdfPath());

    struct Data {
        bool followEnabled;
        bool displayMenuOpen = false;
        QScopedPointer<ViewContext> context;
        QScopedPointer<Ui_OutlinerView> ui;
        QPointer<OutlinerView> view;
    };
    Data d;
};

OutlinerViewPrivate::OutlinerViewPrivate() { d.followEnabled = false; }

void
OutlinerViewPrivate::init()
{
    d.ui.reset(new Ui_OutlinerView());
    d.ui->setupUi(d.view.data());
    d.followEnabled = d.ui->follow->isChecked();
    d.context.reset(new ViewContext(d.view.data()));
    d.context->setStageLock(session()->stageLock());
    d.context->setCommandStack(session()->commandStack());
    d.context->setSelectionList(session()->selectionList());
    d.context->setViewState(session()->viewState());
    attach(d.ui->depth);
    outlinerTree()->setColumnCount(3);
    outlinerTree()->setHeaderLabels(QStringList() << "Name"
                                                  << ""
                                                  << "");
    outlinerTree()->setContext(d.context.data());
    outlinerTree()->installEventFilter(this);
    d.ui->clear->setIcon(ui()->icon(UI::IconRole::Clear));
    d.ui->follow->setIcon(ui()->icon(UI::IconRole::Follow));
    d.ui->display->setIcon(ui()->icon(UI::IconRole::Display));
    d.ui->collapse->setIcon(ui()->icon(UI::IconRole::List));
    d.ui->expand->setIcon(ui()->icon(UI::IconRole::Nested));
    // connect
    connect(d.ui->filter, &QLineEdit::textChanged, this, &OutlinerViewPrivate::filterChanged);
    connect(d.ui->clear, &QToolButton::clicked, this, &OutlinerViewPrivate::clearFilter);
    connect(d.ui->collapse, &QToolButton::clicked, this, &OutlinerViewPrivate::collapse);
    connect(d.ui->expand, &QToolButton::clicked, this, &OutlinerViewPrivate::expand);
    connect(d.ui->follow, &QToolButton::toggled, this, &OutlinerViewPrivate::follow);
    connect(d.ui->display, &QToolButton::clicked, this, &OutlinerViewPrivate::showDisplayMenu);
    connect(d.ui->display, &QWidget::customContextMenuRequested, this, [this](const QPoint&) { showDisplayMenu(); });
    connect(outlinerTree(), &OutlinerTree::displayFiltersChanged, this, &OutlinerViewPrivate::updateDisplayButton);
    connect(d.ui->depth, &QSlider::valueChanged, this, &OutlinerViewPrivate::depthChanged);
    connect(session(), &Session::colorsChanged, this, &OutlinerViewPrivate::colorsChanged);
    connect(session(), &Session::editLayerChanged, this, &OutlinerViewPrivate::editLayerChanged);
    connect(session(), &Session::maskChanged, this, &OutlinerViewPrivate::maskChanged);
    connect(session(), &Session::primsChanged, this, &OutlinerViewPrivate::primsChanged);
    connect(session(), &Session::stageChanged, this, &OutlinerViewPrivate::stageChanged);
    connect(session()->selectionList(), &SelectionList::selectionChanged, this, &OutlinerViewPrivate::selectionChanged);
    updateDisplayButton();
}

OutlinerTree*
OutlinerViewPrivate::outlinerTree()
{
    return d.ui->outlinerTree;
}

bool
OutlinerViewPrivate::eventFilter(QObject* obj, QEvent* event)
{
    if (event->type() == QEvent::Show) {
        if (auto* tree = qobject_cast<QTreeWidget*>(obj)) {
            if (tree == outlinerTree()) {
                auto* header = tree->header();
                header->setStretchLastSection(false);
                header->setSectionResizeMode(PrimItem::Name, QHeaderView::Stretch);
                header->setSectionResizeMode(PrimItem::Visibility, QHeaderView::Fixed);
                header->setSectionResizeMode(PrimItem::Override, QHeaderView::Fixed);
                tree->setColumnWidth(PrimItem::Visibility, 20);
                tree->setColumnWidth(PrimItem::Override, 35);
            }
        }
    }
    return QObject::eventFilter(obj, event);
}

void
OutlinerViewPrivate::clearFilter()
{
    d.ui->filter->setText(QString());
}

void
OutlinerViewPrivate::clearDepth()
{
    d.ui->depth->setMinimum(0);
    d.ui->depth->setMaximum(10);
    d.ui->depth->setValue(0);
    d.ui->depth->setEnabled(false);
    d.ui->collapse->setEnabled(false);
    d.ui->expand->setEnabled(false);
}

void
OutlinerViewPrivate::collapse()
{
    d.ui->depth->setValue(d.ui->depth->minimum());
}

void
OutlinerViewPrivate::expand()
{
    d.ui->depth->setValue(d.ui->depth->maximum());
}

void
OutlinerViewPrivate::follow(bool enabled)
{
    d.followEnabled = enabled;

    if (d.ui->follow->isChecked() != enabled) {
        const QSignalBlocker blocker(d.ui->follow);
        d.ui->follow->setChecked(enabled);
    }

    if (!enabled)
        return;

    const SelectionList* list = d.context->selectionList();
    const QList<SdfPath> paths = list ? list->paths() : QList<SdfPath>();

    if (!paths.isEmpty())
        outlinerTree()->reveal(paths.first());
}

void
OutlinerViewPrivate::showDisplayMenu()
{
    struct DisplayEntry {
        QString key;
        QString label;
    };

    QList<QPair<QString, QString>> layers;
    QStringList contributors;
    QStringList types;
    const bool untitledStage = session()->filename().isEmpty();
    {
        READ_LOCKER(locker, session()->stageLock(), "stageLock");
        const UsdStageRefPtr stage = session()->stageUnsafe();
        if (!stage)
            return;

        QSet<QString> seenLayers;
        for (const SdfLayerHandle& layer : stage->GetLayerStack(false)) {
            if (!layer)
                continue;
            const QString identifier = QString::fromStdString(layer->GetIdentifier());
            if (seenLayers.contains(identifier))
                continue;
            seenLayers.insert(identifier);

            QString label = QString::fromStdString(layer->GetDisplayName());
            if (label.isEmpty())
                label = identifier;
            if (layer == stage->GetRootLayer()) {
                if (untitledStage)
                    label = tr("Untitled");
                label += tr("  (Root)");
            }
            layers.append({ identifier, label });
        }

        for (const auto& layer : stage->GetUsedLayers()) {
            if (layer && layer != stage->GetSessionLayer())
                contributors.append(QString::fromStdString(layer->GetIdentifier()));
        }

        QSet<QString> seenTypes;
        for (const UsdPrim& prim : UsdPrimRange(stage->GetPseudoRoot())) {
            const QString type = QString::fromStdString(prim.GetTypeName().GetString());
            if (!type.isEmpty())
                seenTypes.insert(type);
        }
        types = seenTypes.values();
        std::sort(types.begin(), types.end(),
                  [](const QString& lhs, const QString& rhs) { return lhs.compare(rhs, Qt::CaseInsensitive) < 0; });
    }

    OutlinerTree* tree = outlinerTree();
    if (!tree)
        return;

    QMenu menu(d.ui->display);
    menu.setObjectName("outlinerDisplayMenu");
    menu.setToolTipsVisible(true);
    const auto sourceActions = qt::sourceFilterMenu(&menu, layers, contributors, tree->displayLayers(),
                                                    tree->displayLayersFiltered());
    QStringList allLayerKeys;
    for (auto* action : sourceActions)
        allLayerKeys.append(action->data().toString());
    for (auto* action : sourceActions) {
        connect(action, &QAction::toggled, &menu, [tree, allLayerKeys, action](bool checked) {
            QStringList selected = tree->displayLayers();
            if (!tree->displayLayersFiltered())
                selected = allLayerKeys;

            const QString key = action->data().toString();
            if (checked && !selected.contains(key))
                selected.append(key);
            else if (!checked)
                selected.removeAll(key);

            const bool filtered = selected.size() != allLayerKeys.size();
            if (!filtered)
                selected.clear();
            tree->setDisplayFilters(selected, tree->displayTypes(), tree->showUntyped(), filtered,
                                    tree->displayTypesFiltered());
        });
    }

    menu.addSeparator();
    menu.addAction(tr("Prim types"))->setEnabled(false);

    const QStringList activeTypes = tree->displayTypes();
    for (const QString& type : std::as_const(types)) {
        QAction* action = menu.addAction(type);
        action->setCheckable(true);
        action->setChecked(!tree->displayTypesFiltered() || activeTypes.contains(type));
        action->setData(type);
        connect(action, &QAction::toggled, &menu, [tree, types, action](bool checked) {
            QStringList selected = tree->displayTypes();
            if (!tree->displayTypesFiltered())
                selected = types;

            const QString typeName = action->data().toString();
            if (checked && !selected.contains(typeName))
                selected.append(typeName);
            else if (!checked)
                selected.removeAll(typeName);

            const bool filtered = selected.size() != types.size();
            if (!filtered)
                selected.clear();
            tree->setDisplayFilters(tree->displayLayers(), selected, tree->showUntyped(), tree->displayLayersFiltered(),
                                    filtered);
        });
    }

    menu.addSeparator();
    QAction* untyped = menu.addAction(tr("Untyped"));
    untyped->setCheckable(true);
    untyped->setChecked(tree->showUntyped());
    untyped->setToolTip(tr("Show prims without a schema type"));
    connect(untyped, &QAction::toggled, &menu, [tree](bool checked) {
        tree->setDisplayFilters(tree->displayLayers(), tree->displayTypes(), checked, tree->displayLayersFiltered(),
                                tree->displayTypesFiltered());
    });

    menu.addSeparator();
    menu.addAction(tr("Purpose"))->setEnabled(false);
    const QList<DisplayEntry> purposes { { QStringLiteral("default"), tr("Default") },
                                         { QStringLiteral("render"), tr("Render") },
                                         { QStringLiteral("proxy"), tr("Proxy") },
                                         { QStringLiteral("guide"), tr("Guide") } };
    for (const auto& purpose : purposes) {
        QAction* action = menu.addAction(purpose.label);
        action->setObjectName(QStringLiteral("outlinerPurpose_%1").arg(purpose.key));
        action->setCheckable(true);
        action->setChecked(tree->displayPurposes().contains(purpose.key));
        action->setData(QStringLiteral("purpose:%1").arg(purpose.key));
        connect(action, &QAction::toggled, &menu, [tree, purpose](bool checked) {
            QStringList selected = tree->displayPurposes();
            if (checked && !selected.contains(purpose.key))
                selected.append(purpose.key);
            else if (!checked)
                selected.removeAll(purpose.key);
            tree->setDisplayPurposes(selected);
        });
    }

    d.displayMenuOpen = true;
    updateDisplayButton();
    menu.exec(d.ui->display->mapToGlobal(QPoint(0, d.ui->display->height())));
    d.displayMenuOpen = false;
    updateDisplayButton();
}

void
OutlinerViewPrivate::updateDisplayButton()
{
    const auto* tree = outlinerTree();
    const bool customized = tree->displayLayersFiltered() || tree->displayTypesFiltered() || tree->showUntyped()
                            || tree->displayPurposes().size() != 4;
    d.ui->display->setChecked(d.displayMenuOpen || (d.ui->display->isEnabled() && customized));
}

void
OutlinerViewPrivate::filterChanged(const QString& filter)
{
    outlinerTree()->setFilter(filter);
    d.ui->clear->setEnabled(!filter.isEmpty());
}

void
OutlinerViewPrivate::colorsChanged()
{
    outlinerTree()->viewport()->update();
}

void
OutlinerViewPrivate::editLayerChanged(SdfLayerHandle layer)
{
    Q_UNUSED(layer);
    outlinerTree()->updateEditLayer();
}

void
OutlinerViewPrivate::maskChanged(const QList<SdfPath>& paths)
{
    outlinerTree()->updateMask(paths);
}

void
OutlinerViewPrivate::primsChanged(const NoticeBatch& batch)
{
    PerfScope perfScope("ui.outliner_prims");
    outlinerTree()->updatePrims(batch);

    const SelectionList* list = d.context->selectionList();
    const QList<SdfPath> paths = list ? list->paths() : QList<SdfPath>();

    if (paths.size() == 1)
        updateDepth(paths.first());
    else
        updateDepth();
}

void
OutlinerViewPrivate::selectionChanged(const QList<SdfPath>& paths)
{
    SignalGuard::Scope guard(this);

    if (paths.size() == 1)
        updateDepth(paths.first());
    else
        updateDepth();

    if (!paths.isEmpty() && d.followEnabled)
        outlinerTree()->reveal(paths.first());
}

void
OutlinerViewPrivate::stageChanged(UsdStageRefPtr stage, Session::LoadPolicy policy, Session::StageStatus status)
{
    PerfScope perfScope("ui.outliner_stage");
    const bool loaded = (status == Session::StageStatus::Loaded);

    d.ui->filter->setEnabled(loaded);
    d.ui->depth->setEnabled(loaded);
    d.ui->follow->setEnabled(loaded);
    d.ui->display->setEnabled(loaded);
    updateDisplayButton();

    if (loaded) {
        outlinerTree()->setDisplayFilters({}, {});
        outlinerTree()->setPayloadEnabled(policy == Session::LoadPolicy::None);
        outlinerTree()->updateStage(stage);
        updateDepth();
        return;
    }

    outlinerTree()->close();
    d.ui->clear->setEnabled(false);
    clearFilter();
    clearDepth();
}

void
OutlinerViewPrivate::depthChanged(int value)
{
    const SelectionList* list = d.context->selectionList();
    const QList<SdfPath> paths = list ? list->paths() : QList<SdfPath>();

    if (paths.size() == 1)
        outlinerTree()->expandDepth(value, paths.first());
    else
        outlinerTree()->expandDepth(value);

    d.ui->collapse->setEnabled(value > d.ui->depth->minimum());
    d.ui->expand->setEnabled(value < d.ui->depth->maximum());
}

void
OutlinerViewPrivate::updateDepth(const SdfPath& path)
{
    SignalGuard::Scope guard(this);

    d.ui->depth->setEnabled(true);
    d.ui->depth->setMinimum(0);
    const int maximum = outlinerTree()->maxDepth(path);

    d.ui->depth->setMaximum(maximum);
    d.ui->depth->setValue(outlinerTree()->depth(path));

    const int value = d.ui->depth->value();
    d.ui->collapse->setEnabled(value > d.ui->depth->minimum());
    d.ui->expand->setEnabled(value < d.ui->depth->maximum());
}

OutlinerView::OutlinerView(QWidget* parent)
    : QWidget(parent)
    , p(new OutlinerViewPrivate())
{
    p->d.view = this;
    p->init();
}

OutlinerView::~OutlinerView() = default;

bool
OutlinerView::followEnabled()
{
    return p->d.followEnabled;
}

void
OutlinerView::enableFollow(bool enable)
{
    p->follow(enable);
}

}  // namespace stageviz
