// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 - present Mikael Sundell

#include "renderdiagnostics.h"
#include <QJsonArray>
#include <cmath>
#include <pxr/base/gf/bbox3d.h>
#include <pxr/base/gf/frustum.h>
#include <pxr/base/tf/debug.h>
#include <pxr/imaging/hd/bufferArrayRange.h>
#include <pxr/imaging/hd/changeTracker.h>
#include <pxr/imaging/hd/renderIndex.h>
#include <pxr/imaging/hd/rprim.h>
#include <pxr/imaging/hd/sceneDelegate.h>
#include <pxr/imaging/hd/tokens.h>
#include <pxr/imaging/hd/visibilitySchema.h>
#include <pxr/imaging/hdSt/bufferArrayRange.h>
#include <pxr/imaging/hdSt/bufferResource.h>
#include <pxr/imaging/hdSt/drawItem.h>
#include <pxr/usd/usdGeom/imageable.h>
#include <pxr/usd/usdGeom/mesh.h>
#include <pxr/usd/usdGeom/xformCache.h>

PXR_NAMESPACE_USING_DIRECTIVE

namespace stageviz {
namespace {
    QString tokenText(const TfToken& value) { return QString::fromStdString(value.GetString()); }
    QJsonArray vectorJson(const GfVec3d& value) { return { value[0], value[1], value[2] }; }
    QJsonArray matrixJson(const GfMatrix4d& value)
    {
        QJsonArray result;
        for (int row = 0; row < 4; ++row)
            result.append(QJsonArray { value[row][0], value[row][1], value[row][2], value[row][3] });
        return result;
    }
    QJsonObject boundsJson(const GfRange3d& value)
    {
        QJsonObject result { { "empty", value.IsEmpty() } };
        if (!value.IsEmpty()) {
            result.insert("min", vectorJson(value.GetMin()));
            result.insert("max", vectorJson(value.GetMax()));
        }
        return result;
    }
    QJsonObject scenePrimJson(const HdSceneIndexBaseRefPtr& scene, const SdfPath& path)
    {
        if (!scene)
            return { { "available", false } };
        const HdSceneIndexPrim prim = scene->GetPrim(path);
        QJsonObject result { { "available", bool(prim.dataSource) }, { "type", tokenText(prim.primType) } };
        if (!prim.dataSource)
            return result;
        const auto schema = HdVisibilitySchema::GetFromParent(prim.dataSource);
        const auto visibility = schema.GetVisibility();
        result.insert("visibilityAuthored", bool(visibility));
        // The Hydra scene delegate treats an absent visibility data source as visible.
        result.insert("visible", visibility ? visibility->GetTypedValue(0.0f) : true);
        return result;
    }
    QJsonObject rangeJson(const HdBufferArrayRangeSharedPtr& range)
    {
        if (!range)
            return { { "present", false } };
        QJsonObject result { { "present", true },
                             { "valid", range->IsValid() },
                             { "assigned", range->IsAssigned() },
                             { "elements", double(range->GetNumElements()) },
                             { "elementOffset", range->GetElementOffset() } };
        if (const auto stormRange = std::dynamic_pointer_cast<HdStBufferArrayRange>(range)) {
            QJsonArray resources;
            for (const auto& entry : stormRange->GetResources()) {
                const auto& resource = entry.second;
                if (!resource)
                    continue;
                resources.append(QJsonObject { { "name", tokenText(entry.first) },
                                               { "bufferBytes", double(resource->GetSize()) },
                                               { "stride", resource->GetStride() },
                                               { "resourceOffset", resource->GetOffset() },
                                               { "handlePresent", bool(resource->GetHandle()) } });
            }
            result.insert("resources", resources);
        }
        return result;
    }
}  // namespace

QJsonObject
inspectRenderPrim(const UsdStageRefPtr& stage, HdRenderIndex* index, const HdSceneIndexBaseRefPtr& inputScene,
                  const SdfPath& usdPath, const SdfPathVector& hydraPaths, const GfCamera& camera, double timeCode,
                  const HdRprimCollection& collection)
{
    const UsdPrim prim = stage->GetPrimAtPath(usdPath);
    const UsdTimeCode time(timeCode);
    const UsdGeomImageable imageable(prim);
    QJsonObject usd { { "type", tokenText(prim.GetTypeName()) },
                      { "active", prim.IsActive() },
                      { "loaded", prim.IsLoaded() },
                      { "instanceProxy", prim.IsInstanceProxy() } };
    if (imageable) {
        const auto visibility = imageable.GetVisibilityAttr();
        std::vector<double> samples;
        visibility.GetTimeSamples(&samples);
        QJsonArray sampleTimes;
        for (double sample : samples)
            sampleTimes.append(sample);
        usd.insert("visibilityTimeSamples", sampleTimes);
        usd.insert("visibilityDefault", tokenText(imageable.ComputeVisibility(UsdTimeCode::Default())));
        usd.insert("visibilityAtViewportTime", tokenText(imageable.ComputeVisibility(time)));
        usd.insert("purpose", tokenText(imageable.ComputePurpose()));
    }
    UsdGeomXformCache cache(time);
    const GfMatrix4d world = cache.GetLocalToWorldTransform(prim);
    usd.insert("worldTransform", matrixJson(world));
    if (const UsdGeomMesh mesh { prim }) {
        VtVec3fArray points;
        VtIntArray counts, indices;
        mesh.GetPointsAttr().Get(&points, time);
        mesh.GetFaceVertexCountsAttr().Get(&counts, time);
        mesh.GetFaceVertexIndicesAttr().Get(&indices, time);
        usd.insert("points", double(points.size()));
        usd.insert("faces", double(counts.size()));
        usd.insert("faceVertexIndices", double(indices.size()));
        bool finite = true, valid = true;
        size_t expectedIndices = 0;
        for (int count : counts) {
            valid = valid && count >= 3;
            if (count >= 0)
                expectedIndices += size_t(count);
        }
        valid = valid && expectedIndices == indices.size();
        for (int i : indices)
            valid = valid && i >= 0 && size_t(i) < points.size();
        GfRange3d actualBounds;
        for (const auto& point : points) {
            const bool pointFinite = std::isfinite(point[0]) && std::isfinite(point[1]) && std::isfinite(point[2]);
            finite = finite && pointFinite;
            if (pointFinite)
                actualBounds.UnionWith(GfVec3d(point));
        }
        usd.insert("pointsFinite", finite);
        usd.insert("topologyValid", valid);
        usd.insert("pointsBoundsLocal", boundsJson(actualBounds));
        usd.insert("pointsBoundsWorld", boundsJson(GfBBox3d(actualBounds, world).ComputeAlignedRange()));
        VtVec3fArray extent;
        if (mesh.GetExtentAttr().Get(&extent, time) && extent.size() == 2)
            usd.insert("authoredExtent", QJsonArray { vectorJson(GfVec3d(extent[0])), vectorJson(GfVec3d(extent[1])) });
    }
    const GfFrustum frustum = camera.GetFrustum();
    const GfMatrix4d viewProjection = frustum.ComputeViewMatrix() * frustum.ComputeProjectionMatrix();
    const auto terminalScene = index->GetTerminalSceneIndex();
    QJsonArray rprims;
    for (const SdfPath& path : hydraPaths) {
        // Selection mapping includes material subsets, which are scene-index
        // children of a mesh but do not have their own render primitive.
        if (terminalScene && terminalScene->GetPrim(path).primType == HdPrimTypeTokens->geomSubset)
            continue;
        const HdRprim* rprim = index->GetRprim(path);
        QJsonObject row { { "path", QString::fromStdString(path.GetString()) },
                          { "present", bool(rprim) },
                          { "inputScene", scenePrimJson(inputScene, path) },
                          { "terminalScene", scenePrimJson(terminalScene, path) } };
        if (rprim) {
            row.insert("visible", rprim->IsVisible());
            row.insert("dirtyBits", double(index->GetChangeTracker().GetRprimDirtyBits(path)));
            row.insert("material", QString::fromStdString(rprim->GetMaterialId().GetString()));
            if (HdSceneDelegate* delegate = index->GetSceneDelegateForRprim(path))
                row.insert("delegateVisible", delegate->GetVisible(path));
            QJsonArray drawItems;
            bool truncated = false;
            const HdReprSelector selector = collection.IsForcedRepr()
                                                ? collection.GetReprSelector()
                                                : rprim->GetReprSelector().CompositeOver(collection.GetReprSelector());
            const auto dirtyBits = index->GetChangeTracker().GetRprimDirtyBits(path);
            const bool pendingRepr = dirtyBits & (HdChangeTracker::InitRepr | HdChangeTracker::DirtyRepr);
            row.insert("drawItemsPendingReprSync", pendingRepr);
            for (size_t slot = 0; !pendingRepr && slot < HdReprSelector::MAX_TOPOLOGY_REPRS; ++slot) {
                if (!selector.IsActiveRepr(slot))
                    continue;
                const TfToken& repr = selector[slot];
                for (const auto& item : rprim->GetDrawItems(repr)) {
                    if (drawItems.size() >= 256) {
                        truncated = true;
                        break;
                    }
                    const auto* stormItem = dynamic_cast<const HdStDrawItem*>(item.get());
                    QJsonObject draw { { "repr", tokenText(repr) },
                                       { "visible", item->GetVisible() },
                                       { "boundsWorld", boundsJson(item->GetBounds().ComputeAlignedRange()) },
                                       { "boundsLocal", boundsJson(item->GetBounds().GetRange()) },
                                       { "boundsTransform", matrixJson(item->GetBounds().GetMatrix()) },
                                       { "materialTag", tokenText(item->GetMaterialTag()) } };
                    if (stormItem) {
                        draw.insert("intersectsCameraFrustum", stormItem->IntersectsViewVolume(viewProjection));
                        draw.insert("hasInstancer", stormItem->HasInstancer());
                        draw.insert("vertexRange", rangeJson(stormItem->GetVertexPrimvarRange()));
                        draw.insert("topologyRange", rangeJson(stormItem->GetTopologyRange()));
                        draw.insert("faceVaryingRange", rangeJson(stormItem->GetFaceVaryingPrimvarRange()));
                        draw.insert("constantRange", rangeJson(stormItem->GetConstantPrimvarRange()));
                        draw.insert("instanceIndexRange", rangeJson(stormItem->GetInstanceIndexRange()));
                    }
                    drawItems.append(draw);
                }
                if (truncated)
                    break;
            }
            row.insert("drawItems", drawItems);
            row.insert("drawItemsTruncated", truncated);
        }
        rprims.append(row);
    }
    QJsonObject flags;
    for (const char* name : { "HD_FREEZE_CULL_FRUSTUM", "HDST_DISABLE_FRUSTUM_CULLING",
                              "HDST_DISABLE_MULTITHREADED_CULLING", "HDST_FORCE_DRAW_BATCH_REBUILD" })
        flags.insert(name, TfDebug::IsDebugSymbolNameEnabled(name));
    const auto clip = camera.GetClippingRange();
    return {
        { "path", QString::fromStdString(usdPath.GetString()) },
        { "timeCode", timeCode },
        { "usd", usd },
        { "rprims", rprims },
        { "debugFlags", flags },
        { "camera", QJsonObject { { "view", matrixJson(frustum.ComputeViewMatrix()) },
                                  { "projection", matrixJson(frustum.ComputeProjectionMatrix()) },
                                  { "clippingRange", QJsonArray { clip.GetMin(), clip.GetMax() } } } },
        { "note",
          "Hydra data is from the last synchronization; no rendering, buffer readback, or USD authoring was performed." }
    };
}
}  // namespace stageviz
