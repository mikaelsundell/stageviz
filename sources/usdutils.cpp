// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#include "usdutils.h"
#include "qtutils.h"
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
#include <pxr/usd/sdf/copyUtils.h>
#include <pxr/usd/sdf/primSpec.h>
#include <pxr/usd/sdf/variantSetSpec.h>
#include <pxr/usd/sdf/variantSpec.h>
#include <pxr/usd/usd/attribute.h>
#include <pxr/usd/usd/editContext.h>
#include <pxr/usd/usd/editTarget.h>
#include <pxr/usd/usd/namespaceEditor.h>
#include <pxr/usd/usd/prim.h>
#include <pxr/usd/usd/primRange.h>
#include <pxr/usd/usd/variantSets.h>
#include <pxr/usd/usdGeom/bboxCache.h>
#include <pxr/usd/usdGeom/gprim.h>
#include <pxr/usd/usdGeom/imageable.h>
#include <pxr/usd/usdGeom/modelAPI.h>
#include <pxr/usd/usdGeom/tokens.h>
#include <pxr/usd/usdGeom/xform.h>
#include <pxr/usd/usdGeom/xformCache.h>
#include <pxr/usd/usdGeom/xformOp.h>
#include <pxr/usd/usdGeom/xformable.h>
#include <pxr/usd/usdShade/material.h>
#include <pxr/usd/usdShade/materialBindingAPI.h>
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

    bool matrixClose(const GfMatrix4d& a, const GfMatrix4d& b, double tolerance = 1.0e-8)
    {
        for (int row = 0; row < 4; ++row) {
            for (int column = 0; column < 4; ++column) {
                if (std::abs(a[row][column] - b[row][column]) > tolerance)
                    return false;
            }
        }
        return true;
    }

    bool localPivot(const UsdGeomXformable& xformable, GfVec3d& pivot)
    {
        if (!xformable)
            return false;

        const TfToken pivotToken("xformOp:translate:pivot");
        const TfToken inversePivotToken("!invert!xformOp:translate:pivot");

        VtTokenArray order;
        if (!xformable.GetXformOpOrderAttr().Get(&order, UsdTimeCode::Default()))
            return false;

        int pivotIndex = -1;
        int inversePivotIndex = -1;

        for (int i = 0; i < static_cast<int>(order.size()); ++i) {
            if (order[i] == pivotToken)
                pivotIndex = i;
            else if (order[i] == inversePivotToken)
                inversePivotIndex = i;
        }

        if (pivotIndex < 0 || inversePivotIndex <= pivotIndex)
            return false;

        const UsdAttribute pivotAttr = xformable.GetPrim().GetAttribute(pivotToken);
        if (!pivotAttr)
            return false;

        VtValue value;
        if (!pivotAttr.Get(&value, UsdTimeCode::Default()))
            return false;

        if (value.IsHolding<GfVec3d>()) {
            pivot = value.UncheckedGet<GfVec3d>();
            return true;
        }

        if (value.IsHolding<GfVec3f>()) {
            const GfVec3f v = value.UncheckedGet<GfVec3f>();
            pivot = GfVec3d(v[0], v[1], v[2]);
            return true;
        }

        return false;
    }

    bool setLocalMatrixWithPivot(const UsdGeomXformable& xformable, const GfMatrix4d& localMatrix,
                                 const GfVec3d& pivotValue, QString& error)
    {
        if (!xformable) {
            error = "prim is not xformable";
            return false;
        }

        xformable.ClearXformOpOrder();

        const UsdGeomXformOp pivotOp = xformable.AddTranslateOp(UsdGeomXformOp::PrecisionDouble, TfToken("pivot"),
                                                                false);
        const UsdGeomXformOp matrixOp = xformable.AddTransformOp(UsdGeomXformOp::PrecisionDouble);
        const UsdGeomXformOp inversePivotOp = xformable.AddTranslateOp(UsdGeomXformOp::PrecisionDouble,
                                                                       TfToken("pivot"), true);

        if (!pivotOp || !matrixOp || !inversePivotOp || !pivotOp.Set(pivotValue, UsdTimeCode::Default())) {
            error = "failed to preserve pivot transform ops";
            return false;
        }

        GfMatrix4d pivotMatrix(1.0);
        pivotMatrix.SetTranslate(pivotValue);

        GfMatrix4d inversePivotMatrix(1.0);
        inversePivotMatrix.SetTranslate(-pivotValue);

        const GfMatrix4d candidates[] = {
            pivotMatrix * localMatrix * inversePivotMatrix,
            inversePivotMatrix * localMatrix * pivotMatrix,
        };

        for (const GfMatrix4d& candidate : candidates) {
            if (!matrixOp.Set(candidate, UsdTimeCode::Default()))
                continue;

            GfMatrix4d evaluated(1.0);
            bool resetsXformStack = false;
            if (!xformable.GetLocalTransformation(&evaluated, &resetsXformStack, UsdTimeCode::Default()))
                continue;

            if (matrixClose(evaluated, localMatrix))
                return true;
        }

        error = "failed to preserve local transform while retaining pivot";
        return false;
    }


    struct PayloadNeighborCandidate {
        SdfPath path;
        double distance = 0.0;
    };

    bool payloadExtentsHintWorldBounds(const UsdPrim& prim, UsdGeomXformCache& xformCache, GfRange3d& bounds)
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

