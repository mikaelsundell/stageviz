// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#include "materialdialog.h"
#include "application.h"
#include "command.h"
#include "commandstack.h"
#include "materialbrowser.h"
#include "materialrenderer.h"
#include "materialtree.h"
#include "materialutils.h"
#include "notice.h"
#include "selectionlist.h"
#include "session.h"
#include "settings.h"
#include "style.h"
#include "tracelocks.h"
#include <QAction>
#include <QFileDialog>
#include <QFileInfo>
#include <QMenu>
#include <QPointer>
#include <QSet>
#include <QTimer>
#include <QToolButton>
#include <algorithm>
#include <pxr/base/vt/value.h>
#include <pxr/usd/usd/primRange.h>
#include <pxr/usd/usdShade/materialBindingAPI.h>

// generated files
#include "ui_materialdialog.h"

PXR_NAMESPACE_USING_DIRECTIVE

namespace stageviz {

class MaterialDialogPrivate : public QObject {
public:
    void init();
    void refresh();
    void updatePrims(const NoticeBatch& batch);
    void updateSelection();
    void requestSwatch(int row);
    void updateSwatch(const QString& materialPath, const QImage& image);
    void previewFloat(const QString& parameter, double value);
    void previewColor(const QString& parameter, const QColor& value);
    void editFloat(const QString& parameter, double value);
    void editColor(const QString& parameter, const QColor& value);
    void createPreviewSurface();
    void createStandardSurface();
    void loadMaterialX();
    void applyToSelection();
    void selectFromStage();
    void setStatus(const QString& text);

public:
    struct Data {
        QPointer<MaterialDialog> dialog;
        QScopedPointer<Ui_MaterialDialog> ui;
        QPointer<MaterialRenderer> renderer;
        QTimer* refreshTimer = nullptr;
    };
    Data d;
};

void
MaterialDialogPrivate::init()
{
    d.ui.reset(new Ui_MaterialDialog());
    d.ui->setupUi(d.dialog.data());

    d.renderer = new MaterialRenderer(this);

    d.ui->newMaterial->setIcon(style()->icon(Style::IconRole::New));
    d.ui->loadMaterial->setIcon(style()->icon(Style::IconRole::Open));
    d.ui->newMaterial->setText(QString());
    d.ui->newMaterial->setContextMenuPolicy(Qt::CustomContextMenu);

    d.ui->splitter->setChildrenCollapsible(false);
    d.ui->splitter->setCollapsible(0, false);
    d.ui->splitter->setCollapsible(1, true);
    d.ui->splitter->setStretchFactor(0, 1);
    d.ui->splitter->setStretchFactor(1, 0);

    const int total = d.ui->splitter->width() > 0 ? d.ui->splitter->width() : d.dialog->width();
    const int property = total / 2;
    const int browser = total - property;
    d.ui->splitter->setSizes({ browser, property });

    d.refreshTimer = new QTimer(this);
    d.refreshTimer->setSingleShot(true);
    d.refreshTimer->setInterval(160);

    // connect
    connect(d.refreshTimer, &QTimer::timeout, this, [this]() { refresh(); });
    connect(d.ui->materialBrowser, &MaterialBrowser::selectionChanged, this, [this]() { updateSelection(); });
    connect(d.ui->materialBrowser, &MaterialBrowser::swatchRequested, this, [this](int row) { requestSwatch(row); });
    connect(d.ui->materialTree, &MaterialTree::floatPreviewChanged, this,
            [this](const QString& parameter, double value) { previewFloat(parameter, value); });
    connect(d.ui->materialTree, &MaterialTree::colorPreviewChanged, this,
            [this](const QString& parameter, const QColor& value) { previewColor(parameter, value); });
    connect(d.ui->materialTree, &MaterialTree::floatChanged, this,
            [this](const QString& parameter, double value) { editFloat(parameter, value); });
    connect(d.ui->materialTree, &MaterialTree::colorChanged, this,
            [this](const QString& parameter, const QColor& value) { editColor(parameter, value); });
    connect(d.renderer, &MaterialRenderer::rendered, this,
            [this](const QString& path, const QImage& image) { updateSwatch(path, image); });
    connect(d.renderer, &MaterialRenderer::error, this,
            [this](const QString&, const QString& message) { setStatus(message); });
    connect(d.ui->newMaterial, &QToolButton::clicked, this, [this]() { createPreviewSurface(); });
    connect(d.ui->newMaterial, &QWidget::customContextMenuRequested, this, [this](const QPoint&) {
        QMenu menu(d.ui->newMaterial);
        QAction* standardSurface = menu.addAction("MaterialX Standard Surface");
        if (menu.exec(d.ui->newMaterial->mapToGlobal(QPoint(0, d.ui->newMaterial->height()))) == standardSurface)
            createStandardSurface();
    });
    connect(d.ui->loadMaterial, &QToolButton::clicked, this, [this]() { loadMaterialX(); });
    connect(session(), &Session::stageChanged, this, [this](UsdStageRefPtr, Session::LoadPolicy, Session::StageStatus) {
        d.renderer->clear();
        d.refreshTimer->start(0);
    });
    connect(session(), &Session::primsChanged, this, [this](const NoticeBatch& batch) {
        if (d.dialog->isVisible())
            updatePrims(batch);
    });
    refresh();
}

void
MaterialDialogPrivate::setStatus(const QString& text)
{
    d.ui->name->setToolTip(text);
}

void
MaterialDialogPrivate::refresh()
{
    QList<MaterialEntry> entries;
    {
        READ_LOCKER(locker, session()->stageLock(), "stageLock");
        entries = MaterialUtils::sceneMaterials(session()->stageUnsafe());
    }
    d.ui->materialBrowser->setEntries(entries);
    updateSelection();
}

void
MaterialDialogPrivate::updatePrims(const NoticeBatch& batch)
{
    if (batch.entries.isEmpty())
        return;

    QSet<QString> dirtyMaterials;
    bool structuralChange = false;

    auto knownMaterialForPath = [this](SdfPath path) -> SdfPath {
        if (path.IsPropertyPath())
            path = path.GetPrimPath();

        while (!path.IsEmpty() && path != SdfPath::AbsoluteRootPath()) {
            if (d.ui->materialBrowser->rowForMaterialPath(path) >= 0)
                return path;
            path = path.GetParentPath();
        }
        return {};
    };

    for (const NoticeEntry& entry : batch.entries) {
        if (entry.path.IsEmpty())
            continue;

        const SdfPath known = knownMaterialForPath(entry.path);
        const bool resync = entry.resolvedAssetPathsResynced
                            || entry.primResyncType != UsdNotice::ObjectsChanged::PrimResyncType::Invalid;

        if (resync) {
            if (!known.IsEmpty()) {
                structuralChange = true;
                break;
            }

            const SdfPath changedPrim = entry.path.IsPropertyPath() ? entry.path.GetPrimPath() : entry.path;
            for (const MaterialEntry& material : d.ui->materialBrowser->entries()) {
                if (material.materialPath.HasPrefix(changedPrim)) {
                    structuralChange = true;
                    break;
                }
            }

            if (structuralChange)
                break;

            READ_LOCKER(locker, session()->stageLock(), "stageLock");
            const UsdStageRefPtr stage = session()->stageUnsafe();
            if (stage) {
                const UsdPrim prim = stage->GetPrimAtPath(changedPrim);
                if (prim) {
                    if (prim.IsA<UsdShadeMaterial>()) {
                        structuralChange = true;
                    }
                    else {
                        for (const UsdPrim& descendant : UsdPrimRange(prim)) {
                            if (descendant.IsA<UsdShadeMaterial>()) {
                                structuralChange = true;
                                break;
                            }
                        }
                    }
                }
            }

            if (structuralChange)
                break;

            continue;
        }

        if (!known.IsEmpty())
            dirtyMaterials.insert(QString::fromStdString(known.GetString()));
    }

    if (structuralChange) {
        // Keep existing swatch images and renderer cache. MaterialBrowser
        // preserves them by material path when the material list changes.
        d.refreshTimer->start();
        return;
    }

    if (dirtyMaterials.isEmpty())
        return;

    {
        READ_LOCKER(locker, session()->stageLock(), "stageLock");
        const UsdStageRefPtr stage = session()->stageUnsafe();
        if (!stage)
            return;

        for (const QString& pathString : std::as_const(dirtyMaterials)) {
            const SdfPath materialPath(pathString.toStdString());
            const int row = d.ui->materialBrowser->rowForMaterialPath(materialPath);
            if (row < 0)
                continue;

            const UsdPrim prim = stage->GetPrimAtPath(materialPath);
            if (!prim || !prim.IsA<UsdShadeMaterial>()) {
                structuralChange = true;
                break;
            }

            QString shaderId;
            const UsdShadeShader shader = MaterialUtils::surfaceShader(UsdShadeMaterial(prim), &shaderId);
            if (!shader) {
                structuralChange = true;
                break;
            }

            MaterialEntry updated;
            updated.materialPath = materialPath;
            updated.shaderPath = shader.GetPath();
            updated.name = QString::fromStdString(prim.GetName().GetString());
            updated.shaderId = shaderId;
            updated.parameters = MaterialUtils::readParameters(shader, shaderId);

            d.ui->materialBrowser->updateEntry(row, updated);
            d.ui->materialBrowser->invalidateSwatch(row);
            d.renderer->invalidate(materialPath);
        }
    }

    if (structuralChange) {
        d.refreshTimer->start();
        return;
    }

    updateSelection();
    d.ui->materialBrowser->refreshVisibleSwatches();
}

void
MaterialDialogPrivate::updateSelection()
{
    const QList<MaterialEntry> entries = d.ui->materialBrowser->selectedEntries();

    if (entries.isEmpty()) {
        d.ui->name->setText("Material");
        d.ui->name->setToolTip(QString());
        d.ui->materialTree->clearMaterials();
        return;
    }

    d.ui->materialTree->setMaterials(entries);

    if (entries.size() == 1) {
        d.ui->name->setText(entries.first().name);
        d.ui->name->setToolTip(QString("%1\n%2").arg(MaterialUtils::shaderTypeLabel(entries.first().shaderId),
                                                     QString::fromStdString(entries.first().materialPath.GetString())));
    }
    else {
        d.ui->name->setText(QString("%1 materials").arg(entries.size()));
        d.ui->name->setToolTip("Editing common supported values");
    }
}

void
MaterialDialogPrivate::requestSwatch(int row)
{
    const MaterialEntry* entry = d.ui->materialBrowser->entry(row);
    if (entry)
        d.renderer->request(entry->materialPath, entry->parameters);
}

void
MaterialDialogPrivate::updateSwatch(const QString& materialPath, const QImage& image)
{
    const int row = d.ui->materialBrowser->rowForMaterialPath(SdfPath(materialPath.toStdString()));
    if (row >= 0)
        d.ui->materialBrowser->setSwatch(row, image);
}

void
MaterialDialogPrivate::previewFloat(const QString& parameter, double value)
{
    const QList<int> rows = d.ui->materialBrowser->selectedRows();

    for (int row : rows) {
        const MaterialEntry* entry = d.ui->materialBrowser->entry(row);
        if (!entry || !MaterialUtils::isSupportedParameter(*entry, parameter))
            continue;

        MaterialParameters parameters = entry->parameters;

        if (parameter == "metalness")
            parameters.metalness = static_cast<float>(value);
        else if (parameter == "roughness")
            parameters.roughness = static_cast<float>(value);
        else if (parameter == "specular")
            parameters.specular = static_cast<float>(value);
        else if (parameter == "ior")
            parameters.ior = static_cast<float>(value);
        else if (parameter == "coat")
            parameters.coat = static_cast<float>(value);
        else if (parameter == "coatRoughness")
            parameters.coatRoughness = static_cast<float>(value);
        else if (parameter == "opacity")
            parameters.opacity = static_cast<float>(value);
        else if (parameter == "transmission")
            parameters.transmission = static_cast<float>(value);
        else
            continue;

        // Keep the existing image visible. The renderer coalesces repeated
        // slider previews and replaces it only when a new swatch is ready.
        d.renderer->request(entry->materialPath, parameters, true);
    }
}

void
MaterialDialogPrivate::previewColor(const QString& parameter, const QColor& value)
{
    const QList<int> rows = d.ui->materialBrowser->selectedRows();
    const GfVec3f color(value.redF(), value.greenF(), value.blueF());

    for (int row : rows) {
        const MaterialEntry* entry = d.ui->materialBrowser->entry(row);
        if (!entry || !MaterialUtils::isSupportedParameter(*entry, parameter))
            continue;

        MaterialParameters parameters = entry->parameters;

        if (parameter == "baseColor")
            parameters.baseColor = color;
        else if (parameter == "transmissionColor")
            parameters.transmissionColor = color;
        else
            continue;

        d.renderer->request(entry->materialPath, parameters, true);
    }
}

void
MaterialDialogPrivate::editFloat(const QString& parameter, double value)
{
    const QList<MaterialEntry> entries = d.ui->materialBrowser->selectedEntries();
    QList<SdfPath> paths;

    for (const MaterialEntry& entry : entries) {
        if (!MaterialUtils::isSupportedParameter(entry, parameter))
            continue;

        const SdfPath path = MaterialUtils::inputPath(entry, parameter);
        if (!path.IsEmpty())
            paths.append(path);
    }

    if (paths.isEmpty())
        return;

    VtValue authored;
    if (parameter == "opacity") {
        const bool standardOnly = std::all_of(entries.cbegin(), entries.cend(), [](const MaterialEntry& entry) {
            return entry.shaderId == "ND_standard_surface_surfaceshader";
        });

        authored = standardOnly ? VtValue(GfVec3f(static_cast<float>(value))) : VtValue(static_cast<float>(value));
    }
    else {
        authored = VtValue(static_cast<float>(value));
    }

    session()->commandStack()->run(new Command(setAttributeValues(paths, authored)));
}

void
MaterialDialogPrivate::editColor(const QString& parameter, const QColor& value)
{
    const QList<MaterialEntry> entries = d.ui->materialBrowser->selectedEntries();
    QList<SdfPath> paths;

    for (const MaterialEntry& entry : entries) {
        if (!MaterialUtils::isSupportedParameter(entry, parameter))
            continue;

        const SdfPath path = MaterialUtils::inputPath(entry, parameter);
        if (!path.IsEmpty())
            paths.append(path);
    }

    if (paths.isEmpty())
        return;

    const GfVec3f color(value.redF(), value.greenF(), value.blueF());
    session()->commandStack()->run(new Command(setAttributeValues(paths, VtValue(color))));
}

void
MaterialDialogPrivate::createPreviewSurface()
{
    SdfPath path;
    {
        WRITE_LOCKER(locker, session()->stageLock(), "stageLock");
        path = MaterialUtils::createPreviewSurfaceMaterial(session()->stageUnsafe());
    }

    if (!path.IsEmpty())
        d.refreshTimer->start(0);
}

void
MaterialDialogPrivate::createStandardSurface()
{
    SdfPath path;
    {
        WRITE_LOCKER(locker, session()->stageLock(), "stageLock");
        path = MaterialUtils::createStandardSurfaceMaterial(session()->stageUnsafe());
    }

    if (!path.IsEmpty())
        d.refreshTimer->start(0);
}

void
MaterialDialogPrivate::loadMaterialX()
{
    const QString directory = settings()->value("materialXDir", QDir::homePath()).toString();

    QFileDialog dialog(d.dialog, tr("Load MaterialX"), directory, tr("MaterialX (*.mtlx);;All Files (*)"));
    dialog.setFileMode(QFileDialog::ExistingFile);
    dialog.setAcceptMode(QFileDialog::AcceptOpen);
    dialog.setOption(QFileDialog::DontUseNativeDialog, true);

    if (dialog.exec() != QDialog::Accepted || dialog.selectedFiles().isEmpty())
        return;

    const QString filename = dialog.selectedFiles().first();
    QList<SdfPath> created;
    QString error;

    {
        WRITE_LOCKER(locker, session()->stageLock(), "stageLock");
        if (!MaterialUtils::importMaterialX(session()->stageUnsafe(), filename, created, error)) {
            setStatus(error);
            return;
        }
    }

    settings()->setValue("materialXDir", QFileInfo(filename).absolutePath());
    d.refreshTimer->start(0);
}

void
MaterialDialogPrivate::applyToSelection()
{
    const QList<MaterialEntry> materials = d.ui->materialBrowser->selectedEntries();
    if (materials.size() != 1)
        return;

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

        for (const SdfPath& selectedPath : selectedPaths) {
            const SdfPath primPath = selectedPath.IsPropertyPath() ? selectedPath.GetPrimPath() : selectedPath;
            const QString key = QString::fromStdString(primPath.GetString());

            if (primPath.IsEmpty() || primPath == SdfPath::AbsoluteRootPath() || seen.contains(key))
                continue;

            const UsdPrim prim = stage->GetPrimAtPath(primPath);
            if (!prim || !prim.IsValid())
                continue;

            seen.insert(key);
            paths.append(primPath);
        }
    }

