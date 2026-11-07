// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#include "commandmaterial.h"
#include "commandutils.h"
#include "materialutils.h"
#include "qtutils.h"
#include "selectionlist.h"
#include "tracelocks.h"
#include "usdutils.h"
#include <memory>
#include <pxr/usd/sdf/copyUtils.h>
#include <pxr/usd/sdf/layer.h>
#include <pxr/usd/usd/attribute.h>
#include <pxr/usd/usd/editContext.h>
#include <pxr/usd/usd/editTarget.h>
#include <pxr/usd/usd/prim.h>
#include <pxr/usd/usdShade/material.h>
#include <pxr/usd/usdShade/materialBindingAPI.h>
#include <pxr/usd/usdShade/shader.h>

namespace stageviz {

namespace {
    using command::appendError;
    using command::currentEditLayer;
    using command::pathText;
    using command::removePropertySpec;
    using command::summarizeErrors;
    using command::validateNotInstanceProxy;

    struct MaterialBindingState {
        SdfPath primPath;
        bool hadDirectBinding = false;
        SdfPath previousMaterialPath;
    };

    bool captureDirectMaterialBinding(const UsdPrim& prim, MaterialBindingState& state)
    {
        if (!prim)
            return false;

        state.primPath = prim.GetPath();
        state.hadDirectBinding = false;
        state.previousMaterialPath = SdfPath();

        const UsdShadeMaterialBindingAPI bindingApi(prim);
        const UsdRelationship relationship = bindingApi.GetDirectBindingRel();
        if (!relationship)
            return true;

        SdfPathVector targets;
        if (!relationship.GetTargets(&targets) || targets.empty())
            return true;

        state.hadDirectBinding = true;
        state.previousMaterialPath = targets.front();
        return true;
    }

    bool applyDirectMaterialBinding(UsdStageRefPtr stage, const SdfPath& primPath, const SdfPath& materialPath,
                                    QString& error)
    {
        if (!stage) {
            error = "stage missing";
            return false;
        }

        const UsdPrim prim = stage->GetPrimAtPath(primPath);
        if (!prim || !prim.IsValid()) {
            error = QString("target prim missing: %1").arg(pathText(primPath));
            return false;
        }

        const UsdPrim materialPrim = stage->GetPrimAtPath(materialPath);
        if (!materialPrim || !materialPrim.IsA<UsdShadeMaterial>()) {
            error = QString("material missing: %1").arg(pathText(materialPath));
            return false;
        }

        UsdShadeMaterialBindingAPI bindingApi = UsdShadeMaterialBindingAPI::Apply(prim);
        if (!bindingApi) {
            error = QString("could not apply material binding API: %1").arg(pathText(primPath));
            return false;
        }

        if (!bindingApi.Bind(UsdShadeMaterial(materialPrim))) {
            error = QString("failed to bind material to: %1").arg(pathText(primPath));
            return false;
        }

        return true;
    }

    bool restoreDirectMaterialBinding(UsdStageRefPtr stage, const MaterialBindingState& state, QString& error)
    {
        if (!stage) {
            error = "stage missing";
            return false;
        }

        const UsdPrim prim = stage->GetPrimAtPath(state.primPath);
        if (!prim || !prim.IsValid()) {
            error = QString("target prim missing: %1").arg(pathText(state.primPath));
            return false;
        }

        UsdShadeMaterialBindingAPI bindingApi = UsdShadeMaterialBindingAPI::Apply(prim);
        if (!bindingApi) {
            error = QString("could not apply material binding API: %1").arg(pathText(state.primPath));
            return false;
        }

        if (!state.hadDirectBinding) {
            if (!bindingApi.UnbindDirectBinding()) {
                error = QString("failed to remove material binding from: %1").arg(pathText(state.primPath));
                return false;
            }
            return true;
        }

        const UsdPrim materialPrim = stage->GetPrimAtPath(state.previousMaterialPath);
        if (!materialPrim || !materialPrim.IsA<UsdShadeMaterial>()) {
            error = QString("previous material missing: %1").arg(pathText(state.previousMaterialPath));
            return false;
        }

        if (!bindingApi.Bind(UsdShadeMaterial(materialPrim))) {
            error = QString("failed to restore material binding on: %1").arg(pathText(state.primPath));
            return false;
        }

        return true;
    }

    struct ShaderInputPropertyState {
        SdfPath propertyPath;
        bool hadSpec = false;
        SdfLayerRefPtr snapshotLayer;
    };

    bool captureShaderInputProperty(const SdfLayerHandle& editLayer, const SdfPath& propertyPath,
                                    ShaderInputPropertyState& state, QString& error)
    {
        if (!editLayer || propertyPath.IsEmpty() || !propertyPath.IsPropertyPath()) {
            error = "invalid shader input property";
            return false;
        }

        state.propertyPath = propertyPath;
        state.hadSpec = bool(editLayer->GetPropertyAtPath(propertyPath));
        if (!state.hadSpec)
            return true;

        state.snapshotLayer = SdfLayer::CreateAnonymous("stageviz_shader_input_snapshot.usda");
        if (!state.snapshotLayer) {
            error = QString("failed to create shader input snapshot: %1").arg(pathText(propertyPath));
            return false;
        }

        SdfCreatePrimInLayer(state.snapshotLayer, propertyPath.GetPrimPath());
        if (!SdfCopySpec(editLayer, propertyPath, state.snapshotLayer, propertyPath)) {
            error = QString("failed to snapshot shader input: %1").arg(pathText(propertyPath));
            return false;
        }
        return true;
    }

    bool restoreShaderInputProperty(const SdfLayerHandle& editLayer, const ShaderInputPropertyState& state,
                                    QString& error)
    {
        if (!editLayer || state.propertyPath.IsEmpty()) {
            error = "invalid shader input snapshot";
            return false;
        }

        if (!removePropertySpec(editLayer, state.propertyPath)) {
            error = QString("failed to clear shader input: %1").arg(pathText(state.propertyPath));
            return false;
        }

        if (!state.hadSpec)
            return true;

        if (!state.snapshotLayer) {
            error = "shader input snapshot missing";
            return false;
        }

        if (!editLayer->GetPrimAtPath(state.propertyPath.GetPrimPath()))
            SdfCreatePrimInLayer(editLayer, state.propertyPath.GetPrimPath());

        if (!SdfCopySpec(state.snapshotLayer, state.propertyPath, editLayer, state.propertyPath)) {
            error = QString("failed to restore shader input: %1").arg(pathText(state.propertyPath));
            return false;
        }
        return true;
    }

