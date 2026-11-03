// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#include "commandprim.h"
#include "commandprimprivate.h"
#include "commandtransform.h"
#include "commandutils.h"
#include "primutils.h"
#include "qtutils.h"
#include "selectionlist.h"
#include "tracelocks.h"
#include "usdedit.h"
#include "usdutils.h"
#include <QHash>
#include <QSet>
#include <algorithm>
#include <cmath>
#include <functional>
#include <memory>
#include <optional>
#include <pxr/base/gf/vec2d.h>
#include <pxr/base/gf/vec3f.h>
#include <pxr/usd/sdf/changeBlock.h>
#include <pxr/usd/sdf/copyUtils.h>
#include <pxr/usd/sdf/layer.h>
#include <pxr/usd/sdf/listOp.h>
#include <pxr/usd/sdf/reference.h>
#include <pxr/usd/sdf/schema.h>
#include <pxr/usd/usd/attribute.h>
#include <pxr/usd/usd/editContext.h>
#include <pxr/usd/usd/editTarget.h>
#include <pxr/usd/usd/prim.h>
#include <pxr/usd/usd/primRange.h>
#include <pxr/usd/usd/references.h>
#include <pxr/usd/usdGeom/basisCurves.h>
#include <pxr/usd/usdGeom/cube.h>
#include <pxr/usd/usdGeom/mesh.h>
#include <pxr/usd/usdGeom/metrics.h>
#include <pxr/usd/usdGeom/nurbsCurves.h>
#include <pxr/usd/usdGeom/nurbsPatch.h>
#include <pxr/usd/usdGeom/pointInstancer.h>
#include <pxr/usd/usdGeom/points.h>
#include <pxr/usd/usdGeom/scope.h>
#include <pxr/usd/usdGeom/tokens.h>
#include <pxr/usd/usdGeom/xform.h>
#include <pxr/usd/usdGeom/xformable.h>
#include <pxr/usd/usdShade/material.h>
#include <pxr/usd/usdUtils/dependencies.h>
#include <pxr/usd/usdUtils/flattenLayerStack.h>
#include <vector>

namespace stageviz {

namespace {
    using command::appendError;
    using command::currentEditLayer;
    using command::DependencyPropertyState;
    using command::pathText;
    using command::restoreDependencyProperty;
    using command::snapshotDependencyProperty;
    using command::summarizeErrors;

    bool remapDuplicatedMaterialConnections(UsdStageRefPtr stage, const SdfPath& sourceRoot,
                                            const SdfPath& destinationRoot, QString& error)
    {
        if (!stage || sourceRoot.IsEmpty() || destinationRoot.IsEmpty()) {
            error = "invalid material duplicate roots";
            return false;
        }

        const UsdPrim duplicate = stage->GetPrimAtPath(destinationRoot);
        if (!duplicate || !duplicate.IsA<UsdShadeMaterial>()) {
            error = QString("duplicated material missing: %1").arg(pathText(destinationRoot));
            return false;
        }

        auto remapPaths = [&](SdfPathVector& paths) {
            bool changed = false;
            for (SdfPath& path : paths) {
                if (path == sourceRoot || path.HasPrefix(sourceRoot)) {
                    path = path.ReplacePrefix(sourceRoot, destinationRoot);
                    changed = true;
                }
            }
            return changed;
        };

        for (const UsdPrim& prim : UsdPrimRange(duplicate)) {
            if (!prim || !prim.IsValid())
                continue;

            for (const UsdAttribute& attribute : prim.GetAttributes()) {
                SdfPathVector connections;
                if (!attribute.GetConnections(&connections) || connections.empty())
                    continue;
                if (!remapPaths(connections))
                    continue;
                if (!attribute.SetConnections(connections)) {
                    error = QString("failed to remap duplicated material connection: %1")
                                .arg(pathText(attribute.GetPath()));
                    return false;
                }
            }

            for (const UsdRelationship& relationship : prim.GetRelationships()) {
                SdfPathVector targets;
                if (!relationship.GetTargets(&targets) || targets.empty())
                    continue;
                if (!remapPaths(targets))
                    continue;
                if (!relationship.SetTargets(targets)) {
                    error = QString("failed to remap duplicated material target: %1")
                                .arg(pathText(relationship.GetPath()));
                    return false;
                }
            }
        }

        return true;
    }

    bool pathFallsWithinRoots(const SdfPath& path, const QList<SdfPath>& roots)
    {
        if (path.IsEmpty())
            return false;

        for (const SdfPath& root : roots) {
            if (root.IsEmpty())
                continue;

            if (path == root || path.HasPrefix(root))
                return true;
        }

        return false;
    }

    SdfPathVector filterDependencyPaths(const SdfPathVector& paths, const QList<SdfPath>& removedRoots, bool& changed)
    {
        SdfPathVector filtered;
        filtered.reserve(paths.size());

        for (const SdfPath& path : paths) {
            if (pathFallsWithinRoots(path, removedRoots)) {
                changed = true;
                continue;
            }

            filtered.push_back(path);
        }

        return filtered;
    }

    bool dependencyListOpEmpty(const SdfPathListOp& listOp)
    {
        return listOp.GetExplicitItems().empty() && listOp.GetAddedItems().empty() && listOp.GetPrependedItems().empty()
               && listOp.GetAppendedItems().empty() && listOp.GetDeletedItems().empty()
               && listOp.GetOrderedItems().empty();
    }

    bool filterDependencyListOp(const SdfLayerHandle& layer, const SdfPath& propertyPath, const TfToken& field,
                                const QList<SdfPath>& removedRoots, bool& changed, QString& error)
    {
        if (!layer || propertyPath.IsEmpty() || removedRoots.isEmpty())
            return true;

        if (!layer->HasField(propertyPath, field))
            return true;

        const VtValue value = layer->GetField(propertyPath, field);
        if (!value.IsHolding<SdfPathListOp>())
            return true;

        SdfPathListOp listOp = value.UncheckedGet<SdfPathListOp>();
        bool localChanged = false;

        if (listOp.IsExplicit()) {
            listOp.SetExplicitItems(filterDependencyPaths(listOp.GetExplicitItems(), removedRoots, localChanged));
        }
        else {
            listOp.SetAddedItems(filterDependencyPaths(listOp.GetAddedItems(), removedRoots, localChanged));
            listOp.SetPrependedItems(filterDependencyPaths(listOp.GetPrependedItems(), removedRoots, localChanged));
            listOp.SetAppendedItems(filterDependencyPaths(listOp.GetAppendedItems(), removedRoots, localChanged));
            listOp.SetDeletedItems(filterDependencyPaths(listOp.GetDeletedItems(), removedRoots, localChanged));
            listOp.SetOrderedItems(filterDependencyPaths(listOp.GetOrderedItems(), removedRoots, localChanged));
        }

        if (!localChanged)
            return true;

        if (dependencyListOpEmpty(listOp))
            layer->EraseField(propertyPath, field);
        else
            layer->SetField(propertyPath, field, VtValue(listOp));

        changed = true;
        return true;
    }

