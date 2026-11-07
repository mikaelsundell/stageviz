// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#include "commandproperty.h"
#include "commandtransformprivate.h"
#include "commandutils.h"
#include "tracelocks.h"
#include "usdutils.h"
#include <memory>
#include <pxr/usd/sdf/changeBlock.h>
#include <pxr/usd/sdf/copyUtils.h>
#include <pxr/usd/sdf/layer.h>
#include <pxr/usd/sdf/schema.h>
#include <pxr/usd/sdf/types.h>
#include <pxr/usd/usd/attribute.h>
#include <pxr/usd/usd/editContext.h>
#include <pxr/usd/usd/editTarget.h>
#include <pxr/usd/usd/prim.h>
#include <pxr/usd/usd/primDefinition.h>
#include <pxr/usd/usdGeom/xformable.h>

namespace stageviz {

namespace {
    using command::appendError;
    using command::captureRootPropertyState;
    using command::currentEditLayer;
    using command::DependencyPropertyState;
    using command::pathText;
    using command::removePropertySpec;
    using command::restoreDependencyProperty;
    using command::restoreRootPropertyState;
    using command::RootPropertyState;
    using command::snapshotDependencyProperty;
    using command::summarizeErrors;
    using command::detail::authorCanonicalTransformWithPivot;
    using command::detail::captureTransformPropertyState;
    using command::detail::restoreTransformPropertyState;
    using command::detail::TransformPropertyState;

    bool clearDependencyFields(const SdfLayerHandle& editLayer, const SdfPath& propertyPath, bool& changed,
                               QString& error)
    {
        changed = false;

        if (!editLayer || propertyPath.IsEmpty() || !propertyPath.IsPropertyPath()) {
            error = "invalid dependency property";
            return false;
        }

        const bool hasTargets = editLayer->HasField(propertyPath, SdfFieldKeys->TargetPaths);
        const bool hasConnections = editLayer->HasField(propertyPath, SdfFieldKeys->ConnectionPaths);

        if (!hasTargets && !hasConnections)
            return true;

        if (hasTargets)
            editLayer->EraseField(propertyPath, SdfFieldKeys->TargetPaths);

        if (hasConnections)
            editLayer->EraseField(propertyPath, SdfFieldKeys->ConnectionPaths);

        changed = true;

        const SdfPrimSpecHandle remainingPrimSpec = editLayer->GetPrimAtPath(propertyPath.GetPrimPath());
        if (remainingPrimSpec && remainingPrimSpec->IsInert())
            stage::removePrimSpec(editLayer, propertyPath.GetPrimPath());

        return true;
    }
}  // namespace

Command
resetDependencies(const QList<SdfPath>& propertyPaths)
{
    auto states = std::make_shared<QList<DependencyPropertyState>>();

    return Command(
        [propertyPaths, states](Session* session) {
            if (!session || propertyPaths.isEmpty())
                return;

            const QList<SdfPath> paths = path::uniquePaths(propertyPaths);
            command::beginDeferred(session, "Reset dependencies", static_cast<int>(paths.size()));

            command::runWorker([session, paths, states]() {
                QList<SdfPath> affected;
                path::PathCollector affectedCollector(affected);
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
                            states->clear();

                            for (const SdfPath& propertyPath : paths) {
                                if (propertyPath.IsEmpty() || !propertyPath.IsPropertyPath())
                                    continue;

                                const SdfPropertySpecHandle propertySpec = editLayer->GetPropertyAtPath(propertyPath);
                                if (!propertySpec)
                                    continue;

                                const bool hasTargets = editLayer->HasField(propertyPath, SdfFieldKeys->TargetPaths);
                                const bool hasConnections = editLayer->HasField(propertyPath,
                                                                                SdfFieldKeys->ConnectionPaths);

                                if (!hasTargets && !hasConnections)
                                    continue;

                                DependencyPropertyState state;
                                QString error;

                                if (!snapshotDependencyProperty(editLayer, propertyPath, state, error)) {
                                    errors.append(error);
                                    continue;
                                }

                                bool changed = false;
                                if (!clearDependencyFields(editLayer, propertyPath, changed, error)) {
                                    errors.append(error);

                                    QString restoreError;
                                    restoreDependencyProperty(editLayer, state, restoreError);
                                    if (!restoreError.isEmpty())
                                        errors.append(restoreError);

                                    continue;
                                }

                                if (!changed)
                                    continue;

                                states->append(state);
                                affectedCollector.append(propertyPath.GetPrimPath());
                            }
                        }
                    }
                }

                const bool success = errors.isEmpty() && !affected.isEmpty();
                const QString errorText = summarizeErrors(errors);

                command::queueToSession(session, [session, affected, success, errorText]() {
                    using Status = Session::Notify::Status;

                    const QString message = success ? QStringLiteral("Dependencies reset")
                                                    : (errorText.isEmpty()
                                                           ? QStringLiteral("No dependencies to reset")
                                                           : appendError("Reset dependencies failed", errorText));

                    command::finishDeferred(session, message, affected, success ? Status::Success : Status::Error);
                });
            });
        },
        [states](Session* session) {
            if (!session || states->isEmpty())
                return;

            command::beginDeferred(session, "Undo reset dependencies", static_cast<int>(states->size()));

            command::runWorker([session, states]() {
                QList<SdfPath> restored;
                path::PathCollector restoredCollector(restored);
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
                            for (const DependencyPropertyState& state : *states) {
                                QString error;
                                if (restoreDependencyProperty(editLayer, state, error))
                                    restoredCollector.append(state.propertyPath.GetPrimPath());
                                else
                                    errors.append(error);
                            }
                        }
                    }
                }

                const bool success = errors.isEmpty();
                const QString errorText = summarizeErrors(errors);

                command::queueToSession(session, [session, restored, success, errorText]() {
                    using Status = Session::Notify::Status;
                    command::finishDeferred(session,
                                            success ? "Reset dependencies undone"
                                                    : appendError("Undo reset dependencies failed", errorText),
                                            restored, success ? Status::Success : Status::Error);
                });
            });
        });
}

