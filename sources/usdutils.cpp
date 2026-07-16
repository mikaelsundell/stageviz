// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#include "usdutils.h"
#include "qtutils.h"
#include <QFileInfo>
#include <algorithm>
#include <functional>
#include <pxr/usd/sdf/copyUtils.h>
#include <pxr/usd/sdf/variantSetSpec.h>
#include <pxr/usd/sdf/variantSpec.h>
#include <pxr/usd/usd/namespaceEditor.h>
#include <pxr/usd/usd/prim.h>
#include <pxr/usd/usd/primRange.h>
#include <pxr/usd/usd/variantSets.h>
#include <pxr/usd/usdGeom/bboxCache.h>
#include <pxr/usd/usdGeom/imageable.h>
#include <pxr/usd/usdGeom/tokens.h>
#include <stack>

namespace stageviz {
namespace {

    void ensureParentSpecs(const SdfLayerHandle& layer, const SdfPath& path)
    {
        if (!layer)
            return;

        const SdfPath parent = path.GetParentPath();
        if (parent.IsEmpty() || parent == SdfPath::AbsoluteRootPath())
            return;

        if (!layer->GetPrimAtPath(parent)) {
            ensureParentSpecs(layer, parent);
            SdfCreatePrimInLayer(layer, parent);
        }
    }

}  // namespace

namespace rootlayer {

    SdfLayerHandle opened(const UsdStageRefPtr& stage, QString& error)
    {
        if (!stage) {
            error = "stage missing";
            return {};
        }

        const SdfLayerHandle layer = stage->GetRootLayer();
        if (!layer) {
            error = "opened root layer missing";
            return {};
        }

        return layer;
    }

    bool validatePrim(const UsdStageRefPtr& stage, const SdfPath& path, QString& error, bool requireStrongest)
    {
        const SdfPath primPath = path.IsPropertyPath() ? path.GetPrimPath() : path;
        if (primPath.IsEmpty() || primPath == SdfPath::AbsoluteRootPath()) {
            error = "invalid prim path";
            return false;
        }

        const SdfLayerHandle layer = opened(stage, error);
        if (!layer)
            return false;

        const UsdPrim prim = stage->GetPrimAtPath(primPath);
        if (!prim || !prim.IsValid()) {
            error = QString("prim missing: %1").arg(qt::SdfPathToQString(primPath));
            return false;
        }

        if (!layer->GetPrimAtPath(primPath)) {
            error = QString("prim is not authored in opened root layer: %1").arg(qt::SdfPathToQString(primPath));
            return false;
        }

        if (requireStrongest) {
            const SdfPrimSpecHandleVector stack = prim.GetPrimStack();
            if (stack.empty() || !stack.front() || stack.front()->GetLayer() != layer) {
                error = QString("prim strongest opinion is not in opened root layer: %1")
                            .arg(qt::SdfPathToQString(primPath));
                return false;
            }
        }

        return true;
    }

    bool validateParent(const UsdStageRefPtr& stage, const SdfPath& parentPath, QString& error, bool requireStrongest)
    {
        if (parentPath == SdfPath::AbsoluteRootPath())
            return true;
        return validatePrim(stage, parentPath, error, requireStrongest);
    }

}  // namespace rootlayer

namespace identifier {
    std::string makeValidIdentifier(const std::string& input)
    {
        if (input.empty())
            return "Prim";

        return TfMakeValidIdentifier(input);
    }

    QString makeSafeIdentifier(const UsdStageRefPtr& stage, const SdfPath& parentPath, const QString& inputName)
    {
        const QString baseName = qt::StringToQString(makeValidIdentifier(qt::QStringToString(inputName)));

        if (!stage || !parentPath.IsAbsolutePath())
            return baseName;

        const UsdPrim parentPrim = stage->GetPrimAtPath(parentPath);
        if (!parentPrim || !parentPrim.IsValid())
            return baseName;

        auto childExists = [&](const QString& name) {
            const SdfPath childPath = parentPath.AppendChild(TfToken(qt::QStringToString(name)));
            const UsdPrim childPrim = stage->GetPrimAtPath(childPath);
            return childPrim && childPrim.IsValid();
        };

        if (!childExists(baseName))
            return baseName;

        for (int i = 1;; ++i) {
            const QString uniqueName = QString("%1_%2").arg(baseName).arg(i);
            if (!childExists(uniqueName))
                return uniqueName;
        }
    }

    QString makeSafeIdentifier(const UsdStageRefPtr& stage, const SdfPath& parentPath, const QString& inputName,
                               const SdfPath& ignorePath)
    {
        const QString baseName = qt::StringToQString(makeValidIdentifier(qt::QStringToString(inputName)));

        if (!stage || !parentPath.IsAbsolutePath())
            return baseName;

        const UsdPrim parentPrim = stage->GetPrimAtPath(parentPath);
        if (!parentPrim || !parentPrim.IsValid())
            return baseName;

        auto childExists = [&](const QString& name) {
            const SdfPath childPath = parentPath.AppendChild(TfToken(qt::QStringToString(name)));
            if (childPath == ignorePath)
                return false;

            const UsdPrim childPrim = stage->GetPrimAtPath(childPath);
            return childPrim && childPrim.IsValid();
        };

        if (!childExists(baseName))
            return baseName;

        for (int i = 1;; ++i) {
            const QString uniqueName = QString("%1_%2").arg(baseName).arg(i);
            if (!childExists(uniqueName))
                return uniqueName;
        }
    }

}  // namespace identifier

namespace path {
    QList<SdfPath> topLevelPaths(const QList<SdfPath>& paths)
    {
        QList<SdfPath> result;
        result.reserve(paths.size());

        for (const SdfPath& path : paths) {
            bool isChild = false;
            for (const SdfPath& other : paths) {
                if (path != other && path.HasPrefix(other)) {
                    isChild = true;
                    break;
                }
            }
            if (!isChild)
                result.append(path);
        }

        return result;
    }

