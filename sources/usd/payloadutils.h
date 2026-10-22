// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#pragma once

#include "stageviz.h"
#include <QList>
#include <QMap>
#include <pxr/usd/usd/stage.h>

PXR_NAMESPACE_USING_DIRECTIVE

namespace stageviz {

namespace payload {
    /**
     * @struct PayloadState
     * @brief Captured payload state used to restore load and variant selection.
     */
    struct PayloadState {
        SdfPath path;
        bool wasLoaded = false;
        bool hadVariantSet = false;
        std::string variantSetName;
        std::string previousVariantSelection;
    };

    /**
     * @brief Load a payload prim and optionally switch a variant first.
     *
     * The previous loaded state and variant selection are written into @p undoItem.
     */
    bool applyLoad(UsdStageRefPtr stage, const SdfPath& path, bool useVariant, const std::string& variantSetName,
                   const std::string& variantSelection, PayloadState& payloadState, QString& error);

    /**
     * @brief Unload a payload prim and capture previous loaded state.
     */
    bool applyUnload(UsdStageRefPtr stage, const SdfPath& path, PayloadState& payloadState, QString& error);

    /**
     * @brief Restore a payload prim to a previously captured loaded/variant state.
     */
    bool restoreState(UsdStageRefPtr stage, const PayloadState& payloadState, QString& error);

    using PayloadVariantTargets = QMap<QString, QMap<QString, QList<SdfPath>>>;

    /**
     * @brief Collects payload variant targets relevant to selected branches.
     *
     * Inspects each top-level selected prim, its ancestor chain, and its descendants,
     * while collecting only prims that have payloads and variant sets. Sibling branches
     * outside the selected subtree are not traversed. Duplicate prims are ignored.
     *
     * @param stage USD stage to query.
     * @param paths Prim or property paths that define the selected branches.
     *
     * @return Map of variant set name to variant value to compatible payload paths.
     */
    PayloadVariantTargets payloadVariantTargets(UsdStageRefPtr stage, const QList<SdfPath>& paths);

    /**
     * @brief Finds unloaded payloads spatially neighboring selected payloads.
     *
     * Resolves each input path to its top-most payload ancestor and evaluates
     * the source payload extentsHint bounds in world space. Extents are read
     * through UsdGeomModelAPI when available, with direct extentsHint attribute
     * access as a fallback for payload prims that carry the metadata without the
     * API schema being applied.
     *
     * The source bounds are combined into one world-space bounding box. That box
     * is expanded by a factor of 1.5 around its center while preserving its
     * proportions. Only unloaded top-level payloads with usable extentsHint are
     * considered as candidates.
     *
     * A candidate is included when the center of its world-space extents lies
     * inside the expanded source box. Accepted candidates are sorted nearest-first
     * by distance between the candidate center and the combined source center.
     *
     * @param stage Stage containing the payloads.
     * @param inputPaths Payload paths or descendant paths inside payloads.
     *
     * @return Unloaded neighboring payload paths sorted nearest-first.
     */
    QList<SdfPath> neighboringPaths(UsdStageRefPtr stage, const QList<SdfPath>& inputPaths);

    /**
     * @struct AssetEntry
     * @brief Describes an asset referenced by a payload prim.
     *
     * Entries may represent either a payload authored directly on the prim or
     * a payload authored inside one of the prim's variants.
     *
     * For direct payloads, @p variantSet and @p variantValue are empty.
     * For variant payloads, they identify the variant selection under which
     * the asset is authored.
     */
    struct AssetEntry {
        SdfPath primPath;
        QString variantSet;
        QString variantValue;
        QString assetPath;
    };

    /**
     * @brief Collects payload asset entries for the specified prim paths.
     *
     * Inspects the selected prims and returns payload asset paths authored
     * directly on those prims and inside their variant specifications.
     *
     * Each returned entry records the owning prim path, optional variant set
     * and variant value, and the authored asset path. Duplicate authored
     * entries may be returned when the same asset is referenced by multiple
     * prims or variants.
     *
     * The function does not resolve asset paths to absolute filesystem paths;
     * @p assetPath contains the authored USD asset path value.
     *
     * @param stage Stage containing the prims to inspect.
     * @param paths Prim paths whose payload specifications should be queried.
     *
     * @return List of payload asset entries found at the specified paths.
     */
    QList<AssetEntry> assetEntries(UsdStageRefPtr stage, const QList<SdfPath>& paths);

}  // namespace payload

namespace stage {
    /**
     * @brief Returns true when a payload opinion for @p path is authored in @p layer.
     */
    bool isPayloadInLayer(UsdStageRefPtr stage, const SdfLayerHandle& layer, const SdfPath& path);

