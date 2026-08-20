// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#include "materialoverridesceneindex.h"
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

class MaterialOverrideSceneIndexPrivate {
public:
    bool active() const;
    bool isDisplayPath(const SdfPath& path) const;
    bool isGprim(const HdSceneIndexPrim& prim) const;
    bool isMesh(const HdSceneIndexPrim& prim) const;
    SdfPath effectiveMaterialPath() const;
    HdContainerDataSourceHandle createMaterialBindings(const SdfPath& materialPath) const;

    struct Data {
        bool sceneMaterialsEnabled = true;
        MaterialOverrideSceneIndex::Mode mode = MaterialOverrideSceneIndex::None;
        SdfPath materialPath;
        SdfPath displayPath = SdfPath("/Display");
        SdfPath clayMaterialPath = SdfPath("/Materials/Clay");

        bool doubleSidedOverrideEnabled = false;
        bool doubleSidedOverride = false;
    };

    Data d;
};

bool
MaterialOverrideSceneIndexPrivate::active() const
{
    if (d.doubleSidedOverrideEnabled)
        return true;

    if (d.mode == MaterialOverrideSceneIndex::Clay)
        return true;

    if (d.mode == MaterialOverrideSceneIndex::Custom)
        return !d.materialPath.IsEmpty();

    return !d.sceneMaterialsEnabled;
}

bool
MaterialOverrideSceneIndexPrivate::isDisplayPath(const SdfPath& path) const
{
    return path == d.displayPath || path.HasPrefix(d.displayPath);
}

bool
MaterialOverrideSceneIndexPrivate::isGprim(const HdSceneIndexPrim& prim) const
{
    const TfToken& type = prim.primType;

    return type == HdPrimTypeTokens->mesh || type == HdPrimTypeTokens->basisCurves || type == HdPrimTypeTokens->points
           || type == HdPrimTypeTokens->cube || type == HdPrimTypeTokens->sphere || type == HdPrimTypeTokens->cylinder
           || type == HdPrimTypeTokens->cone || type == HdPrimTypeTokens->capsule;
}

bool
MaterialOverrideSceneIndexPrivate::isMesh(const HdSceneIndexPrim& prim) const
{
    return prim.primType == HdPrimTypeTokens->mesh;
}

SdfPath
MaterialOverrideSceneIndexPrivate::effectiveMaterialPath() const
{
    if (d.mode == MaterialOverrideSceneIndex::Clay)
        return d.clayMaterialPath;

    if (d.mode == MaterialOverrideSceneIndex::Custom)
        return d.materialPath;

    return {};
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

    p->d.mode = mode;
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

bool
MaterialOverrideSceneIndex::doubleSidedOverrideEnabled() const
{
    return p->d.doubleSidedOverrideEnabled;
}

void
MaterialOverrideSceneIndex::setDoubleSidedOverrideEnabled(bool enabled)
{
    if (enabled == p->d.doubleSidedOverrideEnabled)
        return;

    p->d.doubleSidedOverrideEnabled = enabled;
    dirtyDoubleSided();
}

bool
MaterialOverrideSceneIndex::doubleSidedOverride() const
{
    return p->d.doubleSidedOverride;
}

void
MaterialOverrideSceneIndex::setDoubleSidedOverride(bool doubleSided)
{
    if (doubleSided == p->d.doubleSidedOverride)
        return;

    p->d.doubleSidedOverride = doubleSided;

    if (p->d.doubleSidedOverrideEnabled)
        dirtyDoubleSided();
}

HdSceneIndexPrim
MaterialOverrideSceneIndex::GetPrim(const SdfPath& primPath) const
{
    HdSceneIndexPrim prim = _GetInputSceneIndex()->GetPrim(primPath);

    if (!p->isGprim(prim))
        return prim;

    // Never override auxiliary display geometry such as the grid.
    if (p->isDisplayPath(primPath))
        return prim;

    if (!p->active() || !prim.dataSource)
        return prim;

    HdContainerDataSourceEditor editor(prim.dataSource);

    //
    // Material presentation override.
    //
    if (p->d.mode == Clay || p->d.mode == Custom) {
        const SdfPath materialPath = p->effectiveMaterialPath();

        if (!materialPath.IsEmpty()) {
            editor.Set(HdMaterialBindingsSchema::GetDefaultLocator(), p->createMaterialBindings(materialPath));
        }
    }
    else if (!p->d.sceneMaterialsEnabled) {
        editor.Set(HdMaterialBindingsSchema::GetDefaultLocator(), HdBlockDataSource::New());
    }

    //
    // Mesh double-sided presentation override.
    //
    // The normal-direction diagnostic uses:
    //
    //     doubleSided = false
    //     cullStyle   = CULL_STYLE_NOTHING
    //
    // This allows both rasterized sides of the polygon to remain visible
    // while preserving true front/back-facing evaluation in the material.
    //
    if (p->d.doubleSidedOverrideEnabled && p->isMesh(prim)) {
        using BoolDataSource = HdRetainedTypedSampledDataSource<bool>;

        editor.Set(HdMeshSchema::GetDoubleSidedLocator(), BoolDataSource::New(p->d.doubleSidedOverride));
    }

    prim.dataSource = editor.Finish();

    return prim;
}

SdfPathVector
MaterialOverrideSceneIndex::GetChildPrimPaths(const SdfPath& primPath) const
{
    return _GetInputSceneIndex()->GetChildPrimPaths(primPath);
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

            if (p->isDisplayPath(child))
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
MaterialOverrideSceneIndex::dirtyDoubleSided()
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
MaterialOverrideSceneIndex::_PrimsAdded(const HdSceneIndexBase& sender,
                                        const HdSceneIndexObserver::AddedPrimEntries& entries)
{
    Q_UNUSED(sender);
    _SendPrimsAdded(entries);
}

void
MaterialOverrideSceneIndex::_PrimsRemoved(const HdSceneIndexBase& sender,
                                          const HdSceneIndexObserver::RemovedPrimEntries& entries)
{
    Q_UNUSED(sender);
    _SendPrimsRemoved(entries);
}

void
MaterialOverrideSceneIndex::_PrimsDirtied(const HdSceneIndexBase& sender,
                                          const HdSceneIndexObserver::DirtiedPrimEntries& entries)
{
    Q_UNUSED(sender);
    _SendPrimsDirtied(entries);
}

void
MaterialOverrideSceneIndex::_PrimsRenamed(const HdSceneIndexBase& sender,
                                          const HdSceneIndexObserver::RenamedPrimEntries& entries)
{
    Q_UNUSED(sender);
    _SendPrimsRenamed(entries);
}

}  // namespace stageviz
