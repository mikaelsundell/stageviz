// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#include "command.h"
#include "commandstack.h"
#include "commandutils.h"
#include "qtutils.h"
#include "tracelocks.h"
#include "usdutils.h"
#include <QPointer>
#include <algorithm>
#include <pxr/usd/sdf/layer.h>
#include <pxr/usd/usd/editContext.h>
#include <pxr/usd/usd/editTarget.h>
#include <pxr/usd/usd/prim.h>
#include <pxr/usd/usd/primRange.h>
#include <pxr/usd/usdGeom/imageable.h>
#include <pxr/usd/usdGeom/tokens.h>
#include <pxr/usd/usdGeom/xform.h>

namespace stageviz {

namespace payload {
    struct State {
        QList<PayloadState> payloadStates;
        QList<SdfPath> previousSelection;
        QList<SdfPath> previousMask;
    };
}  // namespace payload

namespace {

    QString pathText(const SdfPath& path) { return qt::SdfPathToQString(path); }

    SdfLayerHandle openedRootLayer(UsdStageRefPtr stage, QString& error)
    {
        if (!stage) {
            error = "stage missing";
            return SdfLayerHandle();
        }

        const SdfLayerHandle rootLayer = stage->GetRootLayer();
        if (!rootLayer) {
            error = "opened root layer missing";
            return SdfLayerHandle();
        }

        return rootLayer;
    }

    bool isRootLayerAuthoredPrim(UsdStageRefPtr stage, const SdfPath& path, QString& error,
                                 bool requireStrongest = true)
    {
        if (path.IsEmpty() || path == SdfPath::AbsoluteRootPath()) {
            error = "invalid prim path";
            return false;
        }

        const SdfLayerHandle rootLayer = openedRootLayer(stage, error);
        if (!rootLayer)
            return false;

        const UsdPrim prim = stage->GetPrimAtPath(path);
        if (!prim || !prim.IsValid()) {
            error = QString("prim missing: %1").arg(pathText(path));
            return false;
        }

        if (!rootLayer->GetPrimAtPath(path)) {
            error = QString("prim is not authored in opened root layer: %1").arg(pathText(path));
            return false;
        }

        if (requireStrongest) {
            const auto& stack = prim.GetPrimStack();
            if (stack.empty() || !stack.front() || stack.front()->GetLayer() != rootLayer) {
                error = QString("prim strongest opinion is not in opened root layer: %1").arg(pathText(path));
                return false;
            }
        }

        return true;
    }

    bool isRootLayerParent(UsdStageRefPtr stage, const SdfPath& parentPath, QString& error)
    {
        if (parentPath == SdfPath::AbsoluteRootPath())
            return true;

        return isRootLayerAuthoredPrim(stage, parentPath, error);
    }

    QString appendError(const QString& message, const QString& error)
    {
        if (error.isEmpty())
            return message;

        return QString("%1: %2").arg(message, error);
    }

