// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 - present Mikael Sundell
#include "renderselection.h"
#include "perfscope.h"
#include <algorithm>
#include <map>
#include <pxr/base/work/loops.h>
#include <pxr/imaging/hd/instanceSchema.h>
#include <pxr/imaging/hd/instancedBySchema.h>
#include <pxr/imaging/hd/instancerTopologySchema.h>
#include <pxr/imaging/hd/sceneIndexObserver.h>
#include <pxr/imaging/hd/tokens.h>
#include <pxr/usd/usdGeom/gprim.h>
#include <pxr/usdImaging/usdImaging/usdPrimInfoSchema.h>
#include <set>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
PXR_NAMESPACE_USING_DIRECTIVE
namespace stageviz {
namespace {
    // Portions adapted from OpenUSD 25.11's UsdImagingSelectionSceneIndex.
    // Keep grouped/cached expansion: AddSelection expands descendants for each
    // supplied path, while HdxSelectionSceneIndexObserver initially walks the
    // whole input. The native pipeline is a parity oracle in regression tests.
    // Sources: pxr/usdImaging/usdImaging/selectionSceneIndex.cpp and
    // pxr/imaging/hdx/selectionSceneIndexObserver.cpp in the v25.11 tag.
    // Copyright 2022 Pixar. Licensed under the terms in OpenUSD's LICENSE.txt:
    // https://openusd.org/license.
    // SPDX-License-Identifier: BSD-3-Clause

    struct InstanceSelection {
        SdfPath instancer;
        int prototypeIndex = 0;
        VtIntArray indices;
    };

    using NestedInstanceSelection = std::vector<InstanceSelection>;

    struct SelectionSeed {
        SdfPath path;
        NestedInstanceSelection nested;
    };

    using MappedPathCache = std::unordered_map<SdfPath, std::vector<SelectionSeed>, SdfPath::Hash>;

    struct SelectionStateLess {
        bool operator()(const SelectionSeed& lhs, const SelectionSeed& rhs) const
        {
            if (lhs.path != rhs.path)
                return lhs.path < rhs.path;
            if (lhs.nested.size() != rhs.nested.size())
                return lhs.nested.size() < rhs.nested.size();
            for (size_t i = 0; i < lhs.nested.size(); ++i) {
                const InstanceSelection& a = lhs.nested[i];
                const InstanceSelection& b = rhs.nested[i];
                if (a.instancer != b.instancer)
                    return a.instancer < b.instancer;
                if (a.prototypeIndex != b.prototypeIndex)
                    return a.prototypeIndex < b.prototypeIndex;
                if (a.indices != b.indices)
                    return std::lexicographical_compare(a.indices.begin(), a.indices.end(), b.indices.begin(),
                                                        b.indices.end());
            }
            return false;
        }
    };

    struct SelectionSeedGroupKey {
        SdfPath path;
        NestedInstanceSelection outerInstances;
        SdfPath instancer;
        int prototypeIndex = 0;
        bool hasInstance = false;
    };

    struct SelectionSeedGroupKeyLess {
        bool operator()(const SelectionSeedGroupKey& lhs, const SelectionSeedGroupKey& rhs) const
        {
            if (lhs.path != rhs.path)
                return lhs.path < rhs.path;
            SelectionStateLess less;
            const SelectionSeed leftOuter { SdfPath(), lhs.outerInstances };
            const SelectionSeed rightOuter { SdfPath(), rhs.outerInstances };
            if (less(leftOuter, rightOuter))
                return true;
            if (less(rightOuter, leftOuter))
                return false;
            if (lhs.hasInstance != rhs.hasInstance)
                return lhs.hasInstance < rhs.hasInstance;
            if (!lhs.hasInstance)
                return false;
            if (lhs.instancer != rhs.instancer)
                return lhs.instancer < rhs.instancer;
            return lhs.prototypeIndex < rhs.prototypeIndex;
        }
    };

    SelectionSeedGroupKey groupKey(const SelectionSeed& seed)
    {
        SelectionSeedGroupKey key;
        key.path = seed.path;
        if (seed.nested.empty())
            return key;

        key.hasInstance = true;
        key.outerInstances.assign(seed.nested.begin(), seed.nested.end() - 1);
        key.instancer = seed.nested.back().instancer;
        key.prototypeIndex = seed.nested.back().prototypeIndex;
        return key;
    }

