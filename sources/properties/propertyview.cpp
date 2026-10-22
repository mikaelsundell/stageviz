// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#include "propertyview.h"
#include "application.h"
#include "notice.h"
#include "propertyitem.h"
#include "propertytree.h"
#include "selectionlist.h"
#include "session.h"
#include "style.h"
#include "viewcontext.h"
#include <QHeaderView>
#include <QPointer>
#include <QSizePolicy>
#include <QTreeWidgetItem>
#include <functional>

// generated files
#include "ui_propertyview.h"

namespace stageviz {

class PropertyViewPrivate : public QObject {
public:
    void init();

public Q_SLOTS:
    void clearFilter();
    void filterChanged(const QString& filter);
    void updatePrims(const NoticeBatch& batch);
    void updateSelection(const QList<SdfPath>& paths);
    void updateStage(UsdStageRefPtr stage, Session::LoadPolicy policy, Session::StageStatus status);

public:
    void updateFilter();

    struct Data {
        QString filter;
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
    d.ui->verticalLayout->setStretch(1, 1);

    d.context.reset(new ViewContext(d.view.data()));
    d.context->setStageLock(session()->stageLock());
    d.context->setCommandStack(session()->commandStack());

    d.ui->propertyTree->setHeaderLabels(QStringList() << "Name"
                                                      << "Value");
    d.ui->propertyTree->setContext(d.context.data());
    d.ui->propertyTree->setColumnWidth(0, 200);
    d.ui->propertyTree->header()->setSectionResizeMode(1, QHeaderView::Stretch);

    d.ui->clear->setIcon(style()->icon(Style::IconRole::Clear));

    connect(d.ui->filter, &QLineEdit::textChanged, this, &PropertyViewPrivate::filterChanged);
    connect(d.ui->clear, &QToolButton::clicked, this, &PropertyViewPrivate::clearFilter);
    connect(session(), &Session::primsChanged, this, &PropertyViewPrivate::updatePrims);
    connect(session(), &Session::stageChanged, this, &PropertyViewPrivate::updateStage);
    connect(session()->selectionList(), &SelectionList::selectionChanged, this, &PropertyViewPrivate::updateSelection);

    const bool loaded = session()->isLoaded();
    d.ui->filter->setEnabled(loaded);

    if (loaded) {
        d.ui->propertyTree->updateStage(session()->stage());

        const QList<SdfPath> paths = session()->selectionList()->paths();
        if (!paths.isEmpty())
            d.ui->propertyTree->updateSelection(paths);
    }
}

void
PropertyViewPrivate::clearFilter()
{
    d.ui->filter->clear();
}

void
PropertyViewPrivate::filterChanged(const QString& filter)
{
    d.filter = filter;
    d.ui->clear->setEnabled(!filter.isEmpty());
    updateFilter();
}

void
PropertyViewPrivate::updateFilter()
{
    if (!d.ui || !d.ui->propertyTree)
        return;

    const QString filter = d.filter.trimmed();

    std::function<bool(QTreeWidgetItem*, bool)> filterItem = [&](QTreeWidgetItem* item,
                                                                  bool keepValueDescendants) -> bool {
        if (!item)
            return false;

        bool matches = filter.isEmpty();
        if (!matches) {
            for (int column = 0; column < d.ui->propertyTree->columnCount(); ++column) {
                if (item->text(column).contains(filter, Qt::CaseInsensitive)) {
                    matches = true;
                    break;
                }
            }
        }

        const auto* propertyItem = dynamic_cast<PropertyItem*>(item);
        const bool keepChildren = keepValueDescendants || (matches && propertyItem
                                                            && propertyItem->kind() != PropertyItem::Group);

        bool childMatches = false;
        for (int i = 0; i < item->childCount(); ++i) {
            if (filterItem(item->child(i), keepChildren))
                childMatches = true;
        }

        const bool visible = keepValueDescendants || matches || childMatches;
        item->setHidden(!visible);
        return visible;
    };

    for (int i = 0; i < d.ui->propertyTree->topLevelItemCount(); ++i)
        filterItem(d.ui->propertyTree->topLevelItem(i), false);
}

void
PropertyViewPrivate::updatePrims(const NoticeBatch& batch)
{
    d.ui->propertyTree->updatePrims(batch);
    if (!d.filter.trimmed().isEmpty())
        updateFilter();
}

void
PropertyViewPrivate::updateSelection(const QList<SdfPath>& paths)
{
    d.ui->propertyTree->updateSelection(paths);
    if (!d.filter.trimmed().isEmpty())
        updateFilter();
}

void
PropertyViewPrivate::updateStage(UsdStageRefPtr stage, Session::LoadPolicy policy, Session::StageStatus status)
{
    Q_UNUSED(policy);

    const bool loaded = status == Session::StageStatus::Loaded && stage;
    d.ui->filter->setEnabled(loaded);

    if (!loaded) {
        d.ui->propertyTree->close();
        d.ui->clear->setEnabled(false);
        d.ui->filter->clear();
        return;
    }

    d.ui->propertyTree->updateStage(stage);

    const QList<SdfPath> paths = session()->selectionList()->paths();
    if (!paths.isEmpty())
        d.ui->propertyTree->updateSelection(paths);

    updateFilter();
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
