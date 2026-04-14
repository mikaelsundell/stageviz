// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#include "progressview.h"
#include "application.h"
#include "qtutils.h"
#include "selectionlist.h"
#include "session.h"
#include "style.h"
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QPointer>
#include <QTreeWidgetItem>
#include <pxr/usd/ar/resolver.h>
#include <pxr/usd/usd/primCompositionQuery.h>

// generated files
#include "ui_progressview.h"

namespace stageviz {

namespace {
    constexpr int kNotifyPathsRole = Qt::UserRole + 1;
    constexpr int kNotifyMessageRole = Qt::UserRole + 2;
}  // namespace

class ProgressViewPrivate : public QObject {
public:
    void init();
    QTreeWidget* progressTree();
    bool eventFilter(QObject* obj, QEvent* event);
    void trimHistory();
    QString pathLabel(const Session::Notify& notify) const;
    QString statusLabel(const Session::Notify& notify) const;
    QStringList pathStrings(const QList<SdfPath>& paths) const;
    QList<SdfPath> itemPaths(QTreeWidgetItem* item) const;

public Q_SLOTS:
    void cancel();
    void clear();
    void progressBlockChanged(const QString& name, Session::ProgressMode mode);
    void progressNotifyChanged(const Session::Notify& notify, size_t completed, size_t expected);
    void progressItemSelectionChanged();
    void maskChanged(const QList<SdfPath>& paths);
    void selectionChanged(const QList<SdfPath>& paths);
    void stageChanged(UsdStageRefPtr stage, Session::LoadPolicy policy, Session::StageStatus status);

public:
    QString updateStatus(size_t completed, size_t expected);