    QList<DependencyPropertyState> removeDependenciesToRoots(UsdStageRefPtr stage, const SdfLayerHandle& editLayer,
                                                             const QList<SdfPath>& removedRoots,
                                                             QList<SdfPath>& affected, QStringList& errors)
    {
        QList<DependencyPropertyState> states;
        if (!stage || !editLayer || removedRoots.isEmpty())
            return states;

        // Resolve membership by walking ancestors rather than comparing every
        // property target against every deleted root (potentially O(N*M)).
        const QSet<SdfPath> rootSet(removedRoots.begin(), removedRoots.end());
        const auto isRemoved = [&rootSet](SdfPath path) {
            if (path.IsPropertyPath())
                path = path.GetPrimPath();
            while (!path.IsEmpty() && path != SdfPath::AbsoluteRootPath()) {
                if (rootSet.contains(path))
                    return true;
                path = path.GetParentPath();
            }
            return false;
        };

        const auto filterPaths = [&isRemoved](const SdfPathVector& paths, bool& changed) {
            SdfPathVector filtered;
            filtered.reserve(paths.size());
            for (const SdfPath& path : paths) {
                if (isRemoved(path)) {
                    changed = true;
                    continue;
                }
                filtered.push_back(path);
            }
            return filtered;
        };

        const auto filterField = [&](const SdfPath& propertyPath, const TfToken& field, bool& changed) {
            if (!editLayer->HasField(propertyPath, field))
                return;
            const VtValue value = editLayer->GetField(propertyPath, field);
            if (!value.IsHolding<SdfPathListOp>())
                return;
            SdfPathListOp listOp = value.UncheckedGet<SdfPathListOp>();
            bool localChanged = false;
            if (listOp.IsExplicit()) {
                listOp.SetExplicitItems(filterPaths(listOp.GetExplicitItems(), localChanged));
            }
            else {
                listOp.SetAddedItems(filterPaths(listOp.GetAddedItems(), localChanged));
                listOp.SetPrependedItems(filterPaths(listOp.GetPrependedItems(), localChanged));
                listOp.SetAppendedItems(filterPaths(listOp.GetAppendedItems(), localChanged));
                listOp.SetDeletedItems(filterPaths(listOp.GetDeletedItems(), localChanged));
                listOp.SetOrderedItems(filterPaths(listOp.GetOrderedItems(), localChanged));
            }
            if (!localChanged)
                return;
            if (dependencyListOpEmpty(listOp))
                editLayer->EraseField(propertyPath, field);
            else
                editLayer->SetField(propertyPath, field, VtValue(listOp));
            changed = true;
        };

        // Only snapshot properties that actually refer to removed paths.
        // This avoids creating an anonymous SdfLayer for every unrelated
        // relationship or attribute in a large scene.
        const auto fieldHasRemovedPath = [&](const SdfPath& propertyPath, const TfToken& field) {
            if (!editLayer->HasField(propertyPath, field))
                return false;
            const VtValue value = editLayer->GetField(propertyPath, field);
            if (!value.IsHolding<SdfPathListOp>())
                return false;
            const SdfPathListOp& op = value.UncheckedGet<SdfPathListOp>();
            const auto anyRemoved = [&isRemoved](const SdfPathVector& paths) {
                return std::any_of(paths.begin(), paths.end(), isRemoved);
            };
            return anyRemoved(op.GetExplicitItems()) || anyRemoved(op.GetAddedItems())
                   || anyRemoved(op.GetPrependedItems()) || anyRemoved(op.GetAppendedItems())
                   || anyRemoved(op.GetDeletedItems()) || anyRemoved(op.GetOrderedItems());
        };
        for (const UsdPrim& prim : stage->Traverse()) {
            if (!prim || !prim.IsValid() || prim.IsInstanceProxy() || isRemoved(prim.GetPath()))
                continue;

            const auto processProperty = [&](const SdfPath& propertyPath) {
                if (!editLayer->GetPropertyAtPath(propertyPath))
                    return;
                if (!fieldHasRemovedPath(propertyPath, SdfFieldKeys->TargetPaths)
                    && !fieldHasRemovedPath(propertyPath, SdfFieldKeys->ConnectionPaths))
                    return;

                DependencyPropertyState state;
                QString error;
                if (!snapshotDependencyProperty(editLayer, propertyPath, state, error)) {
                    errors.append(error);
                    return;
                }
                bool changed = false;
                filterField(propertyPath, SdfFieldKeys->TargetPaths, changed);
                filterField(propertyPath, SdfFieldKeys->ConnectionPaths, changed);
                if (changed) {
                    states.append(state);
                    path::appendUnique(affected, propertyPath.GetPrimPath());
                }
            };
            for (const UsdRelationship& relationship : prim.GetRelationships())
                processProperty(relationship.GetPath());
            for (const UsdAttribute& attribute : prim.GetAttributes())
                processProperty(attribute.GetPath());
        }
        return states;
    }
}  // namespace

namespace snapshot {
    struct DeleteState {
        QVector<PrimState> prims;
        QHash<SdfPath, TfTokenVector> parentOrders;
        QList<DependencyPropertyState> dependencies;
        SdfPath previousDefaultPrimPath;
        QList<SdfPath> previousSelection;
        QList<SdfPath> previousMask;
    };
}  // namespace snapshot

Command
deletePaths(const QList<SdfPath>& inPaths)
{
    auto state = std::make_shared<snapshot::DeleteState>();

    return Command(
        [inPaths, state](Session* session) {
            state->previousSelection = session->selectionList()->paths();
            state->previousMask = session->mask();
            state->previousDefaultPrimPath = SdfPath();
            command::beginDeferred(session, "Delete paths", 1);

            command::runWorker([session, inPaths, state]() {
                bool success = false;
                QList<SdfPath> changed;
                QList<SdfPath> removedPaths;
                QString error;

                {
                    WRITE_LOCKER(locker, session->stageLock(), "stageLock");

                    const UsdStageRefPtr stage = session->stageUnsafe();
                    if (stage) {
                        QString editError;
                        const SdfLayerHandle editLayer = currentEditLayer(stage, editError);
                        if (!editLayer) {
                            error = editError;
                        }
                        else {
                            QList<SdfPath> editable;
                            QStringList rejected;

                            const QList<SdfPath> candidates = path::minimalRootPaths(path::uniquePaths(inPaths));
                            editable.reserve(candidates.size());

                            for (const SdfPath& candidate : candidates) {
                                const SdfPath primPath = candidate.IsPropertyPath() ? candidate.GetPrimPath()
                                                                                    : candidate;

                                QString pathError;
                                if (layer::validatePrim(stage, editLayer, primPath, pathError)) {
                                    editable.append(primPath);
                                }
                                else {
                                    rejected.append(pathError);
                                }
                            }

                            const QList<SdfPath> paths = path::minimalRootPaths(editable);

                            state->prims.clear();
                            state->parentOrders.clear();
                            state->dependencies.clear();

                            if (UsdPrim defaultPrim = stage->GetDefaultPrim())
                                state->previousDefaultPrimPath = defaultPrim.GetPath();

                            bool deletesDefaultPrim = false;
                            if (!state->previousDefaultPrimPath.IsEmpty()) {
                                for (const SdfPath& path : paths) {
                                    if (state->previousDefaultPrimPath == path
                                        || state->previousDefaultPrimPath.HasPrefix(path)) {
                                        deletesDefaultPrim = true;
                                        break;
                                    }
                                }
                            }

                            QSet<SdfPath> changedSet;

                            for (const SdfPath& path : paths) {
                                changedSet.insert(path);

                                const SdfPath parentPath = path.GetParentPath();
                                if (!parentPath.IsEmpty()) {
                                    changedSet.insert(parentPath);

                                    if (!state->parentOrders.contains(parentPath)) {
                                        TfTokenVector order;
                                        if (stage::captureChildOrder(stage, parentPath, order))
                                            state->parentOrders.insert(parentPath, order);
                                    }
                                }
                            }

                            // Snapshot before editing: USD stage queries must not occur while
                            // an SdfChangeBlock is open (composition is deferred).
                            QList<snapshot::PrimState> snapshots;
                            QList<SdfPath> snapshotPaths;
                            snapshots.reserve(paths.size());
                            snapshotPaths.reserve(paths.size());
                            for (const SdfPath& path : paths) {
                                snapshot::PrimState primState;
                                if (!snapshot::capturePrimToLayer(stage, path, primState)) {
                                    rejected.append(QString("failed to snapshot prim: %1").arg(pathText(path)));
                                    continue;
                                }
                                snapshots.append(primState);
                                snapshotPaths.append(path);
                            }

                            if (deletesDefaultPrim)
                                stage->ClearDefaultPrim();

                            UsdEditContext context(stage, UsdEditTarget(editLayer));
                            {
                                // Batch only Sdf-layer mutations. Do not perform UsdStage
                                // queries or dependency discovery inside this block.
                                SdfChangeBlock changeBlock;
                                for (qsizetype i = 0; i < snapshots.size(); ++i) {
                                    const auto& primState = snapshots.at(i);
                                    if (!stage::removePrimSpec(editLayer, primState.specPath)) {
                                        rejected.append(QString("failed to remove edit-layer spec: %1")
                                                            .arg(pathText(primState.specPath)));
                                        continue;
                                    }
                                    state->prims.append(primState);
                                    removedPaths.append(snapshotPaths.at(i));
                                }
                            }
                            const bool removedAny = !removedPaths.isEmpty();
                            if (removedAny) {
                                QList<SdfPath> dependencyAffected;
                                QStringList dependencyErrors;
                                state->dependencies = removeDependenciesToRoots(stage, editLayer, removedPaths,
                                                                                dependencyAffected, dependencyErrors);

                                for (const SdfPath& dependencyPath : dependencyAffected)
                                    changedSet.insert(dependencyPath);

                                for (const QString& dependencyError : dependencyErrors)
                                    rejected.append(dependencyError);
                            }
                            if (removedAny) {
                                changed = changedSet.values();
                                success = true;
                            }

                            if (!rejected.isEmpty()) {
                                error = summarizeErrors(rejected);
                                if (success)
                                    error = QString("Some paths skipped: %1").arg(error);
                            }
                        }
                    }
                    else {
                        error = "stage missing";
                    }
                }
                command::queueToSession(session, [session, state, changed, removedPaths, success, error]() {
                    using Status = Session::Notify::Status;

                    session->selectionList()->updatePaths(
                        path::removeAffectedPaths(session->selectionList()->paths(), removedPaths));
                    session->setMask(path::removeAffectedPaths(session->mask(), removedPaths));
                    command::finishDeferred(session,
                                            success ? (error.isEmpty() ? QStringLiteral("Paths deleted")
                                                                       : QString("Paths deleted (%1)").arg(error))
                                                    : appendError(QStringLiteral("Delete paths failed"), error),
                                            success ? changed : removedPaths,
                                            success ? Status::Success : Status::Error);
                });
            });
        },
        [state](Session* session) {
            command::beginDeferred(session, "Undo delete paths", 1);

            command::runWorker([session, state]() {
                bool success = false;
                QList<SdfPath> changed;
                QString error;

                {
                    WRITE_LOCKER(locker, session->stageLock(), "stageLock");

                    const UsdStageRefPtr stage = session->stageUnsafe();
                    if (stage) {
                        QString editError;
                        const SdfLayerHandle editLayer = currentEditLayer(stage, editError);
                        if (!editLayer) {
                            error = editError;
                        }
                        else {
                            UsdEditContext context(stage, UsdEditTarget(editLayer));
                            snapshot::sortByHierarchy(state->prims);

                            QSet<SdfPath> changedSet;

                            for (const auto& primState : state->prims) {
                                snapshot::restorePrimFromSnapshotLayer(editLayer, primState);
                                changedSet.insert(primState.stagePath);

                                const SdfPath parentPath = primState.stagePath.GetParentPath();
                                if (!parentPath.IsEmpty() && parentPath != SdfPath::AbsoluteRootPath())
                                    changedSet.insert(parentPath);
                            }

                            for (const DependencyPropertyState& dependency : state->dependencies) {
                                QString dependencyError;
                                if (!restoreDependencyProperty(editLayer, dependency, dependencyError)) {
                                    error = dependencyError;
                                    continue;
                                }

                                changedSet.insert(dependency.propertyPath.GetPrimPath());
                            }

                            for (auto it = state->parentOrders.cbegin(); it != state->parentOrders.cend(); ++it) {
                                stage::restoreChildOrder(stage, it.key(), it.value());
                                changedSet.insert(it.key());
                            }

                            if (!state->previousDefaultPrimPath.IsEmpty()) {
                                const UsdPrim defaultPrim = stage->GetPrimAtPath(state->previousDefaultPrimPath);
                                if (defaultPrim)
                                    stage->SetDefaultPrim(defaultPrim);
                            }

                            changed = changedSet.values();
                            success = error.isEmpty();
                        }
                    }
                    else {
                        error = "stage missing";
                    }
                }

                command::queueToSession(session, [session, state, changed, success, error]() {
                    using Status = Session::Notify::Status;

                    if (success) {
                        session->selectionList()->updatePaths(state->previousSelection);
                        session->setMask(state->previousMask);
                    }
                    command::finishDeferred(session,
                                            success ? QStringLiteral("Delete undone")
                                                    : appendError(QStringLiteral("Undo delete paths failed"), error),
                                            changed, success ? Status::Success : Status::Error);
                });
            });
        });
}

Command
command::copyPathsCommand(const QList<SdfPath>& inPaths, bool selectCopies, std::optional<GfMatrix4d> worldDelta)
{
    struct CopyState {
        struct Item {
            SdfPath sourcePath;
            SdfPath destinationPath;
            SdfPath parentPath;
        };

        QList<Item> items;
        QHash<SdfPath, TfTokenVector> oldParentOrders;
        QSet<SdfPath> createdParentSpecs;
        QList<SdfPath> previousSelection;
        QList<SdfPath> previousMask;
    };

    auto state = std::make_shared<CopyState>();

    return Command(
        [inPaths, selectCopies, state, worldDelta](Session* session) {
            if (!session || inPaths.isEmpty())
                return;

            state->previousSelection = session->selectionList()->paths();
            state->previousMask = session->mask();

            command::beginDeferred(session, "Copy paths", 1);

            command::runWorker([session, inPaths, selectCopies, state, worldDelta]() {
                bool success = false;
                QList<SdfPath> copiedPaths;
                QList<SdfPath> changed;
                QStringList errors;

                {
                    WRITE_LOCKER(locker, session->stageLock(), "stageLock");
                    const UsdStageRefPtr stage = session->stageUnsafe();

                    if (!stage) {
                        errors.append("stage missing");
                    }
                    else {
                        QString editError;
                        const SdfLayerHandle editLayer = currentEditLayer(stage, editError);

                        if (!editLayer) {
                            errors.append(editError);
                        }
                        else {
                            state->items.clear();
                            state->oldParentOrders.clear();
                            state->createdParentSpecs.clear();

                            QList<SdfPath> paths = path::minimalRootPaths(path::uniquePaths(inPaths));
                            TransformOperation operation;
                            if (worldDelta) {
                                QString error;
                                if (!beginTransformOperation(stage, inPaths, operation, error)) {
                                    errors.append(error);
                                    paths.clear();
                                }
                                else {
                                    paths = operation.paths.mid(0, operation.targetCount);
                                }
                            }

                            for (const SdfPath& inputPath : paths) {
                                const SdfPath sourcePath = inputPath.IsPropertyPath() ? inputPath.GetPrimPath()
                                                                                      : inputPath;
                                const UsdPrim prim = stage->GetPrimAtPath(sourcePath);

                                if (!prim || !prim.IsValid() || prim.IsInstanceProxy()) {
                                    errors.append(QString("invalid prim: %1").arg(pathText(sourcePath)));
                                    continue;
                                }

                                const SdfPrimSpecHandleVector primStack = prim.GetPrimStack();
                                if (primStack.empty() || !primStack.front()) {
                                    errors.append(QString("prim has no authored spec: %1").arg(pathText(sourcePath)));
                                    continue;
                                }

                                const SdfPrimSpecHandle sourceSpec = primStack.front();
                                SdfLayerHandle sourceLayer = sourceSpec->GetLayer();
                                SdfPath sourceSpecPath = sourceSpec->GetPath();
                                SdfLayerRefPtr composedSource;
                                if (worldDelta) {
                                    // Compose only this subtree's local layer opinions. Copying
                                    // the strongest spec alone can copy just a transform over.
                                    // Keep references/payloads and native instances intact.
                                    const auto root = SdfLayer::CreateAnonymous("stageviz_copy.usda");
                                    std::vector<SdfLayerRefPtr> layers;
                                    std::vector<std::string> identifiers;
                                    std::vector<SdfLayerOffset> offsets;
                                    for (const auto& layer : stage->GetLayerStack()) {
                                        if (!layer->GetPrimAtPath(sourcePath))
                                            continue;
                                        const auto snapshot = SdfLayer::CreateAnonymous("stageviz_copy_source.usda");
                                        if (sourcePath.GetParentPath().IsPrimPath())
                                            SdfCreatePrimInLayer(snapshot, sourcePath.GetParentPath());
                                        if (!SdfCopySpec(layer, sourcePath, snapshot, sourcePath))
                                            continue;
                                        UsdUtilsModifyAssetPaths(snapshot, [&](const std::string& asset) {
                                            return UsdUtilsFlattenLayerStackResolveAssetPath(layer, asset);
                                        });
                                        layers.push_back(snapshot);
                                        identifiers.push_back(snapshot->GetIdentifier());
                                        offsets.push_back(
                                            stage->GetEditTargetForLocalLayer(layer).GetMapFunction().GetTimeOffset());
                                    }
                                    root->SetSubLayerPaths(identifiers);
                                    for (size_t i = 0; i < offsets.size(); ++i)
                                        root->SetSubLayerOffset(offsets[i], int(i));
                                    const auto snapshotStage = UsdStage::OpenMasked(root, UsdStagePopulationMask(),
                                                                                    UsdStage::LoadNone);
                                    composedSource = snapshotStage ? UsdUtilsFlattenLayerStack(snapshotStage) : nullptr;
                                    if (!composedSource || !composedSource->GetPrimAtPath(sourcePath)) {
                                        errors.append(QString("failed to compose copy: %1").arg(pathText(sourcePath)));
                                        continue;
                                    }
                                    sourceLayer = composedSource;
                                    sourceSpecPath = sourcePath;
                                }

                                if (!sourceLayer) {
                                    errors.append(QString("source layer missing: %1").arg(pathText(sourcePath)));
                                    continue;
                                }

                                const SdfPath parentPath = sourcePath.GetParentPath();
                                if (parentPath.IsEmpty()) {
                                    errors.append(QString("invalid parent: %1").arg(pathText(sourcePath)));
                                    continue;
                                }

                                if (parentPath != SdfPath::AbsoluteRootPath()) {
                                    QString parentError;
                                    if (!layer::validateParent(stage, editLayer, parentPath, parentError)) {
                                        errors.append(parentError);
                                        continue;
                                    }
                                }

                                const QString sourceName = qt::StringToQString(sourcePath.GetName());
                                const SdfPath destinationPath = stage::buildChildPath(stage, parentPath, sourceName,
                                                                                      editError);

                                if (destinationPath.IsEmpty()) {
                                    errors.append(
                                        editError.isEmpty()
                                            ? QString("failed to build copy path: %1").arg(pathText(sourcePath))
                                            : editError);
                                    editError.clear();
                                    continue;
                                }

                                if (!state->oldParentOrders.contains(parentPath)) {
                                    TfTokenVector order;
                                    stage::captureChildOrder(stage, parentPath, order);
                                    state->oldParentOrders.insert(parentPath, order);
                                }

                                if (parentPath != SdfPath::AbsoluteRootPath()
                                    && !editLayer->GetPrimAtPath(parentPath)) {
                                    if (!SdfCreatePrimInLayer(editLayer, parentPath)) {
                                        errors.append(
                                            QString("failed to create parent override: %1").arg(pathText(parentPath)));
                                        continue;
                                    }
                                    state->createdParentSpecs.insert(parentPath);
                                }

                                if (!SdfCopySpec(sourceLayer, sourceSpecPath, editLayer, destinationPath)) {
                                    errors.append(QString("failed to copy prim spec: %1").arg(pathText(sourcePath)));
                                    continue;
                                }

                                // Explicitly remap connections/targets that point inside the
                                // source material so the copy is a self-contained network
                                // rather than referencing the original material hierarchy.
                                if (prim.IsA<UsdShadeMaterial>()) {
                                    QString remapError;
                                    if (!remapDuplicatedMaterialConnections(stage, sourcePath, destinationPath,
                                                                            remapError)) {
                                        stage::removePrimSpec(editLayer, destinationPath);
                                        errors.append(remapError);
                                        continue;
                                    }
                                }

                                if (worldDelta) {
                                    const qsizetype index = operation.paths.indexOf(sourcePath);
                                    QString error;
                                    if (!stage::setWorldTransform(stage, destinationPath,
                                                                  operation.edit.before.at(index) * *worldDelta,
                                                                  error)) {
                                        stage::removePrimSpec(editLayer, destinationPath);
                                        errors.append(error);
                                        continue;
                                    }
                                }

                                CopyState::Item item;
                                item.sourcePath = sourcePath;
                                item.destinationPath = destinationPath;
                                item.parentPath = parentPath;
                                state->items.append(item);

                                TfTokenVector order;
                                stage::captureChildOrder(stage, parentPath, order);
                                if (std::find(order.begin(), order.end(), destinationPath.GetNameToken())
                                    == order.end()) {
                                    order.push_back(destinationPath.GetNameToken());
                                    stage::restoreChildOrder(stage, parentPath, order);
                                }

                                path::appendUnique(copiedPaths, destinationPath);
                                path::appendUnique(changed, sourcePath);
                                path::appendUnique(changed, destinationPath);
                                path::appendUnique(changed, parentPath);
                            }

                            success = !state->items.isEmpty();
                        }
                    }
                }

                const QString errorText = summarizeErrors(errors);
                command::queueToSession(session, [session, copiedPaths, changed, success, errorText, selectCopies]() {
                    using Status = Session::Notify::Status;

                    command::finishDeferred(session,
                                            success ? (errorText.isEmpty()
                                                           ? QStringLiteral("Paths copied")
                                                           : appendError("Paths copied with errors", errorText))
                                                    : appendError("Copy paths failed", errorText),
                                            changed, success ? Status::Success : Status::Error);

                    if (success && selectCopies)
                        session->selectionList()->updatePaths(copiedPaths);
                });
            });
        },
        [state](Session* session) {
            if (!session || state->items.isEmpty())
                return;

            command::beginDeferred(session, "Undo copy paths", 1);

            command::runWorker([session, state]() {
                bool success = true;
                QList<SdfPath> changed;
                QStringList errors;

                {
                    WRITE_LOCKER(locker, session->stageLock(), "stageLock");
                    const UsdStageRefPtr stage = session->stageUnsafe();

                    if (!stage) {
                        success = false;
                        errors.append("stage missing");
                    }
                    else {
                        QString editError;
                        const SdfLayerHandle editLayer = currentEditLayer(stage, editError);

                        if (!editLayer) {
                            success = false;
                            errors.append(editError);
                        }
                        else {
                            for (auto it = state->items.crbegin(); it != state->items.crend(); ++it) {
                                if (!stage::removePrimSpec(editLayer, it->destinationPath)) {
                                    success = false;
                                    errors.append(
                                        QString("failed to remove copy: %1").arg(pathText(it->destinationPath)));
                                    continue;
                                }

                                path::appendUnique(changed, it->destinationPath);
                                path::appendUnique(changed, it->parentPath);
                            }

                            for (auto it = state->oldParentOrders.cbegin(); it != state->oldParentOrders.cend(); ++it)
                                stage::restoreChildOrder(stage, it.key(), it.value());

                            QList<SdfPath> createdParents = state->createdParentSpecs.values();
                            std::sort(createdParents.begin(), createdParents.end(),
                                      [](const SdfPath& a, const SdfPath& b) {
                                          return a.GetPathElementCount() > b.GetPathElementCount();
                                      });

                            for (const SdfPath& parentPath : createdParents) {
                                const SdfPrimSpecHandle parentSpec = editLayer->GetPrimAtPath(parentPath);
                                if (parentSpec && parentSpec->IsInert())
                                    stage::removePrimSpec(editLayer, parentPath);
                            }
                        }
                    }
                }

                const QString errorText = summarizeErrors(errors);
                command::queueToSession(session, [session, state, changed, success, errorText]() {
                    using Status = Session::Notify::Status;

                    command::finishDeferred(session,
                                            success ? "Copy undone" : appendError("Undo copy paths failed", errorText),
                                            changed, success ? Status::Success : Status::Error);

                    if (success) {
                        session->selectionList()->updatePaths(state->previousSelection);
                        session->setMask(state->previousMask);
                    }
                });
            });
        });
}

Command
copyPaths(const QList<SdfPath>& paths, bool selectCopies)
{
    return command::copyPathsCommand(paths, selectCopies, std::nullopt);
}

Command
instancePaths(const QList<SdfPath>& inPaths)
{
    struct InstanceState {
        struct Item {
            SdfPath destinationPath;
            SdfPath parentPath;
        };

        QList<Item> items;
        QHash<SdfPath, TfTokenVector> oldParentOrders;
        QSet<SdfPath> createdParentSpecs;
        QList<SdfPath> previousSelection;
        QList<SdfPath> previousMask;
    };

    auto state = std::make_shared<InstanceState>();

    return Command(
        [inPaths, state](Session* session) {
            if (!session || inPaths.isEmpty())
                return;

            state->previousSelection = session->selectionList()->paths();
            state->previousMask = session->mask();
            command::beginDeferred(session, "Instance paths", 1);

            command::runWorker([session, inPaths, state]() {
                bool success = false;
                QList<SdfPath> instancePaths;
                QList<SdfPath> changed;
                QStringList errors;

                {
                    WRITE_LOCKER(locker, session->stageLock(), "stageLock");
                    const UsdStageRefPtr stage = session->stageUnsafe();

                    if (!stage) {
                        errors.append("stage missing");
                    }
                    else {
                        QString editError;
                        const SdfLayerHandle editLayer = currentEditLayer(stage, editError);

                        if (!editLayer) {
                            errors.append(editError);
                        }
                        else {
                            state->items.clear();
                            state->oldParentOrders.clear();
                            state->createdParentSpecs.clear();

                            const QList<SdfPath> paths = path::minimalRootPaths(path::uniquePaths(inPaths));
                            bool hasLeafGeometryPrim = false;
                            for (const SdfPath& inputPath : paths) {
                                const SdfPath sourcePath = inputPath.IsPropertyPath() ? inputPath.GetPrimPath()
                                                                                      : inputPath;
                                const UsdPrim sourcePrim = stage->GetPrimAtPath(sourcePath);
                                if (sourcePrim && sourcePrim.IsValid() && !sourcePrim.IsInstanceProxy()
                                    && !stage::canUseAsNativeInstanceRoot(sourcePrim)) {
                                    errors.append(
                                        QString(
                                            "cannot instance leaf geometry prim: %1; select an enclosing Xform or Scope")
                                            .arg(pathText(sourcePath)));
                                    hasLeafGeometryPrim = true;
                                }
                            }

                            const QList<SdfPath> instanceSourcePaths = hasLeafGeometryPrim ? QList<SdfPath>() : paths;
                            for (const SdfPath& inputPath : instanceSourcePaths) {
                                const SdfPath sourcePath = inputPath.IsPropertyPath() ? inputPath.GetPrimPath()
                                                                                      : inputPath;
                                const UsdPrim sourcePrim = stage->GetPrimAtPath(sourcePath);

                                if (!sourcePrim || !sourcePrim.IsValid() || sourcePrim.IsInstanceProxy()) {
                                    errors.append(QString("invalid prim: %1").arg(pathText(sourcePath)));
                                    continue;
                                }

                                const SdfPath parentPath = sourcePath.GetParentPath();
                                if (parentPath.IsEmpty()) {
                                    errors.append(QString("invalid parent: %1").arg(pathText(sourcePath)));
                                    continue;
                                }

                                if (parentPath != SdfPath::AbsoluteRootPath()
                                    && !layer::validateParent(stage, editLayer, parentPath, editError)) {
                                    errors.append(editError);
                                    editError.clear();
                                    continue;
                                }

                                const QString sourceName = qt::StringToQString(sourcePath.GetName());
                                const SdfPath destinationPath = stage::buildChildPath(stage, parentPath, sourceName,
                                                                                      editError);
                                if (destinationPath.IsEmpty()) {
                                    errors.append(
                                        editError.isEmpty()
                                            ? QString("failed to build instance path: %1").arg(pathText(sourcePath))
                                            : editError);
                                    editError.clear();
                                    continue;
                                }

                                if (!state->oldParentOrders.contains(parentPath)) {
                                    TfTokenVector order;
                                    stage::captureChildOrder(stage, parentPath, order);
                                    state->oldParentOrders.insert(parentPath, order);
                                }

                                if (parentPath != SdfPath::AbsoluteRootPath()
                                    && !editLayer->GetPrimAtPath(parentPath)) {
                                    if (!SdfCreatePrimInLayer(editLayer, parentPath)) {
                                        errors.append(
                                            QString("failed to create parent override: %1").arg(pathText(parentPath)));
                                        continue;
                                    }
                                    state->createdParentSpecs.insert(parentPath);
                                }

                                UsdPrim instancePrim;
                                {
                                    UsdEditContext context(stage, UsdEditTarget(editLayer));
                                    instancePrim = stage->OverridePrim(destinationPath);
                                    if (instancePrim && instancePrim.IsValid()) {
                                        const bool referenced = instancePrim.GetReferences().AddReference(
                                            SdfReference(std::string(), sourcePath));
                                        if (!referenced || !instancePrim.SetInstanceable(true))
                                            instancePrim = UsdPrim();
                                    }
                                }

                                if (!instancePrim || !instancePrim.IsValid()) {
                                    stage::removePrimSpec(editLayer, destinationPath);
                                    errors.append(QString("failed to author instance: %1").arg(pathText(sourcePath)));
                                    continue;
                                }

                                InstanceState::Item item;
                                item.destinationPath = destinationPath;
                                item.parentPath = parentPath;
                                state->items.append(item);

                                TfTokenVector order;
                                stage::captureChildOrder(stage, parentPath, order);
                                if (std::find(order.begin(), order.end(), destinationPath.GetNameToken())
                                    == order.end()) {
                                    order.push_back(destinationPath.GetNameToken());
                                    stage::restoreChildOrder(stage, parentPath, order);
                                }

                                path::appendUnique(instancePaths, destinationPath);
                                path::appendUnique(changed, sourcePath);
                                path::appendUnique(changed, destinationPath);
                                path::appendUnique(changed, parentPath);
                            }

                            success = !state->items.isEmpty();
                        }
                    }
                }

                const QString errorText = summarizeErrors(errors);
                command::queueToSession(session, [session, instancePaths, changed, success, errorText]() {
                    using Status = Session::Notify::Status;
                    command::finishDeferred(session,
                                            success ? (errorText.isEmpty()
                                                           ? QStringLiteral("Instances created")
                                                           : appendError("Instances created with errors", errorText))
                                                    : appendError("Instance paths failed", errorText),
                                            changed, success ? Status::Success : Status::Error);
                    if (success)
                        session->selectionList()->updatePaths(instancePaths);
                });
            });
        },
        [state](Session* session) {
            if (!session || state->items.isEmpty())
                return;

            command::beginDeferred(session, "Undo instance paths", 1);
            command::runWorker([session, state]() {
                bool success = true;
                QList<SdfPath> changed;
                QStringList errors;

                {
                    WRITE_LOCKER(locker, session->stageLock(), "stageLock");
                    const UsdStageRefPtr stage = session->stageUnsafe();
                    if (!stage) {
                        success = false;
                        errors.append("stage missing");
                    }
                    else {
                        QString editError;
                        const SdfLayerHandle editLayer = currentEditLayer(stage, editError);
                        if (!editLayer) {
                            success = false;
                            errors.append(editError);
                        }
                        else {
                            for (auto it = state->items.crbegin(); it != state->items.crend(); ++it) {
                                if (!stage::removePrimSpec(editLayer, it->destinationPath)) {
                                    success = false;
                                    errors.append(
                                        QString("failed to remove instance: %1").arg(pathText(it->destinationPath)));
                                    continue;
                                }
                                path::appendUnique(changed, it->destinationPath);
                                path::appendUnique(changed, it->parentPath);
                            }

                            for (auto it = state->oldParentOrders.cbegin(); it != state->oldParentOrders.cend(); ++it)
                                stage::restoreChildOrder(stage, it.key(), it.value());

                            QList<SdfPath> createdParents = state->createdParentSpecs.values();
                            std::sort(createdParents.begin(), createdParents.end(),
                                      [](const SdfPath& a, const SdfPath& b) {
                                          return a.GetPathElementCount() > b.GetPathElementCount();
                                      });
                            for (const SdfPath& parentPath : createdParents) {
                                const SdfPrimSpecHandle parentSpec = editLayer->GetPrimAtPath(parentPath);
                                if (parentSpec && parentSpec->IsInert())
                                    stage::removePrimSpec(editLayer, parentPath);
                            }
                        }
                    }
                }

                const QString errorText = summarizeErrors(errors);
                command::queueToSession(session, [session, state, changed, success, errorText]() {
                    using Status = Session::Notify::Status;
                    command::finishDeferred(session,
                                            success ? QStringLiteral("Instances undone")
                                                    : appendError("Undo instances failed", errorText),
                                            changed, success ? Status::Success : Status::Error);
                    if (success) {
                        session->selectionList()->updatePaths(state->previousSelection);
                        session->setMask(state->previousMask);
                    }
                });
            });
        });
}

namespace {
    GfVec3f stageAlignedPoint(const UsdStageRefPtr& stage, const GfVec3f& point)
    {
        if (!stage || UsdGeomGetStageUpAxis(stage) != UsdGeomTokens->y)
            return point;

        // Sample geometry below is authored in a Z-up convention. Rotate it
        // into Y-up space when the stage uses Y as its up axis.
        return GfVec3f(point[0], point[2], -point[1]);
    }

