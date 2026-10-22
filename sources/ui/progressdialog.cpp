// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#include "progressdialog.h"
#include "application.h"
#include "qtutils.h"
#include "selectionlist.h"
#include "session.h"
#include "style.h"
#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QMenu>
#include <QPointer>
#include <QTreeWidgetItem>
#include <algorithm>
#include <pxr/usd/ar/resolver.h>
#include <pxr/usd/usd/primCompositionQuery.h>

// generated files
#include "ui_progressdialog.h"

namespace stageviz {

namespace {

    struct ProgressRow {
        QString message;
        QList<SdfPath> paths;
    };

    QString messageForPath(const ProgressRow& row, const SdfPath& path)
    {
        const QString pathString = StringToQString(path.GetString());
        if (row.message.contains(pathString))
            return row.message;
        if (row.message.isEmpty())
            return pathString;
        return QStringLiteral("%1: %2").arg(row.message, pathString);
    }

}  // namespace

class ProgressDialogPrivate : public QObject {
public:
    void init();
    QTreeWidget* progressTree();
    bool eventFilter(QObject* obj, QEvent* event);
    void trim();
    QString statusLabel(const Session::Notify::Status status) const;
    QString blockToolTip(const QString& name, int childCount) const;
    QString notifyToolTip(const Session::Notify& notify) const;
    QStringList pathStrings(const QList<SdfPath>& paths) const;
    QList<SdfPath> itemPaths(QTreeWidgetItem* item) const;

public Q_SLOTS:
    void cancel();
    void clear();
    void progressBlockChanged(const QString& name, Session::ProgressMode mode);
    void progressNotifyChanged(const Session::Notify& notify, size_t completed, size_t expected);
    void maskChanged(const QList<SdfPath>& paths);
    void selectionChanged(const QList<SdfPath>& paths);
    void stageChanged(UsdStageRefPtr stage, Session::LoadPolicy policy, Session::StageStatus status);

public:
    QString updateStatus(size_t completed, size_t expected);

    struct Data {
        UsdStageRefPtr stage;
        int expectedCount = 0;
        int history = 100;
        int errorCount = 0;
        int warningCount = 0;
        bool running = false;
        QString currentName;
        QTreeWidgetItem* currentItem = nullptr;
        QElapsedTimer timer;
        QScopedPointer<Ui_ProgressDialog> ui;
        QPointer<ProgressDialog> dialog;
    };
    Data d;
};

void
ProgressDialogPrivate::init()
{
    d.ui.reset(new Ui_ProgressDialog());
    d.ui->setupUi(d.dialog.data());
    progressTree()->installEventFilter(this);
    progressTree()->setContextMenuPolicy(Qt::CustomContextMenu);
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
    connect(d.ui->clear, &QPushButton::clicked, this, &ProgressDialogPrivate::clear);
    connect(d.ui->cancel, &QPushButton::clicked, this, &ProgressDialogPrivate::cancel);
    connect(progressTree(), &QTreeWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
        QTreeWidgetItem* item = progressTree()->itemAt(pos);
        const QList<SdfPath> paths = itemPaths(item);
        if (paths.isEmpty())
            return;

        QMenu menu(progressTree());
        QMenu* copyMenu = menu.addMenu(QStringLiteral("Copy"));
        QAction* copyPath = copyMenu->addAction(QStringLiteral("Path"));
        connect(copyPath, &QAction::triggered, this, [paths]() {
            QStringList values;
            values.reserve(paths.size());
            for (const SdfPath& path : paths)
                values.append(StringToQString(path.GetString()));
            QApplication::clipboard()->setText(values.join('\n'));
        });
        menu.exec(progressTree()->viewport()->mapToGlobal(pos));
    });
    connect(session(), &Session::progressBlockChanged, this, &ProgressDialogPrivate::progressBlockChanged);
    connect(session(), &Session::progressNotifyChanged, this, &ProgressDialogPrivate::progressNotifyChanged);
    connect(session(), &Session::stageChanged, this, &ProgressDialogPrivate::stageChanged);
    connect(session()->selectionList(), &SelectionList::selectionChanged, this,
            &ProgressDialogPrivate::selectionChanged);
}

QTreeWidget*
ProgressDialogPrivate::progressTree()
{
    return d.ui->progressTree;
}