    QList<SdfPath> uniquePaths(const QList<SdfPath>& paths)
    {
        QList<SdfPath> unique;
        unique.reserve(paths.size());
        for (const SdfPath& path : paths) {
            if (!path.IsEmpty() && !unique.contains(path))
                unique.append(path);
        }
        return unique;
    }

    QList<SdfPath> minimalRootPaths(const QList<SdfPath>& paths)
    {
        QList<SdfPath> sorted = paths;
        std::sort(sorted.begin(), sorted.end(),
                  [](const SdfPath& a, const SdfPath& b) { return a.GetPathElementCount() < b.GetPathElementCount(); });

        QList<SdfPath> result;
        result.reserve(sorted.size());

        for (const SdfPath& path : sorted) {
            bool covered = false;
            for (const SdfPath& existing : result) {
                if (path == existing || path.HasPrefix(existing)) {
                    covered = true;
                    break;
                }
            }
            if (!covered)
                result.append(path);
        }

        return result;
    }

    bool isAffectedPath(const SdfPath& selectedPath, const SdfPath& rootPath)
    {
        if (selectedPath.IsEmpty() || rootPath.IsEmpty())
            return false;

        const SdfPath selectedPrimPath = selectedPath.IsPropertyPath() ? selectedPath.GetPrimPath() : selectedPath;
        const SdfPath rootPrimPath = rootPath.IsPropertyPath() ? rootPath.GetPrimPath() : rootPath;

        return selectedPrimPath == rootPrimPath || selectedPrimPath.HasPrefix(rootPrimPath);
    }

    bool isWithinRoots(const QList<SdfPath>& mask, const SdfPath& path)
    {
        if (mask.isEmpty())
            return true;

        for (const SdfPath& maskPath : mask) {
            if (path == maskPath || path.HasPrefix(maskPath))
                return true;
        }

        return false;
    }

    bool isCoveredByRoots(const QList<SdfPath>& selection, const SdfPath& path)
    {
        for (const SdfPath& selectedPath : selection) {
            if (path == selectedPath || path.HasPrefix(selectedPath))
                return true;
        }

        return false;
    }

    QList<SdfPath> removeAffectedPaths(const QList<SdfPath>& paths, const QList<SdfPath>& removedPaths)
    {
        if (paths.isEmpty() || removedPaths.isEmpty())
            return paths;

        QList<SdfPath> result;
        result.reserve(paths.size());

        for (const SdfPath& inputPath : paths) {
            bool keep = true;
            for (const SdfPath& removedPath : removedPaths) {
                if (isAffectedPath(inputPath, removedPath)) {
                    keep = false;
                    break;
                }
            }

            if (keep)
                result.append(inputPath);
        }

        return result;
    }

    QList<SdfPath> remapAffectedPaths(const QList<SdfPath>& paths, const SdfPath& oldPath, const SdfPath& newPath)
    {
        QList<SdfPath> result;
        result.reserve(paths.size());

        for (const SdfPath& inputPath : paths) {
            if (isAffectedPath(inputPath, oldPath))
                result.append(newPath.AppendPath(inputPath.MakeRelativePath(oldPath)));
            else
                result.append(inputPath);
        }

        return result;
    }


    void appendUnique(QList<SdfPath>& paths, const SdfPath& path)
    {
        if (!path.IsEmpty() && !paths.contains(path))
            paths.append(path);
    }

    TfTokenVector removeTokens(TfTokenVector order, const TfTokenVector& tokens)
    {
        for (const TfToken& token : tokens)
            order.erase(std::remove(order.begin(), order.end(), token), order.end());
        return order;
    }

    TfTokenVector insertTokens(TfTokenVector order, const TfTokenVector& tokens, int index)
    {
        if (tokens.empty())
            return order;

        const int safeIndex = qBound(0, index, static_cast<int>(order.size()));
        order.insert(order.begin() + safeIndex, tokens.begin(), tokens.end());
        return order;
    }
}  // namespace path

namespace payload {
    bool applyLoad(UsdStageRefPtr stage, const SdfPath& path, bool useVariant, const std::string& variantSetName,
                   const std::string& variantSelection, PayloadState& payloadState, QString& error)
    {
        if (!stage) {
            error = "stage missing";
            return false;
        }

        UsdPrim prim = stage->GetPrimAtPath(path);
        if (!prim || !prim.HasPayload()) {
            error = "payload missing";
            return false;
        }

        payloadState.path = path;
        payloadState.wasLoaded = prim.IsLoaded();
        payloadState.hadVariantSet = false;
        payloadState.variantSetName.clear();
        payloadState.previousVariantSelection.clear();

        if (useVariant) {
            UsdVariantSet vs = prim.GetVariantSet(variantSetName);
            if (!vs.IsValid()) {
                error = "variant set missing";
                return false;
            }

            const auto variants = vs.GetVariantNames();
            if (std::find(variants.begin(), variants.end(), variantSelection) == variants.end()) {
                error = "variant value missing";
                return false;
            }

            payloadState.hadVariantSet = true;
            payloadState.variantSetName = variantSetName;
            payloadState.previousVariantSelection = vs.GetVariantSelection();

            if (prim.IsLoaded())
                prim.Unload();

            if (vs.GetVariantSelection() != variantSelection)
                vs.SetVariantSelection(variantSelection);
        }

        if (!prim.IsLoaded())
            prim.Load();

        return true;
    }