    bool authorStageAxis(const UsdStageRefPtr& stage, const UsdPrim& prim, const TfToken& typeName, QString& error)
    {
        if (!stage || !prim) {
            error = "stage or prim missing";
            return false;
        }

        if (typeName != TfToken("Plane") && typeName != TfToken("Cylinder") && typeName != TfToken("Cone")
            && typeName != TfToken("Capsule")) {
            return true;
        }

        const TfToken axis = UsdGeomGetStageUpAxis(stage);
        UsdAttribute axisAttr = prim.GetAttribute(UsdGeomTokens->axis);
        if (!axisAttr)
            axisAttr = prim.CreateAttribute(UsdGeomTokens->axis, SdfValueTypeNames->Token, false,
                                            SdfVariabilityUniform);

        if (!axisAttr || !axisAttr.Set(axis)) {
            error = QString("failed to set stage axis on %1").arg(pathText(prim.GetPath()));
            return false;
        }

        return true;
    }


    using PrimAuthor = std::function<bool(const UsdStageRefPtr&, const SdfPath&, QString&)>;

    Command newAuthoredPrimPath(const SdfPath& parentPath, const QString& nameInput, const QString& label,
                                PrimAuthor author, bool selectCreated)
    {
        struct NewAuthoredPrimState {
            SdfPath parentPath;
            SdfPath createdPath;
            TfTokenVector oldParentOrder;
            QList<SdfPath> createdAncestorSpecs;
            QList<SdfPath> previousSelection;
            QList<SdfPath> previousMask;
        };

        auto state = std::make_shared<NewAuthoredPrimState>();

        return Command(
            [parentPath, nameInput, label, author, selectCreated, state](Session* session) {
                if (!session || parentPath.IsEmpty())
                    return;

                state->previousSelection = session->selectionList()->paths();
                state->previousMask = session->mask();

                command::beginDeferred(session, QString("New %1").arg(label), 1);

                command::runWorker([session, parentPath, nameInput, label, author, selectCreated, state]() {
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
                                    success = author(stage, newPath, error);

                                    if (success) {
                                        TfTokenVector order = state->oldParentOrder;
                                        order.push_back(newPath.GetNameToken());
                                        stage::restoreChildOrder(stage, parentPath, order);

                                        path::appendUnique(changed, parentPath);
                                        path::appendUnique(changed, newPath);
                                    }
                                    else {
                                        stage::removePrimSpec(editLayer, newPath);
                                        stage::restoreChildOrder(stage, parentPath, state->oldParentOrder);

                                        for (auto it = state->createdAncestorSpecs.crbegin();
                                             it != state->createdAncestorSpecs.crend(); ++it) {
                                            const SdfPrimSpecHandle spec = editLayer->GetPrimAtPath(*it);
                                            if (spec && spec->IsInert())
                                                stage::removePrimSpec(editLayer, *it);
                                        }
                                    }
                                }
                            }
                        }
                    }

                    command::queueToSession(session, [session, newPath, changed, success, error, label,
                                                      selectCreated]() {
                        using Status = Session::Notify::Status;
                        command::finishDeferred(session,
                                                success ? QString("%1 created").arg(label)
                                                        : appendError(QString("New %1 failed").arg(label), error),
                                                changed, success ? Status::Success : Status::Error);
                        if (success && selectCreated)
                            session->selectionList()->updatePaths({ newPath });
                    });
                });
            },
            [state, label](Session* session) {
                if (!session || state->createdPath.IsEmpty())
                    return;

                command::beginDeferred(session, QString("Undo new %1").arg(label), 1);

                command::runWorker([session, state, label]() {
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

                                path::appendUnique(changed, state->createdPath);
                                success = true;
                            }
                        }
                    }

                    command::queueToSession(session, [session, state, changed, success, error, label]() {
                        using Status = Session::Notify::Status;
                        command::finishDeferred(session,
                                                success ? QString("New %1 undone").arg(label)
                                                        : appendError(QString("Undo new %1 failed").arg(label), error),
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
command::createPrimCommand(const SdfPath& parentPath, const QString& nameInput, const TfToken& typeName,
                           bool selectCreated, std::function<bool(const UsdPrim&, QString&)> initialize)
{
    struct NewPrimState {
        SdfPath parentPath;
        SdfPath createdPath;
        TfTokenVector oldParentOrder;
        QList<SdfPath> createdAncestorSpecs;
        QList<SdfPath> previousSelection;
        QList<SdfPath> previousMask;
    };

    auto state = std::make_shared<NewPrimState>();

    return Command(
        [parentPath, nameInput, typeName, selectCreated, state, initialize](Session* session) {
            if (!session || parentPath.IsEmpty())
                return;

            state->previousSelection = session->selectionList()->paths();
            state->previousMask = session->mask();

            command::beginDeferred(session, "New prim", 1);

            command::runWorker([session, parentPath, nameInput, typeName, selectCreated, state, initialize]() {
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

                                for (SdfPath path = parentPath; !path.IsEmpty() && path != SdfPath::AbsoluteRootPath();
                                     path = path.GetParentPath()) {
                                    if (!editLayer->GetPrimAtPath(path))
                                        state->createdAncestorSpecs.prepend(path);
                                }

                                UsdEditContext context(stage, UsdEditTarget(editLayer));
                                const UsdPrim prim = typeName.IsEmpty() ? stage->DefinePrim(newPath)
                                                                        : stage->DefinePrim(newPath, typeName);

                                if (!prim || !prim.IsValid()) {
                                    error = "define failed";
                                }
                                else if (!authorStageAxis(stage, prim, typeName, error)
                                         || (initialize && !initialize(prim, error))) {
                                    stage::removePrimSpec(editLayer, newPath);
                                }
                                else {
                                    TfTokenVector order = state->oldParentOrder;
                                    order.push_back(newPath.GetNameToken());
                                    stage::restoreChildOrder(stage, parentPath, order);

                                    path::appendUnique(changed, newPath);
                                    success = true;
                                }
                            }
                        }
                    }
                }

                command::queueToSession(session, [session, newPath, changed, success, error, selectCreated]() {
                    using Status = Session::Notify::Status;

                    command::finishDeferred(session, success ? "Prim created" : appendError("New prim failed", error),
                                            changed, success ? Status::Success : Status::Error);

                    if (success && selectCreated)
                        session->selectionList()->updatePaths({ newPath });
                });
            });
        },
        [state](Session* session) {
            if (!session || state->createdPath.IsEmpty())
                return;

            command::beginDeferred(session, "Undo new prim", 1);

            command::runWorker([session, state]() {
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

                command::queueToSession(session, [session, state, changed, success, error]() {
                    using Status = Session::Notify::Status;

                    command::finishDeferred(session,
                                            success ? "New prim undone" : appendError("Undo new prim failed", error),
                                            changed, success ? Status::Success : Status::Error);

                    if (success) {
                        session->selectionList()->updatePaths(state->previousSelection);
                        session->setMask(state->previousMask);
                    }
                });
            });
        });
}

