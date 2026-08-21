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
#include <pxr/usd/usd/prim.h>
#include <pxr/usd/usd/primRange.h>

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

    QList<SdfPath> recursivePaths(UsdStageRefPtr usdStage, const QList<SdfPath>& paths)
    {
        QList<SdfPath> result;
        if (!usdStage)
            return result;

        for (const SdfPath& path : path::topLevelPaths(paths)) {
            const UsdPrim root = usdStage->GetPrimAtPath(path);
            if (!root)
                continue;

            for (const UsdPrim& prim : UsdPrimRange(root)) {
                if (prim && !result.contains(prim.GetPath()))
                    result.append(prim.GetPath());
            }
        }
        return result;
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
    const bool isolateChecked = maskContainsSelection(maskPaths, paths);

    QList<SdfPath> payloadPaths;
    payload::PayloadVariantTargets variantTargets;
    bool hasExactPayloadSelection = false;
    bool canShowSelected = false;
    bool canHideSelected = false;
    bool canLoadSelected = false;
    bool canUnloadSelected = false;

    {
        READ_LOCKER(locker, context->stageLock(), "stageLock");
        if (!usdStage)
            return;

        payloadPaths = payloadEnabled ? stage::resolvePayloadPaths(usdStage, topLevelPaths)
                                      : stage::payloadPaths(usdStage, topLevelPaths);

        if (!paths.isEmpty())
            variantTargets = payload::payloadVariantTargets(usdStage, paths);

        for (const SdfPath& path : paths) {
            if (stage::isPayload(usdStage, path))
                hasExactPayloadSelection = true;

            const bool visible = stage::isVisible(usdStage, path);
            if (visible)
                canHideSelected = true;
            else
                canShowSelected = true;
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

    QMenu* selectMenu = menu.addMenu("Select");
    QAction* selectRecursive = selectMenu->addAction("Recursive");
    selectRecursive->setEnabled(!paths.isEmpty());

    menu.addSeparator();

    QAction* setDefaultPrim = nullptr;
    if (canSetDefaultPrim)
        setDefaultPrim = menu.addAction("Default prim");

    menu.addSeparator();

    QMenu* variantMenu = menu.addMenu("Variant");
    if (variantTargets.isEmpty()) {
        variantMenu->setEnabled(false);
    }
    else {
        for (auto setIt = variantTargets.cbegin(); setIt != variantTargets.cend(); ++setIt) {
            const QString setName = setIt.key();
            QMenu* setMenu = variantMenu->addMenu(setName);

            for (auto valueIt = setIt.value().cbegin(); valueIt != setIt.value().cend(); ++valueIt) {
                const QString value = valueIt.key();
                const QList<SdfPath> targetPaths = valueIt.value();

                QAction* action = setMenu->addAction(value);
                QObject::connect(action, &QAction::triggered, parent, [context, usdStage, setName, value, targetPaths]() {
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
    showRecursive->setEnabled(canShowSelected);

    QMenu* hideMenu = menu.addMenu("Hide");
    QAction* hideSelected = hideMenu->addAction("Selected");
    QAction* hideRecursive = hideMenu->addAction("Recursive");
    hideSelected->setEnabled(canHideSelected);
    hideRecursive->setEnabled(canHideSelected);

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

    QAction* newXform = menu.addAction("New xform");
    QAction* deleteSelected = menu.addAction("Delete");
    newXform->setEnabled(!createParentPath.IsEmpty());
    deleteSelected->setEnabled(!paths.isEmpty());

    QAction* chosen = menu.exec(globalPos);
    if (!chosen)
        return;

    if (chosen == selectRecursive) {
        QList<SdfPath> selection;
        {
            READ_LOCKER(locker, context->stageLock(), "stageLock");
            if (usdStage)
                selection = recursivePaths(usdStage, paths);
        }
        if (!selection.isEmpty())
            context->run(new Command(selectPaths(selection)));
        return;
    }

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
