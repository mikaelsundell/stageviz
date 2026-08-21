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
#include <pxr/usd/sdf/namespaceEdit.h>
#include <pxr/usd/usd/attribute.h>
#include <pxr/usd/usd/editContext.h>
#include <pxr/usd/usd/editTarget.h>
#include <pxr/usd/usd/prim.h>
#include <pxr/usd/usd/primRange.h>
#include <pxr/usd/usdGeom/imageable.h>
#include <pxr/usd/usdGeom/tokens.h>
#include <pxr/usd/usdGeom/xform.h>
#include <pxr/usd/usdGeom/xformable.h>

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

    QString appendError(const QString& message, const QString& error)
    {
        return error.isEmpty() ? message : QString("%1: %2").arg(message, error);
    }

    QString summarizeErrors(const QStringList& errors, int maxCount = 3)
    {
        if (errors.isEmpty())
            return {};

        QStringList result;
        for (int i = 0; i < errors.size() && i < maxCount; ++i)
            result.append(errors.at(i));
        if (errors.size() > maxCount)
            result.append(QString("%1 more").arg(errors.size() - maxCount));
        return result.join("; ");
    }

    struct RootPropertyState {
        SdfPath propertyPath;
        bool hadSpec = false;
        bool hadDefault = false;
        VtValue defaultValue;
    };

    RootPropertyState captureRootPropertyState(const SdfLayerHandle& rootLayer, const SdfPath& propertyPath)
    {
        RootPropertyState state;
        state.propertyPath = propertyPath;
        if (!rootLayer || propertyPath.IsEmpty())
            return state;

        state.hadSpec = bool(rootLayer->GetPropertyAtPath(propertyPath));
        state.hadDefault = rootLayer->HasField(propertyPath, SdfFieldKeys->Default);
        if (state.hadDefault)
            state.defaultValue = rootLayer->GetField(propertyPath, SdfFieldKeys->Default);
        return state;
    }

    bool removePropertySpec(const SdfLayerHandle& layer, const SdfPath& propertyPath)
    {
        if (!layer || propertyPath.IsEmpty() || !propertyPath.IsPropertyPath())
            return false;
        if (!layer->GetPropertyAtPath(propertyPath))
            return true;

        SdfBatchNamespaceEdit edits;
        edits.Add(propertyPath, SdfPath::EmptyPath());
        return layer->CanApply(edits) && layer->Apply(edits);
    }

    bool restoreRootPropertyState(const SdfLayerHandle& rootLayer, const RootPropertyState& state)
    {
        if (!rootLayer || state.propertyPath.IsEmpty())
            return false;

        if (!state.hadSpec)
            return removePropertySpec(rootLayer, state.propertyPath);

        if (state.hadDefault) {
            rootLayer->SetField(state.propertyPath, SdfFieldKeys->Default, state.defaultValue);
            return true;
        }

        rootLayer->EraseField(state.propertyPath, SdfFieldKeys->Default);
        return true;
    }

    QList<SdfPath> visibilityAffectedPaths(UsdStageRefPtr stage, const QList<SdfPath>& paths, bool recursive,
                                           bool makeVisible)
    {
        QList<SdfPath> result;
        if (!stage)
            return result;

        auto appendImageable = [&](const UsdPrim& prim) {
            if (prim && UsdGeomImageable(prim) && !result.contains(prim.GetPath()))
                result.append(prim.GetPath());
        };

        for (const SdfPath& inputPath : paths) {
            const SdfPath primPath = inputPath.IsPropertyPath() ? inputPath.GetPrimPath() : inputPath;
            const UsdPrim prim = stage->GetPrimAtPath(primPath);
            if (!prim)
                continue;

            appendImageable(prim);

            if (recursive) {
                for (const UsdPrim& child : prim.GetAllDescendants())
                    appendImageable(child);
            }

            if (makeVisible) {
                for (UsdPrim ancestor = prim.GetParent(); ancestor && !ancestor.IsPseudoRoot();
                     ancestor = ancestor.GetParent()) {
                    appendImageable(ancestor);
                }
            }
        }

        return result;
    }

    bool restoreTransformRootState(UsdStageRefPtr stage, const SdfPath& primPath, const TransformRootState& state,
                                   QString& error)
    {
        if (!stage) {
            error = "stage missing";
            return false;
        }

        QString rootError;
        const SdfLayerHandle rootLayer = rootlayer::opened(stage, rootError);
        if (!rootLayer) {
            error = rootError;
            return false;
        }

        const SdfPath orderPath = primPath.AppendProperty(TfToken("xformOpOrder"));
        const SdfPath matrixPath = primPath.AppendProperty(TfToken("xformOp:transform"));

        if (state.hadXformOpOrderSpec) {
            if (state.hadXformOpOrderDefault)
                rootLayer->SetField(orderPath, SdfFieldKeys->Default, state.xformOpOrderDefault);
            else
                rootLayer->EraseField(orderPath, SdfFieldKeys->Default);
        }
        else if (!removePropertySpec(rootLayer, orderPath)) {
            error = QString("failed to remove transform order override: %1").arg(pathText(primPath));
            return false;
        }

        if (state.hadMatrixOpSpec) {
            if (state.hadMatrixOpDefault)
                rootLayer->SetField(matrixPath, SdfFieldKeys->Default, state.matrixOpDefault);
            else
                rootLayer->EraseField(matrixPath, SdfFieldKeys->Default);
        }
        else if (!removePropertySpec(rootLayer, matrixPath)) {
            error = QString("failed to remove matrix transform override: %1").arg(pathText(primPath));
            return false;
        }

        return true;
    }
    bool hasUnderlyingTransformOpinion(UsdStageRefPtr stage, const UsdPrim& prim, const SdfLayerHandle& rootLayer)
    {
        if (!stage || !prim || !rootLayer)
            return false;

        const TfToken orderToken("xformOpOrder");

        for (const SdfPrimSpecHandle& primSpec : prim.GetPrimStack()) {
            if (!primSpec)
                continue;

            const SdfLayerHandle layer = primSpec->GetLayer();
            if (!layer || layer == rootLayer)
                continue;

            const SdfPath orderPath = primSpec->GetPath().AppendProperty(orderToken);
            if (layer->HasField(orderPath, SdfFieldKeys->Default))
                return true;
        }

        return false;
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
                        const QList<SdfPath> selectedPayloads = stage::resolvePayloadPaths(stage, previousSelection);

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

                    domain = stage::leafPaths(stage, mask, false);
                }

                state->previousSelection = previousSelection;

                for (const SdfPath& path : domain) {
                    if (!path::isCoveredByRoots(previousSelection, path))
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
    struct VisibilityCommandState {
        QList<RootPropertyState> rootStates;
    };

    auto state = std::make_shared<VisibilityCommandState>();

    return Command(
        [paths, recursive, state](Session* session) {
            command::beginDeferred(session, "Show paths", 1);

            command::runWorker([session, paths, recursive, state]() {
                bool success = false;
                QString error;

                {
                    WRITE_LOCKER(locker, session->stageLock(), "stageLock");
                    const UsdStageRefPtr stage = session->stageUnsafe();

                    if (!stage) {
                        error = "stage missing";
                    }
                    else {
                        QString rootError;
                        const SdfLayerHandle rootLayer = rootlayer::opened(stage, rootError);
                        if (!rootLayer) {
                            error = rootError;
                        }
                        else {
                            state->rootStates.clear();
                            const QList<SdfPath> affected = visibilityAffectedPaths(stage, paths, recursive, true);
                            state->rootStates.reserve(affected.size());
                            for (const SdfPath& path : affected) {
                                state->rootStates.append(
                                    captureRootPropertyState(rootLayer, path.AppendProperty(UsdGeomTokens->visibility)));
                            }

                            stage::setVisible(stage, paths, true, recursive);
                            success = true;
                        }
                    }
                }

                command::queueToSession(session, [session, paths, success, error]() {
                    using Status = Session::Notify::Status;
                    command::finishDeferred(session, success ? "Paths shown" : appendError("Show paths failed", error),
                                            paths, success ? Status::Success : Status::Error);
                });
            });
        },
        [state](Session* session) {
            command::beginDeferred(session, "Undo show paths", 1);

            command::runWorker([session, state]() {
                bool success = true;
                QList<SdfPath> restoredPaths;
                QString error;

                {
                    WRITE_LOCKER(locker, session->stageLock(), "stageLock");
                    const UsdStageRefPtr stage = session->stageUnsafe();
                    QString rootError;
                    const SdfLayerHandle rootLayer = rootlayer::opened(stage, rootError);

                    if (!stage || !rootLayer) {
                        success = false;
                        error = !rootError.isEmpty() ? rootError : QStringLiteral("stage missing");
                    }
                    else {
                        for (const RootPropertyState& rootState : state->rootStates) {
                            if (!restoreRootPropertyState(rootLayer, rootState)) {
                                success = false;
                                error = QString("failed to restore visibility override: %1")
                                            .arg(pathText(rootState.propertyPath.GetPrimPath()));
                                break;
                            }
                            path::appendUnique(restoredPaths, rootState.propertyPath.GetPrimPath());
                        }
                    }
                }

                command::queueToSession(session, [session, restoredPaths, success, error]() {
                    using Status = Session::Notify::Status;
                    command::finishDeferred(session,
                                            success ? "Show undone" : appendError("Undo show paths failed", error),
                                            restoredPaths, success ? Status::Success : Status::Error);
                });
            });
        });
}

