// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#pragma once

#include "stageviz.h"
#include <QList>
#include <QString>
#include <pxr/base/gf/matrix4d.h>
#include <pxr/base/gf/vec3d.h>
#include <pxr/usd/sdf/layer.h>
#include <pxr/usd/usd/prim.h>

PXR_NAMESPACE_USING_DIRECTIVE

namespace stageviz::command::detail {

/**
 * @brief Snapshots edit-layer transform properties for pivot edits and undo.
 */
struct TransformPropertyState {
    bool hadPrimSpec = false;
    QList<SdfPath> missingAncestors;
    SdfPath primPath;
    SdfLayerRefPtr snapshotLayer;
    QList<SdfPath> propertyPaths;
};

/**
 * @brief Captures transform property specs without authoring to the edit layer.
 */
bool
captureTransformPropertyState(const SdfLayerHandle& editLayer, const SdfPath& primPath, TransformPropertyState& state,
                              QString& error);

/**
 * @brief Restores the complete captured edit-layer transform property specs.
 */
bool
restoreTransformPropertyState(const SdfLayerHandle& editLayer, const TransformPropertyState& state, QString& error);

/**
 * @brief Authors paired pivot ops while preserving the evaluated local transform.
 */
bool
authorCanonicalTransformWithPivot(const UsdPrim& prim, const GfMatrix4d& localMatrix, const GfVec3d& localPivot,
                                  QString& error);

/**
 * @brief Authors a full paired pivot frame while preserving the local transform and stack reset.
 */
bool
authorCanonicalTransformWithPivot(const UsdPrim& prim, const GfMatrix4d& localMatrix, const GfMatrix4d& localPivot,
                                  QString& error);

}  // namespace stageviz::command::detail
