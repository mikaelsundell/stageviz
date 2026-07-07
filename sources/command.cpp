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
#include <QThreadPool>
#include <pxr/usd/usd/prim.h>
#include <pxr/usd/usd/primRange.h>
#include <pxr/usd/usdGeom/imageable.h>
#include <pxr/usd/usdGeom/tokens.h>
#include <pxr/usd/usdGeom/xform.h>
#include <algorithm>

namespace stageviz {

namespace payload {
    struct State {
        QList<PayloadState> payloadStates;
        QList<SdfPath> previousSelection;
        QList<SdfPath> previousMask;
    };
}  // namespace payload

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

            QThreadPool::globalInstance()->start([session, paths, variantSet, variantValue, state]() {
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
                            result.success = payload::applyLoad(
                                stage,
                                path,
                                useVariant,
                                variantSetName,
                                variantSelection,
                                payloadState,
                                error);

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

                    result.message = result.success
                                         ? "Payload loaded"
                                         : (error.isEmpty() ? "Payload failed" : error);

                    result.status = result.success
                                        ? Session::Notify::Status::Success
                                        : Session::Notify::Status::Error;

                    if (result.success)
                        payloadStates.append(payloadState);

                    pending.append(result);
                    ++completed;

                    if (pending.size() >= 16) {
                        const QList<command::Result> batch = pending;
                        QMetaObject::invokeMethod(
                            session,
                            [session, batch, completed]() {
                                command::flushResults(session, batch, completed);
                            },
                            Qt::QueuedConnection);
                        pending.clear();
                    }
                }

                if (!pending.isEmpty()) {
                    const QList<command::Result> batch = pending;
                    QMetaObject::invokeMethod(
                        session,
                        [session, batch, completed]() {
                            command::flushResults(session, batch, completed);
                        },
                        Qt::QueuedConnection);
                }

                state->payloadStates = payloadStates;

                QMetaObject::invokeMethod(
                    session,
                    [session]() {
                        session->setPrimsUpdate(Session::PrimsUpdate::Immediate);
                        session->endProgressBlock();
                    },
                    Qt::QueuedConnection);
            });
        },
        [state](Session* session) {
            if (!session || state->payloadStates.isEmpty())
                return;

            session->beginProgressBlock("Undo load payloads", state->payloadStates.size());
            session->setPrimsUpdate(Session::PrimsUpdate::Deferred);

            QThreadPool::globalInstance()->start([session, state]() {
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

                    result.message = result.success
                                         ? "Payload undone"
                                         : (error.isEmpty() ? "Payload undo failed" : error);

                    result.status = result.success
                                        ? Session::Notify::Status::Success
                                        : Session::Notify::Status::Error;

                    pending.append(result);
                    ++completed;

                    if (pending.size() >= 16) {
                        const QList<command::Result> batch = pending;
                        QMetaObject::invokeMethod(
                            session,
                            [session, batch, completed]() {
                                command::flushResults(session, batch, completed);
                            },
                            Qt::QueuedConnection);
                        pending.clear();
                    }
                }

                QMetaObject::invokeMethod(
                    session,
                    [session, state, pending, completed]() {
                        if (!pending.isEmpty())
                            command::flushResults(session, pending, completed);

                        session->selectionList()->updatePaths(state->previousSelection);
                        session->setMask(state->previousMask);
                        session->setPrimsUpdate(Session::PrimsUpdate::Immediate);
                        session->endProgressBlock();
                    },
                    Qt::QueuedConnection);
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

            QThreadPool::globalInstance()->start([session, paths, state]() {
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
                        QMetaObject::invokeMethod(
                            session,
                            [session, batch, completed]() { command::flushResults(session, batch, completed); },
                            Qt::QueuedConnection);
                        pending.clear();
                    }
                }

                if (!pending.isEmpty()) {
                    const QList<command::Result> batch = pending;
                    QMetaObject::invokeMethod(
                        session, [session, batch, completed]() { command::flushResults(session, batch, completed); },
                        Qt::QueuedConnection);
                }

                state->payloadStates = payloadStates;

                QMetaObject::invokeMethod(
                    session,
                    [session, state, unloadedPaths]() {
                        session->selectionList()->updatePaths(
                            path::removeAffectedPaths(state->previousSelection, unloadedPaths));
                        session->setMask(path::removeAffectedPaths(state->previousMask, unloadedPaths));
                        session->setPrimsUpdate(Session::PrimsUpdate::Immediate);
                        session->endProgressBlock();
                    },
                    Qt::QueuedConnection);
            });
        },
        [state](Session* session) {
            if (!session || state->payloadStates.isEmpty())
                return;

            session->beginProgressBlock("Undo unload payloads", state->payloadStates.size());
            session->setPrimsUpdate(Session::PrimsUpdate::Deferred);

            QThreadPool::globalInstance()->start([session, state]() {
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
                        QMetaObject::invokeMethod(
                            session,
                            [session, batch, completed]() { command::flushResults(session, batch, completed); },
                            Qt::QueuedConnection);
                        pending.clear();
                    }
                }

                QMetaObject::invokeMethod(
                    session,
                    [session, state, pending, completed]() {
                        if (!pending.isEmpty())
                            command::flushResults(session, pending, completed);

                        session->selectionList()->updatePaths(state->previousSelection);
                        session->setMask(state->previousMask);
                        session->setPrimsUpdate(Session::PrimsUpdate::Immediate);
                        session->endProgressBlock();
                    },
                    Qt::QueuedConnection);
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

            session->beginProgressBlock("invert payload selection", 1);

            QThreadPool::globalInstance()->start([session, state]() {
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
                            result.message = selectedSet.contains(path) ? "payload skipped" : "payload inverted";
                            result.status = Status::Success;
                            pending.append(result);
                            ++completed;

                            if (pending.size() >= 16) {
                                const QList<command::Result> batch = pending;
                                QMetaObject::invokeMethod(
                                    session,
                                    [session, batch, completed]() { command::flushResults(session, batch, completed); },
                                    Qt::QueuedConnection);
                                pending.clear();
                            }

                            if (session->isProgressBlockCancelled())
                                break;
                        }
                    }
                }

                if (!pending.isEmpty()) {
                    const QList<command::Result> batch = pending;
                    QMetaObject::invokeMethod(
                        session, [session, batch, completed]() { command::flushResults(session, batch, completed); },
                        Qt::QueuedConnection);
                }

                QMetaObject::invokeMethod(
                    session,
                    [session, hadStage, hadSelectedPayloads, invertedPayloads, total]() {
                        using Status = Session::Notify::Status;

                        if (!hadStage) {
                            session->updateProgressNotify(Session::Notify("invert payload selection failed", {},
                                                                          Status::Error),
                                                          1);
                            session->endProgressBlock();
                            return;
                        }

                        if (!hadSelectedPayloads) {
                            session->updateProgressNotify(Session::Notify("invert payload selection skipped", {},
                                                                          Status::Success),
                                                          1);
                            session->endProgressBlock();
                            return;
                        }

                        session->selectionList()->updatePaths(invertedPayloads);

                        session->updateProgressNotify(Session::Notify(total > 0 ? "payload selection inverted"
                                                                                : "invert payload selection skipped",
                                                                      invertedPayloads, Status::Success),
                                                      qMax(1, total));

                        session->endProgressBlock();
                    },
                    Qt::QueuedConnection);
            });
        },
        [state](Session* session) {
            if (!session)
                return;

            session->beginProgressBlock("undo invert payload selection", 1);

            QMetaObject::invokeMethod(
                session,
                [session, state]() {
                    using Status = Session::Notify::Status;
                    session->selectionList()->updatePaths(state->previousSelection);
                    session->updateProgressNotify(Session::Notify("invert payload selection undone",
                                                                  state->previousSelection, Status::Success),
                                                  1);
                    session->endProgressBlock();
                },
                Qt::QueuedConnection);
        });
}

