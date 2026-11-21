// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#include "contextmenu.h"
#include "bboxutils.h"
#include "commandcomposition.h"
#include "commandmaterial.h"
#include "commandpayload.h"
#include "commandprim.h"
#include "commandproperty.h"
#include "commandselection.h"
#include "commandstage.h"
#include "commandtransform.h"
#include "commandutils.h"
#include "commandvisibility.h"
#include "qtutils.h"
#include "tracelocks.h"
#include "usdutils.h"
#include "viewcamera.h"
#include "viewcontext.h"
#include "viewstate.h"
#include <QApplication>
#include <QClipboard>
#include <QFileInfo>
#include <QFontMetrics>
#include <QMenu>
#include <QScreen>
#include <QSet>
#include <algorithm>
#include <functional>
#include <pxr/base/vt/value.h>
#include <pxr/usd/sdf/listOp.h>
#include <pxr/usd/sdf/primSpec.h>
#include <pxr/usd/sdf/reference.h>
#include <pxr/usd/sdf/relationshipSpec.h>
#include <pxr/usd/usd/prim.h>
#include <pxr/usd/usd/primRange.h>
#include <pxr/usd/usd/relationship.h>
#include <pxr/usd/usd/variantSets.h>
#include <pxr/usd/usdGeom/imageable.h>
#include <pxr/usd/usdGeom/xformable.h>
#include <pxr/usd/usdShade/material.h>
#include <pxr/usd/usdShade/materialBindingAPI.h>

namespace stageviz {

namespace {
    struct RelationshipInfo {
        QList<SdfPath> targets;
        QList<SdfPath> owners;
        QList<SdfPath> clearPaths;
        QStringList provenance;
        QStringList layers;
        QStringList instanceOwners;
    };

    void appendPath(QList<SdfPath>& paths, const SdfPath& path)
    {
        if (!path.IsEmpty() && !paths.contains(path))
            paths.append(path);
    }

    void describeRelationship(RelationshipInfo& info, const UsdRelationship& relationship, const SdfPath& selectedPath,
                              bool material)
    {
        if (!relationship)
            return;
        const auto owner = relationship.GetPrim().GetPath();
        appendPath(info.owners, owner);
        const bool ancestor = owner != selectedPath && selectedPath.HasPrefix(owner);
        const bool strong = material && ancestor
                            && UsdShadeMaterialBindingAPI::GetMaterialBindingStrength(relationship)
                                   == UsdShadeTokens->strongerThanDescendants;
        const auto description = QString("%1 %2").arg(strong     ? "Ancestor override:"
                                                      : ancestor ? "Inherited from"
                                                                 : "Assigned on",
                                                      qt::SdfPathToQString(owner));
        if (!info.provenance.contains(description))
            info.provenance.append(description);
        for (const auto& spec : relationship.GetPropertyStack()) {
            if (!spec->HasInfo(SdfFieldKeys->TargetPaths))
                continue;
            const QString layer = qt::StringToQString(spec->GetLayer()->GetDisplayName());
            if (!info.layers.contains(layer))
                info.layers.append(layer);
            break;
        }
    }

    QString menuText(QMenu* menu, const QString& text)
    {
        const int width = menu->screen() ? qMax(240, menu->screen()->availableGeometry().width() / 2) : 560;
        return menu->fontMetrics().elidedText(text, Qt::ElideMiddle, width);
    }

    void addHeader(QMenu* menu, const QString& text)
    {
        menu->setToolTipsVisible(true);
        QAction* action = menu->addAction(menuText(menu, text));
        action->setToolTip(text);
        action->setEnabled(false);
    }

    void addRelationshipHeaders(QMenu* menu, const RelationshipInfo& info)
    {
        if (info.provenance.size() == 1)
            addHeader(menu, info.provenance.first());
        else if (!info.provenance.isEmpty())
            addHeader(menu, QString("Multiple owners (%1)").arg(info.provenance.size()));
        if (!info.layers.isEmpty()) {
            const QString text = info.layers.size() == 1 ? QString("Layer: %1").arg(info.layers.first())
                                                         : QString("Layers: %1").arg(info.layers.join(", "));
            addHeader(menu, text);
        }
        if (!info.instanceOwners.isEmpty())
            addHeader(menu, info.instanceOwners.size() == 1
                                ? QString("Instance owner: %1").arg(info.instanceOwners.first())
                                : QString("Instance owners (%1)").arg(info.instanceOwners.size()));
        if (!info.provenance.isEmpty() || !info.layers.isEmpty() || !info.instanceOwners.isEmpty())
            menu->addSeparator();
    }