    SdfPath pathAt(const HdPathArrayDataSourceHandle& dataSource, int index)
    {
        if (!dataSource || index < 0)
            return {};
        const VtArray<SdfPath> paths = dataSource->GetTypedValue(0.0f);
        return static_cast<size_t>(index) < paths.size() ? paths[static_cast<size_t>(index)] : SdfPath();
    }

    int prototypeIndex(const HdInstancerTopologySchema& topology, const SdfPath& prototype)
    {
        const HdPathArrayDataSourceHandle prototypesData = topology.GetPrototypes();
        if (!prototypesData)
            return 0;
        const VtArray<SdfPath> prototypes = prototypesData->GetTypedValue(0.0f);
        const auto it = std::find(prototypes.begin(), prototypes.end(), prototype);
        return it == prototypes.end() ? 0 : static_cast<int>(std::distance(prototypes.begin(), it));
    }

    VtIntArray instanceIndicesForPrototype(const HdInstancerTopologySchema& topology, int index)
    {
        const HdIntArrayDataSourceHandle data = topology.GetInstanceIndices().GetElement(index);
        return data ? data->GetTypedValue(0.0f) : VtIntArray();
    }

    bool instanceMapping(const HdContainerDataSourceHandle& source, const HdSceneIndexBaseRefPtr& sceneIndex,
                         SdfPath* prototype, InstanceSelection* selection)
    {
        if (!source || !sceneIndex || !prototype || !selection)
            return false;
        const HdInstanceSchema instance = HdInstanceSchema::GetFromParent(source);
        const HdPathDataSourceHandle instancerData = instance.GetInstancer();
        if (!instancerData)
            return false;

        selection->instancer = instancerData->GetTypedValue(0.0f);
        const HdIntDataSourceHandle prototypeData = instance.GetPrototypeIndex();
        if (!prototypeData || selection->instancer.IsEmpty())
            return false;
        selection->prototypeIndex = prototypeData->GetTypedValue(0.0f);

        const HdContainerDataSourceHandle instancerSource = sceneIndex->GetPrim(selection->instancer).dataSource;
        const HdInstancerTopologySchema topology = HdInstancerTopologySchema::GetFromParent(instancerSource);
        *prototype = pathAt(topology.GetPrototypes(), selection->prototypeIndex);
        if (prototype->IsEmpty())
            return false;

        if (const HdIntDataSourceHandle instanceIndexData = instance.GetInstanceIndex())
            selection->indices = { instanceIndexData->GetTypedValue(0.0f) };
        return true;
    }

    NestedInstanceSelection allNestedInstanceIndices(const SdfPath& prototypePath,
                                                     const HdSceneIndexBaseRefPtr& sceneIndex)
    {
        NestedInstanceSelection result;
        SdfPath path = prototypePath;
        while (!path.IsEmpty()) {
            const HdContainerDataSourceHandle source = sceneIndex->GetPrim(path).dataSource;
            const HdInstancedBySchema instancedBy = HdInstancedBySchema::GetFromParent(source);
            const HdPathArrayDataSourceHandle instancersData = instancedBy.GetPaths();
            if (!instancersData)
                break;
            const VtArray<SdfPath> instancers = instancersData->GetTypedValue(0.0f);
            if (instancers.empty())
                break;

            const SdfPath instancerPath = instancers.front();
            const HdContainerDataSourceHandle instancerSource = sceneIndex->GetPrim(instancerPath).dataSource;
            const HdInstancerTopologySchema topology = HdInstancerTopologySchema::GetFromParent(instancerSource);
            const int index = prototypeIndex(topology, path);
            InstanceSelection selection { instancerPath, index, instanceIndicesForPrototype(topology, index) };
            result.push_back(std::move(selection));
            path = instancerPath;
        }
        std::reverse(result.begin(), result.end());
        return result;
    }

