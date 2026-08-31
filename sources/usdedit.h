// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#pragma once

#include "stageviz.h"
#include <QList>
#include <QSet>
#include <QString>
#include <pxr/usd/usd/stage.h>

PXR_NAMESPACE_USING_DIRECTIVE

namespace stageviz {
namespace edit {

    /**
 * @class NamespaceEditor
 * @brief Performs Stageviz structural namespace edits on a USD stage.
 *
 * NamespaceEditor centralizes prim add, remove, rename, and reparent
 * operations used by Stageviz commands. Structural edits are restricted to
 * the active edit layer and preserve the loaded state of affected payloads.
 *
 * Rename currently uses UsdNamespaceEditor so USD emits semantic
 * RenameSource/RenameDestination notices required by StageTree. Reparent and
 * removal use SdfBatchNamespaceEdit so multiple edit-layer operations can be
 * applied as one batch without repeated UsdNamespaceEditor dependency scans.
 */
    class NamespaceEditor {
    public:
        /**
     * @brief Describes a structural namespace change performed by the editor.
     */
        struct Change {
            enum class Type { Add, Remove, Rename, Reparent };

            Type type = Type::Add;
            SdfPath oldPath;
            SdfPath newPath;
        };

        /**
     * @brief Constructs a namespace editor for a stage.
     *
     * @param stage Stage that owns the namespace being edited.
     */
        explicit NamespaceEditor(const UsdStageRefPtr& stage);

        /**
     * @brief Adds an Xform prim at an absolute path.
     *
     * The parent must be valid for Stageviz edit-layer authoring and the
     * destination path must not already exist.
     *
     * @param path Absolute path of the Xform to create.
     * @param error Receives a descriptive failure reason.
     *
     * @return True when the Xform was created.
     */
        bool addXform(const SdfPath& path, QString& error);

        /**
     * @brief Removes one prim hierarchy from the active edit layer.
     *
     * @param path Root prim path to remove.
     * @param error Receives a descriptive failure reason.
     *
     * @return True when the prim was removed.
     */
        bool removePrim(const SdfPath& path, QString& error);

        /**
     * @brief Removes multiple prim hierarchies as one Sdf namespace batch.
     *
     * Descendant input paths covered by another removed root are ignored.
     *
     * @param paths Prim paths to remove.
     * @param error Receives a descriptive failure reason.
     *
     * @return True when the batch was applied.
     */
        bool removePrims(const QList<SdfPath>& paths, QString& error);

        /**
     * @brief Renames a prim while preserving USD semantic rename notices.
     *
     * The source and destination must share the same parent. This operation
     * intentionally uses UsdNamespaceEditor so ObjectsChanged reports
     * RenameSource/RenameDestination and StageTree can preserve item state.
     *
     * @param from Existing prim path.
     * @param to Destination prim path under the same parent.
     * @param error Receives a descriptive failure reason.
     *
     * @return True when the rename was applied.
     */
        bool renamePrim(const SdfPath& from, const SdfPath& to, QString& error);

        /**
     * @brief Reparents one prim to a destination path.
     *
     * @param from Existing prim path.
     * @param to Destination prim path.
     * @param error Receives a descriptive failure reason.
     *
     * @return True when the reparent was applied.
     */
        bool reparentPrim(const SdfPath& from, const SdfPath& to, QString& error);

        /**
     * @brief Reparents multiple prims using one SdfBatchNamespaceEdit.
     *
     * All source and destination paths are validated against the unchanged
     * stage before the batch is applied. Stageviz composition-arc and
     * strongest-edit-layer restrictions remain in force. Loaded payload paths
     * below moved roots are remapped after the namespace edit without replacing
     * the stage's complete load-rule set.
     *
     * @param moves Source-to-destination prim path pairs.
     * @param error Receives a descriptive failure reason.
     *
     * @return True when the batch was applied.
     */
        bool reparentPrims(const QList<QPair<SdfPath, SdfPath>>& moves, QString& error);

        /**
     * @brief Returns the successfully applied structural changes.
     *
     * The returned mappings can be used by Stageviz update code when generic
     * Sdf resync notices do not contain semantic source/destination paths.
     */
        const QList<Change>& changes() const;

    private:
        bool validateMove(const SdfPath& from, const SdfPath& to, const QSet<SdfPath>& sourcePaths,
                          QString& error) const;
        void captureLoadState(const QList<QPair<SdfPath, SdfPath>>& moves, SdfPathSet& oldLoadedPaths,
                              SdfPathSet& newLoadedPaths) const;
        void restoreLoadState(const SdfPathSet& oldLoadedPaths, const SdfPathSet& newLoadedPaths) const;

        UsdStageRefPtr stage_;
        QList<Change> changes_;
    };

}  // namespace edit
}  // namespace stageviz
