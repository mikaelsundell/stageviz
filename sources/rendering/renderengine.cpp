// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#include "renderengine.h"
#include "paths.h"
#include "qtutils.h"
#include "rendersceneindex.h"
#include "rendertask.h"
#include <QColorSpace>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLFramebufferObject>
#include <QOpenGLFramebufferObjectFormat>
#include <QSurfaceFormat>
#include <QtGui/qopengl.h>
#include <algorithm>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <pxr/base/gf/rotation.h>
#include <pxr/base/gf/vec2d.h>
#include <pxr/base/vt/array.h>
#include <pxr/base/work/loops.h>
#include <pxr/imaging/cameraUtil/framing.h>
#include <pxr/imaging/glf/simpleLight.h>
#include <pxr/imaging/glf/simpleMaterial.h>
#include <pxr/imaging/hd/changeTracker.h>
#include <pxr/imaging/hd/filteringSceneIndex.h>
#include <pxr/imaging/hd/instanceSchema.h>
#include <pxr/imaging/hd/instancedBySchema.h>
#include <pxr/imaging/hd/instancerTopologySchema.h>
#include <pxr/imaging/hd/mergingSceneIndex.h>
#include <pxr/imaging/hd/renderIndex.h>
#include <pxr/imaging/hd/sceneDelegate.h>
#include <pxr/imaging/hd/sceneIndexObserver.h>
#include <pxr/imaging/hd/sceneIndexPluginRegistry.h>
#include <pxr/imaging/hd/tokens.h>
#include <pxr/imaging/hdx/colorCorrectionTask.h>
#include <pxr/imaging/hdx/colorizeSelectionTask.h>
#include <pxr/imaging/hdx/presentTask.h>
#include <pxr/imaging/hdx/renderSetupTask.h>
#include <pxr/imaging/hdx/taskControllerSceneIndex.h>
#include <pxr/imaging/hdx/tokens.h>
#include <pxr/imaging/hgi/hgi.h>
#include <pxr/usd/sdf/assetPath.h>
#include <pxr/usd/usd/prim.h>
#include <pxr/usd/usdGeom/gprim.h>
#include <pxr/usd/usdGeom/metrics.h>
#include <pxr/usd/usdGeom/subset.h>
#include <pxr/usd/usdGeom/tokens.h>
#include <pxr/usdImaging/usdImaging/sceneIndices.h>
#include <pxr/usdImaging/usdImaging/stageSceneIndex.h>
#include <pxr/usdImaging/usdImaging/usdPrimInfoSchema.h>
#include <set>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

PXR_NAMESPACE_USING_DIRECTIVE

namespace stageviz {

namespace {

    // Portions adapted from OpenUSD's UsdImagingSelectionSceneIndex.
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
        static const TfTokenVector supportedTypes = {
            HdPrimTypeTokens->mesh,       HdPrimTypeTokens->basisCurves, HdPrimTypeTokens->points,
            HdPrimTypeTokens->nurbsPatch, HdPrimTypeTokens->nurbsCurves, HdPrimTypeTokens->volume,
            HdPrimTypeTokens->tetMesh,    HdPrimTypeTokens->geomSubset,  HdPrimTypeTokens->plane,
            HdPrimTypeTokens->capsule,    HdPrimTypeTokens->cube,        HdPrimTypeTokens->cylinder,
            HdPrimTypeTokens->sphere,     HdPrimTypeTokens->model,
        };
        return std::find(supportedTypes.begin(), supportedTypes.end(), type) != supportedTypes.end();
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


    class OpenGLContextRestore final {
    public:
        OpenGLContextRestore()
            : m_context(QOpenGLContext::currentContext())
            , m_surface(m_context ? m_context->surface() : nullptr)
        {}

        ~OpenGLContextRestore()
        {
            QOpenGLContext* current = QOpenGLContext::currentContext();
            if (current && current != m_context)
                current->doneCurrent();

            if (m_context && m_surface && QOpenGLContext::currentContext() != m_context)
                m_context->makeCurrent(m_surface);
        }

        OpenGLContextRestore(const OpenGLContextRestore&) = delete;
        OpenGLContextRestore& operator=(const OpenGLContextRestore&) = delete;

    private:
        QOpenGLContext* m_context = nullptr;
        QSurface* m_surface = nullptr;
    };

    QString renderResourcePath(const QString& filename)
    {
        const QString applicationDir = QCoreApplication::applicationDirPath();

#ifdef Q_OS_MAC
        QDir contentsDir(applicationDir);
        if (contentsDir.cdUp()) {
            const QString candidate = contentsDir.filePath(QStringLiteral("Resources/") + filename);
            if (QFileInfo::exists(candidate))
                return QFileInfo(candidate).absoluteFilePath();
        }
#endif

        const QString candidate = QDir(applicationDir).filePath(QStringLiteral("resources/") + filename);
        if (QFileInfo::exists(candidate))
            return QFileInfo(candidate).absoluteFilePath();

        return {};
    }

    QString renderShaderPath()
    {
        static const QString path = renderResourcePath(QStringLiteral("Render.glslfx"));
        return path;
    }

    QString sceneIdShaderPath()
    {
        static const QString path = renderResourcePath(QStringLiteral("SceneId.glslfx"));
        return path;
    }

    class RenderTaskDelegate final : public HdSceneDelegate {
    public:
        RenderTaskDelegate(HdRenderIndex* renderIndex, const SdfPath& delegateId)
            : HdSceneDelegate(renderIndex, delegateId)
        {}

        VtValue Get(const SdfPath& id, const TfToken& key) override
        {
            if (id == m_taskPath && key == HdTokens->params)
                return VtValue(m_params);

            return {};
        }

        void setParams(const SdfPath& taskPath, const RenderTaskParams& params)
        {
            const bool pathChanged = taskPath != m_taskPath;
            const bool paramsChanged = params != m_params;
            if (!pathChanged && !paramsChanged)
                return;

            m_taskPath = taskPath;
            m_params = params;

            if (HdRenderIndex& renderIndex = GetRenderIndex(); renderIndex.HasTask(taskPath))
                renderIndex.GetChangeTracker().MarkTaskDirty(taskPath, HdChangeTracker::DirtyParams);
        }

    private:
        SdfPath m_taskPath;
        RenderTaskParams m_params;
    };

    class AuxiliaryMaterialSceneIndex final : public HdSingleInputFilteringSceneIndexBase {
    public:
        static TfRefPtr<AuxiliaryMaterialSceneIndex> New(const HdSceneIndexBaseRefPtr& input)
        {
            return TfCreateRefPtr(new AuxiliaryMaterialSceneIndex(input));
        }

        HdSceneIndexPrim GetPrim(const SdfPath& primPath) const override
        {
            // Keep the root visible for scene-index traversal, but expose only
            // the canonical auxiliary material branch below it.
            if (primPath == SdfPath::AbsoluteRootPath() || isMaterialPath(primPath))
                return _GetInputSceneIndex()->GetPrim(primPath);
            return {};
        }

        SdfPathVector GetChildPrimPaths(const SdfPath& primPath) const override
        {
            if (primPath == SdfPath::AbsoluteRootPath()) {
                const SdfPathVector children = _GetInputSceneIndex()->GetChildPrimPaths(primPath);
                return std::find(children.begin(), children.end(), paths::auxiliary::materials) != children.end()
                           ? SdfPathVector { paths::auxiliary::materials }
                           : SdfPathVector {};
            }

            if (!isMaterialPath(primPath))
                return {};

            SdfPathVector result;
            for (const SdfPath& child : _GetInputSceneIndex()->GetChildPrimPaths(primPath)) {
                if (isMaterialPath(child))
                    result.push_back(child);
            }
            return result;
        }