    void appendPiPropagatedSeeds(const HdContainerDataSourceHandle& source, const HdSceneIndexBaseRefPtr& sceneIndex,
                                 const SelectionSeed& seed, std::vector<SelectionSeed>* output)
    {
        if (!output)
            return;
        const UsdImagingUsdPrimInfoSchema primInfo = UsdImagingUsdPrimInfoSchema::GetFromParent(source);
        const HdContainerDataSourceHandle propagated = primInfo.GetPiPropagatedPrototypes();
        if (!propagated)
            return;

        for (const TfToken& name : propagated->GetNames()) {
            const HdPathDataSourceHandle pathData = HdPathDataSource::Cast(propagated->Get(name));
            if (!pathData)
                continue;
            SelectionSeed copy { pathData->GetTypedValue(0.0f), seed.nested };
            NestedInstanceSelection nested = allNestedInstanceIndices(copy.path, sceneIndex);
            copy.nested.insert(copy.nested.end(), nested.begin(), nested.end());
            output->push_back(std::move(copy));
        }
    }

    struct PropagatedSelectionMapping {
        SdfPath path;
        NestedInstanceSelection nested;
    };

    struct PrimSelectionMapping {
        SdfPath prototype;
        InstanceSelection instance;
        std::vector<PropagatedSelectionMapping> propagated;
    };

    class SelectionMappingCache {
    public:
        void invalidate()
        {
            ++generation;
            primMappings.clear();
            inspectedPrimPaths.clear();
            mappedPaths.clear();
            hydraMappingRequired.clear();
            lastResolvedRoots.clear();
            lastResolvedRprims.clear();
        }

        void invalidatePathClassification() { hydraMappingRequired.clear(); }

        bool ensure(const HdSceneIndexBaseRefPtr& sceneIndex)
        {
            if (!sceneIndex)
                return false;
            if (indexedScene != sceneIndex) {
                indexedScene = sceneIndex;
                invalidate();
            }
            return true;
        }

        HdSceneIndexBaseRefPtr indexedScene;
        MappedPathCache mappedPaths;
        std::unordered_map<SdfPath, bool, SdfPath::Hash> hydraMappingRequired;
        std::unordered_map<SdfPath, PrimSelectionMapping, SdfPath::Hash> primMappings;
        std::unordered_set<SdfPath, SdfPath::Hash> inspectedPrimPaths;
        SdfPathVector lastResolvedRoots;
        std::vector<HydraSelectionRprim> lastResolvedRprims;
        uint64_t generation = 0;
    };

    class SelectionMappingCacheObserver final : public HdSceneIndexObserver {
    public:
        explicit SelectionMappingCacheObserver(const std::weak_ptr<SelectionMappingCache>& cache)
            : m_cache(cache)
        {}

        void PrimsAdded(const HdSceneIndexBase&, const AddedPrimEntries&) override { invalidate(); }
        void PrimsRemoved(const HdSceneIndexBase&, const RemovedPrimEntries&) override { invalidate(); }
        void PrimsRenamed(const HdSceneIndexBase&, const RenamedPrimEntries&) override { invalidate(); }

        void PrimsDirtied(const HdSceneIndexBase&, const DirtiedPrimEntries& entries) override
        {
            const HdDataSourceLocator& instanceLocator = HdInstanceSchema::GetDefaultLocator();
            const HdDataSourceLocator& topologyLocator = HdInstancerTopologySchema::GetDefaultLocator();
            const HdDataSourceLocator& instancedByLocator = HdInstancedBySchema::GetDefaultLocator();
            const HdDataSourceLocator& primInfoLocator = UsdImagingUsdPrimInfoSchema::GetDefaultLocator();
            for (const DirtiedPrimEntry& entry : entries) {
                if (entry.dirtyLocators.Intersects(instanceLocator) || entry.dirtyLocators.Intersects(topologyLocator)
                    || entry.dirtyLocators.Intersects(instancedByLocator)
                    || entry.dirtyLocators.Intersects(primInfoLocator)) {
                    invalidate();
                    return;
                }
            }

            // Partition classification also depends on prim type and hierarchy
            // shape. Clear its cache for any other scene-index dirty batch.
            if (!entries.empty()) {
                if (const std::shared_ptr<SelectionMappingCache> cache = m_cache.lock())
                    cache->invalidatePathClassification();
            }
        }

    private:
        void invalidate()
        {
            if (const std::shared_ptr<SelectionMappingCache> cache = m_cache.lock())
                cache->invalidate();
        }

        std::weak_ptr<SelectionMappingCache> m_cache;
    };

