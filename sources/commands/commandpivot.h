// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#pragma once

#include "commandtransform.h"
#include <memory>

namespace stageviz {

struct PivotOperationState;

/**
 * @brief Extends resolved transform targets with pivot frames and complete undo snapshots.
 *
 * Frames map pivot coordinates into world space. Object transforms stay fixed.
 * State owns the edit-layer snapshots shared with an undoable command.
 */
struct PivotOperation : TransformOperation {
    QList<GfMatrix4d> beforeFrames;
    QList<GfMatrix4d> afterFrames;
    std::shared_ptr<PivotOperationState> state;
};

/**
 * @brief Resolves pivot targets and captures their original frames and layer opinions.
 *
 * The caller holds the stage read or write lock. Session selection is unchanged.
 */
bool
beginPivotOperation(const UsdStageRefPtr& stage, const QList<SdfPath>& selection, PivotOperation& operation,
                    QString& error);

/**
 * @brief Applies a drag-start-relative world delta to pivot frames, preserving geometry.
 *
 * The caller holds the stage write lock. Singular and non-finite frames are rejected.
 */
bool
applyPivotOperation(const UsdStageRefPtr& stage, PivotOperation& operation, const GfMatrix4d& delta,
                    QStringList* errors = nullptr);

/**
 * @brief Restores the original edit-layer transform property specs for undo or cancellation.
 *
 * The caller holds the stage write lock.
 */
bool
restorePivotOperation(const UsdStageRefPtr& stage, const PivotOperation& operation, QStringList* errors = nullptr);

/**
 * @brief Creates a command for an already-applied pivot preview and its undo state.
 */
Command
setPivotTransforms(const PivotOperation& operation);

/**
 * @brief Creates a command that applies a world-space delta to selected pivot frames.
 *
 * Target resolution and snapshot capture occur on first execution.
 */
Command
transformPivots(const QList<SdfPath>& paths, const GfMatrix4d& delta);

}  // namespace stageviz
