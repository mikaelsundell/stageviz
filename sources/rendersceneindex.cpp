// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#include "rendersceneindex.h"
#include <QtGlobal>
#include <pxr/imaging/hd/containerDataSourceEditor.h>
#include <pxr/imaging/hd/dataSource.h>
#include <pxr/imaging/hd/legacyDisplayStyleSchema.h>
#include <pxr/imaging/hd/materialBindingSchema.h>
#include <pxr/imaging/hd/materialBindingsSchema.h>
#include <pxr/imaging/hd/meshSchema.h>
#include <pxr/imaging/hd/repr.h>
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
    bool isMesh(const HdSceneIndexPrim& prim) const;
    SdfPath effectiveMaterialPath() const;
    HdContainerDataSourceHandle createMaterialBindings(const SdfPath& materialPath) const;

    struct Data {
        bool sceneMaterialsEnabled = true;
        RenderSceneIndex::Mode mode = RenderSceneIndex::None;
        SdfPath materialPath;
        SdfPath displayPath = SdfPath("/Display");
        SdfPath clayMaterialPath = SdfPath("/Materials/Clay");
        SdfPath selectionMaterialPath = SdfPath("/Materials/Selection");
        SdfPathVector selectionPaths;
        bool selectionPresentationEnabled = true;
        bool doubleSidedOverrideEnabled = false;
        bool doubleSidedOverride = false;
    };
    Data d;
};

bool
RenderSceneIndexPrivate::active() const
{
    if (d.selectionPresentationEnabled && !d.selectionPaths.empty())
        return true;

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
    return path == d.displayPath || path.HasPrefix(d.displayPath);
}

bool
RenderSceneIndexPrivate::isGprim(const HdSceneIndexPrim& prim) const
{
    const TfToken& type = prim.primType;
    return type == HdPrimTypeTokens->mesh || type == HdPrimTypeTokens->basisCurves || type == HdPrimTypeTokens->points
           || type == HdPrimTypeTokens->cube || type == HdPrimTypeTokens->sphere || type == HdPrimTypeTokens->cylinder
           || type == HdPrimTypeTokens->cone || type == HdPrimTypeTokens->capsule;
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

void
RenderSceneIndex::setSelectionPaths(const SdfPathVector& paths)
{
    if (paths == p->d.selectionPaths)
        return;

    SdfPathVector dirtyPaths = p->d.selectionPaths;
    dirtyPaths.insert(dirtyPaths.end(), paths.begin(), paths.end());

    p->d.selectionPaths = paths;
    dirtySelectionPresentation(dirtyPaths);
}

bool
RenderSceneIndex::selectionPresentationEnabled() const
{
    return p->d.selectionPresentationEnabled;
}

void
RenderSceneIndex::setSelectionPresentationEnabled(bool enabled)
{
    if (enabled == p->d.selectionPresentationEnabled)
        return;

    p->d.selectionPresentationEnabled = enabled;
    dirtySelectionPresentation(p->d.selectionPaths);
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

    if (!p->isGprim(prim))
        return prim;

    // Never override auxiliary display geometry such as the grid.
    if (p->isDisplayPath(primPath))
        return prim;

    if (!p->active() || !prim.dataSource)
        return prim;

    HdContainerDataSourceEditor editor(prim.dataSource);

    bool selected = false;
    if (p->d.selectionPresentationEnabled) {
        for (const SdfPath& selectedPath : p->d.selectionPaths) {
            if (primPath == selectedPath || primPath.HasPrefix(selectedPath)) {
                selected = true;
                break;
            }
        }
    }

    if (selected) {
        editor.Set(HdMaterialBindingsSchema::GetDefaultLocator(),
                   p->createMaterialBindings(p->d.selectionMaterialPath));

        using TokenArrayDataSource = HdRetainedTypedSampledDataSource<VtArray<TfToken>>;
        VtArray<TfToken> reprSelector;
        reprSelector.push_back(HdReprTokens->solidWireOnSurf);
        reprSelector.push_back(HdReprTokens->solidWireOnSurf);
        reprSelector.push_back(HdReprTokens->disabled);

        editor.Set(HdLegacyDisplayStyleSchema::GetDefaultLocator().Append(
                       HdLegacyDisplayStyleSchemaTokens->reprSelector),
                   TokenArrayDataSource::New(reprSelector));
    }
    else if (p->d.mode == Clay || p->d.mode == Custom) {
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
RenderSceneIndex::dirtySelectionPresentation(const SdfPathVector& paths)
{
    if (!_IsObserved() || paths.empty())
        return;

    HdSceneIndexObserver::DirtiedPrimEntries entries;
    HdDataSourceLocatorSet locators;
    locators.insert(HdMaterialBindingsSchema::GetDefaultLocator());
    locators.insert(
        HdLegacyDisplayStyleSchema::GetDefaultLocator().Append(HdLegacyDisplayStyleSchemaTokens->reprSelector));

    SdfPathVector visited;

    for (const SdfPath& rootPath : paths) {
        if (rootPath.IsEmpty() || rootPath == SdfPath::AbsoluteRootPath())
            continue;

        std::vector<SdfPath> pending { rootPath };
        while (!pending.empty()) {
            const SdfPath path = pending.back();
            pending.pop_back();

            if (std::find(visited.begin(), visited.end(), path) != visited.end())
                continue;
            visited.push_back(path);

            if (p->isDisplayPath(path))
                continue;

            const HdSceneIndexPrim prim = _GetInputSceneIndex()->GetPrim(path);
            if (p->isGprim(prim))
                entries.push_back({ path, locators });

            const SdfPathVector children = _GetInputSceneIndex()->GetChildPrimPaths(path);
            pending.insert(pending.end(), children.begin(), children.end());
        }
    }

    if (!entries.empty())
        _SendPrimsDirtied(entries);
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