    bool applyUnload(UsdStageRefPtr stage, const SdfPath& path, PayloadState& payloadState, QString& error)
    {
        if (!stage) {
            error = "stage missing";
            return false;
        }

        UsdPrim prim = stage->GetPrimAtPath(path);
        if (!prim || !prim.HasPayload()) {
            error = "payload missing";
            return false;
        }

        payloadState.path = path;
        payloadState.wasLoaded = prim.IsLoaded();
        payloadState.hadVariantSet = false;
        payloadState.variantSetName.clear();
        payloadState.previousVariantSelection.clear();

        if (prim.IsLoaded())
            prim.Unload();

        return true;
    }

    bool restoreState(UsdStageRefPtr stage, const PayloadState& payloadState, QString& error)
    {
        if (!stage) {
            error = "stage missing";
            return false;
        }

        UsdPrim prim = stage->GetPrimAtPath(payloadState.path);
        if (!prim || !prim.HasPayload()) {
            error = "payload missing";
            return false;
        }

        if (payloadState.hadVariantSet) {
            if (prim.IsLoaded())
                prim.Unload();

            UsdVariantSet vs = prim.GetVariantSet(payloadState.variantSetName);
            if (!vs.IsValid()) {
                error = "variant set missing";
                return false;
            }

            if (vs.GetVariantSelection() != payloadState.previousVariantSelection)
                vs.SetVariantSelection(payloadState.previousVariantSelection);
        }

        if (payloadState.wasLoaded) {
            if (!prim.IsLoaded())
                prim.Load();
        }
        else {
            if (prim.IsLoaded())
                prim.Unload();
        }

        return true;
    }

    PayloadVariantTargets payloadVariantTargets(UsdStageRefPtr stage, const QList<SdfPath>& paths)
    {
        PayloadVariantTargets result;
        if (!stage || paths.isEmpty()) {
            return result;
        }

        std::unordered_set<SdfPath, SdfPath::Hash> visitedRoots;
        std::unordered_set<SdfPath, SdfPath::Hash> visitedPayloads;
        QList<SdfPath> roots;

        auto appendRoot = [&](const SdfPath& path) {
            if (path.IsEmpty())
                return;

            if (visitedRoots.find(path) != visitedRoots.end()) {
                return;
            }

            visitedRoots.insert(path);
            roots.append(path);
        };

        for (const SdfPath& inputPath : path::topLevelPaths(paths)) {
            SdfPath payloadRootPath;
            SdfPath currentPath = inputPath;

            while (!currentPath.IsEmpty() && currentPath != SdfPath::AbsoluteRootPath()) {
                if (stage::isPayload(stage, currentPath)) {
                    payloadRootPath = currentPath;
                }
                currentPath = currentPath.GetParentPath();
            }

            appendRoot(payloadRootPath.IsEmpty() ? inputPath : payloadRootPath);
        }

        roots = path::minimalRootPaths(roots);
        auto addPayloadVariants = [&](const UsdPrim& prim) {
            if (!prim)
                return;

            const SdfPath primPath = prim.GetPath();

            if (!stage::isPayload(stage, primPath))
                return;

            if (visitedPayloads.find(primPath) != visitedPayloads.end()) {
                return;
            }
            visitedPayloads.insert(primPath);
            UsdVariantSets variantSets = prim.GetVariantSets();
            std::vector<std::string> setNames = variantSets.GetNames();

            for (const std::string& setName : setNames) {
                UsdVariantSet variantSet = variantSets.GetVariantSet(setName);
                std::vector<std::string> variantNames = variantSet.GetVariantNames();

                // Cache the converted set name so we don't convert it multiple times per variant value
                QString qSetName = QString::fromStdString(setName);

                for (const std::string& variantName : variantNames) {
                    // Map out directly into our nested Qt container
                    result[qSetName][QString::fromStdString(variantName)].append(primPath);
                }
            }
        };

        for (const SdfPath& rootPath : roots) {
            const UsdPrim root = (rootPath == SdfPath::AbsoluteRootPath()) ? stage->GetPseudoRoot()
                                                                           : stage->GetPrimAtPath(rootPath);

            if (!root)
                continue;

            for (const UsdPrim& prim : UsdPrimRange(root))
                addPayloadVariants(prim);
        }
        return result;
    }