    bool maskContainsSelection(const QList<SdfPath>& maskPaths, const QList<SdfPath>& selectedPaths)
    {
        if (maskPaths.isEmpty() || selectedPaths.isEmpty())
            return false;

        const path::PathIndex mask(maskPaths);
        for (const SdfPath& selectedPath : selectedPaths) {
            if (!mask.contains(selectedPath))
                return false;
        }
        return true;
    }

    SdfPath internalReferencePrimPath(UsdStageRefPtr stage, const UsdPrim& prim)
    {
        if (!stage || !prim)
            return {};

        const auto layers = stage->GetLayerStack();
        for (const SdfPrimSpecHandle& spec : prim.GetPrimStack()) {
            // Asset-local reference paths are not selectable sources in this stage.
            if (!spec || std::find(layers.begin(), layers.end(), spec->GetLayer()) == layers.end()
                || spec->GetPath().StripAllVariantSelections() != prim.GetPath()
                || !spec->HasInfo(SdfFieldKeys->References))
                continue;

            const VtValue value = spec->GetInfo(SdfFieldKeys->References);
            if (!value.IsHolding<SdfReferenceListOp>())
                continue;

            const SdfReferenceListOp references = value.UncheckedGet<SdfReferenceListOp>();
            for (const SdfReference& ref : references.GetAppliedItems()) {
                if (!ref.GetAssetPath().empty() || ref.GetPrimPath().IsEmpty())
                    continue;
                const UsdPrim source = stage->GetPrimAtPath(ref.GetPrimPath());
                if (source && source.IsActive() && !source.IsPrototype() && !source.IsInPrototype()
                    && !source.IsInstanceProxy() && source.GetPath() != prim.GetPath())
                    return source.GetPath();
            }
        }
        return {};
    }

    QString payloadClipboardText(UsdStageRefPtr usdStage, const QList<SdfPath>& paths)
    {
        const QList<payload::AssetEntry> entries = payload::assetEntries(usdStage, paths);

        QStringList lines;
        for (const payload::AssetEntry& entry : entries) {
            const QString filename = QFileInfo(entry.assetPath).fileName();
            if (filename.isEmpty())
                continue;

            if (!entry.variantSet.isEmpty())
                lines.append(QString("%1=%2:%3").arg(entry.variantSet, entry.variantValue, filename));
            else
                lines.append(filename);
        }

        lines.removeDuplicates();
        return lines.join('\n');
    }