Command
newPrimPath(const SdfPath& parentPath, const QString& nameInput, const TfToken& typeName, bool selectCreated)
{
    return command::createPrimCommand(parentPath, nameInput, typeName, selectCreated);
}

Command
newScopePath(const SdfPath& parentPath, const QString& nameInput, bool selectCreated)
{
    return newPrimPath(parentPath, nameInput, TfToken("Scope"), selectCreated);
}

Command
newMeshPath(const SdfPath& parentPath, const QString& nameInput, bool selectCreated)
{
    return newAuthoredPrimPath(
        parentPath, nameInput, "mesh",
        [](const UsdStageRefPtr& stage, const SdfPath& path, QString& error) {
            const UsdGeomMesh mesh = UsdGeomMesh::Define(stage, path);
            if (!mesh) {
                error = "failed to define UsdGeomMesh";
                return false;
            }

            constexpr int width = 11;
            constexpr int height = 11;
            constexpr float spacing = 0.5f;
            VtVec3fArray points;
            VtIntArray counts;
            VtIntArray indices;
            points.reserve(width * height);
            counts.reserve((width - 1) * (height - 1));
            indices.reserve((width - 1) * (height - 1) * 4);

            for (int y = 0; y < height; ++y) {
                for (int x = 0; x < width; ++x) {
                    const float fx = (float(x) - float(width - 1) * 0.5f) * spacing;
                    const float fy = (float(y) - float(height - 1) * 0.5f) * spacing;
                    const float z = std::sin(fx * 1.35f) * std::cos(fy * 1.15f) * 0.45f;
                    points.push_back(stageAlignedPoint(stage, GfVec3f(fx, fy, z)));
                }
            }

            for (int y = 0; y < height - 1; ++y) {
                for (int x = 0; x < width - 1; ++x) {
                    const int a = y * width + x;
                    const int b = a + 1;
                    const int c = a + width + 1;
                    const int d = a + width;
                    counts.push_back(4);
                    indices.push_back(a);
                    indices.push_back(b);
                    indices.push_back(c);
                    indices.push_back(d);
                }
            }

            return mesh.CreatePointsAttr().Set(points) && mesh.CreateFaceVertexCountsAttr().Set(counts)
                   && mesh.CreateFaceVertexIndicesAttr().Set(indices)
                   && mesh.CreateSubdivisionSchemeAttr().Set(UsdGeomTokens->none);
        },
        selectCreated);
}

