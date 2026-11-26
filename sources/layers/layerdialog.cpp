// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#include "layerdialog.h"
#include "application.h"
#include "commandlayer.h"
#include "commandstack.h"
#include "fileformats.h"
#include "layertree.h"
#include "session.h"
#include "settings.h"
#include "tracelocks.h"
#include "ui.h"
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QPointer>
#include <QShowEvent>
#include <QToolButton>
#include <pxr/usd/sdf/layer.h>

PXR_NAMESPACE_USING_DIRECTIVE

// generated files
#include "ui_layerdialog.h"

namespace stageviz {

class LayerDialogPrivate : public QObject {
public:
    LayerDialogPrivate(LayerDialog* dialog);
    ~LayerDialogPrivate() override;
    void init();
    void refresh();
    void updateStageState();
    void createLayer();
    void addLayer();
    void addLayerFile(const QString& filename);
    QString layerDirectory() const;
    void rememberLayerDirectory(const QString& filename);

public:
    struct Data {
        QScopedPointer<Ui_LayerDialog> ui;
        QPointer<LayerDialog> dialog;
    };
    Data d;
};

LayerDialogPrivate::LayerDialogPrivate(LayerDialog* dialog)
{
    d.dialog = dialog;
    init();
}

LayerDialogPrivate::~LayerDialogPrivate() = default;

void
LayerDialogPrivate::init()
{
    d.ui.reset(new Ui_LayerDialog());
    d.ui->setupUi(d.dialog.data());
    d.dialog->setWindowTitle(tr("Layer Editor"));

    d.ui->newMaterial->setIcon(ui()->icon(UI::IconRole::New));
    d.ui->load->setIcon(ui()->icon(UI::IconRole::Open));
    d.ui->select->setIcon(ui()->icon(UI::IconRole::Select));

    connect(d.ui->newMaterial, &QToolButton::clicked, this, &LayerDialogPrivate::createLayer);
    connect(d.ui->load, &QToolButton::clicked, this, &LayerDialogPrivate::addLayer);
    connect(d.ui->select, &QToolButton::clicked, d.ui->layerTree, &LayerTree::selectCurrentLayer);
    connect(d.ui->layerTree, &LayerTree::newLayerRequested, this, &LayerDialogPrivate::createLayer);
    connect(d.ui->layerTree, &LayerTree::addLayerRequested, this, &LayerDialogPrivate::addLayer);
    connect(d.ui->layerTree, &LayerTree::layerSaved, this, [this]() { Q_EMIT d.dialog->layerSaved(); });
    if (session()) {
        connect(session(), &Session::stageChanged, this,
                [this](UsdStageRefPtr, Session::LoadPolicy, Session::StageStatus) { updateStageState(); });
    }
    d.ui->layerTree->refresh();
    updateStageState();
}

void
LayerDialogPrivate::refresh()
{
    if (d.ui && d.ui->layerTree)
        d.ui->layerTree->refresh();
    updateStageState();
}

void
LayerDialogPrivate::updateStageState()
{
    const bool loaded = session() && session()->isLoaded();
    d.ui->newMaterial->setEnabled(loaded);
    d.ui->load->setEnabled(loaded);
    d.ui->select->setEnabled(loaded);
}

QString
LayerDialogPrivate::layerDirectory() const
{
    QString current;
    if (session() && session()->isLoaded()) {
        READ_LOCKER(locker, session()->stageLock(), "stageLock");
        const UsdStageRefPtr stage = session()->stageUnsafe();
        if (stage) {
            const SdfLayerHandle editLayer = stage->GetEditTarget().GetLayer();
            const SdfLayerHandle rootLayer = stage->GetRootLayer();
            if (editLayer)
                current = QString::fromStdString(editLayer->GetRealPath());
            if (current.isEmpty() && rootLayer)
                current = QString::fromStdString(rootLayer->GetRealPath());
        }
    }

    if (!current.isEmpty())
        return QFileInfo(current).absolutePath();
    return settings() ? settings()->value("layerDir", QDir::homePath()).toString() : QDir::homePath();
}

void
LayerDialogPrivate::rememberLayerDirectory(const QString& filename)
{
    if (settings() && !filename.isEmpty())
        settings()->setValue("layerDir", QFileInfo(filename).absolutePath());
}

void
LayerDialogPrivate::createLayer()
{
    if (!session() || !session()->isLoaded()) {
        return;
    }

    QFileDialog dialog(d.dialog.data(), tr("Create USD Layer"), layerDirectory(), fileformats::filters(true, true));
    dialog.setAcceptMode(QFileDialog::AcceptSave);
    dialog.setFileMode(QFileDialog::AnyFile);
    dialog.setDefaultSuffix(fileformats::defaultExtension(true));
    if (dialog.exec() != QDialog::Accepted || dialog.selectedFiles().isEmpty())
        return;

    const QString path = QFileInfo(dialog.selectedFiles().first()).absoluteFilePath();
    rememberLayerDirectory(path);
    session()->commandStack()->execute(new Command(createSublayer(path)));
    d.ui->layerTree->refresh();
}

void
LayerDialogPrivate::addLayer()
{
    if (!session() || !session()->isLoaded()) {
        return;
    }

    const QString filename = QFileDialog::getOpenFileName(d.dialog.data(), tr("Add USD Layer"), layerDirectory(),
                                                          fileformats::filters(false, true));
    if (!filename.isEmpty()) {
        rememberLayerDirectory(filename);
        addLayerFile(filename);
    }
    else {}
}

void
LayerDialogPrivate::addLayerFile(const QString& filename)
{
    const QString path = QFileInfo(filename).absoluteFilePath();
    SdfLayerRefPtr layer;
    try {
        layer = SdfLayer::FindOrOpen(path.toStdString());
    } catch (const std::exception& exception) {
        session()->notifyStatus(Session::Notify::Status::Error,
                                tr("Could not open layer: %1").arg(QString::fromUtf8(exception.what())));
        return;
    }
    if (!layer) {
        session()->notifyStatus(Session::Notify::Status::Error, tr("Could not open USD layer: %1").arg(path));
        return;
    }

    session()->commandStack()->execute(new Command(addSublayer(path)));
    bool addedToStage = false;
    {
        READ_LOCKER(locker, session()->stageLock(), "stageLock");
        const UsdStageRefPtr stage = session()->stageUnsafe();
        addedToStage = stage && stage->HasLocalLayer(layer);
    }
    if (!addedToStage) {
        session()->notifyStatus(Session::Notify::Status::Error, tr("Could not add layer to the stage: %1").arg(path));
        return;
    }
    d.ui->layerTree->refresh();
    session()->notifyStatus(Session::Notify::Status::Success, tr("Added layer %1").arg(QFileInfo(path).fileName()));
}

LayerDialog::LayerDialog(QWidget* parent)
    : QDialog(parent)
    , p(new LayerDialogPrivate(this))
{}

LayerDialog::~LayerDialog() = default;

void
LayerDialog::refreshLayers()
{
    p->refresh();
}

void
LayerDialog::showEvent(QShowEvent* event)
{
    QDialog::showEvent(event);
    p->refresh();
}

}  // namespace stageviz