namespace {
    struct AttributeSpecBatchState {
        struct Item {
            SdfPath path;
            SdfPath specPath;
            SdfLayerRefPtr snapshot;
            bool hadSpec = false;
        };
        QList<Item> items;
        SdfLayerRefPtr snapshot;
        bool captured = false;
    };
}  // namespace

static Command
attributeSpecBatch(const QList<SdfPath>& paths, const TfToken& name, const SdfValueTypeName& type, bool custom,
                   bool removing)
{
    auto state = std::make_shared<AttributeSpecBatchState>();
    return Command(
        [paths, name, type, custom, removing, state](Session* session) {
            if (!session || paths.isEmpty())
                return;
            command::beginDeferred(session, removing ? "Remove attributes" : "Add attributes",
                                   static_cast<int>(paths.size()));
            command::runWorker([session, paths, name, type, custom, removing, state]() {
                QList<SdfPath> affected;
                path::PathCollector affectedCollector(affected);
                QStringList errors;
                {
                    WRITE_LOCKER(locker, session->stageLock(), "stageLock");
                    const UsdStageRefPtr stage = session->stageUnsafe();
                    QString error;
                    const SdfLayerHandle layer = currentEditLayer(stage, error);
                    if (!layer)
                        errors.append(error);
                    else {
                        if (!state->captured) {
                            state->items.clear();
                            for (const SdfPath& path : path::uniquePaths(paths)) {
                                const SdfPath propPath = removing ? path : path.AppendProperty(name);
                                if (!propPath.IsPropertyPath())
                                    continue;
                                const SdfPath specPath = stage->GetEditTarget().MapToSpecPath(propPath);
                                if (specPath.IsEmpty()) {
                                    errors.append(QString("Cannot map attribute: %1").arg(pathText(propPath)));
                                    continue;
                                }
                                const UsdPrim prim = stage->GetPrimAtPath(propPath.GetPrimPath());
                                if (!prim || prim.IsInstanceProxy())
                                    continue;
                                const UsdAttribute attr = prim.GetAttribute(propPath.GetNameToken());
                                if (removing) {
                                    if (!attr)
                                        continue;
                                    if (prim.GetPrimDefinition().GetAttributeDefinition(propPath.GetNameToken())) {
                                        errors.append(
                                            QString("Cannot remove built-in attribute %1").arg(pathText(propPath)));
                                        continue;
                                    }
                                    if (!layer->GetPropertyAtPath(specPath)) {
                                        errors.append(QString("Attribute is not authored in the current edit layer: %1")
                                                          .arg(pathText(propPath)));
                                        continue;
                                    }
                                }
                                else {
                                    if (layer->GetPropertyAtPath(specPath))
                                        continue;
                                    if (!custom && (!attr || attr.GetTypeName() != type))
                                        continue;
                                    if (custom && prim.HasProperty(name))
                                        continue;
                                }
                                AttributeSpecBatchState::Item item;
                                item.path = propPath;
                                item.specPath = specPath;
                                item.hadSpec = bool(layer->GetPropertyAtPath(specPath));
                                if (item.hadSpec) {
                                    if (!state->snapshot)
                                        state->snapshot = SdfLayer::CreateAnonymous("stageviz_attribute_snapshot.usda");
                                    item.snapshot = state->snapshot;
                                    SdfCreatePrimInLayer(item.snapshot, specPath.GetParentPath());
                                    if (!SdfCopySpec(layer, specPath, item.snapshot, specPath))
                                        continue;
                                }
                                state->items.append(item);
                            }
                            state->captured = true;
                        }
                        struct Plan {
                            SdfPath path, specPath;
                            SdfVariability variability;
                        };
                        QList<Plan> plans;
                        // USD validation and schema discovery precede Sdf edits.
                        for (const auto& item : state->items) {
                            const auto prim = stage->GetPrimAtPath(item.path.GetPrimPath());
                            if (!prim)
                                continue;
                            if (removing && prim.GetPrimDefinition().GetAttributeDefinition(item.path.GetNameToken())) {
                                errors.append(QString("Cannot remove built-in attribute %1").arg(pathText(item.path)));
                                continue;
                            }
                            const auto attr = prim.GetAttribute(item.path.GetNameToken());
                            plans.append(
                                { item.path, item.specPath, attr ? attr.GetVariability() : SdfVariabilityVarying });
                        }
                        const VtValue fallback = type ? type.GetDefaultValue() : VtValue();
                        SdfChangeBlock block;
                        for (const auto& plan : plans) {
                            bool ok = false;
                            if (removing)
                                ok = removePropertySpec(layer, plan.specPath);
                            else {
                                const auto created = layer::ensureAttributeSpec(layer, plan.specPath, type,
                                                                                plan.variability, custom);
                                ok = bool(created);
                                if (ok && !fallback.IsEmpty())
                                    ok = created->SetDefaultValue(fallback);
                            }
                            if (ok)
                                affectedCollector.append(plan.path.GetPrimPath());
                            else
                                errors.append(QString("Attribute operation failed: %1").arg(pathText(plan.path)));
                        }
                    }
                }
                command::queueToSession(session, [session, affected, errors, removing]() {
                    using Status = Session::Notify::Status;
                    command::finishDeferred(session,
                                            errors.isEmpty()
                                                ? (removing ? "Attributes removed" : "Attributes added")
                                                : appendError("Attribute operation failed", summarizeErrors(errors)),
                                            affected, errors.isEmpty() ? Status::Success : Status::Error);
                });
            });
        },
        [state](Session* session) {
            if (!session || state->items.isEmpty())
                return;
            command::beginDeferred(session, "Undo attribute operation", static_cast<int>(state->items.size()));
            command::runWorker([session, state]() {
                QList<SdfPath> affected;
                path::PathCollector affectedCollector(affected);
                QStringList errors;
                {
                    WRITE_LOCKER(locker, session->stageLock(), "stageLock");
                    const UsdStageRefPtr stage = session->stageUnsafe();
                    QString error;
                    const SdfLayerHandle layer = currentEditLayer(stage, error);
                    if (!layer)
                        errors.append(error);
                    else {
                        SdfChangeBlock block;
                        for (const auto& item : state->items) {
                            if (!removePropertySpec(layer, item.specPath)) {
                                errors.append(QString("Failed to clear %1").arg(pathText(item.path)));
                                continue;
                            }
                            if (item.hadSpec) {
                                SdfCreatePrimInLayer(layer, item.specPath.GetParentPath());
                                if (!SdfCopySpec(item.snapshot, item.specPath, layer, item.specPath)) {
                                    errors.append(QString("Failed to restore %1").arg(pathText(item.path)));
                                    continue;
                                }
                            }
                            affectedCollector.append(item.path.GetPrimPath());
                        }
                    }
                }
                command::queueToSession(session, [session, affected, errors]() {
                    using Status = Session::Notify::Status;
                    command::finishDeferred(session,
                                            errors.isEmpty() ? "Attribute operation undone"
                                                             : appendError("Undo attribute operation failed",
                                                                           summarizeErrors(errors)),
                                            affected, errors.isEmpty() ? Status::Success : Status::Error);
                });
            });
        });
}