    /**
     * @brief Collects composed payload prims whose payload opinion is authored in @p layer.
     */
    QList<SdfPath> layerPayloadPaths(UsdStageRefPtr stage, const SdfLayerHandle& layer);

    /**
     * @brief Finds the nearest enclosing payload authored in @p layer for each input path.
     */
    QList<SdfPath> nearestLayerPayloadPaths(UsdStageRefPtr stage, const SdfLayerHandle& layer,
                                            const QList<SdfPath>& paths);

    /**
     * @brief Collects nearest payload ancestor paths for the specified prim paths.
     *
     * Walks upward from each input path until it finds the nearest composed
     * payload prim, regardless of which layer authored the payload opinion.
     */
    QList<SdfPath> nearestPayloadPaths(UsdStageRefPtr stage, const QList<SdfPath>& paths);

    /**
     * @brief Collects the outermost payload ancestor paths for specified prim paths.
     *
     * Walks from each input prim to the pseudo-root and keeps the outermost
     * payload encountered. Property paths are normalized to their owning prim
     * path. Duplicate and descendant results are removed.
     */
    QList<SdfPath> outermostPayloadPaths(UsdStageRefPtr stage, const QList<SdfPath>& paths);

    /**
     * @brief Collects payload paths at and below the specified prim paths.
     *
     * Traverses each input root and collects descendant prims that directly
     * author or contain payloads.
     *
     * @param stage USD stage to query.
     * @param paths Root prim paths to traverse.
     *
     * @return List of descendant payload paths.
     */
    QList<SdfPath> descendantPayloadPaths(UsdStageRefPtr stage, const QList<SdfPath>& paths);

    /**
     * @brief Checks whether payloads at and below a path are fully loaded.
     *
     * Traverses the path hierarchy and returns true only when at least one
     * payload is found and every found payload is loaded.
     *
     * @param stage USD stage containing the hierarchy.
     * @param path Root prim path to evaluate.
     *
     * @return True if all found payloads are loaded.
     */
    bool isLoaded(UsdStageRefPtr stage, const SdfPath& path);

    /**
     * @brief Checks whether a composed prim has a payload.
     *
     * Uses UsdPrim::HasPayload() on the composed stage prim.
     *
     * @param stage USD stage containing the prim.
     * @param path Prim path to evaluate.
     *
     * @return True if the composed prim has a payload.
     */
    bool isPayload(UsdStageRefPtr stage, const SdfPath& path);

    /**
     * @brief Checks whether a prim is inside a payload hierarchy.
     *
     * Walks from the prim to its ancestors and checks for payload boundaries.
     *
     * @param stage USD stage containing the prim.
     * @param path Prim path to evaluate.
     *
     * @return True if the prim or an ancestor has a payload.
     */
    bool isPayloadHierarchy(UsdStageRefPtr stage, const SdfPath& path);

    /**
     * @brief Collects payload paths exactly at the specified prim paths.
     *
     * Checks each input path and returns paths whose prim directly authors
     * or contains a payload.
     *
     * @param stage USD stage to query.
     * @param paths Prim paths to test.
     *
     * @return Payload paths found at the input paths.
     */
    QList<SdfPath> payloadPaths(UsdStageRefPtr stage, const QList<SdfPath>& paths);

    /**
     * @brief Remaps stage load rules from one hierarchy path to another.
     *
     * Rules at or below @p oldPath are moved under @p newPath while preserving
     * their relative suffix and policy.
     *
     * @param rules Existing stage load rules.
     * @param oldPath Original hierarchy root.
     * @param newPath New hierarchy root.
     *
     * @return Remapped stage load rules.
     */
    UsdStageLoadRules remapLoadRules(const UsdStageLoadRules& rules, const SdfPath& oldPath, const SdfPath& newPath);

    /**
     * @brief Resolves selection paths to payload paths for load/unload commands.
     *
     * Uses payload ancestors when available; otherwise searches descendants.
     * The result is deduplicated and reduced to outermost paths.
     *
     * @param stage USD stage to query.
     * @param paths Selected prim paths to resolve.
     *
     * @return Resolved payload paths.
     */
    QList<SdfPath> resolvePayloadPaths(UsdStageRefPtr stage, const QList<SdfPath>& paths);
}  // namespace stage

}  // namespace stageviz
