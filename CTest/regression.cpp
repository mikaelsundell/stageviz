// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell

#include "command.h"
#include "commandstack.h"
#include "materialmenu.h"
#include "materialutils.h"
#include "selectionlist.h"
#include "session.h"
#include "usdedit.h"
#include "usdutils.h"
#include "viewcamera.h"
#include "viewstate.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QThreadPool>
#include <algorithm>
#include <cmath>
#include <functional>
#include <iostream>
#include <map>
#include <pxr/base/gf/matrix4d.h>
#include <pxr/base/gf/range3d.h>
#include <pxr/base/gf/rotation.h>
#include <pxr/base/tf/errorMark.h>
#include <pxr/usd/sdf/assetPath.h>
#include <pxr/usd/sdf/layer.h>
#include <pxr/usd/sdf/schema.h>
#include <pxr/usd/sdf/types.h>
#include <pxr/usd/usd/attribute.h>
#include <pxr/usd/usd/payloads.h>
#include <pxr/usd/usd/references.h>
#include <pxr/usd/usd/relationship.h>
#include <pxr/usd/usd/variantSets.h>
#include <pxr/usd/usdGeom/cube.h>
#include <pxr/usd/usdGeom/mesh.h>
#include <pxr/usd/usdGeom/subset.h>
#include <pxr/usd/usdGeom/scope.h>
#include <pxr/usd/usdGeom/xform.h>
#include <pxr/usd/usdGeom/xformCommonAPI.h>
#include <pxr/usd/usdGeom/xformable.h>
#include <pxr/usd/usdShade/material.h>
#include <pxr/usd/usdShade/materialBindingAPI.h>
#include <pxr/usd/usdShade/shader.h>
#include <stdexcept>

PXR_NAMESPACE_USING_DIRECTIVE
using stageviz::SelectionList;
using stageviz::Session;
using stageviz::ViewCamera;
using stageviz::ViewState;

namespace {
void
require(bool result, const char* message)
{
    if (!result)
        throw std::runtime_error(message);
}

void
writeFile(const QString& path, const QByteArray& bytes)
{
    QFile file(path);
    require(file.open(QIODevice::WriteOnly), "open fixture");
    require(file.write(bytes) == bytes.size(), "write fixture");
}

UsdStageRefPtr
diskStage(const QString& path)
{
    // Bypass the live layer registry so assertions inspect disk content.
    const auto layer = SdfLayer::OpenAsAnonymous(path.toStdString());
    require(bool(layer), "read saved layer from disk");
    const auto stage = UsdStage::Open(layer);
    require(bool(stage), "open saved layer");
    return stage;
}

bool
closeEnough(double a, double b, double epsilon = 1e-6)
{
    return std::abs(a - b) <= epsilon;
}

stageviz::XformState
captureXformState(const SdfLayerHandle& layer, const SdfPath& path)
{
    stageviz::XformState state;
    if (!layer)
        return state;

    state.hadPrimSpec = bool(layer->GetPrimAtPath(path));
    const SdfPath orderPath = path.AppendProperty(TfToken("xformOpOrder"));
    const SdfPath matrixPath = path.AppendProperty(TfToken("xformOp:transform"));
    state.hadXformOpOrderSpec = bool(layer->GetPropertyAtPath(orderPath));
    state.hadXformOpOrderDefault = layer->HasField(orderPath, SdfFieldKeys->Default);
    if (state.hadXformOpOrderDefault)
        state.xformOpOrderDefault = layer->GetField(orderPath, SdfFieldKeys->Default);
    state.hadMatrixOpSpec = bool(layer->GetPropertyAtPath(matrixPath));
    state.hadMatrixOpDefault = layer->HasField(matrixPath, SdfFieldKeys->Default);
    if (state.hadMatrixOpDefault)
        state.matrixOpDefault = layer->GetField(matrixPath, SdfFieldKeys->Default);
    return state;
}

void
drainWorkers()
{
    // Stageviz commands intentionally do their USD work on the global Qt
    // thread pool and queue completion back to the Session thread. Drain
    // both sides so command-factory tests remain deterministic.
    for (int i = 0; i < 6; ++i) {
        QThreadPool::globalInstance()->waitForDone();
        QCoreApplication::sendPostedEvents(nullptr, 0);
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    }
}

void
executeCommand(stageviz::Command& command, Session& session)
{
    command.execute(&session);
    drainWorkers();
}

void
undoCommand(stageviz::Command& command, Session& session)
{
    command.undo(&session);
    drainWorkers();
}

QString
makeAssetFixture(const QString& path, const char* rootName = "/Asset")
{
    const auto stage = UsdStage::CreateNew(path.toStdString());
    require(bool(stage), "create asset fixture");
    const SdfPath root(rootName);
    stage->DefinePrim(root, TfToken("Xform"));
    stage->DefinePrim(root.AppendChild(TfToken("Geometry")), TfToken("Xform"));
    stage->SetDefaultPrim(stage->GetPrimAtPath(root));
    require(stage->GetRootLayer()->Save(), "save asset fixture");
    return path;
}

void
saveTwice(const QString& extension)
{
    QTemporaryDir dir;
    require(dir.isValid(), "temporary directory");
    Session session;
    session.setPreserveState(false);
    require(session.newStage(), "new stage");
    const QString path = dir.filePath("scene." + extension);

    session.stage()->DefinePrim(SdfPath("/World/First"));
    require(session.saveToFile(path), "first save");
    require(!session.stage()->GetRootLayer()->IsAnonymous(), "first save retargets anonymous root layer");
    require(session.stage()->GetRootLayer()->GetRealPath() == path.toStdString(),
            "first save retargets root layer to destination");
    require(!session.stage()->GetRootLayer()->IsDirty(), "first save clears root dirty state");

    session.stage()->RemovePrim(SdfPath("/World/First"));
    session.stage()->DefinePrim(SdfPath("/World/Second"));
    require(session.saveToFile(path), "second save");
    require(!session.stage()->GetRootLayer()->IsDirty(), "second save clears root dirty state");

    const auto reopened = diskStage(path);
    require(!reopened->GetPrimAtPath(SdfPath("/World/First")), "deleted prim remains deleted on disk");
    require(bool(reopened->GetPrimAtPath(SdfPath("/World/Second"))), "second edit saved");
}

void
saveFailure()
{
    QTemporaryDir dir;
    require(dir.isValid(), "temporary directory");
    Session session;
    session.setPreserveState(false);
    require(session.newStage(), "new stage");
    const QString target = dir.filePath("scene.usda");
    // An existing directory at the destination deterministically fails the
    // replacement even when the test runs with elevated filesystem rights.
    require(QDir().mkpath(target), "create occupied destination");
    writeFile(target + "/original", "keep me");
    QString error;
    QObject::connect(&session, &Session::notifyStatusChanged, &session,
                     [&](Session::Notify::Status status, const QString& message, const QString&) {
                         if (status == Session::Notify::Status::Error)
                             error = message;
                     });
    session.stage()->DefinePrim(SdfPath("/World/Unsaved"));
    require(session.stage()->GetRootLayer()->IsAnonymous(), "failed-save fixture starts anonymous");
    require(session.stage()->GetRootLayer()->IsDirty(), "failed-save fixture starts dirty");

    TfErrorMark mark;
    require(!session.saveToFile(target), "save must fail");
    QCoreApplication::processEvents();
    mark.Clear();

    require(session.stage()->GetRootLayer()->IsAnonymous(), "failed save does not retarget live root layer");
    require(session.stage()->GetRootLayer()->IsDirty(), "failed save keeps live root dirty");
    require(bool(session.stage()->GetPrimAtPath(SdfPath("/World/Unsaved"))), "failed save keeps live edits");
    QFile original(target + "/original");
    require(original.open(QIODevice::ReadOnly) && original.readAll() == "keep me", "original preserved");
    const QString prefix = "Recovery file: ";
    const qsizetype index = error.indexOf(prefix);
    require(index >= 0, "recovery location reported");
    require(bool(diskStage(error.mid(index + prefix.size()))->GetPrimAtPath(SdfPath("/World"))),
            "complete recovery layer retained");
}

void
sublayerSave()
{
    QTemporaryDir dir;
    require(dir.isValid(), "temporary directory");
    const QString childPath = dir.filePath("child.usda");
    const auto child = UsdStage::CreateNew(childPath.toStdString());
    child->DefinePrim(SdfPath("/Child"));
    require(child->GetRootLayer()->Save(), "save child fixture");
    Session session;
    session.setPreserveState(false);
    require(session.newStage(), "new stage");
    session.stage()->GetRootLayer()->SetSubLayerPaths({ childPath.toStdString() });
    require(session.setEditLayer(child->GetRootLayer()), "select sublayer");
    session.stage()->DefinePrim(SdfPath("/Child/Edited"));
    require(session.saveToFile(dir.filePath("scene.usda")), "save root and child");
    require(!child->GetRootLayer()->IsDirty(), "saved sublayer is clean");
    require(bool(diskStage(childPath)->GetPrimAtPath(SdfPath("/Child/Edited"))), "sublayer persisted");

    session.stage()->DefinePrim(SdfPath("/Child/Unsaved"));
    child->GetRootLayer()->SetPermissionToSave(false);
    require(!session.saveToFile(dir.filePath("scene.usda")), "unsavable sublayer fails whole save");
    child->GetRootLayer()->SetPermissionToSave(true);
    require(!diskStage(childPath)->GetPrimAtPath(SdfPath("/Child/Unsaved")), "failed save did not claim persistence");
}

void
saveAs()
{
    QTemporaryDir dir;
    require(dir.isValid(), "temporary directory");
    require(QDir().mkpath(dir.filePath("old")) && QDir().mkpath(dir.filePath("new")), "fixture directories");
    const QString assetPath = dir.filePath("old/asset.usda");
    const auto asset = UsdStage::CreateNew(assetPath.toStdString());
    asset->DefinePrim(SdfPath("/Asset"));
    asset->DefinePrim(SdfPath("/Asset/Geometry"));
    asset->SetDefaultPrim(asset->GetPrimAtPath(SdfPath("/Asset")));
    require(asset->GetRootLayer()->Save(), "save asset fixture");
    const QString oldPath = dir.filePath("old/scene.usda");
    {
        const auto stage = UsdStage::CreateNew(oldPath.toStdString());
        stage->GetRootLayer()->SetSubLayerPaths({ "asset.usda" });
        const auto prim = stage->DefinePrim(SdfPath("/Referenced"));
        require(prim.GetReferences().AddReference("asset.usda"), "author reference");
        require(prim.CreateAttribute(TfToken("texture"), SdfValueTypeNames->Asset)
                    .Set(SdfAssetPath("textures/tile.<UDIM>.exr")),
                "author unresolved texture pattern");
        require(stage->GetRootLayer()->Save(), "save source fixture");
    }
    Session session;
    session.setPreserveState(false);
    require(session.loadFromFile(oldPath), "load source");
    const QString newPath = dir.filePath("new/scene.usda");
    require(session.saveToFile(newPath), "save in another directory");
    require(bool(session.stage()->GetPrimAtPath(SdfPath("/Referenced/Geometry"))), "live reference survives Save As");
    const auto reopened = diskStage(newPath);
    require(bool(reopened->GetPrimAtPath(SdfPath("/Referenced/Geometry"))), "saved reference survives Save As");
    require(bool(reopened->GetPrimAtPath(SdfPath("/Asset/Geometry"))), "sublayer survives Save As");
    SdfAssetPath texture;
    require(reopened->GetPrimAtPath(SdfPath("/Referenced")).GetAttribute(TfToken("texture")).Get(&texture),
            "texture authored");
    require(QString::fromStdString(texture.GetAssetPath()) == dir.filePath("old/textures/tile.<UDIM>.exr"),
            "unresolved texture keeps original anchor");
}

void
saveAsPayloadAnchor()
{
    QTemporaryDir dir;
    require(dir.isValid(), "temporary directory");
    require(QDir().mkpath(dir.filePath("old")) && QDir().mkpath(dir.filePath("new")), "fixture directories");

    const QString assetPath = dir.filePath("old/payload.usda");
    const auto asset = UsdStage::CreateNew(assetPath.toStdString());
    asset->DefinePrim(SdfPath("/Asset"));
    asset->DefinePrim(SdfPath("/Asset/Geometry"));
    asset->SetDefaultPrim(asset->GetPrimAtPath(SdfPath("/Asset")));
    require(asset->GetRootLayer()->Save(), "save payload fixture");

    const QString oldPath = dir.filePath("old/scene.usda");
    {
        const auto stage = UsdStage::CreateNew(oldPath.toStdString());
        const auto prim = stage->DefinePrim(SdfPath("/Payloaded"));
        require(prim.GetPayloads().AddPayload("payload.usda"), "author relative payload");
        require(stage->GetRootLayer()->Save(), "save payload source fixture");
    }

    Session session;
    session.setPreserveState(false);
    require(session.loadFromFile(oldPath), "load payload source");
    require(bool(session.stage()->GetPrimAtPath(SdfPath("/Payloaded/Geometry"))), "source payload resolves");

    const QString newPath = dir.filePath("new/scene.usda");
    require(session.saveToFile(newPath), "save payload scene in another directory");
    require(bool(session.stage()->GetPrimAtPath(SdfPath("/Payloaded/Geometry"))), "live payload survives Save As");
    require(bool(diskStage(newPath)->GetPrimAtPath(SdfPath("/Payloaded/Geometry"))), "saved payload survives Save As");
}

void
corruptState()
{
    QTemporaryDir dir;
    require(dir.isValid(), "temporary directory");
    const QString path = dir.filePath("scene.usda");
    {
        const auto stage = UsdStage::CreateNew(path.toStdString());
        stage->DefinePrim(SdfPath("/Keep"));
        require(stage->GetRootLayer()->Save(), "save fixture");
    }
    writeFile(path + ".session", "{ broken JSON");
    Session session;
    require(session.loadFromFile(path), "corrupt optional state must not reject stage");
    require(bool(session.stage()->GetPrimAtPath(SdfPath("/Keep"))), "valid stage retained");
}

void
payloadState(bool legacy)
{
    QTemporaryDir dir;
    require(dir.isValid(), "temporary directory");
    const QString assetPath = dir.filePath("payload.usda");
    const auto asset = UsdStage::CreateNew(assetPath.toStdString());
    asset->DefinePrim(SdfPath("/Asset"));
    asset->DefinePrim(SdfPath("/Asset/Geometry"));
    asset->SetDefaultPrim(asset->GetPrimAtPath(SdfPath("/Asset")));
    require(asset->GetRootLayer()->Save(), "save payload fixture");
    const QString path = dir.filePath("scene.usda");
    Session session;
    require(session.newStage(), "new stage");
    for (const char* name : { "/World/Loaded", "/World/Unloaded" })
        require(session.stage()->DefinePrim(SdfPath(name)).GetPayloads().AddPayload(assetPath.toStdString()),
                "author payload");
    session.stage()->Unload(SdfPath("/World/Unloaded"));
    const auto expected = session.stage()->GetLoadRules();
    require(session.saveToFile(path), "save payload state");
    if (legacy) {
        QJsonObject object;
        object["version"] = 3;
        object["loadedPayloads"] = QJsonArray { QString("/World/Loaded") };
        writeFile(path + ".session", QJsonDocument(object).toJson());
    }
    require(session.loadFromFile(path, Session::All), "reopen with load-all policy");
    require(session.stage()->GetPrimAtPath(SdfPath("/World/Loaded")).IsLoaded(), "loaded payload restored");
    require(!session.stage()->GetPrimAtPath(SdfPath("/World/Unloaded")).IsLoaded(), "unloaded payload restored");
    if (!legacy)
        require(session.stage()->GetLoadRules() == expected, "exact load rules restored");
    if (legacy) {
        QJsonObject object;
        object["loadedPayloads"] = QJsonArray();
        writeFile(path + ".session", QJsonDocument(object).toJson());
        require(session.loadState(path + ".session"), "read empty legacy list");
        require(!session.stage()->GetPrimAtPath(SdfPath("/World/Loaded")).IsLoaded(), "empty list unloads everything");
    }
}

void
reparentDependencies()
{
    const auto stage = UsdStage::CreateInMemory();
    stage->DefinePrim(SdfPath("/A"));
    stage->DefinePrim(SdfPath("/B"));
    const auto material = stage->DefinePrim(SdfPath("/A/Material"));
    material.CreateAttribute(TfToken("outputs:surface"), SdfValueTypeNames->Token);
    const auto mesh = stage->DefinePrim(SdfPath("/Mesh"));
    require(mesh.CreateRelationship(TfToken("material:binding")).SetTargets({ SdfPath("/A/Material") }),
            "binding fixture");
    require(mesh.CreateAttribute(TfToken("input"), SdfValueTypeNames->Token)
                .SetConnections({ SdfPath("/A/Material.outputs:surface") }),
            "connection fixture");
    stageviz::edit::NamespaceEditor editor(stage, stage->GetEditTarget());
    QString error;
    require(editor.reparentPrim(SdfPath("/A/Material"), SdfPath("/B/Material"), error), "move material");
    SdfPathVector paths;
    require(mesh.GetRelationship(TfToken("material:binding")).GetTargets(&paths)
                && paths == SdfPathVector { SdfPath("/B/Material") },
            "binding updated");
    require(mesh.GetAttribute(TfToken("input")).GetConnections(&paths)
                && paths == SdfPathVector { SdfPath("/B/Material.outputs:surface") },
            "connection updated");
    require(editor.reparentPrim(SdfPath("/B/Material"), SdfPath("/A/Material"), error), "reverse move");
    require(mesh.GetRelationship(TfToken("material:binding")).GetTargets(&paths)
                && paths == SdfPathVector { SdfPath("/A/Material") },
            "binding restored by undo path");
}

void
renameDependencies()
{
    const auto stage = UsdStage::CreateInMemory();
    stage->DefinePrim(SdfPath("/A"));
    const auto material = stage->DefinePrim(SdfPath("/A/Material"));
    material.CreateAttribute(TfToken("outputs:surface"), SdfValueTypeNames->Token);
    const auto mesh = stage->DefinePrim(SdfPath("/Mesh"));
    require(mesh.CreateRelationship(TfToken("material:binding")).SetTargets({ SdfPath("/A/Material") }),
            "binding fixture");
    require(mesh.CreateAttribute(TfToken("input"), SdfValueTypeNames->Token)
                .SetConnections({ SdfPath("/A/Material.outputs:surface") }),
            "connection fixture");

    stageviz::edit::NamespaceEditor editor(stage, stage->GetEditTarget());
    QString error;
    require(editor.renamePrim(SdfPath("/A/Material"), SdfPath("/A/Renamed"), error), "rename material");

    SdfPathVector paths;
    require(mesh.GetRelationship(TfToken("material:binding")).GetTargets(&paths)
                && paths == SdfPathVector { SdfPath("/A/Renamed") },
            "binding updated by rename");
    require(mesh.GetAttribute(TfToken("input")).GetConnections(&paths)
                && paths == SdfPathVector { SdfPath("/A/Renamed.outputs:surface") },
            "connection updated by rename");
}

void
reparentCollision()
{
    const auto stage = UsdStage::CreateInMemory();
    for (const char* path : { "/A", "/B", "/A/Part", "/B/Part" })
        stage->DefinePrim(SdfPath(path));

    UsdStageLoadRules rules = UsdStageLoadRules::LoadAll();
    rules.AddRule(SdfPath("/A/Part"), UsdStageLoadRules::NoneRule);
    stage->SetLoadRules(rules);
    const UsdStageLoadRules beforeRules = stage->GetLoadRules();

    std::string before;
    stage->GetRootLayer()->ExportToString(&before);

    stageviz::edit::NamespaceEditor editor(stage, stage->GetEditTarget());
    QString error;
    require(!editor.reparentPrim(SdfPath("/A/Part"), SdfPath("/B/Part"), error), "move onto existing prim fails");

    std::string after;
    stage->GetRootLayer()->ExportToString(&after);
    require(before == after, "collision failure leaves layer unchanged");
    require(stage->GetLoadRules() == beforeRules, "collision failure leaves load rules unchanged");
}

void
reparentIntoDescendant()
{
    const auto stage = UsdStage::CreateInMemory();
    stage->DefinePrim(SdfPath("/A"));
    stage->DefinePrim(SdfPath("/A/Child"));

    std::string before;
    stage->GetRootLayer()->ExportToString(&before);

    stageviz::edit::NamespaceEditor editor(stage, stage->GetEditTarget());
    QString error;
    require(!editor.reparentPrim(SdfPath("/A"), SdfPath("/A/Child/A"), error), "move into descendant fails");

    std::string after;
    stage->GetRootLayer()->ExportToString(&after);
    require(before == after, "descendant failure leaves layer unchanged");
}

void
reparentBatch()
{
    const auto stage = UsdStage::CreateInMemory();
    for (const char* path : { "/A", "/B", "/A/First", "/A/Second" })
        stage->DefinePrim(SdfPath(path));
    stageviz::edit::NamespaceEditor editor(stage, stage->GetEditTarget());
    QString error;
    require(editor.reparentPrims({ { SdfPath("/A/First"), SdfPath("/B/First") },
                                   { SdfPath("/A/Second"), SdfPath("/B/Second") } },
                                 error),
            "move both prims");
    require(bool(stage->GetPrimAtPath(SdfPath("/B/First"))) && bool(stage->GetPrimAtPath(SdfPath("/B/Second"))),
            "both moves applied");
    std::string before;
    stage->GetRootLayer()->ExportToString(&before);
    require(!editor.reparentPrims({ { SdfPath("/B/First"), SdfPath("/A/First") },
                                    { SdfPath("/B/Second"), SdfPath("/B/First/Second") } },
                                  error),
            "second move into a moved-away parent fails");
    std::string after;
    stage->GetRootLayer()->ExportToString(&after);
    require(before == after, "failed multi-move restores all layers");
}
}  // namespace