    protected:
        void _PrimsAdded(const HdSceneIndexBase&, const HdSceneIndexObserver::AddedPrimEntries& entries) override
        {
            HdSceneIndexObserver::AddedPrimEntries filtered;
            for (const auto& entry : entries) {
                if (isMaterialPath(entry.primPath))
                    filtered.push_back(entry);
            }
            if (!filtered.empty())
                _SendPrimsAdded(filtered);
        }

        void _PrimsRemoved(const HdSceneIndexBase&, const HdSceneIndexObserver::RemovedPrimEntries& entries) override
        {
            HdSceneIndexObserver::RemovedPrimEntries filtered;
            for (const auto& entry : entries) {
                if (isMaterialPath(entry.primPath))
                    filtered.push_back(entry);
            }
            if (!filtered.empty())
                _SendPrimsRemoved(filtered);
        }

        void _PrimsDirtied(const HdSceneIndexBase&, const HdSceneIndexObserver::DirtiedPrimEntries& entries) override
        {
            HdSceneIndexObserver::DirtiedPrimEntries filtered;
            for (const auto& entry : entries) {
                if (isMaterialPath(entry.primPath))
                    filtered.push_back(entry);
            }
            if (!filtered.empty())
                _SendPrimsDirtied(filtered);
        }

        void _PrimsRenamed(const HdSceneIndexBase&, const HdSceneIndexObserver::RenamedPrimEntries& entries) override
        {
            HdSceneIndexObserver::RenamedPrimEntries renamed;
            HdSceneIndexObserver::RemovedPrimEntries removed;
            HdSceneIndexObserver::AddedPrimEntries added;
            const HdSceneIndexBaseRefPtr input = _GetInputSceneIndex();
            for (const auto& entry : entries) {
                const bool oldIsMaterial = isMaterialPath(entry.oldPrimPath);
                const bool newIsMaterial = isMaterialPath(entry.newPrimPath);
                if (oldIsMaterial && newIsMaterial)
                    renamed.push_back(entry);
                else if (oldIsMaterial)
                    removed.emplace_back(entry.oldPrimPath);
                else if (newIsMaterial && input) {
                    const HdSceneIndexPrim prim = input->GetPrim(entry.newPrimPath);
                    added.emplace_back(entry.newPrimPath, prim.primType);
                }
            }
            if (!renamed.empty())
                _SendPrimsRenamed(renamed);
            if (!removed.empty())
                _SendPrimsRemoved(removed);
            if (!added.empty())
                _SendPrimsAdded(added);
        }

    private:
        explicit AuxiliaryMaterialSceneIndex(const HdSceneIndexBaseRefPtr& input)
            : HdSingleInputFilteringSceneIndexBase(input)
        {}

        static bool isMaterialPath(const SdfPath& path)
        {
            return path == paths::auxiliary::materials || path.HasPrefix(paths::auxiliary::materials);
        }
    };

    struct SceneIndices {
        HdMergingSceneIndexRefPtr merging;
        TfRefPtr<RenderSceneIndex> renderSceneIndex;
        HdSceneIndexBaseRefPtr selectionInputScene;
        std::shared_ptr<SelectionMappingCache> selectionMappingCache = std::make_shared<SelectionMappingCache>();
        std::unique_ptr<SelectionMappingCacheObserver> selectionMappingObserver;
        UsdImagingSceneIndices auxiliary;
        HdSceneIndexBaseRefPtr auxiliaryMaterials;
        bool auxiliaryInserted = false;

        ~SceneIndices()
        {
            if (selectionInputScene && selectionMappingObserver)
                selectionInputScene->RemoveObserver(TfCreateWeakPtr(selectionMappingObserver.get()));
        }

        void clearAuxiliary()
        {
            auxiliary = {};
            auxiliaryMaterials = {};
            auxiliaryInserted = false;
        }
    };

    class ImagingGLEngine final : public UsdImagingGLEngine {
    public:
        explicit ImagingGLEngine(const UsdImagingGLEngine::Parameters& params)
            : ImagingGLEngine(params, std::make_shared<SceneIndices>())
        {}

        ~ImagingGLEngine() { removeRenderTask(); }

        SceneIndices& sceneIndices() { return *m_sceneIndices; }
        const SceneIndices& sceneIndices() const { return *m_sceneIndices; }

        bool resolveSelectionRprims(const UsdStageRefPtr& stage, const SdfPathVector& usdPaths,
                                    SdfPathVector* directPaths, std::vector<HydraSelectionRprim>* rprims)
        {
            if (!directPaths || !rprims)
                return false;
            directPaths->clear();
            rprims->clear();

            SceneIndices& indices = *m_sceneIndices;
            SelectionMappingCache* mappingCache = indices.selectionMappingCache.get();
            const bool canCacheClassification = mappingCache && mappingCache->ensure(indices.selectionInputScene);
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
                    needsHydraMapping = !prim || prim.IsInstance() || prim.IsInstanceProxy()
                                        || !prim.GetChildren().empty() || !UsdGeomGprim(prim);

                    if (!needsHydraMapping && indices.selectionInputScene) {
                        const HdSceneIndexPrim scenePrim = indices.selectionInputScene->GetPrim(path);
                        const HdContainerDataSourceHandle source = scenePrim.dataSource;
                        SdfPath prototype;
                        InstanceSelection instance;
                        const UsdImagingUsdPrimInfoSchema primInfo = UsdImagingUsdPrimInfoSchema::GetFromParent(source);
                        needsHydraMapping = instanceMapping(source, indices.selectionInputScene, &prototype, &instance)
                                            || static_cast<bool>(primInfo.GetPiPropagatedPrototypes());
                    }

                    if (canCacheClassification)
                        mappingCache->hydraMappingRequired.emplace(path, needsHydraMapping);
                }
                (needsHydraMapping ? hydraPaths : *directPaths).push_back(path);
            }
            return resolveHydraSelection(indices.selectionInputScene, indices.selectionMappingCache.get(), hydraPaths,
                                         rprims);
        }

        uint64_t selectionMappingGeneration() const
        {
            const std::shared_ptr<SelectionMappingCache>& cache = m_sceneIndices->selectionMappingCache;
            return cache ? cache->generation : 0;
        }

        void renderBatch(const SdfPathVector& paths, const UsdImagingGLRenderParams& params)
        {
            if (!_taskControllerSceneIndex || paths.empty())
                return;

            _UpdateHydraCollection(&_renderCollection, paths, params);
            _taskControllerSceneIndex->SetCollection(_renderCollection);
            _PrepareRender(params);
            _SetBBoxParams(params.bboxes, params.bboxLineColor, params.bboxLineDashSize);
            _taskControllerSceneIndex->SetEnableSelection(false);
            _taskControllerSceneIndex->SetEnablePresentation(true);

            const VtValue selectionValue(_selTracker);
            if (HdEngine* hdEngine = _GetHdEngine())
                hdEngine->SetTaskContextData(HdxTokens->selectionState, selectionValue);

            _Execute(params, _taskControllerSceneIndex->GetRenderingTaskPaths());
        }

