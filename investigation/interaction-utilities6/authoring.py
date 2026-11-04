"""Transform authoring correction: preflight USD, batch Sdf, then query USD."""
from pathlib import Path
p=Path('sources/commands/commandtransform.cpp')
s=p.read_text(encoding='utf-8')
s=s.replace('#include <pxr/usd/sdf/changeBlock.h>', '#include <pxr/usd/sdf/changeBlock.h>\n#include <pxr/usd/sdf/attributeSpec.h>\n#include <pxr/usd/sdf/primSpec.h>')
start=s.index('    QList<GfMatrix4d> localPivots;',s.index('prepareTransforms(const'))
end=s.index('\n    UsdGeomXformCache xformCache',start)
s=s[:start]+'''    QList<GfMatrix4d> localPivots;
    QList<bool> resetStacks;
    struct Plan {
        SdfPath specPath;
        GfMatrix4d local;
        VtTokenArray order;
    };
    QList<Plan> plans;
    plans.reserve(paths.size());
    const UsdEditTarget editTarget = stage->GetEditTarget();
    const SdfLayerHandle editLayer = editTarget.GetLayer();
    UsdGeomXformCache parentCache(UsdTimeCode::Default());

    // USD queries must finish before opening SdfChangeBlock: composition and
    // attribute handles are stale until the block's changes have been delivered.
    for (qsizetype i = 0; i < paths.size(); ++i) {
        const auto& path = paths.at(i);
        const UsdPrim prim = stage->GetPrimAtPath(path);
        const UsdGeomXformable xformable(prim);
        Plan plan;
        plan.specPath = editTarget.MapToSpecPath(path);
        if (!editLayer || !editLayer->PermissionToEdit() || plan.specPath.IsEmpty()
            || !prim || prim.IsInstanceProxy() || prim.IsInPrototype() || !xformable) {
            if (errors)
                errors->append(QString("invalid transform authoring target: %1").arg(pathText(path)));
            return {};
        }
        GfMatrix4d localPivot(1.0);
        const bool resetsStack = xformable.GetResetXformStack();
        VtTokenArray oldOrder;
        xformable.GetXformOpOrderAttr().Get(&oldOrder);
        TfToken pivotName;
        for (const auto& name : { TfToken("xformOp:transform:pivot"), TfToken("xformOp:translate:pivot") }) {
            const auto forward = std::find(oldOrder.begin(), oldOrder.end(), name);
            const auto inverse = std::find(oldOrder.begin(), oldOrder.end(), TfToken("!invert!" + name.GetString()));
            if (forward != oldOrder.end() && inverse != oldOrder.end() && forward < inverse) {
                pivotName = name;
                localPivot = UsdGeomXformOp(prim.GetAttribute(name)).GetOpTransform(UsdTimeCode::Default());
                break;
            }
        }
        GfMatrix4d parentWorld(1.0);
        const UsdPrim parent = prim.GetParent();
        if (!resetsStack && parent && !parent.IsPseudoRoot())
            parentWorld = parentCache.GetLocalToWorldTransform(parent);
        plan.local = localPivot * baseMatrices.at(i) * parentWorld.GetInverse() * localPivot.GetInverse();
        if (resetsStack)
            plan.order.push_back(TfToken("!resetXformStack!"));
        if (!pivotName.IsEmpty())
            plan.order.push_back(pivotName);
        plan.order.push_back(TfToken("xformOp:transform"));
        if (!pivotName.IsEmpty())
            plan.order.push_back(TfToken("!invert!" + pivotName.GetString()));
        plans.append(plan);
        localPivots.append(localPivot);
        resetStacks.append(resetsStack);
    }
    {
        SdfChangeBlock changeBlock;
        for (const auto& plan : plans) {
            const auto primSpec = SdfCreatePrimInLayer(editLayer, plan.specPath);
            if (!primSpec) {
                if (errors)
                    errors->append(QString("failed to create transform spec: %1").arg(pathText(plan.specPath)));
                return {};
            }
            const auto matrixPath = plan.specPath.AppendProperty(TfToken("xformOp:transform"));
            auto matrix = editLayer->GetAttributeAtPath(matrixPath);
            if (!matrix)
                matrix = SdfAttributeSpec::New(primSpec, "xformOp:transform", SdfValueTypeNames->Matrix4d,
                                               SdfVariabilityVarying, false);
            const auto orderPath = plan.specPath.AppendProperty(TfToken("xformOpOrder"));
            auto order = editLayer->GetAttributeAtPath(orderPath);
            if (!order)
                order = SdfAttributeSpec::New(primSpec, "xformOpOrder", SdfValueTypeNames->TokenArray,
                                              SdfVariabilityUniform, false);
            if (!matrix || !order || matrix->GetTypeName() != SdfValueTypeNames->Matrix4d) {
                if (errors)
                    errors->append(QString("invalid transform attribute spec: %1").arg(pathText(plan.specPath)));
                return {};
            }
            matrix->SetDefaultValue(VtValue(plan.local));
            order->SetDefaultValue(VtValue(plan.order));
        }
    }
    const path::PathIndex selectedRoots(paths);
''' +s[end:]
s=s.replace('        prepared.path = path;','        prepared.path = path;\n        prepared.editLayer = editLayer;\n        prepared.matrixSpecPath = plans.at(i).specPath.AppendProperty(TfToken("xformOp:transform"));')
s=s.replace('            if (paths.contains(ancestor.GetPath())) {','            if (selectedRoots.contains(ancestor.GetPath())) {')
start=s.index('    bool success = true;\n    SdfChangeBlock changeBlock;',s.index('applyPreparedTransforms(const'))
end=s.index('\n    return success;',start)
s=s[:start]+'''    bool success = true;
    struct Update { SdfLayerHandle layer; SdfPath specPath; GfMatrix4d local; SdfPath path; };
    QList<Update> updates;
    QList<qsizetype> fallback;
    const UsdEditTarget editTarget = stage->GetEditTarget();
    for (qsizetype i = 0; i < prepared.size(); ++i) {
        const auto& item = prepared.at(i);
        const UsdPrim prim = stage->GetPrimAtPath(item.path);
        if (!prim || prim.IsInstanceProxy() || prim.IsInPrototype()) {
            if (errors)
                errors->append(QString("invalid transform authoring target: %1").arg(pathText(item.path)));
            success = false;
            continue;
        }
        if (item.fast && item.editLayer && item.editLayer == editTarget.GetLayer()
            && item.matrixSpecPath == editTarget.MapToSpecPath(item.path).AppendProperty(TfToken("xformOp:transform"))
            && item.editLayer->PermissionToEdit() && item.editLayer->GetAttributeAtPath(item.matrixSpecPath)
            && (!item.parentFollowsSelection || hasSelectionDelta)) {
            const GfMatrix4d parentWorld = item.parentFollowsSelection
                                              ? item.parentWorld * selectionDelta : item.parentWorld;
            updates.append({ item.editLayer, item.matrixSpecPath,
                             item.localPivot * matrices.at(i) * parentWorld.GetInverse() * item.localPivot.GetInverse(),
                             item.path });
        }
        else
            fallback.append(i);
    }
    {
        SdfChangeBlock changeBlock;
        for (const auto& update : updates)
            update.layer->SetField(update.specPath, SdfFieldKeys->Default, VtValue(update.local));
    }
    QList<SdfPath> ignored;
    path::PathCollector collector(changed ? *changed : ignored);
    for (const auto& update : updates)
        collector.append(update.path);
    // The generic USD path may read composed transforms. Keep it outside the
    // Sdf batch so it observes all prior changes, including selected parents.
    for (qsizetype i : fallback) {
        QString error;
        if (stage::setWorldTransform(stage, prepared.at(i).path, matrices.at(i), error))
            collector.append(prepared.at(i).path);
        else {
            success = false;
            if (errors)
                errors->append(error);
        }
    }
''' +s[end:]
# Use mapped paths for capture and restore; record exactly the ancestor overs we introduce.
s=s.replace('        state.hadPrimSpec = bool(layer->GetPrimAtPath(path));\n        const SdfPath orderPath = path.AppendProperty', '''        state.specPath = stage->GetEditTarget().MapToSpecPath(path);
        if (state.specPath.IsEmpty()) {
            error = QString("unmappable transform target: %1").arg(pathText(path));
            return false;
        }
        for (SdfPath ancestor = state.specPath.GetParentPath(); ancestor.IsPrimPath(); ancestor = ancestor.GetParentPath()) {
            if (!layer->GetPrimAtPath(ancestor))
                state.missingAncestors.append(ancestor);
        }
        state.hadPrimSpec = bool(layer->GetPrimAtPath(state.specPath));
        const SdfPath orderPath = state.specPath.AppendProperty''')
