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

namespace editlayer {

    /**
     * @brief Result container for edit-layer path validation.
     *
     * Stores the opened edit layer together with the normalized prim path
     * that was validated. Property paths are normalized to their owning prim
     * path before validation.
     */
    struct Validation {
        SdfLayerHandle layer;
        SdfPath primPath;
    };

    /**
     * @brief Returns the stage's opened edit layer.
     *
     * This function defines the current Stageviz structural editing scope.
     * Namespace and structural edits are restricted to the stage's opened
     * edit layer and do not target sublayers, session layers, referenced
     * layers, payload layers, or other composed layers.
     *
     * @param stage Stage whose edit layer should be returned.
     * @param error Receives a descriptive failure reason.
     *
     * @return Opened edit layer, or an invalid handle on failure.
     */
    SdfLayerHandle opened(const UsdStageRefPtr& stage, QString& error);

    /**
     * @brief Validates that a prim may be edited in the opened edit layer.
     *
     * Property paths are normalized to their owning prim path. Validation
     * requires that the stage and prim exist and that the opened edit layer
     * contains a prim specification at the normalized path.
     *
     * When @p requireStrongest is true, the strongest composed prim
     * specification must also belong to the opened edit layer. This prevents
     * Stageviz from structurally editing a weaker edit-layer opinion while a
     * stronger opinion from another layer controls the composed prim.
     *
     * @param stage Stage containing the prim.
     * @param path Prim or property path to validate.
     * @param error Receives a descriptive failure reason.
     * @param requireStrongest Require the opened edit layer to provide the
     * strongest prim specification.
     *
     * @return True when the prim is valid for edit-layer editing.
     */
    bool validatePrim(const UsdStageRefPtr& stage, const SdfPath& path, QString& error, bool requireStrongest = true);

    /**
     * @brief Validates a destination parent for edit-layer structural edits.
     *
     * The absolute root path is always accepted as a valid parent when the
     * stage has an opened edit layer. Other parent paths are validated using
     * the same edit-layer ownership and strongest-opinion policy as
     * validatePrim().
     *
     * This function does not validate operation-specific hierarchy rules such
     * as self-parenting, destination collisions, or composition-arc
     * boundaries. Those checks remain the responsibility of the namespace
     * edit operation.
     *
     * @param stage Stage containing the destination parent.
     * @param parentPath Parent prim path or the absolute root path.
     * @param error Receives a descriptive failure reason.
     * @param requireStrongest Require the opened edit layer to provide the
     * strongest parent prim specification.
     *
     * @return True when the parent is valid for edit-layer editing.
     */
    bool validateParent(const UsdStageRefPtr& stage, const SdfPath& parentPath, QString& error,
                        bool requireStrongest = true);

}  // namespace editlayer

// Transitional compatibility alias for callers that still use the former
// rootlayer namespace. New code should use editlayer directly.
namespace rootlayer = editlayer;

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

    void appendUnique(QList<SdfPath>& paths, const SdfPath& path);

    TfTokenVector removeTokens(TfTokenVector order, const TfTokenVector& tokens);
    TfTokenVector insertTokens(TfTokenVector order, const TfTokenVector& tokens, int index);
}  // namespace path

namespace payload {
    /**
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
 * @brief Collects payload variant targets below selected paths.
 *
 * Traverses the selected roots, finds payload prims, and groups only
 * variant sets authored on those payload prims. Duplicate payload prims are
 * ignored, and duplicate menu names are naturally merged by the map.
 *
 * @param stage USD stage to query.
 * @param paths Selected prim paths to scan recursively.
 *
 * @return Map of variant set name to variant value to compatible payload paths.
 */
    PayloadVariantTargets payloadVariantTargets(UsdStageRefPtr stage, const QList<SdfPath>& paths);

    /**
 * @brief Finds unloaded payloads spatially neighboring selected payloads.
 *
 * Resolves the nearest payload ancestor for each input path and evaluates
 * each source payload's extentsHint in world space.
 *
 * The source bounds are combined into a single world-space bounding box so
 * multiple selected payloads are treated as one spatial region. Candidate
 * payloads must be currently unloaded and provide a usable extentsHint.
 *
 * Candidates are ranked by the minimum world-space box-to-box distance from
 * the combined selection bounds. Distances are normalized by the larger of
 * the selection and candidate dimensions so the search remains independent
 * of stage units and absolute model scale.
 *
 * A distance-gap heuristic keeps the nearest spatial cluster rather than
 * relying on a fixed expanded bounding-box intersection. A normalized safety
 * limit prevents repeated neighbor loads from progressively reaching remote
 * payloads.
 *
 * @param stage Stage containing the payloads.
 * @param inputPaths Payload paths or descendant paths inside payloads.
 *
 * @return Unloaded payload paths belonging to the nearest spatial cluster.
 */
    QList<SdfPath> neighboringPaths(UsdStageRefPtr stage, const QList<SdfPath>& inputPaths);

