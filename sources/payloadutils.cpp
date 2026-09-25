// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#include "payloadutils.h"
#include "qtutils.h"
#include "usdutils.h"
#include <QDir>
#include <QFileInfo>
#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <pxr/base/gf/vec3d.h>
#include <pxr/base/gf/vec3f.h>
#include <pxr/base/vt/array.h>
#include <pxr/base/vt/value.h>
#include <pxr/usd/sdf/primSpec.h>
#include <pxr/usd/sdf/variantSetSpec.h>
#include <pxr/usd/sdf/variantSpec.h>
#include <pxr/usd/usd/attribute.h>
#include <pxr/usd/usd/prim.h>
#include <pxr/usd/usd/primRange.h>
#include <pxr/usd/usd/variantSets.h>
#include <pxr/usd/usdGeom/bboxCache.h>
#include <pxr/usd/usdGeom/imageable.h>
#include <pxr/usd/usdGeom/modelAPI.h>
#include <pxr/usd/usdGeom/tokens.h>
#include <pxr/usd/usdGeom/xformCache.h>

namespace stageviz {

namespace payload {
    namespace {
        struct NeighborCandidate {
            SdfPath path;
            double distance = 0.0;
        };

        bool extentsHintWorldBounds(const UsdPrim& prim, UsdGeomXformCache& xformCache, GfRange3d& bounds)
        {
            bounds = GfRange3d();
            if (!prim || !prim.IsValid())
                return false;

            VtVec3fArray extents;
            const UsdGeomModelAPI model(prim);

            if (model) {
                if (!model.GetExtentsHint(&extents))
                    return false;
            }
            else {
                const UsdAttribute attribute = prim.GetAttribute(UsdGeomTokens->extentsHint);
                if (!attribute || !attribute.Get(&extents))
                    return false;
            }

            if (extents.size() < 2 || (extents.size() % 2) != 0)
                return false;

            const GfMatrix4d world = xformCache.GetLocalToWorldTransform(prim);

            for (size_t i = 0; i + 1 < extents.size(); i += 2) {
                const GfVec3d minimum(extents[i]);
                const GfVec3d maximum(extents[i + 1]);

                for (int x = 0; x < 2; ++x) {
                    for (int y = 0; y < 2; ++y) {
                        for (int z = 0; z < 2; ++z) {
                            const GfVec3d corner(x ? maximum[0] : minimum[0], y ? maximum[1] : minimum[1],
                                                 z ? maximum[2] : minimum[2]);
                            bounds.UnionWith(world.Transform(corner));
                        }
                    }
                }
            }

            return !bounds.IsEmpty();
        }
    }  // namespace

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
        if (!stage || paths.isEmpty())
            return result;

        QSet<SdfPath> visited;

        auto collectPrim = [&](const UsdPrim& prim) {
            if (!prim || !prim.IsValid() || prim.IsPseudoRoot())
                return;

            const SdfPath primPath = prim.GetPath();
            if (visited.contains(primPath))
                return;

            visited.insert(primPath);

            if (!prim.HasPayload() || !prim.HasVariantSets())
                return;

            const UsdVariantSets sets = prim.GetVariantSets();
            for (const std::string& setName : sets.GetNames()) {
                const UsdVariantSet set = sets.GetVariantSet(setName);
                if (!set.IsValid())
                    continue;

                const QString qSetName = qt::StringToQString(setName);
                for (const std::string& value : set.GetVariantNames()) {
                    QList<SdfPath>& targets = result[qSetName][qt::StringToQString(value)];
                    if (!targets.contains(primPath))
                        targets.append(primPath);
                }
            }
        };

        const QList<SdfPath> roots = path::topLevelPaths(path::uniquePaths(paths));
        for (const SdfPath& inputPath : roots) {
            const SdfPath primPath = inputPath.IsPropertyPath() ? inputPath.GetPrimPath() : inputPath;
            const UsdPrim root = stage->GetPrimAtPath(primPath);
            if (!root)
                continue;

            collectPrim(root);

            for (UsdPrim ancestor = root.GetParent(); ancestor && !ancestor.IsPseudoRoot();
                 ancestor = ancestor.GetParent()) {
                collectPrim(ancestor);
            }

            for (const UsdPrim& prim : UsdPrimRange::AllPrims(root))
                collectPrim(prim);
        }

