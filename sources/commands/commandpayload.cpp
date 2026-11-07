// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#include "commandpayload.h"
#include "commandutils.h"
#include "qtutils.h"
#include "selectionlist.h"
#include "tracelocks.h"
#include "usdutils.h"
#include <algorithm>
#include <memory>
#include <pxr/usd/usd/prim.h>
#include <pxr/usd/usd/stageLoadRules.h>

namespace stageviz {

namespace payload {
    struct State {
        QList<PayloadState> payloadStates;
        QList<SdfPath> previousSelection;
        QList<SdfPath> previousMask;
        UsdStageWeakPtr loadRulesStage;
        UsdStageLoadRules previousLoadRules;
    };
}  // namespace payload


namespace {

    void captureLoadRules(Session* session, const std::shared_ptr<payload::State>& state)
    {
        READ_LOCKER(locker, session->stageLock(), "stageLock");
        const UsdStageRefPtr stage = session->stageUnsafe();
        state->loadRulesStage = stage;
        if (stage)
            state->previousLoadRules = stage->GetLoadRules();
    }

    UsdStageLoadRules::Rule inheritedRule(const UsdStageLoadRules& rules, const SdfPath& path)
    {
        // Unlike GetEffectiveRuleForPath(), do not promote ancestors because
        // of loaded descendants: their explicit overrides are replayed later.
        const auto& entries = rules.GetRules();
        const auto it = SdfPathFindLongestPrefix(entries.begin(), entries.end(), path,
                                                [](const auto& entry) -> const SdfPath& { return entry.first; });
        if (it == entries.end())
            return UsdStageLoadRules::AllRule;
        if (it->second == UsdStageLoadRules::OnlyRule && it->first != path)
            return UsdStageLoadRules::NoneRule;
        return it->second;
    }

    void restoreLoadRules(Session* session, const std::shared_ptr<payload::State>& state)
    {
        WRITE_LOCKER(locker, session->stageLock(), "stageLock");
        const UsdStageRefPtr stage = session->stageUnsafe();
        if (!stage || state->loadRulesStage != stage || stage->GetLoadRules() == state->previousLoadRules)
            return;

        // SetLoadRules() sends a root resync in USD 25.11. Restore changed
        // prefixes instead, visiting parents before their saved child overrides.
        QList<SdfPath> scopes;
        scopes.reserve(state->payloadStates.size());
        path::PathCollector collector(scopes);
        for (const auto& payloadState : state->payloadStates)
            collector.append(payloadState.path);

        SdfPathVector paths;
        const auto appendRules = [&](const UsdStageLoadRules& rules, const SdfPath& scope) {
            const auto& entries = rules.GetRules();
            const auto range = SdfPathFindPrefixedRange(entries.begin(), entries.end(), scope,
                                                       [](const auto& entry) -> const SdfPath& { return entry.first; });
            for (auto it = range.first; it != range.second; ++it)
                paths.push_back(it->first);
        };
        for (const SdfPath& scope : path::stableRootPaths(scopes)) {
            paths.push_back(scope);
            appendRules(state->previousLoadRules, scope);
            appendRules(stage->GetLoadRules(), scope);
        }
        std::sort(paths.begin(), paths.end());
        paths.erase(std::unique(paths.begin(), paths.end()), paths.end());
        for (const SdfPath& path : paths) {
            const auto wanted = inheritedRule(state->previousLoadRules, path);
            if (inheritedRule(stage->GetLoadRules(), path) == wanted)
                continue;
            if (wanted == UsdStageLoadRules::NoneRule)
                stage->Unload(path);
            else
                stage->Load(path, wanted == UsdStageLoadRules::OnlyRule ? UsdLoadWithoutDescendants
                                                                      : UsdLoadWithDescendants);
        }
    }

    QList<SdfPath> remapPayloadAffectedPaths(const QList<SdfPath>& paths, const QList<SdfPath>& payloadPaths)
    {
        QList<SdfPath> result;
        result.reserve(paths.size());
        path::PathCollector collector(result);

        for (const SdfPath& inputPath : paths) {
            const SdfPath primPath = inputPath.IsPropertyPath() ? inputPath.GetPrimPath() : inputPath;
            bool remapped = false;

            for (const SdfPath& payloadPath : payloadPaths) {
                if (primPath == payloadPath || primPath.HasPrefix(payloadPath)) {
                    collector.append(payloadPath);
                    remapped = true;
                    break;
                }
            }

            if (!remapped)
                collector.append(inputPath);
        }

        return result;
    }