namespace {
void
mergeAssetPaths()
{
    QTemporaryDir dir;
    require(dir.isValid(), "temporary directory");
    require(QDir().mkpath(dir.filePath("source")), "source directory");
    require(QDir().mkpath(dir.filePath("destination")), "destination directory");
    const QString sourceFile = dir.filePath("source/scene.usda");
    const auto source = UsdStage::CreateNew(sourceFile.toStdString());
    const UsdPrim part = source->DefinePrim(SdfPath("/Part"));
    part.CreateAttribute(TfToken("texture"), SdfValueTypeNames->Asset).Set(SdfAssetPath("textures/paint.<UDIM>.exr"));
    require(source->GetRootLayer()->Save(), "save merge source");
    Session session;
    session.setPreserveState(false);
    require(session.newStage(), "new stage");
    require(session.saveToFile(dir.filePath("destination/scene.usda")), "save destination");
    stageviz::Command merge = stageviz::mergeStage(sourceFile);
    executeCommand(merge, session);
    SdfAssetPath texture;
    require(session.stage()->GetPrimAtPath(SdfPath("/Part")).GetAttribute(TfToken("texture")).Get(&texture),
            "merged asset attribute");
    require(texture.GetAssetPath() == dir.filePath("source/textures/paint.<UDIM>.exr").toStdString(),
            "merged texture retains source anchor");
}

void
mergeCompositionAssetPaths()
{
    QTemporaryDir dir;
    require(dir.isValid(), "temporary directory");
    require(QDir().mkpath(dir.filePath("source")), "source directory");
    require(QDir().mkpath(dir.filePath("destination")), "destination directory");

    const QString assetFile = dir.filePath("source/asset.usda");
    const auto asset = UsdStage::CreateNew(assetFile.toStdString());
    asset->DefinePrim(SdfPath("/Asset"));
    asset->DefinePrim(SdfPath("/Asset/Geometry"));
    asset->SetDefaultPrim(asset->GetPrimAtPath(SdfPath("/Asset")));
    require(asset->GetRootLayer()->Save(), "save composition asset fixture");

    const QString sourceFile = dir.filePath("source/scene.usda");
    const auto source = UsdStage::CreateNew(sourceFile.toStdString());
    require(source->DefinePrim(SdfPath("/Referenced")).GetReferences().AddReference("asset.usda"),
            "author relative merge reference");
    require(source->DefinePrim(SdfPath("/Payloaded")).GetPayloads().AddPayload("asset.usda"),
            "author relative merge payload");
    require(source->GetRootLayer()->Save(), "save merge composition source");

    Session session;
    session.setPreserveState(false);
    require(session.newStage(), "new stage");
    require(session.saveToFile(dir.filePath("destination/scene.usda")), "save merge destination");
    stageviz::Command merge = stageviz::mergeStage(sourceFile);
    executeCommand(merge, session);
    require(bool(session.stage()->GetPrimAtPath(SdfPath("/Referenced/Geometry"))),
            "merged reference retains source anchor");
    require(bool(session.stage()->GetPrimAtPath(SdfPath("/Payloaded/Geometry"))),
            "merged payload retains source anchor");
}

void
namespaceLoadRules()
{
    const auto stage = UsdStage::CreateInMemory();
    stage->DefinePrim(SdfPath("/A"));
    stage->DefinePrim(SdfPath("/B"));
    stage->DefinePrim(SdfPath("/A/Part"));
    stage->DefinePrim(SdfPath("/A/Part/Detail"));
    UsdStageLoadRules rules = UsdStageLoadRules::LoadAll();
    rules.AddRule(SdfPath("/A/Part"), UsdStageLoadRules::NoneRule);
    rules.AddRule(SdfPath("/A/Part/Detail"), UsdStageLoadRules::OnlyRule);
    rules.AddRule(SdfPath("/Unrelated"), UsdStageLoadRules::NoneRule);
    stage->SetLoadRules(rules);
    stageviz::edit::NamespaceEditor editor(stage, stage->GetEditTarget());
    QString error;
    require(editor.reparentPrim(SdfPath("/A/Part"), SdfPath("/B/Part"), error), "move load rules");
    const auto& moved = stage->GetLoadRules();
    require(moved.GetEffectiveRuleForPath(SdfPath("/B/Part/Other")) == UsdStageLoadRules::NoneRule,
            "unloaded descendant policy follows moved root");
    require(moved.GetEffectiveRuleForPath(SdfPath("/B/Part/Detail")) == UsdStageLoadRules::OnlyRule,
            "only rule follows moved descendant");
    require(moved.GetEffectiveRuleForPath(SdfPath("/Unrelated")) == UsdStageLoadRules::NoneRule,
            "unrelated exclusion preserved");
    require(editor.renamePrim(SdfPath("/B/Part"), SdfPath("/B/Renamed"), error), "rename load rules");
    require(stage->GetLoadRules().GetEffectiveRuleForPath(SdfPath("/B/Renamed/Other")) == UsdStageLoadRules::NoneRule,
            "rename preserves unloaded policy");
}

void
namespaceLoadRulesBatch()
{
    const auto stage = UsdStage::CreateInMemory();
    for (const char* path : { "/A", "/B", "/C", "/A/First", "/A/First/Detail", "/A/Second", "/A/Second/Detail" })
        stage->DefinePrim(SdfPath(path));

    UsdStageLoadRules rules = UsdStageLoadRules::LoadAll();
    rules.AddRule(SdfPath("/A/First"), UsdStageLoadRules::NoneRule);
    rules.AddRule(SdfPath("/A/First/Detail"), UsdStageLoadRules::OnlyRule);
    rules.AddRule(SdfPath("/A/Second"), UsdStageLoadRules::OnlyRule);
    rules.AddRule(SdfPath("/Unrelated"), UsdStageLoadRules::NoneRule);
    stage->SetLoadRules(rules);

    stageviz::edit::NamespaceEditor editor(stage, stage->GetEditTarget());
    QString error;
    require(editor.reparentPrims({ { SdfPath("/A/First"), SdfPath("/B/First") },
                                   { SdfPath("/A/Second"), SdfPath("/C/Second") } },
                                 error),
            "batch move load rules");

    const UsdStageLoadRules moved = stage->GetLoadRules();
    require(moved.GetEffectiveRuleForPath(SdfPath("/B/First/Other")) == UsdStageLoadRules::NoneRule,
            "batch move preserves none rule");
    require(moved.GetEffectiveRuleForPath(SdfPath("/B/First/Detail")) == UsdStageLoadRules::OnlyRule,
            "batch move preserves descendant only rule");
    require(moved.GetEffectiveRuleForPath(SdfPath("/C/Second")) == UsdStageLoadRules::OnlyRule,
            "batch move preserves second source rule");
    require(moved.GetEffectiveRuleForPath(SdfPath("/Unrelated")) == UsdStageLoadRules::NoneRule,
            "batch move preserves unrelated rule");
}

void
namespaceLoadRulesBatchFailure()
{
    const auto stage = UsdStage::CreateInMemory();
    for (const char* path : { "/A", "/B", "/A/First", "/A/First/Detail", "/A/Second" })
        stage->DefinePrim(SdfPath(path));

    UsdStageLoadRules rules = UsdStageLoadRules::LoadAll();
    rules.AddRule(SdfPath("/A/First"), UsdStageLoadRules::NoneRule);
    rules.AddRule(SdfPath("/A/First/Detail"), UsdStageLoadRules::OnlyRule);
    rules.AddRule(SdfPath("/A/Second"), UsdStageLoadRules::NoneRule);
    stage->SetLoadRules(rules);

    const UsdStageLoadRules beforeRules = stage->GetLoadRules();
    std::string beforeLayer;
    stage->GetRootLayer()->ExportToString(&beforeLayer);

    stageviz::edit::NamespaceEditor editor(stage, stage->GetEditTarget());
    QString error;
    require(!editor.reparentPrims({ { SdfPath("/A/First"), SdfPath("/B/First") },
                                    { SdfPath("/A/Second"), SdfPath("/B/First/Second") } },
                                  error),
            "invalid batch move fails");

    std::string afterLayer;
    stage->GetRootLayer()->ExportToString(&afterLayer);
    require(beforeLayer == afterLayer, "failed batch move restores layer");
    require(stage->GetLoadRules() == beforeRules, "failed batch move restores load rules");
}

void
namespaceLoadRulesRename()
{
    const auto stage = UsdStage::CreateInMemory();
    stage->DefinePrim(SdfPath("/World"));
    stage->DefinePrim(SdfPath("/World/Part"));
    stage->DefinePrim(SdfPath("/World/Part/Detail"));

    UsdStageLoadRules rules = UsdStageLoadRules::LoadAll();
    rules.AddRule(SdfPath("/World/Part"), UsdStageLoadRules::NoneRule);
    rules.AddRule(SdfPath("/World/Part/Detail"), UsdStageLoadRules::OnlyRule);
    stage->SetLoadRules(rules);

    stageviz::edit::NamespaceEditor editor(stage, stage->GetEditTarget());
    QString error;
    require(editor.renamePrim(SdfPath("/World/Part"), SdfPath("/World/Renamed"), error), "rename load rules");

    const UsdStageLoadRules renamed = stage->GetLoadRules();
    require(renamed.GetEffectiveRuleForPath(SdfPath("/World/Renamed/Other")) == UsdStageLoadRules::NoneRule,
            "rename preserves none rule");
    require(renamed.GetEffectiveRuleForPath(SdfPath("/World/Renamed/Detail")) == UsdStageLoadRules::OnlyRule,
            "rename preserves descendant only rule");
    require(renamed.GetEffectiveRuleForPath(SdfPath("/World/Part")) == UsdStageLoadRules::AllRule,
            "rename removes old-path rule");
}

void
exportSessionOpinions()
{
    QTemporaryDir dir;
    require(dir.isValid(), "temporary directory");
    Session session;
    require(session.newStage(), "new stage");
    const auto stage = session.stage();
    stage->DefinePrim(SdfPath("/World/Part"));
    stage->SetEditTarget(UsdEditTarget(stage->GetSessionLayer()));
    stage->GetPrimAtPath(SdfPath("/World/Part"))
        .CreateAttribute(TfToken("marker"), SdfValueTypeNames->String)
        .Set(std::string("session opinion"));
    const QString file = dir.filePath("selection.usda");
    require(session.flattenPathsToFile({ SdfPath("/World/Part") }, file), "export selection");
    std::string marker;
    require(diskStage(file)->GetPrimAtPath(SdfPath("/World/Part")).GetAttribute(TfToken("marker")).Get(&marker)
                && marker == "session opinion",
            "selection export preserves session opinions");
}
void
exportMultipleSessionOpinions()
{
    QTemporaryDir dir;
    require(dir.isValid(), "temporary directory");
    Session session;
    require(session.newStage(), "new stage");
    const auto stage = session.stage();
    stage->DefinePrim(SdfPath("/World/First"));
    stage->DefinePrim(SdfPath("/World/Second"));
    stage->DefinePrim(SdfPath("/World/Unrelated"));

    stage->SetEditTarget(UsdEditTarget(stage->GetSessionLayer()));
    stage->GetPrimAtPath(SdfPath("/World/First"))
        .CreateAttribute(TfToken("marker"), SdfValueTypeNames->String)
        .Set(std::string("first"));
    stage->GetPrimAtPath(SdfPath("/World/Second"))
        .CreateAttribute(TfToken("marker"), SdfValueTypeNames->String)
        .Set(std::string("second"));
    stage->GetPrimAtPath(SdfPath("/World/Unrelated"))
        .CreateAttribute(TfToken("marker"), SdfValueTypeNames->String)
        .Set(std::string("unrelated"));

    const QString file = dir.filePath("selection.usda");
    require(session.flattenPathsToFile({ SdfPath("/World/First"), SdfPath("/World/Second") }, file),
            "export multiple selections");

    const auto reopened = diskStage(file);
    std::string first;
    std::string second;
    require(reopened->GetPrimAtPath(SdfPath("/World/First")).GetAttribute(TfToken("marker")).Get(&first)
                && first == "first",
            "first session opinion exported");
    require(reopened->GetPrimAtPath(SdfPath("/World/Second")).GetAttribute(TfToken("marker")).Get(&second)
                && second == "second",
            "second session opinion exported");
    require(!reopened->GetPrimAtPath(SdfPath("/World/Unrelated")), "unselected session opinion excluded");
}

void
selectionListApi()
{
    SelectionList selection;
    int changes = 0;
    QObject::connect(&selection, &SelectionList::selectionChanged, &selection,
                     [&](const QList<SdfPath>&) { ++changes; });

    require(selection.isValid(), "selection list reports valid");
    require(selection.paths().isEmpty(), "new selection has no paths");
    require(selection.isEmpty(), "new selection reports empty");

    selection.addPaths({ SdfPath("/A"), SdfPath("/B"), SdfPath("/A") });
    require(selection.paths() == QList<SdfPath>({ SdfPath("/A"), SdfPath("/B") }),
            "addPaths deduplicates while preserving order");
    require(selection.isSelected(SdfPath("/A")), "added path selected");
    require(!selection.isEmpty(), "non-empty selection reports non-empty");

    selection.removePaths({ SdfPath("/A") });
    require(selection.paths() == QList<SdfPath>({ SdfPath("/B") }), "removePaths removes path");
    selection.togglePaths({ SdfPath("/B"), SdfPath("/C") });
    require(selection.paths() == QList<SdfPath>({ SdfPath("/C") }), "togglePaths toggles both states");
    selection.updatePaths({ SdfPath("/D"), SdfPath("/E") });
    require(selection.paths() == QList<SdfPath>({ SdfPath("/D"), SdfPath("/E") }), "updatePaths replaces selection");
    selection.clear();
    require(selection.paths().isEmpty() && selection.isEmpty(), "clear empties selection");
    require(changes >= 5, "selection changes emit notifications");
}

void
viewCameraApi()
{
    ViewCamera camera(16.0 / 9.0, 50.0, ViewCamera::Horizontal);
    int changes = 0;
    QObject::connect(&camera, &ViewCamera::cameraChanged, &camera, [&](const GfCamera&) { ++changes; });

    camera.setAspectRatio(2.0);
    require(closeEnough(camera.aspectRatio(), 2.0), "camera aspect ratio roundtrip");
    camera.setAspectRatioLocked(true);
    require(camera.aspectRatioLocked(), "camera aspect lock roundtrip");
    camera.setLetterboxEnabled(true);
    require(camera.letterboxEnabled(), "camera letterbox roundtrip");
    camera.setLetterboxOpacity(2.0);
    require(closeEnough(camera.letterboxOpacity(), 1.0), "letterbox opacity clamps high");
    camera.setLetterboxOpacity(-1.0);
    require(closeEnough(camera.letterboxOpacity(), 0.0), "letterbox opacity clamps low");

    camera.setProjectionMode(ViewCamera::Physical);
    require(camera.projectionMode() == ViewCamera::Physical, "camera projection mode roundtrip");
    camera.setFov(42.0);
    require(closeEnough(camera.fov(), 42.0), "camera fov roundtrip");
    camera.setFovDirection(ViewCamera::Vertical);
    require(camera.fovDirection() == ViewCamera::Vertical, "camera fov direction roundtrip");
    camera.setFocalLength(85.0);
    camera.setSensorWidth(36.0);
    camera.setSensorHeight(24.0);
    require(closeEnough(camera.focalLength(), 85.0), "camera focal length roundtrip");
    require(closeEnough(camera.sensorWidth(), 36.0) && closeEnough(camera.sensorHeight(), 24.0),
            "camera sensor roundtrip");

    const GfVec3d focus(1.0, 2.0, 3.0);
    camera.setFocusPoint(focus);
    require(camera.focusPoint() == focus, "camera focus point roundtrip");
    camera.setFit(1.25);
    require(closeEnough(camera.fit(), 1.25), "camera fit roundtrip");
    camera.setCameraUp(ViewCamera::Y);
    require(camera.cameraUp() == ViewCamera::Y, "camera up roundtrip");
    camera.setAxisYaw(12.0);
    camera.setAxisPitch(-7.0);
    camera.setAxisRoll(3.0);
    require(closeEnough(camera.axisYaw(), 12.0) && closeEnough(camera.axisPitch(), -7.0)
                && closeEnough(camera.axisRoll(), 3.0),
            "camera axis roundtrip");
    camera.setCameraMode(ViewCamera::Tumble);
    require(camera.cameraMode() == ViewCamera::Tumble, "camera mode roundtrip");
    camera.setNearClipping(0.25);
    camera.setFarClipping(25000.0);
    require(closeEnough(camera.nearClipping(), 0.25) && closeEnough(camera.farClipping(), 25000.0),
            "camera clipping roundtrip");
    camera.setCameraDistance(25.0);
    require(closeEnough(camera.cameraDistance(), 25.0), "camera distance roundtrip");

    const GfBBox3d bbox(GfRange3d(GfVec3d(-2.0, -1.0, -3.0), GfVec3d(2.0, 1.0, 3.0)));
    camera.setBoundingBox(bbox);
    require(camera.boundingBox().GetRange() == bbox.GetRange(), "camera scene bounds roundtrip");
    camera.frame(bbox);
    require(!camera.isIdentity(), "frame changes identity view");
    require(camera.mapToFrustumHeight(100) > 0.0, "frustum mapping produces positive height");
    camera.tumble(3.0, -2.0);
    camera.truck(0.1, 0.2);
    camera.distance(0.9);
    const GfCamera usdCamera = camera.camera();
    require(usdCamera.GetClippingRange().GetMin() > 0.0, "camera produces valid USD camera");
    camera.reset();
    require(changes > 0, "camera mutations emit change notifications");
}

void
viewStateApi()
{
    ViewState state;
    require(state.camera() != nullptr, "view state exposes camera");

    int backgroundChanges = 0;
    QObject::connect(&state, &ViewState::backgroundColorChanged, &state, [&](const QColor&) { ++backgroundChanges; });

    const QColor background(12, 34, 56, 255);
    const QColor grid(70, 80, 90, 255);
    state.setBackgroundColor(background);
    state.setGridColor(grid);
    state.setGridEnabled(false);
    require(state.backgroundColor() == background && state.gridColor() == grid && !state.gridEnabled(),
            "view presentation roundtrip");

    state.setMaterialMode(ViewState::Clay);
    require(state.materialMode() == ViewState::Clay, "material mode roundtrip");
    state.setOverrideMaterial(SdfPath("/Stageviz/Materials/Override"));
    require(state.overrideMaterial() == SdfPath("/Stageviz/Materials/Override")
                && state.materialMode() == ViewState::Override,
            "override material activates override mode");
    state.setOverrideMaterial(SdfPath());
    require(state.overrideMaterial().IsEmpty() && state.materialMode() == ViewState::All,
            "clearing override material restores all materials");

    state.setDefaultCameraLightEnabled(false);
    state.setDefaultDomeLightEnabled(true);
    state.setDomeLightTexture("  environment.exr  ");
    state.setDomeLightCameraVisibility(true);
    state.setSceneLightsEnabled(false);
    state.setSceneMaterialsEnabled(false);
    state.setDoubleSidedMode(ViewState::DoubleSided);
    require(!state.defaultCameraLightEnabled() && state.defaultDomeLightEnabled(), "view light toggles roundtrip");
    require(state.domeLightTexture() == "environment.exr" && state.domeLightCameraVisibility(),
            "dome settings roundtrip");
    require(!state.sceneLightsEnabled() && !state.sceneMaterialsEnabled(), "scene rendering toggles roundtrip");
    require(state.doubleSidedMode() == ViewState::DoubleSided, "double-sided mode roundtrip");

    state.setRenderMode(ViewState::Wireframe);
    state.setComplexityLevel(ViewState::VeryHigh);
    state.setRendererAov("depth");
    state.setSceneStatsEnabled(false);
    state.setPerformanceStatsEnabled(true);
    state.setCameraAxisEnabled(false);
    require(state.renderMode() == ViewState::Wireframe && state.complexityLevel() == ViewState::VeryHigh,
            "render state roundtrip");
    require(state.rendererAov() == "depth", "renderer AOV roundtrip");
    require(!state.sceneStatsEnabled() && state.performanceStatsEnabled() && !state.cameraAxisEnabled(),
            "HUD state roundtrip");
    require(backgroundChanges == 1, "view state suppresses duplicate background notifications");
    state.setBackgroundColor(background);
    require(backgroundChanges == 1, "identical view state value does not re-emit");
}

void
sessionCoreApi()
{
    Session session;
    require(session.stageLock() != nullptr && session.auxiliaryLock() != nullptr, "session exposes locks");
    require(session.commandStack() != nullptr && session.selectionList() != nullptr && session.viewState() != nullptr,
            "session exposes shared subsystems");

    int stageChanges = 0;
    int redraws = 0;
    int statusChanges = 0;
    int maskChanges = 0;
    int stageUpChanges = 0;
    int primChanges = 0;
    stageviz::NoticeBatch lastPrimBatch;
    QObject::connect(&session, &Session::primsChanged, &session, [&](const stageviz::NoticeBatch& batch) {
        ++primChanges;
        lastPrimBatch = batch;
    });
    QObject::connect(&session, &Session::stageChanged, &session,
                     [&](UsdStageRefPtr, Session::LoadPolicy, Session::StageStatus) { ++stageChanges; });
    QObject::connect(&session, &Session::redrawRequested, &session, [&]() { ++redraws; });
    QObject::connect(&session, &Session::notifyStatusChanged, &session,
                     [&](Session::Notify::Status, const QString&, const QString&) { ++statusChanges; });
    QObject::connect(&session, &Session::maskChanged, &session, [&](const QList<SdfPath>&) { ++maskChanges; });
    QObject::connect(&session, &Session::stageUpChanged, &session, [&](Session::StageUp) { ++stageUpChanges; });

    require(session.newStage(Session::None), "session newStage none");
    require(session.isLoaded() && session.stage() && session.stageUnsafe(), "session stage accessors");
    require(session.loadPolicy() == Session::None, "session load policy retained");
    require(session.auxiliary() && session.auxiliaryUnsafe(), "session auxiliary stage accessors");
    require(session.stage() != session.auxiliary(), "document and auxiliary stages are separate");
    require(session.filename().isEmpty(), "new anonymous stage has no filename");

    session.stage()->DefinePrim(SdfPath("/World"), TfToken("Xform"));
    session.setMask({ SdfPath("/World") });
    require(session.mask() == QList<SdfPath>({ SdfPath("/World") }), "session mask roundtrip");
    session.setStageUp(Session::Y);
    require(session.stageUp() == Session::Y && session.viewState()->camera()->cameraUp() == ViewCamera::Y,
            "session stage-up synchronizes camera");
    session.setStageUp(Session::Z);
    require(session.stageUp() == Session::Z && session.viewState()->camera()->cameraUp() == ViewCamera::Z,
            "session stage-up restores z");

    session.setPrimsUpdate(Session::Deferred);
    require(session.primsUpdate() == Session::Deferred, "session deferred prim mode roundtrip");
    session.stage()->DefinePrim(SdfPath("/World/Deferred"));
    session.setPrimsUpdate(Session::Immediate);
    require(session.primsUpdate() == Session::Immediate, "session immediate prim mode roundtrip");

    const UsdGeomXform deferredXform = UsdGeomXform::Define(session.stage(), SdfPath("/World/DeferredCompact"));
    UsdGeomXformOp deferredOp = deferredXform.AddTransformOp();
    deferredOp.Set(GfMatrix4d(1.0));
    drainWorkers();

    const int primChangesBeforeCompact = primChanges;
    session.setPrimsUpdate(Session::Deferred);
    for (int i = 0; i < 32; ++i) {
        GfMatrix4d matrix(1.0);
        matrix.SetTranslate(GfVec3d(double(i + 1), 0.0, 0.0));
        deferredOp.Set(matrix);
    }
    require(primChanges == primChangesBeforeCompact, "deferred prim edits do not emit immediately");
    session.setPrimsUpdate(Session::Immediate);
    require(primChanges == primChangesBeforeCompact + 1, "restoring immediate prim updates emits one signal");
    require(lastPrimBatch.entries.size() == 1, "deferred repeated info-only notices are compacted by path");
    require(lastPrimBatch.entries.first().changedInfoOnly, "compacted deferred transform notice remains info-only");

    session.notifyRedraw();
    session.notifyStatus(Session::Notify::Status::Warning, "warning", "details");
    require(redraws == 1 && statusChanges == 1, "session explicit notifications emitted");

    int progressBegin = 0;
    int progressEnd = 0;
    int progressUpdates = 0;
    QObject::connect(&session, &Session::progressBlockChanged, &session,
                     [&](const QString&, Session::ProgressMode mode) {
                         if (mode == Session::Running)
                             ++progressBegin;
                         else
                             ++progressEnd;
                     });
    QObject::connect(&session, &Session::progressNotifyChanged, &session,
                     [&](const Session::Notify&, size_t, size_t) { ++progressUpdates; });
    session.beginProgressBlock("test", 2);
    session.updateProgressNotify(Session::Notify("one"), 1);
    session.cancelProgressBlock();
    require(session.isProgressBlockCancelled(), "session progress cancellation roundtrip");
    session.endProgressBlock();
    require(progressBegin == 1 && progressEnd == 1 && progressUpdates == 1, "session progress signals emitted");
    require(stageChanges >= 1 && maskChanges >= 1 && stageUpChanges >= 2, "session state signals emitted");
}

void
sessionFileApi()
{
    QTemporaryDir dir;
    require(dir.isValid(), "temporary directory");

    Session session;
    session.setPreserveState(false);
    require(session.newStage(), "new stage");
    session.stage()->DefinePrim(SdfPath("/World"), TfToken("Xform"));
    session.stage()->DefinePrim(SdfPath("/World/Part"), TfToken("Xform"));

    const QString source = dir.filePath("source.usda");
    require(session.saveToFile(source), "save source");
    require(session.filename() == source, "save sets filename");

    const QString copy = dir.filePath("copy.usda");
    require(session.copyToFile(copy), "copy root layer");
    require(session.filename() == source, "copy does not retarget session filename");
    require(bool(diskStage(copy)->GetPrimAtPath(SdfPath("/World/Part"))), "copy contains authored prim");

    const QString flattened = dir.filePath("flattened.usda");
    require(session.flattenToFile(flattened), "flatten whole stage");
    require(bool(diskStage(flattened)->GetPrimAtPath(SdfPath("/World/Part"))), "flatten contains composed prim");
    require(!session.flattenPathsToFile({}, dir.filePath("empty.usda")), "empty selection export rejected");

    session.stage()->DefinePrim(SdfPath("/Transient"));
    require(session.reload(), "reload current stage");
    require(!session.stage()->GetPrimAtPath(SdfPath("/Transient")), "reload discards unsaved edit");
    require(bool(session.stage()->GetPrimAtPath(SdfPath("/World/Part"))), "reload restores saved content");

    require(session.close(), "close stage");
    require(!session.isLoaded() && !session.stage(), "close clears current stage");
    require(session.loadFromFile(source, Session::All), "load stage after close");
    require(session.isLoaded() && session.loadPolicy() == Session::All, "load restores stage and policy");
}

void
sessionEditLayerApi()
{
    Session session;
    require(session.newStage(), "new stage");
    const auto stage = session.stage();
    const SdfLayerRefPtr child = SdfLayer::CreateAnonymous("child.usda");
    stage->GetRootLayer()->SetSubLayerPaths({ child->GetIdentifier() });

    require(session.setEditLayer(child), "set local sublayer edit target");
    stage->DefinePrim(SdfPath("/ChildAuthored"));
    require(bool(child->GetPrimAtPath(SdfPath("/ChildAuthored"))), "authoring lands in selected edit layer");
    require(session.setEditLayer(stage->GetRootLayer()), "restore root edit target");

    const SdfLayerRefPtr foreign = SdfLayer::CreateAnonymous("foreign.usda");
    require(!session.setEditLayer(foreign), "foreign layer rejected as edit target");
}

void
sessionStateApi()
{
    QTemporaryDir dir;
    require(dir.isValid(), "temporary directory");
    const QString stagePath = dir.filePath("scene.usda");
    const QString statePath = dir.filePath("scene.usda.session");

    Session session;
    require(session.newStage(), "new stage");
    session.stage()->DefinePrim(SdfPath("/World"));
    require(session.saveToFile(stagePath), "save stage before state test");

    ViewState* state = session.viewState();
    state->setGridEnabled(false);
    state->setRenderMode(ViewState::Wireframe);
    state->setComplexityLevel(ViewState::High);
    state->setRendererAov("depth");
    state->camera()->setAspectRatio(2.39);
    state->camera()->setAspectRatioLocked(true);
    state->camera()->setFov(47.0);
    require(session.saveState(statePath), "save explicit session state");

    state->setGridEnabled(true);
    state->setRenderMode(ViewState::Shaded);
    state->setComplexityLevel(ViewState::Low);
    state->setRendererAov("color");
    state->camera()->setAspectRatio(1.0);
    state->camera()->setAspectRatioLocked(false);
    state->camera()->setFov(25.0);

    require(session.loadState(statePath), "load explicit session state");
    require(!state->gridEnabled() && state->renderMode() == ViewState::Wireframe
                && state->complexityLevel() == ViewState::High && state->rendererAov() == "depth",
            "view state restored");
    require(closeEnough(state->camera()->aspectRatio(), 2.39) && state->camera()->aspectRatioLocked()
                && closeEnough(state->camera()->fov(), 47.0),
            "camera state restored");
}

void
sessionLoadPolicyApi()
{
    QTemporaryDir dir;
    require(dir.isValid(), "temporary directory");

    const QString assetPath = makeAssetFixture(dir.filePath("payload.usda"));
    const QString scenePath = dir.filePath("scene.usda");
    {
        const UsdStageRefPtr stage = UsdStage::CreateNew(scenePath.toStdString());
        require(bool(stage), "create load-policy fixture stage");
        const UsdPrim payload = stage->DefinePrim(SdfPath("/Payload"), TfToken("Xform"));
        require(payload.GetPayloads().AddPayload(assetPath.toStdString()), "author load-policy payload");
        require(stage->GetRootLayer()->Save(), "save load-policy fixture");
    }

    Session session;
    session.setPreserveState(false);

    require(session.loadFromFile(scenePath, Session::None), "load stage with load-none policy");
    require(session.loadPolicy() == Session::None, "session retains load-none policy");
    UsdPrim payload = session.stage()->GetPrimAtPath(SdfPath("/Payload"));
    require(bool(payload) && !payload.IsLoaded(), "load-none leaves payload unloaded");
    require(!session.stage()->GetPrimAtPath(SdfPath("/Payload/Geometry")),
            "load-none does not compose payload contents");

    require(session.reload(), "reload preserves load-none policy");
    require(session.loadPolicy() == Session::None, "reload retains load-none policy");
    payload = session.stage()->GetPrimAtPath(SdfPath("/Payload"));
    require(bool(payload) && !payload.IsLoaded(), "reload keeps payload unloaded");

    require(session.loadFromFile(scenePath, Session::All), "load stage with load-all policy");
    require(session.loadPolicy() == Session::All, "session retains load-all policy");
    payload = session.stage()->GetPrimAtPath(SdfPath("/Payload"));
    require(bool(payload) && payload.IsLoaded(), "load-all loads payload");
    require(bool(session.stage()->GetPrimAtPath(SdfPath("/Payload/Geometry"))),
            "load-all composes payload contents");
}

void
commandMergeApi()
{
    QTemporaryDir dir;
    require(dir.isValid(), "temporary directory");
    const QString assetPath = makeAssetFixture(dir.filePath("asset.usda"));

    {
        Session session;
        require(session.newStage(), "new stage for destructive merge");
        stageviz::Command command = stageviz::mergeStage(assetPath);
        executeCommand(command, session);
        require(bool(session.stage()->GetPrimAtPath(SdfPath("/Asset/Geometry"))), "mergeStage copies authored content");
        undoCommand(command, session);
        require(!session.stage()->GetPrimAtPath(SdfPath("/Asset")), "mergeStage undo restores layer");
        executeCommand(command, session);
        require(bool(session.stage()->GetPrimAtPath(SdfPath("/Asset/Geometry"))), "mergeStage redo restores content");
    }
    {
        Session session;
        require(session.newStage(), "new stage for flattened merge");
        stageviz::Command command = stageviz::mergeFlattenedStage(assetPath);
        executeCommand(command, session);
        require(bool(session.stage()->GetPrimAtPath(SdfPath("/Asset/Geometry"))),
                "mergeFlattenedStage copies composed content");
        undoCommand(command, session);
        require(!session.stage()->GetPrimAtPath(SdfPath("/Asset")), "mergeFlattenedStage undo restores layer");
        executeCommand(command, session);
        require(bool(session.stage()->GetPrimAtPath(SdfPath("/Asset/Geometry"))),
                "mergeFlattenedStage redo restores content");
    }
    {
        Session session;
        require(session.newStage(), "new stage for sublayer command");
        const std::vector<std::string> before = session.stage()->GetRootLayer()->GetSubLayerPaths();
        stageviz::Command command = stageviz::addSublayer(assetPath);
        executeCommand(command, session);
        require(bool(session.stage()->GetPrimAtPath(SdfPath("/Asset/Geometry"))), "addSublayer composes content");
        require(session.stage()->GetRootLayer()->GetSubLayerPaths().size() == before.size() + 1,
                "addSublayer authors one sublayer path");
        undoCommand(command, session);
        require(session.stage()->GetRootLayer()->GetSubLayerPaths() == before,
                "addSublayer undo restores sublayer paths");
        require(!session.stage()->GetPrimAtPath(SdfPath("/Asset")), "addSublayer undo removes composition");
        executeCommand(command, session);
        require(bool(session.stage()->GetPrimAtPath(SdfPath("/Asset/Geometry"))),
                "addSublayer redo restores composition");
    }
    {
        Session session;
        require(session.newStage(), "new stage for reference command");
        session.stage()->DefinePrim(SdfPath("/Target"));
        stageviz::Command command = stageviz::addReference(assetPath, SdfPath("/Target"));
        executeCommand(command, session);
        require(session.stage()->GetPrimAtPath(SdfPath("/Target")).HasAuthoredReferences(),
                "addReference authors reference arc");
        require(bool(session.stage()->GetPrimAtPath(SdfPath("/Target/Geometry"))),
                "addReference composes default prim");
        undoCommand(command, session);
        require(bool(session.stage()->GetPrimAtPath(SdfPath("/Target"))), "addReference undo keeps target");
        require(!session.stage()->GetPrimAtPath(SdfPath("/Target")).HasAuthoredReferences(),
                "addReference undo restores reference field");
        require(!session.stage()->GetPrimAtPath(SdfPath("/Target/Geometry")),
                "addReference undo removes composed content");
        executeCommand(command, session);
        require(bool(session.stage()->GetPrimAtPath(SdfPath("/Target/Geometry"))),
                "addReference redo restores composition");
    }
    {
        Session session;
        require(session.newStage(Session::None), "new stage for payload command");
        session.stage()->DefinePrim(SdfPath("/Target"));
        stageviz::Command command = stageviz::addPayload(assetPath, SdfPath("/Target"));
        executeCommand(command, session);
        require(session.stage()->GetPrimAtPath(SdfPath("/Target")).HasPayload(), "addPayload authors payload arc");
        require(!session.stage()->GetPrimAtPath(SdfPath("/Target")).IsLoaded(), "addPayload respects load-none policy");
        undoCommand(command, session);
        require(bool(session.stage()->GetPrimAtPath(SdfPath("/Target"))), "addPayload undo keeps target");
        require(!session.stage()->GetPrimAtPath(SdfPath("/Target")).HasPayload(),
                "addPayload undo restores payload field");
        executeCommand(command, session);
        require(session.stage()->GetPrimAtPath(SdfPath("/Target")).HasPayload(),
                "addPayload redo restores payload arc");
    }
}

void
namespaceEditorCrudApi()
{
    const auto stage = UsdStage::CreateInMemory();
    stage->DefinePrim(SdfPath("/World"));
    stageviz::edit::NamespaceEditor editor(stage, stage->GetEditTarget());
    QString error;

    require(editor.addXform(SdfPath("/World/New"), error), "namespace add xform");
    require(bool(stage->GetPrimAtPath(SdfPath("/World/New"))), "namespace add materialized");
    require(!editor.changes().isEmpty()
                && editor.changes().last().type == stageviz::edit::NamespaceEditor::Change::Type::Add,
            "namespace add recorded semantic change");
    require(editor.renamePrim(SdfPath("/World/New"), SdfPath("/World/Renamed"), error), "namespace rename");
    require(bool(stage->GetPrimAtPath(SdfPath("/World/Renamed"))), "namespace rename materialized");
    require(editor.reparentPrim(SdfPath("/World/Renamed"), SdfPath("/Moved"), error), "namespace reparent");
    require(bool(stage->GetPrimAtPath(SdfPath("/Moved"))), "namespace reparent materialized");
    require(editor.removePrim(SdfPath("/Moved"), error), "namespace remove prim");
    require(!stage->GetPrimAtPath(SdfPath("/Moved")), "namespace remove materialized");

    stage->DefinePrim(SdfPath("/World/A"));
    stage->DefinePrim(SdfPath("/World/B"));
    stage->DefinePrim(SdfPath("/World/B/Child"));
    require(editor.removePrims({ SdfPath("/World/A"), SdfPath("/World/B"), SdfPath("/World/B/Child") }, error),
            "namespace batch remove");
    require(!stage->GetPrimAtPath(SdfPath("/World/A")) && !stage->GetPrimAtPath(SdfPath("/World/B")),
            "namespace batch remove covers descendants");
}

void
usdPathUtilsApi()
{
    const auto stage = UsdStage::CreateInMemory();
    stage->DefinePrim(SdfPath("/Root"));
    stage->DefinePrim(SdfPath("/Root/Name"));

    require(stageviz::identifier::makeSafeIdentifier(stage, SdfPath("/Root"), "123 bad name") == "_23_bad_name",
            "identifier sanitizes invalid leading digit and spaces");
    require(stageviz::identifier::makeSafeIdentifier(stage, SdfPath("/Root"), "Name") == "Name_1",
            "identifier uniquifies sibling name");
    require(stageviz::identifier::makeSafeIdentifier(stage, SdfPath("/Root"), "Name", SdfPath("/Root/Name")) == "Name",
            "identifier ignore path supports rename");

    const QList<SdfPath> input { SdfPath("/A/B"), SdfPath("/A"), SdfPath("/C"), SdfPath("/A/B"), SdfPath() };
    require(stageviz::path::uniquePaths(input) == QList<SdfPath>({ SdfPath("/A/B"), SdfPath("/A"), SdfPath("/C") }),
            "uniquePaths preserves first occurrence");
    const QList<SdfPath> roots = stageviz::path::minimalRootPaths(input);
    require(roots.contains(SdfPath("/A")) && roots.contains(SdfPath("/C")) && roots.size() == 2,
            "minimalRootPaths removes covered descendants");
    const QList<SdfPath> top = stageviz::path::topLevelPaths({ SdfPath("/A"), SdfPath("/A/B"), SdfPath("/C") });
    require(top.contains(SdfPath("/A")) && top.contains(SdfPath("/C")) && top.size() == 2,
            "topLevelPaths removes descendants");
    require(stageviz::path::isAffectedPath(SdfPath("/A/B.attr"), SdfPath("/A")), "property path affected by root");
    require(stageviz::path::isWithinRoots({ SdfPath("/A") }, SdfPath("/A/B")), "path lies within mask root");
    require(stageviz::path::isWithinRoots({}, SdfPath("/Anything")), "empty mask covers all paths");
    require(stageviz::path::isCoveredByRoots({ SdfPath("/A") }, SdfPath("/A/B")), "selection root covers descendant");
    require(stageviz::path::removeAffectedPaths({ SdfPath("/A"), SdfPath("/B/C") }, { SdfPath("/B") })
                == QList<SdfPath>({ SdfPath("/A") }),
            "removeAffectedPaths removes covered selection");
    require(stageviz::path::remapAffectedPaths({ SdfPath("/A"), SdfPath("/A/B"), SdfPath("/C") }, SdfPath("/A"),
                                               SdfPath("/D"))
                == QList<SdfPath>({ SdfPath("/D"), SdfPath("/D/B"), SdfPath("/C") }),
            "remapAffectedPaths preserves suffixes");
    QList<SdfPath> appended { SdfPath("/A") };
    stageviz::path::appendUnique(appended, SdfPath("/A"));
    stageviz::path::appendUnique(appended, SdfPath("/B"));
    require(appended == QList<SdfPath>({ SdfPath("/A"), SdfPath("/B") }), "appendUnique deduplicates");

    TfTokenVector order { TfToken("A"), TfToken("B"), TfToken("C") };
    require(stageviz::stage::removeChildOrderToken(order, TfToken("B"))
                == TfTokenVector({ TfToken("A"), TfToken("C") }),
            "remove child order token");
    require(stageviz::stage::insertChildOrderToken(order, TfToken("D"), 1)
                == TfTokenVector({ TfToken("A"), TfToken("D"), TfToken("B"), TfToken("C") }),
            "insert child order token");
    require(stageviz::stage::remapChildOrder(order, TfToken("B"), TfToken("X"))
                == TfTokenVector({ TfToken("A"), TfToken("X"), TfToken("C") }),
            "remap child order token");
}

void
usdStageUtilsApi()
{
    const auto stage = UsdStage::CreateInMemory();
    const UsdGeomXform world = UsdGeomXform::Define(stage, SdfPath("/World"));
    const UsdGeomCube visible = UsdGeomCube::Define(stage, SdfPath("/World/Visible"));
    const UsdGeomCube hidden = UsdGeomCube::Define(stage, SdfPath("/World/Hidden"));
    visible.CreateSizeAttr(VtValue(2.0));
    hidden.CreateSizeAttr(VtValue(2.0));

    QString error;
    const SdfLayerHandle root = stage->GetRootLayer();
    require(stageviz::layer::validatePrim(stage, root, SdfPath("/World"), error), "validate authored prim");
    require(stageviz::layer::validateParent(stage, root, SdfPath("/World"), error), "validate authored parent");
    require(stageviz::stage::isAuthored(stage, SdfPath("/World")), "authored prim query");
    require(stageviz::stage::isAuthoredInLayer(stage, root, SdfPath("/World")), "authored layer query");
    require(stageviz::stage::isStrongestInLayer(stage, root, SdfPath("/World")), "strongest layer query");
    require(stageviz::stage::isEditable(stage, SdfPath("/World")), "editable authored xform");
    require(stageviz::stage::isTransformEditable(stage, SdfPath("/World")), "transform editable query");

    stageviz::stage::setVisible(stage, { SdfPath("/World/Hidden") }, false);
    require(!stageviz::stage::isVisible(stage, SdfPath("/World/Hidden")), "visibility hide helper");
    QList<SdfPath> visiblePaths = stageviz::stage::visiblePaths(stage);
    require(visiblePaths.contains(SdfPath("/World/Visible")) && !visiblePaths.contains(SdfPath("/World/Hidden")),
            "visiblePaths respects authored invisibility");
    stageviz::stage::setVisible(stage, { SdfPath("/World") }, false, true);
    require(!stageviz::stage::isVisible(stage, SdfPath("/World/Visible")),
            "recursive visibility applies to descendants");
    stageviz::stage::setVisible(stage, { SdfPath("/World") }, true, true);

    const QList<SdfPath> leaves = stageviz::stage::leafPaths(stage);
    require(leaves.contains(SdfPath("/World/Visible")) && leaves.contains(SdfPath("/World/Hidden")),
            "leafPaths returns terminal prims");
    require(!stageviz::stage::boundingBox(stage, { SdfPath("/World/Visible") }).GetRange().IsEmpty(),
            "boundingBox returns geometry bounds");

    const SdfPath rename = stageviz::stage::buildRenamePath(stage, SdfPath("/World/Visible"), "123 part", error);
    require(rename == SdfPath("/World/_23_part"), "buildRenamePath sanitizes identifier");
    const SdfPath child = stageviz::stage::buildChildPath(stage, SdfPath("/World"), "New Child", error);
    require(child == SdfPath("/World/New_Child"), "buildChildPath sanitizes identifier");
    const SdfPath unique = stageviz::stage::buildUniqueXform(stage, "Visible", SdfPath("/World"));
    require(unique != SdfPath("/World/Visible") && unique.GetParentPath() == SdfPath("/World"),
            "buildUniqueXform avoids existing child");

    TfTokenVector captured;
    require(stageviz::stage::captureChildOrder(stage, SdfPath("/World"), captured), "capture child order");
    TfTokenVector desired = captured;
    std::reverse(desired.begin(), desired.end());
    stageviz::stage::restoreChildOrder(stage, SdfPath("/World"), desired);
    TfTokenVector restored;
    require(stageviz::stage::captureChildOrder(stage, SdfPath("/World"), restored), "recapture child order");
    require(restored == desired, "restore child order");

    GfMatrix4d before;
    require(stageviz::stage::worldTransform(stage, SdfPath("/World"), before, error), "read world transform");
    GfMatrix4d translated(1.0);
    translated.SetTranslate(GfVec3d(5.0, 6.0, 7.0));
    require(stageviz::stage::setWorldTransform(stage, SdfPath("/World"), translated, error), "set world transform");
    GfMatrix4d after;
    require(stageviz::stage::worldTransform(stage, SdfPath("/World"), after, error), "read changed world transform");
    require(GfIsClose(after.ExtractTranslation(), GfVec3d(5.0, 6.0, 7.0), 1e-6), "world transform roundtrip");
    GfVec3d pivot;
    require(stageviz::stage::worldPivot(stage, SdfPath("/World"), pivot, error), "read world pivot");
}

void
usdPayloadUtilsApi()
{
    QTemporaryDir dir;
    require(dir.isValid(), "temporary directory");
    const QString assetPath = makeAssetFixture(dir.filePath("payload.usda"));
    const auto stage = UsdStage::CreateInMemory();
    stage->DefinePrim(SdfPath("/World"));
    const UsdPrim outer = stage->DefinePrim(SdfPath("/World/Outer"));
    require(outer.GetPayloads().AddPayload(assetPath.toStdString()), "author payload fixture");
    require(stageviz::stage::isPayload(stage, SdfPath("/World/Outer")), "isPayload identifies payload root");
    require(stageviz::stage::isPayloadHierarchy(stage, SdfPath("/World/Outer/Geometry")),
            "isPayloadHierarchy identifies payload descendant");
    require(stageviz::stage::isLoaded(stage, SdfPath("/World/Outer")), "loaded payload query");
    require(stageviz::stage::payloadPaths(stage, { SdfPath("/World/Outer") })
                == QList<SdfPath>({ SdfPath("/World/Outer") }),
            "payloadPaths returns direct payload");
    require(stageviz::stage::nearestPayloadPaths(stage, { SdfPath("/World/Outer/Geometry") })
                == QList<SdfPath>({ SdfPath("/World/Outer") }),
            "nearest payload resolves descendant");
    require(stageviz::stage::outermostPayloadPaths(stage, { SdfPath("/World/Outer") })
                == QList<SdfPath>({ SdfPath("/World/Outer") }),
            "outermost payload returns root");
    require(stageviz::stage::resolvePayloadPaths(stage, { SdfPath("/World/Outer/Geometry") })
                == QList<SdfPath>({ SdfPath("/World/Outer") }),
            "resolvePayloadPaths maps descendant");

    stage->Unload(SdfPath("/World/Outer"));
    require(!stageviz::stage::isLoaded(stage, SdfPath("/World/Outer")), "unloaded payload query");
    stageviz::payload::PayloadState state;
    QString error;
    require(stageviz::payload::applyLoad(stage, SdfPath("/World/Outer"), false, {}, {}, state, error),
            "applyLoad loads payload");
    require(stage->GetPrimAtPath(SdfPath("/World/Outer")).IsLoaded(), "applyLoad changed stage state");
    require(stageviz::payload::restoreState(stage, state, error), "restore payload state");
    require(!stage->GetPrimAtPath(SdfPath("/World/Outer")).IsLoaded(), "restoreState restores prior unload");

    stage->Load(SdfPath("/World/Outer"));
    stageviz::payload::PayloadState unloadState;
    require(stageviz::payload::applyUnload(stage, SdfPath("/World/Outer"), unloadState, error),
            "applyUnload unloads payload");
    require(!stage->GetPrimAtPath(SdfPath("/World/Outer")).IsLoaded(), "applyUnload changed stage state");
    require(stageviz::payload::restoreState(stage, unloadState, error), "restore unloaded state");
    require(stage->GetPrimAtPath(SdfPath("/World/Outer")).IsLoaded(), "restoreState restores prior load");

    UsdStageLoadRules rules = UsdStageLoadRules::LoadAll();
    rules.AddRule(SdfPath("/World/Outer"), UsdStageLoadRules::NoneRule);
    const UsdStageLoadRules remapped = stageviz::stage::remapLoadRules(rules, SdfPath("/World/Outer"),
                                                                       SdfPath("/World/Renamed"));
    require(remapped.GetEffectiveRuleForPath(SdfPath("/World/Renamed/Child")) == UsdStageLoadRules::NoneRule,
            "remapLoadRules moves inherited policy");
}

void
materialUtilsApi()
{
    const auto stage = UsdStage::CreateInMemory();
    const SdfPath previewPath = stageviz::MaterialUtils::createPreviewSurfaceMaterial(stage);
    const SdfPath standardPath = stageviz::MaterialUtils::createStandardSurfaceMaterial(stage);
    require(!previewPath.IsEmpty() && !standardPath.IsEmpty() && previewPath != standardPath,
            "material creators return unique paths");

    const QList<stageviz::MaterialEntry> materials = stageviz::MaterialUtils::sceneMaterials(stage);
    require(materials.size() == 2, "sceneMaterials finds supported materials");
    const auto previewIt = std::find_if(materials.cbegin(), materials.cend(), [&](const stageviz::MaterialEntry& e) {
        return e.materialPath == previewPath;
    });
    require(previewIt != materials.cend() && previewIt->shaderId == "UsdPreviewSurface",
            "preview material reports shader id");
    require(stageviz::MaterialUtils::shaderTypeLabel(previewIt->shaderId) == "USD Preview Surface", "shader type label");
    require(stageviz::MaterialUtils::isSupportedParameter(*previewIt, "baseColor"), "preview baseColor supported");
    require(!stageviz::MaterialUtils::isSupportedParameter(*previewIt, "transmission"),
            "preview unsupported transmission rejected");
    require(stageviz::MaterialUtils::inputName(*previewIt, "baseColor") == TfToken("diffuseColor"),
            "preview input name mapping");
    require(stageviz::MaterialUtils::inputPath(*previewIt, "roughness")
                == previewIt->shaderPath.AppendProperty(TfToken("inputs:roughness")),
            "preview input path mapping");

    const auto standardIt = std::find_if(materials.cbegin(), materials.cend(), [&](const stageviz::MaterialEntry& e) {
        return e.materialPath == standardPath;
    });
    require(standardIt != materials.cend() && standardIt->shaderId == "ND_standard_surface_surfaceshader",
            "standard material reports shader id");
    require(stageviz::MaterialUtils::shaderTypeLabel(standardIt->shaderId) == "MaterialX Standard Surface",
            "standard material reports display label");
    require(stageviz::MaterialUtils::isSupportedParameter(*standardIt, "roughness"),
            "standard material supports canonical roughness");
    require(stageviz::MaterialUtils::inputName(*standardIt, "roughness") == TfToken("specular_roughness"),
            "standard roughness maps to MaterialX specular_roughness");

    // Free MaterialX helper nodes must author only identity + outputs. Inputs are
    // NodeDef-declared and materialized lazily when edited/connected. This keeps
    // the USD network aligned with the installed MaterialX declaration and avoids
    // HdMtlx interface-validation failures caused by duplicating the full NodeDef
    // interface into every shader prim.
    {
        stageviz::MaterialXNodeDefinition constantDef;
        require(stageviz::MaterialUtils::materialXNodeDefinition("ND_constant_color3", &constantDef),
                "resolve MaterialX constant color3 NodeDef");

        const SdfPath helperPath("/Materials/Material/RegressionConstant");
        UsdShadeShader helper = UsdShadeShader::Define(stage, helperPath);
        require(bool(helper), "create MaterialX regression helper");

        QString helperError;
        require(stageviz::MaterialUtils::authorMaterialXNodeInterface(helper, constantDef, helperError),
                "author minimal MaterialX helper interface");
        require(stageviz::MaterialUtils::shaderId(helper.GetPrim()) == "ND_constant_color3",
                "MaterialX helper authors NodeDef id");
        require(helper.GetInputs().empty(), "MaterialX helper does not duplicate NodeDef inputs");

        const UsdShadeOutput helperOut = helper.GetOutput(TfToken("out"));
        require(bool(helperOut) && helperOut.GetTypeName() == SdfValueTypeNames->Color3f,
                "MaterialX helper authors typed output");

        const stageviz::MaterialNodeInfo helperInfo = stageviz::MaterialUtils::nodeInfo(stage, helperPath);
        require(!helperInfo.path.IsEmpty() && helperInfo.shaderId == "ND_constant_color3",
                "nodeInfo resolves minimal MaterialX helper");

        const SdfPath valuePath = helperPath.AppendProperty(TfToken("inputs:value"));
        const auto valueIt = std::find_if(helperInfo.inputs.cbegin(), helperInfo.inputs.cend(),
                                          [&](const stageviz::MaterialInputInfo& input) {
                                              return input.inputPath == valuePath;
                                          });
        require(valueIt != helperInfo.inputs.cend() && valueIt->typeName == SdfValueTypeNames->Color3f,
                "nodeInfo exposes unauthored MaterialX NodeDef input");

        require(stageviz::MaterialUtils::ensureShaderInput(stage, valuePath),
                "ensureShaderInput materializes declared MaterialX input");
        const UsdShadeInput valueInput = helper.GetInput(TfToken("value"));
        require(bool(valueInput) && valueInput.GetTypeName() == SdfValueTypeNames->Color3f,
                "materialized MaterialX input preserves NodeDef type");

        require(valueInput.Set(GfVec3f(0.2f, 0.4f, 0.6f)),
                "set lazily-authored MaterialX helper input");
        GfVec3f helperValue;
        require(valueInput.Get(&helperValue) && GfIsClose(helperValue, GfVec3f(0.2f, 0.4f, 0.6f), 1e-6f),
                "read lazily-authored MaterialX helper input");
    }
    require(stageviz::MaterialUtils::inputPath(*standardIt, "roughness")
                == standardIt->shaderPath.AppendProperty(TfToken("inputs:specular_roughness")),
            "standard roughness input path uses MaterialX specular_roughness");
    require(!standardIt->shaderPath.AppendProperty(TfToken("inputs:roughness")).IsEmpty(),
            "standard roughness regression fixture path is valid");

    stageviz::MaterialEntry refreshedPreview;
    require(stageviz::MaterialUtils::materialEntry(stage, previewPath, &refreshedPreview),
            "materialEntry refreshes one material");
    require(refreshedPreview.materialPath == previewPath && refreshedPreview.shaderPath == previewIt->shaderPath
                && refreshedPreview.shaderId == previewIt->shaderId,
            "materialEntry preserves material identity");

    const stageviz::MaterialNodeInfo previewNode = stageviz::MaterialUtils::nodeInfo(stage, previewIt->shaderPath);
    require(previewNode.path == previewIt->shaderPath && previewNode.shaderId == "UsdPreviewSurface",
            "nodeInfo inspects preview shader");
    require(std::any_of(previewNode.inputs.cbegin(), previewNode.inputs.cend(),
                        [](const stageviz::MaterialInputInfo& input) {
                            return input.inputName == TfToken("diffuseColor")
                                   && input.typeName == SdfValueTypeNames->Color3f;
                        }),
            "nodeInfo exposes preview diffuseColor input");

    const SdfPath unique = stageviz::MaterialUtils::uniqueMaterialPath(stage, "123 bad material");
    require(unique.GetName() == "_123_bad_material", "unique material path sanitizes identifier");

    // Exercise the supported Stageviz MaterialX path end-to-end: author a
    // MaterialX Standard Surface in USD, export it using Stageviz's MaterialX
    // serializer, import that file into a clean stage through UsdMtlxRead, and
    // verify that the translated USD material resolves back to the same shader
    // family and parameter values.
    QString standardShaderId;
    const UsdShadeShader standardShader
        = stageviz::MaterialUtils::surfaceShader(UsdShadeMaterial(stage->GetPrimAtPath(standardPath)),
                                                 &standardShaderId);
    require(standardShader && standardShaderId == "ND_standard_surface_surfaceshader",
            "standard material resolves before MaterialX export");
    require(bool(standardShader.GetInput(TfToken("specular_roughness"))),
            "standard material authors specular_roughness");
    require(!standardShader.GetInput(TfToken("roughness")),
            "standard material does not author legacy roughness input");
    require(standardShader.GetInput(TfToken("base_color")).Set(GfVec3f(0.1f, 0.2f, 0.3f)),
            "set MaterialX export base color");
    require(standardShader.GetInput(TfToken("metalness")).Set(0.7f), "set MaterialX export metalness");
    require(standardShader.GetInput(TfToken("specular_roughness")).Set(0.25f),
            "set MaterialX export roughness");
    require(standardShader.GetInput(TfToken("specular")).Set(0.65f), "set MaterialX export specular");
    require(standardShader.GetInput(TfToken("specular_IOR")).Set(1.7f), "set MaterialX export IOR");
    require(standardShader.GetInput(TfToken("coat")).Set(0.35f), "set MaterialX export coat");
    require(standardShader.GetInput(TfToken("coat_roughness")).Set(0.15f),
            "set MaterialX export coat roughness");
    require(standardShader.GetInput(TfToken("transmission")).Set(0.2f),
            "set MaterialX export transmission");
    require(standardShader.GetInput(TfToken("transmission_color")).Set(GfVec3f(0.8f, 0.7f, 0.6f)),
            "set MaterialX export transmission color");
    require(standardShader.GetInput(TfToken("opacity")).Set(GfVec3f(0.9f)),
            "set MaterialX export opacity");

    QTemporaryDir dir;
    require(dir.isValid(), "temporary directory");
    const QString mtlx = dir.filePath("standard_surface_roundtrip.mtlx");
    QString error;
    require(stageviz::MaterialUtils::exportMaterialX(stage, standardPath, mtlx, error),
            "export MaterialX Standard Surface");
    require(QFileInfo::exists(mtlx), "MaterialX export creates file");

    QFile exportedMaterialX(mtlx);
    require(exportedMaterialX.open(QIODevice::ReadOnly | QIODevice::Text),
            "read exported MaterialX");
    const QByteArray exportedMaterialXText = exportedMaterialX.readAll();
    require(exportedMaterialXText.contains("<standard_surface")
                && exportedMaterialXText.contains("type=\"surfaceshader\""),
            "MaterialX export preserves surfaceshader terminal type");
    require(!exportedMaterialXText.contains("type=\"terminal\""),
            "MaterialX export does not serialize Sdr terminal type");
    require(exportedMaterialXText.contains("name=\"specular_roughness\"")
                && exportedMaterialXText.contains("value=\"0.25\""),
            "MaterialX export serializes Standard Surface roughness correctly");
    require(!exportedMaterialXText.contains("<input name=\"roughness\""),
            "MaterialX export does not serialize legacy Standard Surface roughness");

    const UsdStageRefPtr importedStage = UsdStage::CreateInMemory();
    require(bool(importedStage), "create MaterialX import stage");

    QList<SdfPath> imported;
    error.clear();
    require(stageviz::MaterialUtils::importMaterialX(importedStage, mtlx, imported, error),
            "import exported MaterialX");
    require(imported.size() == 1 && bool(importedStage->GetPrimAtPath(imported.first())),
            "MaterialX roundtrip creates one usable USD material");

    QString importedShaderId;
    const UsdShadeShader importedShader
        = stageviz::MaterialUtils::surfaceShader(UsdShadeMaterial(importedStage->GetPrimAtPath(imported.first())),
                                                 &importedShaderId);
    require(bool(importedShader), "imported MaterialX resolves surface shader");
    require(importedShaderId.startsWith("ND_standard_surface"),
            "imported MaterialX exposes standard surface");

    const stageviz::MaterialParameters params
        = stageviz::MaterialUtils::readParameters(importedShader, importedShaderId);

    const bool baseColorOk = GfIsClose(params.baseColor, GfVec3f(0.1f, 0.2f, 0.3f), 1e-5f);
    const bool metalnessOk = closeEnough(params.metalness, 0.7, 1e-5);
    const bool roughnessOk = closeEnough(params.roughness, 0.25, 1e-5);
    const bool specularOk = closeEnough(params.specular, 0.65, 1e-5);
    const bool iorOk = closeEnough(params.ior, 1.7, 1e-5);
    const bool coatOk = closeEnough(params.coat, 0.35, 1e-5);
    const bool coatRoughnessOk = closeEnough(params.coatRoughness, 0.15, 1e-5);
    const bool transmissionOk = closeEnough(params.transmission, 0.2, 1e-5);
    const bool transmissionColorOk
        = GfIsClose(params.transmissionColor, GfVec3f(0.8f, 0.7f, 0.6f), 1e-5f);
    const bool opacityOk = closeEnough(params.opacity, 0.9, 1e-5);

    if (!baseColorOk || !metalnessOk || !roughnessOk || !specularOk || !iorOk || !coatOk
        || !coatRoughnessOk || !transmissionOk || !transmissionColorOk || !opacityOk) {
        std::cerr << "material_utils_api imported shader id: "
                  << importedShaderId.toStdString() << std::endl;
        std::cerr << "material_utils_api roundtrip values:"
                  << " baseColor=(" << params.baseColor[0] << ", " << params.baseColor[1] << ", "
                  << params.baseColor[2] << ")"
                  << " metalness=" << params.metalness
                  << " roughness=" << params.roughness
                  << " specular=" << params.specular
                  << " ior=" << params.ior
                  << " coat=" << params.coat
                  << " coatRoughness=" << params.coatRoughness
                  << " transmission=" << params.transmission
                  << " transmissionColor=(" << params.transmissionColor[0] << ", "
                  << params.transmissionColor[1] << ", " << params.transmissionColor[2] << ")"
                  << " opacity=" << params.opacity << std::endl;

        std::string importedStageText;
        if (importedStage->GetRootLayer()->ExportToString(&importedStageText)) {
            std::cerr << "material_utils_api imported USD:\n"
                      << importedStageText << std::endl;
        }
    }

    require(baseColorOk && metalnessOk && roughnessOk && specularOk && iorOk && coatOk
                && coatRoughnessOk && transmissionOk && transmissionColorOk && opacityOk,
            "MaterialX export/import parameters roundtrip");

    stageviz::MaterialEntry importedEntry;
    require(stageviz::MaterialUtils::materialEntry(importedStage, imported.first(), &importedEntry),
            "materialEntry reads imported MaterialX material");
    require(importedEntry.shaderId == "ND_standard_surface_surfaceshader",
            "imported materialEntry preserves Standard Surface shader id");
    require(stageviz::MaterialUtils::inputName(importedEntry, "roughness") == TfToken("specular_roughness"),
            "imported materialEntry maps canonical roughness correctly");
    require(importedEntry.parameters.roughness == params.roughness,
            "imported materialEntry uses connected MaterialX parameter values");
}

void
materialMenuApi()
{
    using MaterialMenu = stageviz::MaterialMenu;

    require(MaterialMenu::compatibility(SdfValueTypeNames->Float, SdfValueTypeNames->Float)
                == MaterialMenu::Compatibility::Compatible,
            "material menu accepts exact socket types");
    require(MaterialMenu::compatibility(SdfValueTypeNames->Float2, SdfValueTypeNames->TexCoord2f)
                == MaterialMenu::Compatibility::Compatible,
            "material menu accepts float2 to texcoord2f");
    require(MaterialMenu::compatibility(SdfValueTypeNames->TexCoord2f, SdfValueTypeNames->Float2)
                == MaterialMenu::Compatibility::Compatible,
            "material menu accepts texcoord2f to float2");
    require(MaterialMenu::compatibility(SdfValueTypeNames->Float3, SdfValueTypeNames->Color3f)
                == MaterialMenu::Compatibility::Incompatible,
            "material menu rejects generic float3 to color3f");

    MaterialMenu::Request usdRequest;
    usdRequest.usdPreviewOnly = true;
    usdRequest.includeSearch = false;
    const QList<MaterialMenu::Choice> usdChoices = MaterialMenu::choices(usdRequest);
    require(!usdChoices.isEmpty(), "material menu discovers USD Preview helper nodes");
    require(std::all_of(usdChoices.cbegin(), usdChoices.cend(),
                        [](const MaterialMenu::Choice& choice) {
                            return choice.family == MaterialMenu::Family::UsdPreview;
                        }),
            "USD Preview request excludes MaterialX nodes");
    require(std::any_of(usdChoices.cbegin(), usdChoices.cend(),
                        [](const MaterialMenu::Choice& choice) {
                            return choice.shaderId == "UsdUVTexture" && choice.outputName == TfToken("rgb");
                        }),
            "USD Preview menu contains UV Texture");
    require(std::any_of(usdChoices.cbegin(), usdChoices.cend(),
                        [](const MaterialMenu::Choice& choice) {
                            return choice.shaderId == "UsdTransform2d" && choice.outputType == SdfValueTypeNames->Float2;
                        }),
            "USD Preview menu contains Transform2d");

    MaterialMenu::Request colorRequest;
    colorRequest.usdPreviewOnly = true;
    colorRequest.includeSearch = false;
    colorRequest.targetType = SdfValueTypeNames->Color3f;
    const QList<MaterialMenu::Choice> colorChoices = MaterialMenu::choices(colorRequest);
    require(std::any_of(colorChoices.cbegin(), colorChoices.cend(),
                        [](const MaterialMenu::Choice& choice) {
                            return choice.shaderId == "UsdUVTexture" && choice.outputName == TfToken("rgb");
                        }),
            "color input accepts UV Texture rgb");
    require(std::none_of(colorChoices.cbegin(), colorChoices.cend(),
                         [](const MaterialMenu::Choice& choice) {
                             return choice.shaderId == "UsdPrimvarReader_float3";
                         }),
            "color input rejects generic float3 primvar reader");

    MaterialMenu::Request vec2Request;
    vec2Request.usdPreviewOnly = true;
    vec2Request.includeSearch = false;
    vec2Request.targetType = SdfValueTypeNames->Float2;
    const QList<MaterialMenu::Choice> vec2Choices = MaterialMenu::choices(vec2Request);
    require(std::any_of(vec2Choices.cbegin(), vec2Choices.cend(),
                        [](const MaterialMenu::Choice& choice) {
                            return choice.shaderId == "UsdTransform2d";
                        }),
            "float2 input exposes Transform2d");
    require(std::any_of(vec2Choices.cbegin(), vec2Choices.cend(),
                        [](const MaterialMenu::Choice& choice) {
                            return choice.shaderId == "UsdPrimvarReader_float2";
                        }),
            "float2 input exposes PrimvarReader_float2");

    MaterialMenu::Request materialXRequest;
    materialXRequest.materialXOnly = true;
    materialXRequest.includeSearch = false;
    const QList<MaterialMenu::Choice> materialXChoices = MaterialMenu::choices(materialXRequest);
    require(!materialXChoices.isEmpty(), "material menu discovers MaterialX nodes");
    require(std::all_of(materialXChoices.cbegin(), materialXChoices.cend(),
                        [](const MaterialMenu::Choice& choice) {
                            return choice.family == MaterialMenu::Family::MaterialX && !choice.nodeDef.isEmpty();
                        }),
            "MaterialX request returns typed NodeDef choices");
    require(std::none_of(materialXChoices.cbegin(), materialXChoices.cend(),
                         [](const MaterialMenu::Choice& choice) {
                             return choice.nodeDef.startsWith(QStringLiteral("ND_UsdPrimvarReader_"));
                         }),
            "MaterialX menu excludes Storm-incompatible USD PrimvarReader compatibility nodes");

    MaterialMenu::Request materialXVec2Request;
    materialXVec2Request.materialXOnly = true;
    materialXVec2Request.includeSearch = false;
    materialXVec2Request.targetType = SdfValueTypeNames->Float2;
    const QList<MaterialMenu::Choice> materialXVec2Choices = MaterialMenu::choices(materialXVec2Request);
    require(!materialXVec2Choices.isEmpty(), "MaterialX float2 request discovers compatible nodes");
    require(std::none_of(materialXVec2Choices.cbegin(), materialXVec2Choices.cend(),
                         [](const MaterialMenu::Choice& choice) {
                             return choice.nodeDef.startsWith(QStringLiteral("ND_UsdPrimvarReader_"));
                         }),
            "MaterialX float2 menu excludes Storm-incompatible USD PrimvarReader nodes");

    const UsdStageRefPtr stage = UsdStage::CreateInMemory();
    UsdShadeShader source = UsdShadeShader::Define(stage, SdfPath("/Source"));
    UsdShadeShader target = UsdShadeShader::Define(stage, SdfPath("/Target"));
    const UsdShadeOutput vec2Out = source.CreateOutput(TfToken("vec2"), SdfValueTypeNames->Float2);
    const UsdShadeInput texcoordIn = target.CreateInput(TfToken("texcoord"), SdfValueTypeNames->TexCoord2f);
    const UsdShadeOutput vec3Out = source.CreateOutput(TfToken("vec3"), SdfValueTypeNames->Float3);
    const UsdShadeInput colorIn = target.CreateInput(TfToken("color"), SdfValueTypeNames->Color3f);

    require(MaterialMenu::connectionCompatibility(stage, vec2Out.GetAttr().GetPath(), texcoordIn.GetAttr().GetPath())
                == MaterialMenu::Compatibility::Compatible,
            "material menu path compatibility accepts float2 to texcoord2f");
    require(MaterialMenu::connectionCompatibility(stage, vec3Out.GetAttr().GetPath(), colorIn.GetAttr().GetPath())
                == MaterialMenu::Compatibility::Incompatible,
            "material menu path compatibility rejects float3 to color3f");
}

void
commandMaterialApi()
{
    Session session;
    require(session.newStage(), "new stage");
    const UsdStageRefPtr stage = session.stage();

    UsdGeomCube::Define(stage, SdfPath("/Target"));
    const SdfPath firstMaterial = stageviz::MaterialUtils::createPreviewSurfaceMaterial(stage);
    const SdfPath secondMaterial = stageviz::MaterialUtils::createPreviewSurfaceMaterial(stage);
    require(!firstMaterial.IsEmpty() && !secondMaterial.IsEmpty() && firstMaterial != secondMaterial,
            "material command fixture creates two materials");

    stageviz::Command firstBind = stageviz::bindMaterial({ SdfPath("/Target") }, firstMaterial);
    executeCommand(firstBind, session);
    require(UsdShadeMaterialBindingAPI(stage->GetPrimAtPath(SdfPath("/Target"))).ComputeBoundMaterial().GetPath()
                == firstMaterial,
            "bindMaterial authors first binding");

    stageviz::Command secondBind = stageviz::bindMaterial({ SdfPath("/Target") }, secondMaterial);
    executeCommand(secondBind, session);
    require(UsdShadeMaterialBindingAPI(stage->GetPrimAtPath(SdfPath("/Target"))).ComputeBoundMaterial().GetPath()
                == secondMaterial,
            "bindMaterial replaces direct binding");
    undoCommand(secondBind, session);
    require(UsdShadeMaterialBindingAPI(stage->GetPrimAtPath(SdfPath("/Target"))).ComputeBoundMaterial().GetPath()
                == firstMaterial,
            "bindMaterial undo restores previous binding");

    stageviz::MaterialEntry entry;
    require(stageviz::MaterialUtils::materialEntry(stage, firstMaterial, &entry),
            "material command fixture resolves preview material");

    const SdfPath texturePath = firstMaterial.AppendChild(TfToken("Texture"));
    stageviz::Command newTexture = stageviz::newShaderNode(firstMaterial, "UsdUVTexture", "Texture", TfToken("rgb"),
                                                           SdfValueTypeNames->Float3);
    executeCommand(newTexture, session);
    UsdShadeShader texture(stage->GetPrimAtPath(texturePath));
    TfToken textureId;
    require(texture && texture.GetIdAttr().Get(&textureId) && textureId == TfToken("UsdUVTexture"),
            "newShaderNode creates requested USD helper node");
    require(texture.GetOutput(TfToken("rgb")).GetTypeName() == SdfValueTypeNames->Float3,
            "newShaderNode authors requested output type");

    undoCommand(newTexture, session);
    require(!stage->GetPrimAtPath(texturePath), "newShaderNode undo removes node");
    executeCommand(newTexture, session);
    require(bool(stage->GetPrimAtPath(texturePath)), "newShaderNode redo restores node");

    const SdfPath inputPath = entry.shaderPath.AppendProperty(TfToken("inputs:diffuseColor"));
    const SdfPath outputPath = texturePath.AppendProperty(TfToken("outputs:rgb"));
    require(bool(stage->GetAttributeAtPath(inputPath)) && bool(stage->GetAttributeAtPath(outputPath)),
            "shader connection fixture properties exist");

    stageviz::Command connect = stageviz::connectShaderInput(inputPath, outputPath);
    executeCommand(connect, session);
    SdfPathVector connections;
    require(stage->GetAttributeAtPath(inputPath).GetConnections(&connections)
                && connections == SdfPathVector { outputPath },
            "connectShaderInput authors connection");
    undoCommand(connect, session);
    connections.clear();
    const bool hasConnectionsAfterUndo = stage->GetAttributeAtPath(inputPath).GetConnections(&connections);
    require(!hasConnectionsAfterUndo || connections.empty(),
            "connectShaderInput undo restores previous unconnected state");

    executeCommand(connect, session);
    stageviz::Command disconnect = stageviz::disconnectShaderInputs({ inputPath });
    executeCommand(disconnect, session);
    connections.clear();
    require(stage->GetAttributeAtPath(inputPath).GetConnections(&connections) && connections.empty(),
            "disconnectShaderInputs clears connection");
    undoCommand(disconnect, session);
    connections.clear();
    require(stage->GetAttributeAtPath(inputPath).GetConnections(&connections)
                && connections == SdfPathVector { outputPath },
            "disconnectShaderInputs undo restores connection");

    // Disconnect once more so the node can be tested independently of an incoming dependency.
    executeCommand(disconnect, session);
    const SdfLayerHandle editLayer = stage->GetEditTarget().GetLayer();
    const SdfPath roughnessPath = entry.shaderPath.AppendProperty(TfToken("inputs:roughness"));
    require(editLayer && editLayer->GetPropertyAtPath(roughnessPath),
            "resetShaderInputs fixture has authored roughness");
    stageviz::Command resetInput = stageviz::resetShaderInputs({ roughnessPath });
    executeCommand(resetInput, session);
    require(!editLayer->GetPropertyAtPath(roughnessPath),
            "resetShaderInputs removes current edit-layer input opinion");
    undoCommand(resetInput, session);
    require(bool(editLayer->GetPropertyAtPath(roughnessPath)),
            "resetShaderInputs undo restores exact input property");

    stageviz::Command deleteNode = stageviz::deleteShaderNode(texturePath);
    executeCommand(deleteNode, session);
    require(!stage->GetPrimAtPath(texturePath), "deleteShaderNode removes helper node");
    undoCommand(deleteNode, session);
    require(bool(stage->GetPrimAtPath(texturePath)), "deleteShaderNode undo restores helper node");
}

void
commandSelectionApi()
{
    Session session;
    require(session.newStage(), "new stage");
    session.stage()->DefinePrim(SdfPath("/A"));
    session.stage()->DefinePrim(SdfPath("/A/Leaf"));
    session.stage()->DefinePrim(SdfPath("/B"));
    session.stage()->DefinePrim(SdfPath("/B/Leaf"));

    stageviz::Command select = stageviz::selectPaths({ SdfPath("/A") });
    executeCommand(select, session);
    require(session.selectionList()->paths() == QList<SdfPath>({ SdfPath("/A") }), "selectPaths command");
    undoCommand(select, session);
    require(session.selectionList()->paths().isEmpty(), "selectPaths undo");

    stageviz::Command all = stageviz::selectAll(false);
    executeCommand(all, session);
    const QList<SdfPath> roots = session.selectionList()->paths();
    require(roots.contains(SdfPath("/World")) && roots.contains(SdfPath("/A")) && roots.contains(SdfPath("/B"))
                && roots.size() == 3,
            "selectAll root mode");
    undoCommand(all, session);

    session.selectionList()->updatePaths({ SdfPath("/A/Leaf") });
    stageviz::Command invert = stageviz::selectInvert();
    executeCommand(invert, session);
    require(session.selectionList()->paths().contains(SdfPath("/B/Leaf"))
                && !session.selectionList()->paths().contains(SdfPath("/A/Leaf")),
            "selectInvert command");
    undoCommand(invert, session);
    require(session.selectionList()->paths() == QList<SdfPath>({ SdfPath("/A/Leaf") }), "selectInvert undo");

    stageviz::Command isolate = stageviz::isolatePaths({ SdfPath("/A") });
    executeCommand(isolate, session);
    require(session.mask() == QList<SdfPath>({ SdfPath("/A") }), "isolatePaths command");
    undoCommand(isolate, session);
    require(session.mask().isEmpty(), "isolatePaths undo");
}

void
commandAuthoringApi()
{
    Session session;
    require(session.newStage(), "new stage");
    session.stage()->DefinePrim(SdfPath("/World"), TfToken("Xform"));

    stageviz::Command newPrim = stageviz::newPrimPath(SdfPath("/World"), "123 part", TfToken("Xform"));
    executeCommand(newPrim, session);
    require(bool(session.stage()->GetPrimAtPath(SdfPath("/World/_23_part"))), "newPrimPath authors sanitized prim");
    undoCommand(newPrim, session);
    require(!session.stage()->GetPrimAtPath(SdfPath("/World/_23_part")), "newPrimPath undo");

    stageviz::Command scope = stageviz::newScopePath(SdfPath("/World"), "Scope");
    executeCommand(scope, session);
    require(session.stage()->GetPrimAtPath(SdfPath("/World/Scope")).GetTypeName() == TfToken("Scope"),
            "newScopePath authors scope");

    stageviz::Command xform = stageviz::newXformPath(SdfPath("/World"), "Xform");
    executeCommand(xform, session);
    require(session.stage()->GetPrimAtPath(SdfPath("/World/Xform")).GetTypeName() == TfToken("Xform"),
            "newXformPath authors xform");

    stageviz::Command material = stageviz::newMaterialPath(SdfPath("/World"), "Material");
    executeCommand(material, session);
    require(session.stage()->GetPrimAtPath(SdfPath("/World/Material")).IsA<UsdShadeMaterial>(),
            "newMaterialPath authors material");

    stageviz::Command rename = stageviz::renamePath(SdfPath("/World/Xform"), "Renamed");
    executeCommand(rename, session);
    require(bool(session.stage()->GetPrimAtPath(SdfPath("/World/Renamed"))), "renamePath command");
    undoCommand(rename, session);
    require(bool(session.stage()->GetPrimAtPath(SdfPath("/World/Xform"))), "renamePath undo");

    stageviz::Command duplicate = stageviz::duplicatePaths({ SdfPath("/World/Xform") });
    executeCommand(duplicate, session);
    require(bool(session.stage()->GetPrimAtPath(SdfPath("/World/Xform_1"))), "duplicatePaths command");
    undoCommand(duplicate, session);
    require(!session.stage()->GetPrimAtPath(SdfPath("/World/Xform_1")), "duplicatePaths undo");

    stageviz::Command remove = stageviz::deletePaths({ SdfPath("/World/Scope") });
    executeCommand(remove, session);
    require(!session.stage()->GetPrimAtPath(SdfPath("/World/Scope")), "deletePaths command");
    undoCommand(remove, session);
    require(bool(session.stage()->GetPrimAtPath(SdfPath("/World/Scope"))), "deletePaths undo");

    // CommandStack itself depends on the global Stageviz Application/session
    // accessor. Native regression tests exercise command execute/undo directly
    // against their explicit Session; stack history behavior belongs in an
    // Application-backed integration test.
}

void
commandGeneratedGeometryApi()
{
    Session session;
    require(session.newStage(), "new stage");
    session.stage()->DefinePrim(SdfPath("/World"), TfToken("Xform"));

    struct Case {
        const char* name;
        const char* type;
        std::function<stageviz::Command()> make;
    };

    const std::vector<Case> cases {
        { "WaveMesh", "Mesh", [&]() { return stageviz::newMeshPath(SdfPath("/World"), "WaveMesh"); } },
        { "Particles", "Points", [&]() { return stageviz::newPointsPath(SdfPath("/World"), "Particles"); } },
        { "BasisCurves", "BasisCurves",
          [&]() { return stageviz::newBasisCurvesPath(SdfPath("/World"), "BasisCurves"); } },
        { "NurbsCurves", "NurbsCurves",
          [&]() { return stageviz::newNurbsCurvesPath(SdfPath("/World"), "NurbsCurves"); } },
        { "NurbsPatch", "NurbsPatch", [&]() { return stageviz::newNurbsPatchPath(SdfPath("/World"), "NurbsPatch"); } },
        { "PointInstancer", "PointInstancer",
          [&]() { return stageviz::newPointInstancerPath(SdfPath("/World"), "PointInstancer"); } },
    };

    for (const Case& item : cases) {
        const SdfPath path = SdfPath("/World").AppendChild(TfToken(item.name));
        stageviz::Command command = item.make();
        executeCommand(command, session);

        const UsdPrim prim = session.stage()->GetPrimAtPath(path);
        require(bool(prim), "generated geometry command creates prim");
        require(prim.GetTypeName() == TfToken(item.type), "generated geometry command preserves schema type");
        require(session.selectionList()->paths() == QList<SdfPath>({ path }), "generated geometry selects prim");

        if (prim.GetTypeName() == TfToken("Mesh")) {
            VtVec3fArray points;
            require(prim.GetAttribute(TfToken("points")).Get(&points) && !points.empty(), "wavy mesh has points");
        }
        else if (prim.GetTypeName() == TfToken("Points")) {
            VtVec3fArray points;
            VtFloatArray widths;
            require(prim.GetAttribute(TfToken("points")).Get(&points) && !points.empty(), "points prim has points");
            require(prim.GetAttribute(TfToken("widths")).Get(&widths) && widths.size() == points.size(),
                    "points prim has widths");
        }
        else if (prim.GetTypeName() == TfToken("BasisCurves") || prim.GetTypeName() == TfToken("NurbsCurves")) {
            VtVec3fArray points;
            VtIntArray counts;
            require(prim.GetAttribute(TfToken("points")).Get(&points) && !points.empty(), "curves have points");
            require(prim.GetAttribute(TfToken("curveVertexCounts")).Get(&counts) && !counts.empty(),
                    "curves have vertex counts");
        }
        else if (prim.GetTypeName() == TfToken("NurbsPatch")) {
            VtVec3fArray points;
            require(prim.GetAttribute(TfToken("points")).Get(&points) && points.size() == 25,
                    "NURBS patch has control grid");
        }
        else if (prim.GetTypeName() == TfToken("PointInstancer")) {
            require(bool(session.stage()->GetPrimAtPath(
                        path.AppendChild(TfToken("Prototypes")).AppendChild(TfToken("Cube")))),
                    "point instancer has Cube prototype");
            VtIntArray indices;
            VtVec3fArray positions;
            require(prim.GetAttribute(TfToken("protoIndices")).Get(&indices) && !indices.empty(),
                    "point instancer has prototype indices");
            require(prim.GetAttribute(TfToken("positions")).Get(&positions) && positions.size() == indices.size(),
                    "point instancer has positions");
        }

        undoCommand(command, session);
        require(!session.stage()->GetPrimAtPath(path), "generated geometry undo removes prim");
        executeCommand(command, session);
        require(bool(session.stage()->GetPrimAtPath(path)), "generated geometry redo recreates prim");
        undoCommand(command, session);
    }
}

void
commandPropertyApi()
{
    Session session;
    require(session.newStage(), "new stage");
    const UsdPrim prim = session.stage()->DefinePrim(SdfPath("/World"));
    const UsdAttribute attr = prim.CreateAttribute(TfToken("value"), SdfValueTypeNames->Float);
    attr.Set(1.0f);
    const SdfPath attrPath = attr.GetPath();

    stageviz::Command set = stageviz::setAttributeValue(attrPath, VtValue(2.5f));
    executeCommand(set, session);
    float value = 0.0f;
    require(attr.Get(&value) && closeEnough(value, 2.5), "setAttributeValue command");
    undoCommand(set, session);
    require(attr.Get(&value) && closeEnough(value, 1.0), "setAttributeValue undo");

    const UsdAttribute second = prim.CreateAttribute(TfToken("second"), SdfValueTypeNames->Float);
    second.Set(3.0f);
    stageviz::Command setMany = stageviz::setAttributeValues({ attrPath, second.GetPath() }, VtValue(4.0f));
    executeCommand(setMany, session);
    float secondValue = 0.0f;
    require(attr.Get(&value) && second.Get(&secondValue) && closeEnough(value, 4.0) && closeEnough(secondValue, 4.0),
            "setAttributeValues command");
    undoCommand(setMany, session);
    require(attr.Get(&value) && second.Get(&secondValue) && closeEnough(value, 1.0) && closeEnough(secondValue, 3.0),
            "setAttributeValues undo");

    stageviz::Command reset = stageviz::resetAttributeValues({ attrPath });
    executeCommand(reset, session);
    require(!attr.HasAuthoredValueOpinion(), "resetAttributeValues removes authored default");
    undoCommand(reset, session);
    require(attr.Get(&value) && closeEnough(value, 1.0), "resetAttributeValues undo restores value");
}

void
commandVisibilityStageApi()
{
    Session session;
    require(session.newStage(), "new stage");
    require(session.stageUp() == Session::Z, "new stage defaults to Z up");
    UsdGeomCube::Define(session.stage(), SdfPath("/Cube"));

    stageviz::Command hide = stageviz::hidePaths({ SdfPath("/Cube") }, false);
    executeCommand(hide, session);
    require(!stageviz::stage::isVisible(session.stage(), SdfPath("/Cube")), "hidePaths command");
    undoCommand(hide, session);
    require(stageviz::stage::isVisible(session.stage(), SdfPath("/Cube")), "hidePaths undo");

    stageviz::Command show = stageviz::showPaths({ SdfPath("/Cube") }, false);
    executeCommand(show, session);
    require(stageviz::stage::isVisible(session.stage(), SdfPath("/Cube")), "showPaths command");

    stageviz::Command up = stageviz::stageUp(Session::Y);
    executeCommand(up, session);
    require(session.stageUp() == Session::Y, "stageUp command");
    undoCommand(up, session);
    require(session.stageUp() == Session::Z, "stageUp undo");

    stageviz::Command makeDefault = stageviz::defaultPrimPath(SdfPath("/Cube"));
    executeCommand(makeDefault, session);
    require(session.stage()->GetDefaultPrim().GetPath() == SdfPath("/Cube"), "defaultPrimPath command");
    undoCommand(makeDefault, session);
    require(session.stage()->GetDefaultPrim().GetPath() == SdfPath("/World"),
            "defaultPrimPath undo restores previous default prim");

    stageviz::Command clearDefault = stageviz::clearDefaultPrim();
    executeCommand(makeDefault, session);
    executeCommand(clearDefault, session);
    require(!session.stage()->GetDefaultPrim(), "clearDefaultPrim command");
    undoCommand(clearDefault, session);
    require(session.stage()->GetDefaultPrim().GetPath() == SdfPath("/Cube"), "clearDefaultPrim undo");
}

void
commandMaterialVariantApi()
{
    Session session;
    require(session.newStage(), "new stage");
    const UsdPrim target = session.stage()->DefinePrim(SdfPath("/Target"));
    const SdfPath materialPath = stageviz::MaterialUtils::createPreviewSurfaceMaterial(session.stage());

    stageviz::Command bind = stageviz::bindMaterial({ SdfPath("/Target") }, materialPath);
    executeCommand(bind, session);
    const UsdShadeMaterial bound = UsdShadeMaterialBindingAPI(target).ComputeBoundMaterial();
    require(bound && bound.GetPath() == materialPath, "bindMaterial command");
    undoCommand(bind, session);
    require(!UsdShadeMaterialBindingAPI(target).ComputeBoundMaterial(), "bindMaterial undo");

    UsdVariantSet variants = target.GetVariantSets().AddVariantSet("trim");
    require(variants.AddVariant("Base") && variants.AddVariant("Sport"), "author variant fixture");
    require(variants.SetVariantSelection("Base"), "select base variant fixture");
    stageviz::Command variant = stageviz::setVariantSelection({ SdfPath("/Target") }, "trim", "Sport");
    executeCommand(variant, session);
    require(target.GetVariantSet("trim").GetVariantSelection() == "Sport", "setVariantSelection command");
    undoCommand(variant, session);
    require(target.GetVariantSet("trim").GetVariantSelection() == "Base", "setVariantSelection undo");
}

void
commandCompositionApi()
{
    QTemporaryDir dir;
    require(dir.isValid(), "temporary directory");
    const QString assetPath = makeAssetFixture(dir.filePath("asset.usda"));

    Session session;
    require(session.newStage(), "new stage");
    session.stage()->DefinePrim(SdfPath("/World"));

    stageviz::Command reference = stageviz::newReferencePath(SdfPath("/World"), "Reference", assetPath);
    executeCommand(reference, session);
    require(bool(session.stage()->GetPrimAtPath(SdfPath("/World/Reference/Geometry"))),
            "newReferencePath composes asset");
    require(session.stage()->GetPrimAtPath(SdfPath("/World/Reference")).HasAuthoredReferences(),
            "newReferencePath authors reference arc");
    undoCommand(reference, session);
    require(!session.stage()->GetPrimAtPath(SdfPath("/World/Reference")), "newReferencePath undo");

    stageviz::Command payload = stageviz::newPayloadPath(SdfPath("/World"), "Payload", assetPath);
    executeCommand(payload, session);
    require(session.stage()->GetPrimAtPath(SdfPath("/World/Payload")).HasPayload(),
            "newPayloadPath authors payload arc");
    require(bool(session.stage()->GetPrimAtPath(SdfPath("/World/Payload/Geometry"))), "newPayloadPath composes asset");
    undoCommand(payload, session);
    require(!session.stage()->GetPrimAtPath(SdfPath("/World/Payload")), "newPayloadPath undo");
}

void
commandMoveApi()
{
    Session session;
    require(session.newStage(), "new stage");
    UsdGeomXform::Define(session.stage(), SdfPath("/A"));
    UsdGeomXform::Define(session.stage(), SdfPath("/B"));
    UsdGeomXform part = UsdGeomXform::Define(session.stage(), SdfPath("/A/Part"));
    part.AddTranslateOp().Set(GfVec3d(3.0, 4.0, 5.0));

    QString error;
    GfMatrix4d before;
    require(stageviz::stage::worldTransform(session.stage(), SdfPath("/A/Part"), before, error),
            "move fixture world transform");

    stageviz::Command move = stageviz::movePath({ SdfPath("/A/Part") }, SdfPath("/B"), -1, true);
    executeCommand(move, session);
    require(bool(session.stage()->GetPrimAtPath(SdfPath("/B/Part")))
                && !session.stage()->GetPrimAtPath(SdfPath("/A/Part")),
            "movePath reparents prim");
    GfMatrix4d after;
    require(stageviz::stage::worldTransform(session.stage(), SdfPath("/B/Part"), after, error),
            "moved prim world transform readable");
    require(GfIsClose(before, after, 1e-6), "movePath preserves world transform");

    undoCommand(move, session);
    require(bool(session.stage()->GetPrimAtPath(SdfPath("/A/Part")))
                && !session.stage()->GetPrimAtPath(SdfPath("/B/Part")),
            "movePath undo restores namespace");
    GfMatrix4d restored;
    require(stageviz::stage::worldTransform(session.stage(), SdfPath("/A/Part"), restored, error),
            "restored prim world transform readable");
    require(GfIsClose(before, restored, 1e-6), "movePath undo restores world transform");

    // setTransforms executed normally as a command must restore both
    // the original world transforms and the exact authored xform state on undo.
    // One prim starts with a translate op while the other has no xform ops, so
    // undo also proves that temporary matrix-op authoring is removed correctly.
    {
        Session transformSession;
        require(transformSession.newStage(), "new stage for setTransforms execute/undo");
        const UsdStageRefPtr stage = transformSession.stage();
        const SdfLayerHandle editLayer = stage->GetEditTarget().GetLayer();

        const SdfPath firstPath("/TransformFirst");
        const SdfPath secondPath("/TransformSecond");

        UsdGeomXform first = UsdGeomXform::Define(stage, firstPath);
        UsdGeomXform second = UsdGeomXform::Define(stage, secondPath);
        require(bool(first) && bool(second), "define setTransforms execute/undo fixture");

        first.AddTranslateOp().Set(GfVec3d(1.0, 2.0, 3.0));

        const QList<SdfPath> paths { firstPath, secondPath };
        stageviz::XformEdit edit;
        QString error;

        for (const SdfPath& path : paths) {
            GfMatrix4d matrix(1.0);
            require(stageviz::stage::worldTransform(stage, path, matrix, error),
                    "capture setTransforms execute before matrix");
            edit.before.append(matrix);
            edit.beforeState.append(captureXformState(editLayer, path));
        }

        GfMatrix4d firstAfter(1.0);
        firstAfter.SetTranslate(GfVec3d(10.0, 20.0, 30.0));
        GfMatrix4d secondAfter(1.0);
        secondAfter.SetTranslate(GfVec3d(-4.0, 5.0, 6.0));
        edit.after = { firstAfter, secondAfter };

        std::string beforeLayer;
        require(editLayer->ExportToString(&beforeLayer), "capture setTransforms execute before layer");

        stageviz::Command setTransformCommand = stageviz::setTransforms(paths, edit);
        executeCommand(setTransformCommand, transformSession);

        for (qsizetype i = 0; i < paths.size(); ++i) {
            GfMatrix4d matrix(1.0);
            require(stageviz::stage::worldTransform(stage, paths.at(i), matrix, error),
                    "read setTransforms executed matrix");
            require(GfIsClose(matrix, edit.after.at(i), 1e-6),
                    "setTransforms execute reaches requested world matrix");
        }

        undoCommand(setTransformCommand, transformSession);

        std::string undoneLayer;
        require(editLayer->ExportToString(&undoneLayer), "capture setTransforms execute undo layer");
        require(undoneLayer == beforeLayer, "setTransforms undo restores exact authored xform state");

        for (qsizetype i = 0; i < paths.size(); ++i) {
            GfMatrix4d matrix(1.0);
            require(stageviz::stage::worldTransform(stage, paths.at(i), matrix, error),
                    "read setTransforms undone matrix");
            require(GfIsClose(matrix, edit.before.at(i), 1e-6),
                    "setTransforms undo restores original world matrix");
        }

        require(!editLayer->GetPropertyAtPath(secondPath.AppendProperty(TfToken("xformOp:transform"))),
                "setTransforms undo removes temporary matrix op");
        require(!editLayer->GetPropertyAtPath(secondPath.AppendProperty(TfToken("xformOpOrder"))),
                "setTransforms undo removes temporary xformOpOrder");

        executeCommand(setTransformCommand, transformSession);

        for (qsizetype i = 0; i < paths.size(); ++i) {
            GfMatrix4d matrix(1.0);
            require(stageviz::stage::worldTransform(stage, paths.at(i), matrix, error),
                    "read setTransforms redone matrix");
            require(GfIsClose(matrix, edit.after.at(i), 1e-6),
                    "setTransforms redo restores requested world matrix");
        }

        undoCommand(setTransformCommand, transformSession);

        std::string secondUndoLayer;
        require(editLayer->ExportToString(&secondUndoLayer), "capture setTransforms second undo layer");
        require(secondUndoLayer == beforeLayer, "setTransforms redo/undo remains exactly reversible");
    }

    // A pivot authored in the same active layer must remain the gizmo pivot
    // after prepareTransforms() replaces xformOpOrder with the matrix-only fast
    // path. Without this regression, worldPivot() falls back to the translated
    // matrix origin after a rotation and the gizmo jumps away/disappears.
    {
        Session pivotSession;
        require(pivotSession.newStage(), "new stage for same-layer pivot transform");
        const UsdStageRefPtr stage = pivotSession.stage();
        const SdfPath path("/Pivoted");
        UsdGeomXform xform = UsdGeomXform::Define(stage, path);
        require(bool(xform), "define same-layer pivot fixture");

        const GfVec3d localPivot(1729.29284668, -882.01162720, 748.30184936);
        const UsdGeomXformOp pivotOp
            = xform.AddTranslateOp(UsdGeomXformOp::PrecisionDouble, TfToken("pivot"), false);
        const UsdGeomXformOp matrixOp = xform.AddTransformOp(UsdGeomXformOp::PrecisionDouble);
        const UsdGeomXformOp inversePivotOp
            = xform.AddTranslateOp(UsdGeomXformOp::PrecisionDouble, TfToken("pivot"), true);
        require(bool(pivotOp) && bool(matrixOp) && bool(inversePivotOp),
                "author same-layer pivot transform ops");
        require(pivotOp.Set(localPivot), "author same-layer pivot value");
        require(matrixOp.Set(GfMatrix4d(1.0)), "author same-layer base matrix");

        QString error;
        GfVec3d pivotBefore;
        require(stageviz::stage::worldPivot(stage, path, pivotBefore, error),
                "read same-layer pivot before prepared transform");
        require(GfIsClose(pivotBefore, localPivot, 1e-6),
                "same-layer pivot starts at authored location");

        GfMatrix4d before(1.0);
        require(stageviz::stage::worldTransform(stage, path, before, error),
                "read same-layer pivot base world transform");

        stageviz::XformEdit edit;
        edit.before = { before };
        edit.beforeState = { captureXformState(stage->GetEditTarget().GetLayer(), path) };

        GfMatrix4d toOrigin(1.0);
        GfMatrix4d rotation(1.0);
        GfMatrix4d fromOrigin(1.0);
        toOrigin.SetTranslate(-pivotBefore);
        rotation.SetRotate(GfRotation(GfVec3d::ZAxis(), 37.0));
        fromOrigin.SetTranslate(pivotBefore);
        edit.delta = toOrigin * rotation * fromOrigin;
        edit.hasDelta = true;
        edit.after = { before * edit.delta };

        QStringList errors;
        const QList<stageviz::PreparedTransform> prepared
            = stageviz::prepareTransforms(stage, { path }, edit.before, &errors);
        require(errors.isEmpty() && prepared.size() == 1 && prepared.first().fast,
                "prepare same-layer pivot matrix fast path");

        VtTokenArray preparedOrder;
        require(xform.GetXformOpOrderAttr().Get(&preparedOrder)
                    && preparedOrder == VtTokenArray { TfToken("xformOp:transform") },
                "prepared same-layer pivot uses matrix-only order");
        require(bool(stage->GetPrimAtPath(path).GetAttribute(TfToken("xformOp:translate:pivot"))),
                "prepared same-layer pivot keeps authored pivot attribute");

        require(stageviz::applyPreparedTransforms(stage, prepared, edit.after, edit.delta, true, &errors),
                "apply same-layer pivot rotation preview");
        require(errors.isEmpty(), "same-layer pivot preview has no errors");

        GfVec3d pivotAfterPreview;
        require(stageviz::stage::worldPivot(stage, path, pivotAfterPreview, error),
                "read same-layer pivot after prepared rotation");
        require(GfIsClose(pivotAfterPreview, pivotBefore, 1e-6),
                "same-layer pivot remains stable after prepared rotation");

        GfMatrix4d previewWorld(1.0);
        require(stageviz::stage::worldTransform(stage, path, previewWorld, error),
                "read same-layer pivot preview world transform");
        require(GfIsClose(previewWorld, edit.after.first(), 1e-6),
                "same-layer pivot preview reaches requested world transform");

        stageviz::Command pivotCommand = stageviz::setTransforms({ path }, edit);
        undoCommand(pivotCommand, pivotSession);

        GfVec3d pivotAfterUndo;
        require(stageviz::stage::worldPivot(stage, path, pivotAfterUndo, error),
                "read same-layer pivot after undo");
        require(GfIsClose(pivotAfterUndo, pivotBefore, 1e-6),
                "same-layer pivot restored after undo");

        executeCommand(pivotCommand, pivotSession);

        GfVec3d pivotAfterRedo;
        require(stageviz::stage::worldPivot(stage, path, pivotAfterRedo, error),
                "read same-layer pivot after redo");
        require(GfIsClose(pivotAfterRedo, pivotBefore, 1e-6),
                "same-layer pivot remains stable after redo");
    }

    // The interactive prepared path and the command redo path must author the
    // same final USD state, including selected parent/child transforms.
    {
        Session xformSession;
        require(xformSession.newStage(), "new stage for XformEdit");
        const UsdStageRefPtr stage = xformSession.stage();
        const SdfPath parentPath("/Parent");
        const SdfPath childPath("/Parent/Child");
        UsdGeomXform parent = UsdGeomXform::Define(stage, parentPath);
        UsdGeomXform child = UsdGeomXform::Define(stage, childPath);
        require(bool(parent) && bool(child), "define XformEdit fixture");

        parent.AddTransformOp().Set(GfMatrix4d(1.0));
        GfMatrix4d childLocal(1.0);
        childLocal.SetTranslate(GfVec3d(1.0, 2.0, 3.0));
        child.AddTransformOp().Set(childLocal);

        const QList<SdfPath> paths { parentPath, childPath };
        stageviz::XformEdit edit;
        QString error;
        for (const SdfPath& path : paths) {
            GfMatrix4d matrix(1.0);
            require(stageviz::stage::worldTransform(stage, path, matrix, error), "capture XformEdit before matrix");
            edit.before.append(matrix);
            edit.beforeState.append(captureXformState(stage->GetEditTarget().GetLayer(), path));
        }

        edit.delta.SetTranslate(GfVec3d(4.0, 5.0, 6.0));
        edit.hasDelta = true;
        edit.after = edit.before;
        for (qsizetype i = 0; i < edit.after.size(); ++i)
            edit.after[i] = edit.before.at(i) * edit.delta;

        std::string beforeLayer;
        require(stage->GetEditTarget().GetLayer()->ExportToString(&beforeLayer), "capture XformEdit before layer");

        QStringList prepareErrors;
        const QList<stageviz::PreparedTransform> prepared = stageviz::prepareTransforms(stage, paths, edit.before,
                                                                                        &prepareErrors);
        require(prepareErrors.isEmpty() && prepared.size() == paths.size(), "prepare shared XformEdit path");
        require(prepared.at(0).fast && prepared.at(1).fast, "prepared matrix xformOps use fast path");
        require(prepared.at(1).parentFollowsSelection, "prepared child tracks selected parent");

        QStringList applyErrors;
        QList<SdfPath> changed;
        require(stageviz::applyPreparedTransforms(stage, prepared, edit.after, edit.delta, edit.hasDelta, &applyErrors,
                                                  &changed),
                "apply shared XformEdit preview");
        require(applyErrors.isEmpty() && changed.size() == paths.size(), "shared XformEdit reports changed paths");

        std::string previewLayer;
        require(stage->GetEditTarget().GetLayer()->ExportToString(&previewLayer), "capture XformEdit preview layer");

        stageviz::Command xformCommand = stageviz::setTransforms(paths, edit);

        // The live gizmo has already authored previewLayer. An Applied history
        // entry must therefore undo from this state without re-executing first.
        undoCommand(xformCommand, xformSession);
        std::string undoneLayer;
        require(stage->GetEditTarget().GetLayer()->ExportToString(&undoneLayer), "capture XformEdit undo layer");
        require(undoneLayer == beforeLayer, "XformEdit undo restores exact authored state");

        executeCommand(xformCommand, xformSession);
        std::string redoneLayer;
        require(stage->GetEditTarget().GetLayer()->ExportToString(&redoneLayer), "capture XformEdit redo layer");
        require(redoneLayer == previewLayer, "XformEdit redo matches live preview authoring exactly");

        for (qsizetype i = 0; i < paths.size(); ++i) {
            GfMatrix4d matrix(1.0);
            require(stageviz::stage::worldTransform(stage, paths.at(i), matrix, error), "read XformEdit redone matrix");
            require(GfIsClose(matrix, edit.after.at(i), 1e-6), "XformEdit redo restores final world matrix");
        }
    }

    // Commands created without an interactive delta, such as the Python API,
    // must still handle selected parent/child paths correctly by falling back
    // to the canonical world-transform path where required.
    {
        Session commandSession;
        require(commandSession.newStage(), "new stage for XformEdit canonical fallback");
        const UsdStageRefPtr stage = commandSession.stage();
        const SdfPath parentPath("/FallbackParent");
        const SdfPath childPath("/FallbackParent/Child");
        UsdGeomXform::Define(stage, parentPath).AddTransformOp().Set(GfMatrix4d(1.0));
        GfMatrix4d childLocal(1.0);
        childLocal.SetTranslate(GfVec3d(1.0, 0.0, 0.0));
        UsdGeomXform::Define(stage, childPath).AddTransformOp().Set(childLocal);

        const QList<SdfPath> paths { parentPath, childPath };
        stageviz::XformEdit edit;
        QString error;
        for (const SdfPath& path : paths) {
            GfMatrix4d matrix(1.0);
            require(stageviz::stage::worldTransform(stage, path, matrix, error), "capture fallback before matrix");
            edit.before.append(matrix);
        }
        edit.after = edit.before;
        edit.after[0].SetTranslateOnly(GfVec3d(3.0, 0.0, 0.0));
        edit.after[1].SetTranslateOnly(GfVec3d(8.0, 0.0, 0.0));
        require(!edit.hasDelta, "canonical fallback fixture has no interaction delta");

        stageviz::Command fallbackCommand = stageviz::setTransforms(paths, edit);
        executeCommand(fallbackCommand, commandSession);
        for (qsizetype i = 0; i < paths.size(); ++i) {
            GfMatrix4d matrix(1.0);
            require(stageviz::stage::worldTransform(stage, paths.at(i), matrix, error),
                    "read fallback transformed matrix");
            require(GfIsClose(matrix, edit.after.at(i), 1e-6),
                    "XformEdit without delta reaches requested world matrix");
        }

        undoCommand(fallbackCommand, commandSession);
        for (qsizetype i = 0; i < paths.size(); ++i) {
            GfMatrix4d matrix(1.0);
            require(stageviz::stage::worldTransform(stage, paths.at(i), matrix, error), "read fallback undone matrix");
            require(GfIsClose(matrix, edit.before.at(i), 1e-6), "XformEdit fallback undo restores world matrix");
        }
    }

    // XformState must capture and restore the active edit layer rather than the
    // root layer. Undo must also remove an inert prim spec created only by the
    // interactive transform.
    {
        Session layerSession;
        require(layerSession.newStage(), "new stage for XformState edit-layer test");
        const UsdStageRefPtr stage = layerSession.stage();
        const SdfPath path("/Layered");

        // Model the normal override-layer workflow correctly: the active edit
        // layer must be stronger than the layer containing the source transform.
        // A root-layer opinion would be stronger than every sublayer and could
        // not be overridden by authoring into a sublayer.
        const SdfLayerRefPtr editLayer = SdfLayer::CreateAnonymous("xform_edit_layer.usda");
        const SdfLayerRefPtr sourceLayer = SdfLayer::CreateAnonymous("xform_source_layer.usda");
        require(bool(editLayer) && bool(sourceLayer), "create XformState layer stack");
        stage->GetRootLayer()->SetSubLayerPaths({ editLayer->GetIdentifier(), sourceLayer->GetIdentifier() });

        require(layerSession.setEditLayer(sourceLayer), "activate XformState source layer");
        UsdGeomXform::Define(stage, path).AddTranslateOp().Set(GfVec3d(1.0, 0.0, 0.0));
        require(bool(sourceLayer->GetPrimAtPath(path)), "source transform authored in weaker layer");

        require(layerSession.setEditLayer(editLayer), "activate XformState edit layer");
        require(stage->GetEditTarget().GetLayer() == editLayer, "XformState uses active edit layer");

        const QList<SdfPath> paths { path };
        stageviz::XformEdit edit;
        GfMatrix4d before(1.0);
        QString error;
        require(stageviz::stage::worldTransform(stage, path, before, error), "capture layered before transform");
        edit.before = { before };
        edit.beforeState = { captureXformState(editLayer, path) };
        GfMatrix4d delta(1.0);
        delta.SetTranslate(GfVec3d(2.0, 0.0, 0.0));
        edit.delta = delta;
        edit.hasDelta = true;
        edit.after = { before * delta };

        std::string beforeLayer;
        require(editLayer->ExportToString(&beforeLayer), "capture empty XformState edit layer");
        std::string beforeRootLayer;
        require(stage->GetRootLayer()->ExportToString(&beforeRootLayer), "capture layered root layer");
        std::string beforeSourceLayer;
        require(sourceLayer->ExportToString(&beforeSourceLayer), "capture layered source transform");

        QStringList errors;
        const QList<stageviz::PreparedTransform> prepared = stageviz::prepareTransforms(stage, paths, edit.before,
                                                                                        &errors);
        require(errors.isEmpty(), "prepare layered XformEdit");
        require(prepared.size() == 1 && prepared.first().fast && bool(prepared.first().matrixOp),
                "layered XformEdit prepares active-layer matrix fast path");
        require(stageviz::applyPreparedTransforms(stage, prepared, edit.after, edit.delta, true, &errors),
                "apply layered XformEdit preview");
        require(errors.isEmpty() && bool(editLayer->GetPrimAtPath(path)), "preview authors active edit layer");
        require(bool(editLayer->GetPropertyAtPath(path.AppendProperty(TfToken("xformOp:transform"))))
                    && bool(editLayer->GetPropertyAtPath(path.AppendProperty(TfToken("xformOpOrder")))),
                "layered preview authors matrix and order overrides");

        std::string previewRootLayer;
        require(stage->GetRootLayer()->ExportToString(&previewRootLayer), "capture root after layered preview");
        require(previewRootLayer == beforeRootLayer, "layered preview leaves root layer untouched");
        std::string previewSourceLayer;
        require(sourceLayer->ExportToString(&previewSourceLayer), "capture source after layered preview");
        require(previewSourceLayer == beforeSourceLayer, "layered preview leaves weaker source transform untouched");

        GfMatrix4d previewWorld(1.0);
        require(stageviz::stage::worldTransform(stage, path, previewWorld, error), "read layered preview transform");
        require(GfIsClose(previewWorld, edit.after.first(), 1e-6), "layered preview reaches requested world transform");

        stageviz::Command layeredCommand = stageviz::setTransforms(paths, edit);
        undoCommand(layeredCommand, layerSession);

        std::string undoneLayer;
        require(editLayer->ExportToString(&undoneLayer), "capture layered XformEdit undo");
        require(undoneLayer == beforeLayer, "XformState undo restores active edit layer exactly");
        require(!editLayer->GetPrimAtPath(path), "XformState undo removes temporary inert prim spec");

        std::string undoRootLayer;
        require(stage->GetRootLayer()->ExportToString(&undoRootLayer), "capture root after layered undo");
        require(undoRootLayer == beforeRootLayer, "layered undo leaves root layer untouched");
        std::string undoSourceLayer;
        require(sourceLayer->ExportToString(&undoSourceLayer), "capture source after layered undo");
        require(undoSourceLayer == beforeSourceLayer, "layered undo leaves weaker source transform untouched");

        executeCommand(layeredCommand, layerSession);

        GfMatrix4d redoneWorld(1.0);
        require(stageviz::stage::worldTransform(stage, path, redoneWorld, error), "read layered redo transform");
        require(GfIsClose(redoneWorld, edit.after.first(), 1e-6), "layered redo reaches requested world transform");

        std::string redoRootLayer;
        require(stage->GetRootLayer()->ExportToString(&redoRootLayer), "capture root after layered redo");
        require(redoRootLayer == beforeRootLayer, "layered redo leaves root layer untouched");
        std::string redoSourceLayer;
        require(sourceLayer->ExportToString(&redoSourceLayer), "capture source after layered redo");
        require(redoSourceLayer == beforeSourceLayer, "layered redo leaves weaker source transform untouched");

        undoCommand(layeredCommand, layerSession);
        std::string secondUndoLayer;
        require(editLayer->ExportToString(&secondUndoLayer), "capture layered second undo");
        require(secondUndoLayer == beforeLayer, "layered redo/undo remains exactly reversible");
    }
}

void
commandPayloadApi()
{
    QTemporaryDir dir;
    require(dir.isValid(), "temporary directory");
    const QString assetPath = makeAssetFixture(dir.filePath("payload.usda"));

    Session session;
    require(session.newStage(), "new stage");
    const UsdPrim payloadPrim = session.stage()->DefinePrim(SdfPath("/Payload"));
    require(payloadPrim.GetPayloads().AddPayload(assetPath.toStdString()), "author payload fixture");
    require(payloadPrim.IsLoaded(), "payload fixture starts loaded");

    session.selectionList()->updatePaths({ SdfPath("/Payload/Geometry") });
    stageviz::Command select = stageviz::selectPayload();
    executeCommand(select, session);
    require(session.selectionList()->paths() == QList<SdfPath>({ SdfPath("/Payload") }),
            "selectPayload resolves descendant");
    undoCommand(select, session);
    require(session.selectionList()->paths() == QList<SdfPath>({ SdfPath("/Payload/Geometry") }), "selectPayload undo");

    stageviz::Command unload = stageviz::unloadPayloads({ SdfPath("/Payload") });
    executeCommand(unload, session);
    require(!session.stage()->GetPrimAtPath(SdfPath("/Payload")).IsLoaded(), "unloadPayloads command");
    undoCommand(unload, session);
    require(session.stage()->GetPrimAtPath(SdfPath("/Payload")).IsLoaded(), "unloadPayloads undo");

    session.stage()->Unload(SdfPath("/Payload"));
    stageviz::Command load = stageviz::loadPayloads({ SdfPath("/Payload") });
    executeCommand(load, session);
    require(session.stage()->GetPrimAtPath(SdfPath("/Payload")).IsLoaded(), "loadPayloads command");
    undoCommand(load, session);
    require(!session.stage()->GetPrimAtPath(SdfPath("/Payload")).IsLoaded(), "loadPayloads undo");
}

void
geometrySubsetApi()
{
    Session session;
    require(session.newStage(), "subset stage created");
    const auto stage = session.stage();
    const SdfPath meshPath("/Mesh");
    const UsdGeomMesh mesh = UsdGeomMesh::Define(stage, meshPath);
    require(bool(mesh), "subset mesh defined");
    mesh.CreateFaceVertexCountsAttr().Set(VtArray<int> { 3, 3 });
    mesh.CreateFaceVertexIndicesAttr().Set(VtArray<int> { 0, 1, 2, 0, 2, 3 });
    mesh.CreatePointsAttr().Set(VtArray<GfVec3f> {
        GfVec3f(0, 0, 0), GfVec3f(1, 0, 0), GfVec3f(1, 1, 0), GfVec3f(0, 1, 0)
    });

    const TfToken family("materialBind");
    const auto left = UsdGeomSubset::CreateGeomSubset(mesh, TfToken("Left"),
        UsdGeomTokens->face, VtArray<int> { 0 }, family);
    const auto right = UsdGeomSubset::CreateGeomSubset(mesh, TfToken("Right"),
        UsdGeomTokens->face, VtArray<int> { 1 }, family);
    require(bool(left) && bool(right), "two face subsets created");
    UsdGeomSubset::SetFamilyType(mesh, family, UsdGeomTokens->partition);
    std::string validationReason;
    require(UsdGeomSubset::ValidateFamily(mesh, UsdGeomTokens->face, family, &validationReason),
        "complete subset partition valid");

    const SdfPath red = stageviz::MaterialUtils::createPreviewSurfaceMaterial(stage);
    const SdfPath blue = stageviz::MaterialUtils::createPreviewSurfaceMaterial(stage);
    require(red != blue && !red.IsEmpty() && !blue.IsEmpty(), "two subset materials created");
    require(UsdShadeMaterialBindingAPI::Apply(left.GetPrim()).Bind(UsdShadeMaterial(stage->GetPrimAtPath(red))),
        "bind left subset");
    require(UsdShadeMaterialBindingAPI::Apply(right.GetPrim()).Bind(UsdShadeMaterial(stage->GetPrimAtPath(blue))),
        "bind right subset");
    require(UsdShadeMaterialBindingAPI(left.GetPrim()).ComputeBoundMaterial().GetPath() == red,
        "left subset resolves red material");
    require(UsdShadeMaterialBindingAPI(right.GetPrim()).ComputeBoundMaterial().GetPath() == blue,
        "right subset resolves blue material");

    const auto subsets = UsdGeomSubset::GetGeomSubsets(mesh, UsdGeomTokens->face, family);
    require(subsets.size() == 2, "materialBind family enumerates both subsets");
    VtArray<int> indices;
    require(left.GetIndicesAttr().Get(&indices) && indices == VtArray<int> { 0 },
        "left face index retained");
    require(right.GetIndicesAttr().Get(&indices) && indices == VtArray<int> { 1 },
        "right face index retained");

    std::string layerText;
    require(stage->GetRootLayer()->ExportToString(&layerText), "subset layer exported");
    const auto reopenedLayer = SdfLayer::CreateAnonymous("subset.usda");
    require(reopenedLayer->ImportFromString(layerText),
        "subset layer roundtrip");
    const auto reopened = UsdStage::Open(reopenedLayer);
    require(bool(reopened) && bool(reopened->GetPrimAtPath(meshPath.AppendChild(TfToken("Left")))),
        "subset persists after reopening layer");
    require(UsdShadeMaterialBindingAPI(reopened->GetPrimAtPath(meshPath.AppendChild(TfToken("Right"))))
        .ComputeBoundMaterial().GetPath() == blue, "subset binding persists after reopening layer");
}

}  // namespace

