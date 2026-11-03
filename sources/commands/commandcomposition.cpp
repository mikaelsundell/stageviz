// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#include "commandcomposition.h"
#include "commandutils.h"
#include "qtutils.h"
#include "selectionlist.h"
#include "tracelocks.h"
#include "usdutils.h"
#include <QDir>
#include <QFileInfo>
#include <algorithm>
#include <memory>
#include <pxr/usd/sdf/copyUtils.h>
#include <pxr/usd/sdf/layer.h>
#include <pxr/usd/sdf/layerUtils.h>
#include <pxr/usd/sdf/payload.h>
#include <pxr/usd/sdf/reference.h>
#include <pxr/usd/sdf/schema.h>
#include <pxr/usd/usd/editContext.h>
#include <pxr/usd/usd/editTarget.h>
#include <pxr/usd/usd/payloads.h>
#include <pxr/usd/usd/prim.h>
#include <pxr/usd/usd/references.h>
#include <pxr/usd/usd/variantSets.h>
#include <pxr/usd/usdUtils/dependencies.h>
#include <vector>

namespace stageviz {

namespace {
    using command::appendError;
    using command::currentEditLayer;
    using command::pathText;
    using command::validateNotInstanceProxy;

    namespace variant {
        struct SelectionState {
            struct Item {
                SdfPath path;
                bool hadPrimSpec = false;
                bool hadVariantSelectionField = false;
                VtValue variantSelectionField;
            };

            QList<Item> items;
            bool captured = false;
        };
    }  // namespace variant

    SdfLayerRefPtr relocatedMergeCopy(const SdfLayerHandle& layer)
    {
        if (!layer)
            return {};

        const SdfLayerRefPtr copy = SdfLayer::CreateAnonymous("stageviz_merge.usda");
        if (!copy)
            return {};

        copy->TransferContent(layer);
        UsdUtilsModifyAssetPaths(
            copy,
            [&](const std::string& path) {
                if (path.empty() || SdfLayer::IsAnonymousLayerIdentifier(path))
                    return path;

                const std::string anchored = SdfComputeAssetPathRelativeToLayer(layer, path);
                const QString value = QString::fromStdString(anchored);
                if (!value.contains(':') && QDir::isRelativePath(value)) {
                    const QString realPath = QString::fromStdString(layer->GetRealPath());
                    const QDir anchor = realPath.isEmpty() ? QDir::current() : QFileInfo(realPath).absoluteDir();
                    return anchor.absoluteFilePath(value).toStdString();
                }
                return anchored;
            },
            true);
        return copy;
    }

    bool mergeLayer(Session* session, const SdfLayerHandle& sourceLayer, QString& error)
    {
        if (!session || !sourceLayer) {
            error = "source layer missing";
            return false;
        }

        WRITE_LOCKER(locker, session->stageLock(), "stageLock");
        const UsdStageRefPtr stage = session->stageUnsafe();
        if (!stage) {
            error = "stage missing";
            return false;
        }

        const SdfLayerHandle destinationLayer = stage->GetEditTarget().GetLayer();
        if (!destinationLayer) {
            error = "edit layer missing";
            return false;
        }

        if (!sourceLayer->IsAnonymous() && !destinationLayer->IsAnonymous()
            && sourceLayer->GetRealPath() == destinationLayer->GetRealPath()) {
            error = "cannot merge a layer into itself";
            return false;
        }

        const SdfLayerRefPtr anchoredSource = relocatedMergeCopy(sourceLayer);
        if (!anchoredSource) {
            error = "failed to prepare source layer";
            return false;
        }

        for (const SdfPrimSpecHandle& sourcePrim : anchoredSource->GetRootPrims()) {
            if (!sourcePrim)
                continue;

            const SdfPath sourcePath = sourcePrim->GetPath();
            QString pathError;
            const SdfPath destinationPath = stage::buildChildPath(stage, SdfPath::AbsoluteRootPath(),
                                                                  qt::StringToQString(sourcePath.GetName()), pathError);
            if (destinationPath.IsEmpty()) {
                error = pathError.isEmpty()
                            ? QString("failed to build destination path for %1").arg(pathText(sourcePath))
                            : pathError;
                return false;
            }

            if (!SdfCopySpec(anchoredSource, sourcePath, destinationLayer, destinationPath)) {
                error = QString("failed to merge prim: %1").arg(pathText(sourcePath));
                return false;
            }
        }

        return true;
    }

