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

Command
selectAll(bool recursive)
{
    struct SelectAllState {
        QList<SdfPath> previousSelection;
    };

    auto state = std::make_shared<SelectAllState>();

    return Command(
        [recursive, state](Session* session) {
            if (!session)
                return;

            session->beginProgressBlock("Select all", 1);

            command::runWorker([session, recursive, state]() {
                QList<SdfPath> selection;

                {
                    READ_LOCKER(locker, session->stageLock(), "stageLock");

                    const UsdStageRefPtr stage = session->stageUnsafe();
                    if (!stage)
                        return;

                    state->previousSelection = session->selectionList()->paths();

                    if (recursive) {
                        for (const UsdPrim& prim : stage->Traverse()) {
                            if (!prim || !prim.IsValid())
                                continue;

                            selection.append(prim.GetPath());
                        }
                    }
                    else {
                        for (const UsdPrim& prim : stage->GetPseudoRoot().GetChildren()) {
                            if (!prim || !prim.IsValid())
                                continue;

                            selection.append(prim.GetPath());
                        }
                    }
                }

                command::queueToSession(session, [session, selection]() {
                    using Status = Session::Notify::Status;

                    session->selectionList()->updatePaths(selection);
                    command::updateProgressNotify(session, QStringLiteral("Paths selected"), selection, Status::Success,
                                                  1);
                    session->endProgressBlock();
                });
            });
        },
        [state](Session* session) {
            if (!session)
                return;

            session->beginProgressBlock("Undo select all", 1);

            command::runWorker([session, state]() {
                command::queueToSession(session, [session, state]() {
                    using Status = Session::Notify::Status;

                    session->selectionList()->updatePaths(state->previousSelection);
                    command::updateProgressNotify(session, QStringLiteral("Select all undone"),
                                                  state->previousSelection, Status::Success, 1);
                    session->endProgressBlock();
                });
            });
        });
}

static Command
invertSelection(bool geometryOnly, UsdTimeCode timeCode)
{
    struct SelectInvertState {
        QList<SdfPath> previousSelection;
    };

    auto state = std::make_shared<SelectInvertState>();

    return Command(
        [state, geometryOnly, timeCode](Session* session) {
            if (!session)
                return;

            session->beginProgressBlock("Invert selection", 1);

            command::runWorker([session, state, geometryOnly, timeCode]() {
                PerfScope perfScope("selection.invert_worker");
                QList<SdfPath> invertedSelection;
                QList<SdfPath> previousSelection;
                QList<SdfPath> selectedPaths;
                QList<SdfPath> domain;

                {
                    READ_LOCKER(locker, session->stageLock(), "stageLock");
                    const UsdStageRefPtr stage = session->stageUnsafe();
                    previousSelection = session->selectionList()->paths();
                    const QList<SdfPath> mask = session->mask();

                    {
                        PerfScope domainScope("selection.invert_domain");
                        selectedPaths = previousSelection;
                        if (!geometryOnly)
                            domain = stage::leafPaths(stage, mask, false);
                        else if (stage) {
                            selectedPaths = stage::objectPaths(stage, previousSelection);
                            const path::PathIndex maskRoots(mask);
                            const auto geometry = UsdPrimRange(stage->GetPseudoRoot(), UsdTraverseInstanceProxies());
                            for (const UsdPrim& prim : geometry) {
                                if (!prim.IsA<UsdGeomGprim>() && !prim.IsA<UsdGeomPointInstancer>())
                                    continue;
                                const SdfPath& path = prim.GetPath();
                                if ((mask.isEmpty() || maskRoots.covers(path))
                                    && UsdGeomImageable(prim).ComputeVisibility(timeCode) != UsdGeomTokens->invisible)
                                    domain.append(path);
                            }
                        }
                    }
                }

                state->previousSelection = previousSelection;

                const path::PathIndex selectedRoots(selectedPaths);
                for (const SdfPath& path : domain) {
                    if (!selectedRoots.covers(path))
                        invertedSelection.append(path);
                }

                command::queueToSession(session, [session, invertedSelection]() {
                    using Status = Session::Notify::Status;
                    session->selectionList()->updatePaths(invertedSelection);
                    command::updateProgressNotify(session, QStringLiteral("Selection inverted"), invertedSelection,
                                                  Status::Success, 1);
                    session->endProgressBlock();
                });
            });
        },
        [state](Session* session) {
            if (!session)
                return;

            session->beginProgressBlock("Undo invert selection", 1);

            command::runWorker([session, state]() {
                command::queueToSession(session, [session, state]() {
                    using Status = Session::Notify::Status;
                    session->selectionList()->updatePaths(state->previousSelection);
                    command::updateProgressNotify(session, QStringLiteral("Invert selection undone"),
                                                  state->previousSelection, Status::Success, 1);
                    session->endProgressBlock();
                });
            });
        });
}

Command
selectInvert()
{
    return invertSelection(false, UsdTimeCode::Default());
}

Command
selectInvertGeometry(UsdTimeCode timeCode)
{
    return invertSelection(true, timeCode);
}

}  // namespace stageviz