Command
newPointsPath(const SdfPath& parentPath, const QString& nameInput, bool selectCreated)
{
    return newAuthoredPrimPath(
        parentPath, nameInput, "points",
        [](const UsdStageRefPtr& stage, const SdfPath& path, QString& error) {
            const UsdGeomPoints pointsPrim = UsdGeomPoints::Define(stage, path);
            if (!pointsPrim) {
                error = "failed to define UsdGeomPoints";
                return false;
            }

            VtVec3fArray points;
            VtFloatArray widths;
            points.reserve(75);
            widths.reserve(75);
            for (int z = -2; z <= 2; ++z) {
                for (int y = -2; y <= 2; ++y) {
                    for (int x = -1; x <= 1; ++x) {
                        const float jitter = 0.12f * std::sin(float(x * 13 + y * 7 + z * 3));
                        points.push_back(
                            stageAlignedPoint(stage, GfVec3f(float(x) * 0.7f + jitter, float(y) * 0.7f - jitter,
                                                             float(z) * 0.7f + jitter * 0.5f)));
                        widths.push_back(0.14f + 0.03f * float((x + y + z + 9) % 3));
                    }
                }
            }

            return pointsPrim.CreatePointsAttr().Set(points) && pointsPrim.CreateWidthsAttr().Set(widths);
        },
        selectCreated);
}

Command
newBasisCurvesPath(const SdfPath& parentPath, const QString& nameInput, bool selectCreated)
{
    return newAuthoredPrimPath(
        parentPath, nameInput, "basis curves",
        [](const UsdStageRefPtr& stage, const SdfPath& path, QString& error) {
            const UsdGeomBasisCurves curves = UsdGeomBasisCurves::Define(stage, path);
            if (!curves) {
                error = "failed to define UsdGeomBasisCurves";
                return false;
            }

            constexpr int curveCount = 3;
            constexpr int pointsPerCurve = 12;
            VtVec3fArray points;
            VtIntArray counts(curveCount, pointsPerCurve);
            VtFloatArray widths(curveCount * pointsPerCurve, 0.08f);
            points.reserve(curveCount * pointsPerCurve);

            for (int curve = 0; curve < curveCount; ++curve) {
                for (int i = 0; i < pointsPerCurve; ++i) {
                    const float x = (float(i) - 5.5f) * 0.45f;
                    const float y = (float(curve) - 1.0f) * 0.55f;
                    const float z = std::sin(x * 1.7f + float(curve) * 0.8f) * 0.45f;
                    points.push_back(stageAlignedPoint(stage, GfVec3f(x, y, z)));
                }
            }

            return curves.CreateTypeAttr().Set(UsdGeomTokens->cubic)
                   && curves.CreateBasisAttr().Set(UsdGeomTokens->bspline)
                   && curves.CreateWrapAttr().Set(UsdGeomTokens->nonperiodic)
                   && curves.CreateCurveVertexCountsAttr().Set(counts) && curves.CreatePointsAttr().Set(points)
                   && curves.CreateWidthsAttr().Set(widths);
        },
        selectCreated);
}

Command
newNurbsCurvesPath(const SdfPath& parentPath, const QString& nameInput, bool selectCreated)
{
    return newAuthoredPrimPath(
        parentPath, nameInput, "NURBS curves",
        [](const UsdStageRefPtr& stage, const SdfPath& path, QString& error) {
            const UsdGeomNurbsCurves curves = UsdGeomNurbsCurves::Define(stage, path);
            if (!curves) {
                error = "failed to define UsdGeomNurbsCurves";
                return false;
            }

            constexpr int curveCount = 2;
            constexpr int pointsPerCurve = 8;
            VtVec3fArray points;
            VtIntArray counts(curveCount, pointsPerCurve);
            VtIntArray orders(curveCount, 4);
            VtDoubleArray knots;
            VtVec2dArray ranges(curveCount, GfVec2d(0.0, 5.0));
            VtFloatArray widths(curveCount * pointsPerCurve, 0.09f);
            points.reserve(curveCount * pointsPerCurve);
            knots.reserve(curveCount * (pointsPerCurve + 4));

            for (int curve = 0; curve < curveCount; ++curve) {
                for (int i = 0; i < pointsPerCurve; ++i) {
                    const float x = (float(i) - 3.5f) * 0.65f;
                    const float y = (float(curve) - 0.5f) * 0.9f;
                    const float z = std::cos(x * 1.35f + float(curve)) * 0.55f;
                    points.push_back(stageAlignedPoint(stage, GfVec3f(x, y, z)));
                }
                for (double knot : { 0.0, 0.0, 0.0, 0.0, 1.0, 2.0, 3.0, 4.0, 5.0, 5.0, 5.0, 5.0 })
                    knots.push_back(knot);
            }

            return curves.CreateCurveVertexCountsAttr().Set(counts) && curves.CreatePointsAttr().Set(points)
                   && curves.CreateOrderAttr().Set(orders) && curves.CreateKnotsAttr().Set(knots)
                   && curves.CreateRangesAttr().Set(ranges) && curves.CreateWidthsAttr().Set(widths);
        },
        selectCreated);
}

