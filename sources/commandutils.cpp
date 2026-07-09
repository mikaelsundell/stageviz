// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#include "commandutils.h"
#include <QMetaObject>

namespace stageviz {
namespace command {

    void appendResult(Session* session, QList<Result>& pending, const Result& result, int completed, int batchSize)
    {
        pending.append(result);

        if (pending.size() < batchSize)
            return;

        queueFlushResults(session, pending, completed);
        pending.clear();
    }

    void beginDeferred(Session* session, const QString& name, int count)
    {
        if (!session)
            return;

        session->beginProgressBlock(name, count);
        session->setPrimsUpdate(Session::PrimsUpdate::Deferred);
    }

    void finishDeferred(Session* session, const QString& message, const QList<SdfPath>& paths,
                        Session::Notify::Status status, int completed)
    {
        if (!session)
            return;

        session->setPrimsUpdate(Session::PrimsUpdate::Immediate);
        session->updateProgressNotify(Session::Notify(message, paths, status), completed);
        session->endProgressBlock();
    }

    void flushPendingResults(Session* session, QList<Result>& pending, int completed)
    {
        if (pending.isEmpty())
            return;

        queueFlushResults(session, pending, completed);
        pending.clear();
    }

    void flushResults(Session* session, const QList<Result>& results, int completed)
    {
        if (!session || results.isEmpty())
            return;

        const int start = completed - static_cast<int>(results.size()) + 1;
        for (int i = 0; i < results.size(); ++i) {
            const Result& result = results[i];
            session->updateProgressNotify(Session::Notify(result.message, { result.path }, result.status), start + i);
        }
    }

    void queueFlushResults(Session* session, const QList<Result>& results, int completed)
    {
        if (!session || results.isEmpty())
            return;

        const QList<Result> batch = results;
        QMetaObject::invokeMethod(
            session, [session, batch, completed]() { flushResults(session, batch, completed); }, Qt::QueuedConnection);
    }

}  // namespace command
}  // namespace stageviz