namespace editlayer {
    SdfLayerHandle opened(const UsdStageRefPtr& stage, QString& error)
    {
        if (!stage) {
            error = "stage missing";
            return {};
        }
        const SdfLayerHandle layer = stage->GetEditTarget().GetLayer();
        if (!layer) {
            error = "edit layer missing";
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
            error = QString("prim is not authored in edit layer: %1").arg(qt::SdfPathToQString(primPath));
            return false;
        }

        if (requireStrongest) {
            const SdfPrimSpecHandleVector stack = prim.GetPrimStack();

            if (stack.empty() || !stack.front() || stack.front()->GetLayer() != layer) {
                error = QString("prim strongest opinion is not in edit layer: %1").arg(qt::SdfPathToQString(primPath));

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

}  // namespace editlayer

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

        const bool parentIsRoot = parentPath == SdfPath::AbsoluteRootPath();
        const UsdPrim parentPrim = parentIsRoot ? UsdPrim() : stage->GetPrimAtPath(parentPath);

        if (!parentIsRoot && (!parentPrim || !parentPrim.IsValid()))
            return baseName;

        const SdfLayerHandle editLayer = stage->GetEditTarget().GetLayer();

        auto childExists = [&](const QString& name) {
            const SdfPath childPath = parentPath.AppendChild(TfToken(qt::QStringToString(name)));

            const UsdPrim childPrim = stage->GetPrimAtPath(childPath);
            if (childPrim && childPrim.IsValid())
                return true;

            if (editLayer && editLayer->GetPrimAtPath(childPath))
                return true;

            return false;
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

        const bool parentIsRoot = parentPath == SdfPath::AbsoluteRootPath();
        const UsdPrim parentPrim = parentIsRoot ? UsdPrim() : stage->GetPrimAtPath(parentPath);

        if (!parentIsRoot && (!parentPrim || !parentPrim.IsValid()))
            return baseName;

        const SdfLayerHandle editLayer = stage->GetEditTarget().GetLayer();

        auto childExists = [&](const QString& name) {
            const SdfPath childPath = parentPath.AppendChild(TfToken(qt::QStringToString(name)));

            if (childPath == ignorePath)
                return false;

            const UsdPrim childPrim = stage->GetPrimAtPath(childPath);
            if (childPrim && childPrim.IsValid())
                return true;

            if (editLayer && editLayer->GetPrimAtPath(childPath))
                return true;

            return false;
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
                QString qSetName = QString::fromStdString(setName);

                for (const std::string& variantName : variantNames) {
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

        const QList<SdfPath> sourcePayloadPaths = stage::topMostPayloadPaths(stage, selectedPaths);

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

        QList<PayloadNeighborCandidate> candidates;
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
            if (!payloadExtentsHintWorldBounds(prim, xformCache, candidateBounds))
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

            PayloadNeighborCandidate candidate;
            candidate.path = path;
            candidate.distance = (candidateCenter - sourceCenter).GetLength();

            candidates.append(candidate);
        }

        std::sort(candidates.begin(), candidates.end(),
                  [](const PayloadNeighborCandidate& a, const PayloadNeighborCandidate& b) {
                      if (a.distance != b.distance)
                          return a.distance < b.distance;

                      return a.path.GetString() < b.path.GetString();
                  });

        result.reserve(candidates.size());

        for (const PayloadNeighborCandidate& candidate : candidates)
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
    std::string compositionAssetPath(const SdfLayerHandle& destinationLayer, const QString& filename)
    {
        const QString absoluteFilename = QFileInfo(filename).absoluteFilePath();
        if (!destinationLayer || destinationLayer->IsAnonymous())
            return qt::QStringToString(QDir::fromNativeSeparators(absoluteFilename));

        const QString destinationFilename = QString::fromStdString(destinationLayer->GetRealPath());
        if (destinationFilename.isEmpty())
            return qt::QStringToString(QDir::fromNativeSeparators(absoluteFilename));

        const QDir directory(QFileInfo(destinationFilename).absolutePath());
        return qt::QStringToString(QDir::fromNativeSeparators(directory.relativeFilePath(absoluteFilename)));
    }

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

    QList<SdfPath> topMostPayloadPaths(UsdStageRefPtr stage, const QList<SdfPath>& paths)
    {
        QList<SdfPath> result;

        if (!stage || paths.isEmpty())
            return result;

        for (const SdfPath& inputPath : paths) {
            const SdfPath primPath = inputPath.IsPropertyPath() ? inputPath.GetPrimPath() : inputPath;
            UsdPrim prim = stage->GetPrimAtPath(primPath);
            SdfPath topMostPath;

            while (prim && !prim.IsPseudoRoot()) {
                if (isPayload(stage, prim.GetPath()))
                    topMostPath = prim.GetPath();

                prim = prim.GetParent();
            }

            path::appendUnique(result, topMostPath);
        }

        return path::topLevelPaths(result);
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

    bool isTransformEditable(UsdStageRefPtr stage, const SdfPath& path)
    {
        if (!stage || path.IsEmpty() || path == SdfPath::AbsoluteRootPath())
            return false;

        const UsdPrim prim = stage->GetPrimAtPath(path);

        if (!prim || !prim.IsValid() || prim.IsInstanceProxy() || !UsdGeomXformable(prim))
            return false;

        // Transform editing is a property override, not a namespace edit.
        // Composed prims may therefore receive a stronger edit-layer opinion.
        return bool(stage->GetEditTarget().GetLayer());
    }

    bool worldTransform(UsdStageRefPtr stage, const SdfPath& path, GfMatrix4d& matrix, QString& error)
    {
        if (!stage) {
            error = "stage missing";
            return false;
        }

        const UsdPrim prim = stage->GetPrimAtPath(path);

        if (!prim || !prim.IsValid()) {
            error = QString("prim missing: %1").arg(qt::SdfPathToQString(path));
            return false;
        }

        if (!UsdGeomXformable(prim)) {
            error = QString("prim is not xformable: %1").arg(qt::SdfPathToQString(path));
            return false;
        }

        UsdGeomXformCache cache(UsdTimeCode::Default());
        matrix = cache.GetLocalToWorldTransform(prim);
        return true;
    }

    bool worldPivot(UsdStageRefPtr stage, const SdfPath& path, GfVec3d& pivot, QString& error)
    {
        if (!stage) {
            error = "stage missing";
            return false;
        }

        const UsdPrim prim = stage->GetPrimAtPath(path);
        if (!prim || !prim.IsValid()) {
            error = QString("prim missing: %1").arg(qt::SdfPathToQString(path));
            return false;
        }

        const UsdGeomXformable xformable(prim);
        if (!xformable) {
            error = QString("prim is not xformable: %1").arg(qt::SdfPathToQString(path));
            return false;
        }

        UsdGeomXformCache cache(UsdTimeCode::Default());
        const GfMatrix4d world = cache.GetLocalToWorldTransform(prim);

        // Fall back to the transformed prim origin for arbitrary xform stacks.
        pivot = world.Transform(GfVec3d(0.0));

        const TfToken pivotToken("xformOp:translate:pivot");
        const TfToken inversePivotToken("!invert!xformOp:translate:pivot");
        const TfToken orderToken("xformOpOrder");

        GfVec3d localPivot(0.0);
        bool foundPivot = false;

        // author a stronger xformOp:transform while the original
        // artist pivot remains in a weaker payload, reference, or sublayer.
        // walk the prim stack and only accept the standard paired USD pivot.
        for (const SdfPrimSpecHandle& primSpec : prim.GetPrimStack()) {
            if (!primSpec)
                continue;

            const SdfLayerHandle layer = primSpec->GetLayer();
            if (!layer)
                continue;

            const SdfPath orderPath = primSpec->GetPath().AppendProperty(orderToken);
            if (!layer->HasField(orderPath, SdfFieldKeys->Default))
                continue;

            const VtValue orderValue = layer->GetField(orderPath, SdfFieldKeys->Default);
            if (!orderValue.IsHolding<VtTokenArray>())
                continue;

            const VtTokenArray& order = orderValue.UncheckedGet<VtTokenArray>();

            int pivotIndex = -1;
            int inversePivotIndex = -1;

            for (int i = 0; i < static_cast<int>(order.size()); ++i) {
                if (order[i] == pivotToken)
                    pivotIndex = i;
                else if (order[i] == inversePivotToken)
                    inversePivotIndex = i;
            }

            if (pivotIndex < 0 || inversePivotIndex <= pivotIndex)
                continue;

            const SdfPath pivotPath = primSpec->GetPath().AppendProperty(pivotToken);
            VtValue pivotValue;

            if (layer->HasField(pivotPath, SdfFieldKeys->Default)) {
                pivotValue = layer->GetField(pivotPath, SdfFieldKeys->Default);
            }
            else {
                const UsdAttribute pivotAttr = prim.GetAttribute(pivotToken);
                if (pivotAttr)
                    pivotAttr.Get(&pivotValue, UsdTimeCode::Default());
            }

            if (pivotValue.IsHolding<GfVec3d>()) {
                localPivot = pivotValue.UncheckedGet<GfVec3d>();
                foundPivot = true;
            }
            else if (pivotValue.IsHolding<GfVec3f>()) {
                const GfVec3f value = pivotValue.UncheckedGet<GfVec3f>();
                localPivot = GfVec3d(value[0], value[1], value[2]);
                foundPivot = true;
            }

            if (foundPivot)
                break;
        }

        if (!foundPivot)
            return true;

        // map the original artist pivot through the current composed transform
        // so it remains attached after Stageviz authors a matrix override.
        pivot = world.Transform(localPivot);
        return true;
    }

    bool setWorldTransform(UsdStageRefPtr stage, const SdfPath& path, const GfMatrix4d& matrix, QString& error)
    {
        if (!stage) {
            error = "stage missing";
            return false;
        }

        const UsdPrim prim = stage->GetPrimAtPath(path);
        if (!prim || !prim.IsValid()) {
            error = QString("prim missing: %1").arg(qt::SdfPathToQString(path));
            return false;
        }

        UsdGeomXformable xformable(prim);
        if (!xformable) {
            error = QString("prim is not xformable: %1").arg(qt::SdfPathToQString(path));
            return false;
        }

        GfMatrix4d parentWorld(1.0);
        const UsdPrim parent = prim.GetParent();
        if (parent && !parent.IsPseudoRoot()) {
            UsdGeomXformCache cache(UsdTimeCode::Default());
            parentWorld = cache.GetLocalToWorldTransform(parent);
        }

        const GfMatrix4d local = matrix * parentWorld.GetInverse();

        QString editError;
        const SdfLayerHandle editLayer = editlayer::opened(stage, editError);
        if (!editLayer) {
            error = editError;
            return false;
        }

        GfVec3d pivotValue(0.0);
        const bool preservePivot = localPivot(xformable, pivotValue);

        UsdEditContext context(stage, UsdEditTarget(editLayer));

        if (preservePivot)
            return setLocalMatrixWithPivot(xformable, local, pivotValue, error);

        UsdGeomXformOp op = xformable.MakeMatrixXform();
        if (!op) {
            error = "could not create matrix xform op";
            return false;
        }

        if (!op.Set(local, UsdTimeCode::Default())) {
            error = "USD rejected transform matrix";
            return false;
        }

        return true;
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

    SdfPath buildUniqueXform(UsdStageRefPtr stage, const QString& name, const SdfPath& parentPath)
    {
        if (!stage)
            return {};

        QString error;
        const SdfPath path = buildChildPath(stage, parentPath, name, error);
        if (path.IsEmpty())
            return {};

        const UsdGeomXform xform = UsdGeomXform::Define(stage, path);
        if (!xform)
            return {};

        return path;
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

    bool movePrim(UsdStageRefPtr stage, const SdfPath& from, const SdfPath& toParent, QString& error)
    {
        if (from.IsEmpty() || toParent.IsEmpty()) {
            error = "invalid path";
            return false;
        }
        return movePrims(stage, { qMakePair(from, toParent.AppendChild(from.GetNameToken())) }, error);
    }

    bool movePrims(UsdStageRefPtr stage, const QList<QPair<SdfPath, SdfPath>>& moves, QString& error)
    {
        if (!stage) {
            error = "invalid stage";
            return false;
        }

        if (moves.isEmpty())
            return true;

        QString editError;
        const SdfLayerHandle editLayer = editlayer::opened(stage, editError);

        if (!editLayer) {
            error = editError;
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
            if (!editlayer::validatePrim(stage, from, validationError)) {
                error = validationError;
                return false;
            }

            if (!editlayer::validateParent(stage, to.GetParentPath(), validationError)) {
                error = validationError;
                return false;
            }

            const SdfPath fromParentPath = from.GetParentPath();
            const SdfPath toParentPath = to.GetParentPath();

            if ((fromParentPath != SdfPath::AbsoluteRootPath() && isInsideCompositionArc(stage, fromParentPath))
                || (toParentPath != SdfPath::AbsoluteRootPath() && isInsideCompositionArc(stage, toParentPath))) {
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

        if (!editLayer->CanApply(edits)) {
            error = "edit layer cannot apply namespace edit";
            return false;
        }

        if (!editLayer->Apply(edits)) {
            error = "edit layer namespace edit failed";
            return false;
        }
        stage->SetLoadRules(loadRules);
        return true;
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

    QList<SdfPath> visiblePaths(UsdStageRefPtr stage)
    {
        QList<SdfPath> paths;
        if (!stage)
            return paths;

        QSet<SdfPath> uniquePaths;

        UsdPrimRange range(stage->GetPseudoRoot());
        for (auto it = range.begin(); it != range.end(); ++it) {
            const UsdPrim prim = *it;
            if (!prim || !prim.IsValid())
                continue;

            const UsdGeomImageable imageable(prim);
            if (imageable) {
                TfToken visibility;
                imageable.GetVisibilityAttr().Get(&visibility);

                if (visibility == UsdGeomTokens->invisible) {
                    it.PruneChildren();
                    continue;
                }
            }

            if (!prim.IsA<UsdGeomGprim>())
                continue;

            uniquePaths.insert(prim.GetPath());

            const UsdShadeMaterialBindingAPI bindingApi(prim);
            const UsdShadeMaterial material = bindingApi.ComputeBoundMaterial();

            if (material) {
                const UsdPrim materialPrim = material.GetPrim();
                if (materialPrim && materialPrim.IsValid())
                    uniquePaths.insert(materialPrim.GetPath());
            }
        }

        paths.reserve(uniquePaths.size());

        for (const SdfPath& path : uniquePaths)
            paths.append(path);

        return paths;
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
        const SdfLayerHandle editLayer = editlayer::opened(stage, error);
        if (!editLayer)
            return;

        if (parentPath == SdfPath::AbsoluteRootPath()) {
            editLayer->SetRootPrimOrder(childOrder);
            return;
        }

        ensureParentSpecs(editLayer, parentPath);
        if (!editLayer->GetPrimAtPath(parentPath))
            SdfCreatePrimInLayer(editLayer, parentPath);

        const UsdPrim parent = stage->GetPrimAtPath(parentPath);
        if (parent) {
            UsdEditContext context(stage, stage->GetEditTarget());
            parent.SetChildrenReorder(childOrder);
        }
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
        if (!stage)
            return;

        QString error;
        const SdfLayerHandle editLayer = editlayer::opened(stage, error);
        if (!editLayer)
            return;

        // visibility is a property opinion. Always author it into the opened
        // edit layer so composed assets are overridden without editing them.
        UsdEditContext context(stage, stage->GetEditTarget());

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