    struct MergeLayerState {
        SdfLayerRefPtr snapshot;
        SdfLayerHandle editLayer;
        bool captured = false;
    };

    Command mergeLayerCommand(const QString& filename, bool flatten)
    {
        auto state = std::make_shared<MergeLayerState>();
        const QString title = flatten ? QStringLiteral("Merge flattened") : QStringLiteral("Merge stage");
        const QString successMessage = flatten ? QStringLiteral("Flattened stage merged")
                                               : QStringLiteral("Stage merged");

        return Command(
            [filename, flatten, state, title, successMessage](Session* session) {
                if (!session || filename.isEmpty())
                    return;

                session->beginProgressBlock(title, 1);
                bool success = false;
                QString error;

                if (!state->captured) {
                    READ_LOCKER(locker, session->stageLock(), "stageLock");
                    const UsdStageRefPtr stage = session->stageUnsafe();
                    if (!stage) {
                        error = "stage missing";
                    }
                    else {
                        state->editLayer = stage->GetEditTarget().GetLayer();
                        if (!state->editLayer) {
                            error = "edit layer missing";
                        }
                        else {
                            state->snapshot = SdfLayer::CreateAnonymous("stageviz_merge_snapshot.usda");
                            if (!state->snapshot)
                                error = "failed to create merge snapshot";
                            else {
                                state->snapshot->TransferContent(state->editLayer);
                                state->captured = true;
                            }
                        }
                    }
                }

                if (state->captured) {
                    const QString absFilename = QFileInfo(filename).absoluteFilePath();
                    UsdStageRefPtr sourceStage;
                    try {
                        sourceStage = UsdStage::Open(qt::QStringToString(absFilename),
                                                     flatten ? UsdStage::LoadAll : UsdStage::LoadNone);
                    } catch (const std::exception&) {
                        error = "failed to open source stage";
                    }

                    if (sourceStage) {
                        SdfLayerHandle sourceLayer;
                        SdfLayerRefPtr flattenedLayer;
                        if (flatten) {
                            flattenedLayer = sourceStage->Flatten();
                            sourceLayer = flattenedLayer;
                        }
                        else {
                            sourceLayer = sourceStage->GetRootLayer();
                        }

                        if (!sourceLayer)
                            error = flatten ? "failed to flatten source stage" : "source layer missing";
                        else
                            success = mergeLayer(session, sourceLayer, error);
                    }
                    else if (error.isEmpty()) {
                        error = "failed to open source stage";
                    }
                }

                if (!success && state->captured && state->snapshot && state->editLayer) {
                    WRITE_LOCKER(locker, session->stageLock(), "stageLock");
                    const UsdStageRefPtr stage = session->stageUnsafe();
                    if (stage && stage->GetEditTarget().GetLayer() == state->editLayer)
                        state->editLayer->TransferContent(state->snapshot);
                }

                using Status = Session::Notify::Status;
                session->updateProgressNotify(Session::Notify(success
                                                                  ? successMessage
                                                                  : appendError(QString("%1 failed").arg(title), error),
                                                              {}, success ? Status::Success : Status::Error),
                                              1);
                session->endProgressBlock();
            },
            [state, title](Session* session) {
                if (!session || !state->captured || !state->snapshot || !state->editLayer)
                    return;

                session->beginProgressBlock(QString("Undo %1").arg(title.toLower()), 1);
                bool success = false;
                {
                    WRITE_LOCKER(locker, session->stageLock(), "stageLock");
                    const UsdStageRefPtr stage = session->stageUnsafe();
                    if (stage && stage->GetEditTarget().GetLayer() == state->editLayer) {
                        state->editLayer->TransferContent(state->snapshot);
                        success = true;
                    }
                }
                using Status = Session::Notify::Status;
                session->updateProgressNotify(Session::Notify(success ? QString("%1 undone").arg(title)
                                                                      : QString("Undo %1 failed").arg(title.toLower()),
                                                              {}, success ? Status::Success : Status::Error),
                                              1);
                session->endProgressBlock();
            });
    }

