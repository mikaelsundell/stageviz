// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#include "materialoverridesceneindex.h"
#include <pxr/imaging/hd/containerDataSourceEditor.h>
#include <pxr/imaging/hd/dataSource.h>
#include <pxr/imaging/hd/materialBindingSchema.h>
#include <pxr/imaging/hd/materialBindingsSchema.h>
#include <pxr/imaging/hd/materialConnectionSchema.h>
#include <pxr/imaging/hd/materialNetworkSchema.h>
#include <pxr/imaging/hd/materialNodeParameterSchema.h>
#include <pxr/imaging/hd/materialNodeSchema.h>
#include <pxr/imaging/hd/materialSchema.h>
#include <pxr/imaging/hd/retainedDataSource.h>
#include <pxr/imaging/hd/tokens.h>
#include <pxr/usdImaging/usdImaging/tokens.h>

#include <algorithm>
#include <vector>

PXR_NAMESPACE_USING_DIRECTIVE

namespace stageviz {

class MaterialOverrideSceneIndexPrivate {
public:
    void init();
    bool active() const;
    bool usesClayMaterial() const;
    bool isStagevizPath(const SdfPath& path) const;
    bool isGprim(const HdSceneIndexPrim& prim) const;
    SdfPath effectiveMaterialPath() const;
    HdContainerDataSourceHandle createMaterialBindings(const SdfPath& materialPath) const;
    HdSceneIndexPrim createClayMaterialPrim() const;

public:
    struct Data {
        bool sceneMaterialsEnabled = true;
        MaterialOverrideSceneIndex::Mode mode = MaterialOverrideSceneIndex::None;
        SdfPath materialPath;

        SdfPath stagevizPath;
        SdfPath materialOverridePath;
        SdfPath clayMaterialPath;
        SdfPath clayPreviewSurfacePath;

        GfVec3f clayColor;
        HdSceneIndexPrim clayMaterialPrim;
    };

