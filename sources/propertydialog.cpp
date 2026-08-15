// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#include "propertydialog.h"
#include "application.h"
#include "notice.h"
#include "propertytree.h"
#include "selectionlist.h"
#include "session.h"
#include "viewcontext.h"
#include <QHeaderView>
#include <QPointer>

// generated files
#include "ui_propertydialog.h"

namespace stageviz {

class PropertyDialogPrivate : public QObject {
public:
    void init();

public Q_SLOTS:
    void updatePrims(const NoticeBatch& batch);
    void updateSelection(const QList<SdfPath>& paths);
    void updateStage(UsdStageRefPtr stage, Session::LoadPolicy policy, Session::StageStatus status);

public:
    struct Data {
        QScopedPointer<ViewContext> context;
        QScopedPointer<Ui_PropertyDialog> ui;
        QPointer<PropertyDialog> dialog;
    };
    Data d;
};

void
PropertyDialogPrivate::init()
{
    d.ui.reset(new Ui_PropertyDialog());
    d.ui->setupUi(d.dialog.data());

    d.context.reset(new ViewContext(d.dialog.data()));
    d.context->setStageLock(session()->stageLock());
    d.context->setCommandStack(session()->commandStack());

    d.ui->propertyTree->setHeaderLabels(QStringList() << "Name"
                                                      << "Value");
    d.ui->propertyTree->setContext(d.context.data());
    d.ui->propertyTree->setColumnWidth(0, 200);
    d.ui->propertyTree->header()->setSectionResizeMode(1, QHeaderView::Stretch);

    connect(session(), &Session::primsChanged, this, &PropertyDialogPrivate::updatePrims);
    connect(session(), &Session::stageChanged, this, &PropertyDialogPrivate::updateStage);
    connect(session()->selectionList(), &SelectionList::selectionChanged, this,
            &PropertyDialogPrivate::updateSelection);

    if (session()->isLoaded()) {
        d.ui->propertyTree->updateStage(session()->stage());
        const QList<SdfPath> paths = session()->selectionList()->paths();
        if (!paths.isEmpty())
            d.ui->propertyTree->updateSelection(paths);
    }
}

void
PropertyDialogPrivate::updatePrims(const NoticeBatch& batch)
{
    d.ui->propertyTree->updatePrims(batch);
}

void
PropertyDialogPrivate::updateSelection(const QList<SdfPath>& paths)
{
    d.ui->propertyTree->updateSelection(paths);
}

void
PropertyDialogPrivate::updateStage(UsdStageRefPtr stage, Session::LoadPolicy policy, Session::StageStatus status)
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

PropertyDialog::PropertyDialog(QWidget* parent)
    : QDialog(parent)
    , p(new PropertyDialogPrivate())
{
    p->d.dialog = this;
    p->init();
}

PropertyDialog::~PropertyDialog() = default;

}  // namespace stageviz
