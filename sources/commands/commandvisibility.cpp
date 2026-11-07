// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#include "commandvisibility.h"
#include "commandutils.h"
#include "qtutils.h"
#include "tracelocks.h"
#include "usdutils.h"
#include <QSet>
#include <algorithm>
#include <limits>
#include <memory>
#include <pxr/usd/sdf/layer.h>
#include <pxr/usd/usd/prim.h>
#include <pxr/usd/usdGeom/imageable.h>
#include <pxr/usd/usdGeom/tokens.h>

namespace stageviz {

namespace {
    using command::appendError;
    using command::captureRootPropertyState;
    using command::currentEditLayer;
    using command::pathText;
    using command::restoreRootPropertyState;
    using command::RootPropertyState;

    int progressBlockCount(qsizetype itemCount)
    {
        return static_cast<int>(std::clamp<qsizetype>(itemCount, 1, std::numeric_limits<int>::max()));
    }

    void partitionEditablePrimPaths(const UsdStageRefPtr& stage, const QList<SdfPath>& paths,
                                    QList<SdfPath>& editablePaths, QList<SdfPath>& skippedProxyPaths,
                                    QList<SdfPath>& skippedInvalidPaths)
    {
        editablePaths.clear();
        skippedProxyPaths.clear();
        skippedInvalidPaths.clear();
        editablePaths.reserve(paths.size());
        for (const SdfPath& path : paths) {
            const SdfPath primPath = path.IsPropertyPath() ? path.GetPrimPath() : path;
            const UsdPrim prim = stage->GetPrimAtPath(primPath);
            if (!prim || !prim.IsValid())
                skippedInvalidPaths.append(path);
            else if (prim.IsInstanceProxy())
                skippedProxyPaths.append(path);
            else
                editablePaths.append(path);
        }
    }

    QList<command::Result> visibilityResults(const QList<SdfPath>& editablePaths,
                                             const QList<SdfPath>& skippedProxyPaths,
                                             const QList<SdfPath>& skippedInvalidPaths, const QString& verb)
    {
        QList<command::Result> results;
        results.reserve(editablePaths.size() + skippedProxyPaths.size() + skippedInvalidPaths.size());

        const auto append = [&results](const QList<SdfPath>& paths, const QString& prefix,
                                       Session::Notify::Status status) {
            for (const SdfPath& path : paths) {
                command::Result result;
                result.message = QString("%1: %2").arg(prefix, pathText(path));
                result.path = path;
                result.status = status;
                result.success = status == Session::Notify::Status::Success;
                results.append(result);
            }
        };

        append(path::uniquePaths(editablePaths), verb, Session::Notify::Status::Success);
        append(path::uniquePaths(skippedProxyPaths), QStringLiteral("Read-only instance proxy"),
               Session::Notify::Status::Warning);
        append(path::uniquePaths(skippedInvalidPaths), QStringLiteral("Invalid prim path"),
               Session::Notify::Status::Warning);
        return results;
    }

