// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#include "contextmenu.h"
#include "command.h"
#include "qtutils.h"
#include "tracelocks.h"
#include "usdutils.h"
#include "viewcontext.h"
#include <QApplication>
#include <QClipboard>
#include <QFileInfo>
#include <QMenu>
#include <QSet>
#include <algorithm>
#include <functional>
#include <pxr/base/vt/value.h>
#include <pxr/usd/sdf/listOp.h>
#include <pxr/usd/sdf/primSpec.h>
#include <pxr/usd/sdf/reference.h>
#include <pxr/usd/usd/prim.h>
#include <pxr/usd/usd/primRange.h>
#include <pxr/usd/usd/variantSets.h>
#include <pxr/usd/usdGeom/imageable.h>
#include <pxr/usd/usdGeom/xformable.h>

namespace stageviz {

namespace {
    bool hasResettableOverrides(const UsdPrim& prim, const SdfLayerHandle& editLayer)
    {
        if (!prim || prim.IsInstanceProxy() || !editLayer || !editLayer->PermissionToEdit())
            return false;

        const SdfPrimSpecHandle spec = editLayer->GetPrimAtPath(prim.GetPath());
        if (!spec || spec->GetSpecifier() != SdfSpecifierOver || spec->GetProperties().empty())
            return false;

        for (const SdfPrimSpecHandle& primSpec : prim.GetPrimStack()) {
            if (primSpec && primSpec->GetLayer() && primSpec->GetLayer() != editLayer)
                return true;
        }
        return false;
    }

    bool maskContainsSelection(const QList<SdfPath>& maskPaths, const QList<SdfPath>& selectedPaths)
    {
        if (maskPaths.isEmpty() || selectedPaths.isEmpty())
            return false;

        for (const SdfPath& selectedPath : selectedPaths) {
            if (!maskPaths.contains(selectedPath))
                return false;
        }
        return true;
    }