    QList<SdfPath> remapMissingPayloadDescendants(UsdStageRefPtr stage, const QList<SdfPath>& paths,
                                                  const QList<SdfPath>& payloadPaths)
    {
        if (!stage || payloadPaths.isEmpty())
            return paths;

        QList<SdfPath> result;
        result.reserve(paths.size());
        path::PathCollector collector(result);

        for (const SdfPath& inputPath : paths) {
            const SdfPath primPath = inputPath.IsPropertyPath() ? inputPath.GetPrimPath() : inputPath;
            const UsdPrim prim = stage->GetPrimAtPath(primPath);

            if (prim && prim.IsValid()) {
                collector.append(inputPath);
                continue;
            }

            bool remapped = false;
            for (const SdfPath& payloadPath : payloadPaths) {
                if (primPath.HasPrefix(payloadPath)) {
                    collector.append(payloadPath);
                    remapped = true;
                    break;
                }
            }

            if (!remapped)
                collector.append(inputPath);
        }

        return result;
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
            captureLoadRules(session, state);

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

                QList<SdfPath> loadedPaths;
                loadedPaths.reserve(payloadStates.size());
                path::PathCollector loadedCollector(loadedPaths);
                for (const payload::PayloadState& payloadState : payloadStates)
                    loadedCollector.append(payloadState.path);

                QList<SdfPath> selection = state->previousSelection;
                QList<SdfPath> mask = state->previousMask;

                if (useVariant && !loadedPaths.isEmpty()) {
                    READ_LOCKER(locker, session->stageLock(), "stageLock");
                    const UsdStageRefPtr stage = session->stageUnsafe();
                    selection = remapMissingPayloadDescendants(stage, selection, loadedPaths);
                    mask = remapMissingPayloadDescendants(stage, mask, loadedPaths);
                }

                command::queueToSession(session, [session, selection, mask, useVariant]() {
                    if (useVariant) {
                        session->selectionList()->updatePaths(selection);
                        session->setMask(mask);
                    }
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
                // Undo in reverse execution order while child targets still exist.
                for (auto it = state->payloadStates.crbegin(); it != state->payloadStates.crend(); ++it) {
                    const payload::PayloadState& payloadState = *it;
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

                // A parent's loaded bit cannot represent nested exclusions or
                // explicit child loads. Keep one exact rule snapshot per command.
                if (!session->isProgressBlockCancelled())
                    restoreLoadRules(session, state);

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
loadNeighborPayloads(const QList<SdfPath>& paths)
{
    auto state = std::make_shared<payload::State>();

    return Command(
        [paths, state](Session* session) {
            if (!session || paths.isEmpty())
                return;

            state->previousSelection = session->selectionList()->paths();
            state->previousMask = session->mask();
            captureLoadRules(session, state);
            state->payloadStates.clear();

            command::runWorker([session, paths, state]() {
                QList<SdfPath> targets;

                {
                    READ_LOCKER(locker, session->stageLock(), "stageLock");

                    const UsdStageRefPtr stage = session->stageUnsafe();
                    if (stage) {
                        targets = payload::neighboringPaths(stage, paths);

                        const QList<SdfPath> sourcePaths = stage::outermostPayloadPaths(stage, paths);
                        path::PathCollector collector(targets);
                        for (const SdfPath& sourcePath : sourcePaths) {
                            if (!stage::isLoaded(stage, sourcePath))
                                collector.append(sourcePath);
                        }

                        QList<SdfPath> unloadedTargets;
                        unloadedTargets.reserve(targets.size());

                        for (const SdfPath& target : targets) {
                            if (!stage::isLoaded(stage, target))
                                unloadedTargets.append(target);
                        }

                        targets = unloadedTargets;
                    }
                }

                if (targets.isEmpty()) {
                    command::queueToSession(session, [session]() {
                        using Status = Session::Notify::Status;

                        session->beginProgressBlock("Load neighbor payloads", 1);
                        session->updateProgressNotify(Session::Notify("No unloaded neighbor payloads found", {},
                                                                      Status::Success),
                                                      1);
                        session->endProgressBlock();
                    });
                    return;
                }

                command::queueToSession(session, [session, state, targets]() {
                    session->beginProgressBlock("Load neighbor payloads", targets.size());
                    session->setPrimsUpdate(Session::PrimsUpdate::Deferred);
                    session->updateProgressNotify(
                        Session::Notify(QString("Loading %1 neighbor payloads").arg(targets.size()), {},
                                        Session::Notify::Status::Success),
                        0);

                    command::runWorker([session, state, targets]() {
                        QList<command::Result> pending;
                        pending.reserve(16);

                        QList<payload::PayloadState> payloadStates;
                        payloadStates.reserve(targets.size());

                        int completed = 0;

                        for (const SdfPath& path : targets) {
                            if (!session || session->isProgressBlockCancelled())
                                break;

                            command::Result result;
                            result.path = path;
                            result.message = "Neighbor payload failed";
                            result.status = Session::Notify::Status::Error;

                            payload::PayloadState payloadState;
                            QString loadError;

                            try {
                                WRITE_LOCKER(locker, session->stageLock(), "stageLock");

                                const UsdStageRefPtr stage = session->stageUnsafe();

                                if (!stage) {
                                    result.success = false;
                                    loadError = "Stage not available";
                                }
                                else {
                                    result.success = payload::applyLoad(stage, path, false, std::string(),
                                                                        std::string(), payloadState, loadError);
                                }
                            } catch (...) {
                                result.success = false;
                                loadError = "exception";
                            }

                            result.message = result.success
                                                 ? "Neighbor payload loaded"
                                                 : (loadError.isEmpty() ? "Neighbor payload failed" : loadError);

                            result.status = result.success ? Session::Notify::Status::Success
                                                           : Session::Notify::Status::Error;

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

                        state->payloadStates = payloadStates;

                        command::queueToSession(session, [session, pending, completed]() {
                            if (!pending.isEmpty())
                                command::flushResults(session, pending, completed);

                            session->setPrimsUpdate(Session::PrimsUpdate::Immediate);
                            session->endProgressBlock();
                        });
                    });
                });
            });
        },
        [state](Session* session) {
            if (!session || state->payloadStates.isEmpty())
                return;

            session->beginProgressBlock("Undo load neighbor payloads", state->payloadStates.size());
            session->setPrimsUpdate(Session::PrimsUpdate::Deferred);

            command::runWorker([session, state]() {
                QList<command::Result> pending;
                pending.reserve(16);

                int completed = 0;

                // Undo in reverse execution order while child targets still exist.
                for (auto it = state->payloadStates.crbegin(); it != state->payloadStates.crend(); ++it) {
                    const payload::PayloadState& payloadState = *it;
                    if (!session || session->isProgressBlockCancelled())
                        break;

                    command::Result result;
                    result.path = payloadState.path;
                    result.message = "Neighbor payload undo failed";
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

                    result.message = result.success ? "Neighbor payload restored"
                                                    : (error.isEmpty() ? "Neighbor payload undo failed" : error);

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

                // A parent's loaded bit cannot represent nested exclusions or
                // explicit child loads. Keep one exact rule snapshot per command.
                if (!session->isProgressBlockCancelled())
                    restoreLoadRules(session, state);

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
            captureLoadRules(session, state);

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
                        remapPayloadAffectedPaths(state->previousSelection, unloadedPaths));
                    session->setMask(remapPayloadAffectedPaths(state->previousMask, unloadedPaths));
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
                // Undo in reverse execution order while child targets still exist.
                for (auto it = state->payloadStates.crbegin(); it != state->payloadStates.crend(); ++it) {
                    const payload::PayloadState& payloadState = *it;
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

                // A parent's loaded bit cannot represent nested exclusions or
                // explicit child loads. Keep one exact rule snapshot per command.
                if (!session->isProgressBlockCancelled())
                    restoreLoadRules(session, state);

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

}  // namespace stageviz