Command
addAttributes(const QList<SdfPath>& paths, const TfToken& name, const SdfValueTypeName& type, bool custom)
{
    return attributeSpecBatch(paths, name, type, custom, false);
}

Command
removeAttributes(const QList<SdfPath>& paths)
{
    return attributeSpecBatch(paths, TfToken(), SdfValueTypeName(), false, true);
}

Command
renameAttributes(const QList<SdfPath>& attributePaths, const TfToken& newName)
{
    struct RenameState {
        struct Item {
            SdfPath oldPath;
            SdfPath newPath;
            SdfPath oldSpecPath, newSpecPath;
            bool removeOriginal = false;
        };
        QList<Item> items;
        bool captured = false;
    };
    auto state = std::make_shared<RenameState>();
    return Command(
        [attributePaths, newName, state](Session* session) {
            if (!session || attributePaths.isEmpty() || newName.IsEmpty())
                return;
            command::beginDeferred(session, "Rename attributes", static_cast<int>(attributePaths.size()));
            command::runWorker([session, attributePaths, newName, state]() {
                QList<SdfPath> affected;
                path::PathCollector affectedCollector(affected);
                QStringList errors;
                {
                    WRITE_LOCKER(locker, session->stageLock(), "stageLock");
                    const UsdStageRefPtr stage = session->stageUnsafe();
                    QString error;
                    const SdfLayerHandle layer = currentEditLayer(stage, error);
                    if (!layer)
                        errors.append(error);
                    else {
                        if (!state->captured) {
                            state->items.clear();
                            // USD property names support namespace components separated by ':'.
                            const QString requested = QString::fromStdString(newName.GetString());
                            const QStringList components = requested.split(QLatin1Char(':'), Qt::KeepEmptyParts);
                            bool validName = !components.isEmpty();
                            for (const QString& component : components)
                                validName = validName && SdfPath::IsValidIdentifier(component.toStdString());
                            if (!validName)
                                errors.append(QString("Invalid USD attribute name: %1").arg(requested));
                            for (const SdfPath& oldPath :
                                 validName ? path::uniquePaths(attributePaths) : QList<SdfPath>()) {
                                if (!oldPath.IsPropertyPath() || oldPath.GetNameToken() == newName)
                                    continue;
                                const UsdPrim prim = stage->GetPrimAtPath(oldPath.GetPrimPath());
                                if (!prim || prim.IsInstanceProxy())
                                    continue;
                                if (prim.GetPrimDefinition().GetAttributeDefinition(oldPath.GetNameToken())) {
                                    errors.append(
                                        QString("Cannot rename built-in attribute %1").arg(pathText(oldPath)));
                                    continue;
                                }
                                if (prim.HasProperty(newName)) {
                                    errors.append(QString("Attribute already exists: %1")
                                                      .arg(pathText(oldPath.GetPrimPath().AppendProperty(newName))));
                                    continue;
                                }
                                const UsdAttribute attr = stage->GetAttributeAtPath(oldPath);
                                if (!attr)
                                    continue;
                                const SdfPath oldSpecPath = stage->GetEditTarget().MapToSpecPath(oldPath);
                                const SdfPath newPath = oldPath.GetPrimPath().AppendProperty(newName);
                                const SdfPath newSpecPath = stage->GetEditTarget().MapToSpecPath(newPath);
                                if (oldSpecPath.IsEmpty() || newSpecPath.IsEmpty()) {
                                    errors.append(QString("Cannot map attribute: %1").arg(pathText(oldPath)));
                                    continue;
                                }
                                if (!layer->GetPropertyAtPath(oldSpecPath)) {
                                    errors.append(QString("Attribute is not authored in the current edit layer: %1")
                                                      .arg(pathText(oldPath)));
                                    continue;
                                }
                                state->items.append({ oldPath, newPath, oldSpecPath, newSpecPath, true });
                            }
                            state->captured = true;
                        }
                        struct Plan {
                            SdfPath oldPath, newPath, oldSpecPath, newSpecPath;
                            SdfValueTypeName type;
                            SdfVariability variability;
                            VtValue value;
                            bool removeOriginal;
                        };
                        QList<Plan> plans;
                        for (const auto& item : state->items) {
                            if (layer->GetPropertyAtPath(item.newSpecPath))
                                continue;
                            const auto source = stage->GetAttributeAtPath(item.oldPath);
                            if (!source)
                                continue;
                            VtValue value;
                            if (!item.removeOriginal)
                                source.Get(&value);
                            plans.append({ item.oldPath, item.newPath, item.oldSpecPath, item.newSpecPath,
                                           source.GetTypeName(), source.GetVariability(), value, item.removeOriginal });
                        }
                        SdfChangeBlock block;
                        for (const auto& plan : plans) {
                            if (plan.removeOriginal && layer->GetPropertyAtPath(plan.oldSpecPath)) {
                                if (!SdfCopySpec(layer, plan.oldSpecPath, layer, plan.newSpecPath)) {
                                    errors.append(QString("Cannot copy %1").arg(pathText(plan.oldPath)));
                                    continue;
                                }
                                layer->SetField(plan.newSpecPath, SdfFieldKeys->Custom, VtValue(true));
                                if (!removePropertySpec(layer, plan.oldSpecPath)) {
                                    errors.append(QString("Cannot remove %1").arg(pathText(plan.oldPath)));
                                    continue;
                                }
                            }
                            else {
                                const auto target = layer::ensureAttributeSpec(layer, plan.newSpecPath, plan.type,
                                                                               plan.variability, true);
                                if (!target || (!plan.value.IsEmpty() && !target->SetDefaultValue(plan.value))) {
                                    errors.append(QString("Cannot rename %1").arg(pathText(plan.oldPath)));
                                    continue;
                                }
                            }
                            affectedCollector.append(plan.oldPath.GetPrimPath());
                        }
                    }
                }
                command::queueToSession(session, [session, affected, errors]() {
                    command::finishDeferred(session,
                                            errors.isEmpty()
                                                ? "Attributes renamed"
                                                : appendError("Rename attributes failed", summarizeErrors(errors)),
                                            affected,
                                            errors.isEmpty() ? Session::Notify::Status::Success
                                                             : Session::Notify::Status::Error);
                });
            });
        },
        [state](Session* session) {
            if (!session || state->items.isEmpty())
                return;
            command::beginDeferred(session, "Undo rename attributes", static_cast<int>(state->items.size()));
            command::runWorker([session, state]() {
                QList<SdfPath> affected;
                path::PathCollector affectedCollector(affected);
                QStringList errors;
                {
                    WRITE_LOCKER(locker, session->stageLock(), "stageLock");
                    const UsdStageRefPtr stage = session->stageUnsafe();
                    QString error;
                    const SdfLayerHandle layer = currentEditLayer(stage, error);
                    if (!layer)
                        errors.append(error);
                    else {
                        SdfChangeBlock block;
                        for (const auto& item : state->items) {
                            if (item.removeOriginal) {
                                if (!SdfCopySpec(layer, item.newSpecPath, layer, item.oldSpecPath))
                                    errors.append(QString("Cannot restore %1").arg(pathText(item.oldPath)));
                            }
                            if (!removePropertySpec(layer, item.newSpecPath))
                                errors.append(QString("Cannot remove %1").arg(pathText(item.newPath)));
                            affectedCollector.append(item.oldPath.GetPrimPath());
                        }
                    }
                }
                command::queueToSession(session, [session, affected, errors]() {
                    command::finishDeferred(
                        session,
                        errors.isEmpty() ? "Rename undone" : appendError("Undo rename failed", summarizeErrors(errors)),
                        affected, errors.isEmpty() ? Session::Notify::Status::Success : Session::Notify::Status::Error);
                });
            });
        });
}

