// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#include "commandpivot.h"
#include "commandtransformprivate.h"
#include "commandutils.h"
#include "tracelocks.h"
#include "usdutils.h"
#include <cmath>
#include <pxr/usd/usd/editContext.h>
#include <pxr/usd/usdGeom/xformable.h>

namespace stageviz {

struct PivotOperationState {
    UsdEditTarget editTarget;
    QList<command::detail::TransformPropertyState> snapshots;
    QList<GfMatrix4d> localMatrices;
};

namespace {
    bool validFrame(const GfMatrix4d& matrix)
    {
        for (int row = 0; row < 4; ++row)
            for (int col = 0; col < 4; ++col)
                if (!std::isfinite(matrix[row][col]))
                    return false;
        const double determinant = matrix.GetDeterminant();
        return std::isfinite(determinant) && determinant != 0.0 && std::abs(matrix[0][3]) < 1e-10
               && std::abs(matrix[1][3]) < 1e-10 && std::abs(matrix[2][3]) < 1e-10
               && std::abs(matrix[3][3] - 1.0) < 1e-10;
    }

    Command pivotCommand(const QList<SdfPath>& paths, const GfMatrix4d& delta, const PivotOperation& captured)
    {
        auto operation = std::make_shared<PivotOperation>(captured);
        auto action = [operation, paths, delta](Session* session, bool undo) {
            if (!session)
                return;
            command::beginDeferred(session, undo ? "Undo pivot transform" : "Transform pivots",
                                   static_cast<int>(paths.size()));
            command::runWorker([session, operation, paths, delta, undo]() {
                QStringList errors;
                bool success = false;
                {
                    WRITE_LOCKER(locker, session->stageLock(), "stageLock");
                    const auto stage = session->stageUnsafe();
                    if (!operation->state && !undo) {
                        QString error;
                        if (!beginPivotOperation(stage, paths, *operation, error))
                            errors.append(error);
                    }
                    if (operation->state) {
                        success = undo ? restorePivotOperation(stage, *operation, &errors)
                                       : applyPivotOperation(stage, *operation, delta, &errors);
                    }
                }
                command::queueToSession(session, [session, operation, success, errors, undo]() {
                    const QString text = undo ? QStringLiteral("Pivot transform undone")
                                              : QStringLiteral("Pivots transformed");
                    command::finishDeferred(
                        session, success ? text : command::appendError("Pivot transform failed", errors.join('\n')),
                        operation->paths, success ? Session::Notify::Status::Success : Session::Notify::Status::Error);
                });
            });
        };
        return Command([action](Session* session) { action(session, false); },
                       [action](Session* session) { action(session, true); });
    }
}  // namespace

bool
beginPivotOperation(const UsdStageRefPtr& stage, const QList<SdfPath>& selection, PivotOperation& operation,
                    QString& error)
{
    operation = PivotOperation();
    PivotOperation captured;
    if (!beginTransformOperation(stage, selection, captured, error))
        return false;
    if (!stage->GetEditTarget().GetLayer()->PermissionToEdit()) {
        error = "Edit layer is locked";
        return false;
    }
    auto state = std::make_shared<PivotOperationState>();
    state->editTarget = stage->GetEditTarget();
    const auto snapshotLayer = SdfLayer::CreateAnonymous("stageviz_pivot_snapshot.usda");
    if (!snapshotLayer) {
        error = "Failed to create pivot undo snapshot";
        return false;
    }
    for (qsizetype i = 0; i < captured.paths.size(); ++i) {
        const auto& path = captured.paths.at(i);
        if (!validFrame(captured.edit.before.at(i))) {
            error = "Pivot editing requires an invertible affine object transform";
            return false;
        }
        GfMatrix4d frame(1.0), local(1.0);
        bool reset = false;
        const UsdGeomXformable xformable(stage->GetPrimAtPath(path));
        command::detail::TransformPropertyState snapshot;
        snapshot.snapshotLayer = snapshotLayer;
        if (!stage::worldPivotFrame(stage, path, frame, error)
            || !xformable.GetLocalTransformation(&local, &reset, UsdTimeCode::Default())
            || !command::detail::captureTransformPropertyState(state->editTarget.GetLayer(), path, snapshot, error))
            return false;
        if (!validFrame(frame)) {
            error = "Pivot editing requires an invertible affine pivot frame";
            return false;
        }
        state->snapshots.append(snapshot);
        state->localMatrices.append(local);
        captured.beforeFrames.append(frame);
    }
    captured.afterFrames = captured.beforeFrames;
    captured.state = state;
    operation = std::move(captured);
    return true;
}

bool
applyPivotOperation(const UsdStageRefPtr& stage, PivotOperation& operation, const GfMatrix4d& delta,
                    QStringList* errors)
{
    if (!stage || !operation.state || operation.paths.size() != operation.beforeFrames.size()
        || operation.paths.size() != operation.state->snapshots.size() || !validFrame(delta)) {
        if (errors)
            errors->append("Invalid pivot operation or singular/non-finite delta");
        return false;
    }
    QList<GfMatrix4d> frames, localFrames;
    for (qsizetype i = 0; i < operation.paths.size(); ++i) {
        const GfMatrix4d frame = i < operation.targetCount ? operation.beforeFrames.at(i) * delta
                                                           : operation.beforeFrames.at(i);
        const GfMatrix4d local = frame * operation.edit.before.at(i).GetInverse();
        const auto prim = stage->GetPrimAtPath(operation.paths.at(i));
        if (!prim || prim.IsInstanceProxy() || prim.IsInPrototype() || !validFrame(local)) {
            if (errors)
                errors->append("Invalid pivot frame or authoring target");
            return false;
        }
        frames.append(frame);
        localFrames.append(local);
    }
    UsdEditContext context(stage, operation.state->editTarget);
    for (qsizetype i = 0; i < operation.paths.size(); ++i) {
        QString error;
        if (!command::detail::authorCanonicalTransformWithPivot(stage->GetPrimAtPath(operation.paths.at(i)),
                                                                operation.state->localMatrices.at(i), localFrames.at(i),
                                                                error)) {
            if (errors)
                errors->append(error);
            restorePivotOperation(stage, operation, errors);
            return false;
        }
    }
    operation.afterFrames = frames;
    operation.edit.delta = delta;
    operation.edit.hasDelta = true;
    return true;
}

bool
restorePivotOperation(const UsdStageRefPtr& stage, const PivotOperation& operation, QStringList* errors)
{
    if (!stage || !operation.state)
        return false;
    bool success = true;
    for (auto i = operation.state->snapshots.crbegin(); i != operation.state->snapshots.crend(); ++i) {
        QString error;
        if (!command::detail::restoreTransformPropertyState(operation.state->editTarget.GetLayer(), *i, error)) {
            success = false;
            if (errors)
                errors->append(error);
        }
    }
    return success;
}

Command
setPivotTransforms(const PivotOperation& operation)
{
    return pivotCommand(operation.paths, operation.edit.delta, operation);
}

Command
transformPivots(const QList<SdfPath>& paths, const GfMatrix4d& delta)
{
    return pivotCommand(paths, delta, PivotOperation());
}

}  // namespace stageviz