    struct CompositionArcState {
        SdfLayerHandle editLayer;
        SdfPath targetPath;
        TfToken field;
        bool hadPrimSpec = false;
        bool hadField = false;
        VtValue previousValue;
        bool captured = false;
    };

    Command addCompositionArc(const QString& filename, const SdfPath& targetPath, bool payloadArc)
    {
        auto state = std::make_shared<CompositionArcState>();
        const QString title = payloadArc ? QStringLiteral("Add payload") : QStringLiteral("Add reference");
        const QString successMessage = payloadArc ? QStringLiteral("Payload added") : QStringLiteral("Reference added");
        const TfToken field = payloadArc ? SdfFieldKeys->Payload : SdfFieldKeys->References;

        return Command(
            [filename, targetPath, payloadArc, state, title, successMessage, field](Session* session) {
                if (!session || filename.isEmpty() || targetPath.IsEmpty())
                    return;

                session->beginProgressBlock(title, 1);
                bool success = false;
                QString error;
                {
                    WRITE_LOCKER(locker, session->stageLock(), "stageLock");
                    const UsdStageRefPtr stage = session->stageUnsafe();
                    if (!stage || !targetPath.IsAbsolutePath() || !targetPath.IsPrimPath()) {
                        error = "invalid stage or target path";
                    }
                    else {
                        const SdfLayerHandle editLayer = stage->GetEditTarget().GetLayer();
                        const UsdPrim targetPrim = stage->GetPrimAtPath(targetPath);
                        if (!editLayer)
                            error = "edit layer missing";
                        else if (!targetPrim || !targetPrim.IsValid())
                            error = QString("target prim missing: %1").arg(pathText(targetPath));
                        else {
                            const QString absFilename = QFileInfo(filename).absoluteFilePath();
                            UsdStageRefPtr sourceStage;
                            try {
                                sourceStage = UsdStage::Open(qt::QStringToString(absFilename), UsdStage::LoadNone);
                            } catch (const std::exception&) {}

                            if (!sourceStage || !sourceStage->GetDefaultPrim()) {
                                error = "source stage has no default prim";
                            }
                            else {
                                if (!state->captured) {
                                    state->editLayer = editLayer;
                                    state->targetPath = targetPath;
                                    state->field = field;
                                    state->hadPrimSpec = bool(editLayer->GetPrimAtPath(targetPath));
                                    state->hadField = editLayer->HasField(targetPath, field);
                                    if (state->hadField)
                                        state->previousValue = editLayer->GetField(targetPath, field);
                                    state->captured = true;
                                }

                                const std::string assetPath = stage::compositionAssetPath(editLayer, absFilename);
                                success = payloadArc ? targetPrim.GetPayloads().AddPayload(assetPath)
                                                     : targetPrim.GetReferences().AddReference(assetPath);
                                if (!success)
                                    error = payloadArc ? "failed to add payload" : "failed to add reference";
                            }
                        }
                    }
                }

                using Status = Session::Notify::Status;
                session->updateProgressNotify(
                    Session::Notify(success ? successMessage : appendError(QString("%1 failed").arg(title), error),
                                    { targetPath }, success ? Status::Success : Status::Error),
                    1);
                session->endProgressBlock();
            },
            [state, title](Session* session) {
                if (!session || !state->captured || !state->editLayer)
                    return;

                session->beginProgressBlock(QString("Undo %1").arg(title.toLower()), 1);
                bool success = false;
                {
                    WRITE_LOCKER(locker, session->stageLock(), "stageLock");
                    const UsdStageRefPtr stage = session->stageUnsafe();
                    if (stage && stage->GetEditTarget().GetLayer() == state->editLayer) {
                        if (state->hadField)
                            state->editLayer->SetField(state->targetPath, state->field, state->previousValue);
                        else
                            state->editLayer->EraseField(state->targetPath, state->field);

                        if (!state->hadPrimSpec) {
                            const SdfPrimSpecHandle primSpec = state->editLayer->GetPrimAtPath(state->targetPath);
                            if (primSpec && primSpec->IsInert())
                                stage::removePrimSpec(state->editLayer, state->targetPath);
                        }
                        success = true;
                    }
                }

                using Status = Session::Notify::Status;
                session->updateProgressNotify(Session::Notify(success ? QString("%1 undone").arg(title)
                                                                      : QString("Undo %1 failed").arg(title.toLower()),
                                                              { state->targetPath },
                                                              success ? Status::Success : Status::Error),
                                              1);
                session->endProgressBlock();
            });
    }
}  // namespace

Command
mergeStage(const QString& filename)
{
    return mergeLayerCommand(filename, false);
}

Command
mergeFlattenedStage(const QString& filename)
{
    return mergeLayerCommand(filename, true);
}

Command
addReference(const QString& filename, const SdfPath& targetPath)
{
    return addCompositionArc(filename, targetPath, false);
}

Command
addPayload(const QString& filename, const SdfPath& targetPath)
{
    return addCompositionArc(filename, targetPath, true);
}

Command
setVariantSelection(const QList<SdfPath>& paths, const QString& setName, const QString& value)
{
    auto state = std::make_shared<variant::SelectionState>();
    const std::string variantSetName = qt::QStringToString(setName);
    const std::string variantValue = qt::QStringToString(value);

    return Command(
        [paths, variantSetName, variantValue, state](Session* session) {
            if (!session || paths.isEmpty() || variantSetName.empty() || variantValue.empty())
                return;

            const QList<SdfPath> uniquePaths = path::uniquePaths(paths);

            session->beginProgressBlock("Set variant", uniquePaths.size());

            command::runWorker([session, uniquePaths, variantSetName, variantValue, state]() {
                QList<command::Result> pending;
                pending.reserve(16);

                QList<SdfPath> changed;
                changed.reserve(uniquePaths.size());

                {
                    WRITE_LOCKER(locker, session->stageLock(), "stageLock");
                    const UsdStageRefPtr stage = session->stageUnsafe();

                    if (stage) {
                        QString editError;
                        const SdfLayerHandle editLayer = currentEditLayer(stage, editError);

                        if (editLayer) {
                            if (!state->captured) {
                                state->items.clear();
                                state->items.reserve(uniquePaths.size());

                                for (const SdfPath& inputPath : uniquePaths) {
                                    const SdfPath primPath = inputPath.IsPropertyPath() ? inputPath.GetPrimPath()
                                                                                        : inputPath;
                                    const UsdPrim prim = stage->GetPrimAtPath(primPath);

                                    if (!prim || !prim.IsValid() || prim.IsInstanceProxy())
                                        continue;

                                    UsdVariantSet variantSet = prim.GetVariantSet(variantSetName);
                                    if (!variantSet.IsValid())
                                        continue;

                                    const std::vector<std::string> names = variantSet.GetVariantNames();
                                    if (std::find(names.begin(), names.end(), variantValue) == names.end())
                                        continue;

                                    variant::SelectionState::Item item;
                                    item.path = primPath;
                                    item.hadPrimSpec = bool(editLayer->GetPrimAtPath(primPath));
                                    item.hadVariantSelectionField = editLayer->HasField(primPath,
                                                                                        SdfFieldKeys->VariantSelection);

                                    if (item.hadVariantSelectionField) {
                                        item.variantSelectionField
                                            = editLayer->GetField(primPath, SdfFieldKeys->VariantSelection);
                                    }

                                    state->items.append(item);
                                }

                                state->captured = true;
                            }
                        }
                    }
                }

                int completed = 0;
                for (const SdfPath& inputPath : uniquePaths) {
                    if (!session || session->isProgressBlockCancelled())
                        break;

                    command::Result result;
                    result.path = inputPath;
                    result.message = "Variant set";
                    result.status = Session::Notify::Status::Error;

                    QString error;
                    bool success = false;

                    try {
                        WRITE_LOCKER(locker, session->stageLock(), "stageLock");
                        const UsdStageRefPtr stage = session->stageUnsafe();

                        if (!stage) {
                            error = "Stage not available";
                        }
                        else {
                            QString editError;
                            const SdfLayerHandle editLayer = currentEditLayer(stage, editError);

                            if (!editLayer) {
                                error = editError;
                            }
                            else {
                                const SdfPath primPath = inputPath.IsPropertyPath() ? inputPath.GetPrimPath()
                                                                                    : inputPath;
                                const UsdPrim prim = stage->GetPrimAtPath(primPath);
                                if (!prim || !prim.IsValid()) {
                                    error = QString("prim missing: %1").arg(pathText(primPath));
                                }
                                else if (prim.IsInstanceProxy()) {
                                    error = QString("cannot edit instance proxy: %1").arg(pathText(primPath));
                                }
                                else {
                                    UsdVariantSet variantSet = prim.GetVariantSet(variantSetName);

                                    if (!variantSet.IsValid()) {
                                        error = QString("variant set missing: %1").arg(pathText(primPath));
                                    }
                                    else {
                                        const std::vector<std::string> names = variantSet.GetVariantNames();

                                        if (std::find(names.begin(), names.end(), variantValue) == names.end()) {
                                            error = QString("variant value missing: %1").arg(pathText(primPath));
                                        }
                                        else if (variantSet.GetVariantSelection() == variantValue) {
                                            success = true;
                                        }
                                        else {
                                            UsdEditContext context(stage, stage->GetEditTarget());

                                            success = variantSet.SetVariantSelection(variantValue);

                                            if (!success)
                                                error = QString("failed to set variant: %1").arg(pathText(primPath));
                                        }

                                        if (success)
                                            path::appendUnique(changed, primPath);
                                    }
                                }
                            }
                        }
                    } catch (...) {
                        success = false;
                        error = "exception";
                    }

                    result.success = success;
                    result.message = success ? "Variant set" : (error.isEmpty() ? "Variant set failed" : error);
                    result.status = success ? Session::Notify::Status::Success : Session::Notify::Status::Error;

                    pending.append(result);
                    ++completed;

                    if (pending.size() >= 16) {
                        const QList<command::Result> batch = pending;

                        command::queueToSession(session, [session, batch, completed]() {
                            command::flushResults(session, batch, completed);
                        });
                        pending.clear();
                    }
                }

                command::queueToSession(session, [session, pending, completed]() {
                    if (!pending.isEmpty())
                        command::flushResults(session, pending, completed);

                    session->endProgressBlock();
                });
            });
        },
        [state](Session* session) {
            if (!session || !state->captured || state->items.isEmpty())
                return;

            session->beginProgressBlock("Undo set variant", state->items.size());

            command::runWorker([session, state]() {
                QList<command::Result> pending;
                pending.reserve(16);

                int completed = 0;

                for (const variant::SelectionState::Item& item : state->items) {
                    if (!session || session->isProgressBlockCancelled())
                        break;

                    command::Result result;
                    result.path = item.path;
                    result.message = "Variant restored";
                    result.status = Session::Notify::Status::Error;

                    QString error;
                    bool success = false;

                    try {
                        WRITE_LOCKER(locker, session->stageLock(), "stageLock");
                        const UsdStageRefPtr stage = session->stageUnsafe();

                        if (!stage) {
                            error = "Stage not available";
                        }
                        else {
                            QString editError;
                            const SdfLayerHandle editLayer = currentEditLayer(stage, editError);

                            if (!editLayer) {
                                error = editError;
                            }
                            else if (!validateNotInstanceProxy(stage, item.path, error)) {
                                // Keep the undo path from authoring through a proxy after composition changes.
                            }
                            else {
                                if (item.hadVariantSelectionField) {
                                    editLayer->SetField(item.path, SdfFieldKeys->VariantSelection,
                                                        item.variantSelectionField);
                                }
                                else {
                                    editLayer->EraseField(item.path, SdfFieldKeys->VariantSelection);
                                }

                                if (!item.hadPrimSpec) {
                                    const SdfPrimSpecHandle primSpec = editLayer->GetPrimAtPath(item.path);
                                    if (primSpec && primSpec->IsInert()
                                        && !stage::removePrimSpec(editLayer, item.path)) {
                                        error = QString("failed to remove empty variant override: %1")
                                                    .arg(pathText(item.path));
                                    }
                                }

                                success = error.isEmpty();
                            }
                        }
                    } catch (...) {
                        success = false;
                        error = "exception";
                    }

                    result.success = success;
                    result.message = success ? "Variant restored"
                                             : (error.isEmpty() ? "Variant restore failed" : error);
                    result.status = success ? Session::Notify::Status::Success : Session::Notify::Status::Error;

                    pending.append(result);
                    ++completed;

                    if (pending.size() >= 16) {
                        const QList<command::Result> batch = pending;

                        command::queueToSession(session, [session, batch, completed]() {
                            command::flushResults(session, batch, completed);
                        });

                        pending.clear();
                    }
                }

                command::queueToSession(session, [session, pending, completed]() {
                    if (!pending.isEmpty())
                        command::flushResults(session, pending, completed);

                    session->endProgressBlock();
                });
            });
        });
}

namespace {