        void renderBatchWithRenderTask(const SdfPathVector& paths, const UsdImagingGLRenderParams& params,
                                       const RenderTaskParams& renderTaskParams)
        {
            if (!_taskControllerSceneIndex || paths.empty())
                return;

            _UpdateHydraCollection(&_renderCollection, paths, params);
            _taskControllerSceneIndex->SetCollection(_renderCollection);
            _PrepareRender(params);

            _SetBBoxParams(params.bboxes, params.bboxLineColor, params.bboxLineDashSize);
            _taskControllerSceneIndex->SetEnableSelection(false);
            _taskControllerSceneIndex->SetEnablePresentation(true);

            const VtValue selectionValue(_selTracker);
            if (HdEngine* hdEngine = _GetHdEngine())
                hdEngine->SetTaskContextData(HdxTokens->selectionState, selectionValue);

            SdfPathVector taskPaths = _taskControllerSceneIndex->GetRenderingTaskPaths();

            if (renderTaskParams.enabled() && ensureRenderTask()) {
                // The custom scene-ID pass must use the exact same Hydra repr
                // selector as the beauty pass. UsdImagingGLEngine folds draw
                // mode and complexity/refinement into _renderCollection in
                // _UpdateHydraCollection(). Reusing that selector keeps the
                // primId silhouette identical to the viewport, including
                // subdiv/refined geometry at Medium/High/VeryHigh complexity.
                RenderTaskParams taskParams = renderTaskParams;
                taskParams.sceneIds.reprSelector = _renderCollection.GetReprSelector();
                m_renderTaskDelegate->setParams(m_renderTaskPath, taskParams);

                // The custom task must run after Storm has populated color/depth,
                // but before display transforms or presentation. Prefer identifying
                // the controller tasks by type and fall back to their conventional
                // path names for Hydra scene-index emulation builds.
                HdRenderIndex* renderIndex = _GetRenderIndex();
                auto insertionPoint = taskPaths.end();

                for (auto it = taskPaths.begin(); it != taskPaths.end(); ++it) {
                    bool insertBefore = false;

                    if (renderIndex && renderIndex->HasTask(*it)) {
                        const HdTaskSharedPtr& task = renderIndex->GetTask(*it);
                        insertBefore = dynamic_cast<HdxColorizeSelectionTask*>(task.get()) != nullptr
                                       || dynamic_cast<HdxColorCorrectionTask*>(task.get()) != nullptr
                                       || dynamic_cast<HdxPresentTask*>(task.get()) != nullptr;
                    }

                    if (!insertBefore) {
                        const std::string name = it->GetName();
                        insertBefore = name.find("colorizeSelection") != std::string::npos
                                       || name.find("selectionColorize") != std::string::npos
                                       || name.find("colorCorrection") != std::string::npos
                                       || name.find("present") != std::string::npos;
                    }

                    if (insertBefore) {
                        insertionPoint = it;
                        break;
                    }
                }

                taskPaths.insert(insertionPoint, m_renderTaskPath);
            }

            _Execute(params, taskPaths);
        }



        SdfPathVector takeCapturedVisiblePaths()
        {
            HdRenderIndex* renderIndex = _GetRenderIndex();
            if (!renderIndex || !renderIndex->HasTask(m_renderTaskPath))
                return {};

            const HdTaskSharedPtr& task = renderIndex->GetTask(m_renderTaskPath);
            auto* renderTask = dynamic_cast<RenderTask*>(task.get());
            return renderTask ? renderTask->takeCapturedVisiblePaths() : SdfPathVector {};
        }

        GpuPickResult takeGpuPickResult()
        {
            HdRenderIndex* index = _GetRenderIndex();
            if (!index || !index->HasTask(m_renderTaskPath))
                return {};
            const HdTaskSharedPtr& task = index->GetTask(m_renderTaskPath);
            auto* renderTask = dynamic_cast<RenderTask*>(task.get());
            return renderTask ? renderTask->takeGpuPickResult() : GpuPickResult {};
        }

        int32_t takeCapturedElementId()
        {
            HdRenderIndex* renderIndex = _GetRenderIndex();
            if (!renderIndex || !renderIndex->HasTask(m_renderTaskPath))
                return -1;

            const HdTaskSharedPtr& task = renderIndex->GetTask(m_renderTaskPath);
            auto* renderTask = dynamic_cast<RenderTask*>(task.get());
            return renderTask ? renderTask->takeCapturedElementId() : -1;
        }

    private:
        bool ensureRenderTask()
        {
            HdRenderIndex* renderIndex = _GetRenderIndex();
            if (!renderIndex)
                return false;

            if (!m_renderTaskDelegate)
                m_renderTaskDelegate = std::make_unique<RenderTaskDelegate>(renderIndex, SdfPath("/__Stageviz"));

            if (!renderIndex->HasTask(m_renderTaskPath))
                renderIndex->InsertTask<RenderTask>(m_renderTaskDelegate.get(), m_renderTaskPath);

            return renderIndex->HasTask(m_renderTaskPath);
        }

        void removeRenderTask()
        {
            HdRenderIndex* renderIndex = _GetRenderIndex();
            if (renderIndex && renderIndex->HasTask(m_renderTaskPath))
                renderIndex->RemoveTask(m_renderTaskPath);

            m_renderTaskDelegate.reset();
        }

        using SceneIndicesPtr = std::shared_ptr<SceneIndices>;

        ImagingGLEngine(const UsdImagingGLEngine::Parameters& params, const SceneIndicesPtr& sceneIndices)
            : UsdImagingGLEngine(prepareParameters(params, sceneIndices))
            , m_sceneIndices(sceneIndices)
        {
            constructingSceneIndices() = nullptr;
        }

        static SceneIndices*& constructingSceneIndices()
        {
            static thread_local SceneIndices* sceneIndices = nullptr;
            return sceneIndices;
        }

        static void registerSceneIndexFilters()
        {
            static std::once_flag registered;
            std::call_once(registered, [] {
                HdSceneIndexPluginRegistry::SceneIndexAppendCallback callback =
                    [](const std::string& renderInstanceId, const HdSceneIndexBaseRefPtr& inputScene,
                       const HdContainerDataSourceHandle& inputArgs) -> HdSceneIndexBaseRefPtr {
                    Q_UNUSED(renderInstanceId);
                    Q_UNUSED(inputArgs);

                    // Apply document presentation overrides before merging the
                    // auxiliary scene. Viewport support content such as the grid
                    // remains outside document-specific material overrides.
                    TfRefPtr<RenderSceneIndex> renderSceneIndex = RenderSceneIndex::New(inputScene);
                    HdMergingSceneIndexRefPtr mergingSceneIndex = HdMergingSceneIndex::New();

                    mergingSceneIndex->AddInputScene(renderSceneIndex, SdfPath::AbsoluteRootPath());

                    if (SceneIndices* sceneIndices = constructingSceneIndices()) {
                        sceneIndices->merging = mergingSceneIndex;
                        sceneIndices->renderSceneIndex = renderSceneIndex;
                        sceneIndices->selectionInputScene = inputScene;
                        sceneIndices->selectionMappingObserver = std::make_unique<SelectionMappingCacheObserver>(
                            sceneIndices->selectionMappingCache);
                        inputScene->AddObserver(TfCreateWeakPtr(sceneIndices->selectionMappingObserver.get()));
                    }

                    return mergingSceneIndex;
                };

                HdSceneIndexPluginRegistry::GetInstance().RegisterSceneIndexForRenderer(
                    std::string(), callback, nullptr, 0, HdSceneIndexPluginRegistry::InsertionOrderAtStart);
            });
        }