    struct Data {
        UsdStageRefPtr stage;
        int expectedCount = 0;
        int history = 100;
        bool running = false;
        QString currentName;
        QTreeWidgetItem* currentItem = nullptr;
        QElapsedTimer timer;
        QScopedPointer<Ui_ProgressView> ui;
        QPointer<ProgressView> view;
    };
    Data d;
};

void
ProgressViewPrivate::init()
{
    d.ui.reset(new Ui_ProgressView());
    d.ui->setupUi(d.view.data());
    progressTree()->installEventFilter(this);
    progressTree()->setHeaderLabels(QStringList() << "Command"
                                                  << "Status");
    progressTree()->setIndentation(12);
    progressTree()->setRootIsDecorated(true);
    progressTree()->setItemsExpandable(true);
    progressTree()->setExpandsOnDoubleClick(true);
    progressTree()->setSelectionMode(QAbstractItemView::SingleSelection);
    d.ui->progress->setValue(0);
    d.ui->status->setText("Idle");
    d.ui->clear->setEnabled(false);
    d.ui->cancel->setEnabled(false);
    // connect
    connect(d.ui->clear, &QPushButton::clicked, this, &ProgressViewPrivate::clear);
    connect(d.ui->cancel, &QPushButton::clicked, this, &ProgressViewPrivate::cancel);
    connect(progressTree(), &QTreeWidget::itemSelectionChanged, this,
            &ProgressViewPrivate::progressItemSelectionChanged);
    connect(session(), &Session::progressBlockChanged, this, &ProgressViewPrivate::progressBlockChanged);
    connect(session(), &Session::progressNotifyChanged, this, &ProgressViewPrivate::progressNotifyChanged);
    connect(session(), &Session::stageChanged, this, &ProgressViewPrivate::stageChanged);
    connect(session()->selectionList(), &SelectionList::selectionChanged, this, &ProgressViewPrivate::selectionChanged);
}

QTreeWidget*
ProgressViewPrivate::progressTree()
{
    return d.ui->progressTree;
}

bool
ProgressViewPrivate::eventFilter(QObject* obj, QEvent* event)
{
    if (event->type() == QEvent::Show) {
        if (auto* tree = qobject_cast<QTreeWidget*>(obj)) {
            if (tree == progressTree()) {
                auto* header = tree->header();
                header->setStretchLastSection(false);
                header->setSectionResizeMode(0, QHeaderView::Stretch);
                header->setSectionResizeMode(1, QHeaderView::Fixed);
                tree->setColumnWidth(1, 80);
            }
        }
    }
    return QObject::eventFilter(obj, event);
}

void
ProgressViewPrivate::trimHistory()
{
    QTreeWidget* tree = progressTree();
    while (tree->topLevelItemCount() > d.history)
        delete tree->takeTopLevelItem(tree->topLevelItemCount() - 1);
}

QString
ProgressViewPrivate::pathLabel(const Session::Notify& notify) const
{
    if (notify.paths.isEmpty())
        return "No paths";

    const SdfPath& firstPath = notify.paths.first();
    QString first = StringToQString(firstPath.GetName());
    if (first.isEmpty())
        first = StringToQString(firstPath.GetString());

    const qsizetype count = notify.paths.size();
    return QString("%1 (%2)").arg(first).arg(count);
}

QString
ProgressViewPrivate::statusLabel(const Session::Notify& notify) const
{
    switch (notify.status) {
    case Session::Notify::Status::Error: return "Failed";
    case Session::Notify::Status::Warning: return "Warning";
    case Session::Notify::Status::Progress: return "Running";
    case Session::Notify::Status::Info:
    default: break;
    }

    return "Success";
}

QStringList
ProgressViewPrivate::pathStrings(const QList<SdfPath>& paths) const
{
    QStringList values;
    values.reserve(paths.size());

    for (const SdfPath& path : paths)
        values.append(StringToQString(path.GetString()));

    return values;
}

QList<SdfPath>
ProgressViewPrivate::itemPaths(QTreeWidgetItem* item) const
{
    QList<SdfPath> paths;
    if (!item)
        return paths;

    const QStringList values = item->data(0, kNotifyPathsRole).toStringList();
    paths.reserve(values.size());

    for (const QString& value : values) {
        if (!value.isEmpty())
            paths.append(SdfPath(QStringToString(value)));
    }

    return paths;
}

void
ProgressViewPrivate::cancel()
{
    session()->cancelProgressBlock();
}

void
ProgressViewPrivate::clear()
{
    progressTree()->clear();
    d.currentItem = nullptr;
    d.currentName.clear();
    d.expectedCount = 0;
    d.ui->progress->setValue(0);
    if (!d.running)
        d.ui->status->setText("Idle");
    d.ui->clear->setEnabled(false);
}

void
ProgressViewPrivate::progressBlockChanged(const QString& name, Session::ProgressMode mode)
{
    if (mode == Session::ProgressMode::Running) {
        d.ui->progress->setValue(0);
        d.timer.restart();
        d.running = true;
        d.expectedCount = 0;
        d.currentName = name;

        auto* commandItem = new QTreeWidgetItem();
        commandItem->setText(0, QString("%1 (0)").arg(name));
        commandItem->setText(1, "Running...");
        commandItem->setExpanded(false);
        commandItem->setData(0, kNotifyPathsRole, QStringList());

        progressTree()->insertTopLevelItem(0, commandItem);
        d.currentItem = commandItem;

        trimHistory();

        d.ui->status->setText(QString("Running: %1").arg(name));
        d.ui->cancel->setEnabled(true);
        d.ui->clear->setEnabled(progressTree()->topLevelItemCount() > 0);
        return;
    }

    d.running = false;

    qint64 ms = d.timer.elapsed();
    QString timeStr = QTime(0, 0).addMSecs(static_cast<int>(ms)).toString("hh:mm:ss");

    if (d.currentItem) {
        const int childCount = d.currentItem->childCount();
        d.currentItem->setText(0, QString("%1 (%2)").arg(d.currentName).arg(childCount));
        d.currentItem->setText(1, QString("Finished (%1)").arg(timeStr));
        d.currentItem->setExpanded(false);
    }

    d.ui->status->setText(QString("Finished: %1 (Time: %2)").arg(name).arg(timeStr));
    d.ui->progress->setValue(0);
    d.ui->cancel->setEnabled(false);
    d.ui->clear->setEnabled(progressTree()->topLevelItemCount() > 0);

    d.currentItem = nullptr;
    d.currentName.clear();
}

void
ProgressViewPrivate::progressNotifyChanged(const Session::Notify& notify, size_t completed, size_t expected)
{
    if (!d.currentItem)
        return;

    if (d.expectedCount == 0)
        d.expectedCount = static_cast<int>(expected);

    const int index = static_cast<int>(completed) - 1;
    if (index < 0)
        return;

    while (d.currentItem->childCount() <= index) {
        auto* child = new QTreeWidgetItem();
        child->setText(0, "Pending...");
        child->setText(1, QString());
        child->setData(0, kNotifyPathsRole, QStringList());
        d.currentItem->addChild(child);
    }

    d.currentItem->setText(0, QString("%1 (%2)").arg(d.currentName).arg(d.currentItem->childCount()));

    QTreeWidgetItem* item = d.currentItem->child(index);
    if (!item)
        return;

    item->setText(0, statusLabel(notify));
    item->setText(1, pathLabel(notify));
    item->setData(0, kNotifyPathsRole, pathStrings(notify.paths));
    item->setData(0, kNotifyMessageRole, notify.message);

    item->setForeground(0, QBrush());
    item->setForeground(1, QBrush());
    item->setIcon(0, QIcon());

    switch (notify.status) {
    case Session::Notify::Status::Error:
        item->setForeground(0, style()->color(Style::ColorRole::Error));
        item->setForeground(1, style()->color(Style::ColorRole::Error));
        break;
    case Session::Notify::Status::Warning:
        item->setForeground(0, style()->color(Style::ColorRole::Warning));
        item->setForeground(1, style()->color(Style::ColorRole::Warning));
        break;
    case Session::Notify::Status::Progress:
        item->setForeground(0, style()->color(Style::ColorRole::Progress));
        item->setForeground(1, style()->color(Style::ColorRole::Progress));
        break;
    case Session::Notify::Status::Info:
    default: break;
    }

    const int pct = int((double(completed) / std::max<size_t>(1, expected)) * 100.0);
    d.ui->progress->setValue(pct);
    d.ui->status->setText(updateStatus(completed, expected));
    d.ui->clear->setEnabled(progressTree()->topLevelItemCount() > 0);
}

void
ProgressViewPrivate::progressItemSelectionChanged()
{
    QTreeWidgetItem* item = progressTree()->currentItem();
    if (!item)
        return;

    if (!item->parent())
        return;

    const QList<SdfPath> paths = itemPaths(item);
    if (paths.isEmpty())
        return;

    session()->selectionList()->updatePaths(paths);
}

void
ProgressViewPrivate::stageChanged(UsdStageRefPtr stage, Session::LoadPolicy policy, Session::StageStatus status)
{
    Q_UNUSED(policy);
    Q_UNUSED(status);

    d.stage = stage;
    d.running = false;
    d.expectedCount = 0;
    d.currentItem = nullptr;
    d.currentName.clear();
    d.timer.invalidate();

    progressTree()->clear();
    d.ui->progress->setValue(0);
    d.ui->cancel->setEnabled(false);
    d.ui->clear->setEnabled(false);
    d.ui->status->setText("Idle");
}

void
ProgressViewPrivate::selectionChanged(const QList<SdfPath>& paths)
{
    Q_UNUSED(paths);
}

QString
ProgressViewPrivate::updateStatus(size_t completed, size_t expected)
{
    qint64 ms = d.timer.elapsed();
    QString timeStr = QTime(0, 0).addMSecs(static_cast<int>(ms)).toString("hh:mm:ss");
    return QString("Time: %1 (%2/%3)").arg(timeStr).arg(completed).arg(expected);
}

ProgressView::ProgressView(QWidget* parent)
    : QWidget(parent)
    , p(new ProgressViewPrivate())
{
    p->d.view = this;
    p->init();
}

ProgressView::~ProgressView() = default;

}  // namespace stageviz