Command
setAttributeValues(const QList<SdfPath>& attributePaths, const VtValue& value)
{
    struct AttributeValueState {
        struct Item {
            SdfPath attributePath;
            bool hadRootDefault = false;
            VtValue previousRootDefault;
            bool preservePivotTransform = false;
            GfMatrix4d previousLocalTransform { 1.0 };
            TransformPropertyState transformState;
        };

        QList<Item> items;
        bool captured = false;
    };

    auto state = std::make_shared<AttributeValueState>();

    return Command(
        [attributePaths, value, state](Session* session) {
            if (!session || attributePaths.isEmpty() || value.IsEmpty())
                return;

            const QList<SdfPath> paths = path::uniquePaths(attributePaths);
            command::beginDeferred(session, "Set attribute values", static_cast<int>(paths.size()));

            command::runWorker([session, paths, value, state]() {
                QList<SdfPath> affected;
                path::PathCollector affectedCollector(affected);
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
                            if (!state->captured) {
                                state->items.clear();

                                for (const SdfPath& attributePath : paths) {
                                    if (attributePath.IsEmpty() || !attributePath.IsPropertyPath()) {
                                        errors.append(
                                            QString("invalid attribute path: %1").arg(pathText(attributePath)));
                                        continue;
                                    }

                                    const UsdPrim owner = stage->GetPrimAtPath(attributePath.GetPrimPath());
                                    if (owner && owner.IsInstanceProxy()) {
                                        errors.append(QString("cannot edit instance proxy: %1")
                                                          .arg(pathText(attributePath.GetPrimPath())));
                                        continue;
                                    }

                                    const UsdAttribute attr = stage->GetAttributeAtPath(attributePath);
                                    if (!attr) {
                                        errors.append(QString("attribute missing: %1").arg(pathText(attributePath)));
                                        continue;
                                    }

                                    AttributeValueState::Item item;
                                    item.attributePath = attributePath;
                                    item.hadRootDefault = editLayer->HasField(attributePath, SdfFieldKeys->Default);

                                    if (item.hadRootDefault)
                                        item.previousRootDefault = editLayer->GetField(attributePath,
                                                                                       SdfFieldKeys->Default);

                                    const bool isPivot = attributePath.GetNameToken()
                                                             == TfToken("xformOp:translate:pivot")
                                                         && value.IsHolding<GfVec3d>();

                                    const bool isPivotFrame = attributePath.GetNameToken()
                                                                  == TfToken("xformOp:transform:pivot")
                                                              && value.IsHolding<GfMatrix4d>();
                                    if (isPivot || isPivotFrame) {
                                        const UsdPrim prim = stage->GetPrimAtPath(attributePath.GetPrimPath());
                                        const UsdGeomXformable xformable(prim);

                                        if (!prim || !prim.IsValid() || prim.IsInstanceProxy() || !xformable) {
                                            errors.append(QString("pivot prim is not transformable: %1")
                                                              .arg(pathText(attributePath.GetPrimPath())));
                                            continue;
                                        }

                                        bool resetsXformStack = false;
                                        if (!xformable.GetLocalTransformation(&item.previousLocalTransform,
                                                                              &resetsXformStack,
                                                                              UsdTimeCode::Default())) {
                                            errors.append(QString("failed to evaluate local transform: %1")
                                                              .arg(pathText(attributePath.GetPrimPath())));
                                            continue;
                                        }

                                        QString transformError;
                                        if (!captureTransformPropertyState(editLayer, attributePath.GetPrimPath(),
                                                                           item.transformState, transformError)) {
                                            errors.append(transformError);
                                            continue;
                                        }

                                        item.preservePivotTransform = true;
                                    }

                                    state->items.append(item);
                                }

                                state->captured = !state->items.isEmpty();
                            }

                            UsdEditContext context(stage, UsdEditTarget(editLayer));

                            for (const AttributeValueState::Item& item : state->items) {
                                if (item.preservePivotTransform) {
                                    const UsdPrim prim = stage->GetPrimAtPath(item.attributePath.GetPrimPath());
                                    QString error;

                                    if (!prim || !prim.IsValid()) {
                                        errors.append(
                                            QString("prim missing: %1").arg(pathText(item.attributePath.GetPrimPath())));
                                        continue;
                                    }

                                    const bool authored
                                        = value.IsHolding<GfMatrix4d>()
                                              ? authorCanonicalTransformWithPivot(prim, item.previousLocalTransform,
                                                                                  value.UncheckedGet<GfMatrix4d>(),
                                                                                  error)
                                              : authorCanonicalTransformWithPivot(prim, item.previousLocalTransform,
                                                                                  value.UncheckedGet<GfVec3d>(), error);
                                    if (!authored) {
                                        QString restoreError;
                                        restoreTransformPropertyState(editLayer, item.transformState, restoreError);

                                        errors.append(
                                            error.isEmpty()
                                                ? QString("failed to set pivot: %1")
                                                      .arg(pathText(item.attributePath.GetPrimPath()))
                                                : QString("%1: %2").arg(pathText(item.attributePath.GetPrimPath()),
                                                                        error));
                                        continue;
                                    }

                                    affectedCollector.append(item.attributePath.GetPrimPath());
                                    continue;
                                }

                                const UsdAttribute attr = stage->GetAttributeAtPath(item.attributePath);
                                if (!attr) {
                                    errors.append(QString("attribute missing: %1").arg(pathText(item.attributePath)));
                                    continue;
                                }

                                if (!attr.Set(value)) {
                                    errors.append(QString("USD rejected value: %1").arg(pathText(item.attributePath)));
                                    continue;
                                }

                                affectedCollector.append(item.attributePath.GetPrimPath());
                            }
                        }
                    }
                }

                const bool success = errors.isEmpty() && !affected.isEmpty();
                command::queueToSession(session, [session, affected, errors, success]() {
                    using Status = Session::Notify::Status;
                    const QString message = success ? (affected.size() == 1 ? QStringLiteral("Attribute value set")
                                                                            : QStringLiteral("Attribute values set"))
                                                    : appendError("Set attribute values finished with errors",
                                                                  summarizeErrors(errors));
                    command::finishDeferred(session, message, affected, success ? Status::Success : Status::Error);
                });
            });
        },
        [state](Session* session) {
            if (!session || !state->captured || state->items.isEmpty())
                return;

            command::beginDeferred(session, "Undo set attribute values", static_cast<int>(state->items.size()));

            command::runWorker([session, state]() {
                QList<SdfPath> affected;
                path::PathCollector affectedCollector(affected);
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
                            for (const AttributeValueState::Item& item : state->items) {
                                if (item.preservePivotTransform) {
                                    QString error;
                                    if (!restoreTransformPropertyState(editLayer, item.transformState, error)) {
                                        errors.append(error.isEmpty()
                                                          ? QString("failed to restore pivot transform: %1")
                                                                .arg(pathText(item.attributePath.GetPrimPath()))
                                                          : error);
                                        continue;
                                    }

                                    affectedCollector.append(item.attributePath.GetPrimPath());
                                    continue;
                                }

                                if (item.hadRootDefault)
                                    editLayer->SetField(item.attributePath, SdfFieldKeys->Default,
                                                        item.previousRootDefault);
                                else
                                    editLayer->EraseField(item.attributePath, SdfFieldKeys->Default);

                                affectedCollector.append(item.attributePath.GetPrimPath());
                            }
                        }
                    }
                }

                const bool success = errors.isEmpty();
                command::queueToSession(session, [session, affected, errors, success]() {
                    using Status = Session::Notify::Status;
                    command::finishDeferred(session,
                                            success
                                                ? "Attribute values undone"
                                                : appendError("Undo attribute values failed", summarizeErrors(errors)),
                                            affected, success ? Status::Success : Status::Error);
                });
            });
        });
}