        static UsdImagingGLEngine::Parameters prepareParameters(UsdImagingGLEngine::Parameters params,
                                                                const SceneIndicesPtr& sceneIndices)
        {
            registerSceneIndexFilters();
            constructingSceneIndices() = sceneIndices.get();
            return params;
        }

        SceneIndicesPtr m_sceneIndices;
        std::unique_ptr<RenderTaskDelegate> m_renderTaskDelegate;
        const SdfPath m_renderTaskPath = SdfPath("/__Stageviz/Tasks/Render");
    };

    UsdImagingGLCullStyle cullStyle(RenderEngine::DoubleSidedMode mode)
    {
        switch (mode) {
        case RenderEngine::DoubleSidedMode::Primitive:
            return UsdImagingGLCullStyle::CULL_STYLE_BACK_UNLESS_DOUBLE_SIDED;

        case RenderEngine::DoubleSidedMode::SingleSided: return UsdImagingGLCullStyle::CULL_STYLE_BACK;

        case RenderEngine::DoubleSidedMode::DoubleSided:
        default: return UsdImagingGLCullStyle::CULL_STYLE_NOTHING;
        }
    }

}  // namespace

class RenderEngine::Private {
public:
    explicit Private(ContextMode mode)
        : contextMode(mode)
    {}

    bool initialize();
    bool ensureCurrentContext();
    void resetEngine();
    void reset();
    void ensureAuxiliarySceneIndex();
    void refreshAuxiliarySceneIndex();
    void updateDocumentRenderSceneIndex();
    void updateAuxiliaryRenderSceneIndex();
    void updateRenderParams();
    void updateLighting();
    bool render();

    ContextMode contextMode = ContextMode::Current;
    Settings settings;
    UsdStageRefPtr stage;
    UsdStageRefPtr auxiliary;
    GfCamera camera;
    GfVec2i size = GfVec2i(512, 512);
    GfVec4d viewport = GfVec4d(0.0);
    QList<SdfPath> mask;
    QList<SdfPath> selected;
    SdfPathVector cachedSelectedPaths;
    SdfPathVector cachedDirectSelectionPaths;
    std::vector<HydraSelectionRprim> cachedSelectedHydraRprims;
    bool cachedHydraSelectionResolved = false;
    std::vector<SelectionProxyTarget> cachedSelectedProxyTargets;
    std::vector<SelectionSubsetSettings> cachedSelectedSubsets;
    bool selectionCacheValid = false;
    uint64_t cachedSelectionMappingGeneration = 0;
    bool interactiveNavigation = false;
    QColor selectionColor = QColor(255, 210, 0);
    bool captureVisibleRequested = false;
    bool captureElementIdRequested = false;
    bool captureGpuPickRequested = false;
    GfVec2i gpuPickPixel = GfVec2i(-1, -1);
    SdfPathVector gpuPickExcludes;
    GpuPickResult capturedGpuPick;
    GfVec2i captureElementPixel = GfVec2i(-1, -1);
    int32_t capturedElementId = -1;
    int captureVisibleGridSize = 4;
    SdfPathVector capturedVisiblePaths;
    UsdImagingGLRenderParams params;
    std::unique_ptr<ImagingGLEngine> engine;
    std::unique_ptr<ImagingGLEngine> auxiliaryEngine;
    std::unique_ptr<QOpenGLContext> offscreenContext;
    std::unique_ptr<QOffscreenSurface> offscreenSurface;
};

bool
RenderEngine::Private::ensureCurrentContext()
{
    if (contextMode == ContextMode::Current)
        return QOpenGLContext::currentContext() != nullptr;

    if (!offscreenContext) {
        QSurfaceFormat format;
        format.setSamples(4);
        format.setDepthBufferSize(24);
        format.setStencilBufferSize(8);
        format.setAlphaBufferSize(8);
        format.setColorSpace(QColorSpace::SRgb);

        offscreenSurface = std::make_unique<QOffscreenSurface>();
        offscreenSurface->setFormat(format);
        offscreenSurface->create();

        if (!offscreenSurface->isValid())
            return false;

        offscreenContext = std::make_unique<QOpenGLContext>();
        offscreenContext->setFormat(format);

        if (QOpenGLContext::globalShareContext())
            offscreenContext->setShareContext(QOpenGLContext::globalShareContext());

        if (!offscreenContext->create()) {
            offscreenContext.reset();
            return false;
        }
    }

    return offscreenContext->makeCurrent(offscreenSurface.get());
}

bool
RenderEngine::Private::initialize()
{
    if (engine && (contextMode == ContextMode::Offscreen || auxiliaryEngine))
        return true;

    if (!ensureCurrentContext())
        return false;

    UsdImagingGLEngine::Parameters documentParams {};
    documentParams.displayUnloadedPrimsWithBounds = false;
    // Stageviz has no independent polling loop for Hydra's asynchronous scene
    // processing. Keep updates synchronous so scene-index changes are fully
    // applied during PrepareBatch before dependent work such as selection mapping.
    documentParams.allowAsynchronousSceneProcessing = false;

    engine = std::make_unique<ImagingGLEngine>(documentParams);
    if (!engine->GetHgi()) {
        engine.reset();
        return false;
    }

    // Auxiliary display content remains a separate presentation pass.
    // Selection is handled by the Stageviz RenderTask as a screen-space outline.
    // Offscreen rendering intentionally omits viewport-only auxiliary content.
    if (contextMode == ContextMode::Current) {
        UsdImagingGLEngine::Parameters overlayParams = documentParams;
        overlayParams.allowAsynchronousSceneProcessing = false;

        auxiliaryEngine = std::make_unique<ImagingGLEngine>(overlayParams);
        if (!auxiliaryEngine->GetHgi()) {
            auxiliaryEngine.reset();
            engine.reset();
            return false;
        }
    }

    updateDocumentRenderSceneIndex();
    updateAuxiliaryRenderSceneIndex();
    ensureAuxiliarySceneIndex();

    return true;
}

void
RenderEngine::Private::resetEngine()
{
    if (!engine && !auxiliaryEngine)
        return;

    if (contextMode == ContextMode::Offscreen) {
        OpenGLContextRestore contextRestore;

        if (!ensureCurrentContext()) {
            qWarning() << "could not make offscreen context current while resetting render engine";
            return;
        }

        auxiliaryEngine.reset();
        engine.reset();
        return;
    }

    auxiliaryEngine.reset();
    engine.reset();
}

void
RenderEngine::Private::reset()
{
    if (contextMode == ContextMode::Offscreen) {
        {
            OpenGLContextRestore contextRestore;

            if ((engine || auxiliaryEngine)
                && (!offscreenContext || !offscreenSurface || !offscreenContext->makeCurrent(offscreenSurface.get()))) {
                qWarning() << "could not make offscreen context current while releasing render engine";
                return;
            }

            engine.reset();
        }

        offscreenContext.reset();
        offscreenSurface.reset();
        return;
    }

    auxiliaryEngine.reset();
    engine.reset();
}

