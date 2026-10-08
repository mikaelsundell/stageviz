// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#include "rendersceneindex.h"
#include "paths.h"
#include <QtGlobal>
#include <pxr/imaging/hd/containerDataSourceEditor.h>
#include <pxr/imaging/hd/dataSource.h>
#include <pxr/imaging/hd/materialBindingSchema.h>
#include <pxr/imaging/hd/materialBindingsSchema.h>
#include <pxr/imaging/hd/meshSchema.h>
#include <pxr/imaging/hd/retainedDataSource.h>
#include <pxr/imaging/hd/tokens.h>
#include <vector>

PXR_NAMESPACE_USING_DIRECTIVE

namespace stageviz {

class RenderSceneIndexPrivate {
public:
    bool active() const;
    bool isDisplayPath(const SdfPath& path) const;
    bool isGprim(const HdSceneIndexPrim& prim) const;
    bool isGeomSubset(const HdSceneIndexPrim& prim) const;
    bool isMaterialTarget(const HdSceneIndexPrim& prim) const;
    bool isMesh(const HdSceneIndexPrim& prim) const;
    SdfPath effectiveMaterialPath() const;
    HdContainerDataSourceHandle createMaterialBindings(const SdfPath& materialPath) const;

    struct Data {
        bool sceneMaterialsEnabled = true;
        RenderSceneIndex::Mode mode = RenderSceneIndex::None;
        SdfPath materialPath;
        SdfPath clayMaterialPath = paths::auxiliary::materials.AppendChild(TfToken("Clay"));
        bool doubleSidedOverrideEnabled = false;
        bool doubleSidedOverride = false;
    };
    Data d;
};

bool
RenderSceneIndexPrivate::active() const
{
    if (d.doubleSidedOverrideEnabled)
        return true;

    if (d.mode == RenderSceneIndex::Clay)
        return true;

    if (d.mode == RenderSceneIndex::Custom)
        return !d.materialPath.IsEmpty();

    return !d.sceneMaterialsEnabled;
}

bool
RenderSceneIndexPrivate::isDisplayPath(const SdfPath& path) const
{
    return path == paths::auxiliary::display || path.HasPrefix(paths::auxiliary::display);
}

bool
RenderSceneIndexPrivate::isGprim(const HdSceneIndexPrim& prim) const
{
    const TfToken& type = prim.primType;

    // Keep presentation overrides available for every geometry primitive that
    // Stageviz/Storm can render. Mesh-specific schema edits remain guarded by
    // isMesh() below.
    return type == HdPrimTypeTokens->mesh || type == HdPrimTypeTokens->basisCurves || type == HdPrimTypeTokens->points
           || type == HdPrimTypeTokens->nurbsPatch || type == HdPrimTypeTokens->plane || type == HdPrimTypeTokens->cube
           || type == HdPrimTypeTokens->sphere || type == HdPrimTypeTokens->cylinder || type == HdPrimTypeTokens->cone
           || type == HdPrimTypeTokens->capsule;
}

bool
RenderSceneIndexPrivate::isGeomSubset(const HdSceneIndexPrim& prim) const
{
    return prim.primType == HdPrimTypeTokens->geomSubset;
}

bool
RenderSceneIndexPrivate::isMaterialTarget(const HdSceneIndexPrim& prim) const
{
    // GeomSubset prims carry their own materialBindings in the Hydra scene
    // index. These bindings are more specific than the parent mesh binding,
    // so viewport Clay/Custom presentation and Scene Materials Off must
    // filter them too.
    return isGprim(prim) || isGeomSubset(prim);
}

bool
RenderSceneIndexPrivate::isMesh(const HdSceneIndexPrim& prim) const
{
    return prim.primType == HdPrimTypeTokens->mesh;
}

SdfPath
RenderSceneIndexPrivate::effectiveMaterialPath() const
{
    if (d.mode == RenderSceneIndex::Clay)
        return d.clayMaterialPath;

    if (d.mode == RenderSceneIndex::Custom)
        return d.materialPath;

    return {};
}

HdContainerDataSourceHandle
RenderSceneIndexPrivate::createMaterialBindings(const SdfPath& materialPath) const
{
    using PathDataSource = HdRetainedTypedSampledDataSource<SdfPath>;

    TfTokenVector purposes;
    std::vector<HdDataSourceBaseHandle> bindings;

    purposes.push_back(HdMaterialBindingsSchemaTokens->allPurpose);
    bindings.push_back(HdMaterialBindingSchema::Builder().SetPath(PathDataSource::New(materialPath)).Build());

    return HdMaterialBindingsSchema::BuildRetained(purposes.size(), purposes.data(), bindings.data());
}

TfRefPtr<RenderSceneIndex>
RenderSceneIndex::New(const HdSceneIndexBaseRefPtr& inputSceneIndex)
{
    return TfCreateRefPtr(new RenderSceneIndex(inputSceneIndex));
}

RenderSceneIndex::RenderSceneIndex(const HdSceneIndexBaseRefPtr& inputSceneIndex)
    : HdSingleInputFilteringSceneIndexBase(inputSceneIndex)
    , p(new RenderSceneIndexPrivate())
{
    SetDisplayName("RenderSceneIndex");
}

RenderSceneIndex::~RenderSceneIndex() = default;

bool
RenderSceneIndex::sceneMaterialsEnabled() const
{
    return p->d.sceneMaterialsEnabled;
}

void
RenderSceneIndex::setSceneMaterialsEnabled(bool enabled)
{
    if (enabled == p->d.sceneMaterialsEnabled)
        return;

    p->d.sceneMaterialsEnabled = enabled;
    dirtyMaterialBindings();
}

RenderSceneIndex::Mode
RenderSceneIndex::mode() const
{
    return p->d.mode;
}

void
RenderSceneIndex::setMode(Mode mode)
{
    if (mode == p->d.mode)
        return;

    p->d.mode = mode;
    dirtyMaterialBindings();
}

SdfPath
RenderSceneIndex::materialPath() const
{
    return p->d.materialPath;
}

void
RenderSceneIndex::setMaterialPath(const SdfPath& materialPath)
{
    if (materialPath == p->d.materialPath)
        return;

    p->d.materialPath = materialPath;

    if (p->d.mode == Custom)
        dirtyMaterialBindings();
}

bool
RenderSceneIndex::doubleSidedOverrideEnabled() const
{
    return p->d.doubleSidedOverrideEnabled;
}

void
RenderSceneIndex::setDoubleSidedOverrideEnabled(bool enabled)
{
    if (enabled == p->d.doubleSidedOverrideEnabled)
        return;

    p->d.doubleSidedOverrideEnabled = enabled;
    dirtyDoubleSided();
}

bool
RenderSceneIndex::doubleSidedOverride() const
{
    return p->d.doubleSidedOverride;
}

void
RenderSceneIndex::setDoubleSidedOverride(bool doubleSided)
{
    if (doubleSided == p->d.doubleSidedOverride)
        return;

    p->d.doubleSidedOverride = doubleSided;

    if (p->d.doubleSidedOverrideEnabled)
        dirtyDoubleSided();
}

HdSceneIndexPrim
RenderSceneIndex::GetPrim(const SdfPath& primPath) const
{
    HdSceneIndexPrim prim = _GetInputSceneIndex()->GetPrim(primPath);

    if (!p->isMaterialTarget(prim))
        return prim;

    // Never override auxiliary display geometry such as the grid.
    if (p->isDisplayPath(primPath))
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
    // This allows both rasterized sides of the polygon to remain visible
    // while preserving true front/back-facing evaluation in the material.
    if (p->d.doubleSidedOverrideEnabled && p->isMesh(prim)) {
        using BoolDataSource = HdRetainedTypedSampledDataSource<bool>;

        editor.Set(HdMeshSchema::GetDoubleSidedLocator(), BoolDataSource::New(p->d.doubleSidedOverride));
    }

    prim.dataSource = editor.Finish();

    return prim;
}

SdfPathVector
RenderSceneIndex::GetChildPrimPaths(const SdfPath& primPath) const
{
    return _GetInputSceneIndex()->GetChildPrimPaths(primPath);
}

void
RenderSceneIndex::dirtyMaterialBindings()
{
    if (!_IsObserved())
        return;

    HdSceneIndexObserver::DirtiedPrimEntries gprimEntries;
    HdDataSourceLocatorSet materialLocators;
    materialLocators.insert(HdMaterialBindingsSchema::GetDefaultLocator());

    HdSceneIndexObserver::RemovedPrimEntries subsetRemoved;
    HdSceneIndexObserver::AddedPrimEntries subsetAdded;

    std::vector<SdfPath> pending { SdfPath::AbsoluteRootPath() };
    while (!pending.empty()) {
        const SdfPath path = pending.back();
        pending.pop_back();

        const SdfPathVector children = _GetInputSceneIndex()->GetChildPrimPaths(path);

        for (const SdfPath& child : children) {
            pending.push_back(child);

            if (p->isDisplayPath(child))
                continue;

            const HdSceneIndexPrim prim = _GetInputSceneIndex()->GetPrim(child);

            if (p->isGeomSubset(prim)) {
                subsetRemoved.push_back({ child });
                subsetAdded.push_back({ child, prim.primType });
            }
            else if (p->isGprim(prim)) {
                gprimEntries.push_back({ child, materialLocators });
            }
        }
    }

    if (!subsetRemoved.empty())
        _SendPrimsRemoved(subsetRemoved);
    if (!subsetAdded.empty())
        _SendPrimsAdded(subsetAdded);
    if (!gprimEntries.empty())
        _SendPrimsDirtied(gprimEntries);
}

void
RenderSceneIndex::dirtyDoubleSided()
{
    if (!_IsObserved())
        return;

    HdSceneIndexObserver::DirtiedPrimEntries entries;
    HdDataSourceLocatorSet locators;
    locators.insert(HdMeshSchema::GetDoubleSidedLocator());

    std::vector<SdfPath> pending { SdfPath::AbsoluteRootPath() };
    while (!pending.empty()) {
        const SdfPath path = pending.back();
        pending.pop_back();

        const SdfPathVector children = _GetInputSceneIndex()->GetChildPrimPaths(path);
        for (const SdfPath& child : children) {
            pending.push_back(child);

            // Keep Stageviz auxiliary display geometry untouched.
            if (p->isDisplayPath(child))
                continue;

            const HdSceneIndexPrim prim = _GetInputSceneIndex()->GetPrim(child);

            if (p->isMesh(prim))
                entries.push_back({ child, locators });
        }
    }
    if (!entries.empty())
        _SendPrimsDirtied(entries);
}

void
RenderSceneIndex::_PrimsAdded(const HdSceneIndexBase& sender, const HdSceneIndexObserver::AddedPrimEntries& entries)
{
    Q_UNUSED(sender);
    _SendPrimsAdded(entries);
}

void
RenderSceneIndex::_PrimsRemoved(const HdSceneIndexBase& sender, const HdSceneIndexObserver::RemovedPrimEntries& entries)
{
    Q_UNUSED(sender);
    _SendPrimsRemoved(entries);
}

void
RenderSceneIndex::_PrimsDirtied(const HdSceneIndexBase& sender, const HdSceneIndexObserver::DirtiedPrimEntries& entries)
{
    Q_UNUSED(sender);
    _SendPrimsDirtied(entries);
}

void
RenderSceneIndex::_PrimsRenamed(const HdSceneIndexBase& sender, const HdSceneIndexObserver::RenamedPrimEntries& entries)
{
    Q_UNUSED(sender);
    _SendPrimsRenamed(entries);
}

}  // namespace stageviz
