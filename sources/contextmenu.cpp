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
#include <functional>
#include <pxr/usd/sdf/primSpec.h>
#include <pxr/usd/usd/prim.h>
#include <pxr/usd/usd/variantSets.h>
#include <pxr/usd/usdGeom/bboxCache.h>
#include <pxr/usd/usdGeom/imageable.h>
#include <pxr/usd/usdGeom/xformable.h>

namespace stageviz {

namespace {
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
    payload::PayloadVariantTargets variantTargets;
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

    {
        READ_LOCKER(locker, context->stageLock(), "stageLock");
        if (!usdStage)
            return;

        if (canSetDefaultPrim) {
            const UsdPrim defaultPrim = usdStage->GetDefaultPrim();
            isDefaultPrim = defaultPrim && defaultPrim.GetPath() == paths.first();
        }

        payloadPaths = payloadEnabled ? stage::resolvePayloadPaths(usdStage, topLevelPaths)
                                      : stage::payloadPaths(usdStage, topLevelPaths);

        if (!paths.isEmpty())
            variantTargets = payload::payloadVariantTargets(usdStage, paths);

        for (const SdfPath& path : paths) {
            if (stage::isPayload(usdStage, path))
                hasExactPayloadSelection = true;

            const UsdPrim prim = usdStage->GetPrimAtPath(path);
            if (!prim || !prim.IsValid() || prim.IsInstanceProxy() || !UsdGeomXformable(prim)) {
                canTransform = false;
                canCenterPivot = false;
                canResetPivot = false;
            }
            else if (canCenterPivot) {
                UsdGeomBBoxCache bboxCache(UsdTimeCode::Default(), UsdGeomImageable::GetOrderedPurposeTokens(), true);
                const GfBBox3d bound = bboxCache.ComputeWorldBound(prim);
                if (bound.ComputeAlignedRange().IsEmpty())
                    canCenterPivot = false;
            }

            if (prim && prim.IsValid() && !prim.IsInstanceProxy()) {
                const SdfLayerHandle rootLayer = usdStage->GetRootLayer();
                const SdfPrimSpecHandle rootSpec = rootLayer ? rootLayer->GetPrimAtPath(prim.GetPath())
                                                             : SdfPrimSpecHandle();

                if (rootSpec) {
                    const bool hasDirectOpinions = !rootSpec->GetProperties().empty()
                                                   || !rootSpec->GetMetaDataInfoKeys().empty();

                    if (hasDirectOpinions) {
                        for (const SdfPrimSpecHandle& primSpec : prim.GetPrimStack()) {
                            if (!primSpec)
                                continue;

                            const SdfLayerHandle layer = primSpec->GetLayer();
                            if (layer && layer != rootLayer) {
                                canResetOverrides = true;
                                break;
                            }
                        }
                    }
                }
            }

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

        if (payloadEnabled) {
            for (const SdfPath& path : payloadPaths) {
                const UsdPrim prim = usdStage->GetPrimAtPath(path);
                if (!prim)
                    continue;

                if (prim.IsLoaded())
                    canUnloadSelected = true;
                else
                    canLoadSelected = true;
            }
        }
        else if (!payloadPaths.isEmpty()) {
            canLoadSelected = true;
            canUnloadSelected = true;
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

                QObject::connect(action, &QAction::triggered, parent,
                                 [context, usdStage, setName, value, targetPaths]() {
                                     QList<SdfPath> resolved;
                                     {
                                         READ_LOCKER(locker, context->stageLock(), "stageLock");
                                         if (usdStage)
                                             resolved = stage::payloadPaths(usdStage, targetPaths);
                                     }
                                     if (!resolved.isEmpty())
                                         context->run(new Command(loadPayloads(resolved, setName, value)));
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
    QAction* copyPath = copyMenu->addAction("Path");
    QAction* copyName = copyMenu->addAction("Name");
    QAction* copyPayload = nullptr;
    QAction* copyPaths = nullptr;
    QAction* copyNames = nullptr;

    copyPath->setEnabled(!paths.isEmpty());
    copyName->setEnabled(!paths.isEmpty());

    if (hasExactPayloadSelection)
        copyPayload = copyMenu->addAction("Payload");

    if (paths.size() > 1) {
        copyPaths = copyMenu->addAction("Paths");
        copyNames = copyMenu->addAction("Names");
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
    QAction* deleteSelected = menu.addAction("Delete");
    newXform->setEnabled(!createParentPath.IsEmpty());
    deleteSelected->setEnabled(!paths.isEmpty());

    QAction* chosen = menu.exec(globalPos);
    if (!chosen)
        return;

    if (chosen == setDefaultPrim) {
        context->run(new Command(defaultPrimPath(paths.first())));
        return;
    }

    if (chosen == copyPath) {
        copyToClipboard(pathStrings.first());
        return;
    }

    if (chosen == copyName) {
        copyToClipboard(nameStrings.first());
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

    if (chosen == copyPaths) {
        copyToClipboard(pathStrings.join('\n'));
        return;
    }

    if (chosen == copyNames) {
        copyToClipboard(nameStrings.join('\n'));
        return;
    }

    if (chosen == loadSelected) {
        if (!payloadPaths.isEmpty())
            context->run(new Command(loadPayloads(payloadPaths)));
        return;
    }

    if (chosen == unloadSelected) {
        if (!payloadPaths.isEmpty())
            context->run(new Command(unloadPayloads(payloadPaths)));
        return;
    }

    if (chosen == centerPivot) {
        context->run(new Command(centerPivots(paths)));
        return;
    }

    if (chosen == resetPivot) {
        context->run(new Command(resetPivots(paths)));
        return;
    }

    if (chosen == identityTransform) {
        context->run(new Command(identityTransforms(paths)));
        return;
    }

    if (chosen == resetOverridesAction) {
        context->run(new Command(resetOverrides(paths)));
        return;
    }

    if (chosen == newXform) {
        context->run(new Command(newXformPath(createParentPath, "Xform")));
        return;
    }

    if (chosen == showSelected)
        context->run(new Command(showPaths(paths, false)));
    else if (chosen == showRecursive)
        context->run(new Command(showPaths(paths, true)));
    else if (chosen == hideSelected)
        context->run(new Command(hidePaths(paths, false)));
    else if (chosen == hideRecursive)
        context->run(new Command(hidePaths(paths, true)));
    else if (chosen == isolateAction)
        context->run(new Command(isolatePaths(isolateAction->isChecked() ? paths : QList<SdfPath>())));
    else if (chosen == deleteSelected)
        context->run(new Command(deletePaths(paths)));
}

}  // namespace stageviz