Command
setAttributeValue(const SdfPath& attributePath, const VtValue& value)
{
    return setAttributeValues({ attributePath }, value);
}

Command
resetAttributeValues(const QList<SdfPath>& attributePaths)
{
    struct ResetAttributeValueState {
        QList<RootPropertyState> states;
    };

    auto state = std::make_shared<ResetAttributeValueState>();

    return Command(
        [attributePaths, state](Session* session) {
            if (!session || attributePaths.isEmpty())
                return;

            const QList<SdfPath> paths = path::uniquePaths(attributePaths);
            command::beginDeferred(session, "Reset attribute values", static_cast<int>(paths.size()));

            command::runWorker([session, paths, state]() {
                QList<SdfPath> affected;
                path::PathCollector affectedCollector(affected);
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
                            state->states.clear();

                            for (const SdfPath& attributePath : paths) {
                                if (attributePath.IsEmpty() || !attributePath.IsPropertyPath())
                                    continue;

                                if (!stage->GetAttributeAtPath(attributePath))
                                    continue;

                                if (!editLayer->HasField(attributePath, SdfFieldKeys->Default))
                                    continue;

                                state->states.append(captureRootPropertyState(editLayer, attributePath));
                                editLayer->EraseField(attributePath, SdfFieldKeys->Default);
                                affectedCollector.append(attributePath.GetPrimPath());
                            }
                        }
                    }
                }

                const bool success = errors.isEmpty() && !affected.isEmpty();
                command::queueToSession(session, [session, affected, errors, success]() {
                    using Status = Session::Notify::Status;
                    const QString message
                        = success ? QStringLiteral("Attribute values reset")
                                  : (errors.isEmpty()
                                         ? QStringLiteral("No authored attribute values to reset")
                                         : appendError("Reset attribute values failed", summarizeErrors(errors)));
                    command::finishDeferred(session, message, affected, success ? Status::Success : Status::Error);
                });
            });
        },
        [state](Session* session) {
            if (!session || state->states.isEmpty())
                return;

            command::beginDeferred(session, "Undo reset attribute values", static_cast<int>(state->states.size()));

            command::runWorker([session, state]() {
                QList<SdfPath> affected;
                path::PathCollector affectedCollector(affected);
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
                            for (const RootPropertyState& rootState : state->states) {
                                if (!restoreRootPropertyState(editLayer, rootState)) {
                                    errors.append(QString("failed to restore attribute value: %1")
                                                      .arg(pathText(rootState.propertyPath)));
                                    continue;
                                }

                                affectedCollector.append(rootState.propertyPath.GetPrimPath());
                            }
                        }
                    }
                }

                const bool success = errors.isEmpty();
                command::queueToSession(session, [session, affected, errors, success]() {
                    using Status = Session::Notify::Status;
                    command::finishDeferred(session,
                                            success ? "Reset attribute values undone"
                                                    : appendError("Undo reset attribute values failed",
                                                                  summarizeErrors(errors)),
                                            affected, success ? Status::Success : Status::Error);
                });
            });
        });
}

