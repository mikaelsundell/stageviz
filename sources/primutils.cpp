// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#include "primutils.h"
#include "payloadutils.h"
#include "qtutils.h"
#include "usdutils.h"
#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <pxr/base/gf/vec3d.h>
#include <pxr/base/gf/vec3f.h>
#include <pxr/base/vt/array.h>
#include <pxr/base/vt/value.h>
#include <pxr/usd/sdf/copyUtils.h>
#include <pxr/usd/sdf/namespaceEdit.h>
#include <pxr/usd/sdf/primSpec.h>
#include <pxr/usd/usd/editContext.h>
#include <pxr/usd/usd/editTarget.h>
#include <pxr/usd/usd/prim.h>
#include <pxr/usd/usd/primRange.h>
#include <pxr/usd/usd/variantSets.h>
#include <pxr/usd/usdGeom/bboxCache.h>
#include <pxr/usd/usdGeom/gprim.h>
#include <pxr/usd/usdGeom/imageable.h>
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
    void ensureParentPrimSpecs(const SdfLayerHandle& layer, const SdfPath& path)
    {
        if (!layer)
            return;

        const SdfPath parent = path.GetParentPath();
        if (parent.IsEmpty() || parent == SdfPath::AbsoluteRootPath())
            return;

        if (!layer->GetPrimAtPath(parent)) {
            ensureParentPrimSpecs(layer, parent);
            SdfCreatePrimInLayer(layer, parent);
        }
    }

}  // namespace

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

            ensureParentPrimSpecs(snapshotLayer, specPath);
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

        ensureParentPrimSpecs(dstLayer, state.stagePath);
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

    namespace {

        bool matricesClose(const GfMatrix4d& a, const GfMatrix4d& b, double tolerance = 1.0e-8)
        {
            for (int row = 0; row < 4; ++row) {
                for (int column = 0; column < 4; ++column) {
                    if (std::abs(a[row][column] - b[row][column]) > tolerance)
                        return false;
                }
            }
            return true;
        }

        bool readLocalPivot(const UsdGeomXformable& xformable, GfVec3d& pivot)
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

            UsdGeomXformOp::Precision pivotPrecision = UsdGeomXformOp::PrecisionDouble;
            const UsdAttribute existingPivot = xformable.GetPrim().GetAttribute(TfToken("xformOp:translate:pivot"));
            if (existingPivot && existingPivot.GetTypeName() == SdfValueTypeNames->Float3)
                pivotPrecision = UsdGeomXformOp::PrecisionFloat;

            xformable.ClearXformOpOrder();

            const UsdGeomXformOp pivotOp = xformable.AddTranslateOp(pivotPrecision, TfToken("pivot"), false);
            const UsdGeomXformOp matrixOp = xformable.AddTransformOp(UsdGeomXformOp::PrecisionDouble);
            const UsdGeomXformOp inversePivotOp = xformable.AddTranslateOp(pivotPrecision, TfToken("pivot"), true);

            if (!pivotOp || !matrixOp || !inversePivotOp) {
                error = "failed to preserve pivot transform ops";
                return false;
            }

            const bool pivotSet = pivotPrecision == UsdGeomXformOp::PrecisionFloat
                                      ? pivotOp.Set(GfVec3f(static_cast<float>(pivotValue[0]),
                                                            static_cast<float>(pivotValue[1]),
                                                            static_cast<float>(pivotValue[2])),
                                                    UsdTimeCode::Default())
                                      : pivotOp.Set(pivotValue, UsdTimeCode::Default());

            if (!pivotSet) {
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

                if (matricesClose(evaluated, localMatrix))
                    return true;
            }

            error = "failed to preserve local transform while retaining pivot";
            return false;
        }

    }  // namespace

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

    SdfPath parentPath(UsdStageRefPtr stage, const SdfPath& path)
    {
        if (!stage || path.IsEmpty())
            return {};

        const SdfPath primPath = path.IsPropertyPath() ? path.GetPrimPath() : path;
        if (primPath.IsEmpty() || primPath == SdfPath::AbsoluteRootPath())
            return {};

        const UsdPrim prim = stage->GetPrimAtPath(primPath);
        if (!prim || !prim.IsValid())
            return {};

        const UsdPrim parent = prim.GetParent();
        if (!parent)
            return {};

        return parent.GetPath();
    }

    bool isTransformEditable(UsdStageRefPtr stage, const SdfPath& path)
    {
        if (!stage || path.IsEmpty() || path == SdfPath::AbsoluteRootPath())
            return false;

        const UsdPrim prim = stage->GetPrimAtPath(path);

        if (!prim || !prim.IsValid() || prim.IsInstanceProxy() || !UsdGeomXformable(prim))
            return false;

        // Transform editing is a property override, not a namespace edit.
        // Composed prims may therefore receive a stronger edit-target opinion.
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

        const SdfLayerHandle layer = stage->GetEditTarget().GetLayer();
        if (!layer) {
            error = "edit layer missing";
            return false;
        }

        GfVec3d pivotValue(0.0);
        const bool preservePivot = readLocalPivot(xformable, pivotValue);

        UsdEditContext context(stage, stage->GetEditTarget());

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

    QList<SdfPath> filterStrongestLayerPaths(UsdStageRefPtr stage, const SdfLayerHandle& layer,
                                             const QList<SdfPath>& paths)
    {
        QList<SdfPath> result;

        if (!stage)
            return result;

        for (const SdfPath& path : paths) {
            const SdfPath primPath = path.IsPropertyPath() ? path.GetPrimPath() : path;
            if (isStrongestInLayer(stage, layer, primPath))
                result.append(primPath);
        }
        return result;
    }

    VariantTargets variantTargets(UsdStageRefPtr stage, const QList<SdfPath>& paths, bool recursive)
    {
        VariantTargets result;
        if (!stage)
            return result;

        QSet<SdfPath> visited;

        auto collectPrim = [&](const UsdPrim& prim) {
            if (!prim || !prim.IsValid() || prim.IsPseudoRoot())
                return;

            const SdfPath primPath = prim.GetPath();
            if (visited.contains(primPath))
                return;

            visited.insert(primPath);

            if (!prim.HasVariantSets())
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

        QList<SdfPath> roots = path::topLevelPaths(path::uniquePaths(paths));
        if (roots.isEmpty())
            roots.append(SdfPath::AbsoluteRootPath());

        for (const SdfPath& inputPath : roots) {
            const SdfPath primPath = inputPath.IsPropertyPath() ? inputPath.GetPrimPath() : inputPath;
            const UsdPrim root = primPath == SdfPath::AbsoluteRootPath() ? stage->GetPseudoRoot()
                                                                         : stage->GetPrimAtPath(primPath);
            if (!root)
                continue;

            if (!root.IsPseudoRoot()) {
                collectPrim(root);

                for (UsdPrim ancestor = root.GetParent(); ancestor && !ancestor.IsPseudoRoot();
                     ancestor = ancestor.GetParent()) {
                    collectPrim(ancestor);
                }
            }

            if (!recursive)
                continue;

            for (const UsdPrim& prim : UsdPrimRange::AllPrims(root))
                collectPrim(prim);
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

        const SdfLayerHandle layer = stage->GetEditTarget().GetLayer();
        if (!layer)
            return false;

        if (!isAuthoredInLayer(stage, layer, path))
            return false;

        return true;
    }

    bool isAuthoredInLayer(UsdStageRefPtr stage, const SdfLayerHandle& layer, const SdfPath& path)
    {
        if (!stage)
            return false;

        if (!layer)
            return false;

        return layer->GetPrimAtPath(path) != nullptr;
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

    bool isStrongestInLayer(UsdStageRefPtr stage, const SdfLayerHandle& layer, const SdfPath& path)
    {
        if (!stage)
            return false;

        const UsdPrim prim = stage->GetPrimAtPath(path);
        if (!prim)
            return false;

        if (!layer)
            return false;

        const auto& stack = prim.GetPrimStack();
        if (stack.empty())
            return false;

        const SdfPrimSpecHandle& strongest = stack.front();
        if (!strongest)
            return false;

        return strongest->GetLayer() == layer;
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

    void restoreChildOrder(UsdStageRefPtr stage, const SdfPath& parentPath, const TfTokenVector& childOrder)
    {
        if (!stage || parentPath.IsEmpty())
            return;

        const SdfLayerHandle layer = stage->GetEditTarget().GetLayer();
        if (!layer)
            return;

        if (parentPath == SdfPath::AbsoluteRootPath()) {
            layer->SetRootPrimOrder(childOrder);
            return;
        }

        ensureParentPrimSpecs(layer, parentPath);
        if (!layer->GetPrimAtPath(parentPath))
            SdfCreatePrimInLayer(layer, parentPath);

        const UsdPrim parent = stage->GetPrimAtPath(parentPath);
        if (parent) {
            UsdEditContext context(stage, stage->GetEditTarget());
            parent.SetChildrenReorder(childOrder);
        }
    }

    void setVisible(UsdStageRefPtr stage, const QList<SdfPath>& paths, bool visible, bool recursive)
    {
        if (!stage)
            return;

        const SdfLayerHandle layer = stage->GetEditTarget().GetLayer();
        if (!layer)
            return;

        // Visibility is a property opinion authored into the stage's current
        // edit target so composed assets are overridden without editing them.
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
