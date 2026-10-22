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

namespace layer {

    /**
     * @brief Validates that a prim is authored in a specific layer.
     *
     * Property paths are normalized to their owning prim path. When
     * @p requireStrongest is true, the strongest composed prim spec must also
     * belong to @p layer.
     */
    bool validatePrim(const UsdStageRefPtr& stage, const SdfLayerHandle& layer, const SdfPath& path, QString& error,
                      bool requireStrongest = true);

    /**
     * @brief Validates a destination parent against a specific layer.
     *
     * The absolute root is accepted when both stage and layer are valid.
     * Other paths use the same ownership/strongest-opinion policy as
     * validatePrim().
     */
    bool validateParent(const UsdStageRefPtr& stage, const SdfLayerHandle& layer, const SdfPath& parentPath,
                        QString& error, bool requireStrongest = true);

}  // namespace layer

namespace identifier {

    /**
     * @brief Creates a valid USD identifier candidate from user-provided text.
     *
     * Invalid identifier characters are replaced with underscores. If the
     * resulting identifier starts with a digit, an underscore is prepended. If
     * the sanitized result is empty, consists only of underscores, or is still
     * not a valid USD identifier, the fallback name "Prim" is used.
     *
     * The resulting base name is then checked against existing children under
     * @p parentPath. If a child with the same name already exists, a numeric
     * suffix is appended using the pattern "_1", "_2", and so on until a
     * unique child name is found.
     *
     * If @p stage is null, @p parentPath is not an absolute path, or the parent
     * prim does not exist, the sanitized base name is returned without sibling
     * uniqueness checking.
     *
     * @param stage Stage used to inspect existing sibling names.
     * @param parentPath Absolute path to the parent prim.
     * @param inputName Source name to sanitize and uniquify.
     *
     * @return Valid USD child identifier, unique below @p parentPath when possible.
     */
    QString makeSafeIdentifier(const UsdStageRefPtr& stage, const SdfPath& parentPath, const QString& inputName);

    /**
     * @brief Creates a valid USD identifier candidate while ignoring one child.
     *
     * This overload behaves like makeSafeIdentifier(), but excludes @p ignorePath
     * from the sibling uniqueness check. This is useful for rename operations
     * where the prim being renamed already exists below @p parentPath and should
     * not be treated as a collision with itself.
     *
     * Invalid identifier characters are replaced with underscores. If the
     * resulting identifier starts with a digit, an underscore is prepended. If
     * the sanitized result is empty, consists only of underscores, or is still
     * not a valid USD identifier, the fallback name "Prim" is used.
     *
     * The resulting base name is then checked against existing children under
     * @p parentPath, excluding @p ignorePath. If a conflicting child already
     * exists, a numeric suffix is appended using the pattern "_1", "_2", and
     * so on until a unique child name is found.
     *
     * If @p stage is null, @p parentPath is not an absolute path, or the parent
     * prim does not exist, the sanitized base name is returned without sibling
     * uniqueness checking.
     *
     * @param stage Stage used to inspect existing sibling names.
     * @param parentPath Absolute path to the parent prim.
     * @param inputName Source name to sanitize and uniquify.
     * @param ignorePath Existing child path to ignore during collision checks.
     *
     * @return Valid USD child identifier, unique below @p parentPath when possible.
     */
    QString makeSafeIdentifier(const UsdStageRefPtr& stage, const SdfPath& parentPath, const QString& inputName,
                               const SdfPath& ignorePath);

}  // namespace identifier

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
     * @brief Removes duplicate prim paths while preserving order.
     *
     * Keeps the first occurrence of each non-empty path and skips
     * subsequent duplicates. Empty paths are ignored.
     *
     * Example:
     *   /A
     *   /B
     *   /A
     *
     * Result:
     *   /A
     *   /B
     *
     * @param paths Prim paths to filter.
     *
     * @return List of unique, non-empty prim paths.
     */
    QList<SdfPath> uniquePaths(const QList<SdfPath>& paths);

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
     * @brief Checks whether a path is affected by the current isolation mask.
     *
     * Property paths are normalized to their owning prim paths before
     * comparison. The function returns true if the path is equal to, or a
     * descendant of, any path in the isolation mask. An empty mask matches all
     * paths.
     *
     * @param mask Current isolation mask.
     * @param path Prim or property path to test.
     *
     * @return True if the path is affected by the isolation mask.
     */
    bool isWithinRoots(const QList<SdfPath>& mask, const SdfPath& path);

    /**
     * @brief Checks whether a path is affected by the current selection.
     *
     * Property paths are normalized to their owning prim paths before
     * comparison. The function returns true if the path is equal to, or a
     * descendant of, any selected path.
     *
     * @param selection Current selection.
     * @param path Prim or property path to test.
     *
     * @return True if the path is affected by the current selection.
     */
    bool isCoveredByRoots(const QList<SdfPath>& selection, const SdfPath& path);

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

    /**
     * @brief Appends a path if it is not already present in the list.
     */
    void appendUnique(QList<SdfPath>& paths, const SdfPath& path);

    /**
     * @brief Returns the order with all occurrences of the supplied tokens removed.
     */
    TfTokenVector removeTokens(TfTokenVector order, const TfTokenVector& tokens);
    /**
     * @brief Inserts tokens into an order at the requested index.
     */
    TfTokenVector insertTokens(TfTokenVector order, const TfTokenVector& tokens, int index);
}  // namespace path

namespace stage {
    /**
     * @brief Builds an asset path suitable for authoring into a destination layer.
     *
     * Converts @p filename to an absolute path first. When the destination
     * layer is file-backed, the returned asset path is made relative to the
     * destination layer directory. Anonymous or pathless destination layers
     * receive the normalized absolute path instead.
     *
     * Native path separators are converted to forward slashes before the
     * result is returned for USD composition arcs.
     *
     * @param destinationLayer Layer that will author the asset path.
     * @param filename Source asset filename.
     *
     * @return Normalized asset path suitable for a reference, payload, or sublayer.
     */
    std::string compositionAssetPath(const SdfLayerHandle& destinationLayer, const QString& filename);
}  // namespace stage

}  // namespace stageviz

#include "payloadutils.h"
#include "primutils.h"
