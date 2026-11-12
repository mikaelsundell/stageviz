// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 - present Mikael Sundell

#include "changesdialog.h"
#include "application.h"
#include "os.h"
#include "session.h"
#include "ui.h"
#include <QFileInfo>
#include <QHash>
#include <QHeaderView>
#include <QMenu>
#include <QPointer>
#include <algorithm>

#include "ui_changesdialog.h"

namespace stageviz {

class ChangesDialogPrivate {
public:
    explicit ChangesDialogPrivate(ChangesDialog* dialog)
    {
        d.dialog = dialog;
        init();
    }

    void init()
    {
        d.ui.reset(new Ui_ChangesDialog());
        d.ui->setupUi(d.dialog);
        auto* header = d.ui->changesTree->header();
        header->setStretchLastSection(true);
        header->setSectionsMovable(false);
        for (int column = 0; column < 5; ++column)
            header->setSectionResizeMode(column, QHeaderView::Interactive);
        d.ui->changesTree->setColumnWidth(0, 170);
        d.ui->changesTree->setColumnWidth(1, 90);
        d.ui->changesTree->setColumnWidth(2, 130);
        d.ui->changesTree->setColumnWidth(3, 140);
        d.ui->changesTree->setColumnWidth(4, 250);
        d.ui->changesTree->setContextMenuPolicy(Qt::CustomContextMenu);
        QObject::connect(d.ui->changesTree, &QTreeWidget::customContextMenuRequested, d.dialog,
                         [this](const QPoint& position) { contextMenu(position); });
        QObject::connect(d.ui->saveAllButton, &QPushButton::clicked, d.dialog, [this]() {
            d.result = ChangesDialog::Result::Save;
            d.dialog->accept();
        });
        QObject::connect(d.ui->discardButton, &QPushButton::clicked, d.dialog, [this]() {
            d.result = ChangesDialog::Result::Discard;
            d.dialog->accept();
        });
        QObject::connect(d.ui->cancelButton, &QPushButton::clicked, d.dialog, &ChangesDialog::reject);
    }

    void setEntries(const QList<ChangesDialog::Entry>& entries);
    void contextMenu(const QPoint& position);

    struct Data {
        QScopedPointer<Ui_ChangesDialog> ui;
        QPointer<ChangesDialog> dialog;
        ChangesDialog::Result result = ChangesDialog::Result::Cancel;
    };
    Data d;
};

ChangesDialog::ChangesDialog(QWidget* parent)
    : QDialog(parent)
    , p(new ChangesDialogPrivate(this))
{}

ChangesDialog::~ChangesDialog() = default;

void
ChangesDialog::setDocumentName(const QString& name)
{
    p->d.ui->heading->setText(tr("Save changes to %1?").arg(name));
}

void
ChangesDialogPrivate::setEntries(const QList<ChangesDialog::Entry>& entries)
{
    auto* tree = d.ui->changesTree;
    tree->clear();
    bool canSave = !entries.isEmpty();
    QHash<QString, QTreeWidgetItem*> rows;
    QList<QTreeWidgetItem*> items;
    items.reserve(entries.size());
    for (const auto& entry : entries) {
        QString type;
        switch (entry.type) {
        case ChangesDialog::Type::RootLayer: type = ChangesDialog::tr("Root layer"); break;
        case ChangesDialog::Type::Sublayer: type = ChangesDialog::tr("Sublayer"); break;
        case ChangesDialog::Type::SessionLayer: type = ChangesDialog::tr("Session layer"); break;
        }
        const QFileInfo file(entry.location);
        const bool hasPath = file.isAbsolute();
        auto* row = new QTreeWidgetItem(
            { entry.name, type, entry.changes, hasPath ? file.fileName() : entry.location, entry.status });
        items.append(row);
        rows.insert(entry.identifier, row);
        row->setData(0, Qt::UserRole + 1, entry.identifier);
        row->setData(0, Qt::UserRole + 2, hasPath ? file.absoluteFilePath() : QString());
        row->setIcon(0, ui()->icon(UI::IconRole::Layer));
        row->setFlags(row->flags() & ~Qt::ItemIsEditable);
        for (int column = 0; column < tree->columnCount(); ++column)
            row->setToolTip(column, row->text(column));
        row->setToolTip(3, entry.location);
        canSave &= entry.canSave;
    }
    for (qsizetype index = 0; index < entries.size(); ++index) {
        const auto& entry = entries[index];
        auto* row = items[index];
        auto* parent = rows.value(entry.parentIdentifier);
        if (parent && row != parent)
            parent->addChild(row);
        else
            tree->addTopLevelItem(row);
    }
    tree->expandAll();
    d.ui->saveAllButton->setEnabled(canSave);
    d.ui->blockedNotice->setVisible(!canSave && !entries.isEmpty());
    d.ui->saveNotice->setVisible(entries.size() > 1);
}

void
ChangesDialogPrivate::contextMenu(const QPoint& position)
{
    auto* tree = d.ui->changesTree;
    auto* row = tree->itemAt(position);
    if (!row)
        return;
    const QString path = row->data(0, Qt::UserRole + 2).toString();
    if (path.isEmpty() || !QFileInfo(path).isFile())
        return;
    QMenu menu(d.dialog);
#ifdef Q_OS_MAC
    auto* reveal = menu.addAction(ChangesDialog::tr("Reveal in Finder"));
#elif defined(Q_OS_WIN)
    auto* reveal = menu.addAction(ChangesDialog::tr("Reveal in Explorer"));
#else
    auto* reveal = menu.addAction(ChangesDialog::tr("Reveal in folder"));
#endif
    reveal->setObjectName("changesReveal");
    reveal->setData(path);
    if (menu.exec(tree->viewport()->mapToGlobal(position)) == reveal && !os::revealFileInFolder(path))
        session()->notifyStatus(Session::Notify::Status::Warning,
                                ChangesDialog::tr("Could not reveal file in the file browser: %1").arg(path));
}

void
ChangesDialog::setEntries(const QList<Entry>& entries)
{
    p->setEntries(entries);
}

void
ChangesDialog::setDiscardAllowed(bool allowed)
{
    p->d.ui->discardButton->setVisible(allowed);
}

ChangesDialog::Result
ChangesDialog::choice() const
{
    return p->d.result;
}

void
ChangesDialog::reject()
{
    p->d.result = Result::Cancel;
    QDialog::reject();
}

}  // namespace stageviz
