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
namespace path {
    /**
     * @brief Filters a list of prim paths to only top-most paths.
     *
     * Removes paths that are descendants of other paths in the list.
     * This prevents redundant traversal when operating recursively
     * on hierarchies.
     *
     * Example:
     *   /A
     *   /A/B
     *   /A/B/C
     *
     * Result:
     *   /A
     *
     * @param paths Prim paths to filter.
     *
     * @return List of top-most prim paths.
     */
    QList<SdfPath> topLevelPaths(const QList<SdfPath>& paths);

    /**
     * @brief Filters a list of prim paths to only minimal root paths.
     *
     * Sorts the input by hierarchy depth and removes paths that are equal to,
     * or descendants of, already accepted paths. This is useful when editing
     * or deleting a hierarchy, where operating on a parent path implicitly
     * covers all of its descendants.
     *
     * @param paths Prim paths to reduce.
     *
     * @return List of minimal non-overlapping root paths.
     */
    QList<SdfPath> minimalRootPaths(const QList<SdfPath>& paths);

    /**
     * @brief Checks whether a selected path is affected by a root path.
     *
     * Property paths are normalized to their owning prim paths before
     * comparison. The function returns true if the selected path is equal to,
     * or a descendant of, the root path.
     *
     * @param selectedPath Selected prim or property path.
     * @param rootPath Root prim or property path.
     *
     * @return True if the selected path is affected by the root path.
     */
    bool isAffectedPath(const SdfPath& selectedPath, const SdfPath& rootPath);

    /**
     * @brief Removes paths affected by a set of root paths.
     *
     * For each path in @p paths, removes it from the result if it is equal to,
     * or a descendant of, any path in @p removedPaths.
     *
     * @param paths Input prim or property paths.
     * @param removedPaths Root paths whose affected descendants should be removed.
     *
     * @return Filtered list with affected paths removed.
     */
    QList<SdfPath> removeAffectedPaths(const QList<SdfPath>& paths, const QList<SdfPath>& removedPaths);

    /**
     * @brief Remaps affected paths from one hierarchy root to another.
     *
     * For each path in @p paths, if it is equal to, or a descendant of,
     * @p oldPath, it is remapped into the hierarchy under @p newPath while
     * preserving its relative suffix.
     *
     * Example:
     *   oldPath = /World/A
     *   newPath = /World/B
     *   path    = /World/A/Geom/Cube
     *
     * Result:
     *   /World/B/Geom/Cube
     *
     * @param paths Input prim or property paths.
     * @param oldPath Original hierarchy root.
     * @param newPath New hierarchy root.
     *
     * @return Remapped list of paths.
     */
    QList<SdfPath> remapAffectedPaths(const QList<SdfPath>& paths, const SdfPath& oldPath, const SdfPath& newPath);
}  // namespace path

namespace name {
    /**
     * @brief Creates a valid and unique USD child name for a parent prim.
     *
     * The input text is first converted into a valid USD identifier candidate
     * by replacing invalid characters with underscores. If the identifier
     * starts with a digit, an underscore is prepended. If the sanitized result
     * is empty, consists only of underscores, or is still not a valid USD
     * identifier, the fallback name "Prim" is used.
     *
     * The resulting base name is then checked against existing children under
     * @p parentPath. If a child with the same name already exists, a numeric
     * suffix is appended using the pattern "_1", "_2", and so on until a
     * unique name is found.
     *
     * If @p stage is null, @p parentPath is not an absolute path, or the parent
     * prim does not exist, the sanitized base name is returned without sibling
     * uniqueness checking.
     *
     * @param stage Stage used to inspect existing children.
     * @param parentPath Absolute path to the parent prim.
     * @param inputName Source name to sanitize and uniquify.
     *
     * @return Valid and unique USD child name.
     */
    QString makeSafeName(const UsdStageRefPtr& stage, const SdfPath& parentPath, const QString& inputName);