    SdfValueTypeName declaredMaterialXInputType(UsdStageRefPtr stage, const SdfPath& inputPath)
    {
        if (!stage || !inputPath.IsPropertyPath())
            return {};

        const UsdAttribute attr = stage->GetAttributeAtPath(inputPath);
        const UsdShadeInput input(attr);
        if (!input)
            return {};

        const UsdShadeShader shader(input.GetPrim());
        if (!shader)
            return {};

        TfToken id;
        shader.GetIdAttr().Get(&id);
        const QString shaderId = QString::fromStdString(id.GetString());
        if (!shaderId.startsWith(QStringLiteral("ND_")))
            return {};

        MaterialXNodeDefinition definition;
        if (!MaterialUtils::materialXNodeDefinition(shaderId, &definition))
            return {};

        return MaterialUtils::materialXPortType(definition, QString::fromStdString(input.GetBaseName().GetString()),
                                                false);
    }
}  // namespace

Command
newMaterial(MaterialType type)
{
    struct NewMaterialState {
        SdfPath createdPath;
        bool hadMaterialsSpec = false;
        QList<SdfPath> previousSelection;
        QList<SdfPath> previousMask;
    };

    auto state = std::make_shared<NewMaterialState>();

    auto titleForType = [](MaterialType value) {
        switch (value) {
        case MaterialType::PreviewSurface: return QStringLiteral("New USD Preview Surface material");
        case MaterialType::StandardSurface: return QStringLiteral("New MaterialX Standard Surface material");
        case MaterialType::OpenPBRSurface: return QStringLiteral("New MaterialX OpenPBR Surface material");
        }
        return QStringLiteral("New material");
    };

    const QString title = titleForType(type);

    return Command(
        [type, state, title](Session* session) {
            if (!session)
                return;

            state->previousSelection = session->selectionList()->paths();
            state->previousMask = session->mask();

            command::beginDeferred(session, title, 1);

            command::runWorker([session, type, state, title]() {
                bool success = false;
                QString error;
                QList<SdfPath> changed;
                SdfPath createdPath;

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
                            const SdfPath materialsPath("/Materials");
                            state->hadMaterialsSpec = bool(editLayer->GetPrimAtPath(materialsPath));

                            switch (type) {
                            case MaterialType::PreviewSurface:
                                createdPath = MaterialUtils::createPreviewSurfaceMaterial(stage);
                                break;
                            case MaterialType::StandardSurface:
                                createdPath = MaterialUtils::createStandardSurfaceMaterial(stage);
                                break;
                            case MaterialType::OpenPBRSurface:
                                createdPath = MaterialUtils::createOpenPBRSurfaceMaterial(stage);
                                break;
                            }

                            if (createdPath.IsEmpty()) {
                                error = "failed to create material";
                            }
                            else {
                                state->createdPath = createdPath;
                                path::appendUnique(changed, materialsPath);
                                path::appendUnique(changed, createdPath);
                                success = true;
                            }
                        }
                    }
                }

                command::queueToSession(session, [session, changed, success, error, title]() {
                    using Status = Session::Notify::Status;
                    command::finishDeferred(session,
                                            success ? QStringLiteral("Material created")
                                                    : appendError(QStringLiteral("%1 failed").arg(title), error),
                                            changed, success ? Status::Success : Status::Error);
                });
            });
        },
        [state](Session* session) {
            if (!session || state->createdPath.IsEmpty())
                return;

            command::beginDeferred(session, "Undo new material", 1);

            command::runWorker([session, state]() {
                bool success = false;
                QString error;
                QList<SdfPath> changed;

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
                        else if (!stage::removePrimSpec(editLayer, state->createdPath)) {
                            error = QString("failed to remove material: %1").arg(pathText(state->createdPath));
                        }
                        else {
                            const SdfPath materialsPath("/Materials");
                            if (!state->hadMaterialsSpec) {
                                const SdfPrimSpecHandle materialsSpec = editLayer->GetPrimAtPath(materialsPath);
                                if (materialsSpec && materialsSpec->IsInert())
                                    stage::removePrimSpec(editLayer, materialsPath);
                            }

                            path::appendUnique(changed, materialsPath);
                            path::appendUnique(changed, state->createdPath);
                            success = true;
                        }
                    }
                }

                command::queueToSession(session, [session, state, changed, success, error]() {
                    using Status = Session::Notify::Status;
                    command::finishDeferred(session,
                                            success ? QStringLiteral("New material undone")
                                                    : appendError(QStringLiteral("Undo new material failed"), error),
                                            changed, success ? Status::Success : Status::Error);

                    if (success) {
                        session->selectionList()->updatePaths(state->previousSelection);
                        session->setMask(state->previousMask);
                    }
                });
            });
        });
}

Command
bindMaterial(const QList<SdfPath>& inPaths, const SdfPath& materialPath)
{
    auto states = std::make_shared<QList<MaterialBindingState>>();

    return Command(
        [inPaths, materialPath, states](Session* session) {
            if (!session || inPaths.isEmpty() || materialPath.IsEmpty())
                return;

            const QList<SdfPath> paths = path::uniquePaths(inPaths);
            command::beginDeferred(session, "Bind material", static_cast<int>(paths.size()));

            command::runWorker([session, paths, materialPath, states]() {
                QList<SdfPath> changed;
                path::PathCollector collector(changed);
                QStringList errors;

                {
                    WRITE_LOCKER(locker, session->stageLock(), "stageLock");
                    const UsdStageRefPtr stage = session->stageUnsafe();

                    if (!stage) {
                        errors.append("stage missing");
                    }
                    else {
                        const UsdPrim materialPrim = stage->GetPrimAtPath(materialPath);
                        if (!materialPrim || !materialPrim.IsA<UsdShadeMaterial>()) {
                            errors.append(QString("material missing: %1").arg(pathText(materialPath)));
                        }
                        else {
                            if (states->isEmpty()) {
                                states->reserve(paths.size());

                                for (const SdfPath& inputPath : paths) {
                                    const SdfPath primPath = inputPath.IsPropertyPath() ? inputPath.GetPrimPath()
                                                                                        : inputPath;
                                    const UsdPrim prim = stage->GetPrimAtPath(primPath);
                                    if (!prim || !prim.IsValid()) {
                                        errors.append(QString("target prim missing: %1").arg(pathText(primPath)));
                                        continue;
                                    }

                                    MaterialBindingState state;
                                    if (!captureDirectMaterialBinding(prim, state)) {
                                        errors.append(
                                            QString("failed to capture material binding: %1").arg(pathText(primPath)));
                                        continue;
                                    }

                                    states->append(state);
                                }
                            }

                            for (const MaterialBindingState& state : *states) {
                                QString error;
                                if (applyDirectMaterialBinding(stage, state.primPath, materialPath, error))
                                    collector.append(state.primPath);
                                else
                                    errors.append(error);
                            }
                        }
                    }
                }

                const bool success = errors.isEmpty();
                const QString errorText = summarizeErrors(errors);
                command::queueToSession(session, [session, changed, success, errorText]() {
                    using Status = Session::Notify::Status;
                    command::finishDeferred(session,
                                            success ? "Material bound"
                                                    : appendError("Bind material finished with errors", errorText),
                                            changed, success ? Status::Success : Status::Error);
                });
            });
        },
        [states](Session* session) {
            if (!session || states->isEmpty())
                return;

            command::beginDeferred(session, "Undo bind material", static_cast<int>(states->size()));

            command::runWorker([session, states]() {
                QList<SdfPath> changed;
                path::PathCollector collector(changed);
                QStringList errors;

                {
                    WRITE_LOCKER(locker, session->stageLock(), "stageLock");
                    const UsdStageRefPtr stage = session->stageUnsafe();

                    if (!stage) {
                        errors.append("stage missing");
                    }
                    else {
                        for (const MaterialBindingState& state : *states) {
                            QString error;
                            if (restoreDirectMaterialBinding(stage, state, error))
                                collector.append(state.primPath);
                            else
                                errors.append(error);
                        }
                    }
                }

                const bool success = errors.isEmpty();
                const QString errorText = summarizeErrors(errors);
                command::queueToSession(session, [session, changed, success, errorText]() {
                    using Status = Session::Notify::Status;
                    command::finishDeferred(session,
                                            success ? "Material binding undone"
                                                    : appendError("Undo bind material finished with errors", errorText),
                                            changed, success ? Status::Success : Status::Error);
                });
            });
        });
}