    if (!paths.isEmpty())
        session()->commandStack()->run(new Command(bindMaterial(paths, materials.first().materialPath)));
}

void
MaterialDialogPrivate::selectFromStage()
{
    const QList<SdfPath> paths = session()->selectionList()->paths();
    if (paths.isEmpty())
        return;

    SdfPath materialPath;
    {
        READ_LOCKER(locker, session()->stageLock(), "stageLock");
        const UsdStageRefPtr stage = session()->stageUnsafe();
        if (!stage)
            return;

        for (const SdfPath& path : paths) {
            const UsdPrim prim = stage->GetPrimAtPath(path.IsPropertyPath() ? path.GetPrimPath() : path);
            if (!prim)
                continue;

            const UsdShadeMaterial bound = UsdShadeMaterialBindingAPI(prim).ComputeBoundMaterial();
            if (bound) {
                materialPath = bound.GetPath();
                break;
            }
        }
    }

    if (materialPath.IsEmpty())
        return;

    const int row = d.ui->materialBrowser->rowForMaterialPath(materialPath);
    if (row >= 0)
        d.ui->materialBrowser->selectRow(row);
}

MaterialDialog::MaterialDialog(QWidget* parent)
    : QDialog(parent)
    , p(new MaterialDialogPrivate())
{
    p->d.dialog = this;
    p->init();
}

MaterialDialog::~MaterialDialog() = default;

}  // namespace stageviz