    QList<AssetEntry> assetEntries(UsdStageRefPtr stage, const QList<SdfPath>& paths)
    {
        QList<AssetEntry> result;
        if (!stage)
            return result;

        QSet<QString> seen;
        for (const SdfPath& inputPath : path::minimalRootPaths(path::uniquePaths(paths))) {
            const SdfPath primPath = inputPath.IsPropertyPath() ? inputPath.GetPrimPath() : inputPath;
            const UsdPrim prim = stage->GetPrimAtPath(primPath);
            if (!prim || !prim.IsValid() || !stage::isPayload(stage, primPath))
                continue;

            for (const SdfPrimSpecHandle& spec : prim.GetPrimStack()) {
                if (!spec)
                    continue;

                auto appendItems = [&](const SdfPayloadVector& items, const QString& setName, const QString& value) {
                    for (const SdfPayload& item : items) {
                        const QString assetPath = qt::StringToQString(item.GetAssetPath());
                        if (assetPath.isEmpty())
                            continue;

                        const QString key
                            = QString("%1|%2|%3|%4").arg(qt::SdfPathToQString(primPath), setName, value, assetPath);
                        if (seen.contains(key))
                            continue;
                        seen.insert(key);

                        AssetEntry entry;
                        entry.primPath = primPath;
                        entry.variantSet = setName;
                        entry.variantValue = value;
                        entry.assetPath = assetPath;
                        result.append(entry);
                    }
                };

                appendItems(spec->GetPayloadList().GetAppliedItems(), {}, {});

                for (const auto& setIt : spec->GetVariantSets()) {
                    const QString setName = qt::StringToQString(setIt.first);
                    const SdfVariantSetSpecHandle setSpec = setIt.second;
                    if (!setSpec)
                        continue;

                    for (const SdfVariantSpecHandle& variantSpec : setSpec->GetVariants()) {
                        if (!variantSpec)
                            continue;

                        const SdfPrimSpecHandle variantPrim = variantSpec->GetPrimSpec();
                        if (!variantPrim)
                            continue;

                        appendItems(variantPrim->GetPayloadList().GetAppliedItems(), setName,
                                    qt::StringToQString(variantSpec->GetName()));
                    }
                }
            }
        }

        return result;
    }

}  // namespace payload

namespace snapshot {

    bool capturePrimToLayer(UsdStageRefPtr stage, const SdfPath& stagePath, PrimState& out)
    {
        if (!stage)
            return false;

        const UsdPrim prim = stage->GetPrimAtPath(stagePath);
        if (!prim)
            return false;

        const auto& stack = prim.GetPrimStack();
        if (stack.empty())
            return false;

        for (const SdfPrimSpecHandle& spec : stack) {
            if (!spec)
                continue;

            const SdfLayerHandle srcLayer = spec->GetLayer();
            const SdfPath specPath = spec->GetPath();
            if (!srcLayer || specPath.IsEmpty())
                continue;

            if (!srcLayer->GetPrimAtPath(specPath))
                continue;

            SdfLayerRefPtr snapshotLayer = SdfLayer::CreateAnonymous(".usda");
            if (!snapshotLayer)
                continue;

            ensureParentSpecs(snapshotLayer, specPath);

            if (SdfCopySpec(srcLayer, specPath, snapshotLayer, specPath)) {
                out.stagePath = stagePath;
                out.specPath = specPath;
                out.snapshotLayer = snapshotLayer;
                return true;
            }
        }

        return false;
    }

    void restorePrimFromSnapshotLayer(const SdfLayerHandle& dstLayer, const PrimState& state)
    {
        if (!dstLayer || !state.snapshotLayer)
            return;

        ensureParentSpecs(dstLayer, state.stagePath);
        SdfCopySpec(state.snapshotLayer, state.specPath, dstLayer, state.stagePath);
    }

    void sortByHierarchy(PrimSnapshot& snapshot)
    {
        std::sort(snapshot.begin(), snapshot.end(), [](const PrimState& a, const PrimState& b) {
            const size_t ac = a.stagePath.GetPathElementCount();
            const size_t bc = b.stagePath.GetPathElementCount();
            if (ac != bc)
                return ac < bc;
            return a.stagePath.GetString() < b.stagePath.GetString();
        });
    }

}  // namespace snapshot

namespace stage {

    QList<SdfPath> ancestorPayloadPaths(UsdStageRefPtr stage, const QList<SdfPath>& paths)
    {
        QList<SdfPath> result;
        if (!stage || paths.isEmpty())
            return result;

        QSet<QString> seen;
        for (const SdfPath& path : paths) {
            UsdPrim prim = stage->GetPrimAtPath(path);
            while (prim) {
                const SdfPath primPath = prim.GetPath();
                if (isPayload(stage, primPath)) {
                    const QString key = qt::SdfPathToQString(primPath);
                    if (!seen.contains(key)) {
                        seen.insert(key);
                        result.append(primPath);
                    }
                    break;
                }
                prim = prim.GetParent();
            }
        }

        return result;
    }

    GfBBox3d boundingBox(UsdStageRefPtr stage, const QList<SdfPath>& paths)
    {
        UsdGeomBBoxCache cache(UsdTimeCode::Default(), UsdGeomImageable::GetOrderedPurposeTokens(), true);
        GfBBox3d bbox;
        for (const SdfPath& path : paths) {
            UsdPrim prim = stage->GetPrimAtPath(path);
            if (!prim || !prim.IsA<UsdGeomImageable>())
                continue;

            bbox = GfBBox3d::Combine(bbox, cache.ComputeWorldBound(prim));
        }
        return bbox;
    }