Command
resetAttributeOverrides(const QList<SdfPath>& attributePaths)
{
    struct ResetAttributeOverrideState {
        struct Item {
            SdfPath attributePath;
            SdfPath primPath;
            SdfLayerRefPtr snapshotLayer;
        };

        QList<Item> items;
    };

    auto state = std::make_shared<ResetAttributeOverrideState>();

    return Command(
        [attributePaths, state](Session* session) {
            if (!session || attributePaths.isEmpty())
                return;

            const QList<SdfPath> paths = path::uniquePaths(attributePaths);
            command::beginDeferred(session, "Reset attribute overrides", static_cast<int>(paths.size()));

            command::runWorker([session, paths, state]() {
                QList<SdfPath> affected;
                path::PathCollector affectedCollector(affected);
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

                            for (const SdfPath& attributePath : paths) {
                                if (attributePath.IsEmpty() || !attributePath.IsPropertyPath())
                                    continue;

                                const SdfPath primPath = attributePath.GetPrimPath();
                                const UsdPrim prim = stage->GetPrimAtPath(primPath);
                                const SdfPropertySpecHandle rootAttribute = editLayer->GetPropertyAtPath(attributePath);

                                if (!prim || !prim.IsValid() || prim.IsInstanceProxy() || !rootAttribute)
                                    continue;

                                bool hasUnderlyingPrimOpinion = false;
                                for (const SdfPrimSpecHandle& primSpec : prim.GetPrimStack()) {
                                    if (!primSpec)
                                        continue;

                                    const SdfLayerHandle layer = primSpec->GetLayer();
                                    if (layer && layer != editLayer) {
                                        hasUnderlyingPrimOpinion = true;
                                        break;
                                    }
                                }

                                if (!hasUnderlyingPrimOpinion)
                                    continue;

                                ResetAttributeOverrideState::Item item;
                                item.attributePath = attributePath;
                                item.primPath = primPath;
                                item.snapshotLayer = SdfLayer::CreateAnonymous(
                                    "stageviz_attribute_override_snapshot.usda");

                                if (!item.snapshotLayer) {
                                    errors.append(
                                        QString("failed to create override snapshot: %1").arg(pathText(attributePath)));
                                    continue;
                                }

                                SdfCreatePrimInLayer(item.snapshotLayer, primPath);
                                if (!SdfCopySpec(editLayer, attributePath, item.snapshotLayer, attributePath)) {
                                    errors.append(QString("failed to snapshot attribute override: %1")
                                                      .arg(pathText(attributePath)));
                                    continue;
                                }

                                if (!removePropertySpec(editLayer, attributePath)) {
                                    errors.append(
                                        QString("failed to remove attribute override: %1").arg(pathText(attributePath)));
                                    continue;
                                }

                                const SdfPrimSpecHandle remainingSpec = editLayer->GetPrimAtPath(primPath);
                                if (remainingSpec && remainingSpec->IsInert())
                                    stage::removePrimSpec(editLayer, primPath);

                                state->items.append(item);
                                affectedCollector.append(primPath);
                            }
                        }
                    }
                }

                const bool success = errors.isEmpty() && !affected.isEmpty();
                command::queueToSession(session, [session, affected, errors, success]() {
                    using Status = Session::Notify::Status;
                    const QString message = success
                                                ? QStringLiteral("Attribute overrides reset")
                                                : (errors.isEmpty() ? QStringLiteral("No attribute overrides to reset")
                                                                    : appendError("Reset attribute overrides failed",
                                                                                  summarizeErrors(errors)));
                    command::finishDeferred(session, message, affected, success ? Status::Success : Status::Error);
                });
            });
        },
        [state](Session* session) {
            if (!session || state->items.isEmpty())
                return;

            command::beginDeferred(session, "Undo reset attribute overrides", static_cast<int>(state->items.size()));

            command::runWorker([session, state]() {
                QList<SdfPath> affected;
                path::PathCollector affectedCollector(affected);
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
                            for (const ResetAttributeOverrideState::Item& item : state->items) {
                                if (!editLayer->GetPrimAtPath(item.primPath))
                                    SdfCreatePrimInLayer(editLayer, item.primPath);

                                if (!SdfCopySpec(item.snapshotLayer, item.attributePath, editLayer,
                                                 item.attributePath)) {
                                    errors.append(QString("failed to restore attribute override: %1")
                                                      .arg(pathText(item.attributePath)));
                                    continue;
                                }

                                affectedCollector.append(item.primPath);
                            }
                        }
                    }
                }

                const bool success = errors.isEmpty();
                command::queueToSession(session, [session, affected, errors, success]() {
                    using Status = Session::Notify::Status;
                    command::finishDeferred(session,
                                            success ? "Attribute overrides reset undone"
                                                    : appendError("Undo reset attribute overrides failed",
                                                                  summarizeErrors(errors)),
                                            affected, success ? Status::Success : Status::Error);
                });
            });
        });
}