Command
newNurbsPatchPath(const SdfPath& parentPath, const QString& nameInput, bool selectCreated)
{
    return newAuthoredPrimPath(
        parentPath, nameInput, "NURBS patch",
        [](const UsdStageRefPtr& stage, const SdfPath& path, QString& error) {
            const UsdGeomNurbsPatch patch = UsdGeomNurbsPatch::Define(stage, path);
            if (!patch) {
                error = "failed to define UsdGeomNurbsPatch";
                return false;
            }

            constexpr int uCount = 5;
            constexpr int vCount = 5;
            VtVec3fArray points;
            points.reserve(uCount * vCount);
            for (int v = 0; v < vCount; ++v) {
                for (int u = 0; u < uCount; ++u) {
                    const float x = (float(u) - 2.0f) * 0.75f;
                    const float y = (float(v) - 2.0f) * 0.75f;
                    const float z = std::sin(x * 1.25f) * std::cos(y * 1.25f) * 0.5f;
                    points.push_back(stageAlignedPoint(stage, GfVec3f(x, y, z)));
                }
            }

            const VtDoubleArray knots { 0.0, 0.0, 0.0, 0.0, 1.0, 2.0, 2.0, 2.0, 2.0 };
            return patch.CreateUVertexCountAttr().Set(uCount) && patch.CreateVVertexCountAttr().Set(vCount)
                   && patch.CreateUOrderAttr().Set(4) && patch.CreateVOrderAttr().Set(4)
                   && patch.CreateUKnotsAttr().Set(knots) && patch.CreateVKnotsAttr().Set(knots)
                   && patch.CreateUFormAttr().Set(UsdGeomTokens->open)
                   && patch.CreateVFormAttr().Set(UsdGeomTokens->open)
                   && patch.CreateURangeAttr().Set(GfVec2d(0.0, 2.0)) && patch.CreateVRangeAttr().Set(GfVec2d(0.0, 2.0))
                   && patch.CreatePointsAttr().Set(points);
        },
        selectCreated);
}

Command
newPointInstancerPath(const SdfPath& parentPath, const QString& nameInput, bool selectCreated)
{
    return newAuthoredPrimPath(
        parentPath, nameInput, "point instancer",
        [](const UsdStageRefPtr& stage, const SdfPath& path, QString& error) {
            const UsdGeomPointInstancer instancer = UsdGeomPointInstancer::Define(stage, path);
            if (!instancer) {
                error = "failed to define UsdGeomPointInstancer";
                return false;
            }

            const SdfPath prototypesPath = path.AppendChild(TfToken("Prototypes"));
            const SdfPath cubePath = prototypesPath.AppendChild(TfToken("Cube"));
            if (!UsdGeomScope::Define(stage, prototypesPath) || !UsdGeomCube::Define(stage, cubePath)) {
                error = "failed to create point-instancer prototype";
                return false;
            }

            VtIntArray protoIndices;
            VtVec3fArray positions;
            constexpr int count = 25;
            protoIndices.reserve(count);
            positions.reserve(count);
            for (int i = 0; i < count; ++i) {
                const int x = i % 5;
                const int y = i / 5;
                protoIndices.push_back(0);
                positions.push_back(
                    stageAlignedPoint(stage, GfVec3f((float(x) - 2.0f) * 1.25f, (float(y) - 2.0f) * 1.25f,
                                                     0.4f * std::sin(float(i) * 0.8f))));
            }

            return instancer.CreatePrototypesRel().SetTargets({ cubePath })
                   && instancer.CreateProtoIndicesAttr().Set(protoIndices)
                   && instancer.CreatePositionsAttr().Set(positions);
        },
        selectCreated);
}

Command
renamePath(const SdfPath& path, const QString& newNameInput)
{
    struct RenameState {
        SdfPath oldPath;
        SdfPath newPath;
        SdfPath parentPath;
        TfTokenVector oldOrder;
        TfTokenVector newOrder;
        QList<SdfPath> previousSelection;
        QList<SdfPath> previousMask;
    };

    auto state = std::make_shared<RenameState>();

    return Command(
        [path, newNameInput, state](Session* session) {
            if (!session || path.IsEmpty())
                return;

            state->previousSelection = session->selectionList()->paths();
            state->previousMask = session->mask();

            command::beginDeferred(session, "Rename path", 1);

            command::runWorker([=]() {
                bool hadStage = true;
                bool renamed = false;
                bool noop = false;
                QString error;
                SdfPath newPath;

                {
                    WRITE_LOCKER(locker, session->stageLock(), "stageLock");
                    const UsdStageRefPtr stage = session->stageUnsafe();

                    if (!stage) {
                        hadStage = false;
                    }
                    else {
                        QString editError;
                        const SdfLayerHandle editLayer = currentEditLayer(stage, editError);
                        if (!editLayer) {
                            error = editError;
                        }
                        else if (!layer::validatePrim(stage, editLayer, path, error)) {}
                        else {
                            newPath = stage::buildRenamePath(stage, path, newNameInput, error);

                            if (newPath.IsEmpty()) {}
                            else if (newPath == path) {
                                noop = true;
                            }
                            else {
                                state->oldPath = path;
                                state->newPath = newPath;
                                state->parentPath = path.GetParentPath();
                                state->oldOrder.clear();
                                state->newOrder.clear();

                                if (!state->parentPath.IsEmpty())
                                    stage::captureChildOrder(stage, state->parentPath, state->oldOrder);

                                UsdEditContext context(stage, UsdEditTarget(editLayer));
                                edit::NamespaceEditor namespaceEditor(stage, stage->GetEditTarget());
                                if (namespaceEditor.renamePrim(path, newPath, error)) {
                                    if (!state->oldOrder.empty()) {
                                        state->newOrder = stage::remapChildOrder(state->oldOrder, path.GetNameToken(),
                                                                                 newPath.GetNameToken());

                                        stage::restoreChildOrder(stage, state->parentPath, state->newOrder);
                                    }

                                    renamed = true;
                                }
                            }
                        }
                    }
                }

                command::queueToSession(session, [=]() {
                    using Status = Session::Notify::Status;

                    if (!hadStage) {
                        command::finishDeferred(session, "Rename path failed: stage missing", { path }, Status::Error);
                        return;
                    }

                    if (noop) {
                        command::finishDeferred(session, "Rename skipped", { path }, Status::Success);
                        return;
                    }

                    if (!renamed) {
                        command::finishDeferred(session, appendError("Rename path failed", error), { path },
                                                Status::Error);
                        return;
                    }

                    command::finishDeferred(session, "Path renamed", { path, newPath }, Status::Success);

                    session->selectionList()->updatePaths(
                        path::remapAffectedPaths(state->previousSelection, path, newPath));
                    session->setMask(path::remapAffectedPaths(state->previousMask, path, newPath));
                });
            });
        },
        [state](Session* session) {
            if (!session || state->oldPath.IsEmpty() || state->newPath.IsEmpty())
                return;

            command::beginDeferred(session, "Undo rename path", 1);

            command::runWorker([=]() {
                bool hadStage = true;
                bool restored = false;
                QString error;

                {
                    WRITE_LOCKER(locker, session->stageLock(), "stageLock");
                    const UsdStageRefPtr stage = session->stageUnsafe();

                    if (!stage) {
                        hadStage = false;
                    }
                    else {
                        QString editError;
                        const SdfLayerHandle editLayer = currentEditLayer(stage, editError);
                        if (!editLayer) {
                            error = editError;
                        }
                        else {
                            UsdEditContext context(stage, UsdEditTarget(editLayer));
                            edit::NamespaceEditor namespaceEditor(stage, stage->GetEditTarget());
                            if (namespaceEditor.renamePrim(state->newPath, state->oldPath, error)) {
                                if (!state->oldOrder.empty() && !state->parentPath.IsEmpty()
                                    && state->parentPath != SdfPath::AbsoluteRootPath()) {
                                    stage::restoreChildOrder(stage, state->parentPath, state->oldOrder);
                                }

                                restored = true;
                            }
                        }
                    }
                }

                command::queueToSession(session, [=]() {
                    using Status = Session::Notify::Status;

                    if (!hadStage) {
                        command::finishDeferred(session, "Undo rename path failed: stage missing", {}, Status::Error);
                        return;
                    }

                    if (!restored) {
                        command::finishDeferred(session, appendError("Undo rename path failed", error), {},
                                                Status::Error);
                        return;
                    }

                    command::finishDeferred(session, "Rename undone", { state->oldPath }, Status::Success);

                    session->selectionList()->updatePaths(state->previousSelection);
                    session->setMask(state->previousMask);
                });
            });
        });
}

