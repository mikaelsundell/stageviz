// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 - present Mikael Sundell

#pragma once

#include <QList>
#include <QSet>
#include <pxr/usd/usd/stage.h>

PXR_NAMESPACE_USING_DIRECTIVE

namespace stageviz::command {

/**
 * @brief Finds local flattened source roots referenced beneath the deleted paths.
 *
 * Uses composed USD arcs to identify source sites. Only untyped root overs with
 * USD's flattened-prototype naming convention in the edit layer are eligible.
 * Ordinary references left by Make editable are included.
 */
QSet<SdfPath>
flattenedInstanceSources(const UsdStageRefPtr& stage, const SdfLayerHandle& editLayer, const QList<SdfPath>& paths);

/**
 * @brief Finds candidate sources without surviving authored users in this document.
 *
 * Audits composition dependencies, including inactive variants and muted or
 * unloaded layer assets. Candidate-to-candidate dependencies are retained when
 * reachable from a surviving user. An unresolved layer prevents cleanup.
 * This function never authors or saves layers.
 */
QList<SdfPath>
unusedFlattenedInstanceSources(const UsdStageRefPtr& stage, const SdfLayerHandle& editLayer,
                               const QSet<SdfPath>& candidates);

}  // namespace stageviz::command