void
RenderEngine::Private::ensureAuxiliarySceneIndex()
{
    if (!auxiliary)
        return;

    for (ImagingGLEngine* target : { engine.get() }) {
        if (!target)
            continue;

        SceneIndices& sceneIndices = target->sceneIndices();

        if (sceneIndices.auxiliaryInserted || !sceneIndices.merging)
            continue;

        UsdImagingCreateSceneIndicesInfo createInfo;
        createInfo.stage = auxiliary;
        createInfo.displayUnloadedPrimsWithBounds = false;
        createInfo.addDrawModeSceneIndex = true;

        sceneIndices.auxiliary = UsdImagingCreateSceneIndices(createInfo);

        if (!sceneIndices.auxiliary.stageSceneIndex || !sceneIndices.auxiliary.finalSceneIndex) {
            sceneIndices.clearAuxiliary();
            continue;
        }

        // Keep auxiliary materials available for document/selection overrides,
        // but prune /Display so grid and helpers never enter their color/depth AOVs.
        sceneIndices.auxiliaryMaterials = AuxiliaryMaterialSceneIndex::New(sceneIndices.auxiliary.finalSceneIndex);
        sceneIndices.merging->AddInputScene(sceneIndices.auxiliaryMaterials, SdfPath::AbsoluteRootPath());
        sceneIndices.auxiliaryInserted = true;
    }
}

void
RenderEngine::Private::refreshAuxiliarySceneIndex()
{
    for (ImagingGLEngine* target : { engine.get() }) {
        if (!target)
            continue;

        SceneIndices& sceneIndices = target->sceneIndices();
        if (sceneIndices.auxiliary.stageSceneIndex)
            sceneIndices.auxiliary.stageSceneIndex->ApplyPendingUpdates();
    }
}

void
RenderEngine::Private::updateDocumentRenderSceneIndex()
{
    if (!engine)
        return;

    SceneIndices& sceneIndices = engine->sceneIndices();
    if (!sceneIndices.renderSceneIndex)
        return;

    TfRefPtr<RenderSceneIndex> renderSceneIndex = sceneIndices.renderSceneIndex;

    renderSceneIndex->setSceneMaterialsEnabled(settings.sceneMaterialsEnabled);
    renderSceneIndex->setMaterialPath(settings.overrideMaterial);

    RenderSceneIndex::Mode mode = RenderSceneIndex::None;
    switch (settings.materialMode) {
    case MaterialMode::Clay: mode = RenderSceneIndex::Clay; break;
    case MaterialMode::Override: mode = RenderSceneIndex::Custom; break;
    case MaterialMode::Scene:
    default: mode = RenderSceneIndex::None; break;
    }

    renderSceneIndex->setMode(mode);

    const bool overrideDoubleSided = settings.doubleSidedMode == DoubleSidedMode::DoubleSided;
    renderSceneIndex->setDoubleSidedOverride(false);
    renderSceneIndex->setDoubleSidedOverrideEnabled(overrideDoubleSided);
}

void
RenderEngine::Private::updateAuxiliaryRenderSceneIndex()
{
    if (!auxiliaryEngine)
        return;

    SceneIndices& sceneIndices = auxiliaryEngine->sceneIndices();
    if (!sceneIndices.renderSceneIndex)
        return;

    TfRefPtr<RenderSceneIndex> renderSceneIndex = sceneIndices.renderSceneIndex;
    renderSceneIndex->setSceneMaterialsEnabled(true);
    renderSceneIndex->setMaterialPath({});
    renderSceneIndex->setMode(RenderSceneIndex::None);
    renderSceneIndex->setDoubleSidedOverrideEnabled(false);
}


void
RenderEngine::Private::updateRenderParams()
{
    params.clearColor = qt::QColorToGfVec4f(settings.clearColor);
    params.drawMode = settings.drawMode;
    params.complexity = settings.complexity;
    params.cullStyle = cullStyle(settings.doubleSidedMode);
    params.enableLighting = true;
    params.gammaCorrectColors = settings.gammaCorrectColors;
    params.enableSampleAlphaToCoverage = settings.sampleAlphaToCoverageEnabled;
    params.enableSceneLights = settings.sceneLightsEnabled;
    params.enableSceneMaterials = true;
    params.flipFrontFacing = settings.flipFrontFacing;
    params.showGuides = settings.showGuides;
    params.showProxy = settings.showProxy;
    params.showRender = settings.showRender;
    params.highlight = false;
}

void
RenderEngine::Private::updateLighting()
{
    if (!engine && !auxiliaryEngine)
        return;

    std::vector<GlfSimpleLight> lights;

    if (settings.defaultCameraLightEnabled) {
        const GfMatrix4d cameraTransform = camera.GetTransform();
        const GfVec3d cameraPosition = cameraTransform.ExtractTranslation();

        GlfSimpleLight light;
        light.SetAmbient(GfVec4f(0, 0, 0, 0));
        light.SetPosition(GfVec4f(cameraPosition[0], cameraPosition[1], cameraPosition[2], 1.0f));
        light.SetTransform(cameraTransform);

        lights.push_back(light);
    }

    if (settings.defaultDomeLightEnabled) {
        GlfSimpleLight light;
        light.SetAmbient(GfVec4f(0, 0, 0, 0));
        light.SetPosition(GfVec4f(0, 0, 1, 0));
        light.SetIsDomeLight(true);

        if (stage && UsdGeomGetStageUpAxis(stage) == UsdGeomTokens->z) {
            GfMatrix4d domeTransform(1.0);
            domeTransform.SetRotate(GfRotation(GfVec3d(1.0, 0.0, 0.0), 90.0));
            light.SetTransform(domeTransform);
        }

        if (!settings.domeLightTexture.isEmpty()) {
            const std::string filename = settings.domeLightTexture.toStdString();
            light.SetDomeLightTextureFile(SdfAssetPath(filename, filename));
        }

        lights.push_back(light);
    }

    const GfVec4f ambient(settings.defaultAmbient, settings.defaultAmbient, settings.defaultAmbient, 1.0f);

    GlfSimpleMaterial material;
    material.SetAmbient(ambient);
    material.SetSpecular(GfVec4f(settings.defaultSpecular, settings.defaultSpecular, settings.defaultSpecular, 1.0f));
    material.SetShininess(settings.defaultShininess);

    if (engine)
        engine->SetLightingState(lights, material, ambient);
    if (auxiliaryEngine)
        auxiliaryEngine->SetLightingState(lights, material, ambient);
}