Command
isolatePaths(const QList<SdfPath>& paths)
{
    auto state = std::make_shared<QList<SdfPath>>();

    return Command(
        [paths, state](Session* session) {
            session->beginProgressBlock("isolate paths", 1);

            QThreadPool::globalInstance()->start([session, paths, state]() {
                *state = session->mask();
                session->setMask(paths);

                QMetaObject::invokeMethod(
                    session,
                    [session, paths]() {
                        using Status = Session::Notify::Status;
                        session->updateProgressNotify(Session::Notify("paths isolated", paths, Status::Success), 1);
                        session->endProgressBlock();
                    },
                    Qt::QueuedConnection);
            });
        },
        [state](Session* session) {
            session->beginProgressBlock("undo isolate paths", 1);

            QThreadPool::globalInstance()->start([session, state]() {
                session->setMask(*state);

                QMetaObject::invokeMethod(
                    session,
                    [session, state]() {
                        using Status = Session::Notify::Status;
                        session->updateProgressNotify(Session::Notify("isolate undone", *state, Status::Success), 1);
                        session->endProgressBlock();
                    },
                    Qt::QueuedConnection);
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

            QThreadPool::globalInstance()->start([session, paths, previous]() {
                *previous = session->selectionList()->paths();
                session->selectionList()->updatePaths(paths);

                QMetaObject::invokeMethod(
                    session,
                    [session, paths]() {
                        using Status = Session::Notify::Status;
                        session->updateProgressNotify(Session::Notify("Paths selected", paths, Status::Success), 1);
                        session->endProgressBlock();
                    },
                    Qt::QueuedConnection);
            });
        },
        [previous](Session* session) {
            session->beginProgressBlock("Undo select paths", 1);

            QThreadPool::globalInstance()->start([session, previous]() {
                session->selectionList()->updatePaths(*previous);

                QMetaObject::invokeMethod(
                    session,
                    [session, previous]() {
                        using Status = Session::Notify::Status;
                        session->updateProgressNotify(Session::Notify("Select undone", *previous, Status::Success), 1);
                        session->endProgressBlock();
                    },
                    Qt::QueuedConnection);
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

            QThreadPool::globalInstance()->start([session, state]() {
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

                QMetaObject::invokeMethod(
                    session,
                    [session, selection]() {
                        using Status = Session::Notify::Status;
                        session->selectionList()->updatePaths(selection);
                        session->updateProgressNotify(Session::Notify("Paths selected", selection, Status::Success),
                                                      1);
                        session->endProgressBlock();
                    },
                    Qt::QueuedConnection);
            });
        },
        [state](Session* session) {
            if (!session)
                return;

            session->beginProgressBlock("Undo select all", 1);

            QThreadPool::globalInstance()->start([session, state]() {
                QMetaObject::invokeMethod(
                    session,
                    [session, state]() {
                        using Status = Session::Notify::Status;
                        session->selectionList()->updatePaths(state->previousSelection);
                        session->updateProgressNotify(Session::Notify("Select all undone",
                                                                      state->previousSelection,
                                                                      Status::Success),
                                                      1);
                        session->endProgressBlock();
                    },
                    Qt::QueuedConnection);
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

            QThreadPool::globalInstance()->start([session, state]() {
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

                QMetaObject::invokeMethod(
                    session,
                    [session, invertedSelection]() {
                        using Status = Session::Notify::Status;
                        session->selectionList()->updatePaths(invertedSelection);
                        session->updateProgressNotify(Session::Notify("Selection inverted",
                                                                      invertedSelection,
                                                                      Status::Success),
                                                      1);
                        session->endProgressBlock();
                    },
                    Qt::QueuedConnection);
            });
        },
        [state](Session* session) {
            if (!session)
                return;

            session->beginProgressBlock("Undo invert selection", 1);

            QThreadPool::globalInstance()->start([session, state]() {
                QMetaObject::invokeMethod(
                    session,
                    [session, state]() {
                        using Status = Session::Notify::Status;
                        session->selectionList()->updatePaths(state->previousSelection);
                        session->updateProgressNotify(Session::Notify("Invert selection undone",
                                                                      state->previousSelection,
                                                                      Status::Success),
                                                      1);
                        session->endProgressBlock();
                    },
                    Qt::QueuedConnection);
            });
        });
}

Command
showPaths(const QList<SdfPath>& paths, bool recursive)
{
    auto state = std::make_shared<QHash<SdfPath, bool>>();

    return Command(
        [paths, recursive, state](Session* session) {
            session->beginProgressBlock("Show paths", 1);
            session->setPrimsUpdate(Session::PrimsUpdate::Deferred);

            QThreadPool::globalInstance()->start([session, paths, recursive, state]() {
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

                QMetaObject::invokeMethod(
                    session,
                    [session, paths, success]() {
                        using Status = Session::Notify::Status;
                        session->setPrimsUpdate(Session::PrimsUpdate::Immediate);
                        session->updateProgressNotify(Session::Notify(success ? "Paths shown" : "Show paths failed",
                                                                      paths, success ? Status::Success : Status::Error),
                                                      1);
                        session->endProgressBlock();
                    },
                    Qt::QueuedConnection);
            });
        },
        [state, recursive](Session* session) {
            session->beginProgressBlock("Undo show paths", 1);
            session->setPrimsUpdate(Session::PrimsUpdate::Deferred);

            QThreadPool::globalInstance()->start([session, state, recursive]() {
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

                QMetaObject::invokeMethod(
                    session,
                    [session, restoredPaths, success]() {
                        using Status = Session::Notify::Status;
                        session->setPrimsUpdate(Session::PrimsUpdate::Immediate);
                        session->updateProgressNotify(Session::Notify(success ? "Show undone" : "Undo show paths failed",
                                                                      restoredPaths,
                                                                      success ? Status::Success : Status::Error),
                                                      1);
                        session->endProgressBlock();
                    },
                    Qt::QueuedConnection);
            });
        });
}

Command
hidePaths(const QList<SdfPath>& paths, bool recursive)
{
    auto state = std::make_shared<QHash<SdfPath, bool>>();

    return Command(
        [paths, recursive, state](Session* session) {
            session->beginProgressBlock("Hide paths", 1);
            session->setPrimsUpdate(Session::PrimsUpdate::Deferred);

            QThreadPool::globalInstance()->start([session, paths, recursive, state]() {
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

                QMetaObject::invokeMethod(
                    session,
                    [session, paths, success]() {
                        using Status = Session::Notify::Status;
                        session->setPrimsUpdate(Session::PrimsUpdate::Immediate);
                        session->updateProgressNotify(Session::Notify(success ? "Paths hidden" : "Hide paths failed",
                                                                      paths, success ? Status::Success : Status::Error),
                                                      1);
                        session->endProgressBlock();
                    },
                    Qt::QueuedConnection);
            });
        },
        [state, recursive](Session* session) {
            session->beginProgressBlock("Undo hide paths", 1);
            session->setPrimsUpdate(Session::PrimsUpdate::Deferred);

            QThreadPool::globalInstance()->start([session, state, recursive]() {
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

                QMetaObject::invokeMethod(
                    session,
                    [session, restoredPaths, success]() {
                        using Status = Session::Notify::Status;
                        session->setPrimsUpdate(Session::PrimsUpdate::Immediate);
                        session->updateProgressNotify(Session::Notify(success ? "Hide undone" : "Undo hide paths failed",
                                                                      restoredPaths,
                                                                      success ? Status::Success : Status::Error),
                                                      1);
                        session->endProgressBlock();
                    },
                    Qt::QueuedConnection);
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

            QThreadPool::globalInstance()->start([session, stageUp, state]() {
                *state = session->stageUp();
                session->setStageUp(stageUp);

                QMetaObject::invokeMethod(
                    session,
                    [session, stageUp]() {
                        using Status = Session::Notify::Status;
                        const QString axis = (stageUp == Session::StageUp::Z) ? "Z" : "Y";
                        session->updateProgressNotify(Session::Notify(QString("Stage up set to %1").arg(axis), {},
                                                                      Status::Success),
                                                      1);
                        session->endProgressBlock();
                    },
                    Qt::QueuedConnection);
            });
        },
        [state](Session* session) {
            session->beginProgressBlock("Undo set stage up", 1);

            QThreadPool::globalInstance()->start([session, state]() {
                session->setStageUp(*state);

                QMetaObject::invokeMethod(
                    session,
                    [session, state]() {
                        using Status = Session::Notify::Status;
                        const QString axis = (*state == Session::StageUp::Z) ? "Z" : "Y";
                        session->updateProgressNotify(Session::Notify(QString("Set stage up undone to %1").arg(axis),
                                                                      {}, Status::Success),
                                                      1);
                        session->endProgressBlock();
                    },
                    Qt::QueuedConnection);
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

            QThreadPool::globalInstance()->start([=]() {
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

                        const UsdPrim prim = stage->GetPrimAtPath(path);
                        if (!prim || !prim.IsValid()) {
                            error = "invalid prim";
                        }
                        else if (path.GetParentPath() != SdfPath::AbsoluteRootPath()) {
                            error = "default prim must be a root prim";
                        }
                        else {
                            stage->SetDefaultPrim(prim);
                            state->newDefaultPrimPath = path;
                            success = true;
                        }
                    }
                }

                QMetaObject::invokeMethod(
                    session,
                    [=]() {
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

                        session->updateProgressNotify(Session::Notify("Default prim set", { path }, Status::Success),
                                                      1);
                        session->endProgressBlock();
                    },
                    Qt::QueuedConnection);
            });
        },
        [state](Session* session) {
            if (!session)
                return;

            session->beginProgressBlock("Undo set default prim", 1);

            QThreadPool::globalInstance()->start([=]() {
                bool hadStage = true;
                bool success = false;
                QString error;

                {
                    WRITE_LOCKER(locker, session->stageLock(), "stageLock");
                    const UsdStageRefPtr stage = session->stageUnsafe();

                    if (!stage) {
                        hadStage = false;
                    }
                    else if (state->previousDefaultPrimPath.IsEmpty()) {
                        stage->ClearDefaultPrim();
                        success = true;
                    }
                    else {
                        const UsdPrim prim = stage->GetPrimAtPath(state->previousDefaultPrimPath);
                        if (!prim || !prim.IsValid()) {
                            error = "previous default prim missing";
                        }
                        else {
                            stage->SetDefaultPrim(prim);
                            success = true;
                        }
                    }
                }

                QMetaObject::invokeMethod(
                    session,
                    [=]() {
                        using Status = Session::Notify::Status;

                        if (!hadStage) {
                            session->updateProgressNotify(Session::Notify("Undo set default prim failed", {},
                                                                          Status::Error),
                                                          1);
                            session->endProgressBlock();
                            return;
                        }

                        if (!success) {
                            session->updateProgressNotify(
                                Session::Notify(error.isEmpty()
                                                    ? "Undo set default prim failed"
                                                    : QString("Undo set default prim failed: %1").arg(error),
                                                {}, Status::Error),
                                1);
                            session->endProgressBlock();
                            return;
                        }

                        session->updateProgressNotify(Session::Notify("Default prim undone",
                                                                      { state->previousDefaultPrimPath },
                                                                      Status::Success),
                                                      1);
                        session->endProgressBlock();
                    },
                    Qt::QueuedConnection);
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

            session->beginProgressBlock("Delete paths", 1);
            session->setPrimsUpdate(Session::PrimsUpdate::Deferred);

            QThreadPool::globalInstance()->start([session, inPaths, state]() {
                bool success = false;
                QList<SdfPath> changed;
                QList<SdfPath> removedPaths;

                {
                    WRITE_LOCKER(locker, session->stageLock(), "stageLock");

                    const UsdStageRefPtr stage = session->stageUnsafe();
                    if (stage) {
                        const QList<SdfPath> editable = stage::filterStrongestEditablePaths(stage, inPaths);
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
                        const SdfLayerHandle editLayer = stage->GetEditTarget().GetLayer();
                        if (editLayer) {
                            if (deletesDefaultPrim)
                                stage->ClearDefaultPrim();

                            for (const SdfPath& path : paths) {
                                snapshot::PrimState primState;
                                if (!snapshot::capturePrimToLayer(stage, path, primState))
                                    continue;

                                if (!stage::removePrimSpec(editLayer, primState.specPath))
                                    continue;

                                state->prims.append(primState);
                                removedPaths.append(path);
                                removedAny = true;
                            }
                        }

                        if (removedAny) {
                            changed = changedSet.values();
                            success = true;
                        }
                    }
                }

                QMetaObject::invokeMethod(
                    session,
                    [session, state, changed, removedPaths, success]() {
                        using Status = Session::Notify::Status;

                        session->selectionList()->updatePaths(
                            path::removeAffectedPaths(state->previousSelection, removedPaths));
                        session->setMask(path::removeAffectedPaths(state->previousMask, removedPaths));
                        session->setPrimsUpdate(Session::PrimsUpdate::Immediate);
                        session->updateProgressNotify(Session::Notify(success ? "Paths deleted" : "Delete paths failed",
                                                                      changed, success ? Status::Success : Status::Error),
                                                      1);
                        session->endProgressBlock();
                    },
                    Qt::QueuedConnection);
            });
        },
        [state](Session* session) {
            session->beginProgressBlock("Undo delete paths", 1);
            session->setPrimsUpdate(Session::PrimsUpdate::Deferred);

            QThreadPool::globalInstance()->start([session, state]() {
                bool success = false;
                QList<SdfPath> changed;

                {
                    WRITE_LOCKER(locker, session->stageLock(), "stageLock");

                    const UsdStageRefPtr stage = session->stageUnsafe();
                    if (stage) {
                        const SdfLayerHandle editLayer = stage->GetEditTarget().GetLayer();
                        if (editLayer) {
                            snapshot::sortByHierarchy(state->prims);

                            QSet<SdfPath> changedSet;

                            for (const auto& primState : state->prims) {
                                snapshot::restorePrimFromSnapshotLayer(editLayer, primState);
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
                }

                QMetaObject::invokeMethod(
                    session,
                    [session, state, changed, success]() {
                        using Status = Session::Notify::Status;

                        session->selectionList()->updatePaths(state->previousSelection);
                        session->setMask(state->previousMask);
                        session->setPrimsUpdate(Session::PrimsUpdate::Immediate);
                        session->updateProgressNotify(Session::Notify(success ? "Delete undone"
                                                                              : "Undo delete paths failed",
                                                                      changed, success ? Status::Success : Status::Error),
                                                      1);
                        session->endProgressBlock();
                    },
                    Qt::QueuedConnection);
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

            session->beginProgressBlock("Rename path", 1);
            session->setPrimsUpdate(Session::PrimsUpdate::Deferred);

            QThreadPool::globalInstance()->start([=]() {
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
                        newPath = stage::buildRenamePath(stage, path, newNameInput, error);

                        if (newPath.IsEmpty()) {
                        }
                        else if (newPath == path) {
                            noop = true;
                        }
                        else {
                            state->oldPath = path;
                            state->newPath = newPath;
                            state->parentPath = path.GetParentPath();
                            state->oldOrder.clear();
                            state->newOrder.clear();

                            if (!state->parentPath.IsEmpty()
                                && state->parentPath != SdfPath::AbsoluteRootPath()) {
                                stage::captureChildOrder(stage, state->parentPath, state->oldOrder);
                            }

                            const UsdStageLoadRules rules = stage->GetLoadRules();

                            if (stage::renamePrim(stage, path, newPath, error)) {
                                stage->SetLoadRules(stage::remapLoadRules(rules, path, newPath));

                                if (!state->oldOrder.empty()) {
                                    state->newOrder = stage::remapChildOrder(
                                        state->oldOrder,
                                        path.GetNameToken(),
                                        newPath.GetNameToken());

                                    stage::restoreChildOrder(stage, state->parentPath, state->newOrder);
                                }

                                renamed = true;
                            }
                        }
                    }
                }

                QMetaObject::invokeMethod(
                    session,
                    [=]() {
                        using Status = Session::Notify::Status;

                        session->setPrimsUpdate(Session::PrimsUpdate::Immediate);

                        if (!hadStage) {
                            session->updateProgressNotify(
                                Session::Notify("Rename path failed", { path }, Status::Error),
                                1);
                            session->endProgressBlock();
                            return;
                        }

                        if (noop) {
                            session->updateProgressNotify(
                                Session::Notify("Rename skipped", { path }, Status::Success),
                                1);
                            session->endProgressBlock();
                            return;
                        }

                        if (!renamed) {
                            session->updateProgressNotify(
                                Session::Notify(
                                    error.isEmpty()
                                        ? "Rename path failed"
                                        : QString("Rename path failed: %1").arg(error),
                                    { path },
                                    Status::Error),
                                1);
                            session->endProgressBlock();
                            return;
                        }

                        session->selectionList()->updatePaths(
                            path::remapAffectedPaths(state->previousSelection, path, newPath));
                        session->setMask(path::remapAffectedPaths(state->previousMask, path, newPath));
                        session->updateProgressNotify(
                            Session::Notify("Path renamed", { path, newPath }, Status::Success),
                            1);
                        session->endProgressBlock();
                    },
                    Qt::QueuedConnection);
            });
        },
        [state](Session* session) {
            if (!session || state->oldPath.IsEmpty() || state->newPath.IsEmpty())
                return;

            session->beginProgressBlock("Undo rename path", 1);
            session->setPrimsUpdate(Session::PrimsUpdate::Deferred);

            QThreadPool::globalInstance()->start([=]() {
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
                        const UsdStageLoadRules rules = stage->GetLoadRules();

                        if (stage::renamePrim(stage, state->newPath, state->oldPath, error)) {
                            stage->SetLoadRules(
                                stage::remapLoadRules(rules, state->newPath, state->oldPath));

                            if (!state->oldOrder.empty()
                                && !state->parentPath.IsEmpty()
                                && state->parentPath != SdfPath::AbsoluteRootPath()) {
                                stage::restoreChildOrder(stage, state->parentPath, state->oldOrder);
                            }

                            restored = true;
                        }
                    }
                }

                QMetaObject::invokeMethod(
                    session,
                    [=]() {
                        using Status = Session::Notify::Status;

                        session->selectionList()->updatePaths(state->previousSelection);
                        session->setMask(state->previousMask);
                        session->setPrimsUpdate(Session::PrimsUpdate::Immediate);

                        if (!hadStage) {
                            session->updateProgressNotify(
                                Session::Notify("Undo rename path failed", {}, Status::Error),
                                1);
                            session->endProgressBlock();
                            return;
                        }

                        if (!restored) {
                            session->updateProgressNotify(
                                Session::Notify(
                                    error.isEmpty()
                                        ? "Undo rename path failed"
                                        : QString("Undo rename path failed: %1").arg(error),
                                    {},
                                    Status::Error),
                                1);
                            session->endProgressBlock();
                            return;
                        }

                        session->updateProgressNotify(
                            Session::Notify("Rename undone", { state->oldPath }, Status::Success),
                            1);
                        session->endProgressBlock();
                    },
                    Qt::QueuedConnection);
            });
        });
}

Command
newXformPath(const SdfPath& parentPath, const QString& nameInput)
{
    struct NewXformState {
        SdfPath parentPath;
        SdfPath createdPath;
        TfTokenVector oldOrder;
        TfTokenVector newOrder;
        QList<SdfPath> previousMask;
    };

    auto state = std::make_shared<NewXformState>();

    return Command(
        [parentPath, nameInput, state](Session* session) {
            if (!session || parentPath.IsEmpty())
                return;

            state->previousMask = session->mask();

            session->beginProgressBlock("New xform", 1);
            session->setPrimsUpdate(Session::PrimsUpdate::Deferred);

            QThreadPool::globalInstance()->start([=]() {
                bool hadStage = true;
                bool created = false;
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
                        newPath = stage::buildChildPath(stage, parentPath, nameInput, error);

                        if (newPath.IsEmpty()) {
                            noop = true;
                        }
                        else {
                            state->parentPath = parentPath;
                            state->createdPath = newPath;
                            state->oldOrder.clear();
                            state->newOrder.clear();

                            const SdfLayerHandle editLayer = stage->GetEditTarget().GetLayer();
                            if (!editLayer) {
                                error = "no edit layer";
                            }
                            else {
                                const bool parentIsRoot = parentPath == SdfPath::AbsoluteRootPath();

                                if (!parentIsRoot)
                                    stage::captureChildOrder(stage, parentPath, state->oldOrder);

                                const UsdGeomXform xform = UsdGeomXform::Define(stage, newPath);
                                if (!xform || !xform.GetPrim()) {
                                    error = "define failed";
                                }
                                else {
                                    if (!parentIsRoot) {
                                        state->newOrder = state->oldOrder;
                                        state->newOrder.push_back(newPath.GetNameToken());
                                        stage::restoreChildOrder(stage, parentPath, state->newOrder);
                                    }

                                    created = true;
                                }
                            }
                        }
                    }
                }

                QMetaObject::invokeMethod(
                    session,
                    [=]() {
                        using Status = Session::Notify::Status;

                        session->setPrimsUpdate(Session::PrimsUpdate::Immediate);

                        if (!hadStage) {
                            session->updateProgressNotify(Session::Notify("New xform failed", {}, Status::Error), 1);
                            session->endProgressBlock();
                            return;
                        }

                        if (noop) {
                            session->updateProgressNotify(
                                Session::Notify(error.isEmpty()
                                                    ? "New xform skipped"
                                                    : QString("New xform skipped: %1").arg(error),
                                                {},
                                                Status::Success),
                                1);
                            session->endProgressBlock();
                            return;
                        }

                        if (!created) {
                            session->updateProgressNotify(
                                Session::Notify(error.isEmpty() ? "New xform failed"
                                                                : QString("New xform failed: %1").arg(error),
                                                {},
                                                Status::Error),
                                1);
                            session->endProgressBlock();
                            return;
                        }

                        session->selectionList()->updatePaths({ newPath });
                        session->updateProgressNotify(
                            Session::Notify("Xform created", { parentPath, newPath }, Status::Success),
                            1);
                        session->endProgressBlock();
                    },
                    Qt::QueuedConnection);
            });
        },
        [state](Session* session) {
            if (!session || state->createdPath.IsEmpty())
                return;

            session->beginProgressBlock("Undo new xform", 1);
            session->setPrimsUpdate(Session::PrimsUpdate::Deferred);

            QThreadPool::globalInstance()->start([=]() {
                bool hadStage = true;
                bool removed = false;

                {
                    WRITE_LOCKER(locker, session->stageLock(), "stageLock");
                    const UsdStageRefPtr stage = session->stageUnsafe();

                    if (!stage) {
                        hadStage = false;
                    }
                    else {
                        const SdfLayerHandle editLayer = stage->GetEditTarget().GetLayer();
                        if (editLayer) {
                            removed = stage::removePrimSpec(editLayer, state->createdPath);

                            if (removed && !state->oldOrder.empty() && !state->parentPath.IsEmpty()
                                && state->parentPath != SdfPath::AbsoluteRootPath()) {
                                stage::restoreChildOrder(stage, state->parentPath, state->oldOrder);
                            }
                        }
                    }
                }

                QMetaObject::invokeMethod(
                    session,
                    [=]() {
                        using Status = Session::Notify::Status;

                        session->setPrimsUpdate(Session::PrimsUpdate::Immediate);

                        if (!hadStage) {
                            session->updateProgressNotify(Session::Notify("Undo new xform failed", {}, Status::Error),
                                                          1);
                            session->endProgressBlock();
                            return;
                        }

                        if (!removed) {
                            session->updateProgressNotify(Session::Notify("Undo new xform failed", {}, Status::Error),
                                                          1);
                            session->endProgressBlock();
                            return;
                        }

                        QList<SdfPath> updated;
                        for (const auto& p : session->selectionList()->paths()) {
                            if (p != state->createdPath)
                                updated.append(p);
                        }

                        session->selectionList()->updatePaths(updated);
                        session->setMask(path::removeAffectedPaths(state->previousMask, { state->createdPath }));
                        session->updateProgressNotify(
                            Session::Notify("New xform undone", { state->createdPath }, Status::Success),
                            1);
                        session->endProgressBlock();
                    },
                    Qt::QueuedConnection);
            });
        });
}

Command
movePath(const SdfPath& fromPath, const SdfPath& newParentPath, int insertIndex)
{
    struct MoveState {
        SdfPath oldPath;
        SdfPath newPath;
        SdfPath oldParentPath;
        SdfPath newParentPath;
        int insertIndex = -1;
        TfTokenVector oldParentOrder;
        TfTokenVector newParentOldOrder;
        TfTokenVector newParentNewOrder;
        QList<SdfPath> previousSelection;
        QList<SdfPath> previousMask;
    };

    auto state = std::make_shared<MoveState>();

    return Command(
        [fromPath, newParentPath, insertIndex, state](Session* session) {
            if (!session || fromPath.IsEmpty() || newParentPath.IsEmpty())
                return;

            state->previousSelection = session->selectionList()->paths();
            state->previousMask = session->mask();

            session->beginProgressBlock("Move path", 1);
            session->setPrimsUpdate(Session::PrimsUpdate::Deferred);

            QThreadPool::globalInstance()->start([=]() {
                bool hadStage = true;
                bool moved = false;
                bool noop = false;
                QString error;

                {
                    WRITE_LOCKER(locker, session->stageLock(), "stageLock");
                    const UsdStageRefPtr stage = session->stageUnsafe();

                    if (!stage) {
                        hadStage = false;
                    }
                    else {
                        const SdfPath oldParentPath = fromPath.GetParentPath();
                        const SdfPath targetPath = newParentPath.AppendChild(fromPath.GetNameToken());

                        if (oldParentPath.IsEmpty() || oldParentPath == SdfPath::AbsoluteRootPath()) {
                            noop = true;
                        }
                        else if (fromPath == SdfPath::AbsoluteRootPath()
                                 || newParentPath == SdfPath::AbsoluteRootPath()) {
                            noop = true;
                        }
                        else if (stage::isInsideCompositionArc(stage, fromPath)
                                 || stage::isInsideCompositionArc(stage, newParentPath)) {
                            error = "cannot move into or out of composed prims";
                        }
                        else if (fromPath == targetPath) {
                            state->oldPath = fromPath;
                            state->newPath = fromPath;
                            state->oldParentPath = oldParentPath;
                            state->newParentPath = newParentPath;
                            state->insertIndex = insertIndex;
                            state->oldParentOrder.clear();
                            state->newParentOldOrder.clear();
                            state->newParentNewOrder.clear();

                            stage::captureChildOrder(stage, oldParentPath, state->oldParentOrder);

                            const TfToken movedName = fromPath.GetNameToken();
                            state->newParentOldOrder = state->oldParentOrder;
                            state->newParentNewOrder =
                                stage::insertChildOrderToken(state->oldParentOrder, movedName, insertIndex);

                            if (state->newParentNewOrder != state->oldParentOrder) {
                                stage::restoreChildOrder(stage, oldParentPath, state->newParentNewOrder);
                                moved = true;
                            }
                            else {
                                noop = true;
                            }
                        }
                        else {
                            state->oldPath = fromPath;
                            state->newPath = targetPath;
                            state->oldParentPath = oldParentPath;
                            state->newParentPath = newParentPath;
                            state->insertIndex = insertIndex;
                            state->oldParentOrder.clear();
                            state->newParentOldOrder.clear();
                            state->newParentNewOrder.clear();

                            stage::captureChildOrder(stage, oldParentPath, state->oldParentOrder);
                            stage::captureChildOrder(stage, newParentPath, state->newParentOldOrder);

                            if (stage::movePrim(stage, fromPath, newParentPath, error)) {
                                const TfToken movedName = fromPath.GetNameToken();

                                if (!state->oldParentOrder.empty()) {
                                    stage::restoreChildOrder(
                                        stage,
                                        oldParentPath,
                                        stage::removeChildOrderToken(state->oldParentOrder, movedName));
                                }

                                state->newParentNewOrder =
                                    stage::insertChildOrderToken(state->newParentOldOrder, movedName, insertIndex);

                                if (!state->newParentNewOrder.empty())
                                    stage::restoreChildOrder(stage, newParentPath, state->newParentNewOrder);

                                moved = true;
                            }
                        }
                    }
                }

                QMetaObject::invokeMethod(
                    session,
                    [=]() {
                        using Status = Session::Notify::Status;

                        session->setPrimsUpdate(Session::PrimsUpdate::Immediate);

                        if (!hadStage) {
                            session->updateProgressNotify(Session::Notify("Move path failed", {}, Status::Error), 1);
                            session->endProgressBlock();
                            return;
                        }

                        if (noop) {
                            session->updateProgressNotify(Session::Notify("Move skipped", {}, Status::Success), 1);
                            session->endProgressBlock();
                            return;
                        }

                        if (!moved) {
                            session->updateProgressNotify(
                                Session::Notify(error.isEmpty() ? "Move path failed"
                                                                : QString("Move path failed: %1").arg(error),
                                                {},
                                                Status::Error),
                                1);
                            session->endProgressBlock();
                            return;
                        }

                        session->selectionList()->updatePaths(
                            path::remapAffectedPaths(state->previousSelection, fromPath, state->newPath));
                        session->setMask(path::remapAffectedPaths(state->previousMask, fromPath, state->newPath));
                        session->updateProgressNotify(
                            Session::Notify("Path moved", { state->oldPath, state->newPath }, Status::Success),
                            1);
                        session->endProgressBlock();
                    },
                    Qt::QueuedConnection);
            });
        },
        [state](Session* session) {
            if (!session || state->oldPath.IsEmpty() || state->newPath.IsEmpty())
                return;

            session->beginProgressBlock("Undo move path", 1);
            session->setPrimsUpdate(Session::PrimsUpdate::Deferred);

            QThreadPool::globalInstance()->start([=]() {
                bool hadStage = true;
                bool restored = false;
                QString error;

                {
                    WRITE_LOCKER(locker, session->stageLock(), "stageLock");
                    const UsdStageRefPtr stage = session->stageUnsafe();

                    if (!stage) {
                        hadStage = false;
                    }
                    else if (state->oldPath == state->newPath) {
                        if (!state->oldParentOrder.empty()) {
                            stage::restoreChildOrder(stage, state->oldParentPath, state->oldParentOrder);
                            restored = true;
                        }
                    }
                    else {
                        if (stage::movePrim(stage, state->newPath, state->oldParentPath, error)) {
                            if (!state->oldParentOrder.empty())
                                stage::restoreChildOrder(stage, state->oldParentPath, state->oldParentOrder);

                            if (!state->newParentOldOrder.empty())
                                stage::restoreChildOrder(stage, state->newParentPath, state->newParentOldOrder);

                            restored = true;
                        }
                    }
                }

                QMetaObject::invokeMethod(
                    session,
                    [=]() {
                        using Status = Session::Notify::Status;

                        session->selectionList()->updatePaths(state->previousSelection);
                        session->setMask(state->previousMask);
                        session->setPrimsUpdate(Session::PrimsUpdate::Immediate);

                        if (!hadStage) {
                            session->updateProgressNotify(Session::Notify("Undo move path failed", {}, Status::Error),
                                                          1);
                            session->endProgressBlock();
                            return;
                        }

                        if (!restored) {
                            session->updateProgressNotify(
                                Session::Notify(error.isEmpty() ? "Undo move path failed"
                                                                : QString("Undo move path failed: %1").arg(error),
                                                {},
                                                Status::Error),
                                1);
                            session->endProgressBlock();
                            return;
                        }

                        session->updateProgressNotify(
                            Session::Notify("Move undone", { state->oldPath }, Status::Success),
                            1);
                        session->endProgressBlock();
                    },
                    Qt::QueuedConnection);
            });
        });
}

}  // namespace stageviz
