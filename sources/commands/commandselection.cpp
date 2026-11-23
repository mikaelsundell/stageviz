// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#include "commandselection.h"
#include "commandutils.h"
#include "perfscope.h"
#include "selectionlist.h"
#include "tracelocks.h"
#include "usdutils.h"
#include <memory>
#include <pxr/usd/sdf/layer.h>
#include <pxr/usd/usd/prim.h>
#include <pxr/usd/usd/primRange.h>
#include <pxr/usd/usdGeom/gprim.h>
#include <pxr/usd/usdGeom/pointInstancer.h>

namespace stageviz {

namespace {
    using command::currentEditLayer;
}  // namespace

Command
selectPayload()
{
    struct SelectPayloadState {
        QList<SdfPath> previousSelection;
    };

    auto state = std::make_shared<SelectPayloadState>();

    return Command(
        [state](Session* session) {
            if (!session)
                return;

            const QList<SdfPath> selection = session->selectionList()->paths();
            if (selection.isEmpty())
                return;

            QList<SdfPath> payloadPaths;
            {
                READ_LOCKER(locker, session->stageLock(), "stageLock");
                const UsdStageRefPtr stage = session->stageUnsafe();
                if (!stage)
                    return;

                payloadPaths = stage::nearestPayloadPaths(stage, selection);
            }

            if (payloadPaths.isEmpty())
                return;

            state->previousSelection = selection;
            session->selectionList()->updatePaths(payloadPaths);
        },
        [state](Session* session) {
            if (!session)
                return;

            session->selectionList()->updatePaths(state->previousSelection);
        });
}

Command
selectLayer()
{
    struct SelectLayerState {
        QList<SdfPath> previousSelection;
    };

    auto state = std::make_shared<SelectLayerState>();

    return Command(
        [state](Session* session) {
            if (!session)
                return;

            const QList<SdfPath> selection = session->selectionList()->paths();
            if (selection.isEmpty())
                return;

            QList<SdfPath> layerPaths;
            {
                READ_LOCKER(locker, session->stageLock(), "stageLock");
                const UsdStageRefPtr stage = session->stageUnsafe();
                if (!stage)
                    return;

                QString error;
                const SdfLayerHandle editLayer = currentEditLayer(stage, error);
                if (!editLayer)
                    return;

                layerPaths = stage::nearestLayerPayloadPaths(stage, editLayer, selection);
            }

            if (layerPaths.isEmpty())
                return;

            state->previousSelection = selection;
            session->selectionList()->updatePaths(layerPaths);
        },
        [state](Session* session) {
            if (session)
                session->selectionList()->updatePaths(state->previousSelection);
        });
}

Command
selectInvertInPayload()
{
    struct SelectInvertState {
        QList<SdfPath> previousSelection;
    };

    auto state = std::make_shared<SelectInvertState>();

    return Command(
        [state](Session* session) {
            if (!session)
                return;

            const QList<SdfPath> previousSelection = session->selectionList()->paths();
            if (previousSelection.isEmpty())
                return;

            QList<SdfPath> invertedSelection;
            {
                READ_LOCKER(locker, session->stageLock(), "stageLock");
                const UsdStageRefPtr stage = session->stageUnsafe();
                if (!stage)
                    return;

                const QList<SdfPath> scopes = stage::nearestPayloadPaths(stage, previousSelection);
                if (scopes.isEmpty())
                    return;

                const QList<SdfPath> domain = stage::leafPaths(stage, scopes, false);
                const path::PathIndex selectedRoots(previousSelection);
                for (const SdfPath& path : domain) {
                    if (!selectedRoots.covers(path))
                        invertedSelection.append(path);
                }
            }

            state->previousSelection = previousSelection;
            session->selectionList()->updatePaths(invertedSelection);
        },
        [state](Session* session) {
            if (session)
                session->selectionList()->updatePaths(state->previousSelection);
        });
}

Command
selectInvertInLayer()
{
    struct SelectInvertState {
        QList<SdfPath> previousSelection;
    };

    auto state = std::make_shared<SelectInvertState>();

    return Command(
        [state](Session* session) {
            if (!session)
                return;

            const QList<SdfPath> previousSelection = session->selectionList()->paths();
            if (previousSelection.isEmpty())
                return;

            QList<SdfPath> invertedSelection;
            {
                READ_LOCKER(locker, session->stageLock(), "stageLock");
                const UsdStageRefPtr stage = session->stageUnsafe();
                if (!stage)
                    return;

                QString error;
                const SdfLayerHandle editLayer = currentEditLayer(stage, error);
                if (!editLayer)
                    return;

                const QList<SdfPath> selectedPayloads = stage::nearestLayerPayloadPaths(stage, editLayer,
                                                                                        previousSelection);
                if (selectedPayloads.isEmpty())
                    return;

                const QList<SdfPath> domain = stage::layerPayloadPaths(stage, editLayer);
                const path::PathIndex selectedRoots(selectedPayloads);
                for (const SdfPath& path : domain) {
                    if (!selectedRoots.covers(path))
                        invertedSelection.append(path);
                }
            }

            state->previousSelection = previousSelection;
            session->selectionList()->updatePaths(invertedSelection);
        },
        [state](Session* session) {
            if (session)
                session->selectionList()->updatePaths(state->previousSelection);
        });
}

Command
isolatePaths(const QList<SdfPath>& paths)
{
    auto state = std::make_shared<QList<SdfPath>>();

    return Command(
        [paths, state](Session* session) {
            session->beginProgressBlock("Isolate paths", 1);

            command::runWorker([session, paths, state]() {
                *state = session->mask();
                QList<SdfPath> objects;
                {
                    READ_LOCKER(locker, session->stageLock(), "stageLock");
                    objects = stage::objectPaths(session->stageUnsafe(), paths);
                }
                session->setMask(objects);

                command::queueToSession(session, [session, paths]() {
                    using Status = Session::Notify::Status;
                    command::updateProgressNotify(session, QStringLiteral("Paths isolated"), paths, Status::Success, 1);
                    session->endProgressBlock();
                });
            });
        },
        [state](Session* session) {
            session->beginProgressBlock("Undo isolate paths", 1);

            command::runWorker([session, state]() {
                session->setMask(*state);

                command::queueToSession(session, [session, state]() {
                    using Status = Session::Notify::Status;
                    command::updateProgressNotify(session, QStringLiteral("Isolate undone"), *state, Status::Success,
                                                  1);
                    session->endProgressBlock();
                });
            });
        });
}

Command
selectPaths(const QList<SdfPath>& paths)
{
    auto previous = std::make_shared<QList<SdfPath>>();

    // Selection is high-frequency UI state. Keep it synchronous and avoid the
    // worker/progress machinery used by authoring commands so tree and viewport
    // selection update in the same event-loop turn.
    return Command(
        [paths, previous](Session* session) {
            if (!session)
                return;

            *previous = session->selectionList()->paths();
            session->selectionList()->updatePaths(paths);
        },
        [previous](Session* session) {
            if (session)
                session->selectionList()->updatePaths(*previous);
        });
}

namespace {