bool
RenderEngine::Private::render()
{
    if (!stage || size[0] <= 0 || size[1] <= 0)
        return false;

    if ((!engine || (contextMode == ContextMode::Current && !auxiliaryEngine)) && !initialize())
        return false;

    ensureAuxiliarySceneIndex();
    refreshAuxiliarySceneIndex();
    updateRenderParams();
    updateLighting();

    GfVec4d renderViewport = viewport;
    if (renderViewport[2] <= 0.0 || renderViewport[3] <= 0.0)
        renderViewport = GfVec4d(0, 0, size[0], size[1]);

    const GfFrustum frustum = camera.GetFrustum();
    const GfMatrix4d viewMatrix = frustum.ComputeViewMatrix();
    const GfMatrix4d projectionMatrix = frustum.ComputeProjectionMatrix();

    auto configureEngine = [&](ImagingGLEngine* target) {
        target->SetRendererSetting(TfToken("domeLightCameraVisibility"), VtValue(settings.domeLightCameraVisibility));
        target->SetRendererAov(settings.aov);
        target->SetRenderBufferSize(size);
        // CameraUtilFraming uses image coordinates (origin at the upper left),
        // while the widget's render viewport uses GL coordinates (origin at
        // the lower left).
        const double viewportTop = size[1] - (renderViewport[1] + renderViewport[3]);
        const double viewportBottom = size[1] - renderViewport[1];
        const GfVec2i viewportMin(static_cast<int>(std::floor(renderViewport[0])),
                                  static_cast<int>(std::floor(viewportTop)));
        const GfVec2i viewportMax(static_cast<int>(std::ceil(renderViewport[0] + renderViewport[2])),
                                  static_cast<int>(std::ceil(viewportBottom)));
        const GfVec2i framingMin(std::clamp(viewportMin[0], 0, size[0]), std::clamp(viewportMin[1], 0, size[1]));
        const GfVec2i framingMax(std::clamp(viewportMax[0], framingMin[0], size[0]),
                                 std::clamp(viewportMax[1], framingMin[1], size[1]));
        const GfRect2i dataWindow(framingMin, framingMax);
        const GfRange2f displayWindow { GfVec2f(framingMin), GfVec2f(framingMax) };
        target->SetFraming(CameraUtilFraming(displayWindow, dataWindow));
        target->SetWindowPolicy(CameraUtilMatchVertically);
        target->SetCameraState(viewMatrix, projectionMatrix);
    };

    configureEngine(engine.get());
    if (auxiliaryEngine)
        configureEngine(auxiliaryEngine.get());

    const UsdPrim root = stage->GetPseudoRoot();
    UsdImagingGLRenderParams documentParams = params;
    documentParams.highlight = false;
    Hgi* documentHgi = engine->GetHgi();
    if (!documentHgi)
        return false;

    documentHgi->StartFrame();
    engine->PrepareBatch(root, documentParams);
    updateDocumentRenderSceneIndex();

    // PrepareBatch applies pending USD stage-scene-index updates. Resolve the
    // selection afterward so the mapper and its cache observe the same current
    // Hydra hierarchy and instance topology that the render tasks will consume.
    const uint64_t mappingGeneration = engine->selectionMappingGeneration();
    if (!selectionCacheValid || cachedSelectionMappingGeneration != mappingGeneration) {
        cachedSelectedPaths.clear();
        cachedSelectedPaths.reserve(selected.size());
        cachedSelectedProxyTargets.clear();
        cachedSelectedSubsets.clear();
        std::unordered_set<SdfPath, SdfPath::Hash> selectedSubsetMeshes;

        for (const SdfPath& selectedPath : selected) {
            const SdfPath path = selectedPath.IsPropertyPath() ? selectedPath.GetPrimPath() : selectedPath;
            const UsdPrim prim = stage->GetPrimAtPath(path);
            const UsdGeomSubset subset(prim);
            // GeomSubsets are face groups in the USD hierarchy, not standalone
            // Hydra drawables. For outliner selection, highlight the complete
            // owning mesh while the selection model retains the subset path.
            const UsdPrim parent = subset ? prim.GetParent() : UsdPrim();
            const UsdPrim viewportPrim = parent ? parent : prim;
            const SdfPath viewportPath = viewportPrim ? viewportPrim.GetPath() : path;
            const bool newViewportPath = !viewportPath.IsEmpty()
                                         && (!subset || selectedSubsetMeshes.insert(viewportPath).second);
            if (!newViewportPath)
                continue;

            if (viewportPrim && viewportPrim.IsInstanceProxy()) {
                UsdPrim prototypePrim = viewportPrim.GetPrimInPrototype();
                UsdPrim prototypeRoot = prototypePrim;
                while (prototypeRoot && !prototypeRoot.IsPrototype())
                    prototypeRoot = prototypeRoot.GetParent();
                if (prototypePrim && prototypeRoot) {
                    cachedSelectedProxyTargets.push_back(
                        { viewportPath, prototypeRoot.GetPath(), prototypePrim.GetPath() });
                }
            }

            cachedSelectedPaths.push_back(viewportPath);
        }

        cachedSelectedHydraRprims.clear();
        cachedDirectSelectionPaths.clear();
        cachedHydraSelectionResolved = engine
                                       && engine->resolveSelectionRprims(stage, cachedSelectedPaths,
                                                                         &cachedDirectSelectionPaths,
                                                                         &cachedSelectedHydraRprims);
        cachedSelectionMappingGeneration = engine->selectionMappingGeneration();
        selectionCacheValid = true;
    }
    const auto& selectedPaths = cachedSelectedPaths;
    const auto& selectedSubsets = cachedSelectedSubsets;

    SdfPathVector renderPaths;
    if (mask.isEmpty()) {
        renderPaths.push_back(root.GetPath());
    }
    else {
        renderPaths.reserve(mask.size());
        for (const SdfPath& path : mask)
            renderPaths.push_back(path);
    }

    RenderTaskParams renderTaskParams;
    renderTaskParams.ambientOcclusion = settings.ambientOcclusion;
    renderTaskParams.ambientOcclusion.enabled = settings.ambientOcclusion.enabled && !interactiveNavigation
                                                && settings.aov == HdAovTokens->color
                                                && settings.drawMode != UsdImagingGLDrawMode::DRAW_WIREFRAME_ON_SURFACE;
    renderTaskParams.projectionMatrix = GfMatrix4f(projectionMatrix);

    renderTaskParams.selectionOutline.enabled = contextMode == ContextMode::Current
                                                && (!selectedPaths.empty() || !selectedSubsets.empty())
                                                && settings.aov == HdAovTokens->color;
    {
        renderTaskParams.selectionOutline.paths = selectedPaths;
        renderTaskParams.selectionOutline.directPaths = cachedDirectSelectionPaths;
        renderTaskParams.selectionOutline.hydraRprims = cachedSelectedHydraRprims;
        renderTaskParams.selectionOutline.hydraMappingResolved = cachedHydraSelectionResolved;
        renderTaskParams.selectionOutline.proxyTargets = cachedSelectedProxyTargets;
        renderTaskParams.selectionOutline.subsets = selectedSubsets;
    }
    renderTaskParams.selectionOutline.color = qt::QColorToGfVec4f(selectionColor);
    renderTaskParams.selectionOutline.radius = 2;

    renderTaskParams.captureVisible = captureVisibleRequested;
    renderTaskParams.captureElementId = captureElementIdRequested;
    renderTaskParams.captureGpuPick = captureGpuPickRequested;
    renderTaskParams.gpuPickPixel = gpuPickPixel;
    renderTaskParams.gpuPickExcludes = gpuPickExcludes;
    renderTaskParams.captureElementPixel = captureElementPixel;
    renderTaskParams.captureProjectionMatrices.clear();
    if (renderTaskParams.captureVisible) {
        const int gridSize = std::clamp(captureVisibleGridSize, 1, 8);
        if (gridSize > 1) {
            renderTaskParams.captureProjectionMatrices.reserve(static_cast<size_t>(gridSize * gridSize));

            const GfVec2d tileSize(1.0 / static_cast<double>(gridSize), 1.0 / static_cast<double>(gridSize));
            for (int ty = 0; ty < gridSize; ++ty) {
                for (int tx = 0; tx < gridSize; ++tx) {
                    // ComputeNarrowedFrustum expects the center in NDC and the
                    // size as a fraction of the original frustum. Rendering
                    // each narrow tile at the full viewport resolution gives
                    // gridSize-times finer linear coverage while keeping the
                    // ID buffer allocation unchanged.
                    const GfVec2d center(-1.0 + (2.0 * (static_cast<double>(tx) + 0.5) / static_cast<double>(gridSize)),
                                         1.0 - (2.0 * (static_cast<double>(ty) + 0.5) / static_cast<double>(gridSize)));
                    const GfFrustum tileFrustum = frustum.ComputeNarrowedFrustum(center, tileSize);
                    renderTaskParams.captureProjectionMatrices.push_back(tileFrustum.ComputeProjectionMatrix());
                }
            }
        }
    }
    renderTaskParams.sceneIds.enabled = renderTaskParams.selectionOutline.enabled || renderTaskParams.captureVisible
                                        || renderTaskParams.captureElementId || renderTaskParams.captureGpuPick;
    renderTaskParams.sceneIds.roots = renderPaths;
    renderTaskParams.sceneIds.renderTags = { HdRenderTagTokens->geometry };
    if (settings.showGuides)
        renderTaskParams.sceneIds.renderTags.push_back(HdRenderTagTokens->guide);
    if (settings.showProxy)
        renderTaskParams.sceneIds.renderTags.push_back(HdRenderTagTokens->proxy);
    if (settings.showRender)
        renderTaskParams.sceneIds.renderTags.push_back(HdRenderTagTokens->render);
    renderTaskParams.sceneIds.size = size;
    renderTaskParams.sceneIds.viewport = renderViewport;
    renderTaskParams.sceneIds.viewMatrix = viewMatrix;
    renderTaskParams.sceneIds.projectionMatrix = projectionMatrix;

    const GfRange1d nearFar = frustum.GetNearFar();
    renderTaskParams.nearClip = static_cast<float>(nearFar.GetMin());
    renderTaskParams.farClip = static_cast<float>(nearFar.GetMax());
    renderTaskParams.orthographic = camera.GetProjection() == GfCamera::Orthographic;

    if (renderTaskParams.ambientOcclusion.enabled || renderTaskParams.selectionOutline.enabled) {
        const QString shader = renderShaderPath();
        if (!shader.isEmpty()) {
            renderTaskParams.shaderPath = TfToken(shader.toStdString());
        }
        else {
            static bool warnedMissingShader = false;
            if (!warnedMissingShader) {
                warnedMissingShader = true;
                qWarning() << "render effects disabled: could not locate resources/Render.glslfx";
            }
            renderTaskParams.ambientOcclusion.enabled = false;
            renderTaskParams.selectionOutline.enabled = false;
            renderTaskParams.sceneIds.enabled = renderTaskParams.captureVisible;
        }
    }

    if (renderTaskParams.sceneIds.enabled) {
        const QString sceneIdShader = sceneIdShaderPath();
        if (!sceneIdShader.isEmpty()) {
            renderTaskParams.sceneIdShaderPath = TfToken(sceneIdShader.toStdString());
        }
        else {
            static bool warnedMissingSceneIdShader = false;
            if (!warnedMissingSceneIdShader) {
                warnedMissingSceneIdShader = true;
                qWarning() << "scene ID pass disabled: could not locate resources/SceneId.glslfx";
            }
            renderTaskParams.sceneIds.enabled = false;
            renderTaskParams.selectionOutline.enabled = false;
            renderTaskParams.captureVisible = false;
        }
    }
    engine->renderBatchWithRenderTask(renderPaths, documentParams, renderTaskParams);

    if (captureVisibleRequested) {
        capturedVisiblePaths = renderTaskParams.captureVisible ? engine->takeCapturedVisiblePaths() : SdfPathVector {};
        captureVisibleRequested = false;
    }

    if (captureGpuPickRequested) {
        capturedGpuPick = renderTaskParams.captureGpuPick ? engine->takeGpuPickResult() : GpuPickResult {};
        captureGpuPickRequested = false;
    }

    if (captureElementIdRequested) {
        capturedElementId = renderTaskParams.captureElementId ? engine->takeCapturedElementId() : -1;
        captureElementIdRequested = false;
    }

    documentHgi->EndFrame();

    // Render viewport support geometry after Look processing. The dedicated
    // pass keeps /Display out of document AO/depth while preserving Stageviz
    // grid and helper presentation in the final viewport.
    if (auxiliaryEngine && auxiliary && auxiliary->GetPrimAtPath(paths::auxiliary::display)) {
        Hgi* auxiliaryHgi = auxiliaryEngine->GetHgi();
        if (!auxiliaryHgi)
            return false;

        UsdImagingGLRenderParams auxiliaryParams = documentParams;
        auxiliaryParams.highlight = false;
        auxiliaryParams.clearColor = GfVec4f(0.0f);
        // Viewport support geometry must remain independent of document
        // refinement. The grid and helpers use fixed authored geometry and
        // should not change when scene complexity is adjusted for NURBS or
        // other refinable document primitives.
        auxiliaryParams.complexity = 1.0;

        const UsdPrim auxiliaryRoot = auxiliary->GetPseudoRoot();
        auxiliaryHgi->StartFrame();
        auxiliaryEngine->PrepareBatch(auxiliaryRoot, auxiliaryParams);
        updateAuxiliaryRenderSceneIndex();
        auxiliaryEngine->renderBatch({ paths::auxiliary::display }, auxiliaryParams);

        const SdfPath gizmoPath = paths::auxiliary::transform;
        if (auxiliary->GetPrimAtPath(gizmoPath)) {
            // Gizmo shading must not inherit the document's wireframe mode.
            UsdImagingGLRenderParams gizmoParams = auxiliaryParams;
            gizmoParams.drawMode = UsdImagingGLDrawMode::DRAW_SHADED_SMOOTH;
            // This pass intentionally clears depth but preserves the color
            // attachment. OpenGL state operations require a current GL context.
            glDepthMask(GL_TRUE);
            glClear(GL_DEPTH_BUFFER_BIT);
            auxiliaryEngine->renderBatch({ gizmoPath }, gizmoParams);
        }
        auxiliaryHgi->EndFrame();
    }


    return true;
}