    SdfPath buildRenamePath(UsdStageRefPtr stage, const SdfPath& path, const QString& input, QString& error)
    {
        if (!stage || path.IsEmpty()) {
            error = "invalid stage or path";
            return SdfPath();
        }

        const QString trimmed = input.trimmed();
        if (trimmed.isEmpty()) {
            error = "empty name";
            return SdfPath();
        }

        if (UsdPrim defaultPrim = stage->GetDefaultPrim()) {
            if (path == defaultPrim.GetPath()) {
                error = "cannot rename default prim";
                return SdfPath();
            }
        }

        const SdfPath parentPath = path.GetParentPath();
        if (parentPath.IsEmpty()) {
            error = "invalid parent";
            return SdfPath();
        }

        const QString safeName = identifier::makeSafeIdentifier(stage, parentPath, trimmed, path);
        if (safeName.isEmpty()) {
            error = "invalid name";
            return SdfPath();
        }

        const std::string nameValue = qt::QStringToString(safeName);
        if (!SdfPath::IsValidIdentifier(nameValue)) {
            error = "invalid identifier";
            return SdfPath();
        }

        return parentPath.AppendChild(TfToken(nameValue));
    }

    SdfPath buildChildPath(UsdStageRefPtr stage, const SdfPath& parentPath, const QString& input, QString& error)
    {
        if (!stage || parentPath.IsEmpty()) {
            error = "invalid stage or parent path";
            return SdfPath();
        }

        const QString trimmed = input.trimmed();
        if (trimmed.isEmpty()) {
            error = "empty name";
            return SdfPath();
        }

        if (!parentPath.IsAbsolutePath()) {
            error = "parent path must be absolute";
            return SdfPath();
        }

        const bool parentIsRoot = parentPath == SdfPath::AbsoluteRootPath();
        const UsdPrim parentPrim = parentIsRoot ? UsdPrim() : stage->GetPrimAtPath(parentPath);

        if (!parentIsRoot && (!parentPrim || !parentPrim.IsValid())) {
            error = "invalid parent";
            return SdfPath();
        }

        const QString safeIdentifier = identifier::makeSafeIdentifier(stage, parentPath, trimmed);
        if (safeIdentifier.isEmpty()) {
            error = "invalid identifier";
            return SdfPath();
        }

        const std::string identifierValue = qt::QStringToString(safeIdentifier);
        if (!SdfPath::IsValidIdentifier(identifierValue)) {
            error = "invalid identifier";
            return SdfPath();
        }

        const SdfPath childPath = parentPath.AppendChild(TfToken(identifierValue));
        if (stage->GetPrimAtPath(childPath)) {
            error = "target exists";
            return SdfPath();
        }

        return childPath;
    }

    bool captureChildOrder(UsdStageRefPtr stage, const SdfPath& parentPath, TfTokenVector& out)
    {
        if (!stage || parentPath.IsEmpty())
            return false;

        const UsdPrim parent = parentPath == SdfPath::AbsoluteRootPath() ? stage->GetPseudoRoot()
                                                                         : stage->GetPrimAtPath(parentPath);
        if (!parent)
            return false;

        out.clear();
        for (const UsdPrim& child : parent.GetAllChildren())
            out.push_back(child.GetName());

        return true;
    }

    void restoreChildOrders(UsdStageRefPtr stage, const QHash<SdfPath, TfTokenVector>& orders)
    {
        for (auto it = orders.cbegin(); it != orders.cend(); ++it)
            restoreChildOrder(stage, it.key(), it.value());
    }

    QList<SdfPath> descendantsPayloadPaths(UsdStageRefPtr stage, const QList<SdfPath>& paths)
    {
        QList<SdfPath> result;
        if (!stage || paths.isEmpty())
            return result;

        QSet<QString> seen;
        std::function<void(const UsdPrim&)> collect = [&](const UsdPrim& prim) {
            if (!prim)
                return;

            const SdfPath primPath = prim.GetPath();
            const QString key = qt::SdfPathToQString(primPath);

            if (isPayload(stage, primPath) && !seen.contains(key)) {
                seen.insert(key);
                result.append(primPath);
            }

            for (const UsdPrim& child : prim.GetAllChildren())
                collect(child);
        };

        for (const SdfPath& path : paths) {
            const UsdPrim prim = stage->GetPrimAtPath(path);
            if (prim)
                collect(prim);
        }

        return result;
    }

    QList<SdfPath> filterStrongestEditablePaths(UsdStageRefPtr stage, const QList<SdfPath>& paths)
    {
        QList<SdfPath> result;
        if (!stage)
            return result;

        for (const SdfPath& path : paths) {
            const SdfPath primPath = path.IsPropertyPath() ? path.GetPrimPath() : path;
            if (isStrongestEditable(stage, primPath))
                result.append(primPath);
        }

        return result;
    }

