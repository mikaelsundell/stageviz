// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#include "commandutils.h"
#include <QMetaObject>

namespace stageviz {
namespace command {

void flushResults(Session* session, const QList<Result>& results, int completed)
{
    if (!session || results.isEmpty())
        return;

    const int start = completed - static_cast<int>(results.size()) + 1;
    for (int i = 0; i < results.size(); ++i) {
        const Result& result = results[i];
        session->updateProgressNotify(
            Session::Notify(result.message, { result.path }, result.status),
            start + i);
    }
}

void queueFlushResults(Session* session, const QList<Result>& results, int completed)
{
    if (!session || results.isEmpty())
        return;

    const QList<Result> batch = results;
    QMetaObject::invokeMethod(
        session,
        [session, batch, completed]() {
            flushResults(session, batch, completed);
        },
        Qt::QueuedConnection);
}

void appendResult(Session* session,
                  QList<Result>& pending,
                  const Result& result,
                  int completed,
                  int batchSize)
{
    pending.append(result);

    if (pending.size() < batchSize)
        return;

    queueFlushResults(session, pending, completed);
    pending.clear();
}

void flushPendingResults(Session* session, QList<Result>& pending, int completed)
{
    if (pending.isEmpty())
        return;

    queueFlushResults(session, pending, completed);
    pending.clear();
}

}  // namespace command
}  // namespace stageviz