Command
disconnectShaderInputs(const QList<SdfPath>& inputPaths)
{
    auto states = std::make_shared<QList<ShaderInputPropertyState>>();

    return Command(
        [inputPaths, states](Session* session) {
            if (!session || inputPaths.isEmpty())
                return;

            const QList<SdfPath> paths = path::uniquePaths(inputPaths);
            command::beginDeferred(session, "Disconnect shader inputs", static_cast<int>(paths.size()));
            command::runWorker([session, paths, states]() {
                QList<SdfPath> changed;
                path::PathCollector collector(changed);
                QStringList errors;
                {
                    WRITE_LOCKER(locker, session->stageLock(), "stageLock");
                    const UsdStageRefPtr stage = session->stageUnsafe();
                    if (!stage) {
                        errors.append("stage missing");
                    }
                    else {
                        QString editError;
                        const SdfLayerHandle editLayer = currentEditLayer(stage, editError);
                        if (!editLayer) {
                            errors.append(editError);
                        }
                        else {
                            states->clear();
                            UsdEditContext context(stage, UsdEditTarget(editLayer));
                            for (const SdfPath& propertyPath : paths) {
                                const UsdAttribute attr = stage->GetAttributeAtPath(propertyPath);
                                if (!attr) {
                                    errors.append(QString("shader input missing: %1").arg(pathText(propertyPath)));
                                    continue;
                                }

                                ShaderInputPropertyState state;
                                QString error;
                                if (!captureShaderInputProperty(editLayer, propertyPath, state, error)) {
                                    errors.append(error);
                                    continue;
                                }

                                SdfPathVector existing;
                                attr.GetConnections(&existing);
                                if (existing.empty())
                                    continue;

                                if (!attr.SetConnections(SdfPathVector())) {
                                    errors.append(
                                        QString("failed to disconnect shader input: %1").arg(pathText(propertyPath)));
                                    continue;
                                }

                                states->append(state);
                                collector.append(propertyPath.GetPrimPath());
                            }
                        }
                    }
                }

                const bool success = errors.isEmpty() && !changed.isEmpty();
                const QString errorText = summarizeErrors(errors);
                command::queueToSession(session, [session, changed, success, errorText]() {
                    using Status = Session::Notify::Status;
                    command::finishDeferred(session,
                                            success ? "Shader inputs disconnected"
                                                    : (errorText.isEmpty()
                                                           ? "No connected shader inputs"
                                                           : appendError("Disconnect shader inputs failed", errorText)),
                                            changed, success ? Status::Success : Status::Error);
                });
            });
        },
        [states](Session* session) {
            if (!session || states->isEmpty())
                return;

            command::beginDeferred(session, "Undo disconnect shader inputs", static_cast<int>(states->size()));
            command::runWorker([session, states]() {
                QList<SdfPath> changed;
                path::PathCollector collector(changed);
                QStringList errors;
                {
                    WRITE_LOCKER(locker, session->stageLock(), "stageLock");
                    const UsdStageRefPtr stage = session->stageUnsafe();
                    QString editError;
                    const SdfLayerHandle editLayer = currentEditLayer(stage, editError);
                    if (!stage || !editLayer) {
                        errors.append(!editError.isEmpty() ? editError : QStringLiteral("stage missing"));
                    }
                    else {
                        for (const ShaderInputPropertyState& state : *states) {
                            QString error;
                            if (restoreShaderInputProperty(editLayer, state, error))
                                collector.append(state.propertyPath.GetPrimPath());
                            else
                                errors.append(error);
                        }
                    }
                }
                const bool success = errors.isEmpty();
                const QString errorText = summarizeErrors(errors);
                command::queueToSession(session, [session, changed, success, errorText]() {
                    using Status = Session::Notify::Status;
                    command::finishDeferred(session,
                                            success ? "Shader input disconnect undone"
                                                    : appendError("Undo shader input disconnect failed", errorText),
                                            changed, success ? Status::Success : Status::Error);
                });
            });
        });
}