    QMap<QString, QList<QString>> findVariantSets(UsdStageRefPtr stage, const QList<SdfPath>& paths, bool recursive)
    {
        QMap<QString, QList<QString>> result;
        if (!stage || paths.isEmpty())
            return result;

        const QList<SdfPath> filtered = path::topLevelPaths(paths);

        std::vector<UsdPrim> prims;
        prims.reserve(filtered.size() * 4);

        for (const SdfPath& path : filtered) {
            UsdPrim root = stage->GetPrimAtPath(path);
            if (!root)
                continue;

            prims.push_back(root);

            if (recursive) {
                std::stack<UsdPrim> stack;
                stack.push(root);

                while (!stack.empty()) {
                    UsdPrim prim = stack.top();
                    stack.pop();

                    for (const UsdPrim& child : prim.GetAllChildren()) {
                        prims.push_back(child);
                        stack.push(child);
                    }
                }
            }
        }

        for (const UsdPrim& prim : prims) {
            if (!prim)
                continue;

            const std::vector<std::string> setNames = prim.GetVariantSets().GetNames();
            for (const std::string& setName : setNames) {
                UsdVariantSet variantSet = prim.GetVariantSet(setName);
                const std::vector<std::string> variantNames = variantSet.GetVariantNames();
                const QString key = QString::fromUtf8(setName.c_str());

                QList<QString>& bucket = result[key];
                bucket.reserve(bucket.size() + int(variantNames.size()));
                for (const std::string& value : variantNames)
                    bucket.append(QString::fromUtf8(value.c_str()));
            }
        }

        for (auto it = result.begin(); it != result.end(); ++it) {
            QList<QString>& list = it.value();
            std::sort(list.begin(), list.end());
            list.erase(std::unique(list.begin(), list.end()), list.end());
        }

        return result;
    }

    bool hasCompositionArc(const UsdPrim& prim)
    {
        if (!prim || !prim.IsValid())
            return false;

        if (prim.HasPayload())
            return true;

        if (prim.HasAuthoredReferences())
            return true;

        if (prim.HasAuthoredInherits())
            return true;

        if (prim.HasAuthoredSpecializes())
            return true;

        if (prim.HasVariantSets())
            return true;

        return false;
    }

    TfTokenVector insertChildOrderToken(const TfTokenVector& order, const TfToken& name, int index)
    {
        TfTokenVector out;
        out.reserve(order.size() + 1);

        for (const TfToken& token : order) {
            if (token != name)
                out.push_back(token);
        }

        if (index < 0 || index > static_cast<int>(out.size()))
            index = static_cast<int>(out.size());

        out.insert(out.begin() + index, name);
        return out;
    }

    bool isInsideCompositionArc(UsdStageRefPtr stage, const SdfPath& path)
    {
        if (!stage || path.IsEmpty() || path == SdfPath::AbsoluteRootPath())
            return false;

        SdfPath current = path;
        while (!current.IsEmpty() && current != SdfPath::AbsoluteRootPath()) {
            const UsdPrim prim = stage->GetPrimAtPath(current);
            if (hasCompositionArc(prim))
                return true;

            current = current.GetParentPath();
        }

        return false;
    }

    bool movePrims(UsdStageRefPtr stage, const QList<QPair<SdfPath, SdfPath>>& moves, QString& error)
    {
        if (!stage) {
            error = "invalid stage";
            return false;
        }
        if (moves.isEmpty())
            return true;

        QString rootError;
        const SdfLayerHandle rootLayer = rootlayer::opened(stage, rootError);
        if (!rootLayer) {
            error = rootError;
            return false;
        }

        SdfBatchNamespaceEdit edits;
        bool hasEdits = false;
        QSet<SdfPath> destinations;
        UsdStageLoadRules loadRules = stage->GetLoadRules();

        for (const auto& move : moves) {
            const SdfPath& from = move.first;
            const SdfPath& to = move.second;
            if (from.IsEmpty() || to.IsEmpty() || from == SdfPath::AbsoluteRootPath() || !from.IsPrimPath()
                || !to.IsPrimPath()) {
                error = "invalid move path";
                return false;
            }
            if (from == to)
                continue;
            if (to.HasPrefix(from)) {
                error = "cannot move a prim below itself";
                return false;
            }
            if (destinations.contains(to)) {
                error = "duplicate move destination";
                return false;
            }
            destinations.insert(to);

            QString validationError;
            if (!rootlayer::validatePrim(stage, from, validationError)) {
                error = validationError;
                return false;
            }
            if (!rootlayer::validateParent(stage, to.GetParentPath(), validationError)) {
                error = validationError;
                return false;
            }
            if (isInsideCompositionArc(stage, from)
                || (to.GetParentPath() != SdfPath::AbsoluteRootPath()
                    && isInsideCompositionArc(stage, to.GetParentPath()))) {
                error = "cannot move into or out of composed prims";
                return false;
            }

            const UsdPrim existing = stage->GetPrimAtPath(to);
            if (existing) {
                bool sourceDestination = false;
                for (const auto& other : moves) {
                    if (other.first == to) {
                        sourceDestination = true;
                        break;
                    }
                }
                if (!sourceDestination) {
                    error = QString("destination already exists: %1").arg(qt::SdfPathToQString(to));
                    return false;
                }
            }

            edits.Add(from, to);
            hasEdits = true;
            loadRules = remapLoadRules(loadRules, from, to);
        }

        if (!hasEdits)
            return true;
        if (!rootLayer->CanApply(edits)) {
            error = "root layer cannot apply namespace edit";
            return false;
        }
        if (!rootLayer->Apply(edits)) {
            error = "root layer namespace edit failed";
            return false;
        }

        stage->SetLoadRules(loadRules);
        return true;
    }

    bool movePrim(UsdStageRefPtr stage, const SdfPath& from, const SdfPath& toParent, QString& error)
    {
        if (from.IsEmpty() || toParent.IsEmpty()) {
            error = "invalid path";
            return false;
        }
        return movePrims(stage, { qMakePair(from, toParent.AppendChild(from.GetNameToken())) }, error);
    }


