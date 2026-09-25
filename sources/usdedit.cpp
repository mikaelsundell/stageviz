// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#include "usdedit.h"
#include "qtutils.h"
#include "usdutils.h"
#include <QSet>
#include <QStringList>
#include <pxr/usd/sdf/namespaceEdit.h>
#include <pxr/usd/usd/editContext.h>
#include <pxr/usd/usd/editTarget.h>
#include <pxr/usd/usd/namespaceEditor.h>
#include <pxr/usd/usdGeom/xform.h>

namespace stageviz {
namespace edit {

    NamespaceEditor::NamespaceEditor(const UsdStageRefPtr& stage, const UsdEditTarget& editTarget)
        : stage_(stage)
        , editTarget_(editTarget)
    {}

    bool NamespaceEditor::addXform(const SdfPath& path, QString& error)
    {
        changes_.clear();

        if (!stage_ || !editTarget_.GetLayer()) {
            error = !stage_ ? "invalid stage" : "invalid edit target";
            return false;
        }

        if (path.IsEmpty() || path == SdfPath::AbsoluteRootPath() || !path.IsPrimPath()) {
            error = "invalid add path";
            return false;
        }

        const SdfPath parentPath = path.GetParentPath();
        QString validationError;
        if (!layer::validateParent(stage_, editTarget_.GetLayer(), parentPath, validationError)) {
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

        UsdEditContext editContext(stage_, editTarget_);
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

        if (!stage_ || !editTarget_.GetLayer()) {
            error = !stage_ ? "invalid stage" : "invalid edit target";
            return false;
        }

        const QList<SdfPath> roots = path::minimalRootPaths(path::uniquePaths(paths));
        if (roots.isEmpty())
            return true;

        SdfBatchNamespaceEdit batch;

        for (const SdfPath& root : roots) {
            if (root.IsEmpty() || root == SdfPath::AbsoluteRootPath() || !root.IsPrimPath()) {
                error = "invalid remove path";
                return false;
            }

            QString validationError;
            if (!layer::validatePrim(stage_, editTarget_.GetLayer(), root, validationError)) {
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

        if (!editTarget_.GetLayer()->CanApply(batch)) {
            error = "Sdf namespace remove batch cannot be applied";
            return false;
        }

        if (!editTarget_.GetLayer()->Apply(batch)) {
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

        if (!stage_ || !editTarget_.GetLayer()) {
            error = !stage_ ? "invalid stage" : "invalid edit target";
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
        if (!layer::validatePrim(stage_, editTarget_.GetLayer(), from, validationError)) {
            error = validationError;
            return false;
        }

        if (!layer::validateParent(stage_, editTarget_.GetLayer(), parentPath, validationError)) {
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

        const UsdPrim defaultPrim = stage_->GetDefaultPrim();
        if (defaultPrim && defaultPrim.GetPath() == from) {
            error = "cannot rename default prim";
            return false;
        }

        if (stage_->GetPrimAtPath(to)) {
            error = QString("destination already exists: %1").arg(qt::SdfPathToQString(to));
            return false;
        }

        UsdEditContext editContext(stage_, editTarget_);
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

        const UsdStageLoadRules loadRules = remappedLoadRules({ qMakePair(from, to) });

        if (!editor.ApplyEdits()) {
            error = "USD namespace rename failed";
            return false;
        }

        if (stage_->GetLoadRules() != loadRules)
            stage_->SetLoadRules(loadRules);
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

        if (!stage_ || !editTarget_.GetLayer()) {
            error = !stage_ ? "invalid stage" : "invalid edit target";
            return false;
        }

        if (moves.isEmpty())
            return true;

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

        // Capture the intended load policy before changing namespace.  The
        // resulting rules are only applied if they are actually different;
        // this avoids an unnecessary stage-wide payload recomposition for the
        // common case where source and destination inherit the same policy.
        const UsdStageLoadRules loadRules = remappedLoadRules(effectiveMoves);

        UsdEditContext editContext(stage_, editTarget_);
        UsdNamespaceEditor::EditOptions options;
        options.allowRelocatesAuthoring = false;

        // OpenUSD 25.11 effectively supports one queued namespace operation per
        // editor, so apply the requested moves one at a time.  Do not snapshot
        // the complete local layer stack: on production stages copying every
        // layer is far more expensive than the edit itself.  If a later move
        // fails, roll back the moves that already succeeded through the same
        // namespace editor API so dependency paths are repaired in reverse too.
        QList<QPair<SdfPath, SdfPath>> appliedMoves;
        appliedMoves.reserve(effectiveMoves.size());

        auto rollbackAppliedMoves = [&]() -> QString {
            QStringList rollbackErrors;

            for (auto it = appliedMoves.crbegin(); it != appliedMoves.crend(); ++it) {
                UsdNamespaceEditor rollbackEditor(stage_, options);
                std::string whyNot;

                if (!rollbackEditor.MovePrimAtPath(it->second, it->first)) {
                    rollbackErrors.append(
                        QString("could not queue rollback: %1 -> %2")
                            .arg(qt::SdfPathToQString(it->second), qt::SdfPathToQString(it->first)));
                    continue;
                }

                if (!rollbackEditor.CanApplyEdits(&whyNot)) {
                    rollbackErrors.append(
                        whyNot.empty()
                            ? QString("rollback cannot be applied: %1 -> %2")
                                  .arg(qt::SdfPathToQString(it->second), qt::SdfPathToQString(it->first))
                            : qt::StringToQString(whyNot));
                    continue;
                }

                if (!rollbackEditor.ApplyEdits()) {
                    rollbackErrors.append(
                        QString("rollback failed: %1 -> %2")
                            .arg(qt::SdfPathToQString(it->second), qt::SdfPathToQString(it->first)));
                }
            }

            return rollbackErrors.join("; ");
        };

        for (const auto& move : effectiveMoves) {
            UsdNamespaceEditor editor(stage_, options);
            std::string whyNot;

            if (!editor.MovePrimAtPath(move.first, move.second)) {
                const QString rollbackError = rollbackAppliedMoves();
                error = QString("USD namespace move could not be queued: %1 -> %2")
                            .arg(qt::SdfPathToQString(move.first), qt::SdfPathToQString(move.second));
                if (!rollbackError.isEmpty())
                    error += QString("; rollback: %1").arg(rollbackError);
                return false;
            }

            if (!editor.CanApplyEdits(&whyNot)) {
                const QString rollbackError = rollbackAppliedMoves();
                error = whyNot.empty() ? QStringLiteral("USD namespace move cannot be applied")
                                       : qt::StringToQString(whyNot);
                if (!rollbackError.isEmpty())
                    error += QString("; rollback: %1").arg(rollbackError);
                return false;
            }

            if (!editor.ApplyEdits()) {
                const QString rollbackError = rollbackAppliedMoves();
                error = "USD namespace move failed";
                if (!rollbackError.isEmpty())
                    error += QString("; rollback: %1").arg(rollbackError);
                return false;
            }

            appliedMoves.append(move);
        }

        if (stage_->GetLoadRules() != loadRules)
            stage_->SetLoadRules(loadRules);

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

        const UsdPrim defaultPrim = stage_->GetDefaultPrim();
        if (defaultPrim && defaultPrim.GetPath() == from) {
            error = "cannot move default prim";
            return false;
        }

        if (to.HasPrefix(from)) {
            error = "cannot move a prim below itself";
            return false;
        }

        QString validationError;
        if (!layer::validatePrim(stage_, editTarget_.GetLayer(), from, validationError)) {
            error = validationError;
            return false;
        }

        if (!layer::validateParent(stage_, editTarget_.GetLayer(), to.GetParentPath(), validationError)) {
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

    UsdStageLoadRules NamespaceEditor::remappedLoadRules(const QList<QPair<SdfPath, SdfPath>>& moves) const
    {
        const UsdStageLoadRules original = stage_->GetLoadRules();
        UsdStageLoadRules result;

        // Move explicit rules that live on or below a moved subtree.  Rules at
        // a destination that is being replaced are dropped so stale destination
        // policy cannot override the moved subtree.
        for (const auto& rule : original.GetRules()) {
            SdfPath path = rule.first;
            bool moved = false;

            for (const auto& move : moves) {
                if (path == move.first || path.HasPrefix(move.first)) {
                    path = path.ReplacePrefix(move.first, move.second);
                    moved = true;
                    break;
                }
            }

            if (!moved) {
                bool replaced = false;
                for (const auto& move : moves) {
                    if (path == move.second || path.HasPrefix(move.second)) {
                        replaced = true;
                        break;
                    }
                }

                if (replaced)
                    continue;
            }

            result.AddRule(path, rule.second);
        }

        // If the moved root had no explicit rule, preserve its inherited source
        // policy only when the destination would otherwise inherit a different
        // policy.  Avoiding redundant destination rules is important: merely
        // changing the load-rule table can force payload recomposition across a
        // very large stage even when the effective policy is unchanged.
        for (const auto& move : moves) {
            bool hasExplicitRootRule = false;

            for (const auto& rule : original.GetRules()) {
                if (rule.first == move.first) {
                    hasExplicitRootRule = true;
                    break;
                }
            }

            if (hasExplicitRootRule)
                continue;

            const auto sourceRule = original.GetEffectiveRuleForPath(move.first);
            const auto destinationRule = result.GetEffectiveRuleForPath(move.second);

            if (sourceRule != destinationRule)
                result.AddRule(move.second, sourceRule);
        }

        return result;
    }

}  // namespace edit
}  // namespace stageviz