    /**
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

namespace snapshot {
    /**
     * @brief Captured prim spec state used to restore a deleted prim.
     */
    struct PrimState {
        SdfPath stagePath;
        SdfPath specPath;
        SdfLayerRefPtr snapshotLayer;
    };

    using PrimSnapshot = QVector<PrimState>;

    /**
     * @brief Capture the strongest available prim spec for a stage path into an anonymous layer.
     */
    bool capturePrimToLayer(UsdStageRefPtr stage, const SdfPath& stagePath, PrimState& out);

    /**
     * @brief Restore a captured prim spec into a destination layer at its original stage path.
     */
    void restorePrimFromSnapshotLayer(const SdfLayerHandle& dstLayer, const PrimState& state);

    /**
     * @brief Sort captured prims parent-before-child for safe restoration.
     */
    void sortByHierarchy(PrimSnapshot& snapshot);

}  // namespace snapshot

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

    /**
 * @brief Collects nearest payload ancestor paths for the specified prim paths.
 *
 * Walks upward from each input path until it finds the nearest prim that
 * directly authors or contains a payload.
 *
 * @param stage USD stage to query.
 * @param paths Prim paths to resolve upward from.
 *
 * @return List of nearest payload ancestor paths.
 */
    QList<SdfPath> ancestorPayloadPaths(UsdStageRefPtr stage, const QList<SdfPath>& paths);

    /**
     * @brief Collects the top-most payload ancestor paths for specified prim paths.
     *
     * Walks from each input prim to the pseudo-root and keeps the last payload
     * encountered. Property paths are normalized to their owning prim path.
     * Duplicate and descendant results are removed.
     *
     * @param stage USD stage to query.
     * @param paths Prim or property paths to resolve upward from.
     *
     * @return List of top-most payload ancestor paths.
     */
    QList<SdfPath> topMostPayloadPaths(UsdStageRefPtr stage, const QList<SdfPath>& paths);

    /**
 * @brief Computes the combined world-space bounding box for prim paths.
 *
 * Evaluates each imageable prim and combines its world bound into one
 * resulting bounding box.
 *
 * @param stage USD stage containing the prims.
 * @param paths Prim paths to include.
 *
 * @return Combined bounding box.
 */
    GfBBox3d boundingBox(UsdStageRefPtr stage, const QList<SdfPath>& paths);

    /**
     * @brief Returns whether a prim can be edited as a transform.
     *
     * The prim must exist, be UsdGeomXformable, and satisfy Stageviz's
     * strongest-edit-layer editing policy.
     */
    bool isTransformEditable(UsdStageRefPtr stage, const SdfPath& path);

    /**
     * @brief Reads the composed world transform for a prim.
     *
     * @param stage Stage containing the prim.
     * @param path Prim path to query.
     * @param matrix Receives the world transform.
     * @param error Receives a failure reason.
     */
    bool worldTransform(UsdStageRefPtr stage, const SdfPath& path, GfMatrix4d& matrix, QString& error);

    /**
     * @brief Resolves the manipulation pivot for an xformable prim in world space.
     *
     * Uses the standard paired USD pivot pattern
     * xformOp:translate:pivot / !invert!xformOp:translate:pivot when it can be
     * interpreted unambiguously. Arbitrary transform stacks fall back to the
     * prim's evaluated world-space origin.
     *
     * @param stage Stage containing the prim.
     * @param path Prim path to evaluate.
     * @param pivot Receives the resolved world-space manipulation pivot.
     * @param error Receives a failure reason.
     *
     * @return True when a valid manipulation point was resolved.
     */
    bool worldPivot(UsdStageRefPtr stage, const SdfPath& path, GfVec3d& pivot, QString& error);

    /**
     * @brief Authors a world transform for an xformable prim.
     *
     * The transform is authored in the active edit layer. When the composed
     * transform contains the standard paired USD pivot pattern
     * xformOp:translate:pivot / !invert!xformOp:translate:pivot, the pivot is
     * retained and the matrix operation is recomputed so the requested world
     * transform is preserved. This prevents interactive transforms from
     * orphaning an existing pivot attribute by collapsing xformOpOrder to a
     * matrix-only stack.
     *
     * When no standard paired pivot exists, the transform is authored as a
     * canonical matrix xform.
     *
     * @param stage Stage containing the prim.
     * @param path Prim path to edit.
     * @param matrix Desired world transform.
     * @param error Receives a failure reason.
     */
    bool setWorldTransform(UsdStageRefPtr stage, const SdfPath& path, const GfMatrix4d& matrix, QString& error);

    /**
 * @brief Builds a valid target path for renaming a prim.
 *
 * Sanitizes the input name, validates the rename target, avoids sibling
 * name collisions, and reports validation errors through @p error.
 *
 * @param stage USD stage containing the prim.
 * @param path Existing prim path.
 * @param input Requested new name.
 * @param error Receives a failure reason.
 *
 * @return New prim path, or an empty path on failure.
 */
    SdfPath buildRenamePath(UsdStageRefPtr stage, const SdfPath& path, const QString& input, QString& error);

    /**
 * @brief Builds a valid target path for creating a child prim.
 *
 * Sanitizes the input name, validates the parent path, avoids sibling
 * name collisions, and reports validation errors through @p error.
 *
 * @param stage USD stage containing the parent prim.
 * @param parentPath Parent path where the child should be created.
 * @param input Requested child name.
 * @param error Receives a failure reason.
 *
 * @return New child prim path, or an empty path on failure.
 */
    SdfPath buildChildPath(UsdStageRefPtr stage, const SdfPath& parentPath, const QString& input, QString& error);

    /**
     * @brief Creates a uniquely named UsdGeomXform below a parent path.
     *
     * Sanitizes @p name as a USD identifier, appends a numeric suffix when
     * needed to avoid sibling collisions, and authors the resulting Xform in
     * the stage's current edit target.
     *
     * @param stage USD stage where the Xform should be created.
     * @param name Requested prim name.
     * @param parentPath Parent prim path. Defaults to the stage pseudo-root.
     *
     * @return Path of the created Xform, or an empty path on failure.
     */
    SdfPath buildUniqueXform(UsdStageRefPtr stage, const QString& name,
                             const SdfPath& parentPath = SdfPath::AbsoluteRootPath());

    /**
 * @brief Captures the current child order for a parent prim.
 *
 * Collects the ordered child names of the specified parent prim.
 *
 * @param stage USD stage containing the parent.
 * @param parentPath Parent prim path.
 * @param out Receives the child-name order.
 *
 * @return True if a child order was captured.
 */
    bool captureChildOrder(UsdStageRefPtr stage, const SdfPath& parentPath, TfTokenVector& out);


    void restoreChildOrders(UsdStageRefPtr stage, const QHash<SdfPath, TfTokenVector>& orders);

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
    QList<SdfPath> descendantsPayloadPaths(UsdStageRefPtr stage, const QList<SdfPath>& paths);

    /**
 * @brief Filters paths to those strongest-editable in the current edit target.
 *
 * Property paths are normalized to prim paths before testing.
 *
 * @param stage USD stage to query.
 * @param paths Prim or property paths to evaluate.
 *
 * @return Strongest-editable prim paths.
 */
    QList<SdfPath> filterStrongestEditablePaths(UsdStageRefPtr stage, const QList<SdfPath>& paths);

    /**
 * @brief Finds variant sets for the specified prim paths.
 *
 * Collects variant set names and available variant values from the provided
 * prim paths, optionally including descendants.
 *
 * @param stage USD stage to query.
 * @param paths Prim paths to inspect.
 * @param recursive If true, include descendants.
 *
 * @return Map of variant set names to variant names.
 */
    QMap<QString, QList<QString>> findVariantSets(UsdStageRefPtr stage, const QList<SdfPath>& paths,
                                                  bool recursive = false);

    /**
 * @brief Checks whether a prim authors composition arcs.
 *
 * Tests for payloads, references, inherits, specializes, or variant sets on
 * the given prim. This is used to avoid namespace edits across composed
 * boundaries.
 *
 * @param prim Prim to inspect.
 *
 * @return True if the prim has composition arcs.
 */
    bool hasCompositionArc(const UsdPrim& prim);

    /**
 * @brief Inserts a child name into an existing child order.
 *
 * Removes existing occurrences of @p name, clamps @p index to the valid range,
 * and inserts the name at the requested position.
 *
 * @param order Existing child order.
 * @param name Child name to insert.
 * @param index Target insert index, or a negative value to append.
 *
 * @return Updated child order.
 */
    TfTokenVector insertChildOrderToken(const TfTokenVector& order, const TfToken& name, int index);

    /**
 * @brief Checks whether a prim has authored specifications.
 *
 * Tests whether the composed prim has authored prim specs in its stack.
 *
 * @param stage USD stage containing the prim.
 * @param path Prim path to evaluate.
 *
 * @return True if authored specs exist.
 */
    bool isAuthored(UsdStageRefPtr stage, const SdfPath& path);

    /**
 * @brief Checks whether a prim is editable under the current policy.
 *
 * A prim is editable when it is valid, active, authored in the edit target,
 * and not inside a payload hierarchy.
 *
 * @param stage USD stage containing the prim.
 * @param path Prim path to evaluate.
 *
 * @return True if the prim is editable.
 */
    bool isEditable(UsdStageRefPtr stage, const SdfPath& path);

    /**
 * @brief Checks whether a prim exists in the current edit target.
 *
 * Tests whether the active edit target layer has a prim spec at @p path.
 *
 * @param stage USD stage containing the prim.
 * @param path Prim path to evaluate.
 *
 * @return True if the edit target contains the prim spec.
 */
    bool isEditTarget(UsdStageRefPtr stage, const SdfPath& path);

    /**
 * @brief Checks whether a path is inside a composed hierarchy.
 *
 * Walks from @p path upward to the pseudo-root and returns true if the prim or
 * any ancestor authors composition arcs.
 *
 * @param stage USD stage containing the path.
 * @param path Prim path to evaluate.
 *
 * @return True if the path is inside a composed hierarchy.
 */
    bool isInsideCompositionArc(UsdStageRefPtr stage, const SdfPath& path);

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
 * @brief Checks whether a prim has a payload.
 *
 * Tests both composed prim state and authored payload list opinions.
 *
 * @param stage USD stage containing the prim.
 * @param path Prim path to evaluate.
 *
 * @return True if the prim has a payload.
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
 * @brief Checks whether a prim is strongest-editable in the edit target.
 *
 * A prim is strongest-editable when its strongest composed prim spec is in
 * the current edit target layer.
 *
 * @param stage USD stage containing the prim.
 * @param path Prim path to evaluate.
 *
 * @return True if the strongest spec is in the edit target.
 */
    bool isStrongestEditable(UsdStageRefPtr stage, const SdfPath& path);

    /**
 * @brief Returns the authored visibility state of a prim.
 *
 * Only the prim’s own visibility attribute is queried; inherited parent
 * visibility is not evaluated.
 *
 * @param stage USD stage containing the prim.
 * @param path Prim path to query.
 *
 * @return True unless the prim is explicitly invisible.
 */
    bool isVisible(UsdStageRefPtr stage, const SdfPath& path);

    /**
 * @brief Moves a prim to a new parent using UsdNamespaceEditor.
 *
 * Validates source, destination, hierarchy rules, and composition boundaries
 * before applying the namespace edit.
 *
 * @param stage USD stage containing the prim.
 * @param from Existing prim path.
 * @param toParent Destination parent prim path.
 * @param error Receives a failure reason.
 *
 * @return True if the prim was moved.
 */
    bool movePrim(UsdStageRefPtr stage, const SdfPath& from, const SdfPath& toParent, QString& error);

    /**
 * @brief Moves multiple prims to new paths using a single namespace edit.
 *
 * Validates all source and destination paths, hierarchy rules, destination
 * collisions, and composition boundaries before applying the edits.
 *
 * Stage load rules affected by the moved hierarchies are remapped to their
 * new paths after the namespace edit succeeds.
 *
 * @param stage USD stage containing the prims.
 * @param moves Source and destination path pairs to move.
 * @param error Receives a failure reason.
 *
 * @return True if all prim moves were successfully applied.
 */
    bool movePrims(UsdStageRefPtr stage, const QList<QPair<SdfPath, SdfPath>>& moves, QString& error);

    /**
* @brief Collects leaf prim paths from the stage.
*
* Traverses the stage and returns prims with no traversable children,
* optionally constrained by an isolation mask.
*
* @param stage USD stage to traverse.
* @param mask Optional mask paths.
* @param childMustBeWithinMask If true, only masked children affect leaf status.
*
* @return List of leaf prim paths.
*/
    QList<SdfPath> leafPaths(UsdStageRefPtr stage, const QList<SdfPath>& mask = {}, bool childMustBeWithinMask = true);

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
 * @brief Collects effectively visible renderable prim paths from the stage.
 *
 * Traverses the stage hierarchy and returns paths for visible UsdGeomGprim
 * prims at the default time.
 *
 * Invisible imageable prims cause their entire subtree to be skipped, so
 * geometry beneath an invisible ancestor is excluded. Non-imageable hierarchy
 * prims are traversed but are not included in the returned paths.
 *
 * The resulting paths represent visible renderable geometry rather than
 * visible hierarchy roots, making them suitable for population-based export
 * without implicitly including hidden sibling geometry.
 *
 * @param stage USD stage to traverse.
 *
 * @return List of effectively visible UsdGeomGprim paths.
 */
    QList<SdfPath> visiblePaths(UsdStageRefPtr stage);

    /**
 * @brief Remaps one child name in an existing child order.
 *
 * Replaces the first occurrence of @p oldName with @p newName while
 * preserving the rest of the order.
 *
 * @param order Existing child order.
 * @param oldName Name to replace.
 * @param newName Replacement name.
 *
 * @return Remapped child order.
 */
    TfTokenVector remapChildOrder(const TfTokenVector& order, const TfToken& oldName, const TfToken& newName);

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
 * @brief Removes a child name from an existing child order.
 *
 * Copies @p order while skipping every occurrence of @p name.
 *
 * @param order Existing child order.
 * @param name Child name to remove.
 *
 * @return Child order without the specified name.
 */
    TfTokenVector removeChildOrderToken(const TfTokenVector& order, const TfToken& name);

    /**
 * @brief Removes a prim specification from a layer.
 *
 * Applies a namespace edit that deletes the prim spec at @p specPath from
 * the provided layer.
 *
 * @param layer Layer containing the prim spec.
 * @param specPath Prim spec path to remove.
 *
 * @return True if the prim spec was removed.
 */
    bool removePrimSpec(const SdfLayerHandle& layer, const SdfPath& specPath);

    /**
 * @brief Renames a prim using UsdNamespaceEditor.
 *
 * Validates the source and target paths, checks whether namespace edits can
 * be applied, and performs the rename.
 *
 * @param stage USD stage containing the prim.
 * @param from Existing prim path.
 * @param to Target prim path.
 * @param error Receives a failure reason.
 *
 * @return True if the prim was renamed.
 */
    bool renamePrim(UsdStageRefPtr stage, const SdfPath& from, const SdfPath& to, QString& error);

    /**
 * @brief Restores an authored child order for a parent prim.
 *
 * Ensures parent specs exist in the edit target and authors the provided
 * child reorder on the parent prim.
 *
 * @param stage USD stage containing the parent.
 * @param parentPath Parent prim path.
 * @param childOrder Ordered child names to author.
 */
    void restoreChildOrder(UsdStageRefPtr stage, const SdfPath& parentPath, const TfTokenVector& childOrder);

    /**
 * @brief Resolves selection paths to payload paths for load/unload commands.
 *
 * Uses payload ancestors when available; otherwise searches descendants.
 * The result is deduplicated and reduced to top-most paths.
 *
 * @param stage USD stage to query.
 * @param paths Selected prim paths to resolve.
 *
 * @return Resolved payload paths.
 */
    QList<SdfPath> resolvePayloadPaths(UsdStageRefPtr stage, const QList<SdfPath>& paths);

    /**
 * @brief Sets visibility for the specified prim paths.
 *
 * Authors visible or invisible state on each path and, when requested,
 * applies the same state recursively to descendants.
 *
 * @param stage USD stage containing the prims.
 * @param paths Prim paths to update.
 * @param visible Visibility state to apply.
 * @param recursive If true, apply visibility recursively.
 */
    void setVisible(UsdStageRefPtr stage, const QList<SdfPath>& paths, bool visible, bool recursive = false);

}  // namespace stage
}  // namespace stageviz