    Data d;
};

void
MaterialOverrideSceneIndexPrivate::init()
{
    d.stagevizPath = SdfPath("/__stageviz");
    d.materialOverridePath = d.stagevizPath.AppendPath(SdfPath("MaterialOverride"));
    d.clayMaterialPath = d.materialOverridePath.AppendPath(SdfPath("Clay"));
    d.clayPreviewSurfacePath = d.clayMaterialPath.AppendPath(SdfPath("PreviewSurface"));

    // Warm reddish clay similar to traditional DCC clay/default shading.
    d.clayColor = GfVec3f(0.55f, 0.18f, 0.16f);
    d.clayMaterialPrim = createClayMaterialPrim();
}

bool
MaterialOverrideSceneIndexPrivate::active() const
{
    if (d.mode == MaterialOverrideSceneIndex::Clay)
        return true;

    if (d.mode == MaterialOverrideSceneIndex::Custom)
        return !d.materialPath.IsEmpty();

    return !d.sceneMaterialsEnabled;
}

bool
MaterialOverrideSceneIndexPrivate::usesClayMaterial() const
{
    return d.mode == MaterialOverrideSceneIndex::Clay;
}

bool
MaterialOverrideSceneIndexPrivate::isStagevizPath(const SdfPath& path) const
{
    return path == d.stagevizPath || path.HasPrefix(d.stagevizPath);
}

bool
MaterialOverrideSceneIndexPrivate::isGprim(const HdSceneIndexPrim& prim) const
{
    const TfToken& type = prim.primType;

    return type == HdPrimTypeTokens->mesh || type == HdPrimTypeTokens->basisCurves || type == HdPrimTypeTokens->points
           || type == HdPrimTypeTokens->cube || type == HdPrimTypeTokens->sphere || type == HdPrimTypeTokens->cylinder
           || type == HdPrimTypeTokens->cone || type == HdPrimTypeTokens->capsule;
}

SdfPath
MaterialOverrideSceneIndexPrivate::effectiveMaterialPath() const
{
    if (d.mode == MaterialOverrideSceneIndex::Clay)
        return d.clayMaterialPath;

    if (d.mode == MaterialOverrideSceneIndex::Custom)
        return d.materialPath;

    return SdfPath();
}

HdContainerDataSourceHandle
MaterialOverrideSceneIndexPrivate::createMaterialBindings(const SdfPath& materialPath) const
{
    using PathDataSource = HdRetainedTypedSampledDataSource<SdfPath>;

    TfTokenVector purposes;
    std::vector<HdDataSourceBaseHandle> bindings;

    purposes.push_back(HdMaterialBindingsSchemaTokens->allPurpose);
    bindings.push_back(HdMaterialBindingSchema::Builder().SetPath(PathDataSource::New(materialPath)).Build());

    return HdMaterialBindingsSchema::BuildRetained(purposes.size(), purposes.data(), bindings.data());
}

HdSceneIndexPrim
MaterialOverrideSceneIndexPrivate::createClayMaterialPrim() const
{
    using TokenDataSource = HdRetainedTypedSampledDataSource<TfToken>;
    using FloatDataSource = HdRetainedTypedSampledDataSource<float>;
    using Vec3fDataSource = HdRetainedTypedSampledDataSource<GfVec3f>;

    const auto one = HdMaterialNodeParameterSchema::Builder().SetValue(FloatDataSource::New(1.0f)).Build();

    const auto zero = HdMaterialNodeParameterSchema::Builder().SetValue(FloatDataSource::New(0.0f)).Build();

    const auto roughness = HdMaterialNodeParameterSchema::Builder().SetValue(FloatDataSource::New(0.68f)).Build();

    const auto color = HdMaterialNodeParameterSchema::Builder().SetValue(Vec3fDataSource::New(d.clayColor)).Build();

    const auto surfaceNode = HdMaterialNodeSchema::Builder()
                                 .SetNodeIdentifier(TokenDataSource::New(UsdImagingTokens->UsdPreviewSurface))
                                 .SetParameters(HdRetainedContainerDataSource::New(TfToken("diffuseColor"), color,
                                                                                   TfToken("metallic"), zero,
                                                                                   TfToken("roughness"), roughness,
                                                                                   TfToken("opacity"), one))
                                 .SetInputConnections(HdRetainedContainerDataSource::New())
                                 .Build();

    const auto terminals = HdRetainedContainerDataSource::New(
        HdMaterialTerminalTokens->surface,
        HdMaterialConnectionSchema::Builder()
            .SetUpstreamNodePath(TokenDataSource::New(d.clayPreviewSurfacePath.GetToken()))
            .SetUpstreamNodeOutputName(TokenDataSource::New(HdMaterialTerminalTokens->surface))
            .Build());

    std::vector<TfToken> networkNames;
    std::vector<HdDataSourceBaseHandle> networks;

    networkNames.push_back(HdMaterialSchemaTokens->universalRenderContext);
    networks.push_back(
        HdMaterialNetworkSchema::Builder()
            .SetNodes(HdRetainedContainerDataSource::New(d.clayPreviewSurfacePath.GetToken(), surfaceNode))
            .SetTerminals(terminals)
            .Build());

    const auto material = HdRetainedContainerDataSource::New(HdMaterialSchemaTokens->material,
                                                             HdMaterialSchema::BuildRetained(networkNames.size(),
                                                                                             networkNames.data(),
                                                                                             networks.data()));

    return { HdPrimTypeTokens->material, material };
}



void
MaterialOverrideSceneIndex::dirtyMaterialBindings()
{
    if (!_IsObserved())
        return;

    HdSceneIndexObserver::DirtiedPrimEntries entries;
    HdDataSourceLocatorSet locators;
    locators.insert(HdMaterialBindingsSchema::GetDefaultLocator());

    std::vector<SdfPath> pending { SdfPath::AbsoluteRootPath() };
    while (!pending.empty()) {
        const SdfPath path = pending.back();
        pending.pop_back();

        const SdfPathVector children = _GetInputSceneIndex()->GetChildPrimPaths(path);
        for (const SdfPath& child : children) {
            pending.push_back(child);

            if (p->isStagevizPath(child))
                continue;

            const HdSceneIndexPrim prim = _GetInputSceneIndex()->GetPrim(child);
            if (p->isGprim(prim))
                entries.push_back({ child, locators });
        }
    }

    if (!entries.empty())
        _SendPrimsDirtied(entries);
}

void
MaterialOverrideSceneIndex::updateClayTopology(bool wasUsingClay)
{
    const bool isUsingClay = p->usesClayMaterial();
    if (wasUsingClay == isUsingClay || !_IsObserved())
        return;

    if (isUsingClay) {
        _SendPrimsAdded({
            { p->d.materialOverridePath, TfToken() },
            { p->d.clayMaterialPath, HdPrimTypeTokens->material },
        });
    }
    else {
        _SendPrimsRemoved({
            { p->d.clayMaterialPath },
            { p->d.materialOverridePath },
        });
    }
}

TfRefPtr<MaterialOverrideSceneIndex>
MaterialOverrideSceneIndex::New(const HdSceneIndexBaseRefPtr& inputSceneIndex)
{
    return TfCreateRefPtr(new MaterialOverrideSceneIndex(inputSceneIndex));
}

MaterialOverrideSceneIndex::MaterialOverrideSceneIndex(const HdSceneIndexBaseRefPtr& inputSceneIndex)
    : HdSingleInputFilteringSceneIndexBase(inputSceneIndex)
    , p(new MaterialOverrideSceneIndexPrivate())
{
    SetDisplayName("MaterialOverrideSceneIndex");
    p->init();
}

MaterialOverrideSceneIndex::~MaterialOverrideSceneIndex() = default;

bool
MaterialOverrideSceneIndex::sceneMaterialsEnabled() const
{
    return p->d.sceneMaterialsEnabled;
}

void
MaterialOverrideSceneIndex::setSceneMaterialsEnabled(bool enabled)
{
    if (enabled == p->d.sceneMaterialsEnabled)
        return;

    p->d.sceneMaterialsEnabled = enabled;
    dirtyMaterialBindings();
}

MaterialOverrideSceneIndex::Mode
MaterialOverrideSceneIndex::mode() const
{
    return p->d.mode;
}

void
MaterialOverrideSceneIndex::setMode(Mode mode)
{
    if (mode == p->d.mode)
        return;

    const bool wasUsingClay = p->usesClayMaterial();
    p->d.mode = mode;

    updateClayTopology(wasUsingClay);
    dirtyMaterialBindings();
}

SdfPath
MaterialOverrideSceneIndex::materialPath() const
{
    return p->d.materialPath;
}

void
MaterialOverrideSceneIndex::setMaterialPath(const SdfPath& materialPath)
{
    if (materialPath == p->d.materialPath)
        return;

    p->d.materialPath = materialPath;

    if (p->d.mode == Custom)
        dirtyMaterialBindings();
}

HdSceneIndexPrim
MaterialOverrideSceneIndex::GetPrim(const SdfPath& primPath) const
{
    if (p->usesClayMaterial()) {
        if (primPath == p->d.materialOverridePath)
            return { TfToken(), HdRetainedContainerDataSource::New() };

        if (primPath == p->d.clayMaterialPath)
            return p->d.clayMaterialPrim;
    }

    HdSceneIndexPrim prim = _GetInputSceneIndex()->GetPrim(primPath);

    if (!p->isGprim(prim))
        return prim;

    if (p->isStagevizPath(primPath))
        return prim;

    if (!p->active() || !prim.dataSource)
        return prim;

    HdContainerDataSourceEditor editor(prim.dataSource);

    if (p->d.mode == Clay || p->d.mode == Custom) {
        const SdfPath materialPath = p->effectiveMaterialPath();

        if (!materialPath.IsEmpty()) {
            editor.Set(HdMaterialBindingsSchema::GetDefaultLocator(), p->createMaterialBindings(materialPath));
        }
    }
    else if (!p->d.sceneMaterialsEnabled) {
        editor.Set(HdMaterialBindingsSchema::GetDefaultLocator(), HdBlockDataSource::New());
    }

    prim.dataSource = editor.Finish();
    return prim;
}

SdfPathVector
MaterialOverrideSceneIndex::GetChildPrimPaths(const SdfPath& primPath) const
{
    SdfPathVector children = _GetInputSceneIndex()->GetChildPrimPaths(primPath);

    if (!p->usesClayMaterial())
        return children;

    auto appendUnique = [&](const SdfPath& path) {
        if (std::find(children.begin(), children.end(), path) == children.end())
            children.push_back(path);
    };

    if (primPath == p->d.stagevizPath)
        appendUnique(p->d.materialOverridePath);
    else if (primPath == p->d.materialOverridePath)
        appendUnique(p->d.clayMaterialPath);

    return children;
}

void
MaterialOverrideSceneIndex::_PrimsAdded(const HdSceneIndexBase& sender,
                                        const HdSceneIndexObserver::AddedPrimEntries& entries)
{
    _SendPrimsAdded(entries);
}

void
MaterialOverrideSceneIndex::_PrimsRemoved(const HdSceneIndexBase& sender,
                                          const HdSceneIndexObserver::RemovedPrimEntries& entries)
{
    _SendPrimsRemoved(entries);
}

void
MaterialOverrideSceneIndex::_PrimsDirtied(const HdSceneIndexBase& sender,
                                          const HdSceneIndexObserver::DirtiedPrimEntries& entries)
{
    _SendPrimsDirtied(entries);
}

void
MaterialOverrideSceneIndex::_PrimsRenamed(const HdSceneIndexBase& sender,
                                          const HdSceneIndexObserver::RenamedPrimEntries& entries)
{
    _SendPrimsRenamed(entries);
}

}  // namespace stageviz