    using SelectionKind = SelectionDomain::Kind;

    QList<SdfPath> selectionPaths(UsdStageRefPtr stage, const QList<SdfPath>& mask, const SelectionDomain& domain)
    {
        if (!stage)
            return {};
        if (domain.kind == SelectionKind::LeafPrims)
            return stage::leafPaths(stage, mask, false);

        QList<SdfPath> paths;
        if (domain.kind == SelectionKind::Parents) {
            for (const UsdPrim& prim : stage->GetPseudoRoot().GetChildren())
                paths.append(prim.GetPath());
            return paths;
        }
        if (domain.kind == SelectionKind::AllPrims) {
            for (const UsdPrim& prim : stage->Traverse())
                paths.append(prim.GetPath());
            return paths;
        }

        const path::PathIndex maskRoots(mask);
        const UsdPrimRange range = domain.kind == SelectionKind::Geometry
                                       ? UsdPrimRange(stage->GetPseudoRoot(), UsdTraverseInstanceProxies())
                                       : stage->Traverse();
        for (const UsdPrim& prim : range) {
            const SdfPath& path = prim.GetPath();
            if (!mask.isEmpty() && !maskRoots.covers(path))
                continue;
            if (domain.kind == SelectionKind::PrimType) {
                if (prim.GetTypeName() == domain.type)
                    paths.append(path);
            }
            else if ((prim.IsA<UsdGeomGprim>() || prim.IsA<UsdGeomPointInstancer>())
                     && UsdGeomImageable(prim).ComputeVisibility(domain.timeCode) != UsdGeomTokens->invisible) {
                paths.append(path);
            }
        }
        return paths;
    }

