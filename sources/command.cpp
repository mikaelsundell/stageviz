// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#include "command.h"
#include "commandstack.h"
#include "commandutils.h"
#include "qtutils.h"
#include "tracelocks.h"
#include "usdedit.h"
#include "usdutils.h"
#include <QPointer>
#include <algorithm>
#include <cmath>
#include <pxr/base/gf/vec3f.h>
#include <pxr/base/gf/vec3h.h>
#include <pxr/usd/sdf/copyUtils.h>
#include <pxr/usd/sdf/layer.h>
#include <pxr/usd/sdf/namespaceEdit.h>
#include <pxr/usd/sdf/payload.h>
#include <pxr/usd/sdf/reference.h>
#include <pxr/usd/sdf/types.h>
#include <pxr/usd/usd/attribute.h>
#include <pxr/usd/usd/editContext.h>
#include <pxr/usd/usd/editTarget.h>
#include <pxr/usd/usd/payloads.h>
#include <pxr/usd/usd/prim.h>
#include <pxr/usd/usd/primRange.h>
#include <pxr/usd/usd/references.h>
#include <pxr/usd/usdGeom/bboxCache.h>
#include <pxr/usd/usdGeom/imageable.h>
#include <pxr/usd/usdGeom/tokens.h>
#include <pxr/usd/usdGeom/xform.h>
#include <pxr/usd/usdGeom/xformable.h>
#include <pxr/usd/usdShade/material.h>
#include <pxr/usd/usdShade/materialBindingAPI.h>
#include <pxr/usd/usdShade/shader.h>

namespace stageviz {

namespace payload {
    struct State {
        QList<PayloadState> payloadStates;
        QList<SdfPath> previousSelection;
        QList<SdfPath> previousMask;
    };
}  // namespace payload

namespace {

    QString pathText(const SdfPath& path) { return qt::SdfPathToQString(path); }

    QString appendError(const QString& message, const QString& error)
    {
        return error.isEmpty() ? message : QString("%1: %2").arg(message, error);
    }

    QString summarizeErrors(const QStringList& errors, int maxCount = 3)
    {
        if (errors.isEmpty())
            return {};

        QStringList result;
        for (int i = 0; i < errors.size() && i < maxCount; ++i)
            result.append(errors.at(i));
        if (errors.size() > maxCount)
            result.append(QString("%1 more").arg(errors.size() - maxCount));
        return result.join("; ");
    }

    struct RootPropertyState {
        SdfPath propertyPath;
        bool hadSpec = false;
        bool hadDefault = false;
        VtValue defaultValue;
    };

    RootPropertyState captureRootPropertyState(const SdfLayerHandle& editLayer, const SdfPath& propertyPath)
    {
        RootPropertyState state;
        state.propertyPath = propertyPath;
        if (!editLayer || propertyPath.IsEmpty())
            return state;

        state.hadSpec = bool(editLayer->GetPropertyAtPath(propertyPath));
        state.hadDefault = editLayer->HasField(propertyPath, SdfFieldKeys->Default);
        if (state.hadDefault)
            state.defaultValue = editLayer->GetField(propertyPath, SdfFieldKeys->Default);
        return state;
    }

    bool removePropertySpec(const SdfLayerHandle& layer, const SdfPath& propertyPath)
    {
        if (!layer || propertyPath.IsEmpty() || !propertyPath.IsPropertyPath())
            return false;
        if (!layer->GetPropertyAtPath(propertyPath))
            return true;

        SdfBatchNamespaceEdit edits;
        edits.Add(propertyPath, SdfPath::EmptyPath());
        return layer->CanApply(edits) && layer->Apply(edits);
    }

    bool restoreRootPropertyState(const SdfLayerHandle& editLayer, const RootPropertyState& state)
    {
        if (!editLayer || state.propertyPath.IsEmpty())
            return false;

        if (!state.hadSpec)
            return removePropertySpec(editLayer, state.propertyPath);

        if (state.hadDefault) {
            editLayer->SetField(state.propertyPath, SdfFieldKeys->Default, state.defaultValue);
            return true;
        }

        editLayer->EraseField(state.propertyPath, SdfFieldKeys->Default);
        return true;
    }

    QList<SdfPath> visibilityAffectedPaths(UsdStageRefPtr stage, const QList<SdfPath>& paths, bool recursive,
                                           bool makeVisible)
    {
        QList<SdfPath> result;
        if (!stage)
            return result;

        auto appendImageable = [&](const UsdPrim& prim) {
            if (prim && UsdGeomImageable(prim) && !result.contains(prim.GetPath()))
                result.append(prim.GetPath());
        };

        for (const SdfPath& inputPath : paths) {
            const SdfPath primPath = inputPath.IsPropertyPath() ? inputPath.GetPrimPath() : inputPath;
            const UsdPrim prim = stage->GetPrimAtPath(primPath);
            if (!prim)
                continue;

            appendImageable(prim);

            if (recursive) {
                for (const UsdPrim& child : prim.GetAllDescendants())
                    appendImageable(child);
            }

            if (makeVisible) {
                for (UsdPrim ancestor = prim.GetParent(); ancestor && !ancestor.IsPseudoRoot();
                     ancestor = ancestor.GetParent()) {
                    appendImageable(ancestor);
                }
            }
        }

        return result;
    }

    bool restoreTransformRootState(UsdStageRefPtr stage, const SdfPath& primPath, const TransformRootState& state,
                                   QString& error)
    {
        if (!stage) {
            error = "stage missing";
            return false;
        }

        QString editError;
        const SdfLayerHandle editLayer = editlayer::opened(stage, editError);
        if (!editLayer) {
            error = editError;
            return false;
        }

        const SdfPath orderPath = primPath.AppendProperty(TfToken("xformOpOrder"));
        const SdfPath matrixPath = primPath.AppendProperty(TfToken("xformOp:transform"));

        if (state.hadXformOpOrderSpec) {
            if (state.hadXformOpOrderDefault)
                editLayer->SetField(orderPath, SdfFieldKeys->Default, state.xformOpOrderDefault);
            else
                editLayer->EraseField(orderPath, SdfFieldKeys->Default);
        }
        else if (!removePropertySpec(editLayer, orderPath)) {
            error = QString("failed to remove transform order override: %1").arg(pathText(primPath));
            return false;
        }

        if (state.hadMatrixOpSpec) {
            if (state.hadMatrixOpDefault)
                editLayer->SetField(matrixPath, SdfFieldKeys->Default, state.matrixOpDefault);
            else
                editLayer->EraseField(matrixPath, SdfFieldKeys->Default);
        }
        else if (!removePropertySpec(editLayer, matrixPath)) {
            error = QString("failed to remove matrix transform override: %1").arg(pathText(primPath));
            return false;
        }

        return true;
    }
    bool hasUnderlyingTransformOpinion(UsdStageRefPtr stage, const UsdPrim& prim, const SdfLayerHandle& editLayer)
    {
        if (!stage || !prim || !editLayer)
            return false;

        const TfToken orderToken("xformOpOrder");

        for (const SdfPrimSpecHandle& primSpec : prim.GetPrimStack()) {
            if (!primSpec)
                continue;

            const SdfLayerHandle layer = primSpec->GetLayer();
            if (!layer || layer == editLayer)
                continue;

            const SdfPath orderPath = primSpec->GetPath().AppendProperty(orderToken);
            if (layer->HasField(orderPath, SdfFieldKeys->Default))
                return true;
        }

        return false;
    }


    struct TransformPropertyState {
        SdfPath primPath;
        SdfLayerRefPtr snapshotLayer;
        QList<SdfPath> propertyPaths;
    };

    bool isTransformPropertyPath(const SdfPath& propertyPath)
    {
        if (!propertyPath.IsPropertyPath())
            return false;

        const std::string name = propertyPath.GetName();
        return name == "xformOpOrder" || name.rfind("xformOp:", 0) == 0;
    }

    bool captureTransformPropertyState(const SdfLayerHandle& editLayer, const SdfPath& primPath,
                                       TransformPropertyState& state, QString& error)
    {
        if (!editLayer || primPath.IsEmpty()) {
            error = "edit layer or prim path missing";
            return false;
        }

        state.primPath = primPath;
        state.snapshotLayer = SdfLayer::CreateAnonymous("stageviz_transform_snapshot.usda");
        state.propertyPaths.clear();

        if (!state.snapshotLayer) {
            error = "failed to create transform snapshot";
            return false;
        }

        SdfCreatePrimInLayer(state.snapshotLayer, primPath);

        const SdfPrimSpecHandle primSpec = editLayer->GetPrimAtPath(primPath);
        if (!primSpec)
            return true;

        for (const SdfPropertySpecHandle& property : primSpec->GetProperties()) {
            if (!property)
                continue;

            const SdfPath propertyPath = property->GetPath();
            if (!isTransformPropertyPath(propertyPath))
                continue;

            if (!SdfCopySpec(editLayer, propertyPath, state.snapshotLayer, propertyPath)) {
                error = QString("failed to snapshot transform property: %1").arg(pathText(propertyPath));
                return false;
            }

            state.propertyPaths.append(propertyPath);
        }

        return true;
    }

    bool clearEditLayerTransformProperties(const SdfLayerHandle& editLayer, const SdfPath& primPath, QString& error)
    {
        if (!editLayer || primPath.IsEmpty())
            return false;

        const SdfPrimSpecHandle primSpec = editLayer->GetPrimAtPath(primPath);
        if (!primSpec)
            return true;

        QList<SdfPath> paths;
        for (const SdfPropertySpecHandle& property : primSpec->GetProperties()) {
            if (property && isTransformPropertyPath(property->GetPath()))
                paths.append(property->GetPath());
        }

        for (const SdfPath& propertyPath : paths) {
            if (!removePropertySpec(editLayer, propertyPath)) {
                error = QString("failed to remove transform property: %1").arg(pathText(propertyPath));
                return false;
            }
        }

        return true;
    }

    bool restoreTransformPropertyState(const SdfLayerHandle& editLayer, const TransformPropertyState& state,
                                       QString& error)
    {
        if (!editLayer || state.primPath.IsEmpty() || !state.snapshotLayer)
            return false;

        if (!clearEditLayerTransformProperties(editLayer, state.primPath, error))
            return false;

        if (!editLayer->GetPrimAtPath(state.primPath))
            SdfCreatePrimInLayer(editLayer, state.primPath);

        for (const SdfPath& propertyPath : state.propertyPaths) {
            if (!SdfCopySpec(state.snapshotLayer, propertyPath, editLayer, propertyPath)) {
                error = QString("failed to restore transform property: %1").arg(pathText(propertyPath));
                return false;
            }
        }

        const SdfPrimSpecHandle remaining = editLayer->GetPrimAtPath(state.primPath);
        if (remaining && remaining->IsInert())
            stage::removePrimSpec(editLayer, state.primPath);

        return true;
    }

    bool matrixClose(const GfMatrix4d& a, const GfMatrix4d& b, double tolerance = 1.0e-8)
    {
        for (int row = 0; row < 4; ++row) {
            for (int column = 0; column < 4; ++column) {
                if (std::abs(a[row][column] - b[row][column]) > tolerance)
                    return false;
            }
        }
        return true;
    }