    SdfPath internalReferencePrimPath(UsdStageRefPtr stage, const UsdPrim& prim)
    {
        if (!stage || !prim)
            return {};

        for (const SdfPrimSpecHandle& spec : prim.GetPrimStack()) {
            if (!spec || !spec->HasInfo(SdfFieldKeys->References))
                continue;

            const VtValue value = spec->GetInfo(SdfFieldKeys->References);
            if (!value.IsHolding<SdfReferenceListOp>())
                continue;

            const SdfReferenceListOp references = value.UncheckedGet<SdfReferenceListOp>();
            for (const SdfReference& ref : references.GetAppliedItems()) {
                if (ref.GetAssetPath().empty() && !ref.GetPrimPath().IsEmpty()
                    && stage->GetPrimAtPath(ref.GetPrimPath()))
                    return ref.GetPrimPath();
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
    bool canResetPivot = !paths.isEmpty();
    bool canResetOverrides = false;
    SdfPath instanceRootPath;
    SdfPath instanceSourcePath;

    {
        READ_LOCKER(locker, context->stageLock(), "stageLock");
        if (!usdStage)
            return;

        if (canSetDefaultPrim) {
            const UsdPrim defaultPrim = usdStage->GetDefaultPrim();
            isDefaultPrim = defaultPrim && defaultPrim.GetPath() == paths.first();
        }

        payloadPaths = stage::resolvePayloadPaths(usdStage, topLevelPaths);
        if (!paths.isEmpty())
            variantTargets = stage::variantTargets(usdStage, paths, true);

        for (const SdfPath& path : paths) {
            if (stage::isPayload(usdStage, path))
                hasExactPayloadSelection = true;

            const UsdPrim prim = usdStage->GetPrimAtPath(path);
            if (paths.size() == 1 && prim) {
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
            if (!prim || !prim.IsValid() || prim.IsInstanceProxy() || !UsdGeomXformable(prim)) {
                canTransform = false;
                canCenterPivot = false;
                canResetPivot = false;
            }
            else {
                bool hasPivot = false;
                bool resetsXformStack = false;
                const std::vector<UsdGeomXformOp> ops = UsdGeomXformable(prim).GetOrderedXformOps(&resetsXformStack);
                for (const UsdGeomXformOp& op : ops) {
                    if (!op.IsInverseOp() && op.GetOpName() == TfToken("xformOp:translate:pivot")) {
                        hasPivot = true;
                        break;
                    }
                }
                if (!hasPivot)
                    canResetPivot = false;
            }

            if (!canResetOverrides && prim && prim.IsValid() && !prim.IsInstanceProxy()) {
                const SdfLayerHandle editLayer = usdStage->GetEditTarget().GetLayer();
                canResetOverrides = hasResettableOverrides(prim, editLayer);
                if (!canResetOverrides && editLayer && editLayer->PermissionToEdit()) {
                    for (const UsdPrim& descendant : prim.GetAllDescendants()) {
                        if (hasResettableOverrides(descendant, editLayer)) {
                            canResetOverrides = true;
                            break;
                        }
                    }
                }
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

    QAction* setDefaultPrim = nullptr;
    if (canSetDefaultPrim) {
        setDefaultPrim = menu.addAction("Default prim");
        setDefaultPrim->setCheckable(true);
        setDefaultPrim->setChecked(isDefaultPrim);
    }

    menu.addSeparator();

    QMenu* variantMenu = menu.addMenu("Variant");
    if (variantTargets.isEmpty()) {
        variantMenu->setEnabled(false);
    }
    else {
        for (auto setIt = variantTargets.cbegin(); setIt != variantTargets.cend(); ++setIt) {
            const QString setName = setIt.key();
            QMenu* setMenu = variantMenu->addMenu(setName);

            QList<SdfPath> setTargetPaths;
            for (auto valueIt = setIt.value().cbegin(); valueIt != setIt.value().cend(); ++valueIt) {
                for (const SdfPath& targetPath : valueIt.value()) {
                    if (!setTargetPaths.contains(targetPath))
                        setTargetPaths.append(targetPath);
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

    menu.addSeparator();

    QAction* loadSelected = menu.addAction("Load");
    QAction* unloadSelected = menu.addAction("Unload");
    loadSelected->setEnabled(canLoadSelected);
    unloadSelected->setEnabled(canUnloadSelected);

    menu.addSeparator();

    QAction* selectInstanceRoot = nullptr;
    QAction* selectInstanceSource = nullptr;
    if (!instanceRootPath.IsEmpty() || !instanceSourcePath.IsEmpty()) {
        QMenu* instanceMenu = menu.addMenu("Instance");
        if (!instanceRootPath.IsEmpty() && instanceRootPath != paths.value(0))
            selectInstanceRoot = instanceMenu->addAction("Select");
        if (selectInstanceRoot)
            instanceMenu->addSeparator();
        selectInstanceSource = instanceMenu->addAction("Source");
        selectInstanceSource->setEnabled(!instanceSourcePath.IsEmpty());
    }

    menu.addSeparator();

    QMenu* showMenu = menu.addMenu("Show");
    QAction* showSelected = showMenu->addAction("Selected");
    QAction* showRecursive = showMenu->addAction("Recursive");
    showSelected->setEnabled(canShowSelected);
    showRecursive->setEnabled(canShowRecursive);

    QMenu* hideMenu = menu.addMenu("Hide");
    QAction* hideSelected = hideMenu->addAction("Selected");
    QAction* hideRecursive = hideMenu->addAction("Recursive");
    hideSelected->setEnabled(canHideSelected);
    hideRecursive->setEnabled(canHideRecursive);

    menu.addSeparator();

    QAction* isolateAction = menu.addAction("Isolate");
    isolateAction->setCheckable(true);
    isolateAction->setChecked(isolateChecked);
    isolateAction->setEnabled(!paths.isEmpty());

    menu.addSeparator();

    QMenu* copyMenu = menu.addMenu("Copy");
    QAction* copyName = copyMenu->addAction("Name");

    copyMenu->addSeparator();

    QAction* copyPath = copyMenu->addAction("Path");
    QAction* copyPayload = nullptr;
    QAction* copyPathsAction = nullptr;
    QAction* copyNames = nullptr;

    copyName->setEnabled(!paths.isEmpty());
    copyPath->setEnabled(!paths.isEmpty());

    if (hasExactPayloadSelection)
        copyPayload = copyMenu->addAction("Payload");

    if (paths.size() > 1) {
        copyNames = copyMenu->addAction("Names");
        copyPathsAction = copyMenu->addAction("Paths");
    }

    if (paths.isEmpty())
        copyMenu->setEnabled(false);

    menu.addSeparator();

    QMenu* pivotMenu = menu.addMenu("Pivot");
    QAction* centerPivot = pivotMenu->addAction("Center");
    QAction* resetPivot = pivotMenu->addAction("Reset");
    centerPivot->setEnabled(canCenterPivot);
    resetPivot->setEnabled(canResetPivot);
    pivotMenu->setEnabled(canCenterPivot || canResetPivot);

    QMenu* transformMenu = menu.addMenu("Transform");
    QAction* identityTransform = transformMenu->addAction("Identity");
    identityTransform->setEnabled(canTransform);
    transformMenu->setEnabled(canTransform);

    menu.addSeparator();

    QMenu* resetMenu = menu.addMenu("Reset");
    QAction* resetOverridesAction = resetMenu->addAction("Overrides");
    resetOverridesAction->setEnabled(canResetOverrides);
    resetMenu->setEnabled(canResetOverrides);

    menu.addSeparator();

    QAction* newXform = menu.addAction("New xform");
    QMenu* duplicateMenu = menu.addMenu("Duplicate");
    QAction* copySelected = duplicateMenu->addAction("Copy");
    QAction* instanceSelected = duplicateMenu->addAction("Instance");
    QAction* deleteSelected = menu.addAction("Delete");
    newXform->setEnabled(!createParentPath.IsEmpty());
    duplicateMenu->setEnabled(!paths.isEmpty());
    deleteSelected->setEnabled(!paths.isEmpty());

    QAction* chosen = menu.exec(globalPos);
    if (!chosen)
        return;

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
        if (!instanceRootPath.IsEmpty())
            context->execute(new Command(selectPaths(QList<SdfPath> { instanceRootPath })));
        return;
    }

    if (chosen == selectInstanceSource) {
        if (!instanceSourcePath.IsEmpty())
            context->execute(new Command(selectPaths(QList<SdfPath> { instanceSourcePath })));
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

    if (chosen == centerPivot) {
        context->execute(new Command(centerPivots(paths)));
        return;
    }

    if (chosen == resetPivot) {
        context->execute(new Command(resetPivots(paths)));
        return;
    }

    if (chosen == identityTransform) {
        context->execute(new Command(identityTransforms(paths)));
        return;
    }

    if (chosen == resetOverridesAction) {
        context->execute(new Command(resetOverrides(paths)));
        return;
    }

    if (chosen == newXform) {
        context->execute(new Command(newXformPath(createParentPath, "Xform")));
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