bool
ProgressDialogPrivate::eventFilter(QObject* obj, QEvent* event)
{
    if (event->type() == QEvent::Show) {
        if (auto* tree = qobject_cast<QTreeWidget*>(obj)) {
            if (tree == progressTree()) {
                auto* header = tree->header();
                header->setStretchLastSection(false);
                header->setSectionResizeMode(0, QHeaderView::Stretch);
                header->setSectionResizeMode(1, QHeaderView::Fixed);
                tree->setColumnWidth(1, 140);
            }
        }
    }
    return QObject::eventFilter(obj, event);
}

void
ProgressDialogPrivate::trim()
{
    QTreeWidget* tree = progressTree();
    while (tree->topLevelItemCount() > d.history)
        delete tree->takeTopLevelItem(tree->topLevelItemCount() - 1);
}

QString
ProgressDialogPrivate::statusLabel(const Session::Notify::Status status) const
{
    switch (status) {
    case Session::Notify::Status::Success: return "Success";
    case Session::Notify::Status::Progress: return "Running";
    case Session::Notify::Status::Warning: return "Warning";
    case Session::Notify::Status::Error: return "Failed";
    }

    return QString();
}

QString
ProgressDialogPrivate::blockToolTip(const QString& name, int childCount) const
{
    return QString("%1\n%2 completed update(s) in this command block.").arg(name).arg(childCount);
}

QString
ProgressDialogPrivate::notifyToolTip(const Session::Notify& notify) const
{
    QStringList lines;
    lines << notify.message;
    lines << QString("%1 path(s)").arg(notify.paths.size());

    for (const QString& path : pathStrings(notify.paths))
        lines << path;

    return lines.join('\n');
}

QStringList
ProgressDialogPrivate::pathStrings(const QList<SdfPath>& paths) const
{
    QStringList values;
    values.reserve(paths.size());

    for (const SdfPath& path : paths)
        values.append(StringToQString(path.GetString()));

    return values;
}

QList<SdfPath>
ProgressDialogPrivate::itemPaths(QTreeWidgetItem* item) const
{
    QList<SdfPath> paths;
    if (!item)
        return paths;

    const QStringList values = item->data(0, Qt::UserRole).toStringList();
    paths.reserve(values.size());

    for (const QString& value : values) {
        if (!value.isEmpty())
            paths.append(SdfPath(QStringToString(value)));
    }
    return paths;
}

void
ProgressDialogPrivate::cancel()
{
    session()->cancelProgressBlock();
}

void
ProgressDialogPrivate::clear()
{
    progressTree()->clear();
    d.currentItem = nullptr;
    d.currentName.clear();
    d.expectedCount = 0;
    d.errorCount = 0;
    d.warningCount = 0;
    d.ui->progress->setValue(0);
    if (!d.running)
        d.ui->status->setText("Idle");
    d.ui->clear->setEnabled(false);
}

