"""Keep scene notification paths separate from mapped Sdf authoring paths."""
from pathlib import Path
p=Path('sources/commands/commandproperty.cpp');s=p.read_text(encoding='utf-8')
start=s.index('    struct AttributeSpecBatchState')
end=s.index('\nCommand\naddAttributes(',start)
t=s[start:end]
t=t.replace('            SdfPath path;','            SdfPath path;\n            SdfPath specPath;',1)
t=t.replace('''                                const UsdPrim prim = stage->GetPrimAtPath(propPath.GetPrimPath());''','''                                const SdfPath specPath = stage->GetEditTarget().MapToSpecPath(propPath);
                                if (specPath.IsEmpty()) {
                                    errors.append(QString("Cannot map attribute: %1").arg(pathText(propPath)));
                                    continue;
                                }
                                const UsdPrim prim = stage->GetPrimAtPath(propPath.GetPrimPath());''')
t=t.replace('layer->GetPropertyAtPath(propPath)','layer->GetPropertyAtPath(specPath)')
t=t.replace('item.path = propPath;','item.path = propPath;\n                                item.specPath = specPath;')
t=t.replace('SdfCreatePrimInLayer(item.snapshot, propPath.GetPrimPath());','SdfCreatePrimInLayer(item.snapshot, specPath.GetParentPath());')
t=t.replace('SdfCopySpec(layer, propPath, item.snapshot, propPath)','SdfCopySpec(layer, specPath, item.snapshot, specPath)')
t=t.replace('struct Plan { SdfPath path; SdfVariability variability; };','struct Plan { SdfPath path, specPath; SdfVariability variability; };')
t=t.replace('plans.append({ item.path, attr ?', 'plans.append({ item.path, item.specPath, attr ?')
t=t.replace('removePropertySpec(layer, plan.path)','removePropertySpec(layer, plan.specPath)')
t=t.replace('ensureAttributeSpec(layer, plan.path, type','ensureAttributeSpec(layer, plan.specPath, type')
t=t.replace('removePropertySpec(layer, item.path)','removePropertySpec(layer, item.specPath)')
t=t.replace('SdfCreatePrimInLayer(layer, item.path.GetPrimPath());','SdfCreatePrimInLayer(layer, item.specPath.GetParentPath());')
t=t.replace('SdfCopySpec(item.snapshot, item.path, layer, item.path)','SdfCopySpec(item.snapshot, item.specPath, layer, item.specPath)')
s=s[:start]+t+s[end:]
start=s.index('Command\nrenameAttributes(')
end=s.index('\nCommand\nsetAttributeValues(',start)
t=s[start:end]
t=t.replace('            SdfPath newPath;','            SdfPath newPath;\n            SdfPath oldSpecPath, newSpecPath;',1)
t=t.replace('''                                if (!layer->GetPropertyAtPath(oldPath)) {''','''                                const SdfPath oldSpecPath = stage->GetEditTarget().MapToSpecPath(oldPath);
                                const SdfPath newPath = oldPath.GetPrimPath().AppendProperty(newName);
                                const SdfPath newSpecPath = stage->GetEditTarget().MapToSpecPath(newPath);
                                if (oldSpecPath.IsEmpty() || newSpecPath.IsEmpty()) {
                                    errors.append(QString("Cannot map attribute: %1").arg(pathText(oldPath)));
                                    continue;
                                }
                                if (!layer->GetPropertyAtPath(oldSpecPath)) {''')
t=t.replace('''                                const SdfPath newPath = oldPath.GetPrimPath().AppendProperty(newName);
                                state->items.append({ oldPath, newPath, true });''','''                                state->items.append({ oldPath, newPath, oldSpecPath, newSpecPath, true });''')
t=t.replace('                            SdfPath oldPath, newPath;','                            SdfPath oldPath, newPath, oldSpecPath, newSpecPath;')
t=t.replace('layer->GetPropertyAtPath(item.newPath)','layer->GetPropertyAtPath(item.newSpecPath)')
t=t.replace('''                            source.Get(&value);
                            plans.append({ item.oldPath, item.newPath, source.GetTypeName(), source.GetVariability(),''','''                            if (!item.removeOriginal)
                                source.Get(&value);
                            plans.append({ item.oldPath, item.newPath, item.oldSpecPath, item.newSpecPath,
                                           source.GetTypeName(), source.GetVariability(),''')
t=t.replace('layer->GetPropertyAtPath(plan.oldPath)','layer->GetPropertyAtPath(plan.oldSpecPath)')
t=t.replace('SdfCopySpec(layer, plan.oldPath, layer, plan.newPath)','SdfCopySpec(layer, plan.oldSpecPath, layer, plan.newSpecPath)')
t=t.replace('layer->SetField(plan.newPath,','layer->SetField(plan.newSpecPath,')
t=t.replace('removePropertySpec(layer, plan.oldPath)','removePropertySpec(layer, plan.oldSpecPath)')
t=t.replace('ensureAttributeSpec(layer, plan.newPath,','ensureAttributeSpec(layer, plan.newSpecPath,')
t=t.replace('SdfCopySpec(layer, item.newPath, layer, item.oldPath)','SdfCopySpec(layer, item.newSpecPath, layer, item.oldSpecPath)')
t=t.replace('removePropertySpec(layer, item.newPath)','removePropertySpec(layer, item.newSpecPath)')
s=s[:start]+t+s[end:]
p.write_text(s,encoding='utf-8')
