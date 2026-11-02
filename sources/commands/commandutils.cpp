// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#include "commandutils.h"
#include "qtutils.h"
#include <QMetaObject>
#include <pxr/usd/sdf/namespaceEdit.h>

namespace stageviz {
namespace command {

    QString pathText(const SdfPath& path) { return qt::SdfPathToQString(path); }

    QString appendError(const QString& message, const QString& error)
    {
        return error.isEmpty() ? message : QString("%1: %2").arg(message, error);
    }

    SdfLayerHandle currentEditLayer(const UsdStageRefPtr& stage, QString& error)
    {
        if (!stage) {
            error = "stage missing";
            return {};
        }

        const SdfLayerHandle layer = stage->GetEditTarget().GetLayer();
        if (!layer) {
            error = "edit layer missing";
            return {};
        }
        return layer;
    }

    QString summarizeErrors(const QStringList& errors) { return errors.join('\n'); }

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

    void updateProgressNotify(Session* session, const QString& message, const QList<SdfPath>& paths,
                              Session::Notify::Status status, size_t completed)
    {
        if (!session)
            return;

        session->updateProgressNotify(Session::Notify(message, paths, status), completed);
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
