// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#pragma once

#include "command.h"
#include "session.h"
#include "stageviz.h"
#include <QList>
#include <QThreadPool>
#include <utility>

PXR_NAMESPACE_USING_DIRECTIVE

namespace stageviz {
namespace command {
    /**
     * @brief Formats a USD path for command progress and error messages.
     */
    QString pathText(const SdfPath& path);

    /**
     * @brief Appends a nonempty error to a command message.
     */
    QString appendError(const QString& message, const QString& error);

    /**
     * @brief Returns the active edit layer or reports a missing stage or layer.
     */
    SdfLayerHandle currentEditLayer(const UsdStageRefPtr& stage, QString& error);

    /**
     * @brief Joins command errors into a bounded multiline notification.
     *
     * Large batches retain a representative prefix and an omitted-error count
     * rather than placing every failed path into the UI's status message.
     */
    QString summarizeErrors(const QStringList& errors);

    /**
     * @brief Removes an authored property spec, succeeding when already absent.
     */
    bool removePropertySpec(const SdfLayerHandle& layer, const SdfPath& propertyPath);


    /**
     * @struct Result
     * @brief Result item used for per-path command progress updates.
     *
     * Stores the path affected by one command operation together with its user
     * visible message and status. The command layer can collect these results
     * on a worker thread and flush them to the Session progress UI in batches.
     */
    struct Result {
        SdfPath path;
        bool success = false;
        QString message;
        Session::Notify::Status status = Session::Notify::Status::Success;
    };

    /**
     * @brief Queues a callback on the session thread.
     *
     * This is used by asynchronous commands to return from worker-thread work
     * back to the owning QObject thread before touching UI-facing session state.
     *
     * If @p session is null, no callback is queued.
     *
     * @param session Session used as the queued invocation target.
     * @param callback Callback to execute on the session thread.
     */
    template<typename Callback> void queueToSession(Session* session, Callback&& callback)
    {
        if (!session)
            return;

        QMetaObject::invokeMethod(session, std::forward<Callback>(callback), Qt::QueuedConnection);
    }

    /**
     * @brief Starts command work on the global thread pool.
     *
     * This centralizes command worker dispatch without changing command behavior.
     *
     * @param worker Worker callback to execute asynchronously.
     */
    template<typename Worker> void runWorker(Worker&& worker)
    {
        QThreadPool::globalInstance()->start(std::forward<Worker>(worker));
    }

    /**
     * @brief Flushes a batch of progress results to a session.
     *
     * Updates the active progress block with one notification per result. The
     * @p completed value is the total completed count after the batch has been
     * produced; the function derives the first progress index from the batch size.
     *
     * @param session Session receiving progress notifications.
     * @param results Batched command results to report.
     * @param completed Total completed operation count after this batch.
     */
    void flushResults(Session* session, const QList<Result>& results, int completed);

    /**
     * @brief Appends a result to a pending batch and flushes when the batch is full.
     *
     * Adds @p result to @p pending and queues the batch when it reaches
     * @p batchSize. The pending list is cleared after it has been queued.
     *
     * @param session Session receiving progress notifications.
     * @param pending Pending result batch.
     * @param result Result to append.
     * @param completed Total completed operation count after this result.
     * @param batchSize Maximum batch size before queued flushing.
     */
    void appendResult(Session* session, QList<Result>& pending, const Result& result, int completed,
                      int batchSize = 16);

    /**
     * @brief Begins a deferred command update.
     *
     * Starts a progress block and switches the session into deferred prim updates.
     * Commands should call finishDeferred() when the operation completes.
     *
     * @param session Session executing the command.
     * @param name Progress block title.
     * @param count Total number of progress steps.
     */
    void beginDeferred(Session* session, const QString& name, int count);

    /**
     * @brief Completes a deferred command update.
     *
     * Restores immediate prim updates, posts the final progress notification,
     * and ends the active progress block. The complete path list is retained;
     * the console widget applies its own row limit when displaying output.
     *
     * @param session Session executing the command.
     * @param message Progress notification message.
     * @param paths Paths associated with the completed operation.
     * @param status Result status for the notification.
     * @param completed Completed progress step.
     */
    void finishDeferred(Session* session, const QString& message, const QList<SdfPath>& paths,
                        Session::Notify::Status status, int completed = 1);

    /**
     * @brief Posts a progress notification with all supplied paths.
     *
     * The console widget applies its own row limit when displaying output.
     */
    void updateProgressNotify(Session* session, const QString& message, const QList<SdfPath>& paths,
                              Session::Notify::Status status, size_t completed);

    /**
     * @brief Queues any remaining pending progress results.
     *
     * If @p pending is not empty, queues it for flushing to the session progress UI
     * and clears the pending list.
     *
     * @param session Session receiving progress notifications.
     * @param pending Pending result batch.
     * @param completed Total completed operation count after this batch.
     */
    void flushPendingResults(Session* session, QList<Result>& pending, int completed);

    /**
     * @brief Queues a batch of progress results onto the session thread.
     *
     * Copies the current result batch and invokes flushResults() through a queued
     * Qt call. This is intended for worker-thread command loops that periodically
     * report progress without touching UI/session state directly.
     *
     * @param session Session receiving progress notifications.
     * @param results Batched command results to report.
     * @param completed Total completed operation count after this batch.
     */
    void queueFlushResults(Session* session, const QList<Result>& results, int completed);



    /**
     * @brief Preserves edit-layer property existence and default opinions for undo.
     */
    struct RootPropertyState {
        SdfPath propertyPath;
        bool hadSpec = false;
        bool hadDefault = false;
        VtValue defaultValue;
    };

    /**
     * @brief Captures property existence and default opinions in the edit layer.
     */
    RootPropertyState captureRootPropertyState(const SdfLayerHandle& editLayer, const SdfPath& propertyPath);

    /**
     * @brief Restores captured property existence and default opinions.
     */
    bool restoreRootPropertyState(const SdfLayerHandle& editLayer, const RootPropertyState& state);

    /**
     * @brief Rejects direct authoring on read-only instance proxy prims.
     */
    bool validateNotInstanceProxy(const UsdStageRefPtr& stage, const SdfPath& path, QString& error);

    /**
     * @brief Preserves a complete dependency property spec for undo.
     */
    struct DependencyPropertyState {
        SdfPath propertyPath;
        SdfLayerRefPtr snapshotLayer;
    };

    /**
     * @brief Copies an authored dependency property spec into an undo snapshot.
     */
    bool snapshotDependencyProperty(const SdfLayerHandle& editLayer, const SdfPath& propertyPath,
                                    DependencyPropertyState& state, QString& error);

    /**
     * @brief Restores a dependency property spec from its undo snapshot.
     */
    bool restoreDependencyProperty(const SdfLayerHandle& editLayer, const DependencyPropertyState& state,
                                   QString& error);

}  // namespace command
}  // namespace stageviz