RenderEngine::RenderEngine(ContextMode mode)
    : p(std::make_unique<Private>(mode))
{}

RenderEngine::~RenderEngine()
{
    if (p)
        p->reset();
}

bool
RenderEngine::initialize()
{
    if (p->contextMode == ContextMode::Offscreen) {
        OpenGLContextRestore contextRestore;
        return p->initialize();
    }

    return p->initialize();
}

void
RenderEngine::reset()
{
    p->reset();
}

bool
RenderEngine::isInitialized() const
{
    return p->engine != nullptr && (p->contextMode == ContextMode::Offscreen || p->auxiliaryEngine != nullptr);
}

void
RenderEngine::setStage(UsdStageRefPtr stage)
{
    if (p->stage == stage)
        return;

    p->stage = stage;
    p->selectionCacheValid = false;
    p->resetEngine();
}

UsdStageRefPtr
RenderEngine::stage() const
{
    return p->stage;
}

void
RenderEngine::setAuxiliaryStage(UsdStageRefPtr stage)
{
    if (p->auxiliary == stage)
        return;

    p->auxiliary = stage;
    p->resetEngine();
}

UsdStageRefPtr
RenderEngine::auxiliaryStage() const
{
    return p->auxiliary;
}

void
RenderEngine::refreshAuxiliaryStage()
{
    p->refreshAuxiliarySceneIndex();
}

void
RenderEngine::setCamera(const GfCamera& camera)
{
    p->camera = camera;
}

const GfCamera&
RenderEngine::camera() const
{
    return p->camera;
}

