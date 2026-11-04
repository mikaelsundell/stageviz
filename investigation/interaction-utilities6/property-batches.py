"""Remove downstream USD queries from the audited property Sdf batches."""
from pathlib import Path
p=Path('sources/commands/commandproperty.cpp');s=p.read_text(encoding='utf-8')
start=s.index('                        SdfChangeBlock block;',s.index('static Command\nattributeSpecBatch'))
end=s.index('\n                    }\n                }\n                command::queueToSession',start)
s=s[:start]+'''                        struct Plan { SdfPath path; SdfVariability variability; };
                        QList<Plan> plans;
                        // USD validation and schema discovery precede Sdf edits.
                        for (const auto& item : state->items) {
                            const auto prim = stage->GetPrimAtPath(item.path.GetPrimPath());
                            if (!prim)
                                continue;
                            if (removing && prim.GetPrimDefinition().GetAttributeDefinition(item.path.GetNameToken())) {
                                errors.append(QString("Cannot remove built-in attribute %1").arg(pathText(item.path)));
                                continue;
                            }
                            const auto attr = prim.GetAttribute(item.path.GetNameToken());
                            plans.append({ item.path, attr ? attr.GetVariability() : SdfVariabilityVarying });
                        }
                        const VtValue fallback = type ? type.GetDefaultValue() : VtValue();
                        SdfChangeBlock block;
                        for (const auto& plan : plans) {
                            bool ok = false;
                            if (removing)
                                ok = removePropertySpec(layer, plan.path);
                            else {
                                const auto created = layer::ensureAttributeSpec(layer, plan.path, type,
                                                                                plan.variability, custom);
                                ok = bool(created);
                                if (ok && !fallback.IsEmpty())
                                    ok = created->SetDefaultValue(fallback);
                            }
                            if (ok)
                                affectedCollector.append(plan.path.GetPrimPath());
                            else
                                errors.append(QString("Attribute operation failed: %1").arg(pathText(plan.path)));
                        }''' +s[end:]
start=s.index('                        SdfChangeBlock block;',s.index('renameAttributes('))
end=s.index('\n                    }\n                }\n                command::queueToSession',start)
s=s[:start]+'''                        struct Plan {
                            SdfPath oldPath, newPath;
                            SdfValueTypeName type;
                            SdfVariability variability;
                            VtValue value;
                            bool removeOriginal;
                        };
                        QList<Plan> plans;
                        for (const auto& item : state->items) {
                            if (layer->GetPropertyAtPath(item.newPath))
                                continue;
                            const auto source = stage->GetAttributeAtPath(item.oldPath);
                            if (!source)
                                continue;
                            VtValue value;
                            source.Get(&value);
                            plans.append({ item.oldPath, item.newPath, source.GetTypeName(), source.GetVariability(),
                                           value, item.removeOriginal });
                        }
                        SdfChangeBlock block;
                        for (const auto& plan : plans) {
                            if (plan.removeOriginal && layer->GetPropertyAtPath(plan.oldPath)) {
                                if (!SdfCopySpec(layer, plan.oldPath, layer, plan.newPath)) {
                                    errors.append(QString("Cannot copy %1").arg(pathText(plan.oldPath)));
                                    continue;
                                }
                                layer->SetField(plan.newPath, SdfFieldKeys->Custom, VtValue(true));
                                if (!removePropertySpec(layer, plan.oldPath)) {
                                    errors.append(QString("Cannot remove %1").arg(pathText(plan.oldPath)));
                                    continue;
                                }
                            }
                            else {
                                const auto target = layer::ensureAttributeSpec(layer, plan.newPath, plan.type,
                                                                               plan.variability, true);
                                if (!target || (!plan.value.IsEmpty() && !target->SetDefaultValue(plan.value))) {
                                    errors.append(QString("Cannot rename %1").arg(pathText(plan.oldPath)));
                                    continue;
                                }
                            }
                            affectedCollector.append(plan.oldPath.GetPrimPath());
                        }''' +s[end:]
p.write_text(s,encoding='utf-8')
