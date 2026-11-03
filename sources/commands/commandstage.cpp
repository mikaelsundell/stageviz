// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#include "commandstage.h"
#include "commandutils.h"
#include "tracelocks.h"
#include "usdutils.h"
#include <memory>
#include <pxr/usd/sdf/layer.h>
#include <pxr/usd/usd/editContext.h>
#include <pxr/usd/usd/editTarget.h>
#include <pxr/usd/usd/prim.h>

namespace stageviz {

namespace {
    using command::currentEditLayer;
}  // namespace

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

                        QString editError;
                        const SdfLayerHandle editLayer = currentEditLayer(stage, editError);
                        const UsdPrim prim = stage->GetPrimAtPath(path);
                        if (!editLayer) {
                            error = editError;
                        }
                        else if (!prim || !prim.IsValid()) {
                            error = "invalid prim";
                        }
                        else if (path.GetParentPath() != SdfPath::AbsoluteRootPath()) {
                            error = "default prim must be a root prim";
                        }
                        else if (!layer::validatePrim(stage, editLayer, path, error)) {}
                        else {
                            UsdEditContext context(stage, UsdEditTarget(editLayer));
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
                        QString editError;
                        const SdfLayerHandle editLayer = currentEditLayer(stage, editError);
                        if (!editLayer) {
                            error = editError;
                        }
                        else if (state->previousDefaultPrimPath.IsEmpty()) {
                            UsdEditContext context(stage, UsdEditTarget(editLayer));
                            stage->ClearDefaultPrim();
                            success = true;
                        }
                        else {
                            const UsdPrim prim = stage->GetPrimAtPath(state->previousDefaultPrimPath);
                            if (!prim || !prim.IsValid()) {
                                error = "previous default prim missing";
                            }
                            else {
                                UsdEditContext context(stage, UsdEditTarget(editLayer));
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
clearDefaultPrim()
{
    auto previousDefaultPrimPath = std::make_shared<SdfPath>();

    return Command(
        [previousDefaultPrimPath](Session* session) {
            if (!session)
                return;

            session->beginProgressBlock("Clear default prim", 1);

            command::runWorker([session, previousDefaultPrimPath]() {
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
                        const UsdPrim previousDefaultPrim = stage->GetDefaultPrim();
                        if (!previousDefaultPrim) {
                            error = "stage has no default prim";
                        }
                        else {
                            QString editError;
                            const SdfLayerHandle editLayer = currentEditLayer(stage, editError);
                            if (!editLayer) {
                                error = editError;
                            }
                            else {
                                *previousDefaultPrimPath = previousDefaultPrim.GetPath();
                                UsdEditContext context(stage, UsdEditTarget(editLayer));
                                stage->ClearDefaultPrim();
                                success = true;
                            }
                        }
                    }
                }

                command::queueToSession(session, [session, previousDefaultPrimPath, hadStage, success, error]() {
                    using Status = Session::Notify::Status;
                    if (!hadStage || !success) {
                        const QString message = error.isEmpty() ? "Clear default prim failed"
                                                                : QString("Clear default prim failed: %1").arg(error);
                        session->updateProgressNotify(Session::Notify(message, { *previousDefaultPrimPath },
                                                                      Status::Error),
                                                      1);
                    }
                    else {
                        session->updateProgressNotify(Session::Notify("Default prim cleared",
                                                                      { *previousDefaultPrimPath }, Status::Success),
                                                      1);
                    }
                    session->endProgressBlock();
                });
            });
        },
        [previousDefaultPrimPath](Session* session) {
            if (!session || previousDefaultPrimPath->IsEmpty())
                return;

            session->beginProgressBlock("Undo clear default prim", 1);

            command::runWorker([session, previousDefaultPrimPath]() {
                bool success = false;
                QString error;

                {
                    WRITE_LOCKER(locker, session->stageLock(), "stageLock");
                    const UsdStageRefPtr stage = session->stageUnsafe();
                    if (!stage) {
                        error = "stage unavailable";
                    }
                    else {
                        QString editError;
                        const SdfLayerHandle editLayer = currentEditLayer(stage, editError);
                        const UsdPrim prim = stage->GetPrimAtPath(*previousDefaultPrimPath);
                        if (!editLayer) {
                            error = editError;
                        }
                        else if (!prim || !prim.IsValid()) {
                            error = "previous default prim missing";
                        }
                        else {
                            UsdEditContext context(stage, UsdEditTarget(editLayer));
                            stage->SetDefaultPrim(prim);
                            success = true;
                        }
                    }
                }

                command::queueToSession(session, [session, previousDefaultPrimPath, success, error]() {
                    using Status = Session::Notify::Status;
                    if (!success) {
                        const QString message = error.isEmpty()
                                                    ? "Undo clear default prim failed"
                                                    : QString("Undo clear default prim failed: %1").arg(error);
                        session->updateProgressNotify(Session::Notify(message, { *previousDefaultPrimPath },
                                                                      Status::Error),
                                                      1);
                    }
                    else {
                        session->updateProgressNotify(Session::Notify("Clear default prim undone",
                                                                      { *previousDefaultPrimPath }, Status::Success),
                                                      1);
                    }
                    session->endProgressBlock();
                });
            });
        });
}

}  // namespace stageviz