    TfTokenVector removeChildOrderToken(const TfTokenVector& order, const TfToken& name)
    {
        TfTokenVector out;
        out.reserve(order.size());

        for (const TfToken& token : order) {
            if (token != name)
                out.push_back(token);
        }

        return out;
    }

    bool isAuthored(UsdStageRefPtr stage, const SdfPath& path)
    {
        if (!stage)
            return false;

        UsdPrim prim = stage->GetPrimAtPath(path);
        if (!prim)
            return false;

        return !prim.GetPrimStack().empty();
    }

    bool isEditable(UsdStageRefPtr stage, const SdfPath& path)
    {
        if (!stage)
            return false;

        UsdPrim prim = stage->GetPrimAtPath(path);
        if (!prim || !prim.IsActive())
            return false;

        if (isPayloadHierarchy(stage, path))
            return false;

        if (!isAuthored(stage, path))
            return false;

        if (!isEditTarget(stage, path))
            return false;

        return true;
    }

    bool isEditTarget(UsdStageRefPtr stage, const SdfPath& path)
    {
        if (!stage)
            return false;

        SdfLayerHandle editLayer = stage->GetEditTarget().GetLayer();
        if (!editLayer)
            return false;

        return editLayer->GetPrimAtPath(path) != nullptr;
    }

    bool isLoaded(UsdStageRefPtr stage, const SdfPath& path)
    {
        if (!stage || path.IsEmpty())
            return false;

        const UsdPrim rootPrim = (path == SdfPath::AbsoluteRootPath()) ? stage->GetPseudoRoot()
                                                                       : stage->GetPrimAtPath(path);
        if (!rootPrim)
            return false;

        bool foundPayload = false;

        for (const UsdPrim& prim : UsdPrimRange(rootPrim)) {
            if (!prim)
                continue;

            if (!stage::isPayload(stage, prim.GetPath()))
                continue;

            foundPayload = true;

            if (!prim.IsLoaded())
                return false;
        }

        return foundPayload;
    }

    bool isPayload(UsdStageRefPtr stage, const SdfPath& path)
    {
        if (!stage)
            return false;

        UsdPrim prim = stage->GetPrimAtPath(path);
        if (!prim)
            return false;

        if (prim.HasPayload())
            return true;

        for (const SdfPrimSpecHandle& spec : prim.GetPrimStack()) {
            if (!spec)
                continue;

            auto payloads = spec->GetPayloadList();

            if (!payloads.GetExplicitItems().empty() || !payloads.GetAddedItems().empty()
                || !payloads.GetPrependedItems().empty())
                return true;
        }

        return false;
    }

    bool isPayloadHierarchy(UsdStageRefPtr stage, const SdfPath& path)
    {
        if (!stage)
            return false;

        UsdPrim prim = stage->GetPrimAtPath(path);
        if (!prim)
            return false;

        UsdPrim p = prim;
        while (p) {
            if (isPayload(stage, p.GetPath()))
                return true;
            p = p.GetParent();
        }

        return false;
    }

    bool isStrongestEditable(UsdStageRefPtr stage, const SdfPath& path)
    {
        if (!stage)
            return false;

        const UsdPrim prim = stage->GetPrimAtPath(path);
        if (!prim)
            return false;

        const SdfLayerHandle editLayer = stage->GetEditTarget().GetLayer();
        if (!editLayer)
            return false;

        const auto& stack = prim.GetPrimStack();
        if (stack.empty())
            return false;

        const SdfPrimSpecHandle& strongest = stack.front();
        if (!strongest)
            return false;

        return strongest->GetLayer() == editLayer;
    }

    bool isVisible(UsdStageRefPtr stage, const SdfPath& path)
    {
        UsdPrim prim = stage->GetPrimAtPath(path);
        if (!prim)
            return true;

        UsdGeomImageable imageable(prim);
        if (!imageable)
            return true;

        TfToken vis;
        if (!imageable.GetVisibilityAttr().Get(&vis))
            return true;
        return vis != UsdGeomTokens->invisible;
    }

    QList<SdfPath> leafPaths(UsdStageRefPtr stage, const QList<SdfPath>& mask, bool childMustBeWithinMask)
    {
        QList<SdfPath> paths;
        if (!stage)
            return paths;

        for (const UsdPrim& prim : stage->Traverse()) {
            if (!prim || !prim.IsValid())
                continue;

            const SdfPath path = prim.GetPath();
            if (path.IsEmpty() || path == SdfPath::AbsoluteRootPath())
                continue;

            if (!path::isWithinRoots(mask, path))
                continue;

            bool hasTraversableChild = false;
            for (const UsdPrim& child : prim.GetChildren()) {
                if (!child || !child.IsValid())
                    continue;

                if (!childMustBeWithinMask || path::isWithinRoots(mask, child.GetPath())) {
                    hasTraversableChild = true;
                    break;
                }
            }

            if (!hasTraversableChild)
                paths.append(path);
        }

        return paths;
    }

