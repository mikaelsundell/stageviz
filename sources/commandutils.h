// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#pragma once

#include "stageviz.h"
#include "session.h"
#include <QList>

PXR_NAMESPACE_USING_DIRECTIVE

namespace stageviz {
namespace command {

/**
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
void appendResult(Session* session,
                  QList<Result>& pending,
                  const Result& result,
                  int completed,
                  int batchSize = 16);

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

}  // namespace command
}  // namespace stageviz