    /**
     * @brief Creates a valid and unique USD child name for a parent prim while
     *        ignoring one existing child path.
     *
     * This overload behaves like the standard makeSafeName() variant, but
     * excludes @p ignorePath from the sibling uniqueness check. This is useful
     * for rename operations where the current prim already exists under
     * @p parentPath and should not be treated as a naming conflict.
     *
     * The input text is first converted into a valid USD identifier candidate
     * by replacing invalid characters with underscores. If the identifier
     * starts with a digit, an underscore is prepended. If the sanitized result
     * is empty, consists only of underscores, or is still not a valid USD
     * identifier, the fallback name "Prim" is used.
     *
     * The resulting base name is then checked against existing children under
     * @p parentPath, excluding @p ignorePath. If a conflicting child already
     * exists, a numeric suffix is appended using the pattern "_1", "_2", and
     * so on until a unique name is found.
     *
     * If @p stage is null, @p parentPath is not an absolute path, or the parent
     * prim does not exist, the sanitized base name is returned without sibling
     * uniqueness checking.
     *
     * @param stage Stage used to inspect existing children.
     * @param parentPath Absolute path to the parent prim.
     * @param inputName Source name to sanitize and uniquify.
     * @param ignorePath Existing child path to ignore during collision checks.
     *
     * @return Valid and unique USD child name.
     */
    QString makeSafeName(const UsdStageRefPtr& stage, const SdfPath& parentPath, const QString& inputName,
                         const SdfPath& ignorePath);
}  // namespace name

namespace stage {
    /**
     * @brief Finds variant sets for the specified prim paths.
     *
     * Traverses the provided paths and collects variant sets available
     * on the corresponding prims. The result maps each variant set name
     * to the list of variant names found across the inspected prims.
     *
     * If @p recursive is enabled, child prims are also traversed and
     * included in the result.
     *
     * @param stage USD stage to query.
     * @param paths Prim paths to inspect.
     * @param recursive If true, traverse child prims recursively.
     *
     * @return Map of variant set names to variant names.
     */
    QMap<QString, QList<QString>> findVariantSets(UsdStageRefPtr stage, const QList<SdfPath>& paths,
                                                  bool recursive = false);

    /**
     * @brief Collects payload prim paths at the specified prim paths.
     *
     * Checks each path in @p paths and returns those whose corresponding
     * prim directly authors or contains a payload.
     *
     * @param stage USD stage to query.
     * @param paths Prim paths to test.
     *
     * @return List of payload prim paths found exactly at @p paths.
     */
    QList<SdfPath> payloadPaths(UsdStageRefPtr stage, const QList<SdfPath>& paths);

    /**
     * @brief Collects nearest payload ancestor prim paths for the specified prim paths.
     *
     * For each path in @p paths, walks upward through its parent hierarchy until
     * the nearest prim that directly authors or contains a payload is found.
     * At most one payload ancestor is returned per input path.
     *
     * @param stage USD stage to query.
     * @param paths Prim paths to resolve upward from.
     *
     * @return List of nearest payload ancestor prim paths.
     */
    QList<SdfPath> ancestorPayloadPaths(UsdStageRefPtr stage, const QList<SdfPath>& paths);

    /**
     * @brief Collects payload prim paths at and below the specified prim paths.
     *
     * Traverses each path in @p paths and recursively visits its descendants,
     * collecting prims that directly author or contain payloads.
     *
     * @param stage USD stage to query.
     * @param paths Root prim paths to traverse downward from.
     *
     * @return List of payload prim paths found in the descendant hierarchies.
     */
    QList<SdfPath> descendantsPayloadPaths(UsdStageRefPtr stage, const QList<SdfPath>& paths);

    /**
     * @brief Resolves selection paths to payload prim paths for load/unload operations.
     *
     * For each path in @p paths, this function first checks whether the path
     * itself, or any of its ancestors, directly authors or contains a payload.
     * If so, the nearest such payload ancestor is used as the resolved payload
     * path for that selection.
     *
     * If no payload ancestor is found for a selection path, the path is treated
     * as a container root and payload prims are collected recursively at and
     * below that path.
     *
     * The final result is deduplicated and reduced to top-most payload paths,
     * so overlapping selections do not produce redundant nested payload entries.
     *
     * This helper is intended for selection-driven load and unload behavior,
     * where a selection may refer either to a prim inside a payload hierarchy
     * or to a non-payload container that contains payloads below it.
     *
     * @param stage USD stage to query.
     * @param paths Selected prim paths to resolve.
     *
     * @return List of resolved payload prim paths for the selection.
     */
    QList<SdfPath> selectionPayloadPaths(UsdStageRefPtr stage, const QList<SdfPath>& paths);