    QList<SdfPath> payloadPaths(UsdStageRefPtr stage, const QList<SdfPath>& paths)
    {
        QList<SdfPath> result;
        if (!stage || paths.isEmpty())
            return result;

        QSet<QString> seen;
        for (const SdfPath& path : paths) {
            const UsdPrim prim = stage->GetPrimAtPath(path);
            if (!prim)
                continue;

            const SdfPath primPath = prim.GetPath();
            const QString key = qt::SdfPathToQString(primPath);

            if (isPayload(stage, primPath) && !seen.contains(key)) {
                seen.insert(key);
                result.append(primPath);
            }
        }

        return result;
    }

    TfTokenVector remapChildOrder(const TfTokenVector& order, const TfToken& oldName, const TfToken& newName)
    {
        TfTokenVector out = order;
        for (TfToken& token : out) {
            if (token == oldName) {
                token = newName;
                break;
            }
        }
        return out;
    }

    UsdStageLoadRules remapLoadRules(const UsdStageLoadRules& rules, const SdfPath& oldPath, const SdfPath& newPath)
    {
        UsdStageLoadRules out;

        for (const auto& r : rules.GetRules()) {
            const SdfPath& p = r.first;
            const auto& policy = r.second;

            if (p.HasPrefix(oldPath)) {
                const SdfPath rel = p.MakeRelativePath(oldPath);
                const SdfPath mapped = newPath.AppendPath(rel);
                out.AddRule(mapped, policy);
            }
            else {
                out.AddRule(p, policy);
            }
        }

        return out;
    }

    bool removePrimSpec(const SdfLayerHandle& layer, const SdfPath& specPath)
    {
        if (!layer || specPath.IsEmpty() || specPath == SdfPath::AbsoluteRootPath())
            return false;

        if (!layer->GetPrimAtPath(specPath))
            return false;

        SdfBatchNamespaceEdit edits;
        edits.Add(specPath, SdfPath::EmptyPath());

        if (!layer->CanApply(edits))
            return false;

        return layer->Apply(edits);
    }

    bool renamePrim(UsdStageRefPtr stage, const SdfPath& from, const SdfPath& to, QString& error)
    {
        if (from.GetParentPath() != to.GetParentPath()) {
            error = "invalid rename target";
            return false;
        }
        return movePrims(stage, { qMakePair(from, to) }, error);
    }


    void restoreChildOrder(UsdStageRefPtr stage, const SdfPath& parentPath, const TfTokenVector& childOrder)
    {
        if (!stage || parentPath.IsEmpty())
            return;

        QString error;
        const SdfLayerHandle rootLayer = rootlayer::opened(stage, error);
        if (!rootLayer)
            return;

        if (parentPath == SdfPath::AbsoluteRootPath()) {
            rootLayer->SetRootPrimOrder(childOrder);
            return;
        }

        ensureParentSpecs(rootLayer, parentPath);
        if (!rootLayer->GetPrimAtPath(parentPath))
            SdfCreatePrimInLayer(rootLayer, parentPath);

        const UsdPrim parent = stage->GetPrimAtPath(parentPath);
        if (parent)
            parent.SetChildrenReorder(childOrder);
    }

    QList<SdfPath> resolvePayloadPaths(UsdStageRefPtr stage, const QList<SdfPath>& paths)
    {
        QList<SdfPath> result;
        if (!stage || paths.isEmpty())
            return result;

        const QList<SdfPath> roots = path::topLevelPaths(paths);
        QSet<QString> seen;

        auto appendUnique = [&](const SdfPath& path) {
            if (path.IsEmpty())
                return;

            const QString key = qt::SdfPathToQString(path);
            if (seen.contains(key))
                return;

            seen.insert(key);
            result.append(path);
        };

        for (const SdfPath& path : roots) {
            if (path.IsEmpty())
                continue;

            SdfPath rootMostPayloadPath;
            SdfPath currentPath = path;

            while (!currentPath.IsEmpty() && currentPath != SdfPath::AbsoluteRootPath()) {
                if (stage::isPayload(stage, currentPath))
                    rootMostPayloadPath = currentPath;

                currentPath = currentPath.GetParentPath();
            }

            if (!rootMostPayloadPath.IsEmpty()) {
                appendUnique(rootMostPayloadPath);
                continue;
            }

            const QList<SdfPath> descendantPayloads = stage::descendantsPayloadPaths(stage, { path });
            for (const SdfPath& descendantPayload : descendantPayloads)
                appendUnique(descendantPayload);
        }

        return path::topLevelPaths(result);
    }

    void setVisible(UsdStageRefPtr stage, const QList<SdfPath>& paths, bool visible, bool recursive)
    {
        for (const SdfPath& path : paths) {
            UsdPrim prim = stage->GetPrimAtPath(path);
            if (!prim)
                continue;

            UsdGeomImageable imageable(prim);
            if (imageable) {
                if (visible)
                    imageable.MakeVisible();
                else
                    imageable.MakeInvisible();
            }

            if (recursive) {
                for (const UsdPrim& child : prim.GetAllDescendants()) {
                    UsdGeomImageable childImageable(child);
                    if (!childImageable)
                        continue;

                    TfToken currentVis;
                    childImageable.GetVisibilityAttr().Get(&currentVis);
                    TfToken desiredVis = visible ? UsdGeomTokens->inherited : UsdGeomTokens->invisible;
                    if (currentVis != desiredVis) {
                        if (visible)
                            childImageable.MakeVisible();
                        else
                            childImageable.MakeInvisible();
                    }
                }
            }
        }
    }

}  // namespace stage
}  // namespace stageviz