    enum class CompositionArc { Reference, Payload };

    Command newCompositionArcPath(const SdfPath& parentPath, const QString& nameInput, const QString& assetPath,
                                  const SdfPath& primPath, CompositionArc arc)
    {
        struct NewCompositionArcState {
            SdfPath parentPath;
            SdfPath createdPath;
            TfTokenVector oldParentOrder;
            QList<SdfPath> createdAncestorSpecs;
            QList<SdfPath> previousSelection;
            QList<SdfPath> previousMask;
        };

        auto state = std::make_shared<NewCompositionArcState>();
        const QString title = arc == CompositionArc::Reference ? QStringLiteral("New reference")
                                                               : QStringLiteral("New payload");
        const QString successMessage = arc == CompositionArc::Reference ? QStringLiteral("Reference created")
                                                                        : QStringLiteral("Payload created");
        const QString failureMessage = arc == CompositionArc::Reference ? QStringLiteral("New reference failed")
                                                                        : QStringLiteral("New payload failed");

        return Command(
            [parentPath, nameInput, assetPath, primPath, arc, state, title, successMessage,
             failureMessage](Session* session) {
                if (!session || parentPath.IsEmpty() || assetPath.isEmpty())
                    return;

                state->previousSelection = session->selectionList()->paths();
                state->previousMask = session->mask();

                command::beginDeferred(session, title, 1);

                command::runWorker([session, parentPath, nameInput, assetPath, primPath, arc, state, successMessage,
                                    failureMessage]() {
                    bool success = false;
                    QString error;
                    SdfPath newPath;
                    QList<SdfPath> changed;

                    {
                        WRITE_LOCKER(locker, session->stageLock(), "stageLock");
                        const UsdStageRefPtr stage = session->stageUnsafe();

                        if (!stage) {
                            error = "stage missing";
                        }
                        else {
                            QString editError;
                            const SdfLayerHandle editLayer = currentEditLayer(stage, editError);

                            if (!editLayer) {
                                error = editError;
                            }
                            else if (!layer::validateParent(stage, editLayer, parentPath, error)) {}
                            else {
                                newPath = stage::buildChildPath(stage, parentPath, nameInput, error);

                                if (!newPath.IsEmpty()) {
                                    state->parentPath = parentPath;
                                    state->createdPath = newPath;
                                    state->oldParentOrder.clear();
                                    state->createdAncestorSpecs.clear();

                                    stage::captureChildOrder(stage, parentPath, state->oldParentOrder);

                                    for (SdfPath path = parentPath;
                                         !path.IsEmpty() && path != SdfPath::AbsoluteRootPath();
                                         path = path.GetParentPath()) {
                                        if (!editLayer->GetPrimAtPath(path))
                                            state->createdAncestorSpecs.prepend(path);
                                    }

                                    UsdEditContext context(stage, UsdEditTarget(editLayer));
                                    const UsdPrim prim = stage->DefinePrim(newPath, TfToken("Xform"));

                                    if (!prim || !prim.IsValid()) {
                                        error = "failed to define composition prim";
                                    }
                                    else {
                                        const std::string asset = qt::QStringToString(assetPath);
                                        bool authored = false;

                                        if (arc == CompositionArc::Reference) {
                                            authored = prim.GetReferences().AddReference(SdfReference(asset, primPath));
                                        }
                                        else {
                                            authored = prim.GetPayloads().AddPayload(SdfPayload(asset, primPath));

                                            if (authored)
                                                stage->Load(newPath);
                                        }

                                        if (!authored) {
                                            error = arc == CompositionArc::Reference
                                                        ? QStringLiteral("failed to author reference")
                                                        : QStringLiteral("failed to author payload");
                                        }
                                        else {
                                            TfTokenVector order = state->oldParentOrder;
                                            order.push_back(newPath.GetNameToken());
                                            stage::restoreChildOrder(stage, parentPath, order);

                                            path::appendUnique(changed, parentPath);
                                            path::appendUnique(changed, newPath);
                                            success = true;
                                        }
                                    }

                                    if (!success)
                                        stage::removePrimSpec(editLayer, newPath);
                                }
                            }
                        }
                    }

                    command::queueToSession(session, [session, newPath, changed, success, error, successMessage,
                                                      failureMessage]() {
                        using Status = Session::Notify::Status;

                        command::finishDeferred(session, success ? successMessage : appendError(failureMessage, error),
                                                changed, success ? Status::Success : Status::Error);

                        if (success)
                            session->selectionList()->updatePaths({ newPath });
                    });
                });
            },
            [state, title](Session* session) {
                if (!session || state->createdPath.IsEmpty())
                    return;

                command::beginDeferred(session, QString("Undo %1").arg(title.toLower()), 1);

                command::runWorker([session, state, title]() {
                    bool success = false;
                    QString error;
                    QList<SdfPath> changed;

                    {
                        WRITE_LOCKER(locker, session->stageLock(), "stageLock");
                        const UsdStageRefPtr stage = session->stageUnsafe();

                        if (!stage) {
                            error = "stage missing";
                        }
                        else {
                            QString editError;
                            const SdfLayerHandle editLayer = currentEditLayer(stage, editError);

                            if (!editLayer) {
                                error = editError;
                            }
                            else if (!stage::removePrimSpec(editLayer, state->createdPath)) {
                                error = QString("failed to remove prim: %1").arg(pathText(state->createdPath));
                            }
                            else {
                                stage::restoreChildOrder(stage, state->parentPath, state->oldParentOrder);

                                for (auto it = state->createdAncestorSpecs.crbegin();
                                     it != state->createdAncestorSpecs.crend(); ++it) {
                                    const SdfPrimSpecHandle spec = editLayer->GetPrimAtPath(*it);
                                    if (spec && spec->IsInert())
                                        stage::removePrimSpec(editLayer, *it);
                                }

                                path::appendUnique(changed, state->parentPath);
                                path::appendUnique(changed, state->createdPath);
                                success = true;
                            }
                        }
                    }

                    command::queueToSession(session, [session, state, changed, success, error, title]() {
                        using Status = Session::Notify::Status;

                        command::finishDeferred(session,
                                                success
                                                    ? QString("%1 undone").arg(title.mid(4))
                                                    : appendError(QString("Undo %1 failed").arg(title.mid(4)), error),
                                                changed, success ? Status::Success : Status::Error);

                        if (success) {
                            session->selectionList()->updatePaths(state->previousSelection);
                            session->setMask(state->previousMask);
                        }
                    });
                });
            });
    }

}  // namespace

Command
newReferencePath(const SdfPath& parentPath, const QString& nameInput, const QString& assetPath, const SdfPath& primPath)
{
    return newCompositionArcPath(parentPath, nameInput, assetPath, primPath, CompositionArc::Reference);
}

Command
newPayloadPath(const SdfPath& parentPath, const QString& nameInput, const QString& assetPath, const SdfPath& primPath)
{
    return newCompositionArcPath(parentPath, nameInput, assetPath, primPath, CompositionArc::Payload);
}

}  // namespace stageviz