    /**
     * @brief Computes the bounding box for the specified prim paths.
     *
     * Evaluates the world-space bounding box of the given prims
     * within the USD stage. The result combines the bounds of
     * all provided prims into a single @c GfBBox3d.
     *
     * @param stage USD stage containing the prims.
     * @param paths Prim paths to include in the bounding box calculation.
     *
     * @return Combined bounding box of the prims.
     */
    GfBBox3d boundingBox(UsdStageRefPtr stage, const QList<SdfPath>& paths);

    /**
     * @brief Checks whether a prim has authored specifications.
     *
     * Determines if the prim at the given path has any authored
     * @c SdfPrimSpec entries across the composed layer stack.
     *
     * @param stage USD stage containing the prim.
     * @param path  Prim path to evaluate.
     *
     * @return True if the prim has authored specs, false otherwise.
     */
    bool isAuthored(UsdStageRefPtr stage, const SdfPath& path);

    /**
     * @brief Checks whether a prim exists in the current edit target.
     *
     * Evaluates if the prim has a specification in the active edit
     * target layer of the stage. This indicates whether edits will
     * be authored directly in the current layer.
     *
     * @param stage USD stage containing the prim.
     * @param path  Prim path to evaluate.
     *
     * @return True if the prim exists in the edit target layer, false otherwise.
     */
    bool isEditTarget(UsdStageRefPtr stage, const SdfPath& path);

    /**
     * @brief Checks whether a prim is strongest-editable in the current edit target.
     *
     * A prim is considered strongest-editable if its strongest composed prim spec
     * resides in the current edit target layer. This is useful for destructive
     * operations that should only act on prims whose strongest opinion is authored
     * in the editable layer.
     *
     * @param stage USD stage containing the prim.
     * @param path Prim path to evaluate.
     *
     * @return True if the strongest prim spec belongs to the current edit target.
     */
    bool isStrongestEditable(UsdStageRefPtr stage, const SdfPath& path);

    /**
     * @brief Filters a list of paths to those that are strongest-editable.
     *
     * Property paths are normalized to their owning prim paths before testing.
     * Only prims whose strongest composed prim spec belongs to the current edit
     * target layer are included in the result.
     *
     * @param stage USD stage to query.
     * @param paths Prim or property paths to evaluate.
     *
     * @return Filtered list of strongest-editable prim paths.
     */
    QList<SdfPath> filterStrongestEditablePaths(UsdStageRefPtr stage, const QList<SdfPath>& paths);

    /**
     * @brief Checks whether payloads at and below a prim path are fully loaded.
     *
     * Traverses the prim at the given path and its descendant hierarchy,
     * evaluating payload prims found at or below that path.
     *
     * The function returns true only if one or more payload prims are found
     * and all such payload prims are currently loaded.
     *
     * This is useful when a selected prim may either be a payload prim itself
     * or a non-payload container that contains payload prims below it.
     *
     * @param stage USD stage containing the prim hierarchy.
     * @param path  Root prim path to evaluate.
     *
     * @return True if one or more payload prims are found at or below the path
     *         and all of them are loaded, false otherwise.
     */
    bool isLoaded(UsdStageRefPtr stage, const SdfPath& path);

    /**
     * @brief Checks whether a prim has a payload.
     *
     * Determines if the prim at the given path has an authored
     * payload, either directly or through its composed prim stack.
     *
     * @param stage USD stage containing the prim.
     * @param path  Prim path to evaluate.
     *
     * @return True if the prim has a payload, false otherwise.
     */
    bool isPayload(UsdStageRefPtr stage, const SdfPath& path);