Command
resetAttributeOverride(const SdfPath& attributePath)
{
    return resetAttributeOverrides({ attributePath });
}

Command
resetOverrides(const QList<SdfPath>& paths)
{
    struct ResetOverrideState {
        struct Item {
            SdfPath path;
            SdfLayerRefPtr snapshotLayer;
            QList<SdfPath> propertyPaths;
            bool removedPrimSpec = false;
        };

        QList<Item> items;
    };

    auto state = std::make_shared<ResetOverrideState>();

    return Command(
        [paths, state](Session* session) {
            if (!session || paths.isEmpty())
                return;

            command::beginDeferred(session, "Reset overrides", static_cast<int>(paths.size()));

            command::runWorker([session, paths, state]() {
                QList<SdfPath> affected;
                path::PathCollector affectedCollector(affected);
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

                            QList<SdfPath> primPaths;
                            path::PathCollector primPathsCollector(primPaths);
                            for (const SdfPath& inputPath : path::minimalRootPaths(path::uniquePaths(paths))) {
                                const SdfPath primPath = inputPath.IsPropertyPath() ? inputPath.GetPrimPath()
                                                                                    : inputPath;
                                const UsdPrim prim = stage->GetPrimAtPath(primPath);
                                if (!prim || !prim.IsValid() || prim.IsInstanceProxy()) {
                                    errors.append(QString("invalid prim: %1").arg(pathText(primPath)));
                                    continue;
                                }

                                primPathsCollector.append(primPath);
                                for (const UsdPrim& descendant : prim.GetAllDescendants()) {
                                    if (descendant && descendant.IsValid() && !descendant.IsInstanceProxy())
                                        primPathsCollector.append(descendant.GetPath());
                                }
                            }

                            for (const SdfPath& primPath : primPaths) {
                                const UsdPrim prim = stage->GetPrimAtPath(primPath);
                                if (!prim || !prim.IsValid() || prim.IsInstanceProxy())
                                    continue;

                                const SdfPrimSpecHandle rootSpec = editLayer->GetPrimAtPath(primPath);
                                if (!rootSpec || rootSpec->GetSpecifier() != SdfSpecifierOver
                                    || rootSpec->GetProperties().empty())
                                    continue;

                                bool hasUnderlyingOpinion = false;
                                for (const SdfPrimSpecHandle& primSpec : prim.GetPrimStack()) {
                                    if (!primSpec)
                                        continue;

                                    const SdfLayerHandle layer = primSpec->GetLayer();
                                    if (layer && layer != editLayer) {
                                        hasUnderlyingOpinion = true;
                                        break;
                                    }
                                }

                                if (!hasUnderlyingOpinion)
                                    continue;

                                ResetOverrideState::Item item;
                                item.path = primPath;
                                item.snapshotLayer = SdfLayer::CreateAnonymous("stageviz_override_snapshot.usda");
                                if (!item.snapshotLayer) {
                                    errors.append(
                                        QString("failed to create override snapshot: %1").arg(pathText(primPath)));
                                    continue;
                                }

                                SdfCreatePrimInLayer(item.snapshotLayer, primPath);

                                const auto properties = rootSpec->GetProperties();
                                item.propertyPaths.reserve(static_cast<int>(properties.size()));

                                bool snapshotFailed = false;
                                for (const SdfPropertySpecHandle& property : properties) {
                                    if (!property)
                                        continue;

                                    const SdfPath propertyPath = property->GetPath();
                                    if (!SdfCopySpec(editLayer, propertyPath, item.snapshotLayer, propertyPath)) {
                                        errors.append(QString("failed to snapshot override property: %1")
                                                          .arg(pathText(propertyPath)));
                                        snapshotFailed = true;
                                        break;
                                    }

                                    item.propertyPaths.append(propertyPath);
                                }

                                if (snapshotFailed || item.propertyPaths.isEmpty())
                                    continue;

                                bool reset = true;
                                for (const SdfPath& propertyPath : item.propertyPaths) {
                                    if (!removePropertySpec(editLayer, propertyPath)) {
                                        errors.append(QString("failed to remove override property: %1")
                                                          .arg(pathText(propertyPath)));
                                        reset = false;
                                        break;
                                    }
                                }

                                if (!reset)
                                    continue;

                                const SdfPrimSpecHandle remainingSpec = editLayer->GetPrimAtPath(primPath);
                                if (remainingSpec && remainingSpec->IsInert()) {
                                    item.removedPrimSpec = stage::removePrimSpec(editLayer, primPath);
                                    if (!item.removedPrimSpec) {
                                        errors.append(
                                            QString("failed to remove empty override spec: %1").arg(pathText(primPath)));
                                        continue;
                                    }
                                }

                                state->items.append(item);
                                affectedCollector.append(primPath);
                            }
                        }
                    }
                }

                const bool success = errors.isEmpty();
                command::queueToSession(session, [session, affected, errors, success]() {
                    using Status = Session::Notify::Status;
                    const QString message = success ? QStringLiteral("Overrides reset")
                                                    : appendError("Reset overrides finished with errors",
                                                                  summarizeErrors(errors));

                    command::finishDeferred(session, message, affected, success ? Status::Success : Status::Error);
                });
            });
        },
        [state](Session* session) {
            if (!session || state->items.isEmpty())
                return;

            command::beginDeferred(session, "Undo reset overrides", static_cast<int>(state->items.size()));

            command::runWorker([session, state]() {
                QList<SdfPath> restored;
                path::PathCollector restoredCollector(restored);
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
                            for (const ResetOverrideState::Item& item : state->items) {
                                SdfPrimSpecHandle rootSpec = editLayer->GetPrimAtPath(item.path);
                                if (!rootSpec)
                                    rootSpec = SdfCreatePrimInLayer(editLayer, item.path);

                                if (!rootSpec) {
                                    errors.append(
                                        QString("failed to restore override spec: %1").arg(pathText(item.path)));
                                    continue;
                                }

                                bool restoredItem = true;
                                for (const SdfPath& propertyPath : item.propertyPaths) {
                                    if (!SdfCopySpec(item.snapshotLayer, propertyPath, editLayer, propertyPath)) {
                                        errors.append(QString("failed to restore override property: %1")
                                                          .arg(pathText(propertyPath)));
                                        restoredItem = false;
                                        break;
                                    }
                                }

                                if (restoredItem)
                                    restoredCollector.append(item.path);
                            }
                        }
                    }
                }

                const bool success = errors.isEmpty();
                command::queueToSession(session, [session, restored, errors, success]() {
                    using Status = Session::Notify::Status;
                    const QString message = success ? QStringLiteral("Reset overrides undone")
                                                    : appendError("Undo reset overrides finished with errors",
                                                                  summarizeErrors(errors));

                    command::finishDeferred(session, message, restored, success ? Status::Success : Status::Error);
                });
            });
        });
}

}  // namespace stageviz