        return result;
    }

    QList<SdfPath> neighboringPaths(UsdStageRefPtr stage, const QList<SdfPath>& inputPaths)
    {
        QList<SdfPath> result;

        if (!stage || inputPaths.isEmpty())
            return result;

        const QList<SdfPath> selectedPaths = path::topLevelPaths(path::uniquePaths(inputPaths));

        UsdGeomBBoxCache bboxCache(UsdTimeCode::Default(), UsdGeomImageable::GetOrderedPurposeTokens(), true);

        GfRange3d selectionBounds;

        for (const SdfPath& inputPath : selectedPaths) {
            const SdfPath primPath = inputPath.IsPropertyPath() ? inputPath.GetPrimPath() : inputPath;

            const UsdPrim prim = stage->GetPrimAtPath(primPath);
            if (!prim || !prim.IsValid() || !prim.IsA<UsdGeomImageable>())
                continue;

            const GfBBox3d bbox = bboxCache.ComputeWorldBound(prim);
            selectionBounds.UnionWith(bbox.ComputeAlignedRange());
        }

        if (selectionBounds.IsEmpty())
            return result;

        const QList<SdfPath> sourcePayloadPaths = stage::outermostPayloadPaths(stage, selectedPaths);

        QSet<SdfPath> sourceSet;
        for (const SdfPath& path : sourcePayloadPaths)
            sourceSet.insert(path);

        constexpr double neighborScale = 1.5;

        const GfVec3d sourceCenter = selectionBounds.GetMidpoint();
        const GfVec3d sourceHalfSize = selectionBounds.GetSize() * 0.5;
        const GfVec3d searchHalfSize = sourceHalfSize * neighborScale;

        const GfRange3d searchBounds(sourceCenter - searchHalfSize, sourceCenter + searchHalfSize);

        QList<SdfPath> payloadPaths;

        for (const UsdPrim& prim : stage->TraverseAll()) {
            if (!prim || !prim.IsValid())
                continue;

            const SdfPath path = prim.GetPath();

            if (sourceSet.contains(path))
                continue;

            if (!stage::isPayload(stage, path))
                continue;

            payloadPaths.append(path);
        }

        const QList<SdfPath> candidatePaths = path::topLevelPaths(payloadPaths);

        QList<NeighborCandidate> candidates;
        candidates.reserve(candidatePaths.size());

        const GfVec3d searchMin = searchBounds.GetMin();
        const GfVec3d searchMax = searchBounds.GetMax();

        UsdGeomXformCache xformCache(UsdTimeCode::Default());

        for (const SdfPath& path : candidatePaths) {
            const UsdPrim prim = stage->GetPrimAtPath(path);

            if (!prim || !prim.IsValid())
                continue;

            if (prim.IsLoaded())
                continue;

            GfRange3d candidateBounds;
            if (!extentsHintWorldBounds(prim, xformCache, candidateBounds))
                continue;

            const GfVec3d candidateCenter = candidateBounds.GetMidpoint();

            bool centerInside = true;

            for (int axis = 0; axis < 3; ++axis) {
                if (candidateCenter[axis] < searchMin[axis] || candidateCenter[axis] > searchMax[axis]) {
                    centerInside = false;
                    break;
                }
            }

            if (!centerInside)
                continue;

            NeighborCandidate candidate;
            candidate.path = path;
            candidate.distance = (candidateCenter - sourceCenter).GetLength();

            candidates.append(candidate);
        }

        std::sort(candidates.begin(), candidates.end(), [](const NeighborCandidate& a, const NeighborCandidate& b) {
            if (a.distance != b.distance)
                return a.distance < b.distance;

            return a.path.GetString() < b.path.GetString();
        });

        result.reserve(candidates.size());

        for (const NeighborCandidate& candidate : candidates)
            result.append(candidate.path);

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

namespace stage {

    bool isPayloadInLayer(UsdStageRefPtr stage, const SdfLayerHandle& layer, const SdfPath& path)
    {
        if (!stage || !layer || path.IsEmpty())
            return false;

        const SdfPath primPath = path.IsPropertyPath() ? path.GetPrimPath() : path;
        const UsdPrim prim = stage->GetPrimAtPath(primPath);
        if (!prim || !prim.IsValid())
            return false;

        for (const SdfPrimSpecHandle& spec : prim.GetPrimStack()) {
            if (!spec || spec->GetLayer() != layer)
                continue;

            if (!spec->GetPayloadList().GetAppliedItems().empty())
                return true;
        }

        return false;
    }

    QList<SdfPath> layerPayloadPaths(UsdStageRefPtr stage, const SdfLayerHandle& layer)
    {
        QList<SdfPath> result;
        if (!stage || !layer)
            return result;

        for (const UsdPrim& prim : stage->TraverseAll()) {
            if (!prim || !prim.IsValid())
                continue;

            if (isPayloadInLayer(stage, layer, prim.GetPath()))
                path::appendUnique(result, prim.GetPath());
        }
        return result;
    }

    QList<SdfPath> nearestLayerPayloadPaths(UsdStageRefPtr stage, const SdfLayerHandle& layer,
                                            const QList<SdfPath>& paths)
    {
        QList<SdfPath> result;
        if (!stage || !layer || paths.isEmpty())
            return result;

        for (const SdfPath& inputPath : paths) {
            const SdfPath primPath = inputPath.IsPropertyPath() ? inputPath.GetPrimPath() : inputPath;
            UsdPrim prim = stage->GetPrimAtPath(primPath);

            while (prim && !prim.IsPseudoRoot()) {
                if (isPayloadInLayer(stage, layer, prim.GetPath())) {
                    path::appendUnique(result, prim.GetPath());
                    break;
                }
                prim = prim.GetParent();
            }
        }
        return result;
    }

    QList<SdfPath> nearestPayloadPaths(UsdStageRefPtr stage, const QList<SdfPath>& paths)
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

    QList<SdfPath> outermostPayloadPaths(UsdStageRefPtr stage, const QList<SdfPath>& paths)
    {
        QList<SdfPath> result;

        if (!stage || paths.isEmpty())
            return result;

        for (const SdfPath& inputPath : paths) {
            const SdfPath primPath = inputPath.IsPropertyPath() ? inputPath.GetPrimPath() : inputPath;
            UsdPrim prim = stage->GetPrimAtPath(primPath);
            SdfPath outermostPath;

            while (prim && !prim.IsPseudoRoot()) {
                if (isPayload(stage, prim.GetPath()))
                    outermostPath = prim.GetPath();

                prim = prim.GetParent();
            }

            path::appendUnique(result, outermostPath);
        }

        return path::topLevelPaths(result);
    }

    QList<SdfPath> descendantPayloadPaths(UsdStageRefPtr stage, const QList<SdfPath>& paths)
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

        const UsdPrim prim = stage->GetPrimAtPath(path);
        return prim && prim.HasPayload();
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

            SdfPath outermostPayloadPath;
            SdfPath currentPath = path;
            while (!currentPath.IsEmpty() && currentPath != SdfPath::AbsoluteRootPath()) {
                if (stage::isPayload(stage, currentPath))
                    outermostPayloadPath = currentPath;

                currentPath = currentPath.GetParentPath();
            }

            if (!outermostPayloadPath.IsEmpty()) {
                appendUnique(outermostPayloadPath);
                continue;
            }

            const QList<SdfPath> descendantPayloads = stage::descendantPayloadPaths(stage, { path });
            for (const SdfPath& descendantPayload : descendantPayloads)
                appendUnique(descendantPayload);
        }
        return path::topLevelPaths(result);
    }

}  // namespace stage

}  // namespace stageviz