Command
connectShaderInput(const SdfPath& inputPath, const SdfPath& sourceOutputPath)
{
    auto state = std::make_shared<ShaderInputPropertyState>();
    auto captured = std::make_shared<bool>(false);

    return Command(
        [inputPath, sourceOutputPath, state, captured](Session* session) {
            if (!session || !inputPath.IsPropertyPath() || !sourceOutputPath.IsPropertyPath())
                return;

            command::beginDeferred(session, "Connect shader input", 1);
            command::runWorker([session, inputPath, sourceOutputPath, state, captured]() {
                bool success = false;
                bool repairedInputType = false;
                QString error;
                QList<SdfPath> changed;

                {
                    WRITE_LOCKER(locker, session->stageLock(), "stageLock");
                    const UsdStageRefPtr stage = session->stageUnsafe();
                    QString editError;
                    const SdfLayerHandle editLayer = currentEditLayer(stage, editError);
                    if (!stage || !editLayer) {
                        error = !editError.isEmpty() ? editError : QStringLiteral("stage missing");
                    }
                    else {
                        UsdAttribute inputAttr = stage->GetAttributeAtPath(inputPath);
                        const UsdAttribute outputAttr = stage->GetAttributeAtPath(sourceOutputPath);
                        UsdShadeInput input(inputAttr);
                        const UsdShadeOutput output(outputAttr);

                        if (!input || !output) {
                            error = QStringLiteral("shader input or output missing");
                        }
                        else {
                            QString captureError;
                            if (!captureShaderInputProperty(editLayer, inputPath, *state, captureError)) {
                                error = captureError;
                            }
                            else {
                                // MaterialGraph presents MaterialX sockets from the NodeDef interface.
                                // Older Stageviz builds could author a socket using a stale/wrong Sdf
                                // type (for example ND_image_color3.texcoord as float instead of
                                // float2). If that bad property belongs to the current edit layer,
                                // repair it transactionally before making the connection. Undo then
                                // restores the exact original property spec from the snapshot above.
                                const SdfValueTypeName declaredType = declaredMaterialXInputType(stage, inputPath);
                                if (!declaredType.GetAsToken().IsEmpty() && input.GetTypeName() != declaredType) {
                                    if (!editLayer->GetPropertyAtPath(inputPath)) {
                                        error = QStringLiteral(
                                                    "MaterialX input type disagrees with its NodeDef and the authored "
                                                    "property is not editable here: %1 is %2, expected %3")
                                                    .arg(pathText(inputPath),
                                                         QString::fromStdString(
                                                             input.GetTypeName().GetAsToken().GetString()),
                                                         QString::fromStdString(declaredType.GetAsToken().GetString()));
                                    }
                                    else if (!removePropertySpec(editLayer, inputPath)) {
                                        error = QStringLiteral(
                                                    "failed to replace incorrectly typed MaterialX input: %1")
                                                    .arg(pathText(inputPath));
                                    }
                                    else {
                                        UsdShadeShader targetShader(stage->GetPrimAtPath(inputPath.GetPrimPath()));
                                        if (!targetShader) {
                                            error = QStringLiteral("target MaterialX shader missing");
                                        }
                                        else {
                                            input = targetShader.CreateInput(input.GetBaseName(), declaredType);
                                            inputAttr = input ? input.GetAttr() : UsdAttribute();
                                            if (!input) {
                                                error = QStringLiteral(
                                                    "failed to recreate MaterialX input with NodeDef type");
                                            }
                                            else {
                                                repairedInputType = true;
                                            }
                                        }
                                    }
                                }

                                if (error.isEmpty()) {
                                    TfToken sourceShaderId;
                                    const UsdShadeShader sourceShader(output.GetPrim());
                                    if (sourceShader)
                                        sourceShader.GetIdAttr().Get(&sourceShaderId);

                                    const bool exactType = input.GetTypeName() == output.GetTypeName();
                                    const bool texcoord2Compatible
                                        = (input.GetTypeName() == SdfValueTypeNames->TexCoord2f
                                           && output.GetTypeName() == SdfValueTypeNames->Float2)
                                          || (input.GetTypeName() == SdfValueTypeNames->Float2
                                              && output.GetTypeName() == SdfValueTypeNames->TexCoord2f);
                                    const bool uvTextureRgbToColor = input.GetTypeName() == SdfValueTypeNames->Color3f
                                                                     && output.GetTypeName()
                                                                            == SdfValueTypeNames->Float3
                                                                     && sourceShaderId == TfToken("UsdUVTexture")
                                                                     && output.GetBaseName() == TfToken("rgb");

                                    if (!exactType && !texcoord2Compatible && !uvTextureRgbToColor) {
                                        error = QStringLiteral("shader socket types are incompatible: %1 -> %2")
                                                    .arg(QString::fromStdString(
                                                             output.GetTypeName().GetAsToken().GetString()),
                                                         QString::fromStdString(
                                                             input.GetTypeName().GetAsToken().GetString()));
                                    }
                                    else if (!input.ConnectToSource(output)) {
                                        error = QStringLiteral("failed to connect shader input");
                                    }
                                    else {
                                        *captured = true;
                                        success = true;
                                        path::appendUnique(changed, inputPath.GetPrimPath());
                                        path::appendUnique(changed, sourceOutputPath.GetPrimPath());
                                    }
                                }

                                if (!success && repairedInputType) {
                                    QString restoreError;
                                    if (!restoreShaderInputProperty(editLayer, *state, restoreError) && error.isEmpty())
                                        error = restoreError;
                                }
                            }
                        }
                    }
                }

                command::queueToSession(session, [session, changed, success, error]() {
                    using Status = Session::Notify::Status;
                    const QString message = success ? QStringLiteral("Shader input connected")
                                                    : appendError("Connect shader input failed", error);
                    command::finishDeferred(session, message, changed, success ? Status::Success : Status::Error);
                });
            });
        },
        [state, captured](Session* session) {
            if (!session || !*captured)
                return;

            command::beginDeferred(session, "Undo connect shader input", 1);
            command::runWorker([session, state]() {
                bool success = false;
                QString error;
                QList<SdfPath> changed;
                {
                    WRITE_LOCKER(locker, session->stageLock(), "stageLock");
                    const UsdStageRefPtr stage = session->stageUnsafe();
                    QString editError;
                    const SdfLayerHandle editLayer = currentEditLayer(stage, editError);
                    if (!stage || !editLayer) {
                        error = !editError.isEmpty() ? editError : QStringLiteral("stage missing");
                    }
                    else {
                        success = restoreShaderInputProperty(editLayer, *state, error);
                        if (success)
                            path::appendUnique(changed, state->propertyPath.GetPrimPath());
                    }
                }

                command::queueToSession(session, [session, changed, success, error]() {
                    using Status = Session::Notify::Status;
                    command::finishDeferred(session,
                                            success ? "Shader input connection undone"
                                                    : appendError("Undo shader input connection failed", error),
                                            changed, success ? Status::Success : Status::Error);
                });
            });
        });
}

Command
resetShaderInputs(const QList<SdfPath>& inputPaths)
{
    auto states = std::make_shared<QList<ShaderInputPropertyState>>();

    return Command(
        [inputPaths, states](Session* session) {
            if (!session || inputPaths.isEmpty())
                return;
            const QList<SdfPath> paths = path::uniquePaths(inputPaths);
            command::beginDeferred(session, "Reset shader inputs", static_cast<int>(paths.size()));
            command::runWorker([session, paths, states]() {
                QList<SdfPath> changed;
                path::PathCollector collector(changed);
                QStringList errors;
                {
                    WRITE_LOCKER(locker, session->stageLock(), "stageLock");
                    const UsdStageRefPtr stage = session->stageUnsafe();
                    QString editError;
                    const SdfLayerHandle editLayer = currentEditLayer(stage, editError);
                    if (!stage || !editLayer) {
                        errors.append(!editError.isEmpty() ? editError : QStringLiteral("stage missing"));
                    }
                    else {
                        states->clear();
                        for (const SdfPath& propertyPath : paths) {
                            if (!validateNotInstanceProxy(stage, propertyPath, editError)) {
                                errors.append(editError);
                                continue;
                            }
                            if (!editLayer->GetPropertyAtPath(propertyPath))
                                continue;
                            ShaderInputPropertyState state;
                            QString error;
                            if (!captureShaderInputProperty(editLayer, propertyPath, state, error)) {
                                errors.append(error);
                                continue;
                            }
                            if (!removePropertySpec(editLayer, propertyPath)) {
                                errors.append(QString("failed to reset shader input: %1").arg(pathText(propertyPath)));
                                continue;
                            }
                            states->append(state);
                            collector.append(propertyPath.GetPrimPath());
                        }
                    }
                }
                const bool success = errors.isEmpty() && !changed.isEmpty();
                const QString errorText = summarizeErrors(errors);
                command::queueToSession(session, [session, changed, success, errorText]() {
                    using Status = Session::Notify::Status;
                    command::finishDeferred(session,
                                            success ? "Shader inputs reset"
                                                    : (errorText.isEmpty()
                                                           ? "No edit-layer shader inputs to reset"
                                                           : appendError("Reset shader inputs failed", errorText)),
                                            changed, success ? Status::Success : Status::Error);
                });
            });
        },
        [states](Session* session) {
            if (!session || states->isEmpty())
                return;
            command::beginDeferred(session, "Undo reset shader inputs", static_cast<int>(states->size()));
            command::runWorker([session, states]() {
                QList<SdfPath> changed;
                path::PathCollector collector(changed);
                QStringList errors;
                {
                    WRITE_LOCKER(locker, session->stageLock(), "stageLock");
                    const UsdStageRefPtr stage = session->stageUnsafe();
                    QString editError;
                    const SdfLayerHandle editLayer = currentEditLayer(stage, editError);
                    if (!stage || !editLayer) {
                        errors.append(!editError.isEmpty() ? editError : QStringLiteral("stage missing"));
                    }
                    else {
                        for (const ShaderInputPropertyState& state : *states) {
                            QString error;
                            if (!validateNotInstanceProxy(stage, state.propertyPath, error))
                                errors.append(error);
                            else if (restoreShaderInputProperty(editLayer, state, error))
                                collector.append(state.propertyPath.GetPrimPath());
                            else
                                errors.append(error);
                        }
                    }
                }
                const bool success = errors.isEmpty();
                const QString errorText = summarizeErrors(errors);
                command::queueToSession(session, [session, changed, success, errorText]() {
                    using Status = Session::Notify::Status;
                    command::finishDeferred(session,
                                            success ? "Shader input reset undone"
                                                    : appendError("Undo shader input reset failed", errorText),
                                            changed, success ? Status::Success : Status::Error);
                });
            });
        });
}