    /**
     * @brief Checks whether a prim is part of a payload hierarchy.
     *
     * Evaluates whether the prim or any of its ancestors has a payload.
     * This effectively determines if the prim is located under a payload
     * composition boundary.
     *
     * @param stage USD stage containing the prim.
     * @param path  Prim path to evaluate.
     *
     * @return True if the prim is under a payload hierarchy, false otherwise.
     */
    bool isPayloadHierarchy(UsdStageRefPtr stage, const SdfPath& path);

    /**
     * @brief Checks whether a prim is editable.
     *
     * Determines if the prim can be modified under the current editing
     * policy. A prim is considered editable if it:
     * - Is active and valid.
     * - Is not under a payload hierarchy.
     * - Has authored specifications.
     * - Exists in the current edit target layer.
     *
     * @param stage USD stage containing the prim.
     * @param path  Prim path to evaluate.
     *
     * @return True if the prim is editable, false otherwise.
     */
    bool isEditable(UsdStageRefPtr stage, const SdfPath& path);

    /**
     * @brief Returns the visibility state of a prim.
     *
     * Queries the authored visibility of the specified prim on the stage.
     * If no visibility is authored, the prim is considered visible (inherited).
     *
     * This function evaluates only the prim’s own visibility attribute and
     * does not account for inherited visibility from parent prims.
     *
     * @param stage USD stage containing the prim.
     * @param path Prim path to query.
     *
     * @return True if the prim is visible, false if it is explicitly invisible.
     */
    bool isVisible(UsdStageRefPtr stage, const SdfPath& path);

    /**
     * @brief Sets visibility for the specified prim paths.
     *
     * Updates the visibility state of the given prims on the stage.
     * When @p recursive is enabled, the visibility state is also
     * applied to all descendant prims.
     *
     * @param stage USD stage containing the prims.
     * @param paths Prim paths to update.
     * @param visible Visibility state to apply.
     * @param recursive If true, apply visibility recursively.
     */
    void setVisible(UsdStageRefPtr stage, const QList<SdfPath>& paths, bool visible, bool recursive = false);

    /**
     * @brief Captures the current child order for a parent prim.
     *
     * Collects the ordered child names of the specified parent prim into
     * @p out. This is useful when preserving hierarchy ordering around
     * namespace edits such as delete, rename, reparent, or create.
     *
     * @param stage USD stage containing the parent prim.
     * @param parentPath Parent prim path to inspect.
     * @param out Output child-name order.
     *
     * @return True if the parent exists and one or more children were captured.
     */
    bool captureChildOrder(UsdStageRefPtr stage, const SdfPath& parentPath, TfTokenVector& out);

    /**
     * @brief Restores an authored child order for a parent prim.
     *
     * Ensures the necessary parent specs exist in the current edit target layer
     * and then authors the provided child reorder on the specified parent prim.
     *
     * @param stage USD stage containing the parent prim.
     * @param parentPath Parent prim path to update.
     * @param childOrder Ordered child names to author.
     */
    void restoreChildOrder(UsdStageRefPtr stage, const SdfPath& parentPath, const TfTokenVector& childOrder);

    /**
     * @brief Removes a prim specification from a layer.
     *
     * Applies a namespace edit that deletes the prim spec at @p specPath from
     * the provided layer. This operates on the layer directly rather than on
     * composed stage state.
     *
     * @param layer Layer containing the prim spec.
     * @param specPath Prim spec path to remove.
     *
     * @return True if the prim spec was removed successfully.
     */
    bool removePrimSpec(const SdfLayerHandle& layer, const SdfPath& specPath);

    /**
     * @brief Remaps stage load rules from one hierarchy path to another.
     *
     * For every load rule whose path is equal to, or a descendant of,
     * @p oldPath, a corresponding rule is created under @p newPath while
     * preserving the relative suffix and rule policy.
     *
     * Rules outside the moved hierarchy are copied unchanged.
     *
     * @param rules Existing stage load rules.
     * @param oldPath Original hierarchy root.
     * @param newPath New hierarchy root.
     *
     * @return Remapped stage load rules.
     */
    UsdStageLoadRules remapLoadRules(const UsdStageLoadRules& rules, const SdfPath& oldPath, const SdfPath& newPath);

}  // namespace stage
}  // namespace stageviz
