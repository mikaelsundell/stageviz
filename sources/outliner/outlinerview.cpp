// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#include "outlinerview.h"
#include "application.h"
#include "notice.h"
#include "outlinertree.h"
#include "primitem.h"
#include "signalguard.h"
#include "style.h"
#include "viewcontext.h"
#include <QPointer>
#include <QSignalBlocker>
#include <QTimer>
#include <pxr/usd/usd/prim.h>

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
    void filterChanged(const QString& filter);
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

    attach(d.ui->depth);
    outlinerTree()->setColumnCount(3);
    outlinerTree()->setHeaderLabels(QStringList() << "Name"
                                                  << ""
                                                  << "");
    outlinerTree()->setContext(d.context.data());
    outlinerTree()->installEventFilter(this);

    d.ui->clear->setIcon(style()->icon(Style::IconRole::Clear));
    d.ui->follow->setIcon(style()->icon(Style::IconRole::Select));
    d.ui->collapse->setIcon(style()->icon(Style::IconRole::List));
    d.ui->expand->setIcon(style()->icon(Style::IconRole::Nested));
    // connect
    connect(d.ui->filter, &QLineEdit::textChanged, this, &OutlinerViewPrivate::filterChanged);
    connect(d.ui->clear, &QToolButton::clicked, this, &OutlinerViewPrivate::clearFilter);
    connect(d.ui->collapse, &QToolButton::clicked, this, &OutlinerViewPrivate::collapse);
    connect(d.ui->expand, &QToolButton::clicked, this, &OutlinerViewPrivate::expand);
    connect(d.ui->follow, &QToolButton::toggled, this, &OutlinerViewPrivate::follow);
    connect(d.ui->depth, &QSlider::valueChanged, this, &OutlinerViewPrivate::depthChanged);
    connect(session(), &Session::editLayerChanged, this, &OutlinerViewPrivate::editLayerChanged);
    connect(session(), &Session::maskChanged, this, &OutlinerViewPrivate::maskChanged);
    connect(session(), &Session::primsChanged, this, &OutlinerViewPrivate::primsChanged);
    connect(session(), &Session::stageChanged, this, &OutlinerViewPrivate::stageChanged);
    connect(session()->selectionList(), &SelectionList::selectionChanged, this, &OutlinerViewPrivate::selectionChanged);
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
OutlinerViewPrivate::filterChanged(const QString& filter)
{
    outlinerTree()->setFilter(filter);
    d.ui->clear->setEnabled(!filter.isEmpty());
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
    const bool loaded = (status == Session::StageStatus::Loaded);

    d.ui->filter->setEnabled(loaded);
    d.ui->depth->setEnabled(loaded);
    d.ui->follow->setEnabled(loaded);

    if (loaded) {
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