Command
connectShaderNode(const SdfPath& inputPath, const QString& shaderId, const QString& nodeName, const TfToken& outputName)
{
    struct State {
        ShaderInputPropertyState input;
        SdfPath nodePath;
        bool captured = false;
    };
    auto state = std::make_shared<State>();

    return Command(
        [inputPath, shaderId, nodeName, outputName, state](Session* session) {
            if (!session || inputPath.IsEmpty() || shaderId.isEmpty() || outputName.IsEmpty())
                return;
            command::beginDeferred(session, "Connect shader node", 1);
            command::runWorker([session, inputPath, shaderId, nodeName, outputName, state]() {
                bool success = false;
                QString error;
                QList<SdfPath> changed;
                {
                    WRITE_LOCKER(locker, session->stageLock(), "stageLock");
                    const UsdStageRefPtr stage = session->stageUnsafe();
                    QString editError;
                    const SdfLayerHandle editLayer = currentEditLayer(stage, editError);
                    if (!stage || !editLayer) {
                        error = !editError.isEmpty() ? editError : QStringLiteral("stage missing");
                    }
                    else if (!validateNotInstanceProxy(stage, inputPath, error)) {
                        // Keep the error from the shared path guard.
                    }
                    else {
                        const UsdAttribute attr = stage->GetAttributeAtPath(inputPath);
                        if (!attr) {
                            error = QString("shader input missing: %1").arg(pathText(inputPath));
                        }
                        else {
                            if (!state->captured) {
                                if (!captureShaderInputProperty(editLayer, inputPath, state->input, error)) {
                                    // error already set
                                }
                                else {
                                    QString cleanName = nodeName.isEmpty() ? QStringLiteral("Node") : nodeName;
                                    const SdfPath parentPath = inputPath.GetPrimPath().GetParentPath();
                                    state->nodePath = stage::buildChildPath(stage, parentPath, cleanName, error);
                                    state->captured = !state->nodePath.IsEmpty();
                                }
                            }

                            if (state->captured) {
                                UsdEditContext context(stage, UsdEditTarget(editLayer));
                                UsdShadeShader shader = UsdShadeShader::Define(stage, state->nodePath);
                                if (!shader || !shader.CreateIdAttr(VtValue(TfToken(qt::QStringToString(shaderId))))) {
                                    error = "failed to create shader node";
                                }
                                else {
                                    SdfValueTypeName outputType = attr.GetTypeName();
                                    const QString id = shaderId;
                                    if (id == QStringLiteral("UsdUVTexture")) {
                                        outputType = outputName == TfToken("rgb") ? SdfValueTypeNames->Float3
                                                                                  : SdfValueTypeNames->Float;
                                        shader.CreateInput(TfToken("file"), SdfValueTypeNames->Asset);
                                        shader.CreateInput(TfToken("st"), SdfValueTypeNames->Float2);
                                        shader.CreateInput(TfToken("fallback"), SdfValueTypeNames->Float4);
                                        shader.CreateInput(TfToken("wrapS"), SdfValueTypeNames->Token);
                                        shader.CreateInput(TfToken("wrapT"), SdfValueTypeNames->Token);
                                    }
                                    else if (id.startsWith(QStringLiteral("UsdPrimvarReader_"))) {
                                        // Author the complete Preview Surface primvar-reader
                                        // interface. The previous connected-node path only
                                        // created `varname`; free nodes created by MaterialGraph
                                        // also had `fallback`. Leaving that input absent produces
                                        // an incomplete shader node and can take Storm down when
                                        // the freshly-authored network is synchronized for the
                                        // material swatch.
                                        SdfValueTypeName valueType;
                                        if (id.endsWith(QStringLiteral("_float")))
                                            valueType = SdfValueTypeNames->Float;
                                        else if (id.endsWith(QStringLiteral("_float2")))
                                            valueType = SdfValueTypeNames->Float2;
                                        else if (id.endsWith(QStringLiteral("_float3")))
                                            valueType = SdfValueTypeNames->Float3;
                                        else if (id.endsWith(QStringLiteral("_float4")))
                                            valueType = SdfValueTypeNames->Float4;
                                        else if (id.endsWith(QStringLiteral("_int")))
                                            valueType = SdfValueTypeNames->Int;
                                        else if (id.endsWith(QStringLiteral("_string")))
                                            valueType = SdfValueTypeNames->String;

                                        if (!valueType.GetAsToken().IsEmpty()) {
                                            outputType = valueType;
                                            UsdShadeInput varname = shader.CreateInput(TfToken("varname"),
                                                                                       SdfValueTypeNames->Token);
                                            if (varname)
                                                varname.Set(TfToken());

                                            UsdShadeInput fallback = shader.CreateInput(TfToken("fallback"), valueType);
                                            if (fallback) {
                                                const VtValue defaultValue = valueType.GetDefaultValue();
                                                if (!defaultValue.IsEmpty())
                                                    fallback.Set(defaultValue);
                                            }
                                        }
                                    }
                                    else if (id == QStringLiteral("UsdTransform2d")) {
                                        outputType = SdfValueTypeNames->Float2;
                                        shader.CreateInput(TfToken("in"), SdfValueTypeNames->Float2);
                                        shader.CreateInput(TfToken("rotation"), SdfValueTypeNames->Float);
                                        shader.CreateInput(TfToken("scale"), SdfValueTypeNames->Float2);
                                        shader.CreateInput(TfToken("translation"), SdfValueTypeNames->Float2);
                                    }

                                    const UsdShadeOutput output = shader.CreateOutput(outputName, outputType);
                                    const UsdShadeInput input(attr);

                                    // Do not author generic Float3 -> Color3f shader connections.
                                    // Although the value representations are storage-compatible,
                                    // Storm does not treat every shader semantic as interchangeable
                                    // and can crash compiling e.g. UsdPrimvarReader_float3.result
                                    // into UsdPreviewSurface.diffuseColor. UsdUVTexture.rgb is the
                                    // standard USD Preview exception and is deliberately allowed.
                                    const bool float3ToColor3 = input && outputType == SdfValueTypeNames->Float3
                                                                && input.GetTypeName() == SdfValueTypeNames->Color3f;
                                    const bool safeUvTextureRgb = id == QStringLiteral("UsdUVTexture")
                                                                  && outputName == TfToken("rgb");

                                    if (float3ToColor3 && !safeUvTextureRgb) {
                                        error = QStringLiteral(
                                            "shader socket types are incompatible: float3 -> color3f");
                                        stage::removePrimSpec(editLayer, state->nodePath);
                                        QString restoreError;
                                        restoreShaderInputProperty(editLayer, state->input, restoreError);
                                        state->captured = false;
                                        state->nodePath = SdfPath();
                                    }
                                    else if (!output || !input || !input.ConnectToSource(output)) {
                                        error = "failed to connect shader node";
                                        stage::removePrimSpec(editLayer, state->nodePath);
                                        QString restoreError;
                                        restoreShaderInputProperty(editLayer, state->input, restoreError);
                                        state->captured = false;
                                        state->nodePath = SdfPath();
                                    }
                                    else {
                                        success = true;
                                        path::appendUnique(changed, inputPath.GetPrimPath());
                                        path::appendUnique(changed, state->nodePath);
                                    }
                                }
                            }
                        }
                    }
                }
                command::queueToSession(session, [session, changed, success, error]() {
                    using Status = Session::Notify::Status;
                    const QString message = success ? QStringLiteral("Shader node connected")
                                                    : appendError("Connect shader node failed", error);
                    command::finishDeferred(session, message, changed, success ? Status::Success : Status::Error);
                });
            });
        },
        [state](Session* session) {
            if (!session || !state->captured)
                return;
            command::beginDeferred(session, "Undo connect shader node", 1);
            command::runWorker([session, state]() {
                bool success = false;
                QString error;
                QList<SdfPath> changed;
                {
                    WRITE_LOCKER(locker, session->stageLock(), "stageLock");
                    const UsdStageRefPtr stage = session->stageUnsafe();
                    QString editError;
                    const SdfLayerHandle editLayer = currentEditLayer(stage, editError);
                    if (!stage || !editLayer) {
                        error = !editError.isEmpty() ? editError : QStringLiteral("stage missing");
                    }
                    else {
                        if (!validateNotInstanceProxy(stage, state->input.propertyPath, error)) {
                            // Keep the undo path from restoring an input through a proxy.
                        }
                        else if (!stage::removePrimSpec(editLayer, state->nodePath))
                            error = QString("failed to remove shader node: %1").arg(pathText(state->nodePath));
                        else {
                            QString restoreError;
                            if (!restoreShaderInputProperty(editLayer, state->input, restoreError) && error.isEmpty())
                                error = restoreError;
                        }
                        success = error.isEmpty();
                        path::appendUnique(changed, state->input.propertyPath.GetPrimPath());
                        path::appendUnique(changed, state->nodePath);
                    }
                }
                command::queueToSession(session, [session, changed, success, error]() {
                    using Status = Session::Notify::Status;
                    command::finishDeferred(session,
                                            success ? "Shader node connection undone"
                                                    : appendError("Undo connect shader node failed", error),
                                            changed, success ? Status::Success : Status::Error);
                });
            });
        });
}

