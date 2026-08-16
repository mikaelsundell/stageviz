// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#include "propertyview.h"
#include "application.h"
#include "notice.h"
#include "propertytree.h"
#include "selectionlist.h"
#include "session.h"
#include "viewcontext.h"
#include <QHeaderView>
#include <QPointer>
#include <QSizePolicy>

// generated files
#include "ui_propertyview.h"

namespace stageviz {

class PropertyViewPrivate : public QObject {
public:
    void init();

public Q_SLOTS:
    void updatePrims(const NoticeBatch& batch);
    void updateSelection(const QList<SdfPath>& paths);
    void updateStage(UsdStageRefPtr stage, Session::LoadPolicy policy, Session::StageStatus status);

public:
    struct Data {
        QScopedPointer<ViewContext> context;
        QScopedPointer<Ui_PropertyView> ui;
        QPointer<PropertyView> view;
    };
    Data d;
};

void
PropertyViewPrivate::init()
{
    d.ui.reset(new Ui_PropertyView());
    d.ui->setupUi(d.view.data());

    d.view->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    d.ui->propertyTree->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    d.ui->verticalLayout->setStretch(0, 1);

    d.context.reset(new ViewContext(d.view.data()));
    d.context->setStageLock(session()->stageLock());
    d.context->setCommandStack(session()->commandStack());

    d.ui->propertyTree->setHeaderLabels(QStringList() << "Name"
                                                      << "Value");
    d.ui->propertyTree->setContext(d.context.data());
    d.ui->propertyTree->setColumnWidth(0, 200);
    d.ui->propertyTree->header()->setSectionResizeMode(1, QHeaderView::Stretch);

    connect(session(), &Session::primsChanged, this, &PropertyViewPrivate::updatePrims);
    connect(session(), &Session::stageChanged, this, &PropertyViewPrivate::updateStage);
    connect(session()->selectionList(), &SelectionList::selectionChanged, this, &PropertyViewPrivate::updateSelection);

    if (session()->isLoaded()) {
        d.ui->propertyTree->updateStage(session()->stage());

        const QList<SdfPath> paths = session()->selectionList()->paths();
        if (!paths.isEmpty())
            d.ui->propertyTree->updateSelection(paths);
    }
}

void
PropertyViewPrivate::updatePrims(const NoticeBatch& batch)
{
    d.ui->propertyTree->updatePrims(batch);
}

void
PropertyViewPrivate::updateSelection(const QList<SdfPath>& paths)
{
    d.ui->propertyTree->updateSelection(paths);
}

void
PropertyViewPrivate::updateStage(UsdStageRefPtr stage, Session::LoadPolicy policy, Session::StageStatus status)
{
    Q_UNUSED(policy);

    if (status != Session::StageStatus::Loaded || !stage) {
        d.ui->propertyTree->close();
        return;
    }

    d.ui->propertyTree->updateStage(stage);

    const QList<SdfPath> paths = session()->selectionList()->paths();
    if (!paths.isEmpty())
        d.ui->propertyTree->updateSelection(paths);
}

PropertyView::PropertyView(QWidget* parent)
    : QWidget(parent)
    , p(new PropertyViewPrivate())
{
    p->d.view = this;
    p->init();
}

PropertyView::~PropertyView() = default;

}  // namespace stageviz
