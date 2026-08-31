// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#include "usdedit.h"
#include "qtutils.h"
#include "usdutils.h"
#include <QSet>
#include <pxr/usd/sdf/namespaceEdit.h>
#include <pxr/usd/usd/namespaceEditor.h>
#include <pxr/usd/usdGeom/xform.h>

namespace stageviz {
namespace edit {

    NamespaceEditor::NamespaceEditor(const UsdStageRefPtr& stage)
        : stage_(stage)
    {}

    bool NamespaceEditor::addXform(const SdfPath& path, QString& error)
    {
        changes_.clear();

        if (!stage_) {
            error = "invalid stage";
            return false;
        }

        if (path.IsEmpty() || path == SdfPath::AbsoluteRootPath() || !path.IsPrimPath()) {
            error = "invalid add path";
            return false;
        }

        const SdfPath parentPath = path.GetParentPath();
        QString validationError;
        if (!editlayer::validateParent(stage_, parentPath, validationError)) {
            error = validationError;
            return false;
        }

        if (parentPath != SdfPath::AbsoluteRootPath() && stage::isInsideCompositionArc(stage_, parentPath)) {
            error = "cannot add inside composed prims";
            return false;
        }

        if (stage_->GetPrimAtPath(path)) {
            error = QString("destination already exists: %1").arg(qt::SdfPathToQString(path));
            return false;
        }

        const UsdGeomXform xform = UsdGeomXform::Define(stage_, path);
        if (!xform || !xform.GetPrim()) {
            error = "failed to define Xform";
            return false;
        }

        changes_.append({ Change::Type::Add, SdfPath(), path });
        return true;
    }

    bool NamespaceEditor::removePrim(const SdfPath& path, QString& error) { return removePrims({ path }, error); }

    bool NamespaceEditor::removePrims(const QList<SdfPath>& paths, QString& error)
    {
        changes_.clear();

        if (!stage_) {
            error = "invalid stage";
            return false;
        }

        const QList<SdfPath> roots = path::minimalRootPaths(path::uniquePaths(paths));
        if (roots.isEmpty())
            return true;

        QString editError;
        const SdfLayerHandle editLayer = editlayer::opened(stage_, editError);
        if (!editLayer) {
            error = editError;
            return false;
        }

        SdfBatchNamespaceEdit batch;

        for (const SdfPath& root : roots) {
            if (root.IsEmpty() || root == SdfPath::AbsoluteRootPath() || !root.IsPrimPath()) {
                error = "invalid remove path";
                return false;
            }

            QString validationError;
            if (!editlayer::validatePrim(stage_, root, validationError)) {
                error = validationError;
                return false;
            }

            const SdfPath parentPath = root.GetParentPath();
            if (parentPath != SdfPath::AbsoluteRootPath() && stage::isInsideCompositionArc(stage_, parentPath)) {
                error = "cannot remove inside composed prims";
                return false;
            }

            batch.Add(root, SdfPath::EmptyPath());
        }

        if (!editLayer->CanApply(batch)) {
            error = "Sdf namespace remove batch cannot be applied";
            return false;
        }

        if (!editLayer->Apply(batch)) {
            error = "Sdf namespace remove batch failed";
            return false;
        }

        for (const SdfPath& root : roots)
            changes_.append({ Change::Type::Remove, root, SdfPath() });

        return true;
    }

    bool NamespaceEditor::renamePrim(const SdfPath& from, const SdfPath& to, QString& error)
    {
        changes_.clear();

        if (!stage_) {
            error = "invalid stage";
            return false;
        }

        if (from.IsEmpty() || to.IsEmpty() || !from.IsPrimPath() || !to.IsPrimPath()) {
            error = "invalid rename path";
            return false;
        }

        if (from == to)
            return true;

        const SdfPath parentPath = from.GetParentPath();
        if (parentPath.IsEmpty() || parentPath != to.GetParentPath()) {
            error = "invalid rename target";
            return false;
        }

        QString validationError;
        if (!editlayer::validatePrim(stage_, from, validationError)) {
            error = validationError;
            return false;
        }

        if (!editlayer::validateParent(stage_, parentPath, validationError)) {
            error = validationError;
            return false;
        }

        if (parentPath != SdfPath::AbsoluteRootPath() && stage::isInsideCompositionArc(stage_, parentPath)) {
            error = "cannot rename inside composed prims";
            return false;
        }

        const UsdPrim prim = stage_->GetPrimAtPath(from);
        if (!prim || !prim.IsValid()) {
            error = QString("prim missing: %1").arg(qt::SdfPathToQString(from));
            return false;
        }

        if (stage_->GetPrimAtPath(to)) {
            error = QString("destination already exists: %1").arg(qt::SdfPathToQString(to));
            return false;
        }

        UsdNamespaceEditor::EditOptions options;
        options.allowRelocatesAuthoring = false;
        UsdNamespaceEditor editor(stage_, options);

        if (!editor.RenamePrim(prim, to.GetNameToken())) {
            error = "RenamePrim failed";
            return false;
        }

        std::string whyNot;
        if (!editor.CanApplyEdits(&whyNot)) {
            error = whyNot.empty() ? QStringLiteral("USD namespace rename cannot be applied")
                                   : qt::StringToQString(whyNot);
            return false;
        }

        SdfPathSet oldLoadedPaths;
        SdfPathSet newLoadedPaths;
        captureLoadState({ qMakePair(from, to) }, oldLoadedPaths, newLoadedPaths);

        if (!editor.ApplyEdits()) {
            error = "USD namespace rename failed";
            return false;
        }

        restoreLoadState(oldLoadedPaths, newLoadedPaths);
        changes_.append({ Change::Type::Rename, from, to });
        return true;
    }