Command
connectMaterialXNode(const SdfPath& inputPath, const QString& nodeDef, const QString& nodeName)
{
    struct State {
        ShaderInputPropertyState input;
        SdfPath nodePath;
        bool captured = false;
    };

    auto state = std::make_shared<State>();

    return Command(
        [inputPath, nodeDef, nodeName, state](Session* session) {
            if (!session || !inputPath.IsPropertyPath() || nodeDef.isEmpty())
                return;

            command::beginDeferred(session, "Connect MaterialX node", 1);
            command::runWorker([session, inputPath, nodeDef, nodeName, state]() {
                bool success = false;
                QString error;
                QList<SdfPath> changed;

                {
                    WRITE_LOCKER(locker, session->stageLock(), "stageLock");
                    const UsdStageRefPtr stage = session->stageUnsafe();
                    QString editError;
                    const SdfLayerHandle editLayer = currentEditLayer(stage, editError);
                    if (!stage || !editLayer) {
                        error = !editError.isEmpty() ? editError : QStringLiteral("stage missing");
                    }
                    else if (!validateNotInstanceProxy(stage, inputPath, error)) {
                        // Keep the error from the shared path guard.
                    }
                    else {
                        const UsdAttribute inputAttr = stage->GetAttributeAtPath(inputPath);
                        const UsdShadeInput targetInput(inputAttr);
                        if (!targetInput) {
                            error = QStringLiteral("shader input missing: %1").arg(pathText(inputPath));
                        }
                        else {
                            MaterialXNodeDefinition definition;
                            if (!MaterialUtils::materialXNodeDefinition(nodeDef, &definition)) {
                                error = QStringLiteral("MaterialX NodeDef not found: %1").arg(nodeDef);
                            }
                            else {
                                const SdfValueTypeName outputType
                                    = MaterialUtils::materialXPortType(definition, QStringLiteral("out"), true);
                                if (outputType.GetAsToken().IsEmpty()) {
                                    error = QStringLiteral("MaterialX NodeDef has no typed out output: %1").arg(nodeDef);
                                }
                                else if (targetInput.GetTypeName() != outputType) {
                                    error = QStringLiteral("shader socket types are incompatible: %1 -> %2")
                                                .arg(QString::fromStdString(outputType.GetAsToken().GetString()),
                                                     QString::fromStdString(
                                                         targetInput.GetTypeName().GetAsToken().GetString()));
                                }
                                else {
                                    if (!state->captured) {
                                        if (!captureShaderInputProperty(editLayer, inputPath, state->input, error)) {
                                            // error already set
                                        }
                                        else {
                                            const QString base = nodeName.isEmpty()
                                                                     ? (definition.node.isEmpty()
                                                                            ? QStringLiteral("MaterialXNode")
                                                                            : definition.node)
                                                                     : nodeName;
                                            state->nodePath
                                                = stage::buildChildPath(stage, inputPath.GetPrimPath().GetParentPath(),
                                                                        base, error);
                                            state->captured = !state->nodePath.IsEmpty();
                                        }
                                    }

                                    if (state->captured && error.isEmpty()) {
                                        UsdEditContext context(stage, UsdEditTarget(editLayer));
                                        UsdShadeShader shader = UsdShadeShader::Define(stage, state->nodePath);
                                        if (!shader) {
                                            error = QStringLiteral("failed to create MaterialX shader node");
                                        }
                                        else if (!MaterialUtils::authorMaterialXNodeInterface(shader, definition,
                                                                                              error)) {
                                            // error already set
                                        }
                                        else {
                                            const UsdShadeOutput output = shader.GetOutput(TfToken("out"));
                                            if (!output || !targetInput.ConnectToSource(output)) {
                                                error = QStringLiteral("failed to connect MaterialX shader node");
                                            }
                                            else {
                                                success = true;
                                                path::appendUnique(changed, inputPath.GetPrimPath());
                                                path::appendUnique(changed, state->nodePath);
                                            }
                                        }
                                    }
                                }
                            }
                        }

                        if (!success && state->captured && !state->nodePath.IsEmpty()) {
                            stage::removePrimSpec(editLayer, state->nodePath);
                            QString restoreError;
                            restoreShaderInputProperty(editLayer, state->input, restoreError);
                            state->captured = false;
                            state->nodePath = SdfPath();
                            if (error.isEmpty())
                                error = restoreError;
                        }
                    }
                }

                command::queueToSession(session, [session, changed, success, error]() {
                    using Status = Session::Notify::Status;
                    const QString message = success ? QStringLiteral("MaterialX node connected")
                                                    : appendError("Connect MaterialX node failed", error);
                    command::finishDeferred(session, message, changed, success ? Status::Success : Status::Error);
                });
            });
        },
        [state](Session* session) {
            if (!session || !state->captured || state->nodePath.IsEmpty())
                return;

            command::beginDeferred(session, "Undo connect MaterialX node", 1);
            command::runWorker([session, state]() {
                bool success = false;
                QString error;
                QList<SdfPath> changed;
                {
                    WRITE_LOCKER(locker, session->stageLock(), "stageLock");
                    const UsdStageRefPtr stage = session->stageUnsafe();
                    QString editError;
                    const SdfLayerHandle editLayer = currentEditLayer(stage, editError);
                    if (!stage || !editLayer) {
                        error = !editError.isEmpty() ? editError : QStringLiteral("stage missing");
                    }
                    else {
                        if (!validateNotInstanceProxy(stage, state->input.propertyPath, error)) {
                            // Keep the undo path from restoring an input through a proxy.
                        }
                        else if (!stage::removePrimSpec(editLayer, state->nodePath))
                            error = QStringLiteral("failed to remove MaterialX node: %1").arg(pathText(state->nodePath));
                        else {
                            QString restoreError;
                            if (!restoreShaderInputProperty(editLayer, state->input, restoreError) && error.isEmpty())
                                error = restoreError;
                        }

                        success = error.isEmpty();
                        path::appendUnique(changed, state->input.propertyPath.GetPrimPath());
                        path::appendUnique(changed, state->nodePath);
                    }
                }

                command::queueToSession(session, [session, changed, success, error]() {
                    using Status = Session::Notify::Status;
                    command::finishDeferred(session,
                                            success ? "MaterialX node connection undone"
                                                    : appendError("Undo MaterialX node connection failed", error),
                                            changed, success ? Status::Success : Status::Error);
                });
            });
        });
}