    QList<SdfPath> visibilityAffectedPaths(UsdStageRefPtr stage, const QList<SdfPath>& paths, bool recursive,
                                           bool makeVisible)
    {
        QList<SdfPath> result;
        if (!stage)
            return result;

        path::PathCollector collector(result);

        auto appendImageable = [&](const UsdPrim& prim) {
            if (!prim || prim.IsInstanceProxy() || !UsdGeomImageable(prim))
                return;

            collector.append(prim.GetPath());
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
}  // namespace

Command
showPaths(const QList<SdfPath>& paths, bool recursive)
{
    struct VisibilityCommandState {
        QList<RootPropertyState> rootStates;
    };

    auto state = std::make_shared<VisibilityCommandState>();

    return Command(
        [paths, recursive, state](Session* session) {
            const QList<SdfPath> inputPaths = path::uniquePaths(paths);
            command::beginDeferred(session, QStringLiteral("Show paths"), progressBlockCount(inputPaths.size()));

            command::runWorker([session, inputPaths, recursive, state]() {
                bool success = false;
                QString error;
                QList<SdfPath> editablePaths;
                QList<SdfPath> skippedProxyPaths;
                QList<SdfPath> skippedInvalidPaths;

                {
                    WRITE_LOCKER(locker, session->stageLock(), "stageLock");
                    const UsdStageRefPtr stage = session->stageUnsafe();

                    if (!stage) {
                        error = "stage missing";
                    }
                    else {
                        QString editError;
                        const SdfLayerHandle editLayer = currentEditLayer(stage, editError);
                        if (!editLayer) {
                            error = editError;
                        }
                        else {
                            partitionEditablePrimPaths(stage, stage::objectPaths(stage, inputPaths), editablePaths,
                                                       skippedProxyPaths, skippedInvalidPaths);
                            state->rootStates.clear();
                            const QList<SdfPath> affected = visibilityAffectedPaths(stage, editablePaths, recursive,
                                                                                    true);
                            state->rootStates.reserve(affected.size());
                            for (const SdfPath& path : affected) {
                                state->rootStates.append(
                                    captureRootPropertyState(editLayer, path.AppendProperty(UsdGeomTokens->visibility)));
                            }

                            stage::setVisible(stage, editablePaths, true, recursive);
                            success = true;
                        }
                    }
                }

                QList<command::Result> results;
                if (success) {
                    results = visibilityResults(editablePaths, skippedProxyPaths, skippedInvalidPaths,
                                                QStringLiteral("Prim shown"));
                }
                else {
                    command::Result result;
                    result.message = appendError(QStringLiteral("Show paths failed"), error);
                    result.status = Session::Notify::Status::Error;
                    results.append(result);
                }

                if (results.isEmpty()) {
                    command::Result result;
                    result.message = QStringLiteral("No paths to show");
                    result.status = Session::Notify::Status::Success;
                    results.append(result);
                }

                command::queueToSession(session, [session, results]() {
                    session->setPrimsUpdate(Session::PrimsUpdate::Immediate);
                    command::flushResults(session, results, static_cast<int>(results.size()));
                    session->endProgressBlock();
                });
            });
        },
        [state](Session* session) {
            command::beginDeferred(session, "Undo show paths", 1);

            command::runWorker([session, state]() {
                bool success = true;
                QList<SdfPath> restoredPaths;
                path::PathCollector restoredPathsCollector(restoredPaths);
                QString error;

                {
                    WRITE_LOCKER(locker, session->stageLock(), "stageLock");
                    const UsdStageRefPtr stage = session->stageUnsafe();
                    QString editError;
                    const SdfLayerHandle editLayer = currentEditLayer(stage, editError);

                    if (!stage || !editLayer) {
                        success = false;
                        error = !editError.isEmpty() ? editError : QStringLiteral("stage missing");
                    }
                    else {
                        for (const RootPropertyState& rootState : state->rootStates) {
                            if (!restoreRootPropertyState(editLayer, rootState)) {
                                success = false;
                                error = QString("failed to restore visibility override: %1")
                                            .arg(pathText(rootState.propertyPath.GetPrimPath()));
                                break;
                            }
                            restoredPathsCollector.append(rootState.propertyPath.GetPrimPath());
                        }
                    }
                }

                command::queueToSession(session, [session, restoredPaths, success, error]() {
                    using Status = Session::Notify::Status;
                    command::finishDeferred(session,
                                            success ? QStringLiteral("Show undone")
                                                    : appendError(QStringLiteral("Undo show paths failed"), error),
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
            const QList<SdfPath> inputPaths = path::uniquePaths(paths);
            command::beginDeferred(session, QStringLiteral("Hide paths"), progressBlockCount(inputPaths.size()));

            command::runWorker([session, inputPaths, recursive, state]() {
                bool success = false;
                QString error;
                QList<SdfPath> editablePaths;
                QList<SdfPath> skippedProxyPaths;
                QList<SdfPath> skippedInvalidPaths;

                {
                    WRITE_LOCKER(locker, session->stageLock(), "stageLock");
                    const UsdStageRefPtr stage = session->stageUnsafe();

                    if (!stage) {
                        error = "stage missing";
                    }
                    else {
                        QString editError;
                        const SdfLayerHandle editLayer = currentEditLayer(stage, editError);
                        if (!editLayer) {
                            error = editError;
                        }
                        else {
                            partitionEditablePrimPaths(stage, stage::objectPaths(stage, inputPaths), editablePaths,
                                                       skippedProxyPaths, skippedInvalidPaths);
                            state->rootStates.clear();
                            const QList<SdfPath> affected = visibilityAffectedPaths(stage, editablePaths, recursive,
                                                                                    false);
                            state->rootStates.reserve(affected.size());
                            for (const SdfPath& path : affected) {
                                state->rootStates.append(
                                    captureRootPropertyState(editLayer, path.AppendProperty(UsdGeomTokens->visibility)));
                            }

                            stage::setVisible(stage, editablePaths, false, recursive);
                            success = true;
                        }
                    }
                }

                QList<command::Result> results;
                if (success) {
                    results = visibilityResults(editablePaths, skippedProxyPaths, skippedInvalidPaths,
                                                QStringLiteral("Prim hidden"));
                }
                else {
                    command::Result result;
                    result.message = appendError(QStringLiteral("Hide paths failed"), error);
                    result.status = Session::Notify::Status::Error;
                    results.append(result);
                }

                if (results.isEmpty()) {
                    command::Result result;
                    result.message = QStringLiteral("No paths to hide");
                    result.status = Session::Notify::Status::Success;
                    results.append(result);
                }

                command::queueToSession(session, [session, results]() {
                    session->setPrimsUpdate(Session::PrimsUpdate::Immediate);
                    command::flushResults(session, results, static_cast<int>(results.size()));
                    session->endProgressBlock();
                });
            });
        },
        [state](Session* session) {
            command::beginDeferred(session, "Undo hide paths", 1);

            command::runWorker([session, state]() {
                bool success = true;
                QList<SdfPath> restoredPaths;
                path::PathCollector restoredPathsCollector(restoredPaths);
                QString error;

                {
                    WRITE_LOCKER(locker, session->stageLock(), "stageLock");
                    const UsdStageRefPtr stage = session->stageUnsafe();
                    QString editError;
                    const SdfLayerHandle editLayer = currentEditLayer(stage, editError);

                    if (!stage || !editLayer) {
                        success = false;
                        error = !editError.isEmpty() ? editError : QStringLiteral("stage missing");
                    }
                    else {
                        for (const RootPropertyState& rootState : state->rootStates) {
                            if (!restoreRootPropertyState(editLayer, rootState)) {
                                success = false;
                                error = QString("failed to restore visibility override: %1")
                                            .arg(pathText(rootState.propertyPath.GetPrimPath()));
                                break;
                            }
                            restoredPathsCollector.append(rootState.propertyPath.GetPrimPath());
                        }
                    }
                }

                command::queueToSession(session, [session, restoredPaths, success, error]() {
                    using Status = Session::Notify::Status;
                    command::finishDeferred(session,
                                            success ? QStringLiteral("Hide undone")
                                                    : appendError(QStringLiteral("Undo hide paths failed"), error),
                                            restoredPaths, success ? Status::Success : Status::Error);
                });
            });
        });
}

}  // namespace stageviz
