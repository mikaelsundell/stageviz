// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 - present Mikael Sundell

#include "commandinstanceutils.h"
#include "qtutils.h"
#include <QHash>
#include <algorithm>
#include <pxr/base/tf/errorMark.h>
#include <pxr/usd/sdf/listOp.h>
#include <pxr/usd/sdf/payload.h>
#include <pxr/usd/sdf/reference.h>
#include <pxr/usd/sdf/schema.h>
#include <pxr/usd/usd/primCompositionQuery.h>
#include <pxr/usd/usd/primRange.h>
#include <set>

namespace stageviz::command {
namespace {

    SdfPath rootPath(const SdfPath& path)
    {
        const auto prefixes = path.GetPrimPath().StripAllVariantSelections().GetPrefixes();
        return prefixes.empty() ? SdfPath() : prefixes.front();
    }

    bool isFlattenedSource(const SdfLayerHandle& layer, const SdfPath& path)
    {
        if (!path.IsRootPrimPath() || path.GetName().rfind("Flattened_Prototype_", 0) != 0)
            return false;
        const auto spec = layer->GetPrimAtPath(path);
        return spec && spec->GetSpecifier() == SdfSpecifierOver && spec->GetTypeName().IsEmpty();
    }

    bool collectLayers(const SdfLayerHandle& first, QList<SdfLayerRefPtr>& layers, std::set<std::string>& seen,
                       bool sublayersOnly)
    {
        if (!first || !seen.insert(first->GetIdentifier()).second)
            return true;
        layers.append(first);
        std::vector<std::string> assets;
        if (sublayersOnly)
            assets = first->GetSubLayerPaths();
        else {
            const auto dependencies = first->GetCompositionAssetDependencies();
            assets.assign(dependencies.begin(), dependencies.end());
        }
        for (const auto& asset : assets) {
            // An empty asset path denotes an internal reference or payload.
            if (asset.empty())
                continue;
            TfErrorMark mark;
            const auto layer = SdfLayer::FindOrOpenRelativeToLayer(first, asset);
            const bool clean = mark.IsClean();
            mark.Clear();
            if (!layer || !clean || !collectLayers(layer, layers, seen, sublayersOnly))
                return false;
        }
        return true;
    }

}  // namespace

QSet<SdfPath>
flattenedInstanceSources(const UsdStageRefPtr& stage, const SdfLayerHandle& editLayer, const QList<SdfPath>& paths)
{
    QSet<SdfPath> sources;
    if (!stage || !editLayer)
        return sources;
    bool hasSources = false;
    for (const auto& spec : editLayer->GetRootPrims())
        hasSources |= isFlattenedSource(editLayer, spec->GetPath());
    if (!hasSources)
        return sources;
    QSet<SdfPath> prototypes;
    for (const auto& path : paths) {
        const auto root = stage->GetPrimAtPath(path);
        if (!root)
            continue;
        const auto range = UsdPrimRange(root, UsdTraverseInstanceProxies());
        for (auto it = range.begin(); it != range.end(); ++it) {
            if (it->IsInstance()) {
                const auto prototype = it->GetPrototype().GetPath();
                if (prototypes.contains(prototype)) {
                    it.PruneChildren();
                    continue;
                }
                prototypes.insert(prototype);
            }
            else if (!it->HasAuthoredReferences() && !it->HasAuthoredPayloads())
                continue;
            UsdPrimCompositionQuery query(*it);
            for (const auto& arc : query.GetCompositionArcs()) {
                if (arc.GetArcType() == PcpArcTypeRoot || arc.GetTargetLayer() != stage->GetRootLayer())
                    continue;
                const auto source = rootPath(arc.GetTargetPrimPath());
                if (isFlattenedSource(editLayer, source))
                    sources.insert(source);
            }
        }
    }
    return sources;
}

QList<SdfPath>
unusedFlattenedInstanceSources(const UsdStageRefPtr& stage, const SdfLayerHandle& editLayer, const QSet<SdfPath>& input)
{
    if (!stage || !editLayer || input.isEmpty())
        return {};

    QList<SdfLayerRefPtr> layers;
    std::set<std::string> seen;
    if (!collectLayers(stage->GetRootLayer(), layers, seen, false)
        || !collectLayers(stage->GetSessionLayer(), layers, seen, false))
        return {};
    QList<SdfLayerRefPtr> localLayers;
    std::set<std::string> local;
    if (!collectLayers(stage->GetRootLayer(), localLayers, local, true)
        || !collectLayers(stage->GetSessionLayer(), localLayers, local, true))
        return {};

    QSet<SdfPath> candidates;
    for (const auto& source : input) {
        if (!isFlattenedSource(editLayer, source))
            continue;
        bool sharedOpinion = false;
        for (const auto& layer : layers)
            sharedOpinion |= layer != editLayer && bool(layer->GetPrimAtPath(source));
        if (!sharedOpinion)
            candidates.insert(source);
    }
    if (candidates.isEmpty())
        return {};

    // These layer stacks contain the candidate specs. Internal references in
    // any local sublayer also resolve within the document's whole layer stack.
    std::set<std::string> sourceStacks { editLayer->GetIdentifier() };
    bool changed = true;
    while (changed) {
        changed = false;
        for (const auto& layer : layers) {
            if (sourceStacks.count(layer->GetIdentifier()))
                continue;
            for (const auto& sublayer : layer->GetSubLayerPaths()) {
                const auto child = SdfLayer::FindRelativeToLayer(layer, sublayer);
                if (child && sourceStacks.count(child->GetIdentifier())) {
                    sourceStacks.insert(layer->GetIdentifier());
                    changed = true;
                    break;
                }
            }
        }
    }

    QSet<SdfPath> retained;
    QHash<SdfPath, QSet<SdfPath>> dependencies;
    const auto use = [&](const SdfLayerHandle& layer, const SdfPath& owner, const SdfPath& target) {
        const auto source = rootPath(target);
        if (!candidates.contains(source))
            return;
        const auto ownerRoot = rootPath(owner);
        if (layer == editLayer && candidates.contains(ownerRoot))
            dependencies[ownerRoot].insert(source);
        else
            retained.insert(source);
    };
    for (const auto& layer : layers) {
        for (const auto& pair : layer->GetRelocates()) {
            use(layer, SdfPath::AbsoluteRootPath(), pair.first);
            use(layer, SdfPath::AbsoluteRootPath(), pair.second);
        }
        if (sourceStacks.count(layer->GetIdentifier()) && layer->HasDefaultPrim())
            use(layer, SdfPath::AbsoluteRootPath(), layer->GetDefaultPrimAsPath());
        layer->Traverse(SdfPath::AbsoluteRootPath(), [&](const SdfPath& owner) {
            // Read the authored list operations, not just composed opinions:
            // unselected variants, inactive prims and muted layers count too.
            for (const auto& field : { SdfFieldKeys->References, SdfFieldKeys->Payload }) {
                const auto value = layer->GetField(owner, field);
                const auto arcUse = [&](const auto& arc) {
                    if (arc.GetAssetPath().empty()) {
                        if (local.count(layer->GetIdentifier()))
                            use(layer, owner, arc.GetPrimPath());
                    }
                    else {
                        const auto target = SdfLayer::FindRelativeToLayer(layer, arc.GetAssetPath());
                        if (target && sourceStacks.count(target->GetIdentifier()))
                            use(layer, owner,
                                arc.GetPrimPath().IsEmpty() ? target->GetDefaultPrimAsPath() : arc.GetPrimPath());
                    }
                };
                if (value.IsHolding<SdfReferenceListOp>())
                    for (const auto& arc : value.UncheckedGet<SdfReferenceListOp>().GetAppliedItems())
                        arcUse(arc);
                if (value.IsHolding<SdfPayloadListOp>())
                    for (const auto& arc : value.UncheckedGet<SdfPayloadListOp>().GetAppliedItems())
                        arcUse(arc);
            }
            for (const auto& field : { SdfFieldKeys->InheritPaths, SdfFieldKeys->Specializes, SdfFieldKeys->TargetPaths,
                                       SdfFieldKeys->ConnectionPaths }) {
                const auto value = layer->GetField(owner, field);
                if (!value.IsHolding<SdfPathListOp>())
                    continue;
                for (const auto& path : value.UncheckedGet<SdfPathListOp>().GetAppliedItems())
                    use(layer, owner,
                        path.IsAbsolutePath() ? path
                                              : path.MakeAbsolutePath(owner.GetPrimPath().StripAllVariantSelections()));
            }
            // Relocations can preserve a source under another scene path.
            const auto relocates = layer->GetField(owner, SdfFieldKeys->Relocates);
            if (relocates.IsHolding<SdfRelocatesMap>())
                for (const auto& pair : relocates.UncheckedGet<SdfRelocatesMap>()) {
                    use(layer, SdfPath::AbsoluteRootPath(), pair.first);
                    use(layer, SdfPath::AbsoluteRootPath(), pair.second);
                }
        });
    }
    QList<SdfPath> queue = retained.values();
    for (qsizetype i = 0; i < queue.size(); ++i)
        for (const auto& source : dependencies.value(queue[i]))
            if (!retained.contains(source)) {
                retained.insert(source);
                queue.append(source);
            }
    QList<SdfPath> unused;
    for (const auto& source : candidates)
        if (!retained.contains(source))
            unused.append(source);
    std::sort(unused.begin(), unused.end());
    return unused;
}

}  // namespace stageviz::command