void
ProgressDialogPrivate::progressBlockChanged(const QString& name, Session::ProgressMode mode)
{
    if (mode == Session::ProgressMode::Running) {
        d.ui->progress->setValue(0);
        d.timer.restart();
        d.running = true;
        d.expectedCount = 0;
        d.errorCount = 0;
        d.warningCount = 0;
        d.currentName = name;

        auto* commandItem = new QTreeWidgetItem();
        commandItem->setText(0, QString("%1 (0)").arg(name));
        commandItem->setText(1, "Running...");
        commandItem->setToolTip(0, QString("%1\nCommand block with 0 completed update(s).").arg(name));
        commandItem->setToolTip(1, "Command block is running.");
        commandItem->setExpanded(false);
        commandItem->setData(0, Qt::UserRole, QStringList());

        progressTree()->insertTopLevelItem(0, commandItem);
        d.currentItem = commandItem;

        trim();

        d.ui->status->setText(QString("Running: %1").arg(name));
        d.ui->cancel->setEnabled(true);
        d.ui->clear->setEnabled(progressTree()->topLevelItemCount() > 0);
        return;
    }

    d.running = false;

    const qint64 ms = d.timer.elapsed();
    const QString timeStr = QTime(0, 0).addMSecs(static_cast<int>(ms)).toString("hh:mm:ss");
    const int commandCount = d.currentItem ? d.currentItem->childCount() : 0;
    const QString commandLabel = QStringLiteral("%1 (%2)").arg(name).arg(commandCount);

    QStringList reportDetails;

    if (d.currentItem) {
        const int childCount = d.currentItem->childCount();
        QList<ProgressRow> errors;
        QList<ProgressRow> warnings;

        for (int i = 0; i < childCount; ++i) {
            QTreeWidgetItem* child = d.currentItem->child(i);
            if (!child)
                continue;

            ProgressRow row;
            row.message = child->toolTip(0).section('\n', 0, 0);
            row.paths = itemPaths(child);
            if (child->text(1) == statusLabel(Session::Notify::Status::Error)) {
                errors.append(row);
            }
            else if (child->text(1) == statusLabel(Session::Notify::Status::Warning)) {
                warnings.append(row);
            }
        }

        const QList<QTreeWidgetItem*> oldChildren = d.currentItem->takeChildren();
        for (QTreeWidgetItem* child : oldChildren)
            delete child;

        const auto setStatusColor = [this](QTreeWidgetItem* item, Session::Notify::Status status) {
            const Style::ColorRole role = status == Session::Notify::Status::Error     ? Style::ColorRole::Error
                                          : status == Session::Notify::Status::Warning ? Style::ColorRole::Warning
                                                                                       : Style::ColorRole::Progress;
            const QBrush brush = status == Session::Notify::Status::Success ? QBrush() : QBrush(style()->color(role));
            item->setBackground(0, brush);
            item->setBackground(1, brush);
        };

        const auto appendProblemRows = [this, &reportDetails, &setStatusColor](const QList<ProgressRow>& rows,
                                                                               Session::Notify::Status status) {
            if (rows.isEmpty())
                return;

            for (const ProgressRow& row : rows) {
                if (row.paths.isEmpty()) {
                    reportDetails.append(row.message);
                    auto* pathItem = new QTreeWidgetItem(d.currentItem);
                    pathItem->setText(0, row.message);
                    pathItem->setText(1, statusLabel(status));
                    pathItem->setToolTip(0, row.message);
                    setStatusColor(pathItem, status);
                    continue;
                }

                for (const SdfPath& path : row.paths) {
                    const QString pathString = StringToQString(path.GetString());
                    const QString detail = messageForPath(row, path);
                    reportDetails.append(detail);

                    auto* pathItem = new QTreeWidgetItem(d.currentItem);
                    pathItem->setText(0, detail);
                    pathItem->setText(1, statusLabel(status));
                    pathItem->setToolTip(0, detail);
                    pathItem->setData(0, Qt::UserRole, QStringList { pathString });
                    setStatusColor(pathItem, status);
                }
            }
        };

        appendProblemRows(errors, Session::Notify::Status::Error);
        appendProblemRows(warnings, Session::Notify::Status::Warning);

        d.currentItem->setText(0, commandLabel);
        d.currentItem->setToolTip(
            0, QString("%1\nCommand block with %2 completed update(s).").arg(d.currentName).arg(childCount));
        d.currentItem->setExpanded(true);

        if (d.errorCount > 0)
            d.currentItem->setText(1, QString("Failed (%1)").arg(timeStr));
        else if (d.warningCount > 0)
            d.currentItem->setText(1, QString("Warning (%1)").arg(timeStr));
        else
            d.currentItem->setText(1, QString("Finished (%1)").arg(timeStr));

        d.currentItem->setToolTip(1, d.currentItem->text(1));
    }

    d.ui->status->setText(QString("Finished: %1 (Time: %2)").arg(commandLabel).arg(timeStr));
    d.ui->progress->setValue(0);
    d.ui->cancel->setEnabled(false);
    d.ui->clear->setEnabled(progressTree()->topLevelItemCount() > 0);

    if (d.errorCount > 0 && d.warningCount > 0) {
        session()->notifyStatus(Session::Notify::Status::Error,
                                QString("%1 finished with %2 error(s) and %3 warning(s)")
                                    .arg(commandLabel)
                                    .arg(d.errorCount)
                                    .arg(d.warningCount),
                                reportDetails.join('\n'));
    }
    else if (d.errorCount > 0) {
        session()->notifyStatus(Session::Notify::Status::Error,
                                QString("%1 finished with %2 error(s)").arg(commandLabel).arg(d.errorCount),
                                reportDetails.join('\n'));
    }
    else if (d.warningCount > 0) {
        session()->notifyStatus(Session::Notify::Status::Warning,
                                QString("%1 finished with %2 warning(s)").arg(commandLabel).arg(d.warningCount),
                                reportDetails.join('\n'));
    }
    else {
        session()->notifyStatus(Session::Notify::Status::Success, QString("%1 finished successfully").arg(commandLabel),
                                reportDetails.join('\n'));
    }

    d.currentItem = nullptr;
    d.currentName.clear();
    d.expectedCount = 0;
    d.errorCount = 0;
    d.warningCount = 0;
}