Command
newXformPath(const SdfPath& parentPath, const QString& nameInput, bool selectCreated)
{
    struct MoveItem {
        SdfPath oldPath;
        SdfPath newPath;
        SdfPath oldParentPath;
    };

    struct NewXformState {
        SdfPath parentPath;
        SdfPath createdPath;
        TfTokenVector oldParentOrder;
        TfTokenVector newParentOrder;
        QHash<SdfPath, TfTokenVector> oldMoveParentOrders;
        QList<MoveItem> movedItems;
        QList<SdfPath> previousSelection;
        QList<SdfPath> previousMask;
    };

    auto state = std::make_shared<NewXformState>();

    return Command(
        [parentPath, nameInput, selectCreated, state](Session* session) {
            if (!session || parentPath.IsEmpty())
                return;

            state->previousSelection = session->selectionList()->paths();
            state->previousMask = session->mask();

            command::beginDeferred(session, "New xform", 1);

            command::runWorker([=]() {
                bool hadStage = true;
                bool created = false;
                bool noop = false;
                QString error;
                SdfPath newPath;
                QList<SdfPath> movePaths;
                QList<SdfPath> changed;

                {
                    WRITE_LOCKER(locker, session->stageLock(), "stageLock");
                    const UsdStageRefPtr stage = session->stageUnsafe();

                    if (!stage) {
                        hadStage = false;
                    }
                    else {
                        const QList<SdfPath> selection = path::minimalRootPaths(state->previousSelection);

                        if (selection.size() > 1)
                            movePaths = selection;

                        QString editError;
                        const SdfLayerHandle editLayer = currentEditLayer(stage, editError);
                        if (!editLayer) {
                            error = editError;
                        }
                        else if (!layer::validateParent(stage, editLayer, parentPath, error)) {}
                        else {
                            newPath = stage::buildChildPath(stage, parentPath, nameInput, error);

                            if (newPath.IsEmpty()) {
                                noop = true;
                            }
                            else {
                                state->parentPath = parentPath;
                                state->createdPath = newPath;
                                state->oldParentOrder.clear();
                                state->newParentOrder.clear();
                                state->oldMoveParentOrders.clear();
                                state->movedItems.clear();
                                const bool parentIsRoot = parentPath == SdfPath::AbsoluteRootPath();
                                stage::captureChildOrder(stage, parentPath, state->oldParentOrder);

                                UsdEditContext context(stage, UsdEditTarget(editLayer));
                                const UsdGeomXform xform = UsdGeomXform::Define(stage, newPath);
                                if (!xform || !xform.GetPrim()) {
                                    error = "define failed";
                                }
                                else {
                                    state->newParentOrder = state->oldParentOrder;
                                    state->newParentOrder.push_back(newPath.GetNameToken());
                                    stage::restoreChildOrder(stage, parentPath, state->newParentOrder);

                                    bool movedSelection = true;

                                    for (const SdfPath& movePath : movePaths) {
                                        if (movePath.IsEmpty() || movePath == SdfPath::AbsoluteRootPath()
                                            || movePath == newPath || newPath.HasPrefix(movePath)) {
                                            error = "invalid move path";
                                            movedSelection = false;
                                            break;
                                        }

                                        const SdfPath moveParentPath = movePath.GetParentPath();
                                        if (moveParentPath != SdfPath::AbsoluteRootPath()
                                            && stage::isInsideCompositionArc(stage, moveParentPath)) {
                                            error = "cannot move into or out of composed prims";
                                            movedSelection = false;
                                            break;
                                        }

                                        QString authoredError;
                                        if (!layer::validatePrim(stage, editLayer, movePath, authoredError)) {
                                            error = authoredError;
                                            movedSelection = false;
                                            break;
                                        }

                                        const SdfPath oldParentPath = movePath.GetParentPath();
                                        if (oldParentPath.IsEmpty()) {
                                            error = "invalid source parent";
                                            movedSelection = false;
                                            break;
                                        }

                                        if (!state->oldMoveParentOrders.contains(oldParentPath)) {
                                            TfTokenVector order;
                                            stage::captureChildOrder(stage, oldParentPath, order);
                                            state->oldMoveParentOrders.insert(oldParentPath, order);
                                        }

                                        MoveItem item;
                                        item.oldPath = movePath;
                                        item.oldParentPath = oldParentPath;
                                        item.newPath = newPath.AppendChild(movePath.GetNameToken());
                                        state->movedItems.append(item);
                                    }

                                    if (movedSelection) {
                                        QList<QPair<SdfPath, SdfPath>> moves;
                                        moves.reserve(state->movedItems.size());
                                        for (const MoveItem& item : state->movedItems)
                                            moves.append(qMakePair(item.oldPath, item.newPath));

                                        QString moveError;
                                        edit::NamespaceEditor namespaceEditor(stage, stage->GetEditTarget());
                                        if (!namespaceEditor.reparentPrims(moves, moveError)) {
                                            error = moveError.isEmpty() ? "failed to move selected paths" : moveError;
                                            stage::restoreChildOrders(stage, state->oldMoveParentOrders);
                                            stage::restoreChildOrder(stage, parentPath, state->oldParentOrder);
                                            stage::removePrimSpec(editLayer, newPath);
                                            movedSelection = false;
                                        }
                                    }

                                    if (movedSelection && error.isEmpty()) {
                                        path::appendUnique(changed, parentPath);
                                        path::appendUnique(changed, newPath);

                                        for (const MoveItem& item : state->movedItems) {
                                            path::appendUnique(changed, item.oldParentPath);
                                            path::appendUnique(changed, item.oldPath);
                                            path::appendUnique(changed, item.newPath);
                                        }

                                        created = true;
                                    }
                                }
                            }
                        }
                    }
                }

                command::queueToSession(session, [=]() {
                    using Status = Session::Notify::Status;

                    if (!hadStage) {
                        command::finishDeferred(session, "New xform failed", {}, Status::Error);
                        return;
                    }

                    if (noop) {
                        command::finishDeferred(session,
                                                error.isEmpty() ? "New xform skipped"
                                                                : QString("New xform skipped: %1").arg(error),
                                                {}, Status::Success);
                        return;
                    }

                    if (!created) {
                        command::finishDeferred(session,
                                                error.isEmpty() ? "New xform failed"
                                                                : QString("New xform failed: %1").arg(error),
                                                changed, Status::Error);
                        return;
                    }

                    command::finishDeferred(session,
                                            state->movedItems.isEmpty() ? "Xform created"
                                                                        : "Xform created and paths moved",
                                            changed, Status::Success);

                    if (selectCreated) {
                        session->selectionList()->updatePaths({ newPath });
                    }
                    else if (!state->movedItems.isEmpty()) {
                        QList<SdfPath> remappedSelection = state->previousSelection;
                        for (SdfPath& selectedPath : remappedSelection) {
                            for (const MoveItem& item : state->movedItems) {
                                if (selectedPath == item.oldPath || selectedPath.HasPrefix(item.oldPath)) {
                                    selectedPath = selectedPath.ReplacePrefix(item.oldPath, item.newPath);
                                    break;
                                }
                            }
                        }
                        session->selectionList()->updatePaths(remappedSelection);
                    }
                });
            });
        },
        [state](Session* session) {
            if (!session || state->createdPath.IsEmpty())
                return;

            command::beginDeferred(session, "Undo new xform", 1);

            command::runWorker([=]() {
                bool hadStage = true;
                bool restored = false;
                QString error;
                QList<SdfPath> changed;

                {
                    WRITE_LOCKER(locker, session->stageLock(), "stageLock");
                    const UsdStageRefPtr stage = session->stageUnsafe();

                    if (!stage) {
                        hadStage = false;
                    }
                    else {
                        QString editError;
                        const SdfLayerHandle editLayer = currentEditLayer(stage, editError);
                        if (!editLayer) {
                            error = editError;
                        }
                        else {
                            UsdEditContext context(stage, UsdEditTarget(editLayer));
                            QList<QPair<SdfPath, SdfPath>> reverseMoves;
                            reverseMoves.reserve(state->movedItems.size());
                            for (auto it = state->movedItems.crbegin(); it != state->movedItems.crend(); ++it)
                                reverseMoves.append(qMakePair(it->newPath, it->oldPath));

                            edit::NamespaceEditor namespaceEditor(stage, stage->GetEditTarget());
                            restored = namespaceEditor.reparentPrims(reverseMoves, error);

                            if (restored) {
                                for (auto it = state->oldMoveParentOrders.cbegin();
                                     it != state->oldMoveParentOrders.cend(); ++it) {
                                    stage::restoreChildOrder(stage, it.key(), it.value());
                                }

                                if (!state->parentPath.IsEmpty())
                                    stage::restoreChildOrder(stage, state->parentPath, state->oldParentOrder);

                                restored = stage::removePrimSpec(editLayer, state->createdPath);
                            }

                            if (restored) {
                                path::appendUnique(changed, state->parentPath);
                                path::appendUnique(changed, state->createdPath);

                                for (const MoveItem& item : state->movedItems) {
                                    path::appendUnique(changed, item.oldParentPath);
                                    path::appendUnique(changed, item.oldPath);
                                    path::appendUnique(changed, item.newPath);
                                }
                            }
                        }
                    }
                }

                command::queueToSession(session, [=]() {
                    using Status = Session::Notify::Status;

                    if (!hadStage) {
                        command::finishDeferred(session, "Undo new xform failed", {}, Status::Error);
                        return;
                    }

                    if (!restored) {
                        command::finishDeferred(session,
                                                error.isEmpty() ? "Undo new xform failed"
                                                                : QString("Undo new xform failed: %1").arg(error),
                                                changed, Status::Error);
                        return;
                    }

                    command::finishDeferred(session, "New xform undone", changed, Status::Success);

                    session->selectionList()->updatePaths(state->previousSelection);
                    session->setMask(state->previousMask);
                });
            });
        });
}