s=s.replace('        const SdfPath matrixPath = path.AppendProperty(TfToken("xformOp:transform"));','        const SdfPath matrixPath = state.specPath.AppendProperty(TfToken("xformOp:transform"));')
# restoreXformState is Sdf-only; choose persisted namespace mapping, independent of current edit target.
start=s.index('    bool restoreXformState(')
end=s.index('    bool hasUnderlyingTransformOpinion',start)
part=s[start:end].replace('        const SdfPath orderPath = primPath.AppendProperty', '        const SdfPath specPath = state.specPath.IsEmpty() ? primPath : state.specPath;\n        const SdfPath orderPath = specPath.AppendProperty').replace('const SdfPath matrixPath = primPath.AppendProperty','const SdfPath matrixPath = specPath.AppendProperty').replace('editLayer->GetPrimAtPath(primPath)','editLayer->GetPrimAtPath(specPath)').replace('stage::removePrimSpec(editLayer, primPath)','stage::removePrimSpec(editLayer, specPath)')
s=s[:start]+part+s[end:]
# Prune ancestors only after every target has been restored, deepest first.
start=s.index('    return success;',s.index('restoreTransformOperation(const'))
s=s[:start]+'''    QList<SdfPath> ancestors;
    path::PathCollector collector(ancestors);
    for (const auto& state : operation.edit.beforeState) {
        for (const auto& ancestor : state.missingAncestors)
            collector.append(ancestor);
    }
    std::sort(ancestors.begin(), ancestors.end(), [](const SdfPath& a, const SdfPath& b) {
        return a.GetPathElementCount() > b.GetPathElementCount();
    });
    for (const auto& ancestor : ancestors) {
        const auto spec = layer->GetPrimAtPath(ancestor);
        if (spec && spec->IsInert())
            stage::removePrimSpec(layer, ancestor);
    }
''' +s[start:]
p.write_text(s,encoding='utf-8')