void
ProgressDialogPrivate::progressNotifyChanged(const Session::Notify& notify, size_t completed, size_t expected)
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
        child->setToolTip(0, "Pending update.");
        child->setToolTip(1, QString());
        child->setData(0, Qt::UserRole, QStringList());
        d.currentItem->addChild(child);
    }

    d.currentItem->setText(0, QString("%1 (%2)").arg(d.currentName).arg(d.currentItem->childCount()));
    d.currentItem->setToolTip(
        0,
        QString("%1\nCommand block with %2 completed update(s).").arg(d.currentName).arg(d.currentItem->childCount()));

    QTreeWidgetItem* item = d.currentItem->child(index);
    if (!item)
        return;

    const QStringList paths = pathStrings(notify.paths);
    QStringList tooltip;
    tooltip << notify.message;
    tooltip << QString("%1 affected path(s).").arg(notify.paths.size());
    tooltip.append(paths);

    item->setText(0, QString("%1 (%2)").arg(notify.message).arg(notify.paths.size()));
    item->setText(1, statusLabel(notify.status));
    item->setToolTip(0, tooltip.join('\n'));
    item->setToolTip(1, statusLabel(notify.status));
    item->setData(0, Qt::UserRole, paths);

    item->setBackground(0, QBrush());
    item->setBackground(1, QBrush());

    switch (notify.status) {
    case Session::Notify::Status::Error:
        d.errorCount += std::max(1, static_cast<int>(notify.paths.size()));
        item->setBackground(0, style()->color(Style::ColorRole::Error));
        item->setBackground(1, style()->color(Style::ColorRole::Error));
        break;
    case Session::Notify::Status::Warning:
        d.warningCount += std::max(1, static_cast<int>(notify.paths.size()));
        item->setBackground(0, style()->color(Style::ColorRole::Warning));
        item->setBackground(1, style()->color(Style::ColorRole::Warning));
        break;
    case Session::Notify::Status::Progress:
        item->setBackground(0, style()->color(Style::ColorRole::Progress));
        item->setBackground(1, style()->color(Style::ColorRole::Progress));
        break;
    case Session::Notify::Status::Success:
    default: break;
    }

    const int pct = int((double(completed) / std::max<size_t>(1, expected)) * 100.0);
    d.ui->progress->setValue(pct);
    d.ui->status->setText(updateStatus(completed, expected));
    d.ui->clear->setEnabled(progressTree()->topLevelItemCount() > 0);
}

void
ProgressDialogPrivate::stageChanged(UsdStageRefPtr stage, Session::LoadPolicy policy, Session::StageStatus status)
{
    Q_UNUSED(policy);
    Q_UNUSED(status);
    d.stage = stage;
    d.running = false;
    d.expectedCount = 0;
    d.errorCount = 0;
    d.warningCount = 0;
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
ProgressDialogPrivate::selectionChanged(const QList<SdfPath>& paths)
{
    Q_UNUSED(paths);
}

QString
ProgressDialogPrivate::updateStatus(size_t completed, size_t expected)
{
    qint64 ms = d.timer.elapsed();
    QString timeStr = QTime(0, 0).addMSecs(static_cast<int>(ms)).toString("hh:mm:ss");
    return QString("Time: %1 (%2/%3)").arg(timeStr).arg(completed).arg(expected);
}

ProgressDialog::ProgressDialog(QWidget* parent)
    : QDialog(parent)
    , p(new ProgressDialogPrivate())
{
    p->d.dialog = this;
    p->init();
}

ProgressDialog::~ProgressDialog() = default;

}  // namespace stageviz