Command
movePath(const QList<SdfPath>& paths, const SdfPath& newParentPath, int insertIndex, bool preserveTransform)
{
    struct MoveItem {
        SdfPath oldPath;
        SdfPath newPath;
        SdfPath oldParentPath;
        TfToken name;
        GfMatrix4d worldTransform { 1.0 };
        bool hasWorldTransform = false;
    };

    struct MoveState {
        QList<MoveItem> items;
        SdfPath newParentPath;
        int insertIndex = -1;
        QHash<SdfPath, TfTokenVector> oldParentOrders;
        QHash<SdfPath, TfTokenVector> newParentOrders;
        QList<SdfPath> previousSelection;
        QList<SdfPath> previousMask;
    };

    auto state = std::make_shared<MoveState>();

    return Command(
        [paths, newParentPath, insertIndex, preserveTransform, state](Session* session) {
            if (!session || paths.isEmpty() || newParentPath.IsEmpty())
                return;

            state->previousSelection = session->selectionList()->paths();
            state->previousMask = session->mask();

            command::beginDeferred(session, "Move paths", 1);

            command::runWorker([=]() {
                bool hadStage = true;
                bool moved = false;
                bool noop = false;
                QString error;
                QList<SdfPath> changed;

                {
                    WRITE_LOCKER(locker, session->stageLock(), "stageLock");
                    const UsdStageRefPtr stage = session->stageUnsafe();

                    if (!stage) {
                        hadStage = false;
                    }
                    else {
                        const QList<SdfPath> movePaths = path::minimalRootPaths(paths);

                        if (movePaths.isEmpty()) {
                            noop = true;
                        }
                        else if (newParentPath != SdfPath::AbsoluteRootPath()
                                 && stage::isInsideCompositionArc(stage, newParentPath)) {
                            error = "cannot move into or out of composed prims";
                        }
                        else {
                            QString editError;
                            const SdfLayerHandle editLayer = currentEditLayer(stage, editError);
                            if (!editLayer) {
                                error = editError;
                            }
                            else if (!layer::validateParent(stage, editLayer, newParentPath, error)) {}
                            else {
                                state->items.clear();
                                state->newParentPath = newParentPath;
                                state->insertIndex = insertIndex;
                                state->oldParentOrders.clear();
                                state->newParentOrders.clear();

                                QSet<SdfPath> sourcePaths;
                                QSet<SdfPath> targetPaths;
                                bool valid = true;

                                for (const SdfPath& path : movePaths) {
                                    const SdfPath oldParentPath = path.GetParentPath();
                                    const SdfPath targetPath = newParentPath.AppendChild(path.GetNameToken());

                                    sourcePaths.insert(path);

                                    if (path.IsEmpty() || path == SdfPath::AbsoluteRootPath()
                                        || oldParentPath.IsEmpty()) {
                                        error = "invalid source path";
                                        valid = false;
                                        break;
                                    }

                                    if (newParentPath == path || newParentPath.HasPrefix(path)) {
                                        error = "cannot move a prim below itself";
                                        valid = false;
                                        break;
                                    }

                                    if (oldParentPath != SdfPath::AbsoluteRootPath()
                                        && stage::isInsideCompositionArc(stage, oldParentPath)) {
                                        error = "cannot move into or out of composed prims";
                                        valid = false;
                                        break;
                                    }

                                    QString authoredError;
                                    if (!layer::validatePrim(stage, editLayer, path, authoredError)) {
                                        error = authoredError;
                                        valid = false;
                                        break;
                                    }

                                    if (targetPaths.contains(targetPath)) {
                                        error = "multiple moved prims resolve to the same destination";
                                        valid = false;
                                        break;
                                    }

                                    const UsdPrim targetPrim = stage->GetPrimAtPath(targetPath);
                                    if (targetPrim && targetPath != path && !sourcePaths.contains(targetPath)) {
                                        error = QString("destination already exists: %1")
                                                    .arg(qt::SdfPathToQString(targetPath));
                                        valid = false;
                                        break;
                                    }

                                    targetPaths.insert(targetPath);

                                    MoveItem item;
                                    item.oldPath = path;
                                    item.newPath = targetPath;
                                    item.oldParentPath = oldParentPath;
                                    item.name = path.GetNameToken();

                                    if (preserveTransform && oldParentPath != newParentPath) {
                                        const UsdPrim prim = stage->GetPrimAtPath(path);
                                        const UsdGeomXformable xformable(prim);

                                        if (xformable) {
                                            QString transformError;
                                            if (!stage::worldTransform(stage, path, item.worldTransform,
                                                                       transformError)) {
                                                error = transformError.isEmpty()
                                                            ? QString("failed to capture world transform: %1")
                                                                  .arg(qt::SdfPathToQString(path))
                                                            : transformError;
                                                valid = false;
                                                break;
                                            }
                                            item.hasWorldTransform = true;
                                        }
                                    }

                                    state->items.append(item);

                                    if (!state->oldParentOrders.contains(oldParentPath)) {
                                        TfTokenVector order;
                                        stage::captureChildOrder(stage, oldParentPath, order);
                                        state->oldParentOrders.insert(oldParentPath, order);
                                    }

                                    if (!state->oldParentOrders.contains(newParentPath)) {
                                        TfTokenVector order;
                                        stage::captureChildOrder(stage, newParentPath, order);
                                        state->oldParentOrders.insert(newParentPath, order);
                                    }
                                }

                                if (valid && !noop) {
                                    TfTokenVector movedNames;
                                    movedNames.reserve(state->items.size());

                                    for (const MoveItem& item : state->items)
                                        movedNames.push_back(item.name);

                                    UsdEditContext context(stage, UsdEditTarget(editLayer));

                                    QList<QPair<SdfPath, SdfPath>> moves;
                                    moves.reserve(state->items.size());
                                    for (const MoveItem& item : state->items) {
                                        if (item.oldPath != item.newPath)
                                            moves.append(qMakePair(item.oldPath, item.newPath));
                                    }

                                    // A same-parent drag can be a pure reorder. Avoid invoking
                                    // UsdNamespaceEditor unless at least one prim actually changes
                                    // namespace; on large composed stages even a no-op namespace
                                    // edit can be much more expensive than updating child order.
                                    if (!moves.isEmpty()) {
                                        QString moveError;
                                        edit::NamespaceEditor namespaceEditor(stage, stage->GetEditTarget());
                                        if (!namespaceEditor.reparentPrims(moves, moveError)) {
                                            error = moveError.isEmpty() ? "failed to move paths" : moveError;
                                            stage::restoreChildOrders(stage, state->oldParentOrders);
                                            moved = false;
                                        }
                                    }

                                    if (error.isEmpty() && preserveTransform) {
                                        for (const MoveItem& item : state->items) {
                                            if (!item.hasWorldTransform)
                                                continue;

                                            QString transformError;
                                            if (!stage::setWorldTransform(stage, item.newPath, item.worldTransform,
                                                                          transformError)) {
                                                error = transformError.isEmpty()
                                                            ? QString("failed to preserve world transform: %1")
                                                                  .arg(qt::SdfPathToQString(item.newPath))
                                                            : transformError;
                                                break;
                                            }
                                        }
                                    }

                                    if (error.isEmpty()) {
                                        QSet<SdfPath> affectedParents;

                                        for (const MoveItem& item : state->items)
                                            affectedParents.insert(item.oldParentPath);

                                        affectedParents.insert(newParentPath);

                                        for (const SdfPath& parentPath : affectedParents) {
                                            TfTokenVector order = state->oldParentOrders.value(parentPath);
                                            TfTokenVector newOrder = path::removeTokens(order, movedNames);

                                            if (parentPath == newParentPath)
                                                newOrder = path::insertTokens(newOrder, movedNames, insertIndex);

                                            state->newParentOrders.insert(parentPath, newOrder);
                                            stage::restoreChildOrder(stage, parentPath, newOrder);
                                        }

                                        moved = state->newParentOrders != state->oldParentOrders;

                                        for (const MoveItem& item : state->items) {
                                            if (item.oldPath != item.newPath) {
                                                moved = true;
                                                break;
                                            }
                                        }

                                        if (!moved) {
                                            noop = true;
                                        }
                                        else {
                                            QSet<SdfPath> changedSet;

                                            for (const MoveItem& item : state->items) {
                                                changedSet.insert(item.oldPath);
                                                changedSet.insert(item.newPath);
                                                changedSet.insert(item.oldParentPath);
                                            }

                                            changedSet.insert(newParentPath);
                                            changed = changedSet.values();
                                        }
                                    }
                                }
                            }
                        }
                    }
                }

                command::queueToSession(session, [=]() {
                    using Status = Session::Notify::Status;

                    if (!hadStage) {
                        command::finishDeferred(session, "Move paths failed", {}, Status::Error);
                        return;
                    }

                    if (noop) {
                        command::finishDeferred(session, "Move skipped", {}, Status::Success);
                        return;
                    }

                    if (!moved) {
                        command::finishDeferred(session,
                                                error.isEmpty() ? "Move paths failed"
                                                                : QString("Move paths failed: %1").arg(error),
                                                changed, Status::Error);
                        return;
                    }

                    command::finishDeferred(session, state->items.size() == 1 ? "Path moved" : "Paths moved", changed,
                                            Status::Success);

                    QList<SdfPath> selection = state->previousSelection;
                    QList<SdfPath> mask = state->previousMask;

                    QHash<SdfPath, SdfPath> remaps;
                    remaps.reserve(state->items.size());
                    for (const MoveItem& item : state->items) {
                        if (item.oldPath != item.newPath)
                            remaps.insert(item.oldPath, item.newPath);
                    }

                    auto remapPaths = [&remaps](QList<SdfPath>& paths) {
                        for (SdfPath& inputPath : paths) {
                            const SdfPath primPath = inputPath.IsPropertyPath() ? inputPath.GetPrimPath() : inputPath;
                            for (SdfPath ancestor = primPath;
                                 !ancestor.IsEmpty() && ancestor != SdfPath::AbsoluteRootPath();
                                 ancestor = ancestor.GetParentPath()) {
                                const auto remap = remaps.constFind(ancestor);
                                if (remap == remaps.cend())
                                    continue;

                                inputPath = remap.value().AppendPath(inputPath.MakeRelativePath(ancestor));
                                break;
                            }
                        }
                    };
                    remapPaths(selection);
                    remapPaths(mask);

                    session->selectionList()->updatePaths(selection);
                    session->setMask(mask);
                });
            });
        },
        [state, preserveTransform](Session* session) {
            if (!session || state->items.isEmpty())
                return;

            command::beginDeferred(session, "Undo move paths", 1);

            command::runWorker([=]() {
                bool hadStage = true;
                bool restored = false;
                QString error;
                QList<SdfPath> changed;

                {
                    WRITE_LOCKER(locker, session->stageLock(), "stageLock");
                    const UsdStageRefPtr stage = session->stageUnsafe();

                    if (!stage) {
                        hadStage = false;
                    }
                    else {
                        QString editError;
                        const SdfLayerHandle editLayer = currentEditLayer(stage, editError);
                        if (!editLayer) {
                            error = editError;
                        }
                        else {
                            UsdEditContext context(stage, UsdEditTarget(editLayer));
                            QList<QPair<SdfPath, SdfPath>> reverseMoves;
                            reverseMoves.reserve(state->items.size());
                            for (auto it = state->items.crbegin(); it != state->items.crend(); ++it) {
                                if (it->oldPath != it->newPath)
                                    reverseMoves.append(qMakePair(it->newPath, it->oldPath));
                            }

                            restored = true;
                            if (!reverseMoves.isEmpty()) {
                                edit::NamespaceEditor namespaceEditor(stage, stage->GetEditTarget());
                                restored = namespaceEditor.reparentPrims(reverseMoves, error);
                            }

                            if (restored && preserveTransform) {
                                for (const MoveItem& item : state->items) {
                                    if (!item.hasWorldTransform)
                                        continue;

                                    QString transformError;
                                    if (!stage::setWorldTransform(stage, item.oldPath, item.worldTransform,
                                                                  transformError)) {
                                        error = transformError.isEmpty()
                                                    ? QString("failed to restore world transform: %1")
                                                          .arg(qt::SdfPathToQString(item.oldPath))
                                                    : transformError;
                                        restored = false;
                                        break;
                                    }
                                }
                            }

                            if (restored) {
                                for (auto it = state->oldParentOrders.cbegin(); it != state->oldParentOrders.cend();
                                     ++it) {
                                    stage::restoreChildOrder(stage, it.key(), it.value());
                                }

                                QSet<SdfPath> changedSet;

                                for (const MoveItem& item : state->items) {
                                    changedSet.insert(item.oldPath);
                                    changedSet.insert(item.newPath);
                                    changedSet.insert(item.oldParentPath);
                                }

                                changedSet.insert(state->newParentPath);
                                changed = changedSet.values();
                            }
                        }
                    }
                }

                command::queueToSession(session, [=]() {
                    using Status = Session::Notify::Status;

                    if (!hadStage) {
                        command::finishDeferred(session, "Undo move paths failed", {}, Status::Error);
                        return;
                    }

                    if (!restored) {
                        command::finishDeferred(session,
                                                error.isEmpty() ? "Undo move paths failed"
                                                                : QString("Undo move paths failed: %1").arg(error),
                                                changed, Status::Error);
                        return;
                    }

                    command::finishDeferred(session, state->items.size() == 1 ? "Move undone" : "Moves undone", changed,
                                            Status::Success);

                    session->selectionList()->updatePaths(state->previousSelection);
                    session->setMask(state->previousMask);
                });
            });
        });
}

}  // namespace stageviz