    QList<SdfPath> invertExclusions(UsdStageRefPtr stage, const QList<SdfPath>& selected, SelectionKind kind)
    {
        if (kind == SelectionKind::LeafPrims)
            return selected;
        QList<SdfPath> paths = kind == SelectionKind::Geometry ? stage::objectPaths(stage, selected) : selected;
        for (SdfPath& path : paths) {
            if (path.IsPropertyPath())
                path = path.GetPrimPath();
            if (kind == SelectionKind::Parents) {
                while (path.IsPrimPath() && path.GetPathElementCount() > 1)
                    path = path.GetParentPath();
            }
        }
        return paths;
    }

    Command selectDomain(SelectionDomain domain, bool invert)
    {
        auto previousSelection = std::make_shared<QList<SdfPath>>();
        return Command(
            [domain, invert, previousSelection](Session* session) {
                if (!session)
                    return;

                session->beginProgressBlock(invert ? "Invert selection" : "Select all", 1);
                command::runWorker([session, domain, invert, previousSelection]() {
                    PerfScope workerScope(invert ? "selection.invert_worker" : "selection.all_worker");
                    QList<SdfPath> paths;
                    QList<SdfPath> exclusions;
                    {
                        READ_LOCKER(locker, session->stageLock(), "stageLock");
                        const UsdStageRefPtr stage = session->stageUnsafe();
                        *previousSelection = session->selectionList()->paths();
                        PerfScope domainScope(invert ? "selection.invert_domain" : "selection.all_domain");
                        paths = selectionPaths(stage, session->mask(), domain);
                        if (invert)
                            exclusions = invertExclusions(stage, *previousSelection, domain.kind);
                    }
                    if (invert) {
                        const path::PathIndex selectedRoots(exclusions);
                        paths.removeIf([&](const SdfPath& path) { return selectedRoots.covers(path); });
                    }

                    command::queueToSession(session, [session, invert, paths = std::move(paths)]() {
                        using Status = Session::Notify::Status;
                        session->selectionList()->updatePaths(paths);
                        command::updateProgressNotify(session,
                                                      invert ? QStringLiteral("Selection inverted")
                                                             : QStringLiteral("Paths selected"),
                                                      paths, Status::Success, 1);
                        session->endProgressBlock();
                    });
                });
            },
            [invert, previousSelection](Session* session) {
                if (!session)
                    return;
                session->beginProgressBlock(invert ? "Undo invert selection" : "Undo select all", 1);
                command::runWorker([session, invert, previousSelection]() {
                    command::queueToSession(session, [session, invert, previousSelection]() {
                        using Status = Session::Notify::Status;
                        session->selectionList()->updatePaths(*previousSelection);
                        command::updateProgressNotify(session,
                                                      invert ? QStringLiteral("Invert selection undone")
                                                             : QStringLiteral("Select all undone"),
                                                      *previousSelection, Status::Success, 1);
                        session->endProgressBlock();
                    });
                });
            });
    }

}  // namespace

Command
selectAll(bool recursive)
{
    return selectAll({ recursive ? SelectionKind::AllPrims : SelectionKind::Parents });
}

Command
selectAll(const SelectionDomain& domain)
{
    return selectDomain(domain, false);
}

Command
selectInvert()
{
    return selectInvert({ SelectionKind::LeafPrims });
}

Command
selectInvert(const SelectionDomain& domain)
{
    return selectDomain(domain, true);
}

}  // namespace stageviz
