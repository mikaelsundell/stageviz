// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#include "command.h"
#include "commandstack.h"
#include "qtutils.h"
#include "tracelocks.h"
#include "usdutils.h"
#include <QPointer>
#include <QThreadPool>
#include <algorithm>
#include <pxr/usd/sdf/copyUtils.h>
#include <pxr/usd/usd/namespaceEditor.h>
#include <pxr/usd/usd/prim.h>
#include <pxr/usd/usd/primRange.h>
#include <pxr/usd/usd/variantSets.h>
#include <pxr/usd/usdGeom/imageable.h>
#include <pxr/usd/usdGeom/tokens.h>
#include <pxr/usd/usdGeom/xform.h>

namespace stageviz {

namespace payload {
    struct UndoItem {
        SdfPath path;
        bool wasLoaded = false;
        bool hadVariantSet = false;
        std::string variantSetName;
        std::string previousVariantSelection;
    };

    struct State {
        QList<UndoItem> undoItems;
        QList<SdfPath> previousSelection;
        QList<SdfPath> previousMask;
    };

    struct Result {
        SdfPath path;
        bool success = false;
        QString message;
        Session::Notify::Status status = Session::Notify::Status::Info;
    };

    inline void flushResults(Session* session, const QList<payload::Result>& results, int completed)
    {
        if (!session || results.isEmpty())
            return;

        const int start = completed - static_cast<int>(results.size()) + 1;
        for (int i = 0; i < results.size(); ++i) {
            const payload::Result& r = results[i];
            session->updateProgressNotify(Session::Notify(r.message, { r.path }, r.status), start + i);
        }
    }

    inline bool applyLoad(UsdStageRefPtr stage, const SdfPath& path, bool useVariant, const std::string& variantSetName,
                          const std::string& variantSelection, UndoItem& undoItem, QString& error)
    {
        if (!stage) {
            error = "stage missing";
            return false;
        }

        UsdPrim prim = stage->GetPrimAtPath(path);
        if (!prim || !prim.HasPayload()) {
            error = "payload missing";
            return false;
        }

        undoItem.path = path;
        undoItem.wasLoaded = prim.IsLoaded();

        if (useVariant) {
            UsdVariantSet vs = prim.GetVariantSet(variantSetName);
            if (!vs.IsValid()) {
                error = "variant set missing";
                return false;
            }

            const auto variants = vs.GetVariantNames();
            if (std::find(variants.begin(), variants.end(), variantSelection) == variants.end()) {
                error = "variant value missing";
                return false;
            }

            undoItem.hadVariantSet = true;
            undoItem.variantSetName = variantSetName;
            undoItem.previousVariantSelection = vs.GetVariantSelection();

            if (prim.IsLoaded())
                prim.Unload();

            if (vs.GetVariantSelection() != variantSelection)
                vs.SetVariantSelection(variantSelection);
        }

        if (!prim.IsLoaded())
            prim.Load();

        return true;
    }

    inline bool applyUnload(UsdStageRefPtr stage, const SdfPath& path, UndoItem& undoItem, QString& error)
    {
        if (!stage) {
            error = "stage missing";
            return false;
        }

        UsdPrim prim = stage->GetPrimAtPath(path);
        if (!prim || !prim.HasPayload()) {
            error = "payload missing";
            return false;
        }

        undoItem.path = path;
        undoItem.wasLoaded = prim.IsLoaded();

        if (prim.IsLoaded())
            prim.Unload();

        return true;
    }

    inline bool restoreState(UsdStageRefPtr stage, const UndoItem& item, QString& error)
    {
        if (!stage) {
            error = "stage missing";
            return false;
        }

        UsdPrim prim = stage->GetPrimAtPath(item.path);
        if (!prim || !prim.HasPayload()) {
            error = "payload missing";
            return false;
        }

        if (item.hadVariantSet) {
            if (prim.IsLoaded())
                prim.Unload();

            UsdVariantSet vs = prim.GetVariantSet(item.variantSetName);
            if (!vs.IsValid()) {
                error = "variant set missing";
                return false;
            }

            if (vs.GetVariantSelection() != item.previousVariantSelection)
                vs.SetVariantSelection(item.previousVariantSelection);
        }

        if (item.wasLoaded) {
            if (!prim.IsLoaded())
                prim.Load();
        }
        else {
            if (prim.IsLoaded())
                prim.Unload();
        }

        return true;
    }

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

            session->beginProgressBlock("load payloads", paths.size());
            session->setPrimsUpdate(Session::PrimsUpdate::Deferred);