    QString summarizeErrors(const QStringList& errors, int maxCount = 3)
    {
        if (errors.isEmpty())
            return QString();

        QStringList out;
        for (int i = 0; i < errors.size() && i < maxCount; ++i)
            out.append(errors.at(i));

        if (errors.size() > maxCount)
            out.append(QString("%1 more").arg(errors.size() - maxCount));

        return out.join("; ");
    }

}  // namespace

Command
loadPayloads(const QList<SdfPath>& paths, const QString& variantSet, const QString& variantValue)
{
    auto state = std::make_shared<payload::State>();

    return Command(
        [paths, variantSet, variantValue, state](Session* session) {
            if (!session || paths.isEmpty())
                return;

            state->previousSelection = session->selectionList()->paths();
            state->previousMask = session->mask();

            session->beginProgressBlock("Load payloads", paths.size());
            session->setPrimsUpdate(Session::PrimsUpdate::Deferred);

            command::runWorker([session, paths, variantSet, variantValue, state]() {
                const bool useVariant = !variantSet.isEmpty() && !variantValue.isEmpty();
                const std::string variantSetName = qt::QStringToString(variantSet);
                const std::string variantSelection = qt::QStringToString(variantValue);

                QList<command::Result> pending;
                pending.reserve(16);

                QList<payload::PayloadState> payloadStates;
                payloadStates.reserve(paths.size());

                int completed = 0;
                for (const SdfPath& path : paths) {
                    if (!session || session->isProgressBlockCancelled())
                        break;

                    command::Result result;
                    result.path = path;
                    result.message = "Payload failed";
                    result.status = Session::Notify::Status::Error;

                    payload::PayloadState payloadState;
                    QString error;

                    try {
                        WRITE_LOCKER(locker, session->stageLock(), "stageLock");
                        const UsdStageRefPtr stage = session->stageUnsafe();

                        if (!stage) {
                            result.success = false;
                            error = "Stage not available";
                        }
                        else {
                            result.success = payload::applyLoad(stage, path, useVariant, variantSetName,
                                                                variantSelection, payloadState, error);

                            if (result.success) {
                                const QString pathString = qt::SdfPathToQString(path);
                                const PcpErrorVector errors = stage->GetCompositionErrors();

                                for (const PcpErrorBasePtr& compositionError : errors) {
                                    if (!compositionError)
                                        continue;

                                    const QString text = qt::StringToQString(compositionError->ToString());

                                    if (!text.contains(pathString))
                                        continue;

                                    QString restoreError;
                                    payload::restoreState(stage, payloadState, restoreError);

                                    result.success = false;
                                    error = QString("Payload failed to load: %1").arg(pathString);

                                    if (!restoreError.isEmpty())
                                        error += QString(" (%1)").arg(restoreError);

                                    break;
                                }
                            }
                        }
                    } catch (...) {
                        result.success = false;
                        error = "exception";
                    }

                    result.message = result.success ? "Payload loaded" : (error.isEmpty() ? "Payload failed" : error);

                    result.status = result.success ? Session::Notify::Status::Success : Session::Notify::Status::Error;

                    if (result.success)
                        payloadStates.append(payloadState);

                    pending.append(result);
                    ++completed;

                    if (pending.size() >= 16) {
                        const QList<command::Result> batch = pending;
                        command::queueToSession(session, [session, batch, completed]() {
                            command::flushResults(session, batch, completed);
                        });
                        pending.clear();
                    }
                }

                if (!pending.isEmpty()) {
                    const QList<command::Result> batch = pending;
                    command::queueToSession(session, [session, batch, completed]() {
                        command::flushResults(session, batch, completed);
                    });
                }

                state->payloadStates = payloadStates;

                command::queueToSession(session, [session]() {
                    session->setPrimsUpdate(Session::PrimsUpdate::Immediate);
                    session->endProgressBlock();
                });
            });
        },
        [state](Session* session) {
            if (!session || state->payloadStates.isEmpty())
                return;

            session->beginProgressBlock("Undo load payloads", state->payloadStates.size());
            session->setPrimsUpdate(Session::PrimsUpdate::Deferred);

            command::runWorker([session, state]() {
                QList<command::Result> pending;
                pending.reserve(16);

                int completed = 0;
                for (const payload::PayloadState& payloadState : state->payloadStates) {
                    if (!session || session->isProgressBlockCancelled())
                        break;

                    command::Result result;
                    result.path = payloadState.path;
                    result.message = "Payload undo failed";
                    result.status = Session::Notify::Status::Error;

                    QString error;
                    try {
                        WRITE_LOCKER(locker, session->stageLock(), "stageLock");
                        const UsdStageRefPtr stage = session->stageUnsafe();
                        result.success = payload::restoreState(stage, payloadState, error);
                    } catch (...) {
                        result.success = false;
                        error = "exception";
                    }

                    result.message = result.success ? "Payload undone"
                                                    : (error.isEmpty() ? "Payload undo failed" : error);

                    result.status = result.success ? Session::Notify::Status::Success : Session::Notify::Status::Error;

                    pending.append(result);
                    ++completed;

                    if (pending.size() >= 16) {
                        const QList<command::Result> batch = pending;
                        command::queueToSession(session, [session, batch, completed]() {
                            command::flushResults(session, batch, completed);
                        });
                        pending.clear();
                    }
                }

                command::queueToSession(session, [session, state, pending, completed]() {
                    if (!pending.isEmpty())
                        command::flushResults(session, pending, completed);

                    session->selectionList()->updatePaths(state->previousSelection);
                    session->setMask(state->previousMask);
                    session->setPrimsUpdate(Session::PrimsUpdate::Immediate);
                    session->endProgressBlock();
                });
            });
        });
}

Command
unloadPayloads(const QList<SdfPath>& paths)
{
    auto state = std::make_shared<payload::State>();

    return Command(
        [paths, state](Session* session) {
            if (!session || paths.isEmpty())
                return;

            state->previousSelection = session->selectionList()->paths();
            state->previousMask = session->mask();

            session->beginProgressBlock("Unload payloads", paths.size());
            session->setPrimsUpdate(Session::PrimsUpdate::Deferred);

            command::runWorker([session, paths, state]() {
                QList<command::Result> pending;
                pending.reserve(16);

                QList<payload::PayloadState> payloadStates;
                payloadStates.reserve(paths.size());

                QList<SdfPath> unloadedPaths;
                unloadedPaths.reserve(paths.size());

                int completed = 0;
                for (const SdfPath& path : paths) {
                    if (!session || session->isProgressBlockCancelled())
                        break;

                    command::Result result;
                    result.path = path;
                    result.message = "Payload unload failed";
                    result.status = Session::Notify::Status::Error;

                    payload::PayloadState payloadState;
                    QString error;

                    try {
                        WRITE_LOCKER(locker, session->stageLock(), "stageLock");
                        const UsdStageRefPtr stage = session->stageUnsafe();
                        result.success = payload::applyUnload(stage, path, payloadState, error);
                    } catch (...) {
                        result.success = false;
                        error = "exception";
                    }

                    result.message = result.success ? "Payload unloaded" : "Payload unload failed";
                    result.status = result.success ? Session::Notify::Status::Success : Session::Notify::Status::Error;

                    if (result.success) {
                        payloadStates.append(payloadState);
                        unloadedPaths.append(path);
                    }

                    pending.append(result);
                    ++completed;

                    if (pending.size() >= 16) {
                        const QList<command::Result> batch = pending;
                        command::queueToSession(session, [session, batch, completed]() {
                            command::flushResults(session, batch, completed);
                        });
                        pending.clear();
                    }
                }

                if (!pending.isEmpty()) {
                    const QList<command::Result> batch = pending;
                    command::queueToSession(session, [session, batch, completed]() {
                        command::flushResults(session, batch, completed);
                    });
                }

                state->payloadStates = payloadStates;

                command::queueToSession(session, [session, state, unloadedPaths]() {
                    session->selectionList()->updatePaths(
                        path::removeAffectedPaths(state->previousSelection, unloadedPaths));
                    session->setMask(path::removeAffectedPaths(state->previousMask, unloadedPaths));
                    session->setPrimsUpdate(Session::PrimsUpdate::Immediate);
                    session->endProgressBlock();
                });
            });
        },
        [state](Session* session) {
            if (!session || state->payloadStates.isEmpty())
                return;

            session->beginProgressBlock("Undo unload payloads", state->payloadStates.size());
            session->setPrimsUpdate(Session::PrimsUpdate::Deferred);

            command::runWorker([session, state]() {
                QList<command::Result> pending;
                pending.reserve(16);

                int completed = 0;
                for (const payload::PayloadState& payloadState : state->payloadStates) {
                    if (!session || session->isProgressBlockCancelled())
                        break;

                    command::Result result;
                    result.path = payloadState.path;
                    result.message = "Payload undo failed";
                    result.status = Session::Notify::Status::Error;

                    QString error;
                    try {
                        WRITE_LOCKER(locker, session->stageLock(), "stageLock");
                        const UsdStageRefPtr stage = session->stageUnsafe();
                        result.success = payload::restoreState(stage, payloadState, error);
                    } catch (...) {
                        result.success = false;
                        error = "exception";
                    }

                    result.message = result.success ? "Payload restored" : "Payload undo failed";
                    result.status = result.success ? Session::Notify::Status::Success : Session::Notify::Status::Error;

                    pending.append(result);
                    ++completed;

                    if (pending.size() >= 16) {
                        const QList<command::Result> batch = pending;
                        command::queueToSession(session, [session, batch, completed]() {
                            command::flushResults(session, batch, completed);
                        });
                        pending.clear();
                    }
                }

                command::queueToSession(session, [session, state, pending, completed]() {
                    if (!pending.isEmpty())
                        command::flushResults(session, pending, completed);

                    session->selectionList()->updatePaths(state->previousSelection);
                    session->setMask(state->previousMask);
                    session->setPrimsUpdate(Session::PrimsUpdate::Immediate);
                    session->endProgressBlock();
                });
            });
        });
}

Command
selectInvertPayload()
{
    struct SelectInvertPayloadState {
        QList<SdfPath> previousSelection;
    };

    auto state = std::make_shared<SelectInvertPayloadState>();

    return Command(
        [state](Session* session) {
            if (!session)
                return;

            session->beginProgressBlock("Invert payload selection", 1);

            command::runWorker([session, state]() {
                using Status = Session::Notify::Status;

                QList<SdfPath> previousSelection;
                QList<SdfPath> invertedPayloads;
                QList<command::Result> pending;
                pending.reserve(16);

                bool hadStage = true;
                bool hadSelectedPayloads = false;
                int completed = 0;
                int total = 0;

                {
                    READ_LOCKER(locker, session->stageLock(), "stageLock");
                    const UsdStageRefPtr stage = session->stageUnsafe();

                    if (!stage) {
                        hadStage = false;
                    }
                    else {
                        previousSelection = session->selectionList()->paths();
                        const QList<SdfPath> selectedPayloads = stage::selectionPayloadPaths(stage, previousSelection);

                        state->previousSelection = previousSelection;
                        hadSelectedPayloads = !selectedPayloads.isEmpty();

                        QSet<SdfPath> selectedSet(selectedPayloads.begin(), selectedPayloads.end());

                        for (const UsdPrim& prim : stage->TraverseAll()) {
                            if (!prim || !prim.IsValid())
                                continue;

                            const SdfPath path = prim.GetPath();
                            if (path.IsEmpty() || path == SdfPath::AbsoluteRootPath())
                                continue;

                            if (!prim.HasPayload())
                                continue;

                            if (!prim.IsLoaded())
                                continue;

                            ++total;

                            if (!selectedSet.contains(path))
                                invertedPayloads.append(path);

                            command::Result result;
                            result.path = path;
                            result.success = true;
                            result.message = selectedSet.contains(path) ? "Payload skipped" : "Payload inverted";
                            result.status = Status::Success;
                            pending.append(result);
                            ++completed;

                            if (pending.size() >= 16) {
                                const QList<command::Result> batch = pending;
                                command::queueToSession(session, [session, batch, completed]() {
                                    command::flushResults(session, batch, completed);
                                });
                                pending.clear();
                            }

                            if (session->isProgressBlockCancelled())
                                break;
                        }
                    }
                }

                if (!pending.isEmpty()) {
                    const QList<command::Result> batch = pending;
                    command::queueToSession(session, [session, batch, completed]() {
                        command::flushResults(session, batch, completed);
                    });
                }

                command::queueToSession(session, [session, hadStage, hadSelectedPayloads, invertedPayloads, total]() {
                    using Status = Session::Notify::Status;

                    if (!hadStage) {
                        session->updateProgressNotify(Session::Notify("Invert payload selection failed", {},
                                                                      Status::Error),
                                                      1);
                        session->endProgressBlock();
                        return;
                    }

                    if (!hadSelectedPayloads) {
                        session->updateProgressNotify(Session::Notify("Invert payload selection skipped", {},
                                                                      Status::Success),
                                                      1);
                        session->endProgressBlock();
                        return;
                    }

                    session->selectionList()->updatePaths(invertedPayloads);

                    session->updateProgressNotify(Session::Notify(total > 0 ? "Payload selection inverted"
                                                                            : "Invert payload selection skipped",
                                                                  invertedPayloads, Status::Success),
                                                  qMax(1, total));

                    session->endProgressBlock();
                });
            });
        },
        [state](Session* session) {
            if (!session)
                return;

            session->beginProgressBlock("Undo invert payload selection", 1);

            command::queueToSession(session, [session, state]() {
                using Status = Session::Notify::Status;
                session->selectionList()->updatePaths(state->previousSelection);
                session->updateProgressNotify(Session::Notify("Invert payload selection undone",
                                                              state->previousSelection, Status::Success),
                                              1);
                session->endProgressBlock();
            });
        });
}

Command
isolatePaths(const QList<SdfPath>& paths)
{
    auto state = std::make_shared<QList<SdfPath>>();

    return Command(
        [paths, state](Session* session) {
            session->beginProgressBlock("isolate paths", 1);

            command::runWorker([session, paths, state]() {
                *state = session->mask();
                session->setMask(paths);

                command::queueToSession(session, [session, paths]() {
                    using Status = Session::Notify::Status;
                    session->updateProgressNotify(Session::Notify("paths isolated", paths, Status::Success), 1);
                    session->endProgressBlock();
                });
            });
        },
        [state](Session* session) {
            session->beginProgressBlock("undo isolate paths", 1);

            command::runWorker([session, state]() {
                session->setMask(*state);

                command::queueToSession(session, [session, state]() {
                    using Status = Session::Notify::Status;
                    session->updateProgressNotify(Session::Notify("isolate undone", *state, Status::Success), 1);
                    session->endProgressBlock();
                });
            });
        });
}

Command
selectPaths(const QList<SdfPath>& paths)
{
    auto previous = std::make_shared<QList<SdfPath>>();

    return Command(
        [paths, previous](Session* session) {
            session->beginProgressBlock("Select paths", 1);

            command::runWorker([session, paths, previous]() {
                *previous = session->selectionList()->paths();
                session->selectionList()->updatePaths(paths);

                command::queueToSession(session, [session, paths]() {
                    using Status = Session::Notify::Status;
                    session->updateProgressNotify(Session::Notify("Paths selected", paths, Status::Success), 1);
                    session->endProgressBlock();
                });
            });
        },
        [previous](Session* session) {
            session->beginProgressBlock("Undo select paths", 1);

            command::runWorker([session, previous]() {
                session->selectionList()->updatePaths(*previous);

                command::queueToSession(session, [session, previous]() {
                    using Status = Session::Notify::Status;
                    session->updateProgressNotify(Session::Notify("Select undone", *previous, Status::Success), 1);
                    session->endProgressBlock();
                });
            });
        });
}

Command
selectAll()
{
    struct SelectAllState {
        QList<SdfPath> previousSelection;
    };

    auto state = std::make_shared<SelectAllState>();

    return Command(
        [state](Session* session) {
            if (!session)
                return;

            session->beginProgressBlock("Select all", 1);

            command::runWorker([session, state]() {
                QList<SdfPath> selection;
                QList<SdfPath> previousSelection;
                QList<SdfPath> mask;

                {
                    READ_LOCKER(locker, session->stageLock(), "stageLock");
                    const UsdStageRefPtr stage = session->stageUnsafe();

                    previousSelection = session->selectionList()->paths();
                    mask = session->mask();
                    selection = stage::leafPaths(stage, mask, true);
                }

                state->previousSelection = previousSelection;

                command::queueToSession(session, [session, selection]() {
                    using Status = Session::Notify::Status;
                    session->selectionList()->updatePaths(selection);
                    session->updateProgressNotify(Session::Notify("Paths selected", selection, Status::Success), 1);
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
                    session->updateProgressNotify(Session::Notify("Select all undone", state->previousSelection,
                                                                  Status::Success),
                                                  1);
                    session->endProgressBlock();
                });
            });
        });
}

Command
selectInvert()
{
    struct SelectInvertState {
        QList<SdfPath> previousSelection;
    };

    auto state = std::make_shared<SelectInvertState>();

    return Command(
        [state](Session* session) {
            if (!session)
                return;

            session->beginProgressBlock("Invert selection", 1);

            command::runWorker([session, state]() {
                QList<SdfPath> invertedSelection;
                QList<SdfPath> previousSelection;
                QList<SdfPath> domain;
                QList<SdfPath> mask;

                {
                    READ_LOCKER(locker, session->stageLock(), "stageLock");
                    const UsdStageRefPtr stage = session->stageUnsafe();

                    previousSelection = session->selectionList()->paths();
                    mask = session->mask();

                    // Preserve previous selectInvert() behavior:
                    // children outside the mask still make a masked parent non-leaf.
                    domain = stage::leafPaths(stage, mask, false);
                }

                state->previousSelection = previousSelection;

                for (const SdfPath& path : domain) {
                    if (!path::isCoveredBySelection(previousSelection, path))
                        invertedSelection.append(path);
                }

                command::queueToSession(session, [session, invertedSelection]() {
                    using Status = Session::Notify::Status;
                    session->selectionList()->updatePaths(invertedSelection);
                    session->updateProgressNotify(Session::Notify("Selection inverted", invertedSelection,
                                                                  Status::Success),
                                                  1);
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
                    session->updateProgressNotify(Session::Notify("Invert selection undone", state->previousSelection,
                                                                  Status::Success),
                                                  1);
                    session->endProgressBlock();
                });
            });
        });
}

Command
showPaths(const QList<SdfPath>& paths, bool recursive)
{
    auto state = std::make_shared<QHash<SdfPath, bool>>();

    return Command(
        [paths, recursive, state](Session* session) {
            command::beginDeferred(session, "Show paths", 1);

            command::runWorker([session, paths, recursive, state]() {
                bool success = false;

                {
                    WRITE_LOCKER(locker, session->stageLock(), "stageLock");
                    const UsdStageRefPtr stage = session->stageUnsafe();

                    if (stage) {
                        state->clear();
                        for (const SdfPath& path : paths)
                            state->insert(path, stage::isVisible(stage, path));

                        stage::setVisible(stage, paths, true, recursive);
                        success = true;
                    }
                }

                command::queueToSession(session, [session, paths, success]() {
                    using Status = Session::Notify::Status;
                    command::finishDeferred(session, success ? "Paths shown" : "Show paths failed", paths,
                                            success ? Status::Success : Status::Error);
                });
            });
        },
        [state, recursive](Session* session) {
            command::beginDeferred(session, "Undo show paths", 1);

            command::runWorker([session, state, recursive]() {
                bool success = false;
                QList<SdfPath> restoredPaths;

                {
                    WRITE_LOCKER(locker, session->stageLock(), "stageLock");
                    const UsdStageRefPtr stage = session->stageUnsafe();

                    if (stage) {
                        restoredPaths = state->keys();

                        for (auto it = state->cbegin(); it != state->cend(); ++it)
                            stage::setVisible(stage, { it.key() }, it.value(), recursive);

                        success = true;
                    }
                }

                command::queueToSession(session, [session, restoredPaths, success]() {
                    using Status = Session::Notify::Status;
                    command::finishDeferred(session, success ? "Show undone" : "Undo show paths failed", restoredPaths,
                                            success ? Status::Success : Status::Error);
                });
            });
        });
}

Command
hidePaths(const QList<SdfPath>& paths, bool recursive)
{
    auto state = std::make_shared<QHash<SdfPath, bool>>();

    return Command(
        [paths, recursive, state](Session* session) {
            command::beginDeferred(session, "Hide paths", 1);

            command::runWorker([session, paths, recursive, state]() {
                bool success = false;

                {
                    WRITE_LOCKER(locker, session->stageLock(), "stageLock");
                    const UsdStageRefPtr stage = session->stageUnsafe();

                    if (stage) {
                        state->clear();
                        for (const SdfPath& path : paths)
                            state->insert(path, stage::isVisible(stage, path));

                        stage::setVisible(stage, paths, false, recursive);
                        success = true;
                    }
                }

                command::queueToSession(session, [session, paths, success]() {
                    using Status = Session::Notify::Status;
                    command::finishDeferred(session, success ? "Paths hidden" : "Hide paths failed", paths,
                                            success ? Status::Success : Status::Error);
                });
            });
        },
        [state, recursive](Session* session) {
            command::beginDeferred(session, "Undo hide paths", 1);

            command::runWorker([session, state, recursive]() {
                bool success = false;
                QList<SdfPath> restoredPaths;

                {
                    WRITE_LOCKER(locker, session->stageLock(), "stageLock");
                    const UsdStageRefPtr stage = session->stageUnsafe();

                    if (stage) {
                        restoredPaths = state->keys();

                        for (auto it = state->cbegin(); it != state->cend(); ++it)
                            stage::setVisible(stage, { it.key() }, it.value(), recursive);

                        success = true;
                    }
                }

                command::queueToSession(session, [session, restoredPaths, success]() {
                    using Status = Session::Notify::Status;
                    command::finishDeferred(session, success ? "Hide undone" : "Undo hide paths failed", restoredPaths,
                                            success ? Status::Success : Status::Error);
                });
            });
        });
}

Command
stageUp(Session::StageUp stageUp)
{
    auto state = std::make_shared<Session::StageUp>(Session::StageUp::Y);

    return Command(
        [stageUp, state](Session* session) {
            session->beginProgressBlock("Set stage up", 1);

            command::runWorker([session, stageUp, state]() {
                *state = session->stageUp();
                session->setStageUp(stageUp);

                command::queueToSession(session, [session, stageUp]() {
                    using Status = Session::Notify::Status;
                    const QString axis = (stageUp == Session::StageUp::Z) ? "Z" : "Y";
                    session->updateProgressNotify(Session::Notify(QString("Stage up set to %1").arg(axis), {},
                                                                  Status::Success),
                                                  1);
                    session->endProgressBlock();
                });
            });
        },
        [state](Session* session) {
            session->beginProgressBlock("Undo set stage up", 1);

            command::runWorker([session, state]() {
                session->setStageUp(*state);

                command::queueToSession(session, [session, state]() {
                    using Status = Session::Notify::Status;
                    const QString axis = (*state == Session::StageUp::Z) ? "Z" : "Y";
                    session->updateProgressNotify(Session::Notify(QString("Set stage up undone to %1").arg(axis), {},
                                                                  Status::Success),
                                                  1);
                    session->endProgressBlock();
                });
            });
        });
}

namespace snapshot {
    struct DeleteState {
        QVector<PrimState> prims;
        QHash<SdfPath, TfTokenVector> parentOrders;
        SdfPath previousDefaultPrimPath;
        QList<SdfPath> previousSelection;
        QList<SdfPath> previousMask;
    };
}  // namespace snapshot

Command
defaultPrimPath(const SdfPath& path)
{
    struct DefaultPrimState {
        SdfPath previousDefaultPrimPath;
        SdfPath newDefaultPrimPath;
    };

    auto state = std::make_shared<DefaultPrimState>();

    return Command(
        [path, state](Session* session) {
            if (!session || path.IsEmpty() || path == SdfPath::AbsoluteRootPath())
                return;

            session->beginProgressBlock("Set default prim", 1);

            command::runWorker([=]() {
                bool hadStage = true;
                bool success = false;
                QString error;

                {
                    WRITE_LOCKER(locker, session->stageLock(), "stageLock");
                    const UsdStageRefPtr stage = session->stageUnsafe();

                    if (!stage) {
                        hadStage = false;
                    }
                    else {
                        if (UsdPrim previousDefaultPrim = stage->GetDefaultPrim())
                            state->previousDefaultPrimPath = previousDefaultPrim.GetPath();
                        else
                            state->previousDefaultPrimPath = SdfPath();

                        QString rootError;
                        const SdfLayerHandle rootLayer = openedRootLayer(stage, rootError);
                        const UsdPrim prim = stage->GetPrimAtPath(path);
                        if (!rootLayer) {
                            error = rootError;
                        }
                        else if (!prim || !prim.IsValid()) {
                            error = "invalid prim";
                        }
                        else if (path.GetParentPath() != SdfPath::AbsoluteRootPath()) {
                            error = "default prim must be a root prim";
                        }
                        else if (!isRootLayerAuthoredPrim(stage, path, error)) {}
                        else {
                            UsdEditContext context(stage, UsdEditTarget(rootLayer));
                            stage->SetDefaultPrim(prim);
                            state->newDefaultPrimPath = path;
                            success = true;
                        }
                    }
                }

                command::queueToSession(session, [=]() {
                    using Status = Session::Notify::Status;

                    if (!hadStage) {
                        session->updateProgressNotify(Session::Notify("Set default prim failed", { path },
                                                                      Status::Error),
                                                      1);
                        session->endProgressBlock();
                        return;
                    }

                    if (!success) {
                        session->updateProgressNotify(
                            Session::Notify(error.isEmpty() ? "Set default prim failed"
                                                            : QString("Set default prim failed: %1").arg(error),
                                            { path }, Status::Error),
                            1);
                        session->endProgressBlock();
                        return;
                    }

                    session->updateProgressNotify(Session::Notify("Default prim set", { path }, Status::Success), 1);
                    session->endProgressBlock();
                });
            });
        },
        [state](Session* session) {
            if (!session)
                return;

            session->beginProgressBlock("Undo set default prim", 1);

            command::runWorker([=]() {
                bool hadStage = true;
                bool success = false;
                QString error;

                {
                    WRITE_LOCKER(locker, session->stageLock(), "stageLock");
                    const UsdStageRefPtr stage = session->stageUnsafe();

                    if (!stage) {
                        hadStage = false;
                    }
                    else {
                        QString rootError;
                        const SdfLayerHandle rootLayer = openedRootLayer(stage, rootError);
                        if (!rootLayer) {
                            error = rootError;
                        }
                        else if (state->previousDefaultPrimPath.IsEmpty()) {
                            UsdEditContext context(stage, UsdEditTarget(rootLayer));
                            stage->ClearDefaultPrim();
                            success = true;
                        }
                        else {
                            const UsdPrim prim = stage->GetPrimAtPath(state->previousDefaultPrimPath);
                            if (!prim || !prim.IsValid()) {
                                error = "previous default prim missing";
                            }
                            else {
                                UsdEditContext context(stage, UsdEditTarget(rootLayer));
                                stage->SetDefaultPrim(prim);
                                success = true;
                            }
                        }
                    }
                }

                command::queueToSession(session, [=]() {
                    using Status = Session::Notify::Status;

                    if (!hadStage) {
                        session->updateProgressNotify(Session::Notify("Undo set default prim failed", {}, Status::Error),
                                                      1);
                        session->endProgressBlock();
                        return;
                    }

                    if (!success) {
                        session->updateProgressNotify(
                            Session::Notify(error.isEmpty() ? "Undo set default prim failed"
                                                            : QString("Undo set default prim failed: %1").arg(error),
                                            {}, Status::Error),
                            1);
                        session->endProgressBlock();
                        return;
                    }

                    session->updateProgressNotify(Session::Notify("Default prim undone",
                                                                  { state->previousDefaultPrimPath }, Status::Success),
                                                  1);
                    session->endProgressBlock();
                });
            });
        });
}

Command
deletePaths(const QList<SdfPath>& inPaths)
{
    auto state = std::make_shared<snapshot::DeleteState>();

    return Command(
        [inPaths, state](Session* session) {
            state->previousSelection = session->selectionList()->paths();
            state->previousMask = session->mask();
            state->previousDefaultPrimPath = SdfPath();

            command::beginDeferred(session, "Delete paths", 1);

            command::runWorker([session, inPaths, state]() {
                bool success = false;
                QList<SdfPath> changed;
                QList<SdfPath> removedPaths;
                QString error;

                {
                    WRITE_LOCKER(locker, session->stageLock(), "stageLock");

                    const UsdStageRefPtr stage = session->stageUnsafe();
                    if (stage) {
                        QString rootError;
                        const SdfLayerHandle rootLayer = openedRootLayer(stage, rootError);
                        if (!rootLayer) {
                            error = rootError;
                        }
                        else {
                            QList<SdfPath> editable;
                            QStringList rejected;

                            const QList<SdfPath> candidates = path::minimalRootPaths(path::uniquePaths(inPaths));
                            editable.reserve(candidates.size());

                            for (const SdfPath& candidate : candidates) {
                                const SdfPath primPath = candidate.IsPropertyPath() ? candidate.GetPrimPath()
                                                                                    : candidate;

                                QString pathError;
                                if (isRootLayerAuthoredPrim(stage, primPath, pathError)) {
                                    editable.append(primPath);
                                }
                                else {
                                    rejected.append(pathError);
                                }
                            }

                            const QList<SdfPath> paths = path::minimalRootPaths(editable);

                            state->prims.clear();
                            state->parentOrders.clear();

                            if (UsdPrim defaultPrim = stage->GetDefaultPrim())
                                state->previousDefaultPrimPath = defaultPrim.GetPath();

                            bool deletesDefaultPrim = false;
                            if (!state->previousDefaultPrimPath.IsEmpty()) {
                                for (const SdfPath& path : paths) {
                                    if (state->previousDefaultPrimPath == path
                                        || state->previousDefaultPrimPath.HasPrefix(path)) {
                                        deletesDefaultPrim = true;
                                        break;
                                    }
                                }
                            }

                            QSet<SdfPath> changedSet;

                            for (const SdfPath& path : paths) {
                                changedSet.insert(path);

                                const SdfPath parentPath = path.GetParentPath();
                                if (!parentPath.IsEmpty() && parentPath != SdfPath::AbsoluteRootPath()) {
                                    changedSet.insert(parentPath);

                                    if (!state->parentOrders.contains(parentPath)) {
                                        TfTokenVector order;
                                        if (stage::captureChildOrder(stage, parentPath, order))
                                            state->parentOrders.insert(parentPath, order);
                                    }
                                }
                            }

                            bool removedAny = false;
                            if (deletesDefaultPrim)
                                stage->ClearDefaultPrim();

                            UsdEditContext context(stage, UsdEditTarget(rootLayer));
                            for (const SdfPath& path : paths) {
                                snapshot::PrimState primState;
                                if (!snapshot::capturePrimToLayer(stage, path, primState)) {
                                    rejected.append(QString("failed to snapshot prim: %1").arg(pathText(path)));
                                    continue;
                                }

                                if (!stage::removePrimSpec(rootLayer, primState.specPath)) {
                                    rejected.append(QString("failed to remove root-layer spec: %1")
                                                        .arg(pathText(primState.specPath)));
                                    continue;
                                }

                                state->prims.append(primState);
                                removedPaths.append(path);
                                removedAny = true;
                            }

                            if (removedAny) {
                                changed = changedSet.values();
                                success = true;
                            }

                            if (!rejected.isEmpty()) {
                                error = summarizeErrors(rejected);
                                if (success)
                                    error = QString("Some paths skipped: %1").arg(error);
                            }
                        }
                    }
                    else {
                        error = "stage missing";
                    }
                }

                command::queueToSession(session, [session, state, changed, removedPaths, success, error]() {
                    using Status = Session::Notify::Status;

                    command::finishDeferred(session,
                                            success ? (error.isEmpty() ? "Paths deleted"
                                                                       : QString("Paths deleted (%1)").arg(error))
                                                    : appendError("Delete paths failed", error),
                                            success ? changed : removedPaths,
                                            success ? Status::Success : Status::Error);

                    session->selectionList()->updatePaths(
                        path::removeAffectedPaths(state->previousSelection, removedPaths));
                    session->setMask(path::removeAffectedPaths(state->previousMask, removedPaths));
                });
            });
        },
        [state](Session* session) {
            command::beginDeferred(session, "Undo delete paths", 1);

            command::runWorker([session, state]() {
                bool success = false;
                QList<SdfPath> changed;
                QString error;

                {
                    WRITE_LOCKER(locker, session->stageLock(), "stageLock");

                    const UsdStageRefPtr stage = session->stageUnsafe();
                    if (stage) {
                        QString rootError;
                        const SdfLayerHandle rootLayer = openedRootLayer(stage, rootError);
                        if (!rootLayer) {
                            error = rootError;
                        }
                        else {
                            UsdEditContext context(stage, UsdEditTarget(rootLayer));
                            snapshot::sortByHierarchy(state->prims);

                            QSet<SdfPath> changedSet;

                            for (const auto& primState : state->prims) {
                                snapshot::restorePrimFromSnapshotLayer(rootLayer, primState);
                                changedSet.insert(primState.stagePath);

                                const SdfPath parentPath = primState.stagePath.GetParentPath();
                                if (!parentPath.IsEmpty() && parentPath != SdfPath::AbsoluteRootPath())
                                    changedSet.insert(parentPath);
                            }

                            for (auto it = state->parentOrders.cbegin(); it != state->parentOrders.cend(); ++it) {
                                stage::restoreChildOrder(stage, it.key(), it.value());
                                changedSet.insert(it.key());
                            }

                            if (!state->previousDefaultPrimPath.IsEmpty()) {
                                const UsdPrim defaultPrim = stage->GetPrimAtPath(state->previousDefaultPrimPath);
                                if (defaultPrim)
                                    stage->SetDefaultPrim(defaultPrim);
                            }

                            changed = changedSet.values();
                            success = true;
                        }
                    }
                    else {
                        error = "stage missing";
                    }
                }

                command::queueToSession(session, [session, state, changed, success, error]() {
                    using Status = Session::Notify::Status;

                    command::finishDeferred(session,
                                            success ? "Delete undone" : appendError("Undo delete paths failed", error),
                                            changed, success ? Status::Success : Status::Error);

                    session->selectionList()->updatePaths(state->previousSelection);
                    session->setMask(state->previousMask);
                });
            });
        });
}

Command
renamePath(const SdfPath& path, const QString& newNameInput)
{
    struct RenameState {
        SdfPath oldPath;
        SdfPath newPath;
        SdfPath parentPath;
        TfTokenVector oldOrder;
        TfTokenVector newOrder;
        QList<SdfPath> previousSelection;
        QList<SdfPath> previousMask;
    };

    auto state = std::make_shared<RenameState>();

    return Command(
        [path, newNameInput, state](Session* session) {
            if (!session || path.IsEmpty())
                return;

            state->previousSelection = session->selectionList()->paths();
            state->previousMask = session->mask();

            command::beginDeferred(session, "Rename path", 1);

            command::runWorker([=]() {
                bool hadStage = true;
                bool renamed = false;
                bool noop = false;
                QString error;
                SdfPath newPath;

                {
                    WRITE_LOCKER(locker, session->stageLock(), "stageLock");
                    const UsdStageRefPtr stage = session->stageUnsafe();

                    if (!stage) {
                        hadStage = false;
                    }
                    else {
                        QString rootError;
                        const SdfLayerHandle rootLayer = openedRootLayer(stage, rootError);
                        if (!rootLayer) {
                            error = rootError;
                        }
                        else if (!isRootLayerAuthoredPrim(stage, path, error)) {}
                        else {
                            newPath = stage::buildRenamePath(stage, path, newNameInput, error);

                            if (newPath.IsEmpty()) {}
                            else if (newPath == path) {
                                noop = true;
                            }
                            else {
                                state->oldPath = path;
                                state->newPath = newPath;
                                state->parentPath = path.GetParentPath();
                                state->oldOrder.clear();
                                state->newOrder.clear();

                                if (!state->parentPath.IsEmpty() && state->parentPath != SdfPath::AbsoluteRootPath()) {
                                    stage::captureChildOrder(stage, state->parentPath, state->oldOrder);
                                }

                                const UsdStageLoadRules rules = stage->GetLoadRules();
                                UsdEditContext context(stage, UsdEditTarget(rootLayer));

                                if (stage::renamePrim(stage, path, newPath, error)) {
                                    stage->SetLoadRules(stage::remapLoadRules(rules, path, newPath));

                                    if (!state->oldOrder.empty()) {
                                        state->newOrder = stage::remapChildOrder(state->oldOrder, path.GetNameToken(),
                                                                                 newPath.GetNameToken());

                                        stage::restoreChildOrder(stage, state->parentPath, state->newOrder);
                                    }

                                    renamed = true;
                                }
                            }
                        }
                    }
                }

                command::queueToSession(session, [=]() {
                    using Status = Session::Notify::Status;

                    if (!hadStage) {
                        command::finishDeferred(session, "Rename path failed: stage missing", { path }, Status::Error);
                        return;
                    }

                    if (noop) {
                        command::finishDeferred(session, "Rename skipped", { path }, Status::Success);
                        return;
                    }

                    if (!renamed) {
                        command::finishDeferred(session, appendError("Rename path failed", error), { path },
                                                Status::Error);
                        return;
                    }

                    command::finishDeferred(session, "Path renamed", { path, newPath }, Status::Success);

                    session->selectionList()->updatePaths(
                        path::remapAffectedPaths(state->previousSelection, path, newPath));
                    session->setMask(path::remapAffectedPaths(state->previousMask, path, newPath));
                });
            });
        },
        [state](Session* session) {
            if (!session || state->oldPath.IsEmpty() || state->newPath.IsEmpty())
                return;

            command::beginDeferred(session, "Undo rename path", 1);

            command::runWorker([=]() {
                bool hadStage = true;
                bool restored = false;
                QString error;

                {
                    WRITE_LOCKER(locker, session->stageLock(), "stageLock");
                    const UsdStageRefPtr stage = session->stageUnsafe();

                    if (!stage) {
                        hadStage = false;
                    }
                    else {
                        QString rootError;
                        const SdfLayerHandle rootLayer = openedRootLayer(stage, rootError);
                        if (!rootLayer) {
                            error = rootError;
                        }
                        else {
                            const UsdStageLoadRules rules = stage->GetLoadRules();
                            UsdEditContext context(stage, UsdEditTarget(rootLayer));

                            if (stage::renamePrim(stage, state->newPath, state->oldPath, error)) {
                                stage->SetLoadRules(stage::remapLoadRules(rules, state->newPath, state->oldPath));

                                if (!state->oldOrder.empty() && !state->parentPath.IsEmpty()
                                    && state->parentPath != SdfPath::AbsoluteRootPath()) {
                                    stage::restoreChildOrder(stage, state->parentPath, state->oldOrder);
                                }

                                restored = true;
                            }
                        }
                    }
                }

                command::queueToSession(session, [=]() {
                    using Status = Session::Notify::Status;

                    if (!hadStage) {
                        command::finishDeferred(session, "Undo rename path failed: stage missing", {}, Status::Error);
                        return;
                    }

                    if (!restored) {
                        command::finishDeferred(session, appendError("Undo rename path failed", error), {},
                                                Status::Error);
                        return;
                    }

                    command::finishDeferred(session, "Rename undone", { state->oldPath }, Status::Success);

                    session->selectionList()->updatePaths(state->previousSelection);
                    session->setMask(state->previousMask);
                });
            });
        });
}

Command
newXformPath(const SdfPath& parentPath, const QString& nameInput)
{
    struct MoveItem {
        SdfPath oldPath;
        SdfPath newPath;
        SdfPath oldParentPath;
    };

    struct NewXformState {
        SdfPath parentPath;
        SdfPath createdPath;
        TfTokenVector oldParentOrder;
        TfTokenVector newParentOrder;
        QHash<SdfPath, TfTokenVector> oldMoveParentOrders;
        QList<MoveItem> movedItems;
        QList<SdfPath> previousSelection;
        QList<SdfPath> previousMask;
    };

    auto state = std::make_shared<NewXformState>();

    return Command(
        [parentPath, nameInput, state](Session* session) {
            if (!session || parentPath.IsEmpty())
                return;

            state->previousSelection = session->selectionList()->paths();
            state->previousMask = session->mask();

            command::beginDeferred(session, "New xform", 1);

            command::runWorker([=]() {
                bool hadStage = true;
                bool created = false;
                bool noop = false;
                QString error;
                SdfPath newPath;
                QList<SdfPath> movePaths;
                QList<SdfPath> changed;

                auto appendChanged = [](QList<SdfPath>& paths, const SdfPath& path) {
                    if (!path.IsEmpty() && !paths.contains(path))
                        paths.append(path);
                };

                auto restoreOrders = [](const UsdStageRefPtr& stage, const QHash<SdfPath, TfTokenVector>& orders) {
                    for (auto it = orders.cbegin(); it != orders.cend(); ++it) {
                        if (it.key() == SdfPath::AbsoluteRootPath())
                            continue;

                        stage::restoreChildOrder(stage, it.key(), it.value());
                    }
                };

                {
                    WRITE_LOCKER(locker, session->stageLock(), "stageLock");
                    const UsdStageRefPtr stage = session->stageUnsafe();

                    if (!stage) {
                        hadStage = false;
                    }
                    else {
                        const QList<SdfPath> selection = path::minimalRootPaths(state->previousSelection);

                        if (selection.size() > 1)
                            movePaths = selection;

                        newPath = stage::buildChildPath(stage, parentPath, nameInput, error);

                        if (newPath.IsEmpty()) {
                            noop = true;
                        }
                        else {
                            state->parentPath = parentPath;
                            state->createdPath = newPath;
                            state->oldParentOrder.clear();
                            state->newParentOrder.clear();
                            state->oldMoveParentOrders.clear();
                            state->movedItems.clear();

                            QString rootError;
                            const SdfLayerHandle rootLayer = openedRootLayer(stage, rootError);
                            if (!rootLayer) {
                                error = rootError;
                            }
                            else if (!isRootLayerParent(stage, parentPath, error)) {}
                            else {
                                const bool parentIsRoot = parentPath == SdfPath::AbsoluteRootPath();

                                if (!parentIsRoot)
                                    stage::captureChildOrder(stage, parentPath, state->oldParentOrder);

                                UsdEditContext context(stage, UsdEditTarget(rootLayer));
                                const UsdGeomXform xform = UsdGeomXform::Define(stage, newPath);
                                if (!xform || !xform.GetPrim()) {
                                    error = "define failed";
                                }
                                else {
                                    if (!parentIsRoot) {
                                        state->newParentOrder = state->oldParentOrder;
                                        state->newParentOrder.push_back(newPath.GetNameToken());
                                        stage::restoreChildOrder(stage, parentPath, state->newParentOrder);
                                    }

                                    bool movedSelection = true;

                                    for (const SdfPath& movePath : movePaths) {
                                        if (movePath.IsEmpty() || movePath == SdfPath::AbsoluteRootPath()
                                            || movePath == newPath || newPath.HasPrefix(movePath)) {
                                            error = "invalid move path";
                                            movedSelection = false;
                                            break;
                                        }

                                        if (stage::isInsideCompositionArc(stage, movePath)) {
                                            error = "cannot move into or out of composed prims";
                                            movedSelection = false;
                                            break;
                                        }

                                        QString authoredError;
                                        if (!isRootLayerAuthoredPrim(stage, movePath, authoredError)) {
                                            error = authoredError;
                                            movedSelection = false;
                                            break;
                                        }

                                        const SdfPath oldParentPath = movePath.GetParentPath();
                                        if (oldParentPath.IsEmpty()) {
                                            error = "invalid source parent";
                                            movedSelection = false;
                                            break;
                                        }

                                        if (!state->oldMoveParentOrders.contains(oldParentPath)
                                            && oldParentPath != SdfPath::AbsoluteRootPath()) {
                                            TfTokenVector order;
                                            stage::captureChildOrder(stage, oldParentPath, order);
                                            state->oldMoveParentOrders.insert(oldParentPath, order);
                                        }

                                        MoveItem item;
                                        item.oldPath = movePath;
                                        item.oldParentPath = oldParentPath;
                                        item.newPath = newPath.AppendChild(movePath.GetNameToken());
                                        state->movedItems.append(item);
                                    }

                                    if (movedSelection) {
                                        for (const MoveItem& item : state->movedItems) {
                                            QString moveError;
                                            if (!stage::movePrim(stage, item.oldPath, newPath, moveError)) {
                                                error = QString("failed to move %1 to %2")
                                                            .arg(qt::SdfPathToQString(item.oldPath),
                                                                 qt::SdfPathToQString(newPath));

                                                if (!moveError.isEmpty())
                                                    error += QString(": %1").arg(moveError);

                                                for (auto it = state->movedItems.crbegin();
                                                     it != state->movedItems.crend(); ++it) {
                                                    if (it->oldPath == item.oldPath)
                                                        break;

                                                    QString rollbackError;
                                                    stage::movePrim(stage, it->newPath, it->oldParentPath,
                                                                    rollbackError);
                                                }

                                                restoreOrders(stage, state->oldMoveParentOrders);

                                                if (!parentIsRoot && !state->oldParentOrder.empty())
                                                    stage::restoreChildOrder(stage, parentPath, state->oldParentOrder);

                                                stage::removePrimSpec(rootLayer, newPath);
                                                movedSelection = false;
                                                break;
                                            }
                                        }
                                    }

                                    if (movedSelection && error.isEmpty()) {
                                        appendChanged(changed, parentPath);
                                        appendChanged(changed, newPath);

                                        for (const MoveItem& item : state->movedItems) {
                                            appendChanged(changed, item.oldParentPath);
                                            appendChanged(changed, item.oldPath);
                                            appendChanged(changed, item.newPath);
                                        }

                                        created = true;
                                    }
                                }
                            }
                        }
                    }
                }

                command::queueToSession(session, [=]() {
                    using Status = Session::Notify::Status;

                    if (!hadStage) {
                        command::finishDeferred(session, "New xform failed", {}, Status::Error);
                        return;
                    }

                    if (noop) {
                        command::finishDeferred(session,
                                                error.isEmpty() ? "New xform skipped"
                                                                : QString("New xform skipped: %1").arg(error),
                                                {}, Status::Success);
                        return;
                    }

                    if (!created) {
                        command::finishDeferred(session,
                                                error.isEmpty() ? "New xform failed"
                                                                : QString("New xform failed: %1").arg(error),
                                                changed, Status::Error);
                        return;
                    }

                    command::finishDeferred(session,
                                            state->movedItems.isEmpty() ? "Xform created"
                                                                        : "Xform created and paths moved",
                                            changed, Status::Success);

                    session->selectionList()->updatePaths({ newPath });
                });
            });
        },
        [state](Session* session) {
            if (!session || state->createdPath.IsEmpty())
                return;

            command::beginDeferred(session, "Undo new xform", 1);

            command::runWorker([=]() {
                bool hadStage = true;
                bool restored = false;
                QString error;
                QList<SdfPath> changed;

                auto appendChanged = [](QList<SdfPath>& paths, const SdfPath& path) {
                    if (!path.IsEmpty() && !paths.contains(path))
                        paths.append(path);
                };

                {
                    WRITE_LOCKER(locker, session->stageLock(), "stageLock");
                    const UsdStageRefPtr stage = session->stageUnsafe();

                    if (!stage) {
                        hadStage = false;
                    }
                    else {
                        QString rootError;
                        const SdfLayerHandle rootLayer = openedRootLayer(stage, rootError);
                        if (!rootLayer) {
                            error = rootError;
                        }
                        else {
                            UsdEditContext context(stage, UsdEditTarget(rootLayer));
                            restored = true;

                            for (auto it = state->movedItems.crbegin(); it != state->movedItems.crend(); ++it) {
                                QString moveError;
                                if (!stage::movePrim(stage, it->newPath, it->oldParentPath, moveError)) {
                                    error = QString("failed to restore %1 to %2")
                                                .arg(qt::SdfPathToQString(it->newPath),
                                                     qt::SdfPathToQString(it->oldParentPath));

                                    if (!moveError.isEmpty())
                                        error += QString(": %1").arg(moveError);

                                    restored = false;
                                    break;
                                }
                            }

                            if (restored) {
                                for (auto it = state->oldMoveParentOrders.cbegin();
                                     it != state->oldMoveParentOrders.cend(); ++it) {
                                    if (it.key() == SdfPath::AbsoluteRootPath())
                                        continue;

                                    stage::restoreChildOrder(stage, it.key(), it.value());
                                }

                                if (!state->oldParentOrder.empty() && !state->parentPath.IsEmpty()
                                    && state->parentPath != SdfPath::AbsoluteRootPath()) {
                                    stage::restoreChildOrder(stage, state->parentPath, state->oldParentOrder);
                                }

                                restored = stage::removePrimSpec(rootLayer, state->createdPath);
                            }

                            if (restored) {
                                appendChanged(changed, state->parentPath);
                                appendChanged(changed, state->createdPath);

                                for (const MoveItem& item : state->movedItems) {
                                    appendChanged(changed, item.oldParentPath);
                                    appendChanged(changed, item.oldPath);
                                    appendChanged(changed, item.newPath);
                                }
                            }
                        }
                    }
                }

                command::queueToSession(session, [=]() {
                    using Status = Session::Notify::Status;

                    if (!hadStage) {
                        command::finishDeferred(session, "Undo new xform failed", {}, Status::Error);
                        return;
                    }

                    if (!restored) {
                        command::finishDeferred(session,
                                                error.isEmpty() ? "Undo new xform failed"
                                                                : QString("Undo new xform failed: %1").arg(error),
                                                changed, Status::Error);
                        return;
                    }

                    command::finishDeferred(session, "New xform undone", changed, Status::Success);

                    session->selectionList()->updatePaths(state->previousSelection);
                    session->setMask(state->previousMask);
                });
            });
        });
}

Command
movePath(const QList<SdfPath>& paths, const SdfPath& newParentPath, int insertIndex)
{
    struct MoveItem {
        SdfPath oldPath;
        SdfPath newPath;
        SdfPath oldParentPath;
        TfToken name;
    };

    struct MoveState {
        QList<MoveItem> items;
        SdfPath newParentPath;
        int insertIndex = -1;
        QHash<SdfPath, TfTokenVector> oldParentOrders;
        QHash<SdfPath, TfTokenVector> newParentOrders;
        QList<SdfPath> previousSelection;
        QList<SdfPath> previousMask;
    };

    auto state = std::make_shared<MoveState>();

    return Command(
        [paths, newParentPath, insertIndex, state](Session* session) {
            if (!session || paths.isEmpty() || newParentPath.IsEmpty())
                return;

            state->previousSelection = session->selectionList()->paths();
            state->previousMask = session->mask();

            command::beginDeferred(session, "Move paths", 1);

            command::runWorker([=]() {
                bool hadStage = true;
                bool moved = false;
                bool noop = false;
                QString error;
                QList<SdfPath> changed;

                auto removeTokens = [](TfTokenVector order, const TfTokenVector& tokens) {
                    for (const TfToken& token : tokens)
                        order.erase(std::remove(order.begin(), order.end(), token), order.end());
                    return order;
                };

                auto insertTokens = [](TfTokenVector order, const TfTokenVector& tokens, int index) {
                    if (tokens.empty())
                        return order;

                    const int safeIndex = qBound(0, index, static_cast<int>(order.size()));
                    order.insert(order.begin() + safeIndex, tokens.begin(), tokens.end());
                    return order;
                };

                auto restoreOrders = [](const UsdStageRefPtr& stage, const QHash<SdfPath, TfTokenVector>& orders) {
                    for (auto it = orders.cbegin(); it != orders.cend(); ++it) {
                        if (it.key() == SdfPath::AbsoluteRootPath())
                            continue;

                        stage::restoreChildOrder(stage, it.key(), it.value());
                    }
                };

                {
                    WRITE_LOCKER(locker, session->stageLock(), "stageLock");
                    const UsdStageRefPtr stage = session->stageUnsafe();

                    if (!stage) {
                        hadStage = false;
                    }
                    else {
                        const QList<SdfPath> movePaths = path::minimalRootPaths(paths);

                        if (movePaths.isEmpty()) {
                            noop = true;
                        }
                        else if (newParentPath != SdfPath::AbsoluteRootPath()
                                 && stage::isInsideCompositionArc(stage, newParentPath)) {
                            error = "cannot move into or out of composed prims";
                        }
                        else {
                            QString rootError;
                            const SdfLayerHandle rootLayer = openedRootLayer(stage, rootError);
                            if (!rootLayer) {
                                error = rootError;
                            }
                            else if (!isRootLayerParent(stage, newParentPath, error)) {}
                            else {
                                state->items.clear();
                                state->newParentPath = newParentPath;
                                state->insertIndex = insertIndex;
                                state->oldParentOrders.clear();
                                state->newParentOrders.clear();

                                QSet<SdfPath> sourcePaths;
                                QSet<SdfPath> targetPaths;
                                bool valid = true;

                                for (const SdfPath& path : movePaths) {
                                    const SdfPath oldParentPath = path.GetParentPath();
                                    const SdfPath targetPath = newParentPath.AppendChild(path.GetNameToken());

                                    sourcePaths.insert(path);

                                    if (path.IsEmpty() || path == SdfPath::AbsoluteRootPath()
                                        || oldParentPath.IsEmpty()) {
                                        error = "invalid source path";
                                        valid = false;
                                        break;
                                    }

                                    if (newParentPath == path || newParentPath.HasPrefix(path)) {
                                        error = "cannot move a prim below itself";
                                        valid = false;
                                        break;
                                    }

                                    if (stage::isInsideCompositionArc(stage, path)) {
                                        error = "cannot move into or out of composed prims";
                                        valid = false;
                                        break;
                                    }

                                    QString authoredError;
                                    if (!isRootLayerAuthoredPrim(stage, path, authoredError)) {
                                        error = authoredError;
                                        valid = false;
                                        break;
                                    }

                                    if (targetPaths.contains(targetPath)) {
                                        error = "multiple moved prims resolve to the same destination";
                                        valid = false;
                                        break;
                                    }

                                    const UsdPrim targetPrim = stage->GetPrimAtPath(targetPath);
                                    if (targetPrim && targetPath != path && !sourcePaths.contains(targetPath)) {
                                        error = QString("destination already exists: %1")
                                                    .arg(qt::SdfPathToQString(targetPath));
                                        valid = false;
                                        break;
                                    }

                                    targetPaths.insert(targetPath);

                                    MoveItem item;
                                    item.oldPath = path;
                                    item.newPath = targetPath;
                                    item.oldParentPath = oldParentPath;
                                    item.name = path.GetNameToken();
                                    state->items.append(item);

                                    if (!state->oldParentOrders.contains(oldParentPath)
                                        && oldParentPath != SdfPath::AbsoluteRootPath()) {
                                        TfTokenVector order;
                                        stage::captureChildOrder(stage, oldParentPath, order);
                                        state->oldParentOrders.insert(oldParentPath, order);
                                    }

                                    if (!state->oldParentOrders.contains(newParentPath)
                                        && newParentPath != SdfPath::AbsoluteRootPath()) {
                                        TfTokenVector order;
                                        stage::captureChildOrder(stage, newParentPath, order);
                                        state->oldParentOrders.insert(newParentPath, order);
                                    }
                                }

                                if (valid && !noop) {
                                    TfTokenVector movedNames;
                                    movedNames.reserve(state->items.size());

                                    for (const MoveItem& item : state->items)
                                        movedNames.push_back(item.name);

                                    UsdEditContext context(stage, UsdEditTarget(rootLayer));

                                    for (const MoveItem& item : state->items) {
                                        if (item.oldPath == item.newPath)
                                            continue;

                                        QString moveError;
                                        if (!stage::movePrim(stage, item.oldPath, newParentPath, moveError)) {
                                            error = QString("failed to move %1 to %2")
                                                        .arg(qt::SdfPathToQString(item.oldPath),
                                                             qt::SdfPathToQString(newParentPath));

                                            if (!moveError.isEmpty())
                                                error += QString(": %1").arg(moveError);

                                            for (auto it = state->items.crbegin(); it != state->items.crend(); ++it) {
                                                if (it->oldPath != it->newPath) {
                                                    QString rollbackError;
                                                    stage::movePrim(stage, it->newPath, it->oldParentPath,
                                                                    rollbackError);
                                                }
                                            }

                                            restoreOrders(stage, state->oldParentOrders);
                                            moved = false;
                                            break;
                                        }
                                    }

                                    if (error.isEmpty()) {
                                        QSet<SdfPath> affectedParents;

                                        for (const MoveItem& item : state->items)
                                            affectedParents.insert(item.oldParentPath);

                                        affectedParents.insert(newParentPath);

                                        for (const SdfPath& parentPath : affectedParents) {
                                            if (parentPath == SdfPath::AbsoluteRootPath())
                                                continue;

                                            TfTokenVector order = state->oldParentOrders.value(parentPath);
                                            TfTokenVector newOrder = removeTokens(order, movedNames);

                                            if (parentPath == newParentPath)
                                                newOrder = insertTokens(newOrder, movedNames, insertIndex);

                                            state->newParentOrders.insert(parentPath, newOrder);
                                            stage::restoreChildOrder(stage, parentPath, newOrder);
                                        }

                                        moved = state->newParentOrders != state->oldParentOrders;

                                        for (const MoveItem& item : state->items) {
                                            if (item.oldPath != item.newPath) {
                                                moved = true;
                                                break;
                                            }
                                        }

                                        if (!moved) {
                                            noop = true;
                                        }
                                        else {
                                            QSet<SdfPath> changedSet;

                                            for (const MoveItem& item : state->items) {
                                                changedSet.insert(item.oldPath);
                                                changedSet.insert(item.newPath);
                                                changedSet.insert(item.oldParentPath);
                                            }

                                            changedSet.insert(newParentPath);
                                            changed = changedSet.values();
                                        }
                                    }
                                }
                            }
                        }
                    }
                }

                command::queueToSession(session, [=]() {
                    using Status = Session::Notify::Status;

                    if (!hadStage) {
                        command::finishDeferred(session, "Move paths failed", {}, Status::Error);
                        return;
                    }

                    if (noop) {
                        command::finishDeferred(session, "Move skipped", {}, Status::Success);
                        return;
                    }

                    if (!moved) {
                        command::finishDeferred(session,
                                                error.isEmpty() ? "Move paths failed"
                                                                : QString("Move paths failed: %1").arg(error),
                                                changed, Status::Error);
                        return;
                    }

                    command::finishDeferred(session, state->items.size() == 1 ? "Path moved" : "Paths moved", changed,
                                            Status::Success);

                    QList<SdfPath> selection = state->previousSelection;
                    QList<SdfPath> mask = state->previousMask;

                    for (const MoveItem& item : state->items) {
                        selection = path::remapAffectedPaths(selection, item.oldPath, item.newPath);
                        mask = path::remapAffectedPaths(mask, item.oldPath, item.newPath);
                    }

                    session->selectionList()->updatePaths(selection);
                    session->setMask(mask);
                });
            });
        },
        [state](Session* session) {
            if (!session || state->items.isEmpty())
                return;

            command::beginDeferred(session, "Undo move paths", 1);

            command::runWorker([=]() {
                bool hadStage = true;
                bool restored = false;
                QString error;
                QList<SdfPath> changed;

                {
                    WRITE_LOCKER(locker, session->stageLock(), "stageLock");
                    const UsdStageRefPtr stage = session->stageUnsafe();

                    if (!stage) {
                        hadStage = false;
                    }
                    else {
                        QString rootError;
                        const SdfLayerHandle rootLayer = openedRootLayer(stage, rootError);
                        if (!rootLayer) {
                            error = rootError;
                        }
                        else {
                            UsdEditContext context(stage, UsdEditTarget(rootLayer));
                            restored = true;

                            for (auto it = state->items.crbegin(); it != state->items.crend(); ++it) {
                                if (it->oldPath == it->newPath)
                                    continue;

                                QString moveError;
                                if (!stage::movePrim(stage, it->newPath, it->oldParentPath, moveError)) {
                                    error = QString("failed to restore %1 to %2")
                                                .arg(qt::SdfPathToQString(it->newPath),
                                                     qt::SdfPathToQString(it->oldParentPath));

                                    if (!moveError.isEmpty())
                                        error += QString(": %1").arg(moveError);

                                    restored = false;
                                    break;
                                }
                            }

                            if (restored) {
                                for (auto it = state->oldParentOrders.cbegin(); it != state->oldParentOrders.cend();
                                     ++it) {
                                    if (it.key() == SdfPath::AbsoluteRootPath())
                                        continue;

                                    stage::restoreChildOrder(stage, it.key(), it.value());
                                }

                                QSet<SdfPath> changedSet;

                                for (const MoveItem& item : state->items) {
                                    changedSet.insert(item.oldPath);
                                    changedSet.insert(item.newPath);
                                    changedSet.insert(item.oldParentPath);
                                }

                                changedSet.insert(state->newParentPath);
                                changed = changedSet.values();
                            }
                        }
                    }
                }

                command::queueToSession(session, [=]() {
                    using Status = Session::Notify::Status;

                    if (!hadStage) {
                        command::finishDeferred(session, "Undo move paths failed", {}, Status::Error);
                        return;
                    }

                    if (!restored) {
                        command::finishDeferred(session,
                                                error.isEmpty() ? "Undo move paths failed"
                                                                : QString("Undo move paths failed: %1").arg(error),
                                                changed, Status::Error);
                        return;
                    }

                    command::finishDeferred(session, state->items.size() == 1 ? "Move undone" : "Moves undone", changed,
                                            Status::Success);

                    session->selectionList()->updatePaths(state->previousSelection);
                    session->setMask(state->previousMask);
                });
            });
        });
}

}  // namespace stageviz