    std::vector<SelectionSeed> mapUsdPath(const SdfPath& usdPath, const HdSceneIndexBaseRefPtr& sceneIndex,
                                          SelectionMappingCache* mappingCache)
    {
        const SdfPathVector prefixes = usdPath.GetPrefixes();
        std::vector<SelectionSeed> seeds { { SdfPath::AbsoluteRootPath(), {} } };
        size_t firstUnmappedPrefix = 0;
        for (size_t i = prefixes.size(); i > 0; --i) {
            const auto cached = mappingCache->mappedPaths.find(prefixes[i - 1]);
            if (cached != mappingCache->mappedPaths.end()) {
                seeds = cached->second;
                firstUnmappedPrefix = i;
                break;
            }
        }

        for (size_t i = firstUnmappedPrefix; i < prefixes.size(); ++i) {
            const SdfPath& prefix = prefixes[i];
            const TfToken childName = prefix.GetNameToken();
            std::vector<SelectionSeed> next;
            for (const SelectionSeed& seed : seeds) {
                SelectionSeed child = seed;
                child.path = child.path.AppendChild(childName);
                auto mapping = mappingCache->primMappings.find(child.path);
                if (mapping == mappingCache->primMappings.end()
                    && mappingCache->inspectedPrimPaths.insert(child.path).second) {
                    const HdSceneIndexPrim prim = sceneIndex->GetPrim(child.path);
                    PrimSelectionMapping discovered;
                    if (instanceMapping(prim.dataSource, sceneIndex, &discovered.prototype, &discovered.instance)) {
                        mapping = mappingCache->primMappings.emplace(child.path, std::move(discovered)).first;
                    }
                    else {
                        std::vector<SelectionSeed> propagated;
                        appendPiPropagatedSeeds(prim.dataSource, sceneIndex, SelectionSeed { child.path, {} },
                                                &propagated);
                        if (!propagated.empty()) {
                            discovered.propagated.reserve(propagated.size());
                            for (SelectionSeed& seed : propagated)
                                discovered.propagated.push_back({ std::move(seed.path), std::move(seed.nested) });
                            mapping = mappingCache->primMappings.emplace(child.path, std::move(discovered)).first;
                        }
                    }
                }
                if (mapping != mappingCache->primMappings.end() && !mapping->second.prototype.IsEmpty()) {
                    child.path = mapping->second.prototype;
                    child.nested.push_back(mapping->second.instance);
                    next.push_back(std::move(child));
                }
                else {
                    if (mapping != mappingCache->primMappings.end()) {
                        for (const PropagatedSelectionMapping& propagated : mapping->second.propagated) {
                            SelectionSeed copy { propagated.path, child.nested };
                            copy.nested.insert(copy.nested.end(), propagated.nested.begin(), propagated.nested.end());
                            next.push_back(std::move(copy));
                        }
                    }
                    next.push_back(std::move(child));
                }
            }
            auto cached = mappingCache->mappedPaths.emplace(prefix, std::move(next)).first;
            seeds = cached->second;
        }
        return seeds;
    }

    struct NativeInstanceKey {
        SdfPath prototype;
        SdfPath instancer;
        int prototypeIndex = 0;

        bool operator<(const NativeInstanceKey& other) const
        {
            return std::tie(prototype, instancer, prototypeIndex)
                   < std::tie(other.prototype, other.instancer, other.prototypeIndex);
        }
    };

    struct SelectedPrim {
        bool fullySelected = false;
        std::set<int32_t> instances;
    };

    struct DescendantNode {
        SdfPath path;
        SdfPath prototype;
        SdfPath instancer;
        int prototypeIndex = 0;
        VtIntArray instanceIndices;
        bool supportsSelection = false;
        std::vector<SelectionSeed> propagatedSeeds;
        SdfPathVector children;
    };

    bool primTypeSupportsSelection(const TfToken& type)
    {
        // Use Hydra's complete renderable-prim registry, including implicit
        // surfaces before tessellation. HdPrimTypeIsGprim only covers a subset.
        const TfTokenVector& types = HdRprimTypeTokens->allTokens;
        return std::find(types.begin(), types.end(), type) != types.end();
    }