Command
newShaderNode(const SdfPath& materialPath, const QString& shaderId, const QString& nodeName, const TfToken& outputName,
              const SdfValueTypeName& outputType)
{
    struct State {
        SdfPath nodePath;
        bool captured = false;
    };

    auto state = std::make_shared<State>();

    return Command(
        [materialPath, shaderId, nodeName, outputName, outputType, state](Session* session) {
            if (!session || materialPath.IsEmpty() || shaderId.isEmpty())
                return;

            WRITE_LOCKER(locker, session->stageLock(), "stageLock");
            const UsdStageRefPtr stage = session->stageUnsafe();
            if (!stage)
                return;

            const SdfLayerHandle editLayer = stage->GetEditTarget().GetLayer();
            if (!editLayer)
                return;

            QString error;
            if (!validateNotInstanceProxy(stage, materialPath, error))
                return;

            if (!state->captured) {
                const QString base = nodeName.isEmpty() ? QStringLiteral("Node") : nodeName;
                state->nodePath = stage::buildChildPath(stage, materialPath, base, error);
                state->captured = !state->nodePath.IsEmpty();
            }
            if (!state->captured)
                return;

            UsdEditContext context(stage, UsdEditTarget(editLayer));
            UsdShadeShader shader = UsdShadeShader::Define(stage, state->nodePath);
            if (!shader)
                return;

            shader.CreateIdAttr(VtValue(TfToken(qt::QStringToString(shaderId))));
            MaterialUtils::authorUsdNodeDefaults(shader, shaderId);

            if (!outputName.IsEmpty() && !shader.GetOutput(outputName))
                shader.CreateOutput(outputName, outputType);
        },
        [state](Session* session) {
            if (!session || !state->captured || state->nodePath.IsEmpty())
                return;

            WRITE_LOCKER(locker, session->stageLock(), "stageLock");
            const UsdStageRefPtr stage = session->stageUnsafe();
            if (!stage)
                return;

            const SdfLayerHandle editLayer = stage->GetEditTarget().GetLayer();
            QString error;
            if (editLayer && validateNotInstanceProxy(stage, state->nodePath, error))
                stage::removePrimSpec(editLayer, state->nodePath);
        });
}

Command
newMaterialXNode(const SdfPath& materialPath, const MaterialXNodeDefinition& definition)
{
    struct State {
        SdfPath nodePath;
        bool captured = false;
    };

    auto state = std::make_shared<State>();

    return Command(
        [materialPath, definition, state](Session* session) {
            if (!session || materialPath.IsEmpty() || definition.nodeDef.isEmpty())
                return;

            WRITE_LOCKER(locker, session->stageLock(), "stageLock");
            const UsdStageRefPtr stage = session->stageUnsafe();
            if (!stage)
                return;

            const SdfLayerHandle editLayer = stage->GetEditTarget().GetLayer();
            if (!editLayer)
                return;

            QString error;
            if (!validateNotInstanceProxy(stage, materialPath, error))
                return;
            if (!state->captured) {
                const QString base = definition.node.isEmpty() ? QStringLiteral("MaterialXNode") : definition.node;
                state->nodePath = stage::buildChildPath(stage, materialPath, base, error);
                state->captured = !state->nodePath.IsEmpty();
            }
            if (!state->captured)
                return;

            UsdEditContext context(stage, UsdEditTarget(editLayer));
            UsdShadeShader shader = UsdShadeShader::Define(stage, state->nodePath);
            if (!shader)
                return;

            if (!MaterialUtils::authorMaterialXNodeInterface(shader, definition, error)) {
                stage::removePrimSpec(editLayer, state->nodePath);
                state->captured = false;
                state->nodePath = SdfPath();
            }
        },
        [state](Session* session) {
            if (!session || !state->captured || state->nodePath.IsEmpty())
                return;

            WRITE_LOCKER(locker, session->stageLock(), "stageLock");
            const UsdStageRefPtr stage = session->stageUnsafe();
            if (!stage)
                return;

            const SdfLayerHandle editLayer = stage->GetEditTarget().GetLayer();
            QString error;
            if (editLayer && validateNotInstanceProxy(stage, state->nodePath, error))
                stage::removePrimSpec(editLayer, state->nodePath);
        });
}