    bool authorCanonicalTransformWithPivot(const UsdPrim& prim, const GfMatrix4d& localMatrix,
                                           const GfVec3d& localPivot, QString& error)
    {
        const UsdGeomXformable xformable(prim);
        if (!xformable) {
            error = "prim is not xformable";
            return false;
        }

        UsdGeomXformOp::Precision pivotPrecision = UsdGeomXformOp::PrecisionDouble;
        const UsdAttribute existingPivotAttr = prim.GetAttribute(TfToken("xformOp:translate:pivot"));
        if (existingPivotAttr) {
            const UsdGeomXformOp existingPivotOp(existingPivotAttr);
            if (existingPivotOp)
                pivotPrecision = existingPivotOp.GetPrecision();
        }

        xformable.ClearXformOpOrder();

        const UsdGeomXformOp pivotOp = xformable.AddTranslateOp(pivotPrecision, TfToken("pivot"), false);
        const UsdGeomXformOp matrixOp = xformable.AddTransformOp(UsdGeomXformOp::PrecisionDouble);
        const UsdGeomXformOp inversePivotOp = xformable.AddTranslateOp(pivotPrecision, TfToken("pivot"), true);

        bool pivotSet = false;
        if (pivotOp) {
            switch (pivotPrecision) {
            case UsdGeomXformOp::PrecisionHalf:
                pivotSet = pivotOp.Set(GfVec3h(static_cast<float>(localPivot[0]), static_cast<float>(localPivot[1]),
                                               static_cast<float>(localPivot[2])));
                break;
            case UsdGeomXformOp::PrecisionFloat:
                pivotSet = pivotOp.Set(GfVec3f(static_cast<float>(localPivot[0]), static_cast<float>(localPivot[1]),
                                               static_cast<float>(localPivot[2])));
                break;
            case UsdGeomXformOp::PrecisionDouble:
            default:
                pivotSet = pivotOp.Set(localPivot);
                break;
            }
        }

        if (!pivotOp || !matrixOp || !inversePivotOp || !pivotSet) {
            error = "failed to author pivot transform ops";
            return false;
        }

        GfMatrix4d pivot(1.0);
        pivot.SetTranslate(localPivot);
        GfMatrix4d inversePivot(1.0);
        inversePivot.SetTranslate(-localPivot);

        const GfMatrix4d candidates[] = {
            pivot * localMatrix * inversePivot,
            inversePivot * localMatrix * pivot,
        };

        for (const GfMatrix4d& candidate : candidates) {
            if (!matrixOp.Set(candidate))
                continue;

            GfMatrix4d evaluated(1.0);
            bool resetsXformStack = false;
            if (!xformable.GetLocalTransformation(&evaluated, &resetsXformStack, UsdTimeCode::Default()))
                continue;

            if (matrixClose(evaluated, localMatrix))
                return true;
        }

        error = "failed to preserve local transform while authoring pivot";
        return false;
    }

    bool authorCanonicalMatrixTransform(const UsdPrim& prim, const GfMatrix4d& localMatrix, QString& error)
    {
        const UsdGeomXformable xformable(prim);
        if (!xformable) {
            error = "prim is not xformable";
            return false;
        }

        xformable.ClearXformOpOrder();
        const UsdGeomXformOp matrixOp = xformable.AddTransformOp(UsdGeomXformOp::PrecisionDouble);
        if (!matrixOp || !matrixOp.Set(localMatrix)) {
            error = "failed to author matrix transform";
            return false;
        }

        return true;
    }


    struct MaterialBindingState {
        SdfPath primPath;
        bool hadDirectBinding = false;
        SdfPath previousMaterialPath;
    };

    bool captureDirectMaterialBinding(const UsdPrim& prim, MaterialBindingState& state)
    {
        if (!prim)
            return false;

        state.primPath = prim.GetPath();
        state.hadDirectBinding = false;
        state.previousMaterialPath = SdfPath();

        const UsdShadeMaterialBindingAPI bindingApi(prim);
        const UsdRelationship relationship = bindingApi.GetDirectBindingRel();
        if (!relationship)
            return true;

        SdfPathVector targets;
        if (!relationship.GetTargets(&targets) || targets.empty())
            return true;

        state.hadDirectBinding = true;
        state.previousMaterialPath = targets.front();
        return true;
    }

    bool applyDirectMaterialBinding(UsdStageRefPtr stage, const SdfPath& primPath, const SdfPath& materialPath,
                                    QString& error)
    {
        if (!stage) {
            error = "stage missing";
            return false;
        }

        const UsdPrim prim = stage->GetPrimAtPath(primPath);
        if (!prim || !prim.IsValid()) {
            error = QString("target prim missing: %1").arg(pathText(primPath));
            return false;
        }

        const UsdPrim materialPrim = stage->GetPrimAtPath(materialPath);
        if (!materialPrim || !materialPrim.IsA<UsdShadeMaterial>()) {
            error = QString("material missing: %1").arg(pathText(materialPath));
            return false;
        }

        UsdShadeMaterialBindingAPI bindingApi = UsdShadeMaterialBindingAPI::Apply(prim);
        if (!bindingApi) {
            error = QString("could not apply material binding API: %1").arg(pathText(primPath));
            return false;
        }

        if (!bindingApi.Bind(UsdShadeMaterial(materialPrim))) {
            error = QString("failed to bind material to: %1").arg(pathText(primPath));
            return false;
        }

        return true;
    }

