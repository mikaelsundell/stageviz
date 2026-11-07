// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#include "commandutils.h"
#include "qtutils.h"
#include <QMetaObject>
#include <pxr/usd/sdf/copyUtils.h>
#include <pxr/usd/sdf/namespaceEdit.h>
#include <pxr/usd/sdf/primSpec.h>
#include <pxr/usd/sdf/schema.h>

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

    QString summarizeErrors(const QStringList& errors)
    {
        // A failed bulk edit can contain tens of thousands of paths. Keep UI
        // notifications useful without constructing and painting megabytes of
        // repeated diagnostics on the application thread.
        constexpr qsizetype limit = 32;
        if (errors.size() <= limit)
            return errors.join('\n');
        QStringList summary = errors.mid(0, limit);
        summary.append(QString("%1 additional errors omitted").arg(errors.size() - limit));
        return summary.join('\n');
    }

    bool removePropertySpec(const SdfLayerHandle& layer, const SdfPath& propertyPath)
    {
        if (!layer || propertyPath.IsEmpty() || !propertyPath.IsPropertyPath())
            return false;
        const auto property = layer->GetPropertyAtPath(propertyPath);
        if (!property)
            return true;
        if (!layer->PermissionToEdit())
            return false;
        // This is an exact spec deletion, not a namespace move. Avoid building
        // and validating a layer-wide namespace transaction for every property
        // during large undo/restore operations.
        const auto owner = TfDynamic_cast<SdfPrimSpecHandle>(property->GetOwner());
        if (owner) {
            owner->RemoveProperty(property);
            return !layer->GetPropertyAtPath(propertyPath);
        }
        // Relational attribute specs can have a non-prim owner.
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



    RootPropertyState captureRootPropertyState(const SdfLayerHandle& editLayer, const SdfPath& propertyPath)
    {
        RootPropertyState state;
        state.propertyPath = propertyPath;
        if (!editLayer || propertyPath.IsEmpty())
            return state;

        state.hadSpec = bool(editLayer->GetPropertyAtPath(propertyPath));
        state.hadDefault = editLayer->HasField(propertyPath, SdfFieldKeys->Default);
        if (state.hadDefault)
            state.defaultValue = editLayer->GetField(propertyPath, SdfFieldKeys->Default);
        return state;
    }

    bool restoreRootPropertyState(const SdfLayerHandle& editLayer, const RootPropertyState& state)
    {
        if (!editLayer || state.propertyPath.IsEmpty())
            return false;

        if (!state.hadSpec)
            return removePropertySpec(editLayer, state.propertyPath);

        if (state.hadDefault) {
            editLayer->SetField(state.propertyPath, SdfFieldKeys->Default, state.defaultValue);
            return true;
        }

        editLayer->EraseField(state.propertyPath, SdfFieldKeys->Default);
        return true;
    }

    bool validateNotInstanceProxy(const UsdStageRefPtr& stage, const SdfPath& path, QString& error)
    {
        if (!stage) {
            error = "stage missing";
            return false;
        }
        const SdfPath primPath = path.IsPropertyPath() ? path.GetPrimPath() : path;
        const UsdPrim prim = stage->GetPrimAtPath(primPath);
        if (prim && prim.IsInstanceProxy()) {
            error = QString("cannot edit instance proxy: %1").arg(pathText(primPath));
            return false;
        }
        return true;
    }

    bool snapshotDependencyProperty(const SdfLayerHandle& editLayer, const SdfPath& propertyPath,
                                    DependencyPropertyState& state, QString& error)
    {
        if (!editLayer || propertyPath.IsEmpty() || !propertyPath.IsPropertyPath()) {
            error = "invalid dependency property";
            return false;
        }

        state.propertyPath = propertyPath;
        state.snapshotLayer = SdfLayer::CreateAnonymous("stageviz_dependency_snapshot.usda");

        if (!state.snapshotLayer) {
            error = QString("failed to create dependency snapshot: %1").arg(pathText(propertyPath));
            return false;
        }

        SdfCreatePrimInLayer(state.snapshotLayer, propertyPath.GetPrimPath());

        if (!SdfCopySpec(editLayer, propertyPath, state.snapshotLayer, propertyPath)) {
            error = QString("failed to snapshot dependency property: %1").arg(pathText(propertyPath));
            return false;
        }

        return true;
    }

    bool restoreDependencyProperty(const SdfLayerHandle& editLayer, const DependencyPropertyState& state,
                                   QString& error)
    {
        if (!editLayer || !state.snapshotLayer || state.propertyPath.IsEmpty()) {
            error = "invalid dependency snapshot";
            return false;
        }

        if (!editLayer->GetPrimAtPath(state.propertyPath.GetPrimPath()))
            SdfCreatePrimInLayer(editLayer, state.propertyPath.GetPrimPath());

        if (!SdfCopySpec(state.snapshotLayer, state.propertyPath, editLayer, state.propertyPath)) {
            error = QString("failed to restore dependency property: %1").arg(pathText(state.propertyPath));
            return false;
        }

        return true;
    }

}  // namespace command
}  // namespace stageviz