int
main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    const std::map<std::string, std::function<void()>> cases {
        { "save_twice_usda", [] { saveTwice("usda"); } },
        { "save_twice_usdc", [] { saveTwice("usdc"); } },
        { "save_failure", saveFailure },
        { "sublayer_save", sublayerSave },
        { "save_as", saveAs },
        { "save_as_payload_anchor", saveAsPayloadAnchor },
        { "corrupt_state", corruptState },
        { "payload_state", [] { payloadState(false); } },
        { "legacy_payload_state", [] { payloadState(true); } },
        { "reparent_dependencies", reparentDependencies },
        { "rename_dependencies", renameDependencies },
        { "reparent_collision", reparentCollision },
        { "reparent_into_descendant", reparentIntoDescendant },
        { "reparent_batch", reparentBatch },
        { "namespace_load_rules", namespaceLoadRules },
        { "namespace_load_rules_batch", namespaceLoadRulesBatch },
        { "namespace_load_rules_batch_failure", namespaceLoadRulesBatchFailure },
        { "namespace_load_rules_rename", namespaceLoadRulesRename },
        { "export_session_opinions", exportSessionOpinions },
        { "export_multiple_session_opinions", exportMultipleSessionOpinions },
        { "merge_asset_paths", mergeAssetPaths },
        { "merge_composition_asset_paths", mergeCompositionAssetPaths },
        { "selection_list_api", selectionListApi },
        { "view_camera_api", viewCameraApi },
        { "view_state_api", viewStateApi },
        { "session_core_api", sessionCoreApi },
        { "session_file_api", sessionFileApi },
        { "session_edit_layer_api", sessionEditLayerApi },
        { "session_state_api", sessionStateApi },
        { "session_load_policy_api", sessionLoadPolicyApi },
        { "command_merge_api", commandMergeApi },
        { "namespace_editor_crud_api", namespaceEditorCrudApi },
        { "usd_path_utils_api", usdPathUtilsApi },
        { "usd_stage_utils_api", usdStageUtilsApi },
        { "usd_payload_utils_api", usdPayloadUtilsApi },
        { "material_utils_api", materialUtilsApi },
        { "geometry_subset_api", geometrySubsetApi },
        { "material_menu_api", materialMenuApi },
        { "command_material_api", commandMaterialApi },
        { "command_selection_api", commandSelectionApi },
        { "command_authoring_api", commandAuthoringApi },
        { "command_generated_geometry_api", commandGeneratedGeometryApi },
        { "command_property_api", commandPropertyApi },
        { "command_visibility_stage_api", commandVisibilityStageApi },
        { "command_material_variant_api", commandMaterialVariantApi },
        { "command_composition_api", commandCompositionApi },
        { "command_move_api", commandMoveApi },
        { "command_payload_api", commandPayloadApi }
    };
    if (argc != 2 || !cases.count(argv[1])) {
        std::cerr << "Pass one registered regression case name\n";
        return 2;
    }
    try {
        cases.at(argv[1])();
    } catch (const std::exception& error) {
        std::cerr << argv[1] << ": " << error.what() << '\n';
        return 1;
    } catch (...) {
        std::cerr << argv[1] << ": unknown exception\n";
        return 1;
    }

    return 0;
}