    void classifyNodes(size_t begin, size_t end, const SelectionSeed& seed, const HdSceneIndexBaseRefPtr& sceneIndex,
                       std::vector<DescendantNode>* nodes)
    {
        for (size_t index = begin; index < end; ++index) {
            DescendantNode& node = (*nodes)[index];
            const HdSceneIndexPrim prim = sceneIndex->GetPrim(node.path);
            InstanceSelection instance;
            if (instanceMapping(prim.dataSource, sceneIndex, &node.prototype, &instance)) {
                node.instancer = std::move(instance.instancer);
                node.prototypeIndex = instance.prototypeIndex;
                node.instanceIndices = std::move(instance.indices);
            }
            else {
                const SelectionSeed selectedPath { node.path, seed.nested };
                appendPiPropagatedSeeds(prim.dataSource, sceneIndex, selectedPath, &node.propagatedSeeds);
                node.supportsSelection = primTypeSupportsSelection(prim.primType);
            }
            node.children = sceneIndex->GetChildPrimPaths(node.path);
        }
    }

    std::vector<DescendantNode> classifyDescendantLevel(std::vector<DescendantNode> nodes, const SelectionSeed& seed,
                                                        const HdSceneIndexBaseRefPtr& sceneIndex)
    {
        constexpr size_t grainSize = 32;
        if (nodes.size() < grainSize) {
            classifyNodes(0, nodes.size(), seed, sceneIndex, &nodes);
        }
        else {
            WorkParallelForN(
                nodes.size(), [&](size_t begin, size_t end) { classifyNodes(begin, end, seed, sceneIndex, &nodes); },
                grainSize);
        }
        return nodes;
    }

    void addSelection(const SelectionSeed& seed, const HdSceneIndexBaseRefPtr& sceneIndex,
                      std::map<std::pair<SdfPath, int>, int>* instanceCountCache,
                      std::map<SdfPath, SelectedPrim>* selected)
    {
        if (!selected)
            return;
        SelectedPrim& entry = (*selected)[seed.path];
        if (seed.nested.empty()) {
            entry.fullySelected = true;
            entry.instances.clear();
            return;
        }

        // HdSelection stores one flattened instance index. Combine nested indices
        // from outermost to innermost, matching HdxSelectionSceneIndexObserver.
        std::vector<int32_t> flattened { 0 };
        for (const InstanceSelection& level : seed.nested) {
            const std::pair<SdfPath, int> cacheKey { level.instancer, level.prototypeIndex };
            auto countIt = instanceCountCache->find(cacheKey);
            if (countIt == instanceCountCache->end()) {
                const HdContainerDataSourceHandle instancerSource = sceneIndex->GetPrim(level.instancer).dataSource;
                const HdInstancerTopologySchema topology = HdInstancerTopologySchema::GetFromParent(instancerSource);
                const int count
                    = std::max(1, static_cast<int>(instanceIndicesForPrototype(topology, level.prototypeIndex).size()));
                countIt = instanceCountCache->emplace(cacheKey, count).first;
            }
            const int count = countIt->second;
            std::vector<int32_t> next;
            next.reserve(flattened.size() * level.indices.size());
            for (const int32_t prefix : flattened) {
                for (const int32_t index : level.indices)
                    next.push_back(prefix * count + index);
            }
            flattened = std::move(next);
        }
        if (!entry.fullySelected)
            entry.instances.insert(flattened.begin(), flattened.end());
    }

