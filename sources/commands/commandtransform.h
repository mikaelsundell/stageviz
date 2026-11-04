// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#pragma once

#include "command.h"
#include <QList>
#include <QStringList>
#include <pxr/base/gf/matrix4d.h>
#include <pxr/usd/usdGeom/xformOp.h>

PXR_NAMESPACE_USING_DIRECTIVE

namespace stageviz {

/**
 * @name Transform Operations
 */
/**
 * @{
 */

/**
 * @struct XformState
 * @brief Captures the edit-layer transform opinions that existed before an
 * interactive transform preview authored a stronger matrix override.
 *
 * The state is used by transform undo to restore the original edit-layer
 * xformOpOrder/matrix opinions exactly, or remove the temporary override when
 * no edit-layer transform opinion existed before the drag.
 */
struct XformState {
    SdfPath specPath;
    QList<SdfPath> missingAncestors;
    bool hadPrimSpec = false;
    bool hadXformOpOrderSpec = false;
    bool hadXformOpOrderDefault = false;
    VtValue xformOpOrderDefault;
    bool hadMatrixOpSpec = false;
    bool hadMatrixOpDefault = false;
    VtValue matrixOpDefault;
};

/**
 * @struct XformEdit
 * @brief Durable before/after state for an undoable transform edit.
 *
 * Stores only persistent command data. Live UsdGeomXformOp handles are kept in
 * PreparedTransform while an interactive edit is active and are not retained by
 * the undo stack.
 */
struct XformEdit {
    QList<GfMatrix4d> before;
    QList<GfMatrix4d> after;
    QList<XformState> beforeState;
    GfMatrix4d delta = GfMatrix4d(1.0);
    bool hasDelta = false;
};

/**
 * @struct PreparedTransform
 * @brief Temporary cached USD transform state used for fast repeated authoring.
 */
struct PreparedTransform {
    SdfPath path;
    SdfLayerHandle editLayer;
    SdfPath matrixSpecPath;
    UsdGeomXformOp matrixOp;
    GfMatrix4d parentWorld = GfMatrix4d(1.0);
    GfMatrix4d localPivot = GfMatrix4d(1.0);
    bool parentFollowsSelection = false;
    bool fast = false;
};

/**
 * @brief Resolved object targets and durable undo state for one transform operation.
 *
 * The first targetCount paths move. Remaining paths preserve unselected
 * instances that inherit a selected source's transform. No USD handles are
 * retained in this state, so it can be copied into an undoable command.
 */
struct TransformOperation {
    QList<SdfPath> paths;
    XformEdit edit;
    qsizetype targetCount = 0;
};

/**
 * @brief Resolves selected paths and captures undo state before any authoring.
 *
 * Subsets resolve to geometry, proxy children to instance roots, and shared
 * sources to reference roots. Targets are deduplicated. Session selection is
 * never modified. The caller must hold the stage read or write lock.
 *
 * @param stage Stage to edit.
 * @param selection Exact selected USD paths.
 * @param operation Receives resolved paths and original transform opinions.
 * @param error Receives a failure reason.
 * @param minimalRoots Removes descendants covered by a selected ancestor.
 *
 * @return True when all inputs resolve to valid transform targets.
 */
bool
beginTransformOperation(const UsdStageRefPtr& stage, const QList<SdfPath>& selection, TransformOperation& operation,
                        QString& error, bool minimalRoots = true);

/**
 * @brief Applies a world delta to a resolved operation using cached authoring state.
 *
 * The delta is relative to the operation's original state on every preview
 * update. The caller must hold the stage write lock.
 *
 * @param stage Stage to edit.
 * @param operation Resolved operation whose final command state is updated.
 * @param prepared Cached authoring state for the operation paths.
 * @param delta World-space delta shared by selected targets.
 * @param errors Receives authoring errors.
 *
 * @return True when all resolved targets were updated.
 */
bool
applyTransformOperation(const UsdStageRefPtr& stage, TransformOperation& operation,
                        const QList<PreparedTransform>& prepared, const GfMatrix4d& delta,
                        QStringList* errors = nullptr);

/**
 * @brief Normalizes the supplied prims to their base world transforms and caches
 * the resulting matrix xformOps where possible.
 *
 * Normalization authors a matrix xformOp and xformOpOrder in the active edit
 * layer while preserving an active pivot pair and reset-stack behavior.
 * Weaker composed transform opinions are left untouched, allowing the
 * same prepared path to work for root-layer and sublayer editing.
 *
 * The caller must hold the stage write lock.
 */
QList<PreparedTransform>
prepareTransforms(const UsdStageRefPtr& stage, const QList<SdfPath>& paths, const QList<GfMatrix4d>& baseMatrices,
                  QStringList* errors = nullptr);

/**
 * @brief Applies world transforms using prepared matrix ops when safe and the
 * canonical stage transform path otherwise.
 *
 * The caller must hold the stage write lock.
 */
bool
applyPreparedTransforms(const UsdStageRefPtr& stage, const QList<PreparedTransform>& prepared,
                        const QList<GfMatrix4d>& matrices, const GfMatrix4d& selectionDelta, bool hasSelectionDelta,
                        QStringList* errors = nullptr, QList<SdfPath>* changed = nullptr);

/**
 * @brief Restores the original edit-layer opinions for undo or a cancelled preview.
 *
 * The caller must hold the stage write lock.
 *
 * @param stage Stage to restore.
 * @param operation Operation captured before authoring.
 * @param errors Receives restoration errors.
 *
 * @return True when all original transform opinions were restored.
 */
bool
restoreTransformOperation(const UsdStageRefPtr& stage, const TransformOperation& operation,
                          QStringList* errors = nullptr);

/**
 * @brief Creates a command that applies world-space transforms to prims.
 *
 * The edit stores the before/after transform state required for deterministic
 * undo and redo. Interactive tools may apply the final state live and add the
 * command to CommandStack using ExecutionMode::Applied.
 * Without captured beforeState, matrix pairs describe deltas on the selected
 * paths. Resolution and undo capture occur on first execution, before any
 * writes. Conflicting deltas for one resolved object are rejected. A complete
 * beforeState denotes already-resolved targets captured for a live preview.
 *
 * @param paths Prim paths to transform.
 * @param edit Transform state used for execute, undo, and redo.
 */
Command
setTransforms(const QList<SdfPath>& paths, const XformEdit& edit);

/**
 * @brief Creates an undoable command from a resolved transform operation.
 *
 * Interactive previews register this command using ExecutionMode::Applied.
 *
 * @param operation Resolved targets with captured original and final states.
 *
 * @return Command that restores and replays the resolved operation.
 */
Command
setTransforms(const TransformOperation& operation);

/**
 * @brief Copies resolved objects as siblings and applies a shared world delta.
 *
 * Uses the same subset, instance-proxy and ancestor resolution as transforms.
 * Originals are unchanged. Copies are selected; undo removes them and restores
 * the exact previous selection, and redo recreates them at the moved position.
 * Execute after restoring any interactive preview, using ExecutionMode::Execute.
 *
 * @param paths Exact selected USD paths.
 * @param worldDelta World-space delta applied on top of each original transform.
 */
Command
copyTransforms(const QList<SdfPath>& paths, const GfMatrix4d& worldDelta);

/**
 * @brief Creates a command that centers manipulation pivots on prim bounds.
 *
 * Each selected xformable prim receives its own pivot at the center of its
 * world-space bound. The standard paired USD pivot representation
 * xformOp:translate:pivot / !invert!xformOp:translate:pivot is authored while
 * preserving the prim's evaluated local transform.
 *
 * Undo restores the previous edit-layer transform property specs exactly.
 *
 * @param paths Prim paths whose pivots should be centered.
 */
Command
centerPivots(const QList<SdfPath>& paths);

/**
 * @brief Creates a command that removes the Stageviz-authored pivot.
 *
 * The active edit-layer pivot properties are removed by rewriting the prim to
 * Stageviz's canonical matrix-only transform while preserving its evaluated
 * local transform. No zero-valued pivot attribute is left behind.
 *
 * After reset, stage::worldPivot() resolves a weaker authored paired pivot when
 * one exists. If no authored pivot exists in the composed stack, the gizmo
 * falls back to the prim's transformed local origin.
 *
 * Undo restores the previous edit-layer transform property specs exactly.
 *
 * @param paths Prim paths whose pivots should be reset.
 */
Command
resetPivots(const QList<SdfPath>& paths);

/**
 * @brief Creates a command that resets local transforms for xformable prims.
 *
 * All transform properties authored in the active edit layer are removed,
 * including xformOpOrder and every xformOp:* property. This prevents stale
 * translate, rotate, scale, pivot, or matrix operations from remaining after
 * the reset.
 *
 * When a weaker composed transform opinion exists, removing the edit-layer
 * transform properties exposes that weaker transform again. When no weaker
 * transform opinion exists, a single identity xformOp:transform is authored in
 * the active edit layer.
 *
 * Undo restores the complete previous set of edit-layer transform properties
 * exactly.
 *
 * @param paths Prim paths whose local transforms should be reset.
 */
Command
resetTransforms(const QList<SdfPath>& paths);

/**
 * @brief UI-named wrapper for resetTransforms().
 *
 * Transform > Identity uses the same edit-layer-aware reset behavior: stale
 * transform ops are removed, weaker composed transforms are exposed when they
 * exist, otherwise a single identity matrix op is authored.
 */
Command
identityTransforms(const QList<SdfPath>& paths);

/**
 * @}
 */

}  // namespace stageviz