Command
deleteShaderNode(const SdfPath& nodePath)
{
    struct State {
        SdfLayerRefPtr backup = SdfLayer::CreateAnonymous("stageviz_material_node_backup.usda");
        bool hadEditSpec = false;
        bool captured = false;
    };

    auto state = std::make_shared<State>();

    return Command(
        [nodePath, state](Session* session) {
            if (!session || nodePath.IsEmpty())
                return;

            WRITE_LOCKER(locker, session->stageLock(), "stageLock");
            const UsdStageRefPtr stage = session->stageUnsafe();
            if (!stage)
                return;

            const SdfLayerHandle editLayer = stage->GetEditTarget().GetLayer();
            if (!editLayer)
                return;

            QString error;
            if (!validateNotInstanceProxy(stage, nodePath, error))
                return;

            if (!state->captured) {
                state->hadEditSpec = bool(editLayer->GetPrimAtPath(nodePath));
                if (state->hadEditSpec)
                    SdfCopySpec(editLayer, nodePath, state->backup, nodePath);
                state->captured = true;
            }

            if (state->hadEditSpec) {
                stage::removePrimSpec(editLayer, nodePath);
            }
            else {
                UsdEditContext context(stage, UsdEditTarget(editLayer));
                UsdPrim prim = stage->OverridePrim(nodePath);
                if (prim)
                    prim.SetActive(false);
            }
        },
        [nodePath, state](Session* session) {
            if (!session || !state->captured || nodePath.IsEmpty())
                return;

            WRITE_LOCKER(locker, session->stageLock(), "stageLock");
            const UsdStageRefPtr stage = session->stageUnsafe();
            if (!stage)
                return;

            const SdfLayerHandle editLayer = stage->GetEditTarget().GetLayer();
            if (!editLayer)
                return;

            QString error;
            if (!validateNotInstanceProxy(stage, nodePath, error))
                return;

            if (state->hadEditSpec)
                SdfCopySpec(state->backup, nodePath, editLayer, nodePath);
            else
                stage::removePrimSpec(editLayer, nodePath);
        });
}

Command
newMaterialPath(const SdfPath& parentPath, const QString& nameInput)
{
    struct NewMaterialState {
        SdfPath parentPath;
        SdfPath createdPath;
        TfTokenVector oldParentOrder;
        QList<SdfPath> createdAncestorSpecs;
        QList<SdfPath> previousSelection;
        QList<SdfPath> previousMask;
    };

    auto state = std::make_shared<NewMaterialState>();

    return Command(
        [parentPath, nameInput, state](Session* session) {
            if (!session || parentPath.IsEmpty())
                return;

            state->previousSelection = session->selectionList()->paths();
            state->previousMask = session->mask();

            command::beginDeferred(session, "New material", 1);

            command::runWorker([session, parentPath, nameInput, state]() {
                bool success = false;
                QString error;
                SdfPath materialPath;
                QList<SdfPath> changed;

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
                        else if (!layer::validateParent(stage, editLayer, parentPath, error)) {}
                        else {
                            materialPath = stage::buildChildPath(stage, parentPath, nameInput, error);

                            if (!materialPath.IsEmpty()) {
                                state->parentPath = parentPath;
                                state->createdPath = materialPath;
                                state->oldParentOrder.clear();
                                state->createdAncestorSpecs.clear();

                                stage::captureChildOrder(stage, parentPath, state->oldParentOrder);

                                for (SdfPath path = parentPath; !path.IsEmpty() && path != SdfPath::AbsoluteRootPath();
                                     path = path.GetParentPath()) {
                                    if (!editLayer->GetPrimAtPath(path))
                                        state->createdAncestorSpecs.prepend(path);
                                }

                                UsdEditContext context(stage, UsdEditTarget(editLayer));

                                const UsdShadeMaterial material = UsdShadeMaterial::Define(stage, materialPath);
                                const SdfPath shaderPath = materialPath.AppendChild(TfToken("PreviewSurface"));
                                const UsdShadeShader shader = UsdShadeShader::Define(stage, shaderPath);

                                if (!material || !material.GetPrim() || !shader || !shader.GetPrim()) {
                                    error = "failed to define material";
                                }
                                else if (!shader.CreateIdAttr(VtValue(TfToken("UsdPreviewSurface")))) {
                                    error = "failed to create preview surface shader";
                                }
                                else {
                                    UsdShadeMaterial material = UsdShadeMaterial::Define(stage, materialPath);

                                    const SdfPath shaderPath = materialPath.AppendChild(TfToken("PreviewSurface"));
                                    UsdShadeShader shader = UsdShadeShader::Define(stage, shaderPath);

                                    if (!material || !material.GetPrim() || !shader || !shader.GetPrim()) {
                                        error = "failed to define material";
                                    }
                                    else if (!shader.CreateIdAttr(VtValue(TfToken("UsdPreviewSurface")))) {
                                        error = "failed to create preview surface shader";
                                    }
                                    else {
                                        const UsdShadeOutput shaderOutput
                                            = shader.CreateOutput(TfToken("surface"), SdfValueTypeNames->Token);

                                        const UsdShadeOutput materialOutput = material.CreateSurfaceOutput();

                                        if (!shaderOutput || !materialOutput
                                            || !materialOutput.ConnectToSource(shaderOutput)) {
                                            error = "failed to connect material surface output";
                                        }
                                        else {
                                            TfTokenVector order = state->oldParentOrder;
                                            order.push_back(materialPath.GetNameToken());
                                            stage::restoreChildOrder(stage, parentPath, order);

                                            path::appendUnique(changed, parentPath);
                                            path::appendUnique(changed, materialPath);

                                            success = true;
                                        }
                                    }
                                }

                                if (!success)
                                    stage::removePrimSpec(editLayer, materialPath);
                            }
                        }
                    }
                }

                command::queueToSession(session, [session, materialPath, changed, success, error]() {
                    using Status = Session::Notify::Status;

                    command::finishDeferred(session,
                                            success ? "Material created" : appendError("New material failed", error),
                                            changed, success ? Status::Success : Status::Error);

                    if (success)
                        session->selectionList()->updatePaths({ materialPath });
                });
            });
        },
        [state](Session* session) {
            if (!session || state->createdPath.IsEmpty())
                return;

            command::beginDeferred(session, "Undo new material", 1);

            command::runWorker([session, state]() {
                bool success = false;
                QString error;
                QList<SdfPath> changed;

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
                        else if (!stage::removePrimSpec(editLayer, state->createdPath)) {
                            error = QString("failed to remove material: %1").arg(pathText(state->createdPath));
                        }
                        else {
                            stage::restoreChildOrder(stage, state->parentPath, state->oldParentOrder);

                            for (auto it = state->createdAncestorSpecs.crbegin();
                                 it != state->createdAncestorSpecs.crend(); ++it) {
                                const SdfPrimSpecHandle spec = editLayer->GetPrimAtPath(*it);
                                if (spec && spec->IsInert())
                                    stage::removePrimSpec(editLayer, *it);
                            }

                            path::appendUnique(changed, state->parentPath);
                            path::appendUnique(changed, state->createdPath);
                            success = true;
                        }
                    }
                }

                command::queueToSession(session, [session, state, changed, success, error]() {
                    using Status = Session::Notify::Status;

                    command::finishDeferred(session,
                                            success ? "New material undone"
                                                    : appendError("Undo new material failed", error),
                                            changed, success ? Status::Success : Status::Error);

                    if (success) {
                        session->selectionList()->updatePaths(state->previousSelection);
                        session->setMask(state->previousMask);
                    }
                });
            });
        });
}

}  // namespace stageviz
