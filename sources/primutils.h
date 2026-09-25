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

namespace snapshot {
    /**
     * @struct PrimState
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
     * The prim must exist, be UsdGeomXformable, not be an instance proxy,
     * and the stage must have a valid current edit target. Transform edits are property overrides,
     * so the prim itself does not need to be authored strongest in that layer.
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
     * The transform is authored in the stage's current edit target. When the composed
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
     * needed to avoid sibling collisions, and authors the resulting Xform in the stage's current edit target.
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

    /**
     * @brief Restores child-name ordering for each parent path in the supplied map.
     */
    void restoreChildOrders(UsdStageRefPtr stage, const QHash<SdfPath, TfTokenVector>& orders);

    /**
     * @brief Filters paths whose strongest prim spec belongs to the supplied layer.
     *
     * Property paths are normalized to prim paths before testing.
     *
     * @param stage USD stage to query.
     * @param paths Prim or property paths to evaluate.
     *
     * @return Prim paths whose strongest spec belongs to the supplied layer.
     */
    QList<SdfPath> filterStrongestLayerPaths(UsdStageRefPtr stage, const SdfLayerHandle& layer,
                                             const QList<SdfPath>& paths);

    using VariantTargets = QMap<QString, QMap<QString, QList<SdfPath>>>;

    /**
     * @brief Collects variant sets, values, and owning prim paths relevant to selected branches.
     *
     * Each result entry maps variant set name -> variant value -> prim paths that expose that
     * value. Property paths are normalized to their owning prim. The selected prims and their
     * ancestor chains are always inspected. When @p recursive is true, descendants below each
     * selected root are also included without traversing sibling branches. When @p paths is empty,
     * the pseudo-root is used so recursive queries can inspect the whole stage.
     *
     * @param stage USD stage to query.
     * @param paths Prim or property paths that define the selected branches.
     * @param recursive If true, include descendants below each selected root.
     *
     * @return Variant targets grouped by set name and variant value.
     */
    VariantTargets variantTargets(UsdStageRefPtr stage, const QList<SdfPath>& paths = {}, bool recursive = true);

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
     * A prim is editable when it is valid, active, authored in the current edit target,
     * and not inside a payload hierarchy.
     *
     * @param stage USD stage containing the prim.
     * @param path Prim path to evaluate.
     *
     * @return True if the prim is editable.
     */
    bool isEditable(UsdStageRefPtr stage, const SdfPath& path);

    /**
     * @brief Checks whether a prim is authored in a specific layer.
     *
     * Tests whether @p layer contains a prim spec at @p path.
     *
     * @param stage USD stage containing the prim.
     * @param path Prim path to evaluate.
     *
     * @return True if the supplied layer contains the prim spec.
     */
    bool isAuthoredInLayer(UsdStageRefPtr stage, const SdfLayerHandle& layer, const SdfPath& path);

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
     * @brief Checks whether a prim's strongest composed spec belongs to a layer.
     *
     * The function compares the strongest prim-stack entry against @p layer.
     *
     * @param stage USD stage containing the prim.
     * @param path Prim path to evaluate.
     *
     * @return True if the strongest spec belongs to the supplied layer.
     */
    bool isStrongestInLayer(UsdStageRefPtr stage, const SdfLayerHandle& layer, const SdfPath& path);

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
     * @brief Restores an authored child order for a parent prim.
     *
     * Ensures parent specs exist in the current edit target and authors the provided
     * child reorder on the parent prim.
     *
     * @param stage USD stage containing the parent.
     * @param parentPath Parent prim path.
     * @param childOrder Ordered child names to author.
     */
    void restoreChildOrder(UsdStageRefPtr stage, const SdfPath& parentPath, const TfTokenVector& childOrder);

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