    void expandSeed(const SelectionSeed& root, const HdSceneIndexBaseRefPtr& sceneIndex,
                    std::map<std::pair<SdfPath, int>, int>* instanceCountCache,
                    std::map<SdfPath, SelectedPrim>* selected)
    {
        std::vector<SelectionSeed> pending { root };
        std::set<SelectionSeed, SelectionStateLess> visited;
        while (!pending.empty()) {
            SelectionSeed seed = std::move(pending.back());
            pending.pop_back();
            if (!visited.insert(seed).second)
                continue;

            std::map<NativeInstanceKey, std::set<int32_t>> nativeInstances;
            std::vector<SelectionSeed> propagatedSeeds;
            std::vector<DescendantNode> frontier(1);
            frontier.front().path = seed.path;
            while (!frontier.empty()) {
                frontier = classifyDescendantLevel(std::move(frontier), seed, sceneIndex);
                std::vector<DescendantNode> nextFrontier;
                size_t childCount = 0;
                for (const DescendantNode& node : frontier)
                    childCount += node.children.size();
                nextFrontier.reserve(childCount);
                for (DescendantNode& node : frontier) {
                    if (!node.prototype.IsEmpty()) {
                        NativeInstanceKey key { node.prototype, node.instancer, node.prototypeIndex };
                        auto& indices = nativeInstances[key];
                        indices.insert(node.instanceIndices.begin(), node.instanceIndices.end());
                    }
                    else {
                        for (SelectionSeed& propagated : node.propagatedSeeds)
                            propagatedSeeds.push_back(std::move(propagated));
                        if (node.supportsSelection)
                            addSelection(SelectionSeed { node.path, seed.nested }, sceneIndex, instanceCountCache,
                                         selected);
                    }

                    for (SdfPath& child : node.children) {
                        DescendantNode childNode;
                        childNode.path = std::move(child);
                        nextFrontier.push_back(std::move(childNode));
                    }
                }
                frontier = std::move(nextFrontier);
            }

            for (SelectionSeed& propagated : propagatedSeeds)
                pending.push_back(std::move(propagated));
            for (const auto& [key, indices] : nativeInstances) {
                SelectionSeed prototypeSeed { key.prototype, seed.nested };
                InstanceSelection instance { key.instancer, key.prototypeIndex, {} };
                instance.indices.assign(indices.begin(), indices.end());
                prototypeSeed.nested.push_back(std::move(instance));
                pending.push_back(std::move(prototypeSeed));
            }
        }
    }

