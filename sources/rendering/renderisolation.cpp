// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell

#include "renderisolation.h"
#include "perfscope.h"
#include <algorithm>
#include <atomic>
#include <pxr/base/tf/notice.h>
#include <pxr/base/tf/weakBase.h>
#include <pxr/base/tf/weakPtr.h>
#include <pxr/usd/usd/notice.h>
#include <pxr/usd/usd/primRange.h>
#include <pxr/usd/usdGeom/boundable.h>
#include <pxr/usd/usdGeom/gprim.h>
#include <pxr/usd/usdLux/lightAPI.h>
#include <pxr/usd/usdShade/materialBindingAPI.h>
#include <vector>

PXR_NAMESPACE_USING_DIRECTIVE

namespace stageviz {

class RenderIsolationPrivate : public TfWeakBase {
public:
    ~RenderIsolationPrivate();
    void objectsChanged(const UsdNotice::ObjectsChanged& notice, const UsdStageWeakPtr& sender);
    void synchronize();

    struct Data {
        UsdStageRefPtr source;
        UsdStageRefPtr rendered;
        QList<SdfPath> mask;
        TfNotice::Key notice;
        SdfPathVector lights;
        std::atomic<bool> lightsDirty { true };
        std::atomic<bool> dirty { true };
        std::atomic<uint64_t> sourceVersion { 1 };
    } d;
};

RenderIsolationPrivate::~RenderIsolationPrivate() { TfNotice::Revoke(d.notice); }

void
RenderIsolationPrivate::objectsChanged(const UsdNotice::ObjectsChanged& notice, const UsdStageWeakPtr&)
{
    // Values and transforms already flow through the shared layers. Only
    // composition and dependency changes require rebuilding the population.
    // A deleted dependency property can no longer be classified by querying
    // the source stage, so property resyncs must also invalidate the population.
    if (!notice.GetResyncedPaths().empty()) {
        d.dirty = true;
        ++d.sourceVersion;
    }
    for (const auto& paths : { notice.GetResyncedPaths(), notice.GetChangedInfoOnlyPaths() }) {
        for (const SdfPath& path : paths) {
            if (!path.IsPropertyPath()) {
                d.lightsDirty = true;
                d.dirty = true;
                return;
            }
            if (d.source->GetRelationshipAtPath(path)) {
                d.dirty = true;
                ++d.sourceVersion;
            }
            const auto fields = notice.GetChangedFields(path);
            if (std::find(fields.begin(), fields.end(), TfToken("connectionPaths")) != fields.end()) {
                d.dirty = true;
                ++d.sourceVersion;
            }
        }
    }
}

void
RenderIsolationPrivate::synchronize()
{
    if (!d.source || d.mask.isEmpty())
        return;
    if (!d.dirty.exchange(false))
        return;

    PerfScope scope("viewport.isolation_population");
    UsdStagePopulationMask population;
    UsdShadeMaterialBindingAPI::BindingsCache bindings;
    UsdShadeMaterialBindingAPI::CollectionQueryCache collections;
    for (const SdfPath& path : d.mask) {
        const SdfPath root = path.IsPropertyPath() ? path.GetPrimPath() : path;
        if (!root.IsAbsolutePath() || (!root.IsPrimPath() && !root.IsAbsoluteRootPath()))
            continue;
        population.Add(root);
        const UsdPrim prim = d.source->GetPrimAtPath(root);
        if (!prim)
            continue;
        for (const UsdPrim& child : UsdPrimRange(prim, UsdTraverseInstanceProxies())) {
            if (!UsdGeomBoundable(child))
                continue;
            if (const UsdShadeMaterial material = UsdShadeMaterialBindingAPI(child).ComputeBoundMaterial(&bindings,
                                                                                                         &collections))
                population.Add(material.GetPath());
        }
    }
    // Isolation is a geometry restriction; retain scene lighting just as
    // the existing render collection does. Do not include mesh-light
    // geometry outside the selected population.
    if (d.lightsDirty.exchange(false)) {
        d.lights.clear();
        for (const UsdPrim& prim : d.source->Traverse(UsdTraverseInstanceProxies())) {
            if (UsdLuxLightAPI(prim) && !UsdGeomGprim(prim))
                d.lights.push_back(prim.GetPath());
        }
    }
    for (const SdfPath& light : d.lights)
        population.Add(light);

    population = population.GetIntersection(d.source->GetPopulationMask());
    if (!d.rendered) {
        d.rendered = UsdStage::OpenMasked(d.source->GetRootLayer(), d.source->GetSessionLayer(),
                                          d.source->GetPathResolverContext(), population, UsdStage::LoadNone);
    }
    else {
        d.rendered->SetPopulationMask(population);
    }
    if (!d.rendered)
        return;

    const auto& muted = d.source->GetMutedLayers();
    std::vector<std::string> unmute;
    for (const auto& layer : d.rendered->GetMutedLayers()) {
        if (std::find(muted.begin(), muted.end(), layer) == muted.end())
            unmute.push_back(layer);
    }
    d.rendered->MuteAndUnmuteLayers(muted, unmute);
    d.rendered->SetLoadRules(d.source->GetLoadRules());
    // Shader connections can lead outside a bound material's namespace. Do
    // not follow arbitrary relationships that would pull unrelated geometry
    // into the isolation population.
    d.rendered->ExpandPopulationMask(UsdTraverseInstanceProxies(), [](const UsdRelationship& relationship) {
        return relationship.GetName().GetString().find("material:binding") == 0;
    });
}

RenderIsolation::RenderIsolation()
    : p(std::make_unique<RenderIsolationPrivate>())
{}

RenderIsolation::~RenderIsolation() = default;

void
RenderIsolation::setStage(const UsdStageRefPtr& stage)
{
    if (p->d.source == stage)
        return;
    TfNotice::Revoke(p->d.notice);
    p->d.source = stage;
    ++p->d.sourceVersion;
    p->d.rendered = nullptr;
    p->d.lights.clear();
    p->d.lightsDirty = true;
    p->d.dirty = true;
    if (stage)
        p->d.notice = TfNotice::Register(TfWeakPtr<RenderIsolationPrivate>(p.get()),
                                         &RenderIsolationPrivate::objectsChanged, stage);
}

void
RenderIsolation::setMask(const QList<SdfPath>& paths)
{
    if (p->d.mask == paths)
        return;
    p->d.mask = paths;
    p->d.dirty = true;
}

UsdStageRefPtr
RenderIsolation::stage()
{
    p->synchronize();
    return p->d.mask.isEmpty() ? p->d.source : p->d.rendered;
}

bool
RenderIsolation::active() const
{
    return p->d.rendered && !p->d.mask.isEmpty();
}

uint64_t
RenderIsolation::sourceVersion() const
{
    return p->d.sourceVersion.load();
}

}  // namespace stageviz