Command
hidePaths(const QList<SdfPath>& paths, bool recursive)
{
    struct VisibilityCommandState {
        QList<RootPropertyState> rootStates;
    };

    auto state = std::make_shared<VisibilityCommandState>();

    return Command(
        [paths, recursive, state](Session* session) {
            command::beginDeferred(session, "Hide paths", 1);

            command::runWorker([session, paths, recursive, state]() {
                bool success = false;
                QString error;

                {
                    WRITE_LOCKER(locker, session->stageLock(), "stageLock");
                    const UsdStageRefPtr stage = session->stageUnsafe();

                    if (!stage) {
                        error = "stage missing";
                    }
                    else {
                        QString rootError;
                        const SdfLayerHandle rootLayer = rootlayer::opened(stage, rootError);
                        if (!rootLayer) {
                            error = rootError;
                        }
                        else {
                            state->rootStates.clear();
                            const QList<SdfPath> affected = visibilityAffectedPaths(stage, paths, recursive, false);
                            state->rootStates.reserve(affected.size());
                            for (const SdfPath& path : affected) {
                                state->rootStates.append(
                                    captureRootPropertyState(rootLayer, path.AppendProperty(UsdGeomTokens->visibility)));
                            }

                            stage::setVisible(stage, paths, false, recursive);
                            success = true;
                        }
                    }
                }

                command::queueToSession(session, [session, paths, success, error]() {
                    using Status = Session::Notify::Status;
                    command::finishDeferred(session, success ? "Paths hidden" : appendError("Hide paths failed", error),
                                            paths, success ? Status::Success : Status::Error);
                });
            });
        },
        [state](Session* session) {
            command::beginDeferred(session, "Undo hide paths", 1);

            command::runWorker([session, state]() {
                bool success = true;
                QList<SdfPath> restoredPaths;
                QString error;

                {
                    WRITE_LOCKER(locker, session->stageLock(), "stageLock");
                    const UsdStageRefPtr stage = session->stageUnsafe();
                    QString rootError;
                    const SdfLayerHandle rootLayer = rootlayer::opened(stage, rootError);

                    if (!stage || !rootLayer) {
                        success = false;
                        error = !rootError.isEmpty() ? rootError : QStringLiteral("stage missing");
                    }
                    else {
                        for (const RootPropertyState& rootState : state->rootStates) {
                            if (!restoreRootPropertyState(rootLayer, rootState)) {
                                success = false;
                                error = QString("failed to restore visibility override: %1")
                                            .arg(pathText(rootState.propertyPath.GetPrimPath()));
                                break;
                            }
                            path::appendUnique(restoredPaths, rootState.propertyPath.GetPrimPath());
                        }
                    }
                }

                command::queueToSession(session, [session, restoredPaths, success, error]() {
                    using Status = Session::Notify::Status;
                    command::finishDeferred(session,
                                            success ? "Hide undone" : appendError("Undo hide paths failed", error),
                                            restoredPaths, success ? Status::Success : Status::Error);
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
                        const SdfLayerHandle rootLayer = rootlayer::opened(stage, rootError);
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
                        else if (!rootlayer::validatePrim(stage, path, error)) {}
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
                        const SdfLayerHandle rootLayer = rootlayer::opened(stage, rootError);
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
                        const SdfLayerHandle rootLayer = rootlayer::opened(stage, rootError);
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
                                if (rootlayer::validatePrim(stage, primPath, pathError)) {
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
                                if (!parentPath.IsEmpty()) {
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
                        const SdfLayerHandle rootLayer = rootlayer::opened(stage, rootError);
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
                        const SdfLayerHandle rootLayer = rootlayer::opened(stage, rootError);
                        if (!rootLayer) {
                            error = rootError;
                        }
                        else if (!rootlayer::validatePrim(stage, path, error)) {}
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

                                if (!state->parentPath.IsEmpty())
                                    stage::captureChildOrder(stage, state->parentPath, state->oldOrder);

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
                        const SdfLayerHandle rootLayer = rootlayer::opened(stage, rootError);
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
                            const SdfLayerHandle rootLayer = rootlayer::opened(stage, rootError);
                            if (!rootLayer) {
                                error = rootError;
                            }
                            else if (!rootlayer::validateParent(stage, parentPath, error)) {}
                            else {
                                const bool parentIsRoot = parentPath == SdfPath::AbsoluteRootPath();
                                stage::captureChildOrder(stage, parentPath, state->oldParentOrder);

                                UsdEditContext context(stage, UsdEditTarget(rootLayer));
                                const UsdGeomXform xform = UsdGeomXform::Define(stage, newPath);
                                if (!xform || !xform.GetPrim()) {
                                    error = "define failed";
                                }
                                else {
                                    state->newParentOrder = state->oldParentOrder;
                                    state->newParentOrder.push_back(newPath.GetNameToken());
                                    stage::restoreChildOrder(stage, parentPath, state->newParentOrder);

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
                                        if (!rootlayer::validatePrim(stage, movePath, authoredError)) {
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

                                        if (!state->oldMoveParentOrders.contains(oldParentPath)) {
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
                                        QList<QPair<SdfPath, SdfPath>> moves;
                                        moves.reserve(state->movedItems.size());
                                        for (const MoveItem& item : state->movedItems)
                                            moves.append(qMakePair(item.oldPath, item.newPath));

                                        QString moveError;
                                        if (!stage::movePrims(stage, moves, moveError)) {
                                            error = moveError.isEmpty() ? "failed to move selected paths" : moveError;
                                            stage::restoreChildOrders(stage, state->oldMoveParentOrders);
                                            stage::restoreChildOrder(stage, parentPath, state->oldParentOrder);
                                            stage::removePrimSpec(rootLayer, newPath);
                                            movedSelection = false;
                                        }
                                    }

                                    if (movedSelection && error.isEmpty()) {
                                        path::appendUnique(changed, parentPath);
                                        path::appendUnique(changed, newPath);

                                        for (const MoveItem& item : state->movedItems) {
                                            path::appendUnique(changed, item.oldParentPath);
                                            path::appendUnique(changed, item.oldPath);
                                            path::appendUnique(changed, item.newPath);
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

                {
                    WRITE_LOCKER(locker, session->stageLock(), "stageLock");
                    const UsdStageRefPtr stage = session->stageUnsafe();

                    if (!stage) {
                        hadStage = false;
                    }
                    else {
                        QString rootError;
                        const SdfLayerHandle rootLayer = rootlayer::opened(stage, rootError);
                        if (!rootLayer) {
                            error = rootError;
                        }
                        else {
                            UsdEditContext context(stage, UsdEditTarget(rootLayer));
                            QList<QPair<SdfPath, SdfPath>> reverseMoves;
                            reverseMoves.reserve(state->movedItems.size());
                            for (auto it = state->movedItems.crbegin(); it != state->movedItems.crend(); ++it)
                                reverseMoves.append(qMakePair(it->newPath, it->oldPath));

                            restored = stage::movePrims(stage, reverseMoves, error);

                            if (restored) {
                                for (auto it = state->oldMoveParentOrders.cbegin();
                                     it != state->oldMoveParentOrders.cend(); ++it) {
                                    stage::restoreChildOrder(stage, it.key(), it.value());
                                }

                                if (!state->parentPath.IsEmpty())
                                    stage::restoreChildOrder(stage, state->parentPath, state->oldParentOrder);

                                restored = stage::removePrimSpec(rootLayer, state->createdPath);
                            }

                            if (restored) {
                                path::appendUnique(changed, state->parentPath);
                                path::appendUnique(changed, state->createdPath);

                                for (const MoveItem& item : state->movedItems) {
                                    path::appendUnique(changed, item.oldParentPath);
                                    path::appendUnique(changed, item.oldPath);
                                    path::appendUnique(changed, item.newPath);
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
movePath(const QList<SdfPath>& paths, const SdfPath& newParentPath, int insertIndex, bool preserveTransform)
{
    struct MoveItem {
        SdfPath oldPath;
        SdfPath newPath;
        SdfPath oldParentPath;
        TfToken name;
        GfMatrix4d worldTransform { 1.0 };
        bool hasWorldTransform = false;
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
        [paths, newParentPath, insertIndex, preserveTransform, state](Session* session) {
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
                            const SdfLayerHandle rootLayer = rootlayer::opened(stage, rootError);
                            if (!rootLayer) {
                                error = rootError;
                            }
                            else if (!rootlayer::validateParent(stage, newParentPath, error)) {}
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
                                    if (!rootlayer::validatePrim(stage, path, authoredError)) {
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

                                    if (preserveTransform && oldParentPath != newParentPath) {
                                        const UsdPrim prim = stage->GetPrimAtPath(path);
                                        const UsdGeomXformable xformable(prim);

                                        if (xformable) {
                                            QString transformError;
                                            if (!stage::worldTransform(stage, path, item.worldTransform,
                                                                       transformError)) {
                                                error = transformError.isEmpty()
                                                            ? QString("failed to capture world transform: %1")
                                                                  .arg(qt::SdfPathToQString(path))
                                                            : transformError;
                                                valid = false;
                                                break;
                                            }
                                            item.hasWorldTransform = true;
                                        }
                                    }

                                    state->items.append(item);

                                    if (!state->oldParentOrders.contains(oldParentPath)) {
                                        TfTokenVector order;
                                        stage::captureChildOrder(stage, oldParentPath, order);
                                        state->oldParentOrders.insert(oldParentPath, order);
                                    }

                                    if (!state->oldParentOrders.contains(newParentPath)) {
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

                                    QList<QPair<SdfPath, SdfPath>> moves;
                                    moves.reserve(state->items.size());
                                    for (const MoveItem& item : state->items) {
                                        if (item.oldPath != item.newPath)
                                            moves.append(qMakePair(item.oldPath, item.newPath));
                                    }

                                    QString moveError;
                                    if (!stage::movePrims(stage, moves, moveError)) {
                                        error = moveError.isEmpty() ? "failed to move paths" : moveError;
                                        stage::restoreChildOrders(stage, state->oldParentOrders);
                                        moved = false;
                                    }

                                    if (error.isEmpty() && preserveTransform) {
                                        for (const MoveItem& item : state->items) {
                                            if (!item.hasWorldTransform)
                                                continue;

                                            QString transformError;
                                            if (!stage::setWorldTransform(stage, item.newPath, item.worldTransform,
                                                                          transformError)) {
                                                error = transformError.isEmpty()
                                                            ? QString("failed to preserve world transform: %1")
                                                                  .arg(qt::SdfPathToQString(item.newPath))
                                                            : transformError;
                                                break;
                                            }
                                        }
                                    }

                                    if (error.isEmpty()) {
                                        QSet<SdfPath> affectedParents;

                                        for (const MoveItem& item : state->items)
                                            affectedParents.insert(item.oldParentPath);

                                        affectedParents.insert(newParentPath);

                                        for (const SdfPath& parentPath : affectedParents) {
                                            TfTokenVector order = state->oldParentOrders.value(parentPath);
                                            TfTokenVector newOrder = path::removeTokens(order, movedNames);

                                            if (parentPath == newParentPath)
                                                newOrder = path::insertTokens(newOrder, movedNames, insertIndex);

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
        [state, preserveTransform](Session* session) {
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
                        const SdfLayerHandle rootLayer = rootlayer::opened(stage, rootError);
                        if (!rootLayer) {
                            error = rootError;
                        }
                        else {
                            UsdEditContext context(stage, UsdEditTarget(rootLayer));
                            QList<QPair<SdfPath, SdfPath>> reverseMoves;
                            reverseMoves.reserve(state->items.size());
                            for (auto it = state->items.crbegin(); it != state->items.crend(); ++it) {
                                if (it->oldPath != it->newPath)
                                    reverseMoves.append(qMakePair(it->newPath, it->oldPath));
                            }

                            restored = stage::movePrims(stage, reverseMoves, error);

                            if (restored && preserveTransform) {
                                for (const MoveItem& item : state->items) {
                                    if (!item.hasWorldTransform)
                                        continue;

                                    QString transformError;
                                    if (!stage::setWorldTransform(stage, item.oldPath, item.worldTransform,
                                                                  transformError)) {
                                        error = transformError.isEmpty()
                                                    ? QString("failed to restore world transform: %1")
                                                          .arg(qt::SdfPathToQString(item.oldPath))
                                                    : transformError;
                                        restored = false;
                                        break;
                                    }
                                }
                            }

                            if (restored) {
                                for (auto it = state->oldParentOrders.cbegin(); it != state->oldParentOrders.cend();
                                     ++it) {
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


Command
setAttributeValue(const SdfPath& attributePath, const VtValue& value)
{
    struct AttributeValueState {
        bool captured = false;
        bool hadRootDefault = false;
        VtValue previousRootDefault;
    };

    auto state = std::make_shared<AttributeValueState>();

    return Command(
        [attributePath, value, state](Session* session) {
            if (!session || attributePath.IsEmpty() || !attributePath.IsPropertyPath() || value.IsEmpty())
                return;

            command::beginDeferred(session, "Set attribute value", 1);

            command::runWorker([=]() {
                bool success = false;
                QString error;
                SdfPath primPath = attributePath.GetPrimPath();

                {
                    WRITE_LOCKER(locker, session->stageLock(), "stageLock");
                    const UsdStageRefPtr stage = session->stageUnsafe();

                    if (!stage) {
                        error = "stage missing";
                    }
                    else {
                        QString rootError;
                        const SdfLayerHandle rootLayer = rootlayer::opened(stage, rootError);

                        if (!rootLayer) {
                            error = rootError;
                        }
                        else {
                            const UsdAttribute attr = stage->GetAttributeAtPath(attributePath);
                            if (!attr) {
                                error = "attribute missing";
                            }
                            else {
                                if (!state->captured) {
                                    state->hadRootDefault = rootLayer->HasField(attributePath, SdfFieldKeys->Default);
                                    if (state->hadRootDefault)
                                        state->previousRootDefault = rootLayer->GetField(attributePath,
                                                                                         SdfFieldKeys->Default);
                                    state->captured = true;
                                }

                                UsdEditContext context(stage, UsdEditTarget(rootLayer));
                                success = attr.Set(value);
                                if (!success)
                                    error = "USD rejected the value for this attribute type";
                            }
                        }
                    }
                }

                command::queueToSession(session, [=]() {
                    using Status = Session::Notify::Status;
                    command::finishDeferred(session,
                                            success ? "Attribute value set"
                                                    : appendError("Set attribute value failed", error),
                                            { primPath }, success ? Status::Success : Status::Error);
                });
            });
        },
        [attributePath, state](Session* session) {
            if (!session || !state->captured || attributePath.IsEmpty())
                return;

            command::beginDeferred(session, "Undo set attribute value", 1);

            command::runWorker([=]() {
                bool success = false;
                QString error;
                const SdfPath primPath = attributePath.GetPrimPath();

                {
                    WRITE_LOCKER(locker, session->stageLock(), "stageLock");
                    const UsdStageRefPtr stage = session->stageUnsafe();

                    if (!stage) {
                        error = "stage missing";
                    }
                    else {
                        QString rootError;
                        const SdfLayerHandle rootLayer = rootlayer::opened(stage, rootError);

                        if (!rootLayer) {
                            error = rootError;
                        }
                        else {
                            if (state->hadRootDefault)
                                rootLayer->SetField(attributePath, SdfFieldKeys->Default, state->previousRootDefault);
                            else
                                rootLayer->EraseField(attributePath, SdfFieldKeys->Default);

                            success = true;
                        }
                    }
                }

                command::queueToSession(session, [=]() {
                    using Status = Session::Notify::Status;
                    command::finishDeferred(session,
                                            success ? "Attribute value undone"
                                                    : appendError("Undo attribute value failed", error),
                                            { primPath }, success ? Status::Success : Status::Error);
                });
            });
        });
}

Command
resetTransforms(const QList<SdfPath>& paths)
{
    struct ResetTransformState {
        struct Item {
            SdfPath path;
            RootPropertyState orderState;
            RootPropertyState matrixState;
        };
        QList<Item> items;
    };

    auto state = std::make_shared<ResetTransformState>();

    return Command(
        [paths, state](Session* session) {
            if (!session || paths.isEmpty())
                return;

            command::beginDeferred(session, "Reset xform", static_cast<int>(paths.size()));

            command::runWorker([session, paths, state]() {
                QList<SdfPath> affected;
                QStringList errors;

                {
                    WRITE_LOCKER(locker, session->stageLock(), "stageLock");
                    const UsdStageRefPtr stage = session->stageUnsafe();

                    if (!stage) {
                        errors.append("stage missing");
                    }
                    else {
                        QString rootError;
                        const SdfLayerHandle rootLayer = rootlayer::opened(stage, rootError);

                        if (!rootLayer) {
                            errors.append(rootError);
                        }
                        else {
                            state->items.clear();

                            for (const SdfPath& inputPath : path::uniquePaths(paths)) {
                                const SdfPath primPath =
                                    inputPath.IsPropertyPath() ? inputPath.GetPrimPath() : inputPath;
                                const UsdPrim prim = stage->GetPrimAtPath(primPath);
                                const UsdGeomXformable xformable(prim);

                                if (!prim || !prim.IsValid() || prim.IsInstanceProxy() || !xformable) {
                                    errors.append(QString("prim is not transformable: %1").arg(pathText(primPath)));
                                    continue;
                                }

                                const SdfPath orderPath = primPath.AppendProperty(TfToken("xformOpOrder"));
                                const SdfPath matrixPath = primPath.AppendProperty(TfToken("xformOp:transform"));

                                ResetTransformState::Item item;
                                item.path = primPath;
                                item.orderState = captureRootPropertyState(rootLayer, orderPath);
                                item.matrixState = captureRootPropertyState(rootLayer, matrixPath);
                                state->items.append(item);

                                bool success = false;

                                if (hasUnderlyingTransformOpinion(stage, prim, rootLayer)) {
                                    success = removePropertySpec(rootLayer, orderPath)
                                              && removePropertySpec(rootLayer, matrixPath);
                                }
                                else {
                                    UsdEditContext context(stage, UsdEditTarget(rootLayer));
                                    UsdGeomXformOp op = xformable.MakeMatrixXform();
                                    success = op && op.Set(GfMatrix4d(1.0), UsdTimeCode::Default());
                                }

                                if (success)
                                    affected.append(primPath);
                                else
                                    errors.append(QString("failed to reset xform: %1").arg(pathText(primPath)));
                            }
                        }
                    }
                }

                const bool success = errors.isEmpty();
                command::queueToSession(session, [session, affected, errors, success]() {
                    using Status = Session::Notify::Status;
                    const QString message = success
                                                ? QStringLiteral("Xform reset")
                                                : appendError("Reset xform finished with errors",
                                                              summarizeErrors(errors));
                    command::finishDeferred(session, message, affected,
                                            success ? Status::Success : Status::Error);
                });
            });
        },
        [state](Session* session) {
            if (!session || state->items.isEmpty())
                return;

            command::beginDeferred(session, "Undo reset xform", static_cast<int>(state->items.size()));

            command::runWorker([session, state]() {
                QList<SdfPath> restored;
                QStringList errors;

                {
                    WRITE_LOCKER(locker, session->stageLock(), "stageLock");
                    const UsdStageRefPtr stage = session->stageUnsafe();

                    if (!stage) {
                        errors.append("stage missing");
                    }
                    else {
                        QString rootError;
                        const SdfLayerHandle rootLayer = rootlayer::opened(stage, rootError);

                        if (!rootLayer) {
                            errors.append(rootError);
                        }
                        else {
                            for (const ResetTransformState::Item& item : state->items) {
                                const bool success = restoreRootPropertyState(rootLayer, item.orderState)
                                                     && restoreRootPropertyState(rootLayer, item.matrixState);

                                if (success)
                                    restored.append(item.path);
                                else
                                    errors.append(QString("failed to restore xform: %1").arg(pathText(item.path)));
                            }
                        }
                    }
                }

                const bool success = errors.isEmpty();
                command::queueToSession(session, [session, restored, errors, success]() {
                    using Status = Session::Notify::Status;
                    const QString message = success
                                                ? QStringLiteral("Reset xform undone")
                                                : appendError("Undo reset xform finished with errors",
                                                              summarizeErrors(errors));
                    command::finishDeferred(session, message, restored,
                                            success ? Status::Success : Status::Error);
                });
            });
        });
}

Command
setTransforms(const QList<SdfPath>& paths, const QList<GfMatrix4d>& before, const QList<GfMatrix4d>& after,
              const QList<TransformRootState>& rootBefore)
{
    auto applyMatrices = [](Session* session, const QList<SdfPath>& paths, const QList<GfMatrix4d>& matrices,
                            const QString& title, const QString& successMessage, const QString& failureMessage) {
        if (!session || paths.isEmpty() || paths.size() != matrices.size())
            return;

        command::beginDeferred(session, title, static_cast<int>(paths.size()));

        command::runWorker([session, paths, matrices, successMessage, failureMessage]() {
            bool success = true;
            QStringList errors;
            QList<SdfPath> changed;
            {
                WRITE_LOCKER(locker, session->stageLock(), "stageLock");
                const UsdStageRefPtr stage = session->stageUnsafe();

                if (!stage) {
                    success = false;
                    errors.append("stage missing");
                }
                else {
                    for (qsizetype i = 0; i < paths.size(); ++i) {
                        const SdfPath& path = paths.at(i);
                        const GfMatrix4d& matrix = matrices.at(i);

                        QString error;
                        if (!stage::setWorldTransform(stage, path, matrix, error)) {
                            success = false;
                            errors.append(error.isEmpty() ? QString("failed: %1").arg(qt::SdfPathToQString(path))
                                                          : error);
                            continue;
                        }

                        path::appendUnique(changed, path);
                    }
                }
            }

            const QString errorText = summarizeErrors(errors);
            command::queueToSession(session, [session, changed, success, successMessage, failureMessage, errorText]() {
                using Status = Session::Notify::Status;
                command::finishDeferred(session, success ? successMessage : appendError(failureMessage, errorText),
                                        changed, success ? Status::Success : Status::Error);
            });
        });
    };

    return Command(
        [paths, after, applyMatrices](Session* session) {
            applyMatrices(session, paths, after, "Transform paths", "Paths transformed", "Transform paths failed");
        },
        [paths, before, rootBefore, applyMatrices](Session* session) {
            if (rootBefore.size() != paths.size()) {
                applyMatrices(session, paths, before, "Undo transform paths", "Transform undone",
                              "Undo transform failed");
                return;
            }

            command::beginDeferred(session, "Undo transform paths", static_cast<int>(paths.size()));
            command::runWorker([session, paths, rootBefore]() {
                bool success = true;
                QStringList errors;
                QList<SdfPath> changed;

                {
                    WRITE_LOCKER(locker, session->stageLock(), "stageLock");
                    const UsdStageRefPtr stage = session->stageUnsafe();
                    if (!stage) {
                        success = false;
                        errors.append("stage missing");
                    }
                    else {
                        for (qsizetype i = 0; i < paths.size(); ++i) {
                            QString error;
                            if (!restoreTransformRootState(stage, paths.at(i), rootBefore.at(i), error)) {
                                success = false;
                                errors.append(error.isEmpty() ? QString("failed: %1").arg(pathText(paths.at(i)))
                                                              : error);
                                continue;
                            }
                            path::appendUnique(changed, paths.at(i));
                        }
                    }
                }

                const QString errorText = summarizeErrors(errors);
                command::queueToSession(session, [session, changed, success, errorText]() {
                    using Status = Session::Notify::Status;
                    command::finishDeferred(session,
                                            success ? "Transform undone"
                                                    : appendError("Undo transform failed", errorText),
                                            changed, success ? Status::Success : Status::Error);
                });
            });
        });
}



}  // namespace stageviz