    bool restoreDirectMaterialBinding(UsdStageRefPtr stage, const MaterialBindingState& state, QString& error)
    {
        if (!stage) {
            error = "stage missing";
            return false;
        }

        const UsdPrim prim = stage->GetPrimAtPath(state.primPath);
        if (!prim || !prim.IsValid()) {
            error = QString("target prim missing: %1").arg(pathText(state.primPath));
            return false;
        }

        UsdShadeMaterialBindingAPI bindingApi = UsdShadeMaterialBindingAPI::Apply(prim);
        if (!bindingApi) {
            error = QString("could not apply material binding API: %1").arg(pathText(state.primPath));
            return false;
        }

        if (!state.hadDirectBinding) {
            if (!bindingApi.UnbindDirectBinding()) {
                error = QString("failed to remove material binding from: %1").arg(pathText(state.primPath));
                return false;
            }
            return true;
        }

        const UsdPrim materialPrim = stage->GetPrimAtPath(state.previousMaterialPath);
        if (!materialPrim || !materialPrim.IsA<UsdShadeMaterial>()) {
            error = QString("previous material missing: %1").arg(pathText(state.previousMaterialPath));
            return false;
        }

        if (!bindingApi.Bind(UsdShadeMaterial(materialPrim))) {
            error = QString("failed to restore material binding on: %1").arg(pathText(state.primPath));
            return false;
        }

        return true;
    }

}  // namespace


Command
bindMaterial(const QList<SdfPath>& inPaths, const SdfPath& materialPath)
{
    auto states = std::make_shared<QList<MaterialBindingState>>();

    return Command(
        [inPaths, materialPath, states](Session* session) {
            if (!session || inPaths.isEmpty() || materialPath.IsEmpty())
                return;

            const QList<SdfPath> paths = path::uniquePaths(inPaths);
            command::beginDeferred(session, "Bind material", static_cast<int>(paths.size()));

            command::runWorker([session, paths, materialPath, states]() {
                QList<SdfPath> changed;
                QStringList errors;

                {
                    WRITE_LOCKER(locker, session->stageLock(), "stageLock");
                    const UsdStageRefPtr stage = session->stageUnsafe();

                    if (!stage) {
                        errors.append("stage missing");
                    }
                    else {
                        const UsdPrim materialPrim = stage->GetPrimAtPath(materialPath);
                        if (!materialPrim || !materialPrim.IsA<UsdShadeMaterial>()) {
                            errors.append(QString("material missing: %1").arg(pathText(materialPath)));
                        }
                        else {
                            if (states->isEmpty()) {
                                states->reserve(paths.size());

                                for (const SdfPath& inputPath : paths) {
                                    const SdfPath primPath = inputPath.IsPropertyPath() ? inputPath.GetPrimPath()
                                                                                        : inputPath;
                                    const UsdPrim prim = stage->GetPrimAtPath(primPath);
                                    if (!prim || !prim.IsValid()) {
                                        errors.append(QString("target prim missing: %1").arg(pathText(primPath)));
                                        continue;
                                    }

                                    MaterialBindingState state;
                                    if (!captureDirectMaterialBinding(prim, state)) {
                                        errors.append(
                                            QString("failed to capture material binding: %1").arg(pathText(primPath)));
                                        continue;
                                    }

                                    states->append(state);
                                }
                            }

                            for (const MaterialBindingState& state : *states) {
                                QString error;
                                if (applyDirectMaterialBinding(stage, state.primPath, materialPath, error))
                                    path::appendUnique(changed, state.primPath);
                                else
                                    errors.append(error);
                            }
                        }
                    }
                }

                const bool success = errors.isEmpty();
                const QString errorText = summarizeErrors(errors);
                command::queueToSession(session, [session, changed, success, errorText]() {
                    using Status = Session::Notify::Status;
                    command::finishDeferred(session,
                                            success ? "Material bound"
                                                    : appendError("Bind material finished with errors", errorText),
                                            changed, success ? Status::Success : Status::Error);
                });
            });
        },
        [states](Session* session) {
            if (!session || states->isEmpty())
                return;

            command::beginDeferred(session, "Undo bind material", static_cast<int>(states->size()));

            command::runWorker([session, states]() {
                QList<SdfPath> changed;
                QStringList errors;

                {
                    WRITE_LOCKER(locker, session->stageLock(), "stageLock");
                    const UsdStageRefPtr stage = session->stageUnsafe();

                    if (!stage) {
                        errors.append("stage missing");
                    }
                    else {
                        for (const MaterialBindingState& state : *states) {
                            QString error;
                            if (restoreDirectMaterialBinding(stage, state, error))
                                path::appendUnique(changed, state.primPath);
                            else
                                errors.append(error);
                        }
                    }
                }

                const bool success = errors.isEmpty();
                const QString errorText = summarizeErrors(errors);
                command::queueToSession(session, [session, changed, success, errorText]() {
                    using Status = Session::Notify::Status;
                    command::finishDeferred(session,
                                            success ? "Material binding undone"
                                                    : appendError("Undo bind material finished with errors", errorText),
                                            changed, success ? Status::Success : Status::Error);
                });
            });
        });
}

Command
loadPayloads(const QList<SdfPath>& paths, const QString& variantSet, const QString& variantValue)
{
    auto state = std::make_shared<payload::State>();

    return Command(
        [paths, variantSet, variantValue, state](Session* session) {
            if (!session || paths.isEmpty())
                return;

            state->previousSelection = session->selectionList()->paths();
            state->previousMask = session->mask();

            session->beginProgressBlock("Load payloads", paths.size());
            session->setPrimsUpdate(Session::PrimsUpdate::Deferred);

            command::runWorker([session, paths, variantSet, variantValue, state]() {
                const bool useVariant = !variantSet.isEmpty() && !variantValue.isEmpty();
                const std::string variantSetName = qt::QStringToString(variantSet);
                const std::string variantSelection = qt::QStringToString(variantValue);

                QList<command::Result> pending;
                pending.reserve(16);

                QList<payload::PayloadState> payloadStates;
                payloadStates.reserve(paths.size());

                int completed = 0;
                for (const SdfPath& path : paths) {
                    if (!session || session->isProgressBlockCancelled())
                        break;

                    command::Result result;
                    result.path = path;
                    result.message = "Payload failed";
                    result.status = Session::Notify::Status::Error;

                    payload::PayloadState payloadState;
                    QString error;

                    try {
                        WRITE_LOCKER(locker, session->stageLock(), "stageLock");
                        const UsdStageRefPtr stage = session->stageUnsafe();

                        if (!stage) {
                            result.success = false;
                            error = "Stage not available";
                        }
                        else {
                            result.success = payload::applyLoad(stage, path, useVariant, variantSetName,
                                                                variantSelection, payloadState, error);

                            if (result.success) {
                                const QString pathString = qt::SdfPathToQString(path);
                                const PcpErrorVector errors = stage->GetCompositionErrors();

                                for (const PcpErrorBasePtr& compositionError : errors) {
                                    if (!compositionError)
                                        continue;

                                    const QString text = qt::StringToQString(compositionError->ToString());

                                    if (!text.contains(pathString))
                                        continue;

                                    QString restoreError;
                                    payload::restoreState(stage, payloadState, restoreError);

                                    result.success = false;
                                    error = QString("Payload failed to load: %1").arg(pathString);

                                    if (!restoreError.isEmpty())
                                        error += QString(" (%1)").arg(restoreError);

                                    break;
                                }
                            }
                        }
                    } catch (...) {
                        result.success = false;
                        error = "exception";
                    }

                    result.message = result.success ? "Payload loaded" : (error.isEmpty() ? "Payload failed" : error);

                    result.status = result.success ? Session::Notify::Status::Success : Session::Notify::Status::Error;

                    if (result.success)
                        payloadStates.append(payloadState);

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

                if (!pending.isEmpty()) {
                    const QList<command::Result> batch = pending;
                    command::queueToSession(session, [session, batch, completed]() {
                        command::flushResults(session, batch, completed);
                    });
                }

                state->payloadStates = payloadStates;

                command::queueToSession(session, [session]() {
                    session->setPrimsUpdate(Session::PrimsUpdate::Immediate);
                    session->endProgressBlock();
                });
            });
        },
        [state](Session* session) {
            if (!session || state->payloadStates.isEmpty())
                return;

            session->beginProgressBlock("Undo load payloads", state->payloadStates.size());
            session->setPrimsUpdate(Session::PrimsUpdate::Deferred);

            command::runWorker([session, state]() {
                QList<command::Result> pending;
                pending.reserve(16);

                int completed = 0;
                for (const payload::PayloadState& payloadState : state->payloadStates) {
                    if (!session || session->isProgressBlockCancelled())
                        break;

                    command::Result result;
                    result.path = payloadState.path;
                    result.message = "Payload undo failed";
                    result.status = Session::Notify::Status::Error;

                    QString error;
                    try {
                        WRITE_LOCKER(locker, session->stageLock(), "stageLock");
                        const UsdStageRefPtr stage = session->stageUnsafe();
                        result.success = payload::restoreState(stage, payloadState, error);
                    } catch (...) {
                        result.success = false;
                        error = "exception";
                    }

                    result.message = result.success ? "Payload undone"
                                                    : (error.isEmpty() ? "Payload undo failed" : error);

                    result.status = result.success ? Session::Notify::Status::Success : Session::Notify::Status::Error;

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

                command::queueToSession(session, [session, state, pending, completed]() {
                    if (!pending.isEmpty())
                        command::flushResults(session, pending, completed);

                    session->selectionList()->updatePaths(state->previousSelection);
                    session->setMask(state->previousMask);
                    session->setPrimsUpdate(Session::PrimsUpdate::Immediate);
                    session->endProgressBlock();
                });
            });
        });
}

Command
loadNeighborPayloads(const QList<SdfPath>& paths)
{
    auto state = std::make_shared<payload::State>();

    return Command(
        [paths, state](Session* session) {
            if (!session || paths.isEmpty())
                return;

            state->previousSelection = session->selectionList()->paths();
            state->previousMask = session->mask();
            state->payloadStates.clear();

            command::runWorker([session, paths, state]() {
                QList<SdfPath> targets;

                {
                    READ_LOCKER(locker, session->stageLock(), "stageLock");

                    const UsdStageRefPtr stage = session->stageUnsafe();
                    if (stage) {
                        targets = payload::neighboringPaths(stage, paths);

                        const QList<SdfPath> sourcePaths = stage::topMostPayloadPaths(stage, paths);
                        for (const SdfPath& sourcePath : sourcePaths) {
                            if (!stage::isLoaded(stage, sourcePath))
                                path::appendUnique(targets, sourcePath);
                        }

                        QList<SdfPath> unloadedTargets;
                        unloadedTargets.reserve(targets.size());

                        for (const SdfPath& target : targets) {
                            if (!stage::isLoaded(stage, target))
                                unloadedTargets.append(target);
                        }

                        targets = unloadedTargets;
                    }
                }

                if (targets.isEmpty()) {
                    command::queueToSession(session, [session]() {
                        using Status = Session::Notify::Status;

                        session->beginProgressBlock("Load neighbor payloads", 1);
                        session->updateProgressNotify(Session::Notify("No unloaded neighbor payloads found", {},
                                                                      Status::Success),
                                                      1);
                        session->endProgressBlock();
                    });
                    return;
                }

                command::queueToSession(session, [session, state, targets]() {
                    session->beginProgressBlock("Load neighbor payloads", targets.size());
                    session->setPrimsUpdate(Session::PrimsUpdate::Deferred);
                    session->updateProgressNotify(
                        Session::Notify(QString("Loading %1 neighbor payloads").arg(targets.size()), {},
                                        Session::Notify::Status::Success),
                        0);

                    command::runWorker([session, state, targets]() {
                        QList<command::Result> pending;
                        pending.reserve(16);

                        QList<payload::PayloadState> payloadStates;
                        payloadStates.reserve(targets.size());

                        int completed = 0;

                        for (const SdfPath& path : targets) {
                            if (!session || session->isProgressBlockCancelled())
                                break;

                            command::Result result;
                            result.path = path;
                            result.message = "Neighbor payload failed";
                            result.status = Session::Notify::Status::Error;

                            payload::PayloadState payloadState;
                            QString loadError;

                            try {
                                WRITE_LOCKER(locker, session->stageLock(), "stageLock");

                                const UsdStageRefPtr stage = session->stageUnsafe();

                                if (!stage) {
                                    result.success = false;
                                    loadError = "Stage not available";
                                }
                                else {
                                    result.success = payload::applyLoad(stage, path, false, std::string(),
                                                                        std::string(), payloadState, loadError);
                                }
                            } catch (...) {
                                result.success = false;
                                loadError = "exception";
                            }

                            result.message = result.success
                                                 ? "Neighbor payload loaded"
                                                 : (loadError.isEmpty() ? "Neighbor payload failed" : loadError);

                            result.status = result.success ? Session::Notify::Status::Success
                                                           : Session::Notify::Status::Error;

                            if (result.success)
                                payloadStates.append(payloadState);

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

                        state->payloadStates = payloadStates;

                        command::queueToSession(session, [session, pending, completed]() {
                            if (!pending.isEmpty())
                                command::flushResults(session, pending, completed);

                            session->setPrimsUpdate(Session::PrimsUpdate::Immediate);
                            session->endProgressBlock();
                        });
                    });
                });
            });
        },
        [state](Session* session) {
            if (!session || state->payloadStates.isEmpty())
                return;

            session->beginProgressBlock("Undo load neighbor payloads", state->payloadStates.size());
            session->setPrimsUpdate(Session::PrimsUpdate::Deferred);

            command::runWorker([session, state]() {
                QList<command::Result> pending;
                pending.reserve(16);

                int completed = 0;

                for (const payload::PayloadState& payloadState : state->payloadStates) {
                    if (!session || session->isProgressBlockCancelled())
                        break;

                    command::Result result;
                    result.path = payloadState.path;
                    result.message = "Neighbor payload undo failed";
                    result.status = Session::Notify::Status::Error;

                    QString error;

                    try {
                        WRITE_LOCKER(locker, session->stageLock(), "stageLock");

                        const UsdStageRefPtr stage = session->stageUnsafe();
                        result.success = payload::restoreState(stage, payloadState, error);
                    } catch (...) {
                        result.success = false;
                        error = "exception";
                    }

                    result.message = result.success ? "Neighbor payload restored"
                                                    : (error.isEmpty() ? "Neighbor payload undo failed" : error);

                    result.status = result.success ? Session::Notify::Status::Success : Session::Notify::Status::Error;

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

                command::queueToSession(session, [session, state, pending, completed]() {
                    if (!pending.isEmpty())
                        command::flushResults(session, pending, completed);

                    session->selectionList()->updatePaths(state->previousSelection);
                    session->setMask(state->previousMask);
                    session->setPrimsUpdate(Session::PrimsUpdate::Immediate);
                    session->endProgressBlock();
                });
            });
        });
}

Command
unloadPayloads(const QList<SdfPath>& paths)
{
    auto state = std::make_shared<payload::State>();

    return Command(
        [paths, state](Session* session) {
            if (!session || paths.isEmpty())
                return;

            state->previousSelection = session->selectionList()->paths();
            state->previousMask = session->mask();

            session->beginProgressBlock("Unload payloads", paths.size());
            session->setPrimsUpdate(Session::PrimsUpdate::Deferred);

            command::runWorker([session, paths, state]() {
                QList<command::Result> pending;
                pending.reserve(16);

                QList<payload::PayloadState> payloadStates;
                payloadStates.reserve(paths.size());

                QList<SdfPath> unloadedPaths;
                unloadedPaths.reserve(paths.size());

                int completed = 0;
                for (const SdfPath& path : paths) {
                    if (!session || session->isProgressBlockCancelled())
                        break;

                    command::Result result;
                    result.path = path;
                    result.message = "Payload unload failed";
                    result.status = Session::Notify::Status::Error;

                    payload::PayloadState payloadState;
                    QString error;

                    try {
                        WRITE_LOCKER(locker, session->stageLock(), "stageLock");
                        const UsdStageRefPtr stage = session->stageUnsafe();
                        result.success = payload::applyUnload(stage, path, payloadState, error);
                    } catch (...) {
                        result.success = false;
                        error = "exception";
                    }

                    result.message = result.success ? "Payload unloaded" : "Payload unload failed";
                    result.status = result.success ? Session::Notify::Status::Success : Session::Notify::Status::Error;

                    if (result.success) {
                        payloadStates.append(payloadState);
                        unloadedPaths.append(path);
                    }

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

                if (!pending.isEmpty()) {
                    const QList<command::Result> batch = pending;
                    command::queueToSession(session, [session, batch, completed]() {
                        command::flushResults(session, batch, completed);
                    });
                }

                state->payloadStates = payloadStates;

                command::queueToSession(session, [session, state, unloadedPaths]() {
                    session->selectionList()->updatePaths(
                        path::removeAffectedPaths(state->previousSelection, unloadedPaths));
                    session->setMask(path::removeAffectedPaths(state->previousMask, unloadedPaths));
                    session->setPrimsUpdate(Session::PrimsUpdate::Immediate);
                    session->endProgressBlock();
                });
            });
        },
        [state](Session* session) {
            if (!session || state->payloadStates.isEmpty())
                return;

            session->beginProgressBlock("Undo unload payloads", state->payloadStates.size());
            session->setPrimsUpdate(Session::PrimsUpdate::Deferred);

            command::runWorker([session, state]() {
                QList<command::Result> pending;
                pending.reserve(16);

                int completed = 0;
                for (const payload::PayloadState& payloadState : state->payloadStates) {
                    if (!session || session->isProgressBlockCancelled())
                        break;

                    command::Result result;
                    result.path = payloadState.path;
                    result.message = "Payload undo failed";
                    result.status = Session::Notify::Status::Error;

                    QString error;
                    try {
                        WRITE_LOCKER(locker, session->stageLock(), "stageLock");
                        const UsdStageRefPtr stage = session->stageUnsafe();
                        result.success = payload::restoreState(stage, payloadState, error);
                    } catch (...) {
                        result.success = false;
                        error = "exception";
                    }

                    result.message = result.success ? "Payload restored" : "Payload undo failed";
                    result.status = result.success ? Session::Notify::Status::Success : Session::Notify::Status::Error;

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

                command::queueToSession(session, [session, state, pending, completed]() {
                    if (!pending.isEmpty())
                        command::flushResults(session, pending, completed);

                    session->selectionList()->updatePaths(state->previousSelection);
                    session->setMask(state->previousMask);
                    session->setPrimsUpdate(Session::PrimsUpdate::Immediate);
                    session->endProgressBlock();
                });
            });
        });
}

Command
selectPayload()
{
    struct SelectPayloadState {
        QList<SdfPath> previousSelection;
    };

    auto state = std::make_shared<SelectPayloadState>();

    return Command(
        [state](Session* session) {
            if (!session)
                return;

            const QList<SdfPath> selection = session->selectionList()->paths();
            if (selection.isEmpty())
                return;

            QList<SdfPath> payloadPaths;
            {
                READ_LOCKER(locker, session->stageLock(), "stageLock");
                const UsdStageRefPtr stage = session->stageUnsafe();
                if (!stage)
                    return;

                payloadPaths = stage::ancestorPayloadPaths(stage, selection);
            }

            if (payloadPaths.isEmpty())
                return;

            state->previousSelection = selection;
            session->selectionList()->updatePaths(payloadPaths);
        },
        [state](Session* session) {
            if (!session)
                return;

            session->selectionList()->updatePaths(state->previousSelection);
        });
}

Command
selectInvertPayload()
{
    struct SelectInvertPayloadState {
        QList<SdfPath> previousSelection;
    };

    auto state = std::make_shared<SelectInvertPayloadState>();

    return Command(
        [state](Session* session) {
            if (!session)
                return;

            session->beginProgressBlock("Invert payload selection", 1);

            command::runWorker([session, state]() {
                using Status = Session::Notify::Status;

                QList<SdfPath> previousSelection;
                QList<SdfPath> invertedPayloads;
                QList<command::Result> pending;
                pending.reserve(16);

                bool hadStage = true;
                bool hadSelectedPayloads = false;
                int completed = 0;
                qsizetype total = 0;

                {
                    READ_LOCKER(locker, session->stageLock(), "stageLock");
                    const UsdStageRefPtr stage = session->stageUnsafe();

                    if (!stage) {
                        hadStage = false;
                    }
                    else {
                        previousSelection = session->selectionList()->paths();
                        const QList<SdfPath> selectedPayloads = stage::topMostPayloadPaths(stage, previousSelection);

                        state->previousSelection = previousSelection;
                        hadSelectedPayloads = !selectedPayloads.isEmpty();

                        QList<SdfPath> payloads;
                        for (const UsdPrim& prim : stage->TraverseAll()) {
                            if (!prim || !prim.IsValid())
                                continue;

                            const SdfPath path = prim.GetPath();
                            if (path.IsEmpty() || path == SdfPath::AbsoluteRootPath())
                                continue;

                            if (!stage::isPayload(stage, path))
                                continue;

                            if (!prim.IsLoaded())
                                continue;

                            payloads.append(path);
                        }

                        payloads = path::topLevelPaths(payloads);
                        total = payloads.size();

                        const QSet<SdfPath> selectedSet(selectedPayloads.begin(), selectedPayloads.end());

                        for (const SdfPath& path : payloads) {
                            const bool selected = selectedSet.contains(path);

                            if (!selected)
                                invertedPayloads.append(path);

                            command::Result result;
                            result.path = path;
                            result.success = true;
                            result.message = selected ? "Payload skipped" : "Payload inverted";
                            result.status = Status::Success;
                            pending.append(result);
                            ++completed;

                            if (pending.size() >= 16) {
                                const QList<command::Result> batch = pending;
                                command::queueToSession(session, [session, batch, completed]() {
                                    command::flushResults(session, batch, completed);
                                });
                                pending.clear();
                            }

                            if (session->isProgressBlockCancelled())
                                break;
                        }
                    }
                }

                if (!pending.isEmpty()) {
                    const QList<command::Result> batch = pending;
                    command::queueToSession(session, [session, batch, completed]() {
                        command::flushResults(session, batch, completed);
                    });
                }

                command::queueToSession(session, [session, hadStage, hadSelectedPayloads, invertedPayloads, total]() {
                    using Status = Session::Notify::Status;

                    if (!hadStage) {
                        session->updateProgressNotify(Session::Notify("Invert payload selection failed", {},
                                                                      Status::Error),
                                                      1);
                        session->endProgressBlock();
                        return;
                    }

                    if (!hadSelectedPayloads) {
                        session->updateProgressNotify(Session::Notify("Invert payload selection skipped", {},
                                                                      Status::Success),
                                                      1);
                        session->endProgressBlock();
                        return;
                    }

                    session->selectionList()->updatePaths(invertedPayloads);
                    session->updateProgressNotify(Session::Notify(total > 0 ? "Payload selection inverted"
                                                                            : "Invert payload selection skipped",
                                                                  invertedPayloads, Status::Success),
                                                  qMax(1, total));

                    session->endProgressBlock();
                });
            });
        },
        [state](Session* session) {
            if (!session)
                return;

            session->beginProgressBlock("Undo invert payload selection", 1);

            command::queueToSession(session, [session, state]() {
                using Status = Session::Notify::Status;
                session->selectionList()->updatePaths(state->previousSelection);
                session->updateProgressNotify(Session::Notify("Invert payload selection undone",
                                                              state->previousSelection, Status::Success),
                                              1);
                session->endProgressBlock();
            });
        });
}

Command
isolatePaths(const QList<SdfPath>& paths)
{
    auto state = std::make_shared<QList<SdfPath>>();

    return Command(
        [paths, state](Session* session) {
            session->beginProgressBlock("Isolate paths", 1);

            command::runWorker([session, paths, state]() {
                *state = session->mask();
                session->setMask(paths);

                command::queueToSession(session, [session, paths]() {
                    using Status = Session::Notify::Status;
                    session->updateProgressNotify(Session::Notify("Paths isolated", paths, Status::Success), 1);
                    session->endProgressBlock();
                });
            });
        },
        [state](Session* session) {
            session->beginProgressBlock("undo isolate paths", 1);

            command::runWorker([session, state]() {
                session->setMask(*state);

                command::queueToSession(session, [session, state]() {
                    using Status = Session::Notify::Status;
                    session->updateProgressNotify(Session::Notify("Isolate undone", *state, Status::Success), 1);
                    session->endProgressBlock();
                });
            });
        });
}

Command
selectPaths(const QList<SdfPath>& paths)
{
    auto previous = std::make_shared<QList<SdfPath>>();

    return Command(
        [paths, previous](Session* session) {
            session->beginProgressBlock("Select paths", 1);

            command::runWorker([session, paths, previous]() {
                *previous = session->selectionList()->paths();
                session->selectionList()->updatePaths(paths);

                command::queueToSession(session, [session, paths]() {
                    using Status = Session::Notify::Status;
                    session->updateProgressNotify(Session::Notify("Paths selected", paths, Status::Success), 1);
                    session->endProgressBlock();
                });
            });
        },
        [previous](Session* session) {
            session->beginProgressBlock("Undo select paths", 1);

            command::runWorker([session, previous]() {
                session->selectionList()->updatePaths(*previous);

                command::queueToSession(session, [session, previous]() {
                    using Status = Session::Notify::Status;
                    session->updateProgressNotify(Session::Notify("Select undone", *previous, Status::Success), 1);
                    session->endProgressBlock();
                });
            });
        });
}

Command
selectAll(bool recursive)
{
    struct SelectAllState {
        QList<SdfPath> previousSelection;
    };

    auto state = std::make_shared<SelectAllState>();

    return Command(
        [recursive, state](Session* session) {
            if (!session)
                return;

            session->beginProgressBlock("Select all", 1);

            command::runWorker([session, recursive, state]() {
                QList<SdfPath> selection;

                {
                    READ_LOCKER(locker, session->stageLock(), "stageLock");

                    const UsdStageRefPtr stage = session->stageUnsafe();
                    if (!stage)
                        return;

                    state->previousSelection = session->selectionList()->paths();

                    if (recursive) {
                        for (const UsdPrim& prim : stage->Traverse()) {
                            if (!prim || !prim.IsValid())
                                continue;

                            selection.append(prim.GetPath());
                        }
                    }
                    else {
                        for (const UsdPrim& prim : stage->GetPseudoRoot().GetChildren()) {
                            if (!prim || !prim.IsValid())
                                continue;

                            selection.append(prim.GetPath());
                        }
                    }
                }

                command::queueToSession(session, [session, selection]() {
                    using Status = Session::Notify::Status;

                    session->selectionList()->updatePaths(selection);
                    session->updateProgressNotify(Session::Notify("Paths selected", selection, Status::Success), 1);
                    session->endProgressBlock();
                });
            });
        },
        [state](Session* session) {
            if (!session)
                return;

            session->beginProgressBlock("Undo select all", 1);

            command::runWorker([session, state]() {
                command::queueToSession(session, [session, state]() {
                    using Status = Session::Notify::Status;

                    session->selectionList()->updatePaths(state->previousSelection);
                    session->updateProgressNotify(Session::Notify("Select all undone", state->previousSelection,
                                                                  Status::Success),
                                                  1);
                    session->endProgressBlock();
                });
            });
        });
}

Command
selectInvert()
{
    struct SelectInvertState {
        QList<SdfPath> previousSelection;
    };

    auto state = std::make_shared<SelectInvertState>();

    return Command(
        [state](Session* session) {
            if (!session)
                return;

            session->beginProgressBlock("Invert selection", 1);

            command::runWorker([session, state]() {
                QList<SdfPath> invertedSelection;
                QList<SdfPath> previousSelection;
                QList<SdfPath> domain;
                QList<SdfPath> mask;

                {
                    READ_LOCKER(locker, session->stageLock(), "stageLock");
                    const UsdStageRefPtr stage = session->stageUnsafe();
                    previousSelection = session->selectionList()->paths();
                    mask = session->mask();

                    domain = stage::leafPaths(stage, mask, false);
                }

                state->previousSelection = previousSelection;

                for (const SdfPath& path : domain) {
                    if (!path::isCoveredByRoots(previousSelection, path))
                        invertedSelection.append(path);
                }

                command::queueToSession(session, [session, invertedSelection]() {
                    using Status = Session::Notify::Status;
                    session->selectionList()->updatePaths(invertedSelection);
                    session->updateProgressNotify(Session::Notify("Selection inverted", invertedSelection,
                                                                  Status::Success),
                                                  1);
                    session->endProgressBlock();
                });
            });
        },
        [state](Session* session) {
            if (!session)
                return;

            session->beginProgressBlock("Undo invert selection", 1);

            command::runWorker([session, state]() {
                command::queueToSession(session, [session, state]() {
                    using Status = Session::Notify::Status;
                    session->selectionList()->updatePaths(state->previousSelection);
                    session->updateProgressNotify(Session::Notify("Invert selection undone", state->previousSelection,
                                                                  Status::Success),
                                                  1);
                    session->endProgressBlock();
                });
            });
        });
}

Command
showPaths(const QList<SdfPath>& paths, bool recursive)
{
    struct VisibilityCommandState {
        QList<RootPropertyState> rootStates;
    };

    auto state = std::make_shared<VisibilityCommandState>();

    return Command(
        [paths, recursive, state](Session* session) {
            command::beginDeferred(session, "Show paths", 1);

            command::runWorker([session, paths, recursive, state]() {
                bool success = false;
                QString error;

                {
                    WRITE_LOCKER(locker, session->stageLock(), "stageLock");
                    const UsdStageRefPtr stage = session->stageUnsafe();

                    if (!stage) {
                        error = "stage missing";
                    }
                    else {
                        QString editError;
                        const SdfLayerHandle editLayer = editlayer::opened(stage, editError);
                        if (!editLayer) {
                            error = editError;
                        }
                        else {
                            state->rootStates.clear();
                            const QList<SdfPath> affected = visibilityAffectedPaths(stage, paths, recursive, true);
                            state->rootStates.reserve(affected.size());
                            for (const SdfPath& path : affected) {
                                state->rootStates.append(
                                    captureRootPropertyState(editLayer, path.AppendProperty(UsdGeomTokens->visibility)));
                            }

                            stage::setVisible(stage, paths, true, recursive);
                            success = true;
                        }
                    }
                }

                command::queueToSession(session, [session, paths, success, error]() {
                    using Status = Session::Notify::Status;
                    command::finishDeferred(session, success ? "Paths shown" : appendError("Show paths failed", error),
                                            paths, success ? Status::Success : Status::Error);
                });
            });
        },
        [state](Session* session) {
            command::beginDeferred(session, "Undo show paths", 1);

            command::runWorker([session, state]() {
                bool success = true;
                QList<SdfPath> restoredPaths;
                QString error;

                {
                    WRITE_LOCKER(locker, session->stageLock(), "stageLock");
                    const UsdStageRefPtr stage = session->stageUnsafe();
                    QString editError;
                    const SdfLayerHandle editLayer = editlayer::opened(stage, editError);

                    if (!stage || !editLayer) {
                        success = false;
                        error = !editError.isEmpty() ? editError : QStringLiteral("stage missing");
                    }
                    else {
                        for (const RootPropertyState& rootState : state->rootStates) {
                            if (!restoreRootPropertyState(editLayer, rootState)) {
                                success = false;
                                error = QString("failed to restore visibility override: %1")
                                            .arg(pathText(rootState.propertyPath.GetPrimPath()));
                                break;
                            }
                            path::appendUnique(restoredPaths, rootState.propertyPath.GetPrimPath());
                        }
                    }
                }

                command::queueToSession(session, [session, restoredPaths, success, error]() {
                    using Status = Session::Notify::Status;
                    command::finishDeferred(session,
                                            success ? "Show undone" : appendError("Undo show paths failed", error),
                                            restoredPaths, success ? Status::Success : Status::Error);
                });
            });
        });
}

Command
hidePaths(const QList<SdfPath>& paths, bool recursive)
{
    struct VisibilityCommandState {
        QList<RootPropertyState> rootStates;
    };

    auto state = std::make_shared<VisibilityCommandState>();

    return Command(
        [paths, recursive, state](Session* session) {
            command::beginDeferred(session, "Hide paths", 1);

            command::runWorker([session, paths, recursive, state]() {
                bool success = false;
                QString error;

                {
                    WRITE_LOCKER(locker, session->stageLock(), "stageLock");
                    const UsdStageRefPtr stage = session->stageUnsafe();

                    if (!stage) {
                        error = "stage missing";
                    }
                    else {
                        QString editError;
                        const SdfLayerHandle editLayer = editlayer::opened(stage, editError);
                        if (!editLayer) {
                            error = editError;
                        }
                        else {
                            state->rootStates.clear();
                            const QList<SdfPath> affected = visibilityAffectedPaths(stage, paths, recursive, false);
                            state->rootStates.reserve(affected.size());
                            for (const SdfPath& path : affected) {
                                state->rootStates.append(
                                    captureRootPropertyState(editLayer, path.AppendProperty(UsdGeomTokens->visibility)));
                            }

                            stage::setVisible(stage, paths, false, recursive);
                            success = true;
                        }
                    }
                }

                command::queueToSession(session, [session, paths, success, error]() {
                    using Status = Session::Notify::Status;
                    command::finishDeferred(session, success ? "Paths hidden" : appendError("Hide paths failed", error),
                                            paths, success ? Status::Success : Status::Error);
                });
            });
        },
        [state](Session* session) {
            command::beginDeferred(session, "Undo hide paths", 1);

            command::runWorker([session, state]() {
                bool success = true;
                QList<SdfPath> restoredPaths;
                QString error;

                {
                    WRITE_LOCKER(locker, session->stageLock(), "stageLock");
                    const UsdStageRefPtr stage = session->stageUnsafe();
                    QString editError;
                    const SdfLayerHandle editLayer = editlayer::opened(stage, editError);

                    if (!stage || !editLayer) {
                        success = false;
                        error = !editError.isEmpty() ? editError : QStringLiteral("stage missing");
                    }
                    else {
                        for (const RootPropertyState& rootState : state->rootStates) {
                            if (!restoreRootPropertyState(editLayer, rootState)) {
                                success = false;
                                error = QString("failed to restore visibility override: %1")
                                            .arg(pathText(rootState.propertyPath.GetPrimPath()));
                                break;
                            }
                            path::appendUnique(restoredPaths, rootState.propertyPath.GetPrimPath());
                        }
                    }
                }

                command::queueToSession(session, [session, restoredPaths, success, error]() {
                    using Status = Session::Notify::Status;
                    command::finishDeferred(session,
                                            success ? "Hide undone" : appendError("Undo hide paths failed", error),
                                            restoredPaths, success ? Status::Success : Status::Error);
                });
            });
        });
}

Command
stageUp(Session::StageUp stageUp)
{
    auto state = std::make_shared<Session::StageUp>(Session::StageUp::Y);

    return Command(
        [stageUp, state](Session* session) {
            session->beginProgressBlock("Set stage up", 1);

            command::runWorker([session, stageUp, state]() {
                *state = session->stageUp();
                session->setStageUp(stageUp);

                command::queueToSession(session, [session, stageUp]() {
                    using Status = Session::Notify::Status;
                    const QString axis = (stageUp == Session::StageUp::Z) ? "Z" : "Y";
                    session->updateProgressNotify(Session::Notify(QString("Stage up set to %1").arg(axis), {},
                                                                  Status::Success),
                                                  1);
                    session->endProgressBlock();
                });
            });
        },
        [state](Session* session) {
            session->beginProgressBlock("Undo set stage up", 1);

            command::runWorker([session, state]() {
                session->setStageUp(*state);

                command::queueToSession(session, [session, state]() {
                    using Status = Session::Notify::Status;
                    const QString axis = (*state == Session::StageUp::Z) ? "Z" : "Y";
                    session->updateProgressNotify(Session::Notify(QString("Set stage up undone to %1").arg(axis), {},
                                                                  Status::Success),
                                                  1);
                    session->endProgressBlock();
                });
            });
        });
}

namespace snapshot {
    struct DeleteState {
        QVector<PrimState> prims;
        QHash<SdfPath, TfTokenVector> parentOrders;
        SdfPath previousDefaultPrimPath;
        QList<SdfPath> previousSelection;
        QList<SdfPath> previousMask;
    };
}  // namespace snapshot

Command
defaultPrimPath(const SdfPath& path)
{
    struct DefaultPrimState {
        SdfPath previousDefaultPrimPath;
        SdfPath newDefaultPrimPath;
    };

    auto state = std::make_shared<DefaultPrimState>();

    return Command(
        [path, state](Session* session) {
            if (!session || path.IsEmpty() || path == SdfPath::AbsoluteRootPath())
                return;

            session->beginProgressBlock("Set default prim", 1);

            command::runWorker([=]() {
                bool hadStage = true;
                bool success = false;
                QString error;

                {
                    WRITE_LOCKER(locker, session->stageLock(), "stageLock");
                    const UsdStageRefPtr stage = session->stageUnsafe();

                    if (!stage) {
                        hadStage = false;
                    }
                    else {
                        if (UsdPrim previousDefaultPrim = stage->GetDefaultPrim())
                            state->previousDefaultPrimPath = previousDefaultPrim.GetPath();
                        else
                            state->previousDefaultPrimPath = SdfPath();

                        QString editError;
                        const SdfLayerHandle editLayer = editlayer::opened(stage, editError);
                        const UsdPrim prim = stage->GetPrimAtPath(path);
                        if (!editLayer) {
                            error = editError;
                        }
                        else if (!prim || !prim.IsValid()) {
                            error = "invalid prim";
                        }
                        else if (path.GetParentPath() != SdfPath::AbsoluteRootPath()) {
                            error = "default prim must be a root prim";
                        }
                        else if (!editlayer::validatePrim(stage, path, error)) {}
                        else {
                            UsdEditContext context(stage, UsdEditTarget(editLayer));
                            stage->SetDefaultPrim(prim);
                            state->newDefaultPrimPath = path;
                            success = true;
                        }
                    }
                }

                command::queueToSession(session, [=]() {
                    using Status = Session::Notify::Status;

                    if (!hadStage) {
                        session->updateProgressNotify(Session::Notify("Set default prim failed", { path },
                                                                      Status::Error),
                                                      1);
                        session->endProgressBlock();
                        return;
                    }

                    if (!success) {
                        session->updateProgressNotify(
                            Session::Notify(error.isEmpty() ? "Set default prim failed"
                                                            : QString("Set default prim failed: %1").arg(error),
                                            { path }, Status::Error),
                            1);
                        session->endProgressBlock();
                        return;
                    }

                    session->updateProgressNotify(Session::Notify("Default prim set", { path }, Status::Success), 1);
                    session->endProgressBlock();
                });
            });
        },
        [state](Session* session) {
            if (!session)
                return;

            session->beginProgressBlock("Undo set default prim", 1);

            command::runWorker([=]() {
                bool hadStage = true;
                bool success = false;
                QString error;

                {
                    WRITE_LOCKER(locker, session->stageLock(), "stageLock");
                    const UsdStageRefPtr stage = session->stageUnsafe();

                    if (!stage) {
                        hadStage = false;
                    }
                    else {
                        QString editError;
                        const SdfLayerHandle editLayer = editlayer::opened(stage, editError);
                        if (!editLayer) {
                            error = editError;
                        }
                        else if (state->previousDefaultPrimPath.IsEmpty()) {
                            UsdEditContext context(stage, UsdEditTarget(editLayer));
                            stage->ClearDefaultPrim();
                            success = true;
                        }
                        else {
                            const UsdPrim prim = stage->GetPrimAtPath(state->previousDefaultPrimPath);
                            if (!prim || !prim.IsValid()) {
                                error = "previous default prim missing";
                            }
                            else {
                                UsdEditContext context(stage, UsdEditTarget(editLayer));
                                stage->SetDefaultPrim(prim);
                                success = true;
                            }
                        }
                    }
                }

                command::queueToSession(session, [=]() {
                    using Status = Session::Notify::Status;

                    if (!hadStage) {
                        session->updateProgressNotify(Session::Notify("Undo set default prim failed", {}, Status::Error),
                                                      1);
                        session->endProgressBlock();
                        return;
                    }

                    if (!success) {
                        session->updateProgressNotify(
                            Session::Notify(error.isEmpty() ? "Undo set default prim failed"
                                                            : QString("Undo set default prim failed: %1").arg(error),
                                            {}, Status::Error),
                            1);
                        session->endProgressBlock();
                        return;
                    }

                    session->updateProgressNotify(Session::Notify("Default prim undone",
                                                                  { state->previousDefaultPrimPath }, Status::Success),
                                                  1);
                    session->endProgressBlock();
                });
            });
        });
}

Command
clearDefaultPrim()
{
    auto previousDefaultPrimPath = std::make_shared<SdfPath>();

    return Command(
        [previousDefaultPrimPath](Session* session) {
            if (!session)
                return;

            session->beginProgressBlock("Clear default prim", 1);

            command::runWorker([session, previousDefaultPrimPath]() {
                bool hadStage = true;
                bool success = false;
                QString error;

                {
                    WRITE_LOCKER(locker, session->stageLock(), "stageLock");
                    const UsdStageRefPtr stage = session->stageUnsafe();

                    if (!stage) {
                        hadStage = false;
                    }
                    else {
                        const UsdPrim previousDefaultPrim = stage->GetDefaultPrim();
                        if (!previousDefaultPrim) {
                            error = "stage has no default prim";
                        }
                        else {
                            QString editError;
                            const SdfLayerHandle editLayer = editlayer::opened(stage, editError);
                            if (!editLayer) {
                                error = editError;
                            }
                            else {
                                *previousDefaultPrimPath = previousDefaultPrim.GetPath();
                                UsdEditContext context(stage, UsdEditTarget(editLayer));
                                stage->ClearDefaultPrim();
                                success = true;
                            }
                        }
                    }
                }

                command::queueToSession(session, [session, previousDefaultPrimPath, hadStage, success, error]() {
                    using Status = Session::Notify::Status;
                    if (!hadStage || !success) {
                        const QString message = error.isEmpty() ? "Clear default prim failed"
                                                                : QString("Clear default prim failed: %1").arg(error);
                        session->updateProgressNotify(
                            Session::Notify(message, { *previousDefaultPrimPath }, Status::Error), 1);
                    }
                    else {
                        session->updateProgressNotify(
                            Session::Notify("Default prim cleared", { *previousDefaultPrimPath }, Status::Success), 1);
                    }
                    session->endProgressBlock();
                });
            });
        },
        [previousDefaultPrimPath](Session* session) {
            if (!session || previousDefaultPrimPath->IsEmpty())
                return;

            session->beginProgressBlock("Undo clear default prim", 1);

            command::runWorker([session, previousDefaultPrimPath]() {
                bool success = false;
                QString error;

                {
                    WRITE_LOCKER(locker, session->stageLock(), "stageLock");
                    const UsdStageRefPtr stage = session->stageUnsafe();
                    if (!stage) {
                        error = "stage unavailable";
                    }
                    else {
                        QString editError;
                        const SdfLayerHandle editLayer = editlayer::opened(stage, editError);
                        const UsdPrim prim = stage->GetPrimAtPath(*previousDefaultPrimPath);
                        if (!editLayer) {
                            error = editError;
                        }
                        else if (!prim || !prim.IsValid()) {
                            error = "previous default prim missing";
                        }
                        else {
                            UsdEditContext context(stage, UsdEditTarget(editLayer));
                            stage->SetDefaultPrim(prim);
                            success = true;
                        }
                    }
                }

                command::queueToSession(session, [session, previousDefaultPrimPath, success, error]() {
                    using Status = Session::Notify::Status;
                    if (!success) {
                        const QString message = error.isEmpty() ? "Undo clear default prim failed"
                                                                : QString("Undo clear default prim failed: %1").arg(error);
                        session->updateProgressNotify(
                            Session::Notify(message, { *previousDefaultPrimPath }, Status::Error), 1);
                    }
                    else {
                        session->updateProgressNotify(
                            Session::Notify("Clear default prim undone", { *previousDefaultPrimPath }, Status::Success),
                            1);
                    }
                    session->endProgressBlock();
                });
            });
        });
}

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
                        const SdfLayerHandle editLayer = editlayer::opened(stage, editError);
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
                                if (editlayer::validatePrim(stage, primPath, pathError)) {
                                    editable.append(primPath);
                                }
                                else {
                                    rejected.append(pathError);
                                }
                            }

                            const QList<SdfPath> paths = path::minimalRootPaths(editable);

                            state->prims.clear();
                            state->parentOrders.clear();

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

                            bool removedAny = false;
                            if (deletesDefaultPrim)
                                stage->ClearDefaultPrim();

                            UsdEditContext context(stage, UsdEditTarget(editLayer));
                            for (const SdfPath& path : paths) {
                                snapshot::PrimState primState;
                                if (!snapshot::capturePrimToLayer(stage, path, primState)) {
                                    rejected.append(QString("failed to snapshot prim: %1").arg(pathText(path)));
                                    continue;
                                }

                                if (!stage::removePrimSpec(editLayer, primState.specPath)) {
                                    rejected.append(QString("failed to remove edit-layer spec: %1")
                                                        .arg(pathText(primState.specPath)));
                                    continue;
                                }

                                state->prims.append(primState);
                                removedPaths.append(path);
                                removedAny = true;
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

                    command::finishDeferred(session,
                                            success ? (error.isEmpty() ? "Paths deleted"
                                                                       : QString("Paths deleted (%1)").arg(error))
                                                    : appendError("Delete paths failed", error),
                                            success ? changed : removedPaths,
                                            success ? Status::Success : Status::Error);

                    session->selectionList()->updatePaths(
                        path::removeAffectedPaths(state->previousSelection, removedPaths));
                    session->setMask(path::removeAffectedPaths(state->previousMask, removedPaths));
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
                        const SdfLayerHandle editLayer = editlayer::opened(stage, editError);
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
                            success = true;
                        }
                    }
                    else {
                        error = "stage missing";
                    }
                }

                command::queueToSession(session, [session, state, changed, success, error]() {
                    using Status = Session::Notify::Status;

                    command::finishDeferred(session,
                                            success ? "Delete undone" : appendError("Undo delete paths failed", error),
                                            changed, success ? Status::Success : Status::Error);

                    session->selectionList()->updatePaths(state->previousSelection);
                    session->setMask(state->previousMask);
                });
            });
        });
}