    void copyToClipboard(const QString& text)
    {
        if (QClipboard* clipboard = QGuiApplication::clipboard())
            clipboard->setText(text);
    }

}  // namespace

void
ContextMenu::exec(QWidget* parent, ViewContext* context, UsdStageRefPtr usdStage, const QPoint& globalPos,
                  const QList<SdfPath>& paths, const QList<SdfPath>& maskPaths, const SdfPath& createParentPath,
                  bool payloadEnabled)
{
    if (!parent || !context || !usdStage)
        return;

    const QList<SdfPath> topLevelPaths = path::topLevelPaths(paths);
    const QList<SdfPath> groupPaths = path::minimalRootPaths(paths);
    const SdfPath xformParentPath = !groupPaths.isEmpty() && groupPaths.first() != SdfPath::AbsoluteRootPath()
                                        ? groupPaths.first().GetParentPath()
                                        : createParentPath;
    const bool canSetDefaultPrim = paths.size() == 1 && !paths.first().IsEmpty()
                                   && paths.first() != SdfPath::AbsoluteRootPath()
                                   && paths.first().GetParentPath() == SdfPath::AbsoluteRootPath();
    bool isDefaultPrim = false;
    const bool isolateChecked = maskContainsSelection(maskPaths, paths);

    QList<SdfPath> payloadPaths;
    stage::VariantTargets variantTargets;
    bool hasExactPayloadSelection = false;
    bool canShowSelected = false;
    bool canShowRecursive = false;
    bool canHideSelected = false;
    bool canHideRecursive = false;
    bool canLoadSelected = false;
    bool canUnloadSelected = false;
    bool canTransform = !paths.isEmpty();
    bool canCenterPivot = !paths.isEmpty();
    bool canClearPivot = !paths.isEmpty();
    QList<OpinionGroup> opinionGroups;
    bool canInstance = !paths.isEmpty();
    QList<SdfPath> instanceRootPaths;
    QList<SdfPath> instanceSourcePaths;
    QSet<SdfPath> seenInstanceRoots;
    QSet<SdfPath> seenInstanceSources;
    bool allInstances = !paths.isEmpty();
    bool allInstanceSources = !paths.isEmpty();
    bool hasInstanceProxies = false;
    bool canMakeInstanceEditable = false;
    bool canEdit = false;
    bool canClearMaterial = false;
    QList<SdfPath> materialPaths;
    path::PathCollector materialCollector(materialPaths);
    RelationshipInfo materialInfo;
    QMap<QString, RelationshipInfo> relationships;

    {
        READ_LOCKER(locker, context->stageLock(), "stageLock");
        if (!usdStage)
            return;
        opinionGroups = clearOpinionGroups(usdStage, paths);
        const auto instanceEditLayer = usdStage->GetEditTarget().GetLayer();
        canEdit = instanceEditLayer && instanceEditLayer->PermissionToEdit();
        canMakeInstanceEditable = canEdit;

        if (canSetDefaultPrim) {
            const UsdPrim defaultPrim = usdStage->GetDefaultPrim();
            isDefaultPrim = defaultPrim && defaultPrim.GetPath() == paths.first();
        }

        payloadPaths = stage::resolvePayloadPaths(usdStage, topLevelPaths);
        if (!paths.isEmpty())
            variantTargets = stage::variantTargets(usdStage, paths, true);

        for (const SdfPath& selectedPath : path::minimalRootPaths(paths)) {
            const SdfPath primPath = selectedPath.IsPropertyPath() ? selectedPath.GetPrimPath() : selectedPath;
            if (!stage::canUseAsNativeInstanceRoot(usdStage->GetPrimAtPath(primPath))) {
                canInstance = false;
                break;
            }
        }

        UsdShadeMaterialBindingAPI::BindingsCache materialBindings;
        UsdShadeMaterialBindingAPI::CollectionQueryCache materialCollections;
        for (const SdfPath& path : paths) {
            if (stage::isPayload(usdStage, path))
                hasExactPayloadSelection = true;

            const UsdPrim prim = usdStage->GetPrimAtPath(path);
            if (prim) {
                UsdRelationship binding;
                const auto material
                    = UsdShadeMaterialBindingAPI(prim).ComputeBoundMaterial(&materialBindings, &materialCollections,
                                                                            UsdShadeTokens->allPurpose, &binding);
                describeRelationship(materialInfo, binding, path, true);
                if (material)
                    materialCollector.append(material.GetPath());
                const auto owner = command::materialAssignmentOwner(usdStage, path);
                if (prim.IsInstanceProxy() && !owner.IsEmpty()) {
                    const auto text = qt::SdfPathToQString(owner);
                    if (!materialInfo.instanceOwners.contains(text))
                        materialInfo.instanceOwners.append(text);
                }
                for (const auto& relationship : prim.GetRelationships()) {
                    const auto& name = relationship.GetName().GetString();
                    if (name == "material:binding" || name.rfind("material:binding:", 0) == 0
                        || relationship.GetPropertyStack().empty())
                        continue;
                    auto& info = relationships[qt::StringToQString(name)];
                    describeRelationship(info, relationship, path, false);
                    SdfPathVector targets;
                    relationship.GetTargets(&targets);
                    for (const auto& target : targets)
                        if (usdStage->GetPrimAtPath(target.GetPrimPath()))
                            appendPath(info.targets, target);
                    if (!prim.IsInstanceProxy() && instanceEditLayer && instanceEditLayer->PermissionToEdit()) {
                        const auto specPath = usdStage->GetEditTarget().MapToSpecPath(relationship.GetPath());
                        if (!specPath.IsEmpty() && instanceEditLayer->GetRelationshipAtPath(specPath))
                            appendPath(info.clearPaths, relationship.GetPath());
                    }
                }

                const auto spec = !owner.IsEmpty() && instanceEditLayer && instanceEditLayer->PermissionToEdit()
                                      ? instanceEditLayer->GetPrimAtPath(usdStage->GetEditTarget().MapToSpecPath(owner))
                                      : SdfPrimSpecHandle();
                if (spec) {
                    for (const auto& property : spec->GetProperties()) {
                        const auto name = property->GetName();
                        if (instanceEditLayer->GetRelationshipAtPath(property->GetPath())
                            && (name == "material:binding" || name.rfind("material:binding:", 0) == 0))
                            canClearMaterial = true;
                    }
                }
            }
            SdfPath instanceRootPath;
            SdfPath instanceSourcePath;
            if (prim) {
                if (prim.IsInstanceProxy()) {
                    SdfPath ancestor = path.GetParentPath();
                    while (!ancestor.IsEmpty() && ancestor != SdfPath::AbsoluteRootPath()) {
                        const UsdPrim candidate = usdStage->GetPrimAtPath(ancestor);
                        if (candidate && candidate.IsInstance()) {
                            instanceRootPath = ancestor;
                            const SdfPath sourceRootPath = internalReferencePrimPath(usdStage, candidate);
                            if (!sourceRootPath.IsEmpty()) {
                                const SdfPath relativePath = path.MakeRelativePath(instanceRootPath);
                                const SdfPath sourcePath = relativePath.IsEmpty()
                                                               ? sourceRootPath
                                                               : sourceRootPath.AppendPath(relativePath);
                                if (usdStage->GetPrimAtPath(sourcePath))
                                    instanceSourcePath = sourcePath;
                            }
                            break;
                        }
                        ancestor = ancestor.GetParentPath();
                    }
                }
                else if (prim.IsInstance()) {
                    instanceRootPath = path;
                    instanceSourcePath = internalReferencePrimPath(usdStage, prim);
                }
            }
            if (instanceRootPath.IsEmpty()) {
                allInstances = false;
            }
            else if (!seenInstanceRoots.contains(instanceRootPath)) {
                seenInstanceRoots.insert(instanceRootPath);
                instanceRootPaths.append(instanceRootPath);
            }
            hasInstanceProxies = hasInstanceProxies || (prim && prim.IsInstanceProxy());
            if (instanceSourcePath.IsEmpty()) {
                allInstanceSources = false;
            }
            else if (!seenInstanceSources.contains(instanceSourcePath)) {
                seenInstanceSources.insert(instanceSourcePath);
                instanceSourcePaths.append(instanceSourcePath);
            }
            if (!prim || !prim.IsValid() || prim.IsInstanceProxy() || !UsdGeomXformable(prim)) {
                canTransform = false;
                canCenterPivot = false;
                canClearPivot = false;
            }
            else {
                bool hasPivot = false;
                bool resetsXformStack = false;
                const std::vector<UsdGeomXformOp> ops = UsdGeomXformable(prim).GetOrderedXformOps(&resetsXformStack);
                for (const UsdGeomXformOp& op : ops) {
                    if (!op.IsInverseOp()
                        && (op.GetOpName() == TfToken("xformOp:translate:pivot")
                            || op.GetOpName() == TfToken("xformOp:transform:pivot"))) {
                        hasPivot = true;
                        break;
                    }
                }
                if (!hasPivot)
                    canClearPivot = false;
            }

            if (prim && prim.IsInstanceProxy())
                continue;  // Instance proxies cannot author visibility opinions.

            const bool visible = stage::isVisible(usdStage, path);
            if (visible)
                canHideSelected = true;
            else
                canShowSelected = true;

            if (prim && prim.IsValid()) {
                if (visible)
                    canHideRecursive = true;
                else
                    canShowRecursive = true;

                for (const UsdPrim& descendant : prim.GetAllDescendants()) {
                    if (!descendant || !descendant.IsValid())
                        continue;

                    if (stage::isVisible(usdStage, descendant.GetPath()))
                        canHideRecursive = true;
                    else
                        canShowRecursive = true;

                    if (canShowRecursive && canHideRecursive)
                        break;
                }
            }
        }

        for (const SdfPath& path : payloadPaths) {
            const UsdPrim prim = usdStage->GetPrimAtPath(path);
            if (!prim || !prim.IsValid() || !prim.HasPayload())
                continue;

            if (prim.IsLoaded())
                canUnloadSelected = true;
            else
                canLoadSelected = true;

            if (canLoadSelected && canUnloadSelected)
                break;
        }
    }

    QStringList pathStrings;
    QStringList nameStrings;
    for (const SdfPath& path : paths) {
        pathStrings.append(qt::SdfPathToQString(path));
        nameStrings.append(qt::StringToQString(path.GetName()));
    }

    QMenu menu(parent);
    menu.setObjectName("primContextMenu");
    menu.setToolTipsVisible(true);
    QAction* frameAction = menu.addAction("Frame");
    frameAction->setObjectName("primFrame");
    frameAction->setEnabled(!paths.isEmpty() && context->viewState() && context->viewState()->camera());
    QAction* isolateAction = menu.addAction("Isolate");
    isolateAction->setCheckable(true);
    isolateAction->setChecked(isolateChecked);
    isolateAction->setEnabled(!paths.isEmpty());
    menu.addSeparator();

    QMenu* visibilityMenu = menu.addMenu("Visibility");
    visibilityMenu->setObjectName("primVisibilityMenu");
    addHeader(visibilityMenu, "Show");
    QAction* showSelected = visibilityMenu->addAction("Selected");
    showSelected->setObjectName("visibilityShowSelected");
    QAction* showRecursive = visibilityMenu->addAction("Hierarchy");
    showRecursive->setObjectName("visibilityShowHierarchy");
    showSelected->setEnabled(canEdit && canShowSelected);
    showRecursive->setEnabled(canEdit && canShowRecursive);
    visibilityMenu->addSeparator();
    addHeader(visibilityMenu, "Hide");
    QAction* hideSelected = visibilityMenu->addAction("Selected");
    hideSelected->setObjectName("visibilityHideSelected");
    QAction* hideRecursive = visibilityMenu->addAction("Hierarchy");
    hideRecursive->setObjectName("visibilityHideHierarchy");
    hideSelected->setEnabled(canEdit && canHideSelected);
    hideRecursive->setEnabled(canEdit && canHideRecursive);
    visibilityMenu->setEnabled(canEdit && (canShowSelected || canShowRecursive || canHideSelected || canHideRecursive));
    menu.addSeparator();

    QMenu* transformMenu = menu.addMenu("Transform");
    transformMenu->setObjectName("primTransformMenu");
    QAction* identityTransform = transformMenu->addAction("Identity");
    identityTransform->setEnabled(canEdit && canTransform);
    QMenu* pivotMenu = transformMenu->addMenu("Pivot");
    QAction* centerPivot = pivotMenu->addAction("Center");
    pivotMenu->addSeparator();
    QAction* clearPivot = pivotMenu->addAction("Clear");
    centerPivot->setEnabled(canEdit && canCenterPivot);
    clearPivot->setEnabled(canEdit && canClearPivot);
    pivotMenu->setEnabled(canEdit && (canCenterPivot || canClearPivot));
    transformMenu->setEnabled(canEdit && (canTransform || canCenterPivot || canClearPivot));
    menu.addSeparator();

    QMenu* relationshipsMenu = menu.addMenu("Relationships");
    relationshipsMenu->setObjectName("primRelationshipsMenu");
    QMap<QAction*, QList<SdfPath>> relationshipSelections;
    QMap<QAction*, QList<SdfPath>> relationshipClears;
    QMenu* materialMenu = relationshipsMenu->addMenu("Material");
    materialMenu->setObjectName("materialBindingMenu");
    addRelationshipHeaders(materialMenu, materialInfo);
    QAction* materialSource = materialMenu->addAction("Source");
    materialSource->setObjectName("materialBindingSource");
    materialSource->setEnabled(!materialPaths.isEmpty());
    QAction* materialOwner = materialMenu->addAction("Owner");
    materialOwner->setObjectName("materialBindingOwner");
    materialOwner->setEnabled(!materialInfo.owners.isEmpty());
    relationshipSelections.insert(materialOwner, materialInfo.owners);
    materialMenu->addSeparator();
    QAction* clearMaterialAction = materialMenu->addAction("Clear");
    clearMaterialAction->setObjectName("materialBindingClear");
    clearMaterialAction->setEnabled(canClearMaterial);
    materialMenu->setEnabled(!materialPaths.isEmpty() || canClearMaterial || !materialInfo.owners.isEmpty());
    bool anyRelationships = materialMenu->isEnabled();
    for (auto it = relationships.cbegin(); it != relationships.cend(); ++it) {
        const auto& info = it.value();
        QMenu* relationshipMenu = relationshipsMenu->addMenu(it.key());
        relationshipMenu->setObjectName("relationship_" + it.key());
        addRelationshipHeaders(relationshipMenu, info);
        QMenu* targetsMenu = relationshipMenu->addMenu("Targets");
        targetsMenu->setEnabled(!info.targets.isEmpty());
        for (const auto& target : info.targets) {
            QAction* action = targetsMenu->addAction(menuText(targetsMenu, qt::SdfPathToQString(target)));
            action->setToolTip(qt::SdfPathToQString(target));
            targetsMenu->setToolTipsVisible(true);
            relationshipSelections.insert(action, { target.GetPrimPath() });
        }
        QAction* owner = relationshipMenu->addAction("Owner");
        owner->setObjectName("relationshipOwner_" + it.key());
        owner->setEnabled(!info.owners.isEmpty());
        relationshipSelections.insert(owner, info.owners);
        relationshipMenu->addSeparator();
        QAction* clear = relationshipMenu->addAction("Clear");
        clear->setObjectName("relationshipClear_" + it.key());
        clear->setEnabled(!info.clearPaths.isEmpty());
        relationshipClears.insert(clear, info.clearPaths);
        anyRelationships = true;
    }
    relationshipsMenu->setEnabled(anyRelationships);
    menu.addSeparator();

    bool onlyMaterials = !paths.isEmpty();
    bool canReplace = false;
    QString replaceError;
    {
        READ_LOCKER(locker, context->stageLock(), "stageLock");
        for (const auto& path : paths) {
            const auto prim = usdStage->GetPrimAtPath(path);
            onlyMaterials = onlyMaterials && prim && prim.IsA<UsdShadeMaterial>();
        }
        if (onlyMaterials)
            canReplace = canReplaceMaterialNetwork(usdStage, paths, &replaceError);
    }
    QMap<QAction*, MaterialType> replaceActions;
    if (onlyMaterials) {
        QMenu* replace = menu.addMenu("Replace shader network");
        replace->setObjectName("replaceMaterialNetworkMenu");
        replace->setEnabled(canReplace);
        replace->menuAction()->setToolTip(replaceError);
        for (const auto& pair : { qMakePair(QString("USD Preview Surface"), MaterialType::PreviewSurface),
                                  qMakePair(QString("MaterialX Standard Surface"), MaterialType::StandardSurface),
                                  qMakePair(QString("MaterialX OpenPBR Surface"), MaterialType::OpenPBRSurface) }) {
            auto* action = replace->addAction(pair.first);
            action->setObjectName(QStringLiteral("replaceMaterialNetwork_%1").arg(static_cast<int>(pair.second)));
            replaceActions.insert(action, pair.second);
        }
    }

    QMenu* opinionsMenu = menu.addMenu("Opinions");
    opinionsMenu->setObjectName("clearOpinionsMenu");
    addHeader(opinionsMenu, "Clear");
    QAction* clearOpinionsAction = opinionsMenu->addAction("All");
    clearOpinionsAction->setObjectName("clearOpinionsAll");
    clearOpinionsAction->setEnabled(!opinionGroups.isEmpty());
    opinionsMenu->setEnabled(!opinionGroups.isEmpty());
    QMap<QAction*, OpinionGroup> opinionActions;
    if (!opinionGroups.isEmpty())
        opinionsMenu->addSeparator();
    const QStringList opinionLabels = { "All",      "Transforms", "Visibility", "Materials", "Geometry",
                                        "Primvars", "Lights",     "Cameras",    "Other" };
    for (const OpinionGroup group : opinionGroups) {
        if (group == OpinionGroup::Other && !opinionsMenu->actions().last()->isSeparator())
            opinionsMenu->addSeparator();
        QAction* action = opinionsMenu->addAction(opinionLabels.at(static_cast<int>(group)));
        action->setObjectName(QStringLiteral("clearOpinions%1").arg(static_cast<int>(group)));
        opinionActions.insert(action, group);
    }
    menu.addSeparator();

    QMenu* compositionMenu = menu.addMenu("Composition");
    compositionMenu->setObjectName("primCompositionMenu");
    QAction* setDefaultPrim = compositionMenu->addAction("Default prim");
    setDefaultPrim->setEnabled(canEdit && canSetDefaultPrim);
    setDefaultPrim->setCheckable(true);
    setDefaultPrim->setChecked(isDefaultPrim);
    QMenu* variantMenu = compositionMenu->addMenu("Variants");
    if (variantTargets.isEmpty() || !canEdit) {
        variantMenu->setEnabled(false);
    }
    else {
        for (auto setIt = variantTargets.cbegin(); setIt != variantTargets.cend(); ++setIt) {
            const QString setName = setIt.key();
            QMenu* setMenu = variantMenu->addMenu(setName);

            QList<SdfPath> setTargetPaths;
            path::PathCollector targetCollector(setTargetPaths);
            for (auto valueIt = setIt.value().cbegin(); valueIt != setIt.value().cend(); ++valueIt) {
                for (const SdfPath& targetPath : valueIt.value()) {
                    targetCollector.append(targetPath);
                }
            }

            QString commonSelection;
            bool hasCommonSelection = !setTargetPaths.isEmpty();

            for (const SdfPath& targetPath : setTargetPaths) {
                const UsdPrim prim = usdStage->GetPrimAtPath(targetPath);
                if (!prim) {
                    hasCommonSelection = false;
                    break;
                }

                const UsdVariantSet variantSet = prim.GetVariantSet(qt::QStringToString(setName));
                if (!variantSet.IsValid()) {
                    hasCommonSelection = false;
                    break;
                }

                const QString selection = qt::StringToQString(variantSet.GetVariantSelection());
                if (selection.isEmpty()) {
                    hasCommonSelection = false;
                    break;
                }

                if (commonSelection.isEmpty()) {
                    commonSelection = selection;
                }
                else if (commonSelection != selection) {
                    hasCommonSelection = false;
                    break;
                }
            }

            for (auto valueIt = setIt.value().cbegin(); valueIt != setIt.value().cend(); ++valueIt) {
                const QString value = valueIt.key();
                const QList<SdfPath> targetPaths = valueIt.value();

                QAction* action = setMenu->addAction(value);
                action->setCheckable(true);
                action->setChecked(hasCommonSelection && commonSelection == value);

                QObject::connect(action, &QAction::triggered, parent, [context, setName, value, targetPaths]() {
                    if (!targetPaths.isEmpty())
                        context->execute(new Command(setVariantSelection(targetPaths, setName, value)));
                });
            }
        }
    }


    compositionMenu->addSeparator();
    QAction* loadSelected = compositionMenu->addAction("Load");
    QAction* unloadSelected = compositionMenu->addAction("Unload");
    loadSelected->setEnabled(canLoadSelected);
    unloadSelected->setEnabled(canUnloadSelected);
    compositionMenu->addSeparator();
    QMenu* instanceMenu = compositionMenu->addMenu("Instance");
    instanceMenu->setObjectName("primInstanceMenu");
    instanceMenu->setEnabled(allInstances);
    QAction* selectInstanceRoot = instanceMenu->addAction("Parent");
    selectInstanceRoot->setObjectName("instanceSelectRoots");
    selectInstanceRoot->setEnabled(allInstances && hasInstanceProxies);
    QAction* selectInstanceSource = instanceMenu->addAction("Source");
    selectInstanceSource->setObjectName("instanceSelectSources");
    selectInstanceSource->setEnabled(allInstances && allInstanceSources);
    instanceMenu->addSeparator();
    QAction* makeInstanceEditable = instanceMenu->addAction("Make editable");
    makeInstanceEditable->setObjectName("instanceMakeEditable");
    makeInstanceEditable->setEnabled(allInstances && canMakeInstanceEditable);
    menu.addSeparator();

    QMenu* copyMenu = menu.addMenu("Copy");
    QAction* copyName = copyMenu->addAction("Name");
    QAction* copyPath = copyMenu->addAction("Path");
    copyName->setEnabled(!paths.isEmpty());
    copyPath->setEnabled(!paths.isEmpty());
    QAction* copyNames = nullptr;
    QAction* copyPathsAction = nullptr;
    if (paths.size() > 1) {
        copyMenu->addSeparator();
        copyNames = copyMenu->addAction("Names");
        copyPathsAction = copyMenu->addAction("Paths");
    }
    QAction* copyPayload = nullptr;
    if (hasExactPayloadSelection) {
        copyMenu->addSeparator();
        copyPayload = copyMenu->addAction("Payload");
    }
    copyMenu->setEnabled(!paths.isEmpty());
    menu.addSeparator();

    QMenu* createMenu = menu.addMenu("Create");
    createMenu->setObjectName("createPrimMenu");
    createMenu->setEnabled(canEdit);
    QAction* newXform = createMenu->addAction("Xform");
    QAction* parentXform = createMenu->addAction("Parent Xform");
    newXform->setObjectName("createChildXform");
    parentXform->setObjectName("createParentXform");
    newXform->setEnabled(canEdit && !createParentPath.IsEmpty());
    parentXform->setEnabled(canEdit && !groupPaths.isEmpty() && !groupPaths.contains(SdfPath::AbsoluteRootPath())
                            && !xformParentPath.IsEmpty());
    QMenu* duplicateMenu = menu.addMenu("Duplicate");
    QAction* copySelected = duplicateMenu->addAction("Copy");
    QAction* instanceSelected = duplicateMenu->addAction("Instance");
    instanceSelected->setEnabled(canEdit && canInstance);
    duplicateMenu->setEnabled(canEdit && !paths.isEmpty());
    menu.addSeparator();
    QAction* deleteSelected = menu.addAction("Delete");
    deleteSelected->setEnabled(canEdit && !paths.isEmpty());

    QAction* chosen = menu.exec(globalPos);
    if (!chosen)
        return;

    if (replaceActions.contains(chosen)) {
        context->execute(new Command(replaceMaterialNetwork(paths, replaceActions.value(chosen))));
        return;
    }

    if (chosen == setDefaultPrim) {
        context->execute(
            new Command(setDefaultPrim->isChecked() ? defaultPrimPath(paths.first()) : clearDefaultPrim()));
        return;
    }

    if (chosen == copyName) {
        copyToClipboard(nameStrings.first());
        return;
    }

    if (chosen == copyPath) {
        copyToClipboard(pathStrings.first());
        return;
    }

    if (chosen == copyPayload) {
        QString text;
        {
            READ_LOCKER(locker, context->stageLock(), "stageLock");
            if (usdStage)
                text = payloadClipboardText(usdStage, paths);
        }
        if (!text.isEmpty())
            copyToClipboard(text);
        return;
    }

    if (chosen == copyNames) {
        copyToClipboard(nameStrings.join('\n'));
        return;
    }

    if (chosen == copyPathsAction) {
        copyToClipboard(pathStrings.join('\n'));
        return;
    }

    if (chosen == selectInstanceRoot) {
        if (!instanceRootPaths.isEmpty())
            context->execute(new Command(selectPaths(instanceRootPaths)));
        return;
    }

    if (chosen == selectInstanceSource) {
        if (!instanceSourcePaths.isEmpty())
            context->execute(new Command(selectPaths(instanceSourcePaths)));
        return;
    }

    if (chosen == makeInstanceEditable) {
        if (!instanceRootPaths.isEmpty())
            context->execute(new Command(makeEditablePaths(instanceRootPaths)));
        return;
    }

    if (chosen == loadSelected) {
        if (!payloadPaths.isEmpty())
            context->execute(new Command(loadPayloads(payloadPaths)));
        return;
    }

    if (chosen == unloadSelected) {
        if (!payloadPaths.isEmpty())
            context->execute(new Command(unloadPayloads(payloadPaths)));
        return;
    }

    if (chosen == frameAction) {
        GfBBox3d bounds;
        {
            READ_LOCKER(locker, context->stageLock(), "stageLock");
            bbox::CacheGuard cache;
            for (const auto& path : paths) {
                const auto prim = usdStage->GetPrimAtPath(path.GetPrimPath());
                if (prim)
                    bounds = GfBBox3d::Combine(bounds, cache.ComputeWorldBound(prim));
            }
        }
        if (!bounds.GetRange().IsEmpty() && context->viewState() && context->viewState()->camera())
            context->viewState()->camera()->frame(bounds);
        return;
    }
    if (relationshipSelections.contains(chosen)) {
        context->execute(new Command(selectPaths(relationshipSelections.value(chosen))));
        return;
    }
    if (relationshipClears.contains(chosen)) {
        context->execute(new Command(clearRelationships(relationshipClears.value(chosen))));
        return;
    }

    if (chosen == materialSource) {
        context->execute(new Command(selectPaths(materialPaths)));
        return;
    }

    if (chosen == clearMaterialAction) {
        context->execute(new Command(clearMaterial(paths)));
        return;
    }

    if (chosen == centerPivot) {
        context->execute(new Command(centerPivots(paths)));
        return;
    }

    if (chosen == clearPivot) {
        context->execute(new Command(clearPivots(paths)));
        return;
    }

    if (chosen == identityTransform) {
        context->execute(new Command(identityTransforms(paths)));
        return;
    }

    if (chosen == clearOpinionsAction) {
        context->execute(new Command(clearOpinions(paths)));
        return;
    }

    if (opinionActions.contains(chosen)) {
        context->execute(new Command(clearOpinions(paths, opinionActions.value(chosen))));
        return;
    }

    if (chosen == newXform) {
        context->execute(new Command(newPrimPath(createParentPath, "Xform", TfToken("Xform"))));
        return;
    }

    if (chosen == parentXform) {
        context->execute(new Command(newXformPath(xformParentPath, "Xform")));
        return;
    }

    if (chosen == copySelected) {
        context->execute(new Command(copyPaths(paths)));
        return;
    }

    if (chosen == instanceSelected) {
        context->execute(new Command(instancePaths(paths)));
        return;
    }

    if (chosen == showSelected)
        context->execute(new Command(showPaths(paths, false)));
    else if (chosen == showRecursive)
        context->execute(new Command(showPaths(paths, true)));
    else if (chosen == hideSelected)
        context->execute(new Command(hidePaths(paths, false)));
    else if (chosen == hideRecursive)
        context->execute(new Command(hidePaths(paths, true)));
    else if (chosen == isolateAction)
        context->execute(new Command(isolatePaths(isolateAction->isChecked() ? paths : QList<SdfPath>())));
    else if (chosen == deleteSelected)
        context->execute(new Command(deletePaths(paths)));
}

}  // namespace stageviz