    bool NamespaceEditor::reparentPrim(const SdfPath& from, const SdfPath& to, QString& error)
    {
        return reparentPrims({ qMakePair(from, to) }, error);
    }

    bool NamespaceEditor::reparentPrims(const QList<QPair<SdfPath, SdfPath>>& moves, QString& error)
    {
        changes_.clear();

        if (!stage_) {
            error = "invalid stage";
            return false;
        }

        if (moves.isEmpty())
            return true;

        QString editError;
        const SdfLayerHandle editLayer = editlayer::opened(stage_, editError);
        if (!editLayer) {
            error = editError;
            return false;
        }

        QSet<SdfPath> sourcePaths;
        QSet<SdfPath> destinationPaths;
        QList<QPair<SdfPath, SdfPath>> effectiveMoves;
        effectiveMoves.reserve(moves.size());

        for (const auto& move : moves) {
            const SdfPath& from = move.first;
            const SdfPath& to = move.second;

            if (from == to)
                continue;

            if (sourcePaths.contains(from)) {
                error = QString("duplicate move source: %1").arg(qt::SdfPathToQString(from));
                return false;
            }

            if (destinationPaths.contains(to)) {
                error = QString("duplicate move destination: %1").arg(qt::SdfPathToQString(to));
                return false;
            }

            sourcePaths.insert(from);
            destinationPaths.insert(to);
            effectiveMoves.append(move);
        }

        if (effectiveMoves.isEmpty())
            return true;

        for (const auto& move : effectiveMoves) {
            if (!validateMove(move.first, move.second, sourcePaths, error))
                return false;
        }

        SdfPathSet oldLoadedPaths;
        SdfPathSet newLoadedPaths;
        captureLoadState(effectiveMoves, oldLoadedPaths, newLoadedPaths);

        SdfBatchNamespaceEdit batch;
        for (const auto& move : effectiveMoves)
            batch.Add(move.first, move.second, SdfNamespaceEdit::AtEnd);

        if (!editLayer->CanApply(batch)) {
            error = "Sdf namespace move batch cannot be applied";
            return false;
        }

        if (!editLayer->Apply(batch)) {
            error = "Sdf namespace move batch failed";
            return false;
        }

        restoreLoadState(oldLoadedPaths, newLoadedPaths);

        for (const auto& move : effectiveMoves) {
            const Change::Type type = move.first.GetParentPath() == move.second.GetParentPath()
                                          ? Change::Type::Rename
                                          : Change::Type::Reparent;
            changes_.append({ type, move.first, move.second });
        }

        return true;
    }

    const QList<NamespaceEditor::Change>& NamespaceEditor::changes() const { return changes_; }

    bool NamespaceEditor::validateMove(const SdfPath& from, const SdfPath& to, const QSet<SdfPath>& sourcePaths,
                                       QString& error) const
    {
        if (from.IsEmpty() || to.IsEmpty() || from == SdfPath::AbsoluteRootPath() || !from.IsPrimPath()
            || !to.IsPrimPath()) {
            error = "invalid move path";
            return false;
        }

        if (to.HasPrefix(from)) {
            error = "cannot move a prim below itself";
            return false;
        }

        QString validationError;
        if (!editlayer::validatePrim(stage_, from, validationError)) {
            error = validationError;
            return false;
        }

        if (!editlayer::validateParent(stage_, to.GetParentPath(), validationError)) {
            error = validationError;
            return false;
        }

        const SdfPath fromParentPath = from.GetParentPath();
        const SdfPath toParentPath = to.GetParentPath();

        if ((fromParentPath != SdfPath::AbsoluteRootPath() && stage::isInsideCompositionArc(stage_, fromParentPath))
            || (toParentPath != SdfPath::AbsoluteRootPath() && stage::isInsideCompositionArc(stage_, toParentPath))) {
            error = "cannot move into or out of composed prims";
            return false;
        }

        const UsdPrim existing = stage_->GetPrimAtPath(to);
        if (existing && !sourcePaths.contains(to)) {
            error = QString("destination already exists: %1").arg(qt::SdfPathToQString(to));
            return false;
        }

        return true;
    }

    void NamespaceEditor::captureLoadState(const QList<QPair<SdfPath, SdfPath>>& moves, SdfPathSet& oldLoadedPaths,
                                           SdfPathSet& newLoadedPaths) const
    {
        oldLoadedPaths.clear();
        newLoadedPaths.clear();

        if (!stage_ || moves.isEmpty())
            return;

        const SdfPathSet loadSet = stage_->GetLoadSet();

        for (const SdfPath& loadedPath : loadSet) {
            for (const auto& move : moves) {
                const SdfPath& from = move.first;
                const SdfPath& to = move.second;

                if (loadedPath == from || loadedPath.HasPrefix(from)) {
                    const SdfPath relativePath = loadedPath.MakeRelativePath(from);
                    oldLoadedPaths.insert(loadedPath);
                    newLoadedPaths.insert(to.AppendPath(relativePath));
                    break;
                }
            }
        }
    }

    void NamespaceEditor::restoreLoadState(const SdfPathSet& oldLoadedPaths, const SdfPathSet& newLoadedPaths) const
    {
        if (!stage_ || newLoadedPaths.empty())
            return;

        stage_->LoadAndUnload(newLoadedPaths, oldLoadedPaths, UsdLoadWithoutDescendants);
    }

}  // namespace edit
}  // namespace stageviz