    bool resolveHydraSelection(const HdSceneIndexBaseRefPtr& sceneIndex, SelectionMappingCache* mappingCache,
                               const SdfPathVector& usdPaths, std::vector<HydraSelectionRprim>* rprims)
    {
        if (!rprims || !mappingCache)
            return false;
        rprims->clear();
        if (!mappingCache->ensure(sceneIndex))
            return false;

        SdfPathVector roots = usdPaths;
        for (SdfPath& path : roots) {
            if (path.IsPropertyPath())
                path = path.GetPrimPath();
        }
        SdfPath::RemoveDescendentPaths(&roots);
        SdfPathVector cacheKey = roots;
        std::sort(cacheKey.begin(), cacheKey.end());
        const bool resultCacheHit = cacheKey == mappingCache->lastResolvedRoots;
        if (resultCacheHit) {
            *rprims = mappingCache->lastResolvedRprims;
            return true;
        }

        using GroupedSeed = std::pair<SelectionSeed, std::set<int32_t>>;
        std::map<SelectionSeedGroupKey, GroupedSeed, SelectionSeedGroupKeyLess> groupedSeeds;
        MappedPathCache& mappedPathCache = mappingCache->mappedPaths;
        if (mappedPathCache.empty())
            mappedPathCache.reserve(roots.size() * 2);
        for (const SdfPath& root : roots) {
            std::vector<SelectionSeed> uncachedSeeds;
            const std::vector<SelectionSeed>* seeds = nullptr;
            const auto cachedRoot = mappedPathCache.find(root);
            if (cachedRoot != mappedPathCache.end()) {
                seeds = &cachedRoot->second;
            }
            else {
                uncachedSeeds = mapUsdPath(root, sceneIndex, mappingCache);
                seeds = &uncachedSeeds;
            }
            for (const SelectionSeed& seed : *seeds) {
                const SelectionSeedGroupKey key = groupKey(seed);
                auto it = groupedSeeds.emplace(key, GroupedSeed { seed, {} }).first;
                if (!seed.nested.empty()) {
                    // Multiple authored instances often share one Hydra
                    // prototype. Merge their IDs before traversing that prototype
                    // tree. Outer nested indices remain part of the key so we do
                    // not create false cross-products between nested branches.
                    it->second.second.insert(seed.nested.back().indices.begin(), seed.nested.back().indices.end());
                }
            }
        }

        std::map<SdfPath, SelectedPrim> selected;
        std::map<std::pair<SdfPath, int>, int> instanceCountCache;
        for (auto& groupedEntry : groupedSeeds) {
            GroupedSeed& grouped = groupedEntry.second;
            SelectionSeed& seed = grouped.first;
            if (!seed.nested.empty())
                seed.nested.back().indices.assign(grouped.second.begin(), grouped.second.end());
            expandSeed(seed, sceneIndex, &instanceCountCache, &selected);
        }

        rprims->reserve(selected.size());
        for (auto& [path, state] : selected) {
            HydraSelectionRprim mapped;
            mapped.path = path;
            mapped.fullySelected = state.fullySelected;
            mapped.instanceIndices.assign(state.instances.begin(), state.instances.end());
            rprims->push_back(std::move(mapped));
        }
        mappingCache->lastResolvedRoots = std::move(cacheKey);
        mappingCache->lastResolvedRprims = *rprims;
        return true;
    }
}  // namespace
class RenderSelectionPrivate {
public:
    struct Data {
        HdSceneIndexBaseRefPtr sceneIndex;
        std::shared_ptr<SelectionMappingCache> cache = std::make_shared<SelectionMappingCache>();
        std::unique_ptr<SelectionMappingCacheObserver> observer;
    } d;
};
RenderSelection::RenderSelection()
    : p(std::make_unique<RenderSelectionPrivate>())
{}
RenderSelection::~RenderSelection() { setSceneIndex({}); }
void
RenderSelection::setSceneIndex(const HdSceneIndexBaseRefPtr& sceneIndex)
{
    if (p->d.sceneIndex == sceneIndex)
        return;
    if (p->d.sceneIndex && p->d.observer)
        p->d.sceneIndex->RemoveObserver(TfCreateWeakPtr(p->d.observer.get()));
    p->d.sceneIndex = sceneIndex;
    p->d.cache->invalidate();
    p->d.cache->indexedScene = sceneIndex;
    p->d.observer.reset();
    if (sceneIndex) {
        p->d.observer = std::make_unique<SelectionMappingCacheObserver>(p->d.cache);
        sceneIndex->AddObserver(TfCreateWeakPtr(p->d.observer.get()));
    }
}
const HdSceneIndexBaseRefPtr&
RenderSelection::sceneIndex() const
{
    return p->d.sceneIndex;
}

uint64_t
RenderSelection::generation() const
{
    return p->d.cache->generation;
}
bool
RenderSelection::resolve(const UsdStageRefPtr& stage, const SdfPathVector& usdPaths, SdfPathVector* directPaths,
                         std::vector<HydraSelectionRprim>* rprims)
{
    if (!directPaths || !rprims)
        return false;
    directPaths->clear();
    rprims->clear();

    SelectionMappingCache* mappingCache = p->d.cache.get();
    const bool canCacheClassification = mappingCache && mappingCache->ensure(p->d.sceneIndex);
    if (canCacheClassification && mappingCache->hydraMappingRequired.empty())
        mappingCache->hydraMappingRequired.reserve(usdPaths.size());
    SdfPathVector hydraPaths;
    hydraPaths.reserve(usdPaths.size());
    directPaths->reserve(usdPaths.size());
    for (const SdfPath& path : usdPaths) {
        bool needsHydraMapping = false;
        bool cachedClassification = false;
        if (canCacheClassification) {
            const auto cached = mappingCache->hydraMappingRequired.find(path);
            if (cached != mappingCache->hydraMappingRequired.end()) {
                needsHydraMapping = cached->second;
                cachedClassification = true;
            }
        }

        if (!cachedClassification) {
            const UsdPrim prim = stage ? stage->GetPrimAtPath(path) : UsdPrim();
            needsHydraMapping = !prim || prim.IsInstance() || prim.IsInstanceProxy() || !prim.GetChildren().empty()
                                || !UsdGeomGprim(prim);

            if (!needsHydraMapping && p->d.sceneIndex) {
                const HdSceneIndexPrim scenePrim = p->d.sceneIndex->GetPrim(path);
                const HdContainerDataSourceHandle source = scenePrim.dataSource;
                SdfPath prototype;
                InstanceSelection instance;
                const UsdImagingUsdPrimInfoSchema primInfo = UsdImagingUsdPrimInfoSchema::GetFromParent(source);
                needsHydraMapping = instanceMapping(source, p->d.sceneIndex, &prototype, &instance)
                                    || static_cast<bool>(primInfo.GetPiPropagatedPrototypes());
            }

            if (canCacheClassification)
                mappingCache->hydraMappingRequired.emplace(path, needsHydraMapping);
        }
        (needsHydraMapping ? hydraPaths : *directPaths).push_back(path);
    }
    return resolveHydraSelection(p->d.sceneIndex, p->d.cache.get(), hydraPaths, rprims);
}
}  // namespace stageviz
