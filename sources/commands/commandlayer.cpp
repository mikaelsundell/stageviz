// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#include "commandlayer.h"
#include "commandutils.h"
#include "qtutils.h"
#include "tracelocks.h"
#include "usdutils.h"
#include <QFileInfo>
#include <QSet>
#include <algorithm>
#include <functional>
#include <memory>
#include <pxr/usd/sdf/layer.h>
#include <vector>

namespace stageviz {

namespace {
    using command::appendError;

    struct SublayerState {
        SdfLayerHandle editLayer;
        std::vector<std::string> previousPaths;
        bool captured = false;
    };
}  // namespace

Command
addSublayer(const QString& filename)
{
    auto state = std::make_shared<SublayerState>();

    return Command(
        [filename, state](Session* session) {
            if (!session || filename.isEmpty())
                return;

            session->beginProgressBlock("Add sublayer", 1);
            bool success = false;
            QString error;
            {
                WRITE_LOCKER(locker, session->stageLock(), "stageLock");
                const UsdStageRefPtr stage = session->stageUnsafe();
                const QString absFilename = QFileInfo(filename).absoluteFilePath();
                if (!stage)
                    error = "stage missing";
                else if (!QFileInfo::exists(absFilename))
                    error = "sublayer file missing";
                else {
                    const SdfLayerHandle editLayer = stage->GetEditTarget().GetLayer();
                    if (!editLayer) {
                        error = "edit layer missing";
                    }
                    else {
                        const QString destinationFilename = QString::fromStdString(editLayer->GetRealPath());
                        if (!destinationFilename.isEmpty()
                            && QFileInfo(destinationFilename).absoluteFilePath() == absFilename) {
                            error = "cannot add the edit layer as its own sublayer";
                        }
                        else {
                            if (!state->captured) {
                                state->editLayer = editLayer;
                                state->previousPaths = editLayer->GetSubLayerPaths();
                                state->captured = true;
                            }

                            const std::string assetPath = stage::compositionAssetPath(editLayer, absFilename);
                            std::vector<std::string> sublayers = editLayer->GetSubLayerPaths();
                            if (std::find(sublayers.begin(), sublayers.end(), assetPath) == sublayers.end()) {
                                sublayers.push_back(assetPath);
                                editLayer->SetSubLayerPaths(sublayers);
                            }
                            const std::vector<std::string> resultingSublayers = editLayer->GetSubLayerPaths();
                            success = std::find(resultingSublayers.begin(), resultingSublayers.end(), assetPath)
                                      != resultingSublayers.end();
                            if (!success)
                                error = "failed to add sublayer";
                        }
                    }
                }
            }

            using Status = Session::Notify::Status;
            session->updateProgressNotify(Session::Notify(success ? "Sublayer added"
                                                                  : appendError("Add sublayer failed", error),
                                                          {}, success ? Status::Success : Status::Error),
                                          1);
            session->endProgressBlock();
        },
        [state](Session* session) {
            if (!session || !state->captured || !state->editLayer)
                return;

            session->beginProgressBlock("Undo add sublayer", 1);
            bool success = false;
            {
                WRITE_LOCKER(locker, session->stageLock(), "stageLock");
                const UsdStageRefPtr stage = session->stageUnsafe();
                if (stage && stage->GetEditTarget().GetLayer() == state->editLayer) {
                    state->editLayer->SetSubLayerPaths(state->previousPaths);
                    success = true;
                }
            }
            using Status = Session::Notify::Status;
            session->updateProgressNotify(Session::Notify(success ? "Add sublayer undone" : "Undo add sublayer failed",
                                                          {}, success ? Status::Success : Status::Error),
                                          1);
            session->endProgressBlock();
        });
}

Command
reorderSublayers(const QString& parentIdentifier, const QString& sourceIdentifier, const QString& targetIdentifier,
                 bool afterTarget)
{
    auto previousPaths = std::make_shared<std::vector<std::string>>();
    auto captured = std::make_shared<bool>(false);

    return Command(
        [parentIdentifier, sourceIdentifier, targetIdentifier, afterTarget, previousPaths, captured](Session* session) {
            if (!session || parentIdentifier.isEmpty() || sourceIdentifier.isEmpty() || targetIdentifier.isEmpty()
                || sourceIdentifier == targetIdentifier)
                return;

            bool success = false;
            {
                WRITE_LOCKER(locker, session->stageLock(), "stageLock");
                const UsdStageRefPtr stage = session->stageUnsafe();
                const SdfLayerHandle parent = SdfLayer::Find(parentIdentifier.toStdString());
                if (stage && parent && stage->HasLocalLayer(parent) && parent->PermissionToEdit()) {
                    std::vector<std::string> paths = parent->GetSubLayerPaths();
                    std::vector<std::string> sourcePaths;
                    std::vector<std::string> targetPaths;
                    for (const std::string& path : paths) {
                        const SdfLayerHandle child = SdfLayer::FindRelativeToLayer(parent, path);
                        if (!child)
                            continue;
                        const std::string identifier = child->GetIdentifier();
                        if (identifier == sourceIdentifier.toStdString())
                            sourcePaths.push_back(path);
                        if (identifier == targetIdentifier.toStdString())
                            targetPaths.push_back(path);
                    }

                    if (sourcePaths.size() == 1 && targetPaths.size() == 1) {
                        if (!*captured) {
                            *previousPaths = paths;
                            *captured = true;
                        }
                        const std::string sourcePath = sourcePaths.front();
                        const std::string targetPath = targetPaths.front();
                        paths.erase(std::find(paths.begin(), paths.end(), sourcePath));
                        const auto target = std::find(paths.begin(), paths.end(), targetPath);
                        if (target != paths.end()) {
                            const auto insertAt = afterTarget ? std::next(target) : target;
                            paths.insert(insertAt, sourcePath);
                            parent->SetSubLayerPaths(paths);
                            success = parent->GetSubLayerPaths() == paths;
                        }
                    }
                }
            }

            if (!success)
                session->notifyStatus(Session::Notify::Status::Warning,
                                      QStringLiteral("Could not reorder layer sublayers"));
        },
        [parentIdentifier, previousPaths, captured](Session* session) {
            if (!session || !*captured)
                return;

            WRITE_LOCKER(locker, session->stageLock(), "stageLock");
            const UsdStageRefPtr stage = session->stageUnsafe();
            const SdfLayerHandle parent = SdfLayer::Find(parentIdentifier.toStdString());
            if (stage && parent && stage->HasLocalLayer(parent) && parent->PermissionToEdit())
                parent->SetSubLayerPaths(*previousPaths);
        });
}

Command
removeLayer(const QString& layerIdentifier)
{
    using LayerPaths = std::pair<std::string, std::vector<std::string>>;
    auto previous = std::make_shared<std::vector<LayerPaths>>();
    auto captured = std::make_shared<bool>(false);

    return Command(
        [layerIdentifier, previous, captured](Session* session) {
            if (!session || layerIdentifier.isEmpty())
                return;

            bool success = false;
            {
                WRITE_LOCKER(locker, session->stageLock(), "stageLock");
                const UsdStageRefPtr stage = session->stageUnsafe();
                const SdfLayerHandle layer = SdfLayer::Find(layerIdentifier.toStdString());
                if (stage && layer && stage->HasLocalLayer(layer) && stage->GetRootLayer() != layer
                    && stage->GetEditTarget().GetLayer() != layer) {
                    std::vector<LayerPaths> parents;
                    int referenceCount = 0;
                    for (const SdfLayerHandle& parent : stage->GetLayerStack(false)) {
                        if (!parent)
                            continue;
                        std::vector<std::string> paths = parent->GetSubLayerPaths();
                        bool referencesLayer = false;
                        for (const std::string& path : paths) {
                            if (SdfLayer::FindRelativeToLayer(parent, path) == layer) {
                                ++referenceCount;
                                referencesLayer = true;
                            }
                        }
                        if (referencesLayer)
                            parents.emplace_back(parent->GetIdentifier(), std::move(paths));
                    }

                    if (referenceCount == 1 && parents.size() == 1) {
                        const SdfLayerHandle parent = SdfLayer::Find(parents.front().first);
                        if (parent && parent->PermissionToEdit()) {
                            std::vector<std::string> paths = parent->GetSubLayerPaths();
                            const auto reference
                                = std::find_if(paths.begin(), paths.end(), [&](const std::string& path) {
                                      return SdfLayer::FindRelativeToLayer(parent, path) == layer;
                                  });
                            if (reference != paths.end()) {
                                if (!*captured) {
                                    *previous = parents;
                                    *captured = true;
                                }
                                paths.erase(reference);
                                parent->SetSubLayerPaths(paths);
                                success = parent->GetSubLayerPaths() == paths;
                            }
                        }
                    }
                }
            }

            if (!success)
                session->notifyStatus(Session::Notify::Status::Warning,
                                      QStringLiteral("Could not remove layer from the stage"));
        },
        [previous, captured](Session* session) {
            if (!session || !*captured)
                return;

            WRITE_LOCKER(locker, session->stageLock(), "stageLock");
            const UsdStageRefPtr stage = session->stageUnsafe();
            if (!stage)
                return;
            for (const LayerPaths& state : *previous) {
                const SdfLayerHandle parent = SdfLayer::Find(state.first);
                if (parent && stage->HasLocalLayer(parent))
                    parent->SetSubLayerPaths(state.second);
            }
        });
}

Command
setLayerMuted(const QString& layerIdentifier, bool muted)
{
    struct LayerMuteState {
        std::vector<SdfLayerRefPtr> retainedLayers;
        bool wasMuted = false;
        bool captured = false;
    };

    auto state = std::make_shared<LayerMuteState>();

    auto applyMuteState = [layerIdentifier, state](Session* session, bool muted) {
        if (!session || layerIdentifier.isEmpty())
            return false;

        WRITE_LOCKER(locker, session->stageLock(), "stageLock");
        const UsdStageRefPtr stage = session->stageUnsafe();
        if (!stage)
            return false;

        const std::string identifier = layerIdentifier.toStdString();
        const bool currentlyMuted = stage->IsLayerMuted(identifier);
        if (currentlyMuted == muted)
            return true;

        SdfLayerHandle layer = SdfLayer::Find(identifier);
        if (!layer) {
            for (const SdfLayerRefPtr& retained : state->retainedLayers) {
                if (retained && retained->GetIdentifier() == identifier) {
                    layer = retained;
                    break;
                }
            }
        }

        if (muted) {
            if (!layer || layer == stage->GetRootLayer() || !stage->HasLocalLayer(layer)
                || stage->GetEditTarget().GetLayer() == layer)
                return false;

            if (state->retainedLayers.empty()) {
                QSet<QString> visited;
                std::function<void(const SdfLayerHandle&)> retainSubtree = [&](const SdfLayerHandle& current) {
                    if (!current)
                        return;
                    const QString currentIdentifier = QString::fromStdString(current->GetIdentifier());
                    if (visited.contains(currentIdentifier))
                        return;
                    visited.insert(currentIdentifier);
                    state->retainedLayers.push_back(SdfLayerRefPtr(current));
                    for (const std::string& sublayerPath : current->GetSubLayerPaths())
                        retainSubtree(SdfLayer::FindRelativeToLayer(current, sublayerPath));
                };
                retainSubtree(layer);
            }
            stage->MuteLayer(identifier);
        }
        else {
            if (!currentlyMuted || (stage->GetRootLayer() && stage->GetRootLayer()->GetIdentifier() == identifier))
                return false;
            stage->UnmuteLayer(identifier);
        }
        return stage->IsLayerMuted(identifier) == muted;
    };

    return Command(
        [layerIdentifier, muted, state, applyMuteState](Session* session) {
            if (!session || layerIdentifier.isEmpty())
                return;
            if (!state->captured) {
                const UsdStageRefPtr stage = session->stage();
                if (!stage)
                    return;
                state->wasMuted = stage->IsLayerMuted(layerIdentifier.toStdString());
                state->captured = true;
            }
            if (applyMuteState(session, muted))
                session->notifyStatus(Session::Notify::Status::Success,
                                      muted ? QStringLiteral("Layer muted") : QStringLiteral("Layer unmuted"));
            else
                session->notifyStatus(Session::Notify::Status::Warning,
                                      QStringLiteral("Could not change layer mute state"));
        },
        [state, applyMuteState](Session* session) {
            if (!session || !state->captured)
                return;
            const bool success = applyMuteState(session, state->wasMuted);
            session->notifyStatus(success ? Session::Notify::Status::Success : Session::Notify::Status::Warning,
                                  success ? QStringLiteral("Layer mute state restored")
                                          : QStringLiteral("Could not restore layer mute state"));
        });
}

Command
setLayerPermissionToEdit(const QString& layerIdentifier, bool editable)
{
    struct LayerPermissionState {
        SdfLayerRefPtr layer;
        bool previousEditable = true;
        bool captured = false;
    };

    auto state = std::make_shared<LayerPermissionState>();

    return Command(
        [layerIdentifier, editable, state](Session* session) {
            if (!session || layerIdentifier.isEmpty())
                return;

            bool success = false;
            {
                WRITE_LOCKER(locker, session->stageLock(), "stageLock");
                const UsdStageRefPtr stage = session->stageUnsafe();
                SdfLayerHandle layer = SdfLayer::Find(layerIdentifier.toStdString());
                if (stage && layer && layer != stage->GetRootLayer()
                    && (stage->HasLocalLayer(layer) || stage->IsLayerMuted(layerIdentifier.toStdString()))) {
                    if (!state->captured) {
                        state->layer = SdfLayerRefPtr(layer);
                        state->previousEditable = layer->PermissionToEdit();
                        state->captured = true;
                    }
                    layer->SetPermissionToEdit(editable);
                    success = layer->PermissionToEdit() == editable;
                }
            }
            session->notifyStatus(success ? Session::Notify::Status::Success : Session::Notify::Status::Warning,
                                  success
                                      ? (editable ? QStringLiteral("Layer unlocked") : QStringLiteral("Layer locked"))
                                      : QStringLiteral("Could not change layer permission"));
        },
        [state](Session* session) {
            if (!session || !state->captured || !state->layer)
                return;

            bool success = false;
            {
                WRITE_LOCKER(locker, session->stageLock(), "stageLock");
                const UsdStageRefPtr stage = session->stageUnsafe();
                if (stage && stage->GetRootLayer() != state->layer
                    && (stage->HasLocalLayer(state->layer) || stage->IsLayerMuted(state->layer->GetIdentifier()))) {
                    state->layer->SetPermissionToEdit(state->previousEditable);
                    success = state->layer->PermissionToEdit() == state->previousEditable;
                }
            }
            session->notifyStatus(success ? Session::Notify::Status::Success : Session::Notify::Status::Warning,
                                  success ? QStringLiteral("Layer permission restored")
                                          : QStringLiteral("Could not restore layer permission"));
        });
}

Command
setEditLayer(const QString& layerIdentifier)
{
    struct EditLayerState {
        SdfLayerHandle previousLayer;
        SdfLayerHandle targetLayer;
        bool captured = false;
    };

    auto state = std::make_shared<EditLayerState>();

    return Command(
        [layerIdentifier, state](Session* session) {
            if (!session || layerIdentifier.isEmpty())
                return;

            session->beginProgressBlock("Set edit layer", 1);

            bool success = false;
            QString error;

            if (!state->captured) {
                {
                    READ_LOCKER(locker, session->stageLock(), "stageLock");
                    const UsdStageRefPtr stage = session->stageUnsafe();

                    if (!stage) {
                        error = "stage missing";
                    }
                    else {
                        state->previousLayer = stage->GetEditTarget().GetLayer();
                        const std::string identifier = qt::QStringToString(layerIdentifier);
                        for (const SdfLayerHandle& layer : stage->GetLayerStack(true)) {
                            if (layer && layer->GetIdentifier() == identifier) {
                                state->targetLayer = layer;
                                break;
                            }
                        }

                        if (!state->targetLayer)
                            error = QString("edit layer is not local to the stage: %1").arg(layerIdentifier);
                    }
                }

                state->captured = state->targetLayer && state->previousLayer;
            }

            if (state->captured) {
                success = session->setEditLayer(state->targetLayer);
                if (!success)
                    error = QString("failed to set edit layer: %1").arg(layerIdentifier);
            }

            using Status = Session::Notify::Status;
            session->updateProgressNotify(Session::Notify(success ? QString("Edit layer set to %1").arg(layerIdentifier)
                                                                  : appendError("Set edit layer failed", error),
                                                          {}, success ? Status::Success : Status::Error),
                                          1);
            session->endProgressBlock();
        },
        [state](Session* session) {
            if (!session || !state->captured || !state->previousLayer)
                return;

            session->beginProgressBlock("Undo set edit layer", 1);
            const bool success = session->setEditLayer(state->previousLayer);

            using Status = Session::Notify::Status;
            const QString identifier = qt::StringToQString(state->previousLayer->GetIdentifier());
            session->updateProgressNotify(Session::Notify(success ? QString("Edit layer restored to %1").arg(identifier)
                                                                  : QString("Undo set edit layer failed"),
                                                          {}, success ? Status::Success : Status::Error),
                                          1);
            session->endProgressBlock();
        });
}

}  // namespace stageviz