Command
duplicatePaths(const QList<SdfPath>& inPaths)
{
    struct DuplicateState {
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

    auto state = std::make_shared<DuplicateState>();

    return Command(
        [inPaths, state](Session* session) {
            if (!session || inPaths.isEmpty())
                return;

            state->previousSelection = session->selectionList()->paths();
            state->previousMask = session->mask();

            command::beginDeferred(session, "Duplicate paths", 1);

            command::runWorker([session, inPaths, state]() {
                bool success = false;
                QList<SdfPath> duplicatedPaths;
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
                        const SdfLayerHandle editLayer = editlayer::opened(stage, editError);

                        if (!editLayer) {
                            errors.append(editError);
                        }
                        else {
                            state->items.clear();
                            state->oldParentOrders.clear();
                            state->createdParentSpecs.clear();

                            const QList<SdfPath> paths = path::minimalRootPaths(path::uniquePaths(inPaths));

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
                                const SdfLayerHandle sourceLayer = sourceSpec->GetLayer();
                                const SdfPath sourceSpecPath = sourceSpec->GetPath();

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
                                    if (!editlayer::validateParent(stage, parentPath, parentError)) {
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
                                            ? QString("failed to build duplicate path: %1").arg(pathText(sourcePath))
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

                                DuplicateState::Item item;
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

                                path::appendUnique(duplicatedPaths, destinationPath);
                                path::appendUnique(changed, sourcePath);
                                path::appendUnique(changed, destinationPath);
                                path::appendUnique(changed, parentPath);
                            }

                            success = !state->items.isEmpty();
                        }
                    }
                }

                const QString errorText = summarizeErrors(errors);
                command::queueToSession(session, [session, duplicatedPaths, changed, success, errorText]() {
                    using Status = Session::Notify::Status;

                    command::finishDeferred(session,
                                            success ? (errorText.isEmpty()
                                                           ? QStringLiteral("Paths duplicated")
                                                           : appendError("Paths duplicated with errors", errorText))
                                                    : appendError("Duplicate paths failed", errorText),
                                            changed, success ? Status::Success : Status::Error);

                    if (success)
                        session->selectionList()->updatePaths(duplicatedPaths);
                });
            });
        },
        [state](Session* session) {
            if (!session || state->items.isEmpty())
                return;

            command::beginDeferred(session, "Undo duplicate paths", 1);

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
                        const SdfLayerHandle editLayer = editlayer::opened(stage, editError);

                        if (!editLayer) {
                            success = false;
                            errors.append(editError);
                        }
                        else {
                            for (auto it = state->items.crbegin(); it != state->items.crend(); ++it) {
                                if (!stage::removePrimSpec(editLayer, it->destinationPath)) {
                                    success = false;
                                    errors.append(
                                        QString("failed to remove duplicate: %1").arg(pathText(it->destinationPath)));
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
                                            success ? "Duplicate undone"
                                                    : appendError("Undo duplicate paths failed", errorText),
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
newPrimPath(const SdfPath& parentPath, const QString& nameInput, const TfToken& typeName)
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
        [parentPath, nameInput, typeName, state](Session* session) {
            if (!session || parentPath.IsEmpty())
                return;

            state->previousSelection = session->selectionList()->paths();
            state->previousMask = session->mask();

            command::beginDeferred(session, "New prim", 1);

            command::runWorker([session, parentPath, nameInput, typeName, state]() {
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
                        const SdfLayerHandle editLayer = editlayer::opened(stage, editError);

                        if (!editLayer) {
                            error = editError;
                        }
                        else if (!editlayer::validateParent(stage, parentPath, error)) {}
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
                                else {
                                    TfTokenVector order = state->oldParentOrder;
                                    order.push_back(newPath.GetNameToken());
                                    stage::restoreChildOrder(stage, parentPath, order);

                                    path::appendUnique(changed, parentPath);
                                    path::appendUnique(changed, newPath);
                                    success = true;
                                }
                            }
                        }
                    }
                }

                command::queueToSession(session, [session, newPath, changed, success, error]() {
                    using Status = Session::Notify::Status;

                    command::finishDeferred(session, success ? "Prim created" : appendError("New prim failed", error),
                                            changed, success ? Status::Success : Status::Error);

                    if (success)
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
                        const SdfLayerHandle editLayer = editlayer::opened(stage, editError);

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
newScopePath(const SdfPath& parentPath, const QString& nameInput)
{
    return newPrimPath(parentPath, nameInput, TfToken("Scope"));
}

Command
newMaterialPath(const SdfPath& parentPath, const QString& nameInput)
{
    struct NewMaterialState {
        SdfPath parentPath;
        SdfPath createdPath;
        TfTokenVector oldParentOrder;
        QList<SdfPath> createdAncestorSpecs;
        QList<SdfPath> previousSelection;
        QList<SdfPath> previousMask;
    };

    auto state = std::make_shared<NewMaterialState>();

    return Command(
        [parentPath, nameInput, state](Session* session) {
            if (!session || parentPath.IsEmpty())
                return;

            state->previousSelection = session->selectionList()->paths();
            state->previousMask = session->mask();

            command::beginDeferred(session, "New material", 1);

            command::runWorker([session, parentPath, nameInput, state]() {
                bool success = false;
                QString error;
                SdfPath materialPath;
                QList<SdfPath> changed;

                {
                    WRITE_LOCKER(locker, session->stageLock(), "stageLock");
                    const UsdStageRefPtr stage = session->stageUnsafe();

                    if (!stage) {
                        error = "stage missing";
                    }
                    else {
                        QString editError;
                        const SdfLayerHandle editLayer = editlayer::opened(stage, editError);

                        if (!editLayer) {
                            error = editError;
                        }
                        else if (!editlayer::validateParent(stage, parentPath, error)) {}
                        else {
                            materialPath = stage::buildChildPath(stage, parentPath, nameInput, error);

                            if (!materialPath.IsEmpty()) {
                                state->parentPath = parentPath;
                                state->createdPath = materialPath;
                                state->oldParentOrder.clear();
                                state->createdAncestorSpecs.clear();

                                stage::captureChildOrder(stage, parentPath, state->oldParentOrder);

                                for (SdfPath path = parentPath; !path.IsEmpty() && path != SdfPath::AbsoluteRootPath();
                                     path = path.GetParentPath()) {
                                    if (!editLayer->GetPrimAtPath(path))
                                        state->createdAncestorSpecs.prepend(path);
                                }

                                UsdEditContext context(stage, UsdEditTarget(editLayer));

                                const UsdShadeMaterial material = UsdShadeMaterial::Define(stage, materialPath);
                                const SdfPath shaderPath = materialPath.AppendChild(TfToken("PreviewSurface"));
                                const UsdShadeShader shader = UsdShadeShader::Define(stage, shaderPath);

                                if (!material || !material.GetPrim() || !shader || !shader.GetPrim()) {
                                    error = "failed to define material";
                                }
                                else if (!shader.CreateIdAttr(VtValue(TfToken("UsdPreviewSurface")))) {
                                    error = "failed to create preview surface shader";
                                }
                                else {
                                    UsdShadeMaterial material = UsdShadeMaterial::Define(stage, materialPath);

                                    const SdfPath shaderPath = materialPath.AppendChild(TfToken("PreviewSurface"));
                                    UsdShadeShader shader = UsdShadeShader::Define(stage, shaderPath);

                                    if (!material || !material.GetPrim() || !shader || !shader.GetPrim()) {
                                        error = "failed to define material";
                                    }
                                    else if (!shader.CreateIdAttr(VtValue(TfToken("UsdPreviewSurface")))) {
                                        error = "failed to create preview surface shader";
                                    }
                                    else {
                                        const UsdShadeOutput shaderOutput
                                            = shader.CreateOutput(TfToken("surface"), SdfValueTypeNames->Token);

                                        const UsdShadeOutput materialOutput = material.CreateSurfaceOutput();

                                        if (!shaderOutput || !materialOutput
                                            || !materialOutput.ConnectToSource(shaderOutput)) {
                                            error = "failed to connect material surface output";
                                        }
                                        else {
                                            TfTokenVector order = state->oldParentOrder;
                                            order.push_back(materialPath.GetNameToken());
                                            stage::restoreChildOrder(stage, parentPath, order);

                                            path::appendUnique(changed, parentPath);
                                            path::appendUnique(changed, materialPath);

                                            success = true;
                                        }
                                    }
                                }

                                if (!success)
                                    stage::removePrimSpec(editLayer, materialPath);
                            }
                        }
                    }
                }

                command::queueToSession(session, [session, materialPath, changed, success, error]() {
                    using Status = Session::Notify::Status;

                    command::finishDeferred(session,
                                            success ? "Material created" : appendError("New material failed", error),
                                            changed, success ? Status::Success : Status::Error);

                    if (success)
                        session->selectionList()->updatePaths({ materialPath });
                });
            });
        },
        [state](Session* session) {
            if (!session || state->createdPath.IsEmpty())
                return;

            command::beginDeferred(session, "Undo new material", 1);

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
                        const SdfLayerHandle editLayer = editlayer::opened(stage, editError);

                        if (!editLayer) {
                            error = editError;
                        }
                        else if (!stage::removePrimSpec(editLayer, state->createdPath)) {
                            error = QString("failed to remove material: %1").arg(pathText(state->createdPath));
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
                                            success ? "New material undone"
                                                    : appendError("Undo new material failed", error),
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
                            const SdfLayerHandle editLayer = editlayer::opened(stage, editError);

                            if (!editLayer) {
                                error = editError;
                            }
                            else if (!editlayer::validateParent(stage, parentPath, error)) {}
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
                            const SdfLayerHandle editLayer = editlayer::opened(stage, editError);

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
                        const SdfLayerHandle editLayer = editlayer::opened(stage, editError);
                        if (!editLayer) {
                            error = editError;
                        }
                        else if (!editlayer::validatePrim(stage, path, error)) {}
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
                                edit::NamespaceEditor namespaceEditor(stage);
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
                        const SdfLayerHandle editLayer = editlayer::opened(stage, editError);
                        if (!editLayer) {
                            error = editError;
                        }
                        else {
                            UsdEditContext context(stage, UsdEditTarget(editLayer));
                            edit::NamespaceEditor namespaceEditor(stage);
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
newXformPath(const SdfPath& parentPath, const QString& nameInput)
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
        [parentPath, nameInput, state](Session* session) {
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

                            QString editError;
                            const SdfLayerHandle editLayer = editlayer::opened(stage, editError);
                            if (!editLayer) {
                                error = editError;
                            }
                            else if (!editlayer::validateParent(stage, parentPath, error)) {}
                            else {
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
                                        if (!editlayer::validatePrim(stage, movePath, authoredError)) {
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
                                        edit::NamespaceEditor namespaceEditor(stage);
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

                    session->selectionList()->updatePaths({ newPath });
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
                        const SdfLayerHandle editLayer = editlayer::opened(stage, editError);
                        if (!editLayer) {
                            error = editError;
                        }
                        else {
                            UsdEditContext context(stage, UsdEditTarget(editLayer));
                            QList<QPair<SdfPath, SdfPath>> reverseMoves;
                            reverseMoves.reserve(state->movedItems.size());
                            for (auto it = state->movedItems.crbegin(); it != state->movedItems.crend(); ++it)
                                reverseMoves.append(qMakePair(it->newPath, it->oldPath));

                            edit::NamespaceEditor namespaceEditor(stage);
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
                            const SdfLayerHandle editLayer = editlayer::opened(stage, editError);
                            if (!editLayer) {
                                error = editError;
                            }
                            else if (!editlayer::validateParent(stage, newParentPath, error)) {}
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
                                    if (!editlayer::validatePrim(stage, path, authoredError)) {
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

                                    QString moveError;
                                    edit::NamespaceEditor namespaceEditor(stage);
                                    if (!namespaceEditor.reparentPrims(moves, moveError)) {
                                        error = moveError.isEmpty() ? "failed to move paths" : moveError;
                                        stage::restoreChildOrders(stage, state->oldParentOrders);
                                        moved = false;
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

                    for (const MoveItem& item : state->items) {
                        selection = path::remapAffectedPaths(selection, item.oldPath, item.newPath);
                        mask = path::remapAffectedPaths(mask, item.oldPath, item.newPath);
                    }

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
                        const SdfLayerHandle editLayer = editlayer::opened(stage, editError);
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

                            edit::NamespaceEditor namespaceEditor(stage);
                            restored = namespaceEditor.reparentPrims(reverseMoves, error);

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
                QStringList errors;

                {
                    WRITE_LOCKER(locker, session->stageLock(), "stageLock");
                    const UsdStageRefPtr stage = session->stageUnsafe();

                    if (!stage) {
                        errors.append("stage missing");
                    }
                    else {
                        QString editError;
                        const SdfLayerHandle editLayer = editlayer::opened(stage, editError);

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

                                    if (isPivot) {
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

                                    if (!authorCanonicalTransformWithPivot(prim, item.previousLocalTransform,
                                                                           value.UncheckedGet<GfVec3d>(), error)) {
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

                                    path::appendUnique(affected, item.attributePath.GetPrimPath());
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

                                path::appendUnique(affected, item.attributePath.GetPrimPath());
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
                QStringList errors;

                {
                    WRITE_LOCKER(locker, session->stageLock(), "stageLock");
                    const UsdStageRefPtr stage = session->stageUnsafe();

                    if (!stage) {
                        errors.append("stage missing");
                    }
                    else {
                        QString editError;
                        const SdfLayerHandle editLayer = editlayer::opened(stage, editError);

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

                                    path::appendUnique(affected, item.attributePath.GetPrimPath());
                                    continue;
                                }

                                if (item.hadRootDefault)
                                    editLayer->SetField(item.attributePath, SdfFieldKeys->Default,
                                                        item.previousRootDefault);
                                else
                                    editLayer->EraseField(item.attributePath, SdfFieldKeys->Default);

                                path::appendUnique(affected, item.attributePath.GetPrimPath());
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
                QStringList errors;

                {
                    WRITE_LOCKER(locker, session->stageLock(), "stageLock");
                    const UsdStageRefPtr stage = session->stageUnsafe();

                    if (!stage) {
                        errors.append("stage missing");
                    }
                    else {
                        QString editError;
                        const SdfLayerHandle editLayer = editlayer::opened(stage, editError);

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
                                path::appendUnique(affected, attributePath.GetPrimPath());
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
                QStringList errors;

                {
                    WRITE_LOCKER(locker, session->stageLock(), "stageLock");
                    const UsdStageRefPtr stage = session->stageUnsafe();

                    if (!stage) {
                        errors.append("stage missing");
                    }
                    else {
                        QString editError;
                        const SdfLayerHandle editLayer = editlayer::opened(stage, editError);

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

                                path::appendUnique(affected, rootState.propertyPath.GetPrimPath());
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
                QStringList errors;

                {
                    WRITE_LOCKER(locker, session->stageLock(), "stageLock");
                    const UsdStageRefPtr stage = session->stageUnsafe();

                    if (!stage) {
                        errors.append("stage missing");
                    }
                    else {
                        QString editError;
                        const SdfLayerHandle editLayer = editlayer::opened(stage, editError);

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
                                path::appendUnique(affected, primPath);
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
                QStringList errors;

                {
                    WRITE_LOCKER(locker, session->stageLock(), "stageLock");
                    const UsdStageRefPtr stage = session->stageUnsafe();

                    if (!stage) {
                        errors.append("stage missing");
                    }
                    else {
                        QString editError;
                        const SdfLayerHandle editLayer = editlayer::opened(stage, editError);

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

                                path::appendUnique(affected, item.primPath);
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
resetTransforms(const QList<SdfPath>& paths)
{
    struct ResetTransformState {
        QList<TransformPropertyState> items;
    };

    auto state = std::make_shared<ResetTransformState>();

    return Command(
        [paths, state](Session* session) {
            if (!session || paths.isEmpty())
                return;

            const QList<SdfPath> unique = path::uniquePaths(paths);
            command::beginDeferred(session, "Reset xform", static_cast<int>(unique.size()));

            command::runWorker([session, unique, state]() {
                QList<SdfPath> affected;
                QStringList errors;

                {
                    WRITE_LOCKER(locker, session->stageLock(), "stageLock");
                    const UsdStageRefPtr stage = session->stageUnsafe();

                    if (!stage) {
                        errors.append("stage missing");
                    }
                    else {
                        QString editError;
                        const SdfLayerHandle editLayer = editlayer::opened(stage, editError);

                        if (!editLayer) {
                            errors.append(editError);
                        }
                        else {
                            state->items.clear();

                            for (const SdfPath& inputPath : unique) {
                                const SdfPath primPath = inputPath.IsPropertyPath() ? inputPath.GetPrimPath()
                                                                                    : inputPath;
                                const UsdPrim prim = stage->GetPrimAtPath(primPath);
                                const UsdGeomXformable xformable(prim);

                                if (!prim || !prim.IsValid() || prim.IsInstanceProxy() || !xformable) {
                                    errors.append(QString("prim is not transformable: %1").arg(pathText(primPath)));
                                    continue;
                                }

                                TransformPropertyState item;
                                QString error;
                                if (!captureTransformPropertyState(editLayer, primPath, item, error)) {
                                    errors.append(error);
                                    continue;
                                }

                                const bool hasUnderlying = hasUnderlyingTransformOpinion(stage, prim, editLayer);

                                if (!clearEditLayerTransformProperties(editLayer, primPath, error)) {
                                    errors.append(error);
                                    continue;
                                }

                                bool success = false;

                                if (hasUnderlying) {
                                    success = true;
                                }
                                else {
                                    UsdEditContext context(stage, UsdEditTarget(editLayer));
                                    const UsdGeomXformOp matrixOp = xformable.AddTransformOp(
                                        UsdGeomXformOp::PrecisionDouble);

                                    success = matrixOp && matrixOp.Set(GfMatrix4d(1.0), UsdTimeCode::Default());

                                    if (!success)
                                        error
                                            = QString("failed to author identity transform: %1").arg(pathText(primPath));
                                }

                                if (!success) {
                                    QString restoreError;
                                    restoreTransformPropertyState(editLayer, item, restoreError);
                                    errors.append(error.isEmpty()
                                                      ? QString("failed to reset xform: %1").arg(pathText(primPath))
                                                      : error);
                                    continue;
                                }

                                state->items.append(item);
                                path::appendUnique(affected, primPath);
                            }
                        }
                    }
                }

                const bool success = errors.isEmpty() && !affected.isEmpty();
                command::queueToSession(session, [session, affected, errors, success]() {
                    using Status = Session::Notify::Status;
                    const QString message = success
                                                ? QStringLiteral("Xform reset")
                                                : (errors.isEmpty() ? QStringLiteral("No transforms to reset")
                                                                    : appendError("Reset xform finished with errors",
                                                                                  summarizeErrors(errors)));
                    command::finishDeferred(session, message, affected, success ? Status::Success : Status::Error);
                });
            });
        },
        [state](Session* session) {
            if (!session || state->items.isEmpty())
                return;

            command::beginDeferred(session, "Undo reset xform", static_cast<int>(state->items.size()));

            command::runWorker([session, state]() {
                QList<SdfPath> restored;
                QStringList errors;

                {
                    WRITE_LOCKER(locker, session->stageLock(), "stageLock");
                    const UsdStageRefPtr stage = session->stageUnsafe();

                    if (!stage) {
                        errors.append("stage missing");
                    }
                    else {
                        QString editError;
                        const SdfLayerHandle editLayer = editlayer::opened(stage, editError);

                        if (!editLayer) {
                            errors.append(editError);
                        }
                        else {
                            for (const TransformPropertyState& item : state->items) {
                                QString error;
                                if (restoreTransformPropertyState(editLayer, item, error))
                                    path::appendUnique(restored, item.primPath);
                                else
                                    errors.append(error);
                            }
                        }
                    }
                }

                const bool success = errors.isEmpty();
                command::queueToSession(session, [session, restored, errors, success]() {
                    using Status = Session::Notify::Status;
                    command::finishDeferred(session,
                                            success ? "Reset xform undone"
                                                    : appendError("Undo reset xform finished with errors",
                                                                  summarizeErrors(errors)),
                                            restored, success ? Status::Success : Status::Error);
                });
            });
        });
}


Command
centerPivots(const QList<SdfPath>& paths)
{
    struct PivotState {
        QList<TransformPropertyState> items;
    };

    auto state = std::make_shared<PivotState>();

    return Command(
        [paths, state](Session* session) {
            if (!session || paths.isEmpty())
                return;

            const QList<SdfPath> unique = path::uniquePaths(paths);
            command::beginDeferred(session, "Center pivot", static_cast<int>(unique.size()));

            command::runWorker([session, unique, state]() {
                QList<SdfPath> affected;
                QStringList errors;

                {
                    WRITE_LOCKER(locker, session->stageLock(), "stageLock");
                    const UsdStageRefPtr stage = session->stageUnsafe();

                    if (!stage) {
                        errors.append("stage missing");
                    }
                    else {
                        QString editError;
                        const SdfLayerHandle editLayer = editlayer::opened(stage, editError);

                        if (!editLayer) {
                            errors.append(editError);
                        }
                        else {
                            state->items.clear();
                            UsdGeomBBoxCache bboxCache(UsdTimeCode::Default(),
                                                       UsdGeomImageable::GetOrderedPurposeTokens(), true);

                            for (const SdfPath& inputPath : unique) {
                                const SdfPath primPath = inputPath.IsPropertyPath() ? inputPath.GetPrimPath()
                                                                                    : inputPath;
                                const UsdPrim prim = stage->GetPrimAtPath(primPath);
                                const UsdGeomXformable xformable(prim);

                                if (!prim || !prim.IsValid() || prim.IsInstanceProxy() || !xformable) {
                                    errors.append(QString("prim is not transformable: %1").arg(pathText(primPath)));
                                    continue;
                                }

                                const GfBBox3d worldBound = bboxCache.ComputeWorldBound(prim);
                                const GfRange3d worldRange = worldBound.ComputeAlignedRange();
                                if (worldRange.IsEmpty()) {
                                    errors.append(QString("prim has no bound: %1").arg(pathText(primPath)));
                                    continue;
                                }

                                GfMatrix4d localMatrix(1.0);
                                bool resetsXformStack = false;
                                if (!xformable.GetLocalTransformation(&localMatrix, &resetsXformStack,
                                                                      UsdTimeCode::Default())) {
                                    errors.append(
                                        QString("failed to evaluate local transform: %1").arg(pathText(primPath)));
                                    continue;
                                }

                                UsdGeomXformCache xformCache(UsdTimeCode::Default());
                                const GfMatrix4d worldMatrix = xformCache.GetLocalToWorldTransform(prim);
                                const GfVec3d worldCenter = worldRange.GetMidpoint();
                                const GfVec3d localPivot = worldMatrix.GetInverse().Transform(worldCenter);

                                TransformPropertyState item;
                                QString error;
                                if (!captureTransformPropertyState(editLayer, primPath, item, error)) {
                                    errors.append(error);
                                    continue;
                                }

                                UsdEditContext context(stage, UsdEditTarget(editLayer));
                                if (!authorCanonicalTransformWithPivot(prim, localMatrix, localPivot, error)) {
                                    errors.append(error.isEmpty()
                                                      ? QString("failed to center pivot: %1").arg(pathText(primPath))
                                                      : QString("%1: %2").arg(pathText(primPath), error));
                                    continue;
                                }

                                state->items.append(item);
                                path::appendUnique(affected, primPath);
                            }
                        }
                    }
                }

                const bool success = errors.isEmpty() && !affected.isEmpty();
                command::queueToSession(session, [session, affected, errors, success]() {
                    using Status = Session::Notify::Status;
                    command::finishDeferred(session,
                                            success ? "Pivot centered"
                                                    : appendError("Center pivot finished with errors",
                                                                  summarizeErrors(errors)),
                                            affected, success ? Status::Success : Status::Error);
                });
            });
        },
        [state](Session* session) {
            if (!session || state->items.isEmpty())
                return;

            command::beginDeferred(session, "Undo center pivot", static_cast<int>(state->items.size()));

            command::runWorker([session, state]() {
                QList<SdfPath> restored;
                QStringList errors;

                {
                    WRITE_LOCKER(locker, session->stageLock(), "stageLock");
                    const UsdStageRefPtr stage = session->stageUnsafe();

                    if (!stage) {
                        errors.append("stage missing");
                    }
                    else {
                        QString editError;
                        const SdfLayerHandle editLayer = editlayer::opened(stage, editError);

                        if (!editLayer) {
                            errors.append(editError);
                        }
                        else {
                            for (const TransformPropertyState& item : state->items) {
                                QString error;
                                if (restoreTransformPropertyState(editLayer, item, error))
                                    path::appendUnique(restored, item.primPath);
                                else
                                    errors.append(error);
                            }
                        }
                    }
                }

                const bool success = errors.isEmpty();
                command::queueToSession(session, [session, restored, errors, success]() {
                    using Status = Session::Notify::Status;
                    command::finishDeferred(session,
                                            success ? "Center pivot undone"
                                                    : appendError("Undo center pivot failed", summarizeErrors(errors)),
                                            restored, success ? Status::Success : Status::Error);
                });
            });
        });
}

Command
resetPivots(const QList<SdfPath>& paths)
{
    struct PivotState {
        QList<TransformPropertyState> items;
    };

    auto state = std::make_shared<PivotState>();

    return Command(
        [paths, state](Session* session) {
            if (!session || paths.isEmpty())
                return;

            const QList<SdfPath> unique = path::uniquePaths(paths);
            command::beginDeferred(session, "Reset pivot", static_cast<int>(unique.size()));

            command::runWorker([session, unique, state]() {
                QList<SdfPath> affected;
                QStringList errors;

                {
                    WRITE_LOCKER(locker, session->stageLock(), "stageLock");
                    const UsdStageRefPtr stage = session->stageUnsafe();

                    if (!stage) {
                        errors.append("stage missing");
                    }
                    else {
                        QString editError;
                        const SdfLayerHandle editLayer = editlayer::opened(stage, editError);

                        if (!editLayer) {
                            errors.append(editError);
                        }
                        else {
                            state->items.clear();

                            for (const SdfPath& inputPath : unique) {
                                const SdfPath primPath = inputPath.IsPropertyPath() ? inputPath.GetPrimPath()
                                                                                    : inputPath;
                                const UsdPrim prim = stage->GetPrimAtPath(primPath);
                                const UsdGeomXformable xformable(prim);

                                if (!prim || !prim.IsValid() || prim.IsInstanceProxy() || !xformable) {
                                    errors.append(QString("prim is not transformable: %1").arg(pathText(primPath)));
                                    continue;
                                }

                                GfMatrix4d localMatrix(1.0);
                                bool resetsXformStack = false;
                                if (!xformable.GetLocalTransformation(&localMatrix, &resetsXformStack,
                                                                      UsdTimeCode::Default())) {
                                    errors.append(
                                        QString("failed to evaluate local transform: %1").arg(pathText(primPath)));
                                    continue;
                                }

                                TransformPropertyState item;
                                QString error;
                                if (!captureTransformPropertyState(editLayer, primPath, item, error)) {
                                    errors.append(error);
                                    continue;
                                }

                                if (!clearEditLayerTransformProperties(editLayer, primPath, error)) {
                                    errors.append(error.isEmpty()
                                                      ? QString("failed to clear pivot transform properties: %1")
                                                            .arg(pathText(primPath))
                                                      : error);
                                    continue;
                                }

                                UsdEditContext context(stage, UsdEditTarget(editLayer));
                                if (!authorCanonicalMatrixTransform(prim, localMatrix, error)) {
                                    QString restoreError;
                                    restoreTransformPropertyState(editLayer, item, restoreError);

                                    errors.append(error.isEmpty()
                                                      ? QString("failed to reset pivot: %1").arg(pathText(primPath))
                                                      : QString("%1: %2").arg(pathText(primPath), error));
                                    continue;
                                }

                                state->items.append(item);
                                path::appendUnique(affected, primPath);
                            }
                        }
                    }
                }

                const bool success = errors.isEmpty() && !affected.isEmpty();
                command::queueToSession(session, [session, affected, errors, success]() {
                    using Status = Session::Notify::Status;
                    command::finishDeferred(session,
                                            success ? "Pivot reset"
                                                    : appendError("Reset pivot finished with errors",
                                                                  summarizeErrors(errors)),
                                            affected, success ? Status::Success : Status::Error);
                });
            });
        },
        [state](Session* session) {
            if (!session || state->items.isEmpty())
                return;

            command::beginDeferred(session, "Undo reset pivot", static_cast<int>(state->items.size()));

            command::runWorker([session, state]() {
                QList<SdfPath> restored;
                QStringList errors;

                {
                    WRITE_LOCKER(locker, session->stageLock(), "stageLock");
                    const UsdStageRefPtr stage = session->stageUnsafe();

                    if (!stage) {
                        errors.append("stage missing");
                    }
                    else {
                        QString editError;
                        const SdfLayerHandle editLayer = editlayer::opened(stage, editError);

                        if (!editLayer) {
                            errors.append(editError);
                        }
                        else {
                            for (const TransformPropertyState& item : state->items) {
                                QString error;
                                if (restoreTransformPropertyState(editLayer, item, error))
                                    path::appendUnique(restored, item.primPath);
                                else
                                    errors.append(error);
                            }
                        }
                    }
                }

                const bool success = errors.isEmpty();
                command::queueToSession(session, [session, restored, errors, success]() {
                    using Status = Session::Notify::Status;
                    command::finishDeferred(session,
                                            success ? "Reset pivot undone"
                                                    : appendError("Undo reset pivot failed", summarizeErrors(errors)),
                                            restored, success ? Status::Success : Status::Error);
                });
            });
        });
}

Command
identityTransforms(const QList<SdfPath>& paths)
{
    return resetTransforms(paths);
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
                QStringList errors;

                {
                    WRITE_LOCKER(locker, session->stageLock(), "stageLock");
                    const UsdStageRefPtr stage = session->stageUnsafe();

                    if (!stage) {
                        errors.append("stage missing");
                    }
                    else {
                        QString editError;
                        const SdfLayerHandle editLayer = editlayer::opened(stage, editError);

                        if (!editLayer) {
                            errors.append(editError);
                        }
                        else {
                            state->items.clear();

                            QList<SdfPath> primPaths;
                            for (const SdfPath& inputPath : path::minimalRootPaths(path::uniquePaths(paths))) {
                                const SdfPath primPath = inputPath.IsPropertyPath() ? inputPath.GetPrimPath()
                                                                                    : inputPath;
                                const UsdPrim prim = stage->GetPrimAtPath(primPath);
                                if (!prim || !prim.IsValid() || prim.IsInstanceProxy()) {
                                    errors.append(QString("invalid prim: %1").arg(pathText(primPath)));
                                    continue;
                                }

                                path::appendUnique(primPaths, primPath);
                                for (const UsdPrim& descendant : prim.GetAllDescendants()) {
                                    if (descendant && descendant.IsValid() && !descendant.IsInstanceProxy())
                                        path::appendUnique(primPaths, descendant.GetPath());
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
                                path::appendUnique(affected, primPath);
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
                QStringList errors;

                {
                    WRITE_LOCKER(locker, session->stageLock(), "stageLock");
                    const UsdStageRefPtr stage = session->stageUnsafe();

                    if (!stage) {
                        errors.append("stage missing");
                    }
                    else {
                        QString editError;
                        const SdfLayerHandle editLayer = editlayer::opened(stage, editError);

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
                                    path::appendUnique(restored, item.path);
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

Command
setTransforms(const QList<SdfPath>& paths, const QList<GfMatrix4d>& before, const QList<GfMatrix4d>& after,
              const QList<TransformRootState>& rootBefore)
{
    auto applyMatrices = [](Session* session, const QList<SdfPath>& paths, const QList<GfMatrix4d>& matrices,
                            const QString& title, const QString& successMessage, const QString& failureMessage) {
        if (!session || paths.isEmpty() || paths.size() != matrices.size())
            return;

        command::beginDeferred(session, title, static_cast<int>(paths.size()));

        command::runWorker([session, paths, matrices, successMessage, failureMessage]() {
            bool success = true;
            QStringList errors;
            QList<SdfPath> changed;
            {
                WRITE_LOCKER(locker, session->stageLock(), "stageLock");
                const UsdStageRefPtr stage = session->stageUnsafe();

                if (!stage) {
                    success = false;
                    errors.append("stage missing");
                }
                else {
                    for (qsizetype i = 0; i < paths.size(); ++i) {
                        const SdfPath& path = paths.at(i);
                        const GfMatrix4d& matrix = matrices.at(i);

                        QString error;
                        if (!stage::setWorldTransform(stage, path, matrix, error)) {
                            success = false;
                            errors.append(error.isEmpty() ? QString("failed: %1").arg(qt::SdfPathToQString(path))
                                                          : error);
                            continue;
                        }

                        path::appendUnique(changed, path);
                    }
                }
            }

            const QString errorText = summarizeErrors(errors);
            command::queueToSession(session, [session, changed, success, successMessage, failureMessage, errorText]() {
                using Status = Session::Notify::Status;
                command::finishDeferred(session, success ? successMessage : appendError(failureMessage, errorText),
                                        changed, success ? Status::Success : Status::Error);
            });
        });
    };

    return Command(
        [paths, after, applyMatrices](Session* session) {
            applyMatrices(session, paths, after, "Transform paths", "Paths transformed", "Transform paths failed");
        },
        [paths, before, rootBefore, applyMatrices](Session* session) {
            if (rootBefore.size() != paths.size()) {
                applyMatrices(session, paths, before, "Undo transform paths", "Transform undone",
                              "Undo transform failed");
                return;
            }

            command::beginDeferred(session, "Undo transform paths", static_cast<int>(paths.size()));
            command::runWorker([session, paths, rootBefore]() {
                bool success = true;
                QStringList errors;
                QList<SdfPath> changed;

                {
                    WRITE_LOCKER(locker, session->stageLock(), "stageLock");
                    const UsdStageRefPtr stage = session->stageUnsafe();
                    if (!stage) {
                        success = false;
                        errors.append("stage missing");
                    }
                    else {
                        for (qsizetype i = 0; i < paths.size(); ++i) {
                            QString error;
                            if (!restoreTransformRootState(stage, paths.at(i), rootBefore.at(i), error)) {
                                success = false;
                                errors.append(error.isEmpty() ? QString("failed: %1").arg(pathText(paths.at(i)))
                                                              : error);
                                continue;
                            }
                            path::appendUnique(changed, paths.at(i));
                        }
                    }
                }

                const QString errorText = summarizeErrors(errors);
                command::queueToSession(session, [session, changed, success, errorText]() {
                    using Status = Session::Notify::Status;
                    command::finishDeferred(session,
                                            success ? "Transform undone"
                                                    : appendError("Undo transform failed", errorText),
                                            changed, success ? Status::Success : Status::Error);
                });
            });
        });
}



}  // namespace stageviz
