// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#include "commandtransform.h"
#include "commandtransformprivate.h"
#include "commandutils.h"
#include "qtutils.h"
#include "tracelocks.h"
#include "usdutils.h"
#include <algorithm>
#include <cmath>
#include <memory>
#include <pxr/base/gf/vec3f.h>
#include <pxr/base/gf/vec3h.h>
#include <pxr/usd/sdf/changeBlock.h>
#include <pxr/usd/sdf/copyUtils.h>
#include <pxr/usd/sdf/schema.h>
#include <pxr/usd/usd/editContext.h>
#include <pxr/usd/usdGeom/bboxCache.h>
#include <pxr/usd/usdGeom/tokens.h>
#include <pxr/usd/usdGeom/xformCache.h>
#include <pxr/usd/usdGeom/xformable.h>

namespace stageviz {

namespace {
    using command::appendError;
    using command::currentEditLayer;
    using command::pathText;
    using command::removePropertySpec;
    using command::summarizeErrors;
}  // namespace

namespace command::detail {
    bool restoreXformState(UsdStageRefPtr stage, const SdfLayerHandle& editLayer, const SdfPath& primPath,
                           const XformState& state, QString& error)
    {
        if (!stage) {
            error = "stage missing";
            return false;
        }
        if (!editLayer) {
            error = "edit layer missing";
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

        if (!state.hadPrimSpec) {
            const SdfPrimSpecHandle primSpec = editLayer->GetPrimAtPath(primPath);
            if (primSpec && primSpec->IsInert())
                stage::removePrimSpec(editLayer, primPath);
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
        state.hadPrimSpec = bool(editLayer->GetPrimAtPath(primPath));
        state.missingAncestors.clear();
        for (SdfPath parent = primPath.GetParentPath(); parent.IsPrimPath(); parent = parent.GetParentPath()) {
            if (!editLayer->GetPrimAtPath(parent))
                state.missingAncestors.append(parent);
        }
        if (!state.snapshotLayer)
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
        if (!state.hadPrimSpec && remaining && remaining->IsInert())
            stage::removePrimSpec(editLayer, state.primPath);
        for (const auto& parent : state.missingAncestors) {
            const auto spec = editLayer->GetPrimAtPath(parent);
            if (spec && spec->IsInert())
                stage::removePrimSpec(editLayer, parent);
        }

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
            default: pivotSet = pivotOp.Set(localPivot); break;
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

    bool authorCanonicalTransformWithPivot(const UsdPrim& prim, const GfMatrix4d& localMatrix,
                                           const GfMatrix4d& localPivot, QString& error)
    {
        const UsdGeomXformable xformable(prim);
        const double determinant = localPivot.GetDeterminant();
        if (!xformable || !std::isfinite(determinant) || determinant == 0.0) {
            error = "Pivot frame must be invertible";
            return false;
        }
        for (int row = 0; row < 4; ++row) {
            for (int column = 0; column < 4; ++column) {
                if (!std::isfinite(localPivot[row][column])) {
                    error = "Pivot frame must be finite";
                    return false;
                }
            }
        }
        if (std::abs(localPivot[0][3]) > 1e-10 || std::abs(localPivot[1][3]) > 1e-10
            || std::abs(localPivot[2][3]) > 1e-10 || std::abs(localPivot[3][3] - 1.0) > 1e-10) {
            error = "Pivot frame must be affine";
            return false;
        }
        const bool reset = xformable.GetResetXformStack();
        // USD evaluates this ordered pair as inversePivot * matrix * pivot.
        // Compensate the middle op so authoring the frame leaves localMatrix unchanged.
        const GfMatrix4d compensated = localPivot * localMatrix * localPivot.GetInverse();
        const UsdAttribute pivot = prim.CreateAttribute(TfToken("xformOp:transform:pivot"), SdfValueTypeNames->Matrix4d,
                                                        false);
        const UsdAttribute matrix = prim.CreateAttribute(TfToken("xformOp:transform"), SdfValueTypeNames->Matrix4d,
                                                         false);
        if (!pivot || !matrix || !pivot.Set(localPivot) || !matrix.Set(compensated)
            || !xformable.SetXformOpOrder({ UsdGeomXformOp(pivot), UsdGeomXformOp(matrix), UsdGeomXformOp(pivot, true) },
                                          reset)) {
            error = "Failed to author pivot frame";
            return false;
        }
        GfMatrix4d evaluated(1.0);
        bool evaluatedReset = false;
        if (!xformable.GetLocalTransformation(&evaluated, &evaluatedReset) || !matrixClose(evaluated, localMatrix)) {
            error = "Failed to preserve object transform while authoring pivot frame";
            return false;
        }
        return true;
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

}  // namespace command::detail

namespace {
    using namespace command::detail;
}  // namespace

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
                        const SdfLayerHandle editLayer = currentEditLayer(stage, editError);

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
                        const SdfLayerHandle editLayer = currentEditLayer(stage, editError);

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
                        const SdfLayerHandle editLayer = currentEditLayer(stage, editError);

                        if (!editLayer) {
                            errors.append(editError);
                        }
                        else {
                            state->items.clear();
                            UsdGeomBBoxCache bboxCache(UsdTimeCode::Default(),
                                                       { UsdGeomTokens->default_, UsdGeomTokens->render,
                                                         UsdGeomTokens->proxy },
                                                       true);
                            for (const SdfPath& inputPath : unique) {
                                const SdfPath primPath = inputPath.IsPropertyPath() ? inputPath.GetPrimPath()
                                                                                    : inputPath;
                                const UsdPrim prim = stage->GetPrimAtPath(primPath);
                                const UsdGeomXformable xformable(prim);

                                if (!prim || !prim.IsValid() || prim.IsInstanceProxy() || !xformable) {
                                    errors.append(QString("prim is not transformable: %1").arg(pathText(primPath)));
                                    continue;
                                }

                                GfVec3d localPivot(0.0);
                                // The authored pivot is in the prim's object space. ComputeLocalBound()
                                // includes this prim's local transform, which would make the gizmo apply
                                // that transform a second time when it maps the pivot to world space.
                                const GfBBox3d localBound = bboxCache.ComputeUntransformedBound(prim);
                                const GfRange3d localRange = localBound.ComputeAlignedRange();
                                if (!localRange.IsEmpty())
                                    localPivot = localRange.GetMidpoint();

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
                        const SdfLayerHandle editLayer = currentEditLayer(stage, editError);

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
                        const SdfLayerHandle editLayer = currentEditLayer(stage, editError);

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
                        const SdfLayerHandle editLayer = currentEditLayer(stage, editError);

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

bool
beginTransformOperation(const UsdStageRefPtr& stage, const QList<SdfPath>& selection, TransformOperation& operation,
                        QString& error, bool minimalRoots)
{
    operation = TransformOperation();
    const SdfLayerHandle layer = currentEditLayer(stage, error);
    if (!layer || selection.isEmpty()) {
        if (error.isEmpty())
            error = "no transform selection";
        return false;
    }

    TransformOperation resolved;
    resolved.paths = stage::transformEditTargetPaths(stage, selection, minimalRoots);
    // Every input must resolve before any matrix/order override is authored.
    for (const SdfPath& selected : selection) {
        const SdfPath primPath = selected.IsPropertyPath() ? selected.GetPrimPath() : selected;
        if (!stage->GetPrimAtPath(primPath)) {
            error = QString("prim missing: %1").arg(pathText(selected));
            return false;
        }
        bool covered = false;
        for (const SdfPath& target : resolved.paths)
            covered = covered || primPath.HasPrefix(target);
        if (!covered) {
            error = QString("no editable transform target: %1").arg(pathText(selected));
            return false;
        }
    }
    resolved.targetCount = resolved.paths.size();
    resolved.paths.append(stage::transformPreservationPaths(stage, resolved.paths));
    for (const SdfPath& path : resolved.paths) {
        GfMatrix4d world(1.0);
        if (!stage::worldTransform(stage, path, world, error))
            return false;
        resolved.edit.before.append(world);

        XformState state;
        state.hadPrimSpec = bool(layer->GetPrimAtPath(path));
        const SdfPath orderPath = path.AppendProperty(TfToken("xformOpOrder"));
        const SdfPath matrixPath = path.AppendProperty(TfToken("xformOp:transform"));
        state.hadXformOpOrderSpec = bool(layer->GetPropertyAtPath(orderPath));
        state.hadXformOpOrderDefault = layer->HasField(orderPath, SdfFieldKeys->Default);
        if (state.hadXformOpOrderDefault)
            state.xformOpOrderDefault = layer->GetField(orderPath, SdfFieldKeys->Default);
        state.hadMatrixOpSpec = bool(layer->GetPropertyAtPath(matrixPath));
        state.hadMatrixOpDefault = layer->HasField(matrixPath, SdfFieldKeys->Default);
        if (state.hadMatrixOpDefault)
            state.matrixOpDefault = layer->GetField(matrixPath, SdfFieldKeys->Default);
        resolved.edit.beforeState.append(state);
    }
    resolved.edit.after = resolved.edit.before;
    operation = std::move(resolved);
    return true;
}

bool
applyTransformOperation(const UsdStageRefPtr& stage, TransformOperation& operation,
                        const QList<PreparedTransform>& prepared, const GfMatrix4d& delta, QStringList* errors)
{
    if (!stage || operation.paths.isEmpty() || operation.edit.before.size() != operation.paths.size()
        || prepared.size() != operation.paths.size() || operation.targetCount <= 0
        || operation.targetCount > operation.paths.size()) {
        if (errors)
            errors->append("invalid resolved transform operation");
        return false;
    }
    operation.edit.after.clear();
    for (qsizetype i = 0; i < operation.paths.size(); ++i)
        operation.edit.after.append(i < operation.targetCount ? operation.edit.before.at(i) * delta
                                                              : operation.edit.before.at(i));
    operation.edit.delta = delta;
    operation.edit.hasDelta = true;
    return applyPreparedTransforms(stage, prepared, operation.edit.after, delta, true, errors);
}

// Convert the absolute Python/API matrix pairs into deltas on resolved objects.
// Different inputs resolving to one object must agree; otherwise no edit occurs.
static bool
resolveTransformInput(const UsdStageRefPtr& stage, const QList<SdfPath>& paths, const XformEdit& input,
                      TransformOperation& operation, QString& error)
{
    if (paths.isEmpty() || input.before.size() != paths.size() || input.after.size() != paths.size()) {
        error = "paths, before and after transforms must have equal nonzero lengths";
        return false;
    }
    QList<GfMatrix4d> deltas;
    bool sharedDelta = true;
    for (qsizetype i = 0; i < paths.size(); ++i) {
        const double determinant = input.before.at(i).GetDeterminant();
        if (!std::isfinite(determinant) || determinant == 0.0) {
            error = QString("non-invertible before transform: %1").arg(pathText(paths.at(i)));
            return false;
        }
        const GfMatrix4d delta = input.before.at(i).GetInverse() * input.after.at(i);
        for (int row = 0; row < 4; ++row) {
            for (int column = 0; column < 4; ++column) {
                if (!std::isfinite(delta[row][column])) {
                    error = "transform delta contains a non-finite value";
                    return false;
                }
            }
        }
        if (input.hasDelta && !GfIsClose(delta, input.delta, 1e-8)) {
            error = "transform matrices disagree with the shared world delta";
            return false;
        }
        sharedDelta = sharedDelta && (deltas.isEmpty() || GfIsClose(delta, deltas.first(), 1e-8));
        deltas.append(delta);
    }
    if (!beginTransformOperation(stage, paths, operation, error, sharedDelta))
        return false;

    QList<bool> assigned(operation.targetCount, false);
    QList<GfMatrix4d> targetDeltas(operation.targetCount, GfMatrix4d(1));
    for (qsizetype i = 0; i < paths.size(); ++i) {
        const SdfPath selected = paths.at(i).IsPropertyPath() ? paths.at(i).GetPrimPath() : paths.at(i);
        qsizetype best = -1;
        for (qsizetype j = 0; j < operation.targetCount; ++j) {
            if (selected.HasPrefix(operation.paths.at(j))
                && (best < 0
                    || operation.paths.at(j).GetPathElementCount() > operation.paths.at(best).GetPathElementCount()))
                best = j;
        }
        if (best < 0) {
            error = QString("no resolved transform target: %1").arg(pathText(selected));
            return false;
        }
        if (assigned.at(best) && !GfIsClose(targetDeltas.at(best), deltas.at(i), 1e-8)) {
            error = QString("conflicting transforms resolve to the same object: %1")
                        .arg(pathText(operation.paths.at(best)));
            return false;
        }
        assigned[best] = true;
        targetDeltas[best] = deltas.at(i);
        operation.edit.after[best] = operation.edit.before.at(best) * deltas.at(i);
    }
    operation.edit.hasDelta = sharedDelta;
    operation.edit.delta = sharedDelta ? deltas.first() : GfMatrix4d(1);
    return true;
}

QList<PreparedTransform>
prepareTransforms(const UsdStageRefPtr& stage, const QList<SdfPath>& paths, const QList<GfMatrix4d>& baseMatrices,
                  QStringList* errors)
{
    QList<PreparedTransform> result;
    if (!stage || paths.isEmpty() || paths.size() != baseMatrices.size())
        return result;

    result.reserve(paths.size());
    QList<GfMatrix4d> localPivots;
    QList<bool> resetStacks;

    // Match the interactive gizmo setup exactly: establish the supplied base
    // world transforms first, then cache the resulting matrix ops and parent
    // transforms for repeated updates.
    //
    // Do not use stage::setWorldTransform() for this normalization. That path
    // canonicalizes the composed xform stack with MakeMatrixXform(), which can
    // fail when xformOpOrder is authored only in a weaker layer. The gizmo must
    // instead author a stronger matrix/order override in the active edit layer
    // without modifying the weaker transform opinions.
    {
        SdfChangeBlock changeBlock;

        for (qsizetype i = 0; i < paths.size(); ++i) {
            const SdfPath& path = paths.at(i);
            const UsdPrim prim = stage->GetPrimAtPath(path);
            QString error;
            GfMatrix4d localPivot(1.0);
            bool resetsStack = false;

            if (!prim || !prim.IsValid()) {
                error = QString("prim missing: %1").arg(qt::SdfPathToQString(path));
            }
            else if (prim.IsInstanceProxy() || prim.IsInPrototype()) {
                error = QString("transform requires an instance root: %1").arg(qt::SdfPathToQString(path));
            }
            else {
                UsdGeomXformable xformable(prim);
                if (!xformable) {
                    error = QString("prim is not xformable: %1").arg(qt::SdfPathToQString(path));
                }
                else {
                    resetsStack = xformable.GetResetXformStack();
                    // Retain the active pivot pair instead of leaving orphaned
                    // attributes behind a matrix-only order. Only the central
                    // matrix changes during an object transform.
                    VtTokenArray oldOrder;
                    xformable.GetXformOpOrderAttr().Get(&oldOrder);
                    TfToken pivotName;
                    for (const auto& name :
                         { TfToken("xformOp:transform:pivot"), TfToken("xformOp:translate:pivot") }) {
                        const auto forward = std::find(oldOrder.begin(), oldOrder.end(), name);
                        const auto inverse = std::find(oldOrder.begin(), oldOrder.end(),
                                                       TfToken("!invert!" + name.GetString()));
                        if (forward != oldOrder.end() && inverse != oldOrder.end() && forward < inverse) {
                            pivotName = name;
                            localPivot = UsdGeomXformOp(prim.GetAttribute(name)).GetOpTransform(UsdTimeCode::Default());
                            break;
                        }
                    }
                    GfMatrix4d parentWorld(1.0);
                    const UsdPrim parent = prim.GetParent();
                    if (!resetsStack && parent && !parent.IsPseudoRoot()) {
                        UsdGeomXformCache parentCache(UsdTimeCode::Default());
                        parentWorld = parentCache.GetLocalToWorldTransform(parent);
                    }

                    const GfMatrix4d local = localPivot * baseMatrices.at(i) * parentWorld.GetInverse()
                                             * localPivot.GetInverse();

                    UsdEditContext context(stage, stage->GetEditTarget());

                    UsdAttribute matrixAttr = prim.CreateAttribute(TfToken("xformOp:transform"),
                                                                   SdfValueTypeNames->Matrix4d, false,
                                                                   SdfVariabilityVarying);
                    if (!matrixAttr || matrixAttr.GetTypeName() != SdfValueTypeNames->Matrix4d) {
                        error
                            = QString("failed to create matrix transform override: %1").arg(qt::SdfPathToQString(path));
                    }
                    else if (!matrixAttr.Set(local, UsdTimeCode::Default())) {
                        error = QString("failed to set matrix transform override: %1").arg(qt::SdfPathToQString(path));
                    }
                    else {
                        VtTokenArray order;
                        if (resetsStack)
                            order.push_back(TfToken("!resetXformStack!"));
                        if (!pivotName.IsEmpty())
                            order.push_back(pivotName);
                        order.push_back(TfToken("xformOp:transform"));
                        if (!pivotName.IsEmpty())
                            order.push_back(TfToken("!invert!" + pivotName.GetString()));

                        const UsdAttribute orderAttr = xformable.CreateXformOpOrderAttr();
                        if (!orderAttr || !orderAttr.Set(order, UsdTimeCode::Default())) {
                            error
                                = QString("failed to set transform order override: %1").arg(qt::SdfPathToQString(path));
                        }
                    }
                }
            }

            localPivots.append(localPivot);
            resetStacks.append(resetsStack);
            if (!error.isEmpty() && errors)
                errors->append(error);
        }
    }

    UsdGeomXformCache xformCache(UsdTimeCode::Default());
    for (qsizetype i = 0; i < paths.size(); ++i) {
        const SdfPath& path = paths.at(i);
        PreparedTransform prepared;
        prepared.path = path;
        prepared.localPivot = localPivots.at(i);

        const UsdPrim prim = stage->GetPrimAtPath(path);
        if (!prim || !prim.IsValid() || prim.IsInstanceProxy() || prim.IsInPrototype()) {
            result.append(prepared);
            continue;
        }

        const UsdPrim parent = prim.GetParent();
        if (!resetStacks.at(i) && parent && !parent.IsPseudoRoot())
            prepared.parentWorld = xformCache.GetLocalToWorldTransform(parent);

        for (UsdPrim ancestor = resetStacks.at(i) ? UsdPrim() : parent; ancestor && !ancestor.IsPseudoRoot();
             ancestor = ancestor.GetParent()) {
            if (paths.contains(ancestor.GetPath())) {
                prepared.parentFollowsSelection = true;
                break;
            }
        }

        const UsdGeomXformable xformable(prim);
        bool resetsXformStack = false;
        const std::vector<UsdGeomXformOp> ops = xformable.GetOrderedXformOps(&resetsXformStack);
        if ((ops.size() == 1 || ops.size() == 3)) {
            prepared.matrixOp = ops.at(ops.size() == 3 ? 1 : 0);
            prepared.fast = bool(prepared.matrixOp);
        }

        result.append(prepared);
    }

    return result;
}

bool
applyPreparedTransforms(const UsdStageRefPtr& stage, const QList<PreparedTransform>& prepared,
                        const QList<GfMatrix4d>& matrices, const GfMatrix4d& selectionDelta, bool hasSelectionDelta,
                        QStringList* errors, QList<SdfPath>* changed)
{
    if (!stage || prepared.size() != matrices.size())
        return false;

    bool success = true;
    SdfChangeBlock changeBlock;

    for (qsizetype i = 0; i < prepared.size(); ++i) {
        const PreparedTransform& item = prepared.at(i);
        const UsdPrim prim = stage->GetPrimAtPath(item.path);
        if (!prim || prim.IsInstanceProxy() || prim.IsInPrototype()) {
            if (errors)
                errors->append(QString("invalid transform authoring target: %1").arg(qt::SdfPathToQString(item.path)));
            success = false;
            continue;
        }
        bool applied = false;

        if (item.fast && (!item.parentFollowsSelection || hasSelectionDelta)) {
            GfMatrix4d parentWorld = item.parentWorld;
            if (item.parentFollowsSelection)
                parentWorld = item.parentWorld * selectionDelta;

            const GfMatrix4d local = item.localPivot * matrices.at(i) * parentWorld.GetInverse()
                                     * item.localPivot.GetInverse();
            applied = item.matrixOp.Set(local, UsdTimeCode::Default());
        }

        if (!applied) {
            QString error;
            applied = stage::setWorldTransform(stage, item.path, matrices.at(i), error);
            if (!applied && errors) {
                errors->append(error.isEmpty() ? QString("failed: %1").arg(qt::SdfPathToQString(item.path)) : error);
            }
        }

        if (applied && changed)
            path::appendUnique(*changed, item.path);
        success = applied && success;
    }

    return success;
}

bool
restoreTransformOperation(const UsdStageRefPtr& stage, const TransformOperation& operation, QStringList* errors)
{
    QString error;
    const auto layer = currentEditLayer(stage, error);
    if (!layer || operation.paths.size() != operation.edit.beforeState.size()) {
        if (errors)
            errors->append(error.isEmpty() ? QStringLiteral("missing original transform state") : error);
        return false;
    }
    QList<qsizetype> order;
    for (qsizetype i = 0; i < operation.paths.size(); ++i)
        order.append(i);
    std::sort(order.begin(), order.end(), [&](qsizetype a, qsizetype b) {
        return operation.paths.at(a).GetPathElementCount() > operation.paths.at(b).GetPathElementCount();
    });
    bool success = true;
    SdfChangeBlock block;
    for (qsizetype i : order) {
        error.clear();
        if (!restoreXformState(stage, layer, operation.paths.at(i), operation.edit.beforeState.at(i), error)) {
            success = false;
            if (errors)
                errors->append(error);
        }
    }
    return success;
}

Command
setTransforms(const QList<SdfPath>& paths, const XformEdit& edit)
{
    struct State {
        TransformOperation operation;
        bool resolved = false;
    };
    auto state = std::make_shared<State>();
    // Interactive tools already resolved and captured this state before their
    // first write. Applied skips execute(), so undo must have it immediately.
    if (!paths.isEmpty() && edit.before.size() == paths.size() && edit.after.size() == paths.size()
        && edit.beforeState.size() == paths.size()) {
        state->operation.paths = paths;
        state->operation.edit = edit;
        state->operation.targetCount = paths.size();
        state->resolved = true;
    }

    return Command(
        [paths, edit, state](Session* session) {
            if (!session || paths.isEmpty())
                return;
            command::beginDeferred(session, "Transform paths", static_cast<int>(paths.size()));
            command::runWorker([session, paths, edit, state]() {
                bool success = false;
                QStringList errors;
                QList<SdfPath> changed;
                {
                    WRITE_LOCKER(locker, session->stageLock(), "stageLock");
                    const auto stage = session->stageUnsafe();
                    if (!state->resolved) {
                        QString error;
                        state->resolved = resolveTransformInput(stage, paths, edit, state->operation, error);
                        if (!state->resolved)
                            errors.append(error);
                    }
                    if (state->resolved) {
                        auto& operation = state->operation;
                        const auto prepared = prepareTransforms(stage, operation.paths, operation.edit.before, &errors);
                        if (errors.isEmpty()) {
                            success = applyPreparedTransforms(stage, prepared, operation.edit.after,
                                                              operation.edit.delta, operation.edit.hasDelta, &errors,
                                                              &changed);
                        }
                        if (!success)
                            restoreTransformOperation(stage, operation, &errors);
                    }
                }
                const QString errorText = summarizeErrors(errors);
                command::queueToSession(session, [session, changed, success, errorText]() {
                    command::finishDeferred(session,
                                            success ? QStringLiteral("Paths transformed")
                                                    : appendError(QStringLiteral("Transform paths failed"), errorText),
                                            changed,
                                            success ? Session::Notify::Status::Success
                                                    : Session::Notify::Status::Error);
                });
            });
        },
        [state](Session* session) {
            if (!session)
                return;
            command::beginDeferred(session, "Undo transform paths", 1);
            command::runWorker([session, state]() {
                bool success = true;
                QStringList errors;
                QList<SdfPath> changed;
                {
                    WRITE_LOCKER(locker, session->stageLock(), "stageLock");
                    if (state->resolved) {
                        success = restoreTransformOperation(session->stageUnsafe(), state->operation, &errors);
                        changed = state->operation.paths;
                    }
                }
                const QString errorText = summarizeErrors(errors);
                command::queueToSession(session, [session, changed, success, errorText]() {
                    command::finishDeferred(session,
                                            success ? QStringLiteral("Transform undone")
                                                    : appendError(QStringLiteral("Undo transform failed"), errorText),
                                            changed,
                                            success ? Session::Notify::Status::Success
                                                    : Session::Notify::Status::Error);
                });
            });
        });
}


Command
setTransforms(const TransformOperation& operation)
{
    return setTransforms(operation.paths, operation.edit);
}



}  // namespace stageviz