            QThreadPool::globalInstance()->start([session, paths, variantSet, variantValue, state]() {
                const bool useVariant = !variantSet.isEmpty() && !variantValue.isEmpty();
                const std::string variantSetName = qt::QStringToString(variantSet);
                const std::string variantSelection = qt::QStringToString(variantValue);

                QList<payload::Result> pending;
                pending.reserve(16);

                QList<payload::UndoItem> undoItems;
                undoItems.reserve(paths.size());

                int completed = 0;
                for (const SdfPath& path : paths) {
                    if (!session || session->isProgressBlockCancelled())
                        break;

                    payload::Result result;
                    result.path = path;
                    result.message = "payload failed";
                    result.status = Session::Notify::Status::Error;

                    payload::UndoItem undoItem;
                    QString error;

                    try {
                        WRITE_LOCKER(locker, session->stageLock(), "stageLock");
                        const UsdStageRefPtr stage = session->stageUnsafe();
                        result.success = payload::applyLoad(stage, path, useVariant, variantSetName, variantSelection,
                                                            undoItem, error);
                    } catch (...) {
                        result.success = false;
                        error = "exception";
                    }

                    result.message = result.success ? "payload loaded" : "payload failed";
                    result.status = result.success ? Session::Notify::Status::Info : Session::Notify::Status::Error;

                    if (result.success)
                        undoItems.append(undoItem);

                    pending.append(result);
                    ++completed;

                    if (pending.size() >= 16) {
                        const QList<payload::Result> batch = pending;
                        QMetaObject::invokeMethod(
                            session,
                            [session, batch, completed]() { payload::flushResults(session, batch, completed); },
                            Qt::QueuedConnection);
                        pending.clear();
                    }
                }

                if (!pending.isEmpty()) {
                    const QList<payload::Result> batch = pending;
                    QMetaObject::invokeMethod(
                        session, [session, batch, completed]() { payload::flushResults(session, batch, completed); },
                        Qt::QueuedConnection);
                }

                state->undoItems = undoItems;

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
            if (!session || state->undoItems.isEmpty())
                return;

            session->beginProgressBlock("undo load payloads", state->undoItems.size());
            session->setPrimsUpdate(Session::PrimsUpdate::Deferred);

            QThreadPool::globalInstance()->start([session, state]() {
                QList<payload::Result> pending;
                pending.reserve(16);

                int completed = 0;
                for (const payload::UndoItem& item : state->undoItems) {
                    if (!session || session->isProgressBlockCancelled())
                        break;

                    payload::Result result;
                    result.path = item.path;
                    result.message = "payload undo failed";
                    result.status = Session::Notify::Status::Error;

                    QString error;
                    try {
                        WRITE_LOCKER(locker, session->stageLock(), "stageLock");
                        const UsdStageRefPtr stage = session->stageUnsafe();
                        result.success = payload::restoreState(stage, item, error);
                    } catch (...) {
                        result.success = false;
                        error = "exception";
                    }

                    result.message = result.success ? "payload undone" : "payload undo failed";
                    result.status = result.success ? Session::Notify::Status::Info : Session::Notify::Status::Error;

                    pending.append(result);
                    ++completed;

                    if (pending.size() >= 16) {
                        const QList<payload::Result> batch = pending;
                        QMetaObject::invokeMethod(
                            session,
                            [session, batch, completed]() { payload::flushResults(session, batch, completed); },
                            Qt::QueuedConnection);
                        pending.clear();
                    }
                }

                QMetaObject::invokeMethod(
                    session,
                    [session, state, pending, completed]() {
                        if (!pending.isEmpty())
                            payload::flushResults(session, pending, completed);

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

            session->beginProgressBlock("unload payloads", paths.size());
            session->setPrimsUpdate(Session::PrimsUpdate::Deferred);

            QThreadPool::globalInstance()->start([session, paths, state]() {
                QList<payload::Result> pending;
                pending.reserve(16);

                QList<payload::UndoItem> undoItems;
                undoItems.reserve(paths.size());

                QList<SdfPath> unloadedPaths;
                unloadedPaths.reserve(paths.size());

                int completed = 0;
                for (const SdfPath& path : paths) {
                    if (!session || session->isProgressBlockCancelled())
                        break;

                    payload::Result result;
                    result.path = path;
                    result.message = "payload unload failed";
                    result.status = Session::Notify::Status::Error;

                    payload::UndoItem undoItem;
                    QString error;

                    try {
                        WRITE_LOCKER(locker, session->stageLock(), "stageLock");
                        const UsdStageRefPtr stage = session->stageUnsafe();
                        result.success = payload::applyUnload(stage, path, undoItem, error);
                    } catch (...) {
                        result.success = false;
                        error = "exception";
                    }

                    result.message = result.success ? "payload unloaded" : "payload unload failed";
                    result.status = result.success ? Session::Notify::Status::Info : Session::Notify::Status::Error;

                    if (result.success) {
                        undoItems.append(undoItem);
                        unloadedPaths.append(path);
                    }

                    pending.append(result);
                    ++completed;

                    if (pending.size() >= 16) {
                        const QList<payload::Result> batch = pending;
                        QMetaObject::invokeMethod(
                            session,
                            [session, batch, completed]() { payload::flushResults(session, batch, completed); },
                            Qt::QueuedConnection);
                        pending.clear();
                    }
                }

                if (!pending.isEmpty()) {
                    const QList<payload::Result> batch = pending;
                    QMetaObject::invokeMethod(
                        session, [session, batch, completed]() { payload::flushResults(session, batch, completed); },
                        Qt::QueuedConnection);
                }

                state->undoItems = undoItems;

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
            if (!session || state->undoItems.isEmpty())
                return;

            session->beginProgressBlock("undo unload payloads", state->undoItems.size());
            session->setPrimsUpdate(Session::PrimsUpdate::Deferred);

            QThreadPool::globalInstance()->start([session, state]() {
                QList<payload::Result> pending;
                pending.reserve(16);

                int completed = 0;
                for (const payload::UndoItem& item : state->undoItems) {
                    if (!session || session->isProgressBlockCancelled())
                        break;

                    payload::Result result;
                    result.path = item.path;
                    result.message = "payload undo failed";
                    result.status = Session::Notify::Status::Error;

                    QString error;
                    try {
                        WRITE_LOCKER(locker, session->stageLock(), "stageLock");
                        const UsdStageRefPtr stage = session->stageUnsafe();
                        result.success = payload::restoreState(stage, item, error);
                    } catch (...) {
                        result.success = false;
                        error = "exception";
                    }

                    result.message = result.success ? "payload restored" : "payload undo failed";
                    result.status = result.success ? Session::Notify::Status::Info : Session::Notify::Status::Error;

                    pending.append(result);
                    ++completed;

                    if (pending.size() >= 16) {
                        const QList<payload::Result> batch = pending;
                        QMetaObject::invokeMethod(
                            session,
                            [session, batch, completed]() { payload::flushResults(session, batch, completed); },
                            Qt::QueuedConnection);
                        pending.clear();
                    }
                }

                QMetaObject::invokeMethod(
                    session,
                    [session, state, pending, completed]() {
                        if (!pending.isEmpty())
                            payload::flushResults(session, pending, completed);

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
                QList<payload::Result> pending;
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

                            payload::Result result;
                            result.path = path;
                            result.success = true;
                            result.message = selectedSet.contains(path) ? "payload skipped" : "payload inverted";
                            result.status = Status::Info;
                            pending.append(result);
                            ++completed;

                            if (pending.size() >= 16) {
                                const QList<payload::Result> batch = pending;
                                QMetaObject::invokeMethod(
                                    session,
                                    [session, batch, completed]() { payload::flushResults(session, batch, completed); },
                                    Qt::QueuedConnection);
                                pending.clear();
                            }

                            if (session->isProgressBlockCancelled())
                                break;
                        }
                    }
                }

                if (!pending.isEmpty()) {
                    const QList<payload::Result> batch = pending;
                    QMetaObject::invokeMethod(
                        session, [session, batch, completed]() { payload::flushResults(session, batch, completed); },
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
                                                                          Status::Info),
                                                          1);
                            session->endProgressBlock();
                            return;
                        }

                        session->selectionList()->updatePaths(invertedPayloads);

                        session->updateProgressNotify(Session::Notify(total > 0 ? "payload selection inverted"
                                                                                : "invert payload selection skipped",
                                                                      invertedPayloads, Status::Info),
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
                                                                  state->previousSelection, Status::Info),
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
                        session->updateProgressNotify(Session::Notify("paths isolated", paths, Status::Info), 1);
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
                        session->updateProgressNotify(Session::Notify("isolate undone", *state, Status::Info), 1);
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
            session->beginProgressBlock("select paths", 1);

            QThreadPool::globalInstance()->start([session, paths, previous]() {
                *previous = session->selectionList()->paths();
                session->selectionList()->updatePaths(paths);

                QMetaObject::invokeMethod(
                    session,
                    [session, paths]() {
                        using Status = Session::Notify::Status;
                        session->updateProgressNotify(Session::Notify("paths selected", paths, Status::Info), 1);
                        session->endProgressBlock();
                    },
                    Qt::QueuedConnection);
            });
        },
        [previous](Session* session) {
            session->beginProgressBlock("undo select paths", 1);

            QThreadPool::globalInstance()->start([session, previous]() {
                session->selectionList()->updatePaths(*previous);

                QMetaObject::invokeMethod(
                    session,
                    [session, previous]() {
                        using Status = Session::Notify::Status;
                        session->updateProgressNotify(Session::Notify("select undone", *previous, Status::Info), 1);
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

    auto isWithinMask = [](const QList<SdfPath>& mask, const SdfPath& path) {
        if (mask.isEmpty())
            return true;

        for (const SdfPath& maskPath : mask) {
            if (path == maskPath || path.HasPrefix(maskPath))
                return true;
        }
        return false;
    };

    auto collectLeafPaths = [isWithinMask](const UsdStageRefPtr& stage, const QList<SdfPath>& mask) {
        QList<SdfPath> paths;
        if (!stage)
            return paths;

        for (const UsdPrim& prim : stage->Traverse()) {
            if (!prim || !prim.IsValid())
                continue;

            const SdfPath path = prim.GetPath();
            if (path.IsEmpty() || path == SdfPath::AbsoluteRootPath())
                continue;

            if (!isWithinMask(mask, path))
                continue;

            bool hasTraversableChild = false;
            for (const UsdPrim& child : prim.GetChildren()) {
                if (child && child.IsValid() && isWithinMask(mask, child.GetPath())) {
                    hasTraversableChild = true;
                    break;
                }
            }

            if (!hasTraversableChild)
                paths.append(path);
        }

        return paths;
    };

    return Command(
        [state, collectLeafPaths](Session* session) {
            if (!session)
                return;

            session->beginProgressBlock("select all", 1);

            QThreadPool::globalInstance()->start([session, state, collectLeafPaths]() {
                QList<SdfPath> selection;
                QList<SdfPath> previousSelection;
                QList<SdfPath> mask;

                {
                    READ_LOCKER(locker, session->stageLock(), "stageLock");
                    const UsdStageRefPtr stage = session->stageUnsafe();

                    previousSelection = session->selectionList()->paths();
                    mask = session->mask();
                    selection = collectLeafPaths(stage, mask);
                }

                state->previousSelection = previousSelection;

                QMetaObject::invokeMethod(
                    session,
                    [session, selection]() {
                        using Status = Session::Notify::Status;
                        session->selectionList()->updatePaths(selection);
                        session->updateProgressNotify(Session::Notify("all paths selected", selection, Status::Info),
                                                      1);
                        session->endProgressBlock();
                    },
                    Qt::QueuedConnection);
            });
        },
        [state](Session* session) {
            if (!session)
                return;

            session->beginProgressBlock("undo select all", 1);

            QThreadPool::globalInstance()->start([session, state]() {
                QMetaObject::invokeMethod(
                    session,
                    [session, state]() {
                        using Status = Session::Notify::Status;
                        session->selectionList()->updatePaths(state->previousSelection);
                        session->updateProgressNotify(Session::Notify("select all undone", state->previousSelection,
                                                                      Status::Info),
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

    auto isWithinMask = [](const QList<SdfPath>& mask, const SdfPath& path) {
        if (mask.isEmpty())
            return true;

        for (const SdfPath& maskPath : mask) {
            if (path == maskPath || path.HasPrefix(maskPath))
                return true;
        }
        return false;
    };

    auto isCoveredBySelection = [](const QList<SdfPath>& selection, const SdfPath& path) {
        for (const SdfPath& selectedPath : selection) {
            if (path == selectedPath || path.HasPrefix(selectedPath))
                return true;
        }
        return false;
    };

    auto collectLeafPaths = [isWithinMask](const UsdStageRefPtr& stage, const QList<SdfPath>& mask) {
        QList<SdfPath> paths;
        if (!stage)
            return paths;

        for (const UsdPrim& prim : stage->Traverse()) {
            if (!prim || !prim.IsValid())
                continue;

            const SdfPath path = prim.GetPath();
            if (path.IsEmpty() || path == SdfPath::AbsoluteRootPath())
                continue;

            if (!isWithinMask(mask, path))
                continue;

            bool hasTraversableChild = false;
            for (const UsdPrim& child : prim.GetChildren()) {
                if (child && child.IsValid()) {
                    hasTraversableChild = true;
                    break;
                }
            }

            if (!hasTraversableChild)
                paths.append(path);
        }

        return paths;
    };

    return Command(
        [state, collectLeafPaths, isCoveredBySelection](Session* session) {
            if (!session)
                return;

            session->beginProgressBlock("invert selection", 1);

            QThreadPool::globalInstance()->start([session, state, collectLeafPaths, isCoveredBySelection]() {
                QList<SdfPath> invertedSelection;
                QList<SdfPath> previousSelection;
                QList<SdfPath> domain;
                QList<SdfPath> mask;

                {
                    READ_LOCKER(locker, session->stageLock(), "stageLock");
                    const UsdStageRefPtr stage = session->stageUnsafe();

                    previousSelection = session->selectionList()->paths();
                    mask = session->mask();
                    domain = collectLeafPaths(stage, mask);
                }

                state->previousSelection = previousSelection;

                for (const SdfPath& path : domain) {
                    if (!isCoveredBySelection(previousSelection, path))
                        invertedSelection.append(path);
                }

                QMetaObject::invokeMethod(
                    session,
                    [session, invertedSelection]() {
                        using Status = Session::Notify::Status;
                        session->selectionList()->updatePaths(invertedSelection);
                        session->updateProgressNotify(Session::Notify("selection inverted", invertedSelection,
                                                                      Status::Info),
                                                      1);
                        session->endProgressBlock();
                    },
                    Qt::QueuedConnection);
            });
        },
        [state](Session* session) {
            if (!session)
                return;

            session->beginProgressBlock("undo invert selection", 1);

            QThreadPool::globalInstance()->start([session, state]() {
                QMetaObject::invokeMethod(
                    session,
                    [session, state]() {
                        using Status = Session::Notify::Status;
                        session->selectionList()->updatePaths(state->previousSelection);
                        session->updateProgressNotify(Session::Notify("invert selection undone",
                                                                      state->previousSelection, Status::Info),
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
            session->beginProgressBlock("show paths", 1);
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
                        session->updateProgressNotify(Session::Notify(success ? "paths shown" : "show paths failed",
                                                                      paths, success ? Status::Info : Status::Error),
                                                      1);
                        session->endProgressBlock();
                    },
                    Qt::QueuedConnection);
            });
        },
        [state, recursive](Session* session) {
            session->beginProgressBlock("undo show paths", 1);
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
                        session->updateProgressNotify(Session::Notify(success ? "show undone" : "undo show paths failed",
                                                                      restoredPaths,
                                                                      success ? Status::Info : Status::Error),
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
            session->beginProgressBlock("hide paths", 1);
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
                        session->updateProgressNotify(Session::Notify(success ? "paths hidden" : "hide paths failed",
                                                                      paths, success ? Status::Info : Status::Error),
                                                      1);
                        session->endProgressBlock();
                    },
                    Qt::QueuedConnection);
            });
        },
        [state, recursive](Session* session) {
            session->beginProgressBlock("undo hide paths", 1);
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
                        session->updateProgressNotify(Session::Notify(success ? "hide undone" : "undo hide paths failed",
                                                                      restoredPaths,
                                                                      success ? Status::Info : Status::Error),
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
            session->beginProgressBlock("set stage up", 1);

            QThreadPool::globalInstance()->start([session, stageUp, state]() {
                *state = session->stageUp();
                session->setStageUp(stageUp);

                QMetaObject::invokeMethod(
                    session,
                    [session, stageUp]() {
                        using Status = Session::Notify::Status;
                        const QString axis = (stageUp == Session::StageUp::Z) ? "Z" : "Y";
                        session->updateProgressNotify(Session::Notify(QString("stage up set to %1").arg(axis), {},
                                                                      Status::Info),
                                                      1);
                        session->endProgressBlock();
                    },
                    Qt::QueuedConnection);
            });
        },
        [state](Session* session) {
            session->beginProgressBlock("undo set stage up", 1);

            QThreadPool::globalInstance()->start([session, state]() {
                session->setStageUp(*state);

                QMetaObject::invokeMethod(
                    session,
                    [session, state]() {
                        using Status = Session::Notify::Status;
                        const QString axis = (*state == Session::StageUp::Z) ? "Z" : "Y";
                        session->updateProgressNotify(Session::Notify(QString("set stage up undone to %1").arg(axis),
                                                                      {}, Status::Info),
                                                      1);
                        session->endProgressBlock();
                    },
                    Qt::QueuedConnection);
            });
        });
}

namespace snapshot {
    struct PrimState {
        SdfPath stagePath;
        SdfPath specPath;
        SdfLayerRefPtr snapshotLayer;
    };

    struct DeleteState {
        QVector<PrimState> prims;
        QHash<SdfPath, TfTokenVector> parentOrders;
        QList<SdfPath> previousSelection;
        QList<SdfPath> previousMask;
    };

    using PrimSnapshot = QVector<PrimState>;

    inline void ensureParentSpecs(const SdfLayerHandle& layer, const SdfPath& path)
    {
        if (!layer)
            return;

        const SdfPath parent = path.GetParentPath();
        if (parent.IsEmpty() || parent == SdfPath::AbsoluteRootPath())
            return;

        if (!layer->GetPrimAtPath(parent)) {
            ensureParentSpecs(layer, parent);
            SdfCreatePrimInLayer(layer, parent);
        }
    }

    inline bool capturePrimToLayer(UsdStageRefPtr stage, const SdfPath& stagePath, PrimState& out)
    {
        if (!stage)
            return false;

        const UsdPrim prim = stage->GetPrimAtPath(stagePath);
        if (!prim)
            return false;

        const auto& stack = prim.GetPrimStack();
        if (stack.empty())
            return false;

        for (const SdfPrimSpecHandle& spec : stack) {
            if (!spec)
                continue;

            const SdfLayerHandle srcLayer = spec->GetLayer();
            const SdfPath specPath = spec->GetPath();
            if (!srcLayer || specPath.IsEmpty())
                continue;

            if (!srcLayer->GetPrimAtPath(specPath))
                continue;

            SdfLayerRefPtr snapshotLayer = SdfLayer::CreateAnonymous(".usda");
            if (!snapshotLayer)
                continue;

            ensureParentSpecs(snapshotLayer, specPath);

            if (SdfCopySpec(srcLayer, specPath, snapshotLayer, specPath)) {
                out.stagePath = stagePath;
                out.specPath = specPath;
                out.snapshotLayer = snapshotLayer;
                return true;
            }
        }

        return false;
    }

    inline void restorePrimFromSnapshotLayer(const SdfLayerHandle& dstLayer, const PrimState& state)
    {
        if (!dstLayer || !state.snapshotLayer)
            return;

        ensureParentSpecs(dstLayer, state.stagePath);
        SdfCopySpec(state.snapshotLayer, state.specPath, dstLayer, state.stagePath);
    }

    inline void sortByHierarchy(PrimSnapshot& snapshot)
    {
        std::sort(snapshot.begin(), snapshot.end(), [](const PrimState& a, const PrimState& b) {
            const size_t ac = a.stagePath.GetPathElementCount();
            const size_t bc = b.stagePath.GetPathElementCount();
            if (ac != bc)
                return ac < bc;
            return a.stagePath.GetString() < b.stagePath.GetString();
        });
    }
}  // namespace snapshot

Command
deletePaths(const QList<SdfPath>& inPaths)
{
    auto state = std::make_shared<snapshot::DeleteState>();

    return Command(
        [inPaths, state](Session* session) {
            state->previousSelection = session->selectionList()->paths();
            state->previousMask = session->mask();

            session->beginProgressBlock("delete paths", 1);
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
                        session->updateProgressNotify(Session::Notify(success ? "paths deleted" : "delete paths failed",
                                                                      changed, success ? Status::Info : Status::Error),
                                                      1);
                        session->endProgressBlock();
                    },
                    Qt::QueuedConnection);
            });
        },
        [state](Session* session) {
            session->beginProgressBlock("undo delete paths", 1);
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
                        session->updateProgressNotify(Session::Notify(success ? "delete undone"
                                                                              : "undo delete paths failed",
                                                                      changed, success ? Status::Info : Status::Error),
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

    auto buildNewPath = [](const UsdStageRefPtr& stage, const SdfPath& path, const QString& input, QString& error) {
        if (!stage || path.IsEmpty()) {
            error = "invalid stage or path";
            return SdfPath();
        }

        const QString trimmed = input.trimmed();
        if (trimmed.isEmpty()) {
            error = "empty name";
            return SdfPath();
        }

        if (UsdPrim defaultPrim = stage->GetDefaultPrim()) {
            if (path == defaultPrim.GetPath()) {
                error = "cannot rename default prim";
                return SdfPath();
            }
        }

        const SdfPath parentPath = path.GetParentPath();
        if (parentPath.IsEmpty() || parentPath == SdfPath::AbsoluteRootPath()) {
            error = "invalid parent";
            return SdfPath();
        }

        const QString safeName = name::makeSafeName(stage, parentPath, trimmed, path);
        if (safeName.isEmpty()) {
            error = "invalid name";
            return SdfPath();
        }

        const std::string nameValue = qt::QStringToString(safeName);
        if (!SdfPath::IsValidIdentifier(nameValue)) {
            error = "invalid identifier";
            return SdfPath();
        }

        return parentPath.AppendChild(TfToken(nameValue));
    };

    auto remapChildOrder = [](const TfTokenVector& order, const TfToken& oldName, const TfToken& newName) {
        TfTokenVector out = order;
        for (TfToken& token : out) {
            if (token == oldName) {
                token = newName;
                break;
            }
        }
        return out;
    };

    auto applyRename = [](UsdStageRefPtr stage, const SdfPath& from, const SdfPath& to, QString& error) -> bool {
        if (!stage) {
            error = "invalid stage";
            return false;
        }

        const UsdPrim prim = stage->GetPrimAtPath(from);
        if (!prim) {
            error = "invalid prim";
            return false;
        }

        const SdfPath parentPath = from.GetParentPath();
        if (parentPath.IsEmpty() || parentPath != to.GetParentPath()) {
            error = "invalid rename target";
            return false;
        }

        if (stage->GetPrimAtPath(to)) {
            error = "target exists";
            return false;
        }

        UsdNamespaceEditor editor(stage);
        if (!editor.RenamePrim(prim, to.GetNameToken())) {
            error = "RenamePrim failed";
            return false;
        }

        std::string whyNot;
        if (!editor.CanApplyEdits(&whyNot)) {
            error = qt::StringToQString(whyNot);
            return false;
        }

        return editor.ApplyEdits();
    };

    return Command(
        [path, newNameInput, buildNewPath, remapChildOrder, applyRename, state](Session* session) {
            if (!session || path.IsEmpty())
                return;

            state->previousSelection = session->selectionList()->paths();
            state->previousMask = session->mask();

            session->beginProgressBlock("rename path", 1);
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
                        newPath = buildNewPath(stage, path, newNameInput, error);

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

                            if (!state->parentPath.IsEmpty() && state->parentPath != SdfPath::AbsoluteRootPath())
                                stage::captureChildOrder(stage, state->parentPath, state->oldOrder);

                            const UsdStageLoadRules rules = stage->GetLoadRules();

                            if (applyRename(stage, path, newPath, error)) {
                                stage->SetLoadRules(stage::remapLoadRules(rules, path, newPath));

                                if (!state->oldOrder.empty()) {
                                    state->newOrder = remapChildOrder(state->oldOrder, path.GetNameToken(),
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
                            session->updateProgressNotify(Session::Notify("rename path failed", { path }, Status::Error),
                                                          1);
                            session->endProgressBlock();
                            return;
                        }

                        if (noop) {
                            session->updateProgressNotify(Session::Notify("rename skipped", { path }, Status::Info), 1);
                            session->endProgressBlock();
                            return;
                        }

                        if (!renamed) {
                            session->updateProgressNotify(
                                Session::Notify(error.isEmpty() ? "rename path failed"
                                                                : QString("rename path failed: %1").arg(error),
                                                { path }, Status::Error),
                                1);
                            session->endProgressBlock();
                            return;
                        }

                        session->selectionList()->updatePaths(
                            path::remapAffectedPaths(state->previousSelection, path, newPath));
                        session->setMask(path::remapAffectedPaths(state->previousMask, path, newPath));
                        session->updateProgressNotify(Session::Notify("path renamed", { path, newPath }, Status::Info),
                                                      1);
                        session->endProgressBlock();
                    },
                    Qt::QueuedConnection);
            });
        },
        [applyRename, state](Session* session) {
            if (!session || state->oldPath.IsEmpty() || state->newPath.IsEmpty())
                return;

            session->beginProgressBlock("undo rename path", 1);
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

                        if (applyRename(stage, state->newPath, state->oldPath, error)) {
                            stage->SetLoadRules(stage::remapLoadRules(rules, state->newPath, state->oldPath));

                            if (!state->oldOrder.empty() && !state->parentPath.IsEmpty()
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
                            session->updateProgressNotify(Session::Notify("undo rename path failed", {}, Status::Error),
                                                          1);
                            session->endProgressBlock();
                            return;
                        }

                        if (!restored) {
                            session->updateProgressNotify(
                                Session::Notify(error.isEmpty() ? "undo rename path failed"
                                                                : QString("undo rename path failed: %1").arg(error),
                                                {}, Status::Error),
                                1);
                            session->endProgressBlock();
                            return;
                        }

                        session->updateProgressNotify(Session::Notify("rename undone", { state->oldPath }, Status::Info),
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

    auto buildNewPath = [](const UsdStageRefPtr& stage, const SdfPath& parentPath, const QString& input) {
        if (!stage || parentPath.IsEmpty() || parentPath == SdfPath::AbsoluteRootPath())
            return SdfPath();

        const QString trimmed = input.trimmed();
        if (trimmed.isEmpty())
            return SdfPath();

        const QString safeName = name::makeSafeName(stage, parentPath, trimmed);
        if (safeName.isEmpty())
            return SdfPath();

        const std::string nameValue = qt::QStringToString(safeName);
        if (!SdfPath::IsValidIdentifier(nameValue))
            return SdfPath();

        return parentPath.AppendChild(TfToken(nameValue));
    };

    return Command(
        [parentPath, nameInput, buildNewPath, state](Session* session) {
            if (!session || parentPath.IsEmpty())
                return;

            state->previousMask = session->mask();

            session->beginProgressBlock("new xform", 1);
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
                        newPath = buildNewPath(stage, parentPath, nameInput);
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
                                const UsdPrim parentPrim = stage->GetPrimAtPath(parentPath);
                                if (!parentPrim) {
                                    error = "invalid parent";
                                }
                                else if (stage->GetPrimAtPath(newPath)) {
                                    error = "target exists";
                                }
                                else {
                                    stage::captureChildOrder(stage, parentPath, state->oldOrder);

                                    const UsdGeomXform xform = UsdGeomXform::Define(stage, newPath);
                                    if (!xform || !xform.GetPrim()) {
                                        error = "define failed";
                                    }
                                    else {
                                        state->newOrder = state->oldOrder;
                                        state->newOrder.push_back(newPath.GetNameToken());
                                        stage::restoreChildOrder(stage, parentPath, state->newOrder);
                                        created = true;
                                    }
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
                            session->updateProgressNotify(Session::Notify("new xform failed", {}, Status::Error), 1);
                            session->endProgressBlock();
                            return;
                        }

                        if (noop) {
                            session->updateProgressNotify(Session::Notify("new xform skipped", {}, Status::Info), 1);
                            session->endProgressBlock();
                            return;
                        }

                        if (!created) {
                            session->updateProgressNotify(
                                Session::Notify(error.isEmpty() ? "new xform failed"
                                                                : QString("new xform failed: %1").arg(error),
                                                {}, Status::Error),
                                1);
                            session->endProgressBlock();
                            return;
                        }

                        session->selectionList()->updatePaths({ newPath });
                        session->updateProgressNotify(Session::Notify("xform created", { parentPath, newPath },
                                                                      Status::Info),
                                                      1);
                        session->endProgressBlock();
                    },
                    Qt::QueuedConnection);
            });
        },
        [state](Session* session) {
            if (!session || state->createdPath.IsEmpty())
                return;

            session->beginProgressBlock("undo new xform", 1);
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
                            session->updateProgressNotify(Session::Notify("undo new xform failed", {}, Status::Error),
                                                          1);
                            session->endProgressBlock();
                            return;
                        }

                        if (!removed) {
                            session->updateProgressNotify(Session::Notify("undo new xform failed", {}, Status::Error),
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
                        session->updateProgressNotify(Session::Notify("new xform undone", { state->createdPath },
                                                                      Status::Info),
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

    auto removeToken = [](const TfTokenVector& order, const TfToken& name) {
        TfTokenVector out;
        out.reserve(order.size());
        for (const TfToken& token : order) {
            if (token != name)
                out.push_back(token);
        }
        return out;
    };

    auto insertTokenAt = [](const TfTokenVector& order, const TfToken& name, int index) {
        TfTokenVector out;
        out.reserve(order.size() + 1);

        for (const TfToken& token : order) {
            if (token != name)
                out.push_back(token);
        }

        if (index < 0 || index > static_cast<int>(out.size()))
            index = static_cast<int>(out.size());

        out.insert(out.begin() + index, name);
        return out;
    };

    auto applyMove = [](UsdStageRefPtr stage, const SdfPath& from, const SdfPath& toParent, QString& error) -> bool {
        if (!stage) {
            error = "invalid stage";
            return false;
        }

        const UsdPrim prim = stage->GetPrimAtPath(from);
        if (!prim) {
            error = "invalid prim";
            return false;
        }

        const UsdPrim newParent = stage->GetPrimAtPath(toParent);
        if (!newParent) {
            error = "invalid parent";
            return false;
        }

        if (from.IsEmpty() || toParent.IsEmpty()) {
            error = "invalid path";
            return false;
        }

        if (from == toParent || toParent.HasPrefix(from)) {
            error = "invalid hierarchy";
            return false;
        }

        const SdfPath newPath = toParent.AppendChild(from.GetNameToken());
        if (stage->GetPrimAtPath(newPath)) {
            error = "target exists";
            return false;
        }

        UsdNamespaceEditor editor(stage);
        if (!editor.ReparentPrim(prim, newParent)) {
            error = "ReparentPrim failed";
            return false;
        }

        std::string whyNot;
        if (!editor.CanApplyEdits(&whyNot)) {
            error = qt::StringToQString(whyNot);
            return false;
        }

        return editor.ApplyEdits();
    };

    return Command(
        [fromPath, newParentPath, insertIndex, removeToken, insertTokenAt, applyMove, state](Session* session) {
            if (!session || fromPath.IsEmpty() || newParentPath.IsEmpty())
                return;

            state->previousSelection = session->selectionList()->paths();
            state->previousMask = session->mask();

            session->beginProgressBlock("move path", 1);
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
                            state->newParentNewOrder = insertTokenAt(state->oldParentOrder, movedName, insertIndex);

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

                            if (applyMove(stage, fromPath, newParentPath, error)) {
                                const TfToken movedName = fromPath.GetNameToken();

                                if (!state->oldParentOrder.empty())
                                    stage::restoreChildOrder(stage, oldParentPath,
                                                             removeToken(state->oldParentOrder, movedName));

                                state->newParentNewOrder = insertTokenAt(state->newParentOldOrder, movedName,
                                                                         insertIndex);

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
                            session->updateProgressNotify(Session::Notify("move path failed", {}, Status::Error), 1);
                            session->endProgressBlock();
                            return;
                        }

                        if (noop) {
                            session->updateProgressNotify(Session::Notify("move skipped", {}, Status::Info), 1);
                            session->endProgressBlock();
                            return;
                        }

                        if (!moved) {
                            session->updateProgressNotify(
                                Session::Notify(error.isEmpty() ? "move path failed"
                                                                : QString("move path failed: %1").arg(error),
                                                {}, Status::Error),
                                1);
                            session->endProgressBlock();
                            return;
                        }

                        session->selectionList()->updatePaths(
                            path::remapAffectedPaths(state->previousSelection, fromPath, state->newPath));
                        session->setMask(path::remapAffectedPaths(state->previousMask, fromPath, state->newPath));
                        session->updateProgressNotify(Session::Notify("path moved", { state->oldPath, state->newPath },
                                                                      Status::Info),
                                                      1);
                        session->endProgressBlock();
                    },
                    Qt::QueuedConnection);
            });
        },
        [applyMove, state](Session* session) {
            if (!session || state->oldPath.IsEmpty() || state->newPath.IsEmpty())
                return;

            session->beginProgressBlock("undo move path", 1);
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
                        if (applyMove(stage, state->newPath, state->oldParentPath, error)) {
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
                            session->updateProgressNotify(Session::Notify("undo move path failed", {}, Status::Error),
                                                          1);
                            session->endProgressBlock();
                            return;
                        }

                        if (!restored) {
                            session->updateProgressNotify(
                                Session::Notify(error.isEmpty() ? "undo move path failed"
                                                                : QString("undo move path failed: %1").arg(error),
                                                {}, Status::Error),
                                1);
                            session->endProgressBlock();
                            return;
                        }

                        session->updateProgressNotify(Session::Notify("move undone", { state->oldPath }, Status::Info),
                                                      1);
                        session->endProgressBlock();
                    },
                    Qt::QueuedConnection);
            });
        });
}

}  // namespace stageviz