void
RenderEngine::setSize(const GfVec2i& size)
{
    p->size = size;
}

const GfVec2i&
RenderEngine::size() const
{
    return p->size;
}

void
RenderEngine::setViewport(const GfVec4d& viewport)
{
    p->viewport = viewport;
}

const GfVec4d&
RenderEngine::viewport() const
{
    return p->viewport;
}

void
RenderEngine::setSettings(const Settings& settings)
{
    p->settings = settings;
}

const RenderEngine::Settings&
RenderEngine::settings() const
{
    return p->settings;
}

void
RenderEngine::setMask(const QList<SdfPath>& paths)
{
    p->mask = paths;
}

void
RenderEngine::setSelected(const QList<SdfPath>& paths)
{
    if (paths == p->selected)
        return;

    std::set<SdfPath> seen;
    QList<SdfPath> normalized;
    normalized.reserve(paths.size());
    for (const SdfPath& path : paths) {
        const SdfPath primPath = path.IsPropertyPath() ? path.GetPrimPath() : path;
        if (primPath.IsEmpty() || primPath == SdfPath::AbsoluteRootPath())
            continue;
        if (seen.insert(primPath).second)
            normalized.append(primPath);
    }
    if (normalized == p->selected)
        return;
    p->selected = std::move(normalized);
    p->selectionCacheValid = false;
}

void
RenderEngine::invalidateSelectionCache()
{
    p->selectionCacheValid = false;
}

void
RenderEngine::setInteractiveNavigation(bool enabled)
{
    p->interactiveNavigation = enabled;
}

void
RenderEngine::setSelectionColor(const QColor& color)
{
    p->selectionColor = color;
}

bool
RenderEngine::renderToCurrentFramebuffer()
{
    if (!QOpenGLContext::currentContext())
        return false;

    if (!p->engine && !p->initialize())
        return false;

    glClearColor(p->settings.clearColor.redF(), p->settings.clearColor.greenF(), p->settings.clearColor.blueF(),
                 p->settings.clearColor.alphaF());

    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    if (!p->engine->IsColorCorrectionCapable())
        glEnable(GL_FRAMEBUFFER_SRGB);
    else
        glDisable(GL_FRAMEBUFFER_SRGB);

#ifdef WIN32
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glDepthFunc(GL_LESS);

    GfVec4d viewport = p->viewport;
    if (viewport[2] <= 0.0 || viewport[3] <= 0.0)
        viewport = GfVec4d(0, 0, p->size[0], p->size[1]);

    glViewport(static_cast<GLint>(viewport[0]), static_cast<GLint>(viewport[1]), static_cast<GLsizei>(viewport[2]),
               static_cast<GLsizei>(viewport[3]));
#endif  // WIN32

    return p->render();
}

QList<SdfPath>
RenderEngine::captureVisiblePaths(int gridSize)
{
    QList<SdfPath> result;

    if (p->contextMode != ContextMode::Current || !QOpenGLContext::currentContext())
        return result;

    p->captureVisibleRequested = true;
    p->captureVisibleGridSize = std::clamp(gridSize, 1, 8);
    p->capturedVisiblePaths.clear();

    if (!renderToCurrentFramebuffer()) {
        p->captureVisibleRequested = false;
        return result;
    }

    result.reserve(static_cast<qsizetype>(p->capturedVisiblePaths.size()));
    for (const SdfPath& path : p->capturedVisiblePaths)
        result.append(path);

    p->capturedVisiblePaths.clear();
    return result;
}

int
RenderEngine::captureElementIdAt(const GfVec2i& pixel)
{
    if (p->contextMode != ContextMode::Current || !QOpenGLContext::currentContext())
        return -1;

    p->captureElementIdRequested = true;
    p->captureElementPixel = pixel;
    p->capturedElementId = -1;

    if (!renderToCurrentFramebuffer()) {
        p->captureElementIdRequested = false;
        return -1;
    }

    const int result = static_cast<int>(p->capturedElementId);
    p->capturedElementId = -1;
    return result;
}

GpuPickResult
RenderEngine::gpuPickAt(const GfVec2i& pixel, const QList<SdfPath>& excludedPaths)
{
    if (p->contextMode != ContextMode::Current || !QOpenGLContext::currentContext())
        return {};

    p->captureGpuPickRequested = true;
    p->gpuPickPixel = pixel;
    p->gpuPickExcludes.assign(excludedPaths.begin(), excludedPaths.end());
    p->capturedGpuPick = GpuPickResult {};

    if (!renderToCurrentFramebuffer()) {
        p->captureGpuPickRequested = false;
        return {};
    }
    return p->capturedGpuPick;
}

SdfPath
RenderEngine::pickNextIdAt(const GfVec2i& pixel, const QList<SdfPath>& excludedPaths)
{
    return gpuPickAt(pixel, excludedPaths).path;
}

QImage
RenderEngine::renderImage()
{
    if (p->contextMode != ContextMode::Offscreen)
        return {};

    OpenGLContextRestore contextRestore;

    if (!p->ensureCurrentContext())
        return {};

    if (!p->engine && !p->initialize())
        return {};

    QOpenGLFramebufferObjectFormat format;
    format.setAttachment(QOpenGLFramebufferObject::CombinedDepthStencil);
    format.setSamples(4);

    QOpenGLFramebufferObject framebuffer(p->size[0], p->size[1], format);

    if (!framebuffer.isValid())
        return {};

    if (!framebuffer.bind())
        return {};

    const bool rendered = renderToCurrentFramebuffer();

    const QImage image = rendered ? framebuffer.toImage() : QImage();

    framebuffer.release();
    return image;
}

bool
RenderEngine::testIntersection(const UsdImagingGLEngine::PickParams& pickParams, const GfMatrix4d& viewMatrix,
                               const GfMatrix4d& projectionMatrix, const UsdPrim& root,
                               UsdImagingGLEngine::IntersectionResultVector* results)
{
    if (!results)
        return false;

    OpenGLContextRestore contextRestore;

    if (!p->engine && !p->initialize())
        return false;

    p->updateRenderParams();

    UsdImagingGLEngine::IntersectionResultVector rawResults;

    if (!p->engine->TestIntersection(pickParams, viewMatrix, projectionMatrix, root, p->params, &rawResults)) {
        results->clear();
        return false;
    }

    results->clear();
    results->reserve(rawResults.size());

    for (const auto& result : rawResults) {
        if (result.hitPrimPath.IsEmpty())
            continue;

        if (p->stage && !p->stage->GetPrimAtPath(result.hitPrimPath))
            continue;

        results->push_back(result);
    }

    return !results->empty();
}

QList<QString>
RenderEngine::rendererAovs() const
{
    QList<QString> result;

    if (!p->engine)
        return result;

    for (const TfToken& token : p->engine->GetRendererAovs())
        result.append(QString::fromStdString(token.GetString()));

    return result;
}

VtDictionary
RenderEngine::renderStats() const
{
    return p->engine ? p->engine->GetRenderStats() : VtDictionary();
}

QString
RenderEngine::hgiApiName() const
{
    if (!p->engine)
        return {};

    Hgi* hgi = p->engine->GetHgi();
    return hgi ? QString::fromStdString(hgi->GetAPIName().GetString()) : QString();
}

bool
RenderEngine::isColorCorrectionCapable() const
{
    return p->engine && p->engine->IsColorCorrectionCapable();
}

}  // namespace stageviz
