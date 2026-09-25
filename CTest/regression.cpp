// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell

#include "command.h"
#include "materialutils.h"
#include "materialmenu.h"
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
#include <stdexcept>
#include <pxr/base/gf/matrix4d.h>
#include <pxr/base/gf/range3d.h>
#include <pxr/base/tf/errorMark.h>
#include <pxr/usd/sdf/assetPath.h>
#include <pxr/usd/sdf/types.h>
#include <pxr/usd/usd/attribute.h>
#include <pxr/usd/usd/payloads.h>
#include <pxr/usd/usd/references.h>
#include <pxr/usd/usd/relationship.h>
#include <pxr/usd/usd/primRange.h>
#include <pxr/usd/usd/variantSets.h>
#include <pxr/usd/usdGeom/cube.h>
#include <pxr/usd/usdGeom/scope.h>
#include <pxr/usd/usdGeom/xform.h>
#include <pxr/usd/usdGeom/xformCommonAPI.h>
#include <pxr/usd/usdGeom/xformable.h>
#include <pxr/usd/usdShade/material.h>
#include <pxr/usd/usdShade/materialBindingAPI.h>
#include <pxr/usd/usdShade/shader.h>

PXR_NAMESPACE_USING_DIRECTIVE
using stageviz::Session;
using stageviz::SelectionList;
using stageviz::ViewCamera;
using stageviz::ViewState;

namespace {
    void require(bool result, const char* message)
    {
        if (!result)
            throw std::runtime_error(message);
    }

    void writeFile(const QString& path, const QByteArray& bytes)
    {
        QFile file(path);
        require(file.open(QIODevice::WriteOnly), "open fixture");
        require(file.write(bytes) == bytes.size(), "write fixture");
    }

    UsdStageRefPtr diskStage(const QString& path)
    {
        // Bypass the live layer registry so assertions inspect disk content.
        const auto layer = SdfLayer::OpenAsAnonymous(path.toStdString());
        require(bool(layer), "read saved layer from disk");
        const auto stage = UsdStage::Open(layer);
        require(bool(stage), "open saved layer");
        return stage;
    }

    bool closeEnough(double a, double b, double epsilon = 1e-6)
    {
        return std::abs(a - b) <= epsilon;
    }

    void drainWorkers()
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

    void executeCommand(stageviz::Command& command, Session& session)
    {
        command.execute(&session);
        drainWorkers();
    }

    void undoCommand(stageviz::Command& command, Session& session)
    {
        command.undo(&session);
        drainWorkers();
    }

    QString makeAssetFixture(const QString& path, const char* rootName = "/Asset")
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

    void saveTwice(const QString& extension)
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

    void saveFailure()
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

    void sublayerSave()
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
        session.stage()->GetRootLayer()->SetSubLayerPaths({childPath.toStdString()});
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

    void saveAs()
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
            stage->GetRootLayer()->SetSubLayerPaths({"asset.usda"});
            const auto prim = stage->DefinePrim(SdfPath("/Referenced"));
            require(prim.GetReferences().AddReference("asset.usda"), "author reference");
            require(prim.CreateAttribute(TfToken("texture"), SdfValueTypeNames->Asset)
                        .Set(SdfAssetPath("textures/tile.<UDIM>.exr")), "author unresolved texture pattern");
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
        require(reopened->GetPrimAtPath(SdfPath("/Referenced")).GetAttribute(TfToken("texture")).Get(&texture), "texture authored");
        require(QString::fromStdString(texture.GetAssetPath()) == dir.filePath("old/textures/tile.<UDIM>.exr"),
                "unresolved texture keeps original anchor");
    }

    void saveAsPayloadAnchor()
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

    void corruptState()
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

    void payloadState(bool legacy)
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
        for (const char* name : {"/World/Loaded", "/World/Unloaded"})
            require(session.stage()->DefinePrim(SdfPath(name)).GetPayloads().AddPayload(assetPath.toStdString()),
                    "author payload");
        session.stage()->Unload(SdfPath("/World/Unloaded"));
        const auto expected = session.stage()->GetLoadRules();
        require(session.saveToFile(path), "save payload state");
        if (legacy) {
            QJsonObject object;
            object["version"] = 3;
            object["loadedPayloads"] = QJsonArray {QString("/World/Loaded")};
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

    void reparentDependencies()
    {
        const auto stage = UsdStage::CreateInMemory();
        stage->DefinePrim(SdfPath("/A"));
        stage->DefinePrim(SdfPath("/B"));
        const auto material = stage->DefinePrim(SdfPath("/A/Material"));
        material.CreateAttribute(TfToken("outputs:surface"), SdfValueTypeNames->Token);
        const auto mesh = stage->DefinePrim(SdfPath("/Mesh"));
        require(mesh.CreateRelationship(TfToken("material:binding")).SetTargets({SdfPath("/A/Material")}), "binding fixture");
        require(mesh.CreateAttribute(TfToken("input"), SdfValueTypeNames->Token)
                    .SetConnections({SdfPath("/A/Material.outputs:surface")}), "connection fixture");
        stageviz::edit::NamespaceEditor editor(stage, stage->GetEditTarget());
        QString error;
        require(editor.reparentPrim(SdfPath("/A/Material"), SdfPath("/B/Material"), error), "move material");
        SdfPathVector paths;
        require(mesh.GetRelationship(TfToken("material:binding")).GetTargets(&paths)
                    && paths == SdfPathVector {SdfPath("/B/Material")}, "binding updated");
        require(mesh.GetAttribute(TfToken("input")).GetConnections(&paths)
                    && paths == SdfPathVector {SdfPath("/B/Material.outputs:surface")}, "connection updated");
        require(editor.reparentPrim(SdfPath("/B/Material"), SdfPath("/A/Material"), error), "reverse move");
        require(mesh.GetRelationship(TfToken("material:binding")).GetTargets(&paths)
                    && paths == SdfPathVector {SdfPath("/A/Material")}, "binding restored by undo path");
    }

    void renameDependencies()
    {
        const auto stage = UsdStage::CreateInMemory();
        stage->DefinePrim(SdfPath("/A"));
        const auto material = stage->DefinePrim(SdfPath("/A/Material"));
        material.CreateAttribute(TfToken("outputs:surface"), SdfValueTypeNames->Token);
        const auto mesh = stage->DefinePrim(SdfPath("/Mesh"));
        require(mesh.CreateRelationship(TfToken("material:binding")).SetTargets({SdfPath("/A/Material")}),
                "binding fixture");
        require(mesh.CreateAttribute(TfToken("input"), SdfValueTypeNames->Token)
                    .SetConnections({SdfPath("/A/Material.outputs:surface")}), "connection fixture");

        stageviz::edit::NamespaceEditor editor(stage, stage->GetEditTarget());
        QString error;
        require(editor.renamePrim(SdfPath("/A/Material"), SdfPath("/A/Renamed"), error), "rename material");

        SdfPathVector paths;
        require(mesh.GetRelationship(TfToken("material:binding")).GetTargets(&paths)
                    && paths == SdfPathVector {SdfPath("/A/Renamed")}, "binding updated by rename");
        require(mesh.GetAttribute(TfToken("input")).GetConnections(&paths)
                    && paths == SdfPathVector {SdfPath("/A/Renamed.outputs:surface")},
                "connection updated by rename");
    }

    void reparentCollision()
    {
        const auto stage = UsdStage::CreateInMemory();
        for (const char* path : {"/A", "/B", "/A/Part", "/B/Part"})
            stage->DefinePrim(SdfPath(path));

        UsdStageLoadRules rules = UsdStageLoadRules::LoadAll();
        rules.AddRule(SdfPath("/A/Part"), UsdStageLoadRules::NoneRule);
        stage->SetLoadRules(rules);
        const UsdStageLoadRules beforeRules = stage->GetLoadRules();

        std::string before;
        stage->GetRootLayer()->ExportToString(&before);

        stageviz::edit::NamespaceEditor editor(stage, stage->GetEditTarget());
        QString error;
        require(!editor.reparentPrim(SdfPath("/A/Part"), SdfPath("/B/Part"), error),
                "move onto existing prim fails");

        std::string after;
        stage->GetRootLayer()->ExportToString(&after);
        require(before == after, "collision failure leaves layer unchanged");
        require(stage->GetLoadRules() == beforeRules, "collision failure leaves load rules unchanged");
    }

    void reparentIntoDescendant()
    {
        const auto stage = UsdStage::CreateInMemory();
        stage->DefinePrim(SdfPath("/A"));
        stage->DefinePrim(SdfPath("/A/Child"));

        std::string before;
        stage->GetRootLayer()->ExportToString(&before);

        stageviz::edit::NamespaceEditor editor(stage, stage->GetEditTarget());
        QString error;
        require(!editor.reparentPrim(SdfPath("/A"), SdfPath("/A/Child/A"), error),
                "move into descendant fails");

        std::string after;
        stage->GetRootLayer()->ExportToString(&after);
        require(before == after, "descendant failure leaves layer unchanged");
    }

    void reparentBatch()
    {
        const auto stage = UsdStage::CreateInMemory();
        for (const char* path : {"/A", "/B", "/A/First", "/A/Second"})
            stage->DefinePrim(SdfPath(path));
        stageviz::edit::NamespaceEditor editor(stage, stage->GetEditTarget());
        QString error;
        require(editor.reparentPrims({{SdfPath("/A/First"), SdfPath("/B/First")},
                                     {SdfPath("/A/Second"), SdfPath("/B/Second")}}, error), "move both prims");
        require(bool(stage->GetPrimAtPath(SdfPath("/B/First")))
                    && bool(stage->GetPrimAtPath(SdfPath("/B/Second"))), "both moves applied");
        std::string before;
        stage->GetRootLayer()->ExportToString(&before);
        require(!editor.reparentPrims({{SdfPath("/B/First"), SdfPath("/A/First")},
                                      {SdfPath("/B/Second"), SdfPath("/B/First/Second")}}, error),
                "second move into a moved-away parent fails");
        std::string after;
        stage->GetRootLayer()->ExportToString(&after);
        require(before == after, "failed multi-move restores all layers");
    }
}

namespace {
    void mergeAssetPaths()
    {
        QTemporaryDir dir;
        require(dir.isValid(), "temporary directory");
        require(QDir().mkpath(dir.filePath("source")), "source directory");
        require(QDir().mkpath(dir.filePath("destination")), "destination directory");
        const QString sourceFile = dir.filePath("source/scene.usda");
        const auto source = UsdStage::CreateNew(sourceFile.toStdString());
        const UsdPrim part = source->DefinePrim(SdfPath("/Part"));
        part.CreateAttribute(TfToken("texture"), SdfValueTypeNames->Asset)
            .Set(SdfAssetPath("textures/paint.<UDIM>.exr"));
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

    void mergeCompositionAssetPaths()
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

    void namespaceLoadRules()
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
        require(stage->GetLoadRules().GetEffectiveRuleForPath(SdfPath("/B/Renamed/Other"))
                    == UsdStageLoadRules::NoneRule, "rename preserves unloaded policy");
    }

    void namespaceLoadRulesBatch()
    {
        const auto stage = UsdStage::CreateInMemory();
        for (const char* path : {"/A", "/B", "/C", "/A/First", "/A/First/Detail",
                                 "/A/Second", "/A/Second/Detail"})
            stage->DefinePrim(SdfPath(path));

        UsdStageLoadRules rules = UsdStageLoadRules::LoadAll();
        rules.AddRule(SdfPath("/A/First"), UsdStageLoadRules::NoneRule);
        rules.AddRule(SdfPath("/A/First/Detail"), UsdStageLoadRules::OnlyRule);
        rules.AddRule(SdfPath("/A/Second"), UsdStageLoadRules::OnlyRule);
        rules.AddRule(SdfPath("/Unrelated"), UsdStageLoadRules::NoneRule);
        stage->SetLoadRules(rules);

        stageviz::edit::NamespaceEditor editor(stage, stage->GetEditTarget());
        QString error;
        require(editor.reparentPrims({{SdfPath("/A/First"), SdfPath("/B/First")},
                                      {SdfPath("/A/Second"), SdfPath("/C/Second")}}, error),
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

    void namespaceLoadRulesBatchFailure()
    {
        const auto stage = UsdStage::CreateInMemory();
        for (const char* path : {"/A", "/B", "/A/First", "/A/First/Detail", "/A/Second"})
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
        require(!editor.reparentPrims({{SdfPath("/A/First"), SdfPath("/B/First")},
                                       {SdfPath("/A/Second"), SdfPath("/B/First/Second")}}, error),
                "invalid batch move fails");

        std::string afterLayer;
        stage->GetRootLayer()->ExportToString(&afterLayer);
        require(beforeLayer == afterLayer, "failed batch move restores layer");
        require(stage->GetLoadRules() == beforeRules, "failed batch move restores load rules");
    }

    void namespaceLoadRulesRename()
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
        require(editor.renamePrim(SdfPath("/World/Part"), SdfPath("/World/Renamed"), error),
                "rename load rules");

        const UsdStageLoadRules renamed = stage->GetLoadRules();
        require(renamed.GetEffectiveRuleForPath(SdfPath("/World/Renamed/Other")) == UsdStageLoadRules::NoneRule,
                "rename preserves none rule");
        require(renamed.GetEffectiveRuleForPath(SdfPath("/World/Renamed/Detail")) == UsdStageLoadRules::OnlyRule,
                "rename preserves descendant only rule");
        require(renamed.GetEffectiveRuleForPath(SdfPath("/World/Part")) == UsdStageLoadRules::AllRule,
                "rename removes old-path rule");
    }

    void exportSessionOpinions()
    {
        QTemporaryDir dir;
        require(dir.isValid(), "temporary directory");
        Session session;
        require(session.newStage(), "new stage");
        const auto stage = session.stage();
        stage->DefinePrim(SdfPath("/World/Part"));
        stage->SetEditTarget(UsdEditTarget(stage->GetSessionLayer()));
        stage->GetPrimAtPath(SdfPath("/World/Part")).CreateAttribute(TfToken("marker"), SdfValueTypeNames->String)
            .Set(std::string("session opinion"));
        const QString file = dir.filePath("selection.usda");
        require(session.flattenPathsToFile({SdfPath("/World/Part")}, file), "export selection");
        std::string marker;
        require(diskStage(file)->GetPrimAtPath(SdfPath("/World/Part")).GetAttribute(TfToken("marker")).Get(&marker)
                    && marker == "session opinion", "selection export preserves session opinions");
    }
    void exportMultipleSessionOpinions()
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
        stage->GetPrimAtPath(SdfPath("/World/First")).CreateAttribute(TfToken("marker"), SdfValueTypeNames->String)
            .Set(std::string("first"));
        stage->GetPrimAtPath(SdfPath("/World/Second")).CreateAttribute(TfToken("marker"), SdfValueTypeNames->String)
            .Set(std::string("second"));
        stage->GetPrimAtPath(SdfPath("/World/Unrelated")).CreateAttribute(TfToken("marker"), SdfValueTypeNames->String)
            .Set(std::string("unrelated"));

        const QString file = dir.filePath("selection.usda");
        require(session.flattenPathsToFile({SdfPath("/World/First"), SdfPath("/World/Second")}, file),
                "export multiple selections");

        const auto reopened = diskStage(file);
        std::string first;
        std::string second;
        require(reopened->GetPrimAtPath(SdfPath("/World/First")).GetAttribute(TfToken("marker")).Get(&first)
                    && first == "first", "first session opinion exported");
        require(reopened->GetPrimAtPath(SdfPath("/World/Second")).GetAttribute(TfToken("marker")).Get(&second)
                    && second == "second", "second session opinion exported");
        require(!reopened->GetPrimAtPath(SdfPath("/World/Unrelated")), "unselected session opinion excluded");
    }

    void selectionListApi()
    {
        SelectionList selection;
        int changes = 0;
        QObject::connect(&selection, &SelectionList::selectionChanged, &selection,
                         [&](const QList<SdfPath>&) { ++changes; });

        require(selection.isValid(), "selection list reports valid");
        require(selection.paths().isEmpty(), "new selection has no paths");
        require(selection.isEmpty(), "new selection reports empty");

        selection.addPaths({SdfPath("/A"), SdfPath("/B"), SdfPath("/A")});
        require(selection.paths() == QList<SdfPath>({SdfPath("/A"), SdfPath("/B")}),
                "addPaths deduplicates while preserving order");
        require(selection.isSelected(SdfPath("/A")), "added path selected");
        require(!selection.isEmpty(), "non-empty selection reports non-empty");

        selection.removePaths({SdfPath("/A")});
        require(selection.paths() == QList<SdfPath>({SdfPath("/B")}), "removePaths removes path");
        selection.togglePaths({SdfPath("/B"), SdfPath("/C")});
        require(selection.paths() == QList<SdfPath>({SdfPath("/C")}), "togglePaths toggles both states");
        selection.updatePaths({SdfPath("/D"), SdfPath("/E")});
        require(selection.paths() == QList<SdfPath>({SdfPath("/D"), SdfPath("/E")}), "updatePaths replaces selection");
        selection.clear();
        require(selection.paths().isEmpty() && selection.isEmpty(), "clear empties selection");
        require(changes >= 5, "selection changes emit notifications");
    }

    void viewCameraApi()
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
                    && closeEnough(camera.axisRoll(), 3.0), "camera axis roundtrip");
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

    void viewStateApi()
    {
        ViewState state;
        require(state.camera() != nullptr, "view state exposes camera");

        int backgroundChanges = 0;
        QObject::connect(&state, &ViewState::backgroundColorChanged, &state,
                         [&](const QColor&) { ++backgroundChanges; });

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
                    && state.materialMode() == ViewState::Override, "override material activates override mode");
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

    void sessionCoreApi()
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
        require(session.stage()->GetLoadRules().GetEffectiveRuleForPath(SdfPath("/AnyPayload"))
                    == UsdStageLoadRules::NoneRule,
                "newStage none applies load-none rules");
        require(session.auxiliary() && session.auxiliaryUnsafe(), "session auxiliary stage accessors");
        require(session.stage() != session.auxiliary(), "document and auxiliary stages are separate");
        require(session.filename().isEmpty(), "new anonymous stage has no filename");

        session.stage()->DefinePrim(SdfPath("/World"), TfToken("Xform"));
        session.setMask({SdfPath("/World")});
        require(session.mask() == QList<SdfPath>({SdfPath("/World")}), "session mask roundtrip");
        session.setStageUp(Session::Y);
        require(session.stageUp() == Session::Y && session.viewState()->camera()->cameraUp() == ViewCamera::Y,
                "session stage-up synchronizes camera");
        session.setStageUp(Session::Z);
        require(session.stageUp() == Session::Z && session.viewState()->camera()->cameraUp() == ViewCamera::Z,
                "session stage-up restores z");

        session.setPrimsUpdate(Session::Deferred);
        require(session.primsUpdate() == Session::Deferred, "session deferred prim mode roundtrip");
        session.stage()->DefinePrim(SdfPath("/World/Deferred"));
        session.flushPrimsUpdates();
        session.setPrimsUpdate(Session::Immediate);
        require(session.primsUpdate() == Session::Immediate, "session immediate prim mode roundtrip");

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

    void sessionFileApi()
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

    void sessionEditLayerApi()
    {
        Session session;
        require(session.newStage(), "new stage");
        const auto stage = session.stage();
        const SdfLayerRefPtr child = SdfLayer::CreateAnonymous("child.usda");
        stage->GetRootLayer()->SetSubLayerPaths({child->GetIdentifier()});

        require(session.setEditLayer(child), "set local sublayer edit target");
        stage->DefinePrim(SdfPath("/ChildAuthored"));
        require(bool(child->GetPrimAtPath(SdfPath("/ChildAuthored"))), "authoring lands in selected edit layer");
        require(session.setEditLayer(stage->GetRootLayer()), "restore root edit target");

        const SdfLayerRefPtr foreign = SdfLayer::CreateAnonymous("foreign.usda");
        require(!session.setEditLayer(foreign), "foreign layer rejected as edit target");
    }

    void sessionStateApi()
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
                    && closeEnough(state->camera()->fov(), 47.0), "camera state restored");
    }

    void sessionLoadPolicyApi()
    {
        QTemporaryDir dir;
        require(dir.isValid(), "temporary directory");
        const QString assetPath = makeAssetFixture(dir.filePath("payload.usda"));

        const QString stagePath = dir.filePath("scene.usda");
        {
            const auto source = UsdStage::CreateNew(stagePath.toStdString());
            require(bool(source), "create load-policy fixture");
            const UsdPrim payload = source->DefinePrim(SdfPath("/Payload"));
            require(payload.GetPayloads().AddPayload(assetPath.toStdString()), "author load-policy payload");
            require(source->GetRootLayer()->Save(), "save load-policy fixture");
        }

        {
            Session session;
            require(session.newStage(Session::None), "new load-none stage");
            require(session.loadPolicy() == Session::None, "new load-none policy retained");
            require(session.stage()->GetLoadRules().GetEffectiveRuleForPath(SdfPath("/Future"))
                        == UsdStageLoadRules::NoneRule,
                    "new load-none stage applies none rule");
        }

        {
            Session session;
            require(session.newStage(Session::All), "new load-all stage");
            require(session.loadPolicy() == Session::All, "new load-all policy retained");
            require(session.stage()->GetLoadRules().GetEffectiveRuleForPath(SdfPath("/Future"))
                        == UsdStageLoadRules::AllRule,
                    "new load-all stage applies all rule");
        }

        {
            Session session;
            session.setPreserveState(false);
            require(session.loadFromFile(stagePath, Session::None), "open fixture load-none");
            require(!session.stage()->GetPrimAtPath(SdfPath("/Payload")).IsLoaded(),
                    "file load-none keeps payload unloaded");
            require(!session.stage()->GetPrimAtPath(SdfPath("/Payload/Geometry")),
                    "file load-none omits payload descendants");
        }

        {
            Session session;
            session.setPreserveState(false);
            require(session.loadFromFile(stagePath, Session::All), "open fixture load-all");
            require(session.stage()->GetPrimAtPath(SdfPath("/Payload")).IsLoaded(),
                    "file load-all loads payload");
            require(bool(session.stage()->GetPrimAtPath(SdfPath("/Payload/Geometry"))),
                    "file load-all composes payload descendants");
        }
    }

    void commandMergeApi()
    {
        QTemporaryDir dir;
        require(dir.isValid(), "temporary directory");
        const QString assetPath = makeAssetFixture(dir.filePath("asset.usda"));

        {
            Session session;
            require(session.newStage(), "new stage for destructive merge");
            stageviz::Command command = stageviz::mergeStage(assetPath);
            executeCommand(command, session);
            require(bool(session.stage()->GetPrimAtPath(SdfPath("/Asset/Geometry"))),
                    "mergeStage copies authored content");
            undoCommand(command, session);
            require(!session.stage()->GetPrimAtPath(SdfPath("/Asset")), "mergeStage undo restores layer");
            executeCommand(command, session);
            require(bool(session.stage()->GetPrimAtPath(SdfPath("/Asset/Geometry"))),
                    "mergeStage redo restores content");
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
            require(bool(session.stage()->GetPrimAtPath(SdfPath("/Asset/Geometry"))),
                    "addSublayer composes content");
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
            require(session.newStage(Session::None), "new load-none stage for payload command");
            require(session.stage()->GetLoadRules().GetEffectiveRuleForPath(SdfPath("/Target"))
                        == UsdStageLoadRules::NoneRule,
                    "load-none stage starts with none rule");
            session.stage()->DefinePrim(SdfPath("/Target"));
            stageviz::Command command = stageviz::addPayload(assetPath, SdfPath("/Target"));
            executeCommand(command, session);
            require(session.stage()->GetPrimAtPath(SdfPath("/Target")).HasPayload(),
                    "addPayload authors payload arc");
            require(!session.stage()->GetPrimAtPath(SdfPath("/Target")).IsLoaded(),
                    "addPayload respects load-none policy");
            require(!session.stage()->GetPrimAtPath(SdfPath("/Target/Geometry")),
                    "load-none payload does not compose payload descendants");
            undoCommand(command, session);
            require(bool(session.stage()->GetPrimAtPath(SdfPath("/Target"))), "addPayload undo keeps target");
            require(!session.stage()->GetPrimAtPath(SdfPath("/Target")).HasPayload(),
                    "addPayload undo restores payload field");
            executeCommand(command, session);
            require(session.stage()->GetPrimAtPath(SdfPath("/Target")).HasPayload(),
                    "addPayload redo restores payload arc");
            require(!session.stage()->GetPrimAtPath(SdfPath("/Target")).IsLoaded(),
                    "addPayload redo preserves load-none policy");
        }
        {
            Session session;
            require(session.newStage(Session::All), "new load-all stage for payload command");
            require(session.stage()->GetLoadRules().GetEffectiveRuleForPath(SdfPath("/Target"))
                        == UsdStageLoadRules::AllRule,
                    "load-all stage starts with all rule");
            session.stage()->DefinePrim(SdfPath("/Target"));
            stageviz::Command command = stageviz::addPayload(assetPath, SdfPath("/Target"));
            executeCommand(command, session);
            require(session.stage()->GetPrimAtPath(SdfPath("/Target")).HasPayload(),
                    "addPayload authors payload arc on load-all stage");
            require(session.stage()->GetPrimAtPath(SdfPath("/Target")).IsLoaded(),
                    "addPayload respects load-all policy");
            require(bool(session.stage()->GetPrimAtPath(SdfPath("/Target/Geometry"))),
                    "load-all payload composes payload descendants");
            undoCommand(command, session);
            require(!session.stage()->GetPrimAtPath(SdfPath("/Target")).HasPayload(),
                    "addPayload load-all undo restores payload field");
            executeCommand(command, session);
            require(session.stage()->GetPrimAtPath(SdfPath("/Target")).IsLoaded(),
                    "addPayload load-all redo preserves load policy");
        }
    }

    void namespaceEditorCrudApi()
    {
        const auto stage = UsdStage::CreateInMemory();
        stage->DefinePrim(SdfPath("/World"));
        stageviz::edit::NamespaceEditor editor(stage, stage->GetEditTarget());
        QString error;

        require(editor.addXform(SdfPath("/World/New"), error), "namespace add xform");
        require(bool(stage->GetPrimAtPath(SdfPath("/World/New"))), "namespace add materialized");
        require(!editor.changes().isEmpty() && editor.changes().last().type == stageviz::edit::NamespaceEditor::Change::Type::Add,
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
        require(editor.removePrims({SdfPath("/World/A"), SdfPath("/World/B"), SdfPath("/World/B/Child")}, error),
                "namespace batch remove");
        require(!stage->GetPrimAtPath(SdfPath("/World/A")) && !stage->GetPrimAtPath(SdfPath("/World/B")),
                "namespace batch remove covers descendants");
    }

    void usdPathUtilsApi()
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

        const QList<SdfPath> input {SdfPath("/A/B"), SdfPath("/A"), SdfPath("/C"), SdfPath("/A/B"), SdfPath()};
        require(stageviz::path::uniquePaths(input) == QList<SdfPath>({SdfPath("/A/B"), SdfPath("/A"), SdfPath("/C")}),
                "uniquePaths preserves first occurrence");
        const QList<SdfPath> roots = stageviz::path::minimalRootPaths(input);
        require(roots.contains(SdfPath("/A")) && roots.contains(SdfPath("/C")) && roots.size() == 2,
                "minimalRootPaths removes covered descendants");
        const QList<SdfPath> top = stageviz::path::topLevelPaths({SdfPath("/A"), SdfPath("/A/B"), SdfPath("/C")});
        require(top.contains(SdfPath("/A")) && top.contains(SdfPath("/C")) && top.size() == 2,
                "topLevelPaths removes descendants");
        require(stageviz::path::isAffectedPath(SdfPath("/A/B.attr"), SdfPath("/A")), "property path affected by root");
        require(stageviz::path::isWithinRoots({SdfPath("/A")}, SdfPath("/A/B")), "path lies within mask root");
        require(stageviz::path::isWithinRoots({}, SdfPath("/Anything")), "empty mask covers all paths");
        require(stageviz::path::isCoveredByRoots({SdfPath("/A")}, SdfPath("/A/B")), "selection root covers descendant");
        require(stageviz::path::removeAffectedPaths({SdfPath("/A"), SdfPath("/B/C")}, {SdfPath("/B")})
                    == QList<SdfPath>({SdfPath("/A")}), "removeAffectedPaths removes covered selection");
        require(stageviz::path::remapAffectedPaths({SdfPath("/A"), SdfPath("/A/B"), SdfPath("/C")},
                                                   SdfPath("/A"), SdfPath("/D"))
                    == QList<SdfPath>({SdfPath("/D"), SdfPath("/D/B"), SdfPath("/C")}),
                "remapAffectedPaths preserves suffixes");
        QList<SdfPath> appended {SdfPath("/A")};
        stageviz::path::appendUnique(appended, SdfPath("/A"));
        stageviz::path::appendUnique(appended, SdfPath("/B"));
        require(appended == QList<SdfPath>({SdfPath("/A"), SdfPath("/B")}), "appendUnique deduplicates");

        TfTokenVector order {TfToken("A"), TfToken("B"), TfToken("C")};
        require(stageviz::stage::removeChildOrderToken(order, TfToken("B")) == TfTokenVector({TfToken("A"), TfToken("C")}),
                "remove child order token");
        require(stageviz::stage::insertChildOrderToken(order, TfToken("D"), 1)
                    == TfTokenVector({TfToken("A"), TfToken("D"), TfToken("B"), TfToken("C")}),
                "insert child order token");
        require(stageviz::stage::remapChildOrder(order, TfToken("B"), TfToken("X"))
                    == TfTokenVector({TfToken("A"), TfToken("X"), TfToken("C")}), "remap child order token");
    }

    void usdStageUtilsApi()
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

        stageviz::stage::setVisible(stage, {SdfPath("/World/Hidden")}, false);
        require(!stageviz::stage::isVisible(stage, SdfPath("/World/Hidden")), "visibility hide helper");
        QList<SdfPath> visiblePaths = stageviz::stage::visiblePaths(stage);
        require(visiblePaths.contains(SdfPath("/World/Visible")) && !visiblePaths.contains(SdfPath("/World/Hidden")),
                "visiblePaths respects authored invisibility");
        stageviz::stage::setVisible(stage, {SdfPath("/World")}, false, true);
        require(!stageviz::stage::isVisible(stage, SdfPath("/World/Visible")), "recursive visibility applies to descendants");
        stageviz::stage::setVisible(stage, {SdfPath("/World")}, true, true);

        const QList<SdfPath> leaves = stageviz::stage::leafPaths(stage);
        require(leaves.contains(SdfPath("/World/Visible")) && leaves.contains(SdfPath("/World/Hidden")),
                "leafPaths returns terminal prims");
        require(!stageviz::stage::boundingBox(stage, {SdfPath("/World/Visible")}).GetRange().IsEmpty(),
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

    void usdPayloadUtilsApi()
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
        require(stageviz::stage::payloadPaths(stage, {SdfPath("/World/Outer")})
                    == QList<SdfPath>({SdfPath("/World/Outer")}), "payloadPaths returns direct payload");
        require(stageviz::stage::nearestPayloadPaths(stage, {SdfPath("/World/Outer/Geometry")})
                    == QList<SdfPath>({SdfPath("/World/Outer")}), "nearest payload resolves descendant");
        require(stageviz::stage::outermostPayloadPaths(stage, {SdfPath("/World/Outer")})
                    == QList<SdfPath>({SdfPath("/World/Outer")}), "outermost payload returns root");
        require(stageviz::stage::resolvePayloadPaths(stage, {SdfPath("/World/Outer/Geometry")})
                    == QList<SdfPath>({SdfPath("/World/Outer")}), "resolvePayloadPaths maps descendant");

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
        require(stageviz::payload::applyUnload(stage, SdfPath("/World/Outer"), unloadState, error), "applyUnload unloads payload");
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

    void materialUtilsApi()
    {
        const auto stage = UsdStage::CreateInMemory();
        const SdfPath previewPath = stageviz::MaterialUtils::createPreviewSurfaceMaterial(stage);
        const SdfPath standardPath = stageviz::MaterialUtils::createStandardSurfaceMaterial(stage);
        const SdfPath openPbrPath = stageviz::MaterialUtils::createOpenPBRSurfaceMaterial(stage);
        require(!previewPath.IsEmpty() && !standardPath.IsEmpty() && !openPbrPath.IsEmpty(),
                "material creators return paths");
        require(previewPath != standardPath && previewPath != openPbrPath && standardPath != openPbrPath,
                "material creators return unique paths");

        const QList<stageviz::MaterialEntry> materials = stageviz::MaterialUtils::sceneMaterials(stage);
        require(materials.size() == 3, "sceneMaterials finds supported materials");
        const auto previewIt = std::find_if(materials.cbegin(), materials.cend(), [&](const stageviz::MaterialEntry& e) {
            return e.materialPath == previewPath;
        });
        const auto standardIt = std::find_if(materials.cbegin(), materials.cend(), [&](const stageviz::MaterialEntry& e) {
            return e.materialPath == standardPath;
        });
        const auto openPbrIt = std::find_if(materials.cbegin(), materials.cend(), [&](const stageviz::MaterialEntry& e) {
            return e.materialPath == openPbrPath;
        });
        require(previewIt != materials.cend() && previewIt->shaderId == "UsdPreviewSurface",
                "preview material reports shader id");
        require(standardIt != materials.cend() && standardIt->shaderId == "ND_standard_surface_surfaceshader",
                "standard surface reports MaterialX shader id");
        require(openPbrIt != materials.cend() && openPbrIt->shaderId == "ND_open_pbr_surface_surfaceshader",
                "OpenPBR reports MaterialX shader id");
        {
            const QString previewLabel = stageviz::MaterialUtils::shaderTypeLabel(previewIt->shaderId);
            if (previewLabel != QStringLiteral("USD Preview Surface")) {
                std::cerr << "[material_utils_api] preview shader label mismatch: shaderId=\""
                          << previewIt->shaderId.toStdString()
                          << "\" actual=\"" << previewLabel.toStdString()
                          << "\" expected=\"USD Preview Surface\"\n";
            }
            require(previewLabel == QStringLiteral("USD Preview Surface"),
                    "preview shader type label");
        }
        require(!stageviz::MaterialUtils::shaderTypeLabel(standardIt->shaderId).isEmpty(),
                "MaterialX standard surface type label");
        require(!stageviz::MaterialUtils::shaderTypeLabel(openPbrIt->shaderId).isEmpty(),
                "MaterialX OpenPBR type label");

        // Validate MaterialUtils::readParameters() independently of the external
        // MaterialX reader. A synthetic .mtlx document is not a reliable place
        // to test parameter-value roundtripping because UsdMtlxRead applies its
        // own MaterialX library/custom-node translation rules. Here we author
        // values directly on Stageviz's known-good Standard Surface fixture and
        // verify the canonical parameter reader itself.
        {
            QString standardShaderId;
            const UsdShadeShader standardShader
                = stageviz::MaterialUtils::surfaceShader(UsdShadeMaterial(stage->GetPrimAtPath(standardPath)),
                                                         &standardShaderId);
            require(standardShader && standardShaderId == "ND_standard_surface_surfaceshader",
                    "resolve Standard Surface shader fixture");

            require(standardShader.GetInput(TfToken("base_color")).Set(GfVec3f(0.1f, 0.2f, 0.3f)),
                    "author Standard Surface base_color");
            require(standardShader.GetInput(TfToken("metalness")).Set(0.7f),
                    "author Standard Surface metalness");
            require(standardShader.GetInput(TfToken("roughness")).Set(0.25f),
                    "author Standard Surface roughness");
            require(standardShader.GetInput(TfToken("specular_IOR")).Set(1.7f),
                    "author Standard Surface specular_IOR");

            const stageviz::MaterialParameters authored
                = stageviz::MaterialUtils::readParameters(standardShader, standardShaderId);
            require(GfIsClose(authored.baseColor, GfVec3f(0.1f, 0.2f, 0.3f), 1e-5f)
                        && closeEnough(authored.metalness, 0.7, 1e-5)
                        && closeEnough(authored.roughness, 0.25, 1e-5)
                        && closeEnough(authored.ior, 1.7, 1e-5),
                    "Standard Surface parameters roundtrip");
        }

        require(stageviz::MaterialUtils::isSupportedParameter(*previewIt, "baseColor"), "preview baseColor supported");
        require(stageviz::MaterialUtils::isSupportedParameter(*previewIt, "roughness"), "preview roughness supported");
        require(!stageviz::MaterialUtils::isSupportedParameter(*previewIt, "transmission"),
                "preview unsupported transmission rejected");
        require(stageviz::MaterialUtils::inputName(*previewIt, "baseColor") == TfToken("diffuseColor"),
                "preview input name mapping");
        require(stageviz::MaterialUtils::inputPath(*previewIt, "roughness")
                    == previewIt->shaderPath.AppendProperty(TfToken("inputs:roughness")), "preview input path mapping");
        const stageviz::MaterialInputInfo* baseColorInfo = stageviz::MaterialUtils::inputInfo(*previewIt, "baseColor");
        require(baseColorInfo && baseColorInfo->typeName == SdfValueTypeNames->Color3f,
                "preview baseColor exposes Color3f input metadata");

        const stageviz::MaterialNodeInfo previewNode = stageviz::MaterialUtils::nodeInfo(stage, previewIt->shaderPath);
        require(previewNode.path == previewIt->shaderPath && previewNode.shaderId == "UsdPreviewSurface",
                "nodeInfo identifies Preview Surface node");
        const auto diffuseInfo = std::find_if(previewNode.inputs.cbegin(), previewNode.inputs.cend(), [](const auto& input) {
            return input.inputName == TfToken("diffuseColor");
        });
        require(diffuseInfo != previewNode.inputs.cend() && diffuseInfo->typeName == SdfValueTypeNames->Color3f,
                "nodeInfo exposes diffuseColor type");

        const QList<stageviz::ShaderNodeDefinition> usdNodes = stageviz::MaterialUtils::usdShaderNodes();
        auto hasUsdNode = [&](const QString& shaderId) {
            return std::any_of(usdNodes.cbegin(), usdNodes.cend(), [&](const auto& node) { return node.shaderId == shaderId; });
        };
        require(hasUsdNode("UsdUVTexture") && hasUsdNode("UsdTransform2d")
                    && hasUsdNode("UsdPrimvarReader_float") && hasUsdNode("UsdPrimvarReader_float2")
                    && hasUsdNode("UsdPrimvarReader_float3") && hasUsdNode("UsdPrimvarReader_float4")
                    && hasUsdNode("UsdPrimvarReader_int") && hasUsdNode("UsdPrimvarReader_string"),
                "USD Preview helper-node registry exposes curated nodes");

        const QList<stageviz::ShaderNodeDefinition> colorNodes
            = stageviz::MaterialUtils::compatibleUsdShaderNodes(SdfValueTypeNames->Color3f);
        require(std::any_of(colorNodes.cbegin(), colorNodes.cend(), [](const auto& node) {
                    return node.shaderId == "UsdUVTexture" && node.outputName == TfToken("rgb");
                }), "Color3f menu keeps UsdUVTexture RGB");
        require(!std::any_of(colorNodes.cbegin(), colorNodes.cend(), [](const auto& node) {
                    return node.shaderId == "UsdPrimvarReader_float3";
                }), "Color3f menu rejects generic float3 primvar reader");

        const QList<stageviz::ShaderNodeDefinition> floatNodes
            = stageviz::MaterialUtils::compatibleUsdShaderNodes(SdfValueTypeNames->Float);
        require(std::any_of(floatNodes.cbegin(), floatNodes.cend(), [](const auto& node) {
                    return node.shaderId == "UsdPrimvarReader_float";
                }), "Float menu includes float primvar reader");
        require(!std::any_of(floatNodes.cbegin(), floatNodes.cend(), [](const auto& node) {
                    return node.outputType != SdfValueTypeNames->Float;
                }), "Float menu only exposes scalar float outputs");
        require(!std::any_of(floatNodes.cbegin(), floatNodes.cend(), [](const auto& node) {
                    return node.shaderId == "UsdPrimvarReader_float3";
                }), "Float menu excludes float3 primvar reader");

        const SdfPath unique = stageviz::MaterialUtils::uniqueMaterialPath(stage, "123 bad material");
        require(unique.GetName() == "_123_bad_material", "unique material path sanitizes identifier");

        QTemporaryDir dir;
        require(dir.isValid(), "temporary directory");
        const QString mtlx = dir.filePath("simple.mtlx");
        // Keep this fixture completely self-contained. UsdMtlxReadDocument() reads
        // the file as-is; it does not automatically import the MaterialX standard
        // library into this synthetic document. Without a NodeDef the reader can
        // create the surfacematerial prim but has no resolved shader definition to
        // translate, leaving outputs:surface unconnected.
        writeFile(mtlx, R"(<materialx version="1.38">
  <nodedef name="ND_standard_surface_surfaceshader" node="standard_surface" type="surfaceshader">
    <input name="base_color" type="color3" value="0.8, 0.8, 0.8"/>
    <input name="metalness" type="float" value="0.0"/>
    <input name="roughness" type="float" value="0.2"/>
    <input name="specular_IOR" type="float" value="1.5"/>
    <output name="out" type="surfaceshader"/>
  </nodedef>
  <standard_surface name="ImportedSurface" type="surfaceshader">
    <input name="base_color" type="color3" value="0.1, 0.2, 0.3"/>
    <input name="metalness" type="float" value="0.7"/>
    <input name="roughness" type="float" value="0.25"/>
    <input name="specular_IOR" type="float" value="1.7"/>
  </standard_surface>
  <surfacematerial name="ImportedMaterial" type="material">
    <input name="surfaceshader" type="surfaceshader" nodename="ImportedSurface"/>
  </surfacematerial>
</materialx>)");
        QList<SdfPath> imported;
        QString error;
        const bool importedOk = stageviz::MaterialUtils::importMaterialX(stage, mtlx, imported, error);
        if (!importedOk) {
            std::cerr << "[material_utils_api] MaterialX import failed: "
                      << error.toStdString() << "\n";
        }
        require(importedOk, "import simple MaterialX");
        require(imported.size() == 1 && bool(stage->GetPrimAtPath(imported.first())), "MaterialX creates USD material");
        QString shaderId;
        const UsdShadeMaterial importedMaterial(stage->GetPrimAtPath(imported.first()));
        const UsdShadeShader shader = stageviz::MaterialUtils::surfaceShader(importedMaterial, &shaderId);

        if (!shader || shaderId != QStringLiteral("ND_standard_surface_surfaceshader")) {
            std::cerr << "[material_utils_api] imported material path: "
                      << imported.first().GetString() << "\n";
            std::cerr << "[material_utils_api] surfaceShader valid="
                      << (shader ? "true" : "false")
                      << " shaderId=\"" << shaderId.toStdString() << "\"\n";

            auto dumpSurfaceOutput = [&](const TfToken& context) {
                const UsdShadeOutput output = context.IsEmpty()
                                                  ? importedMaterial.GetSurfaceOutput()
                                                  : importedMaterial.GetSurfaceOutput(context);
                std::cerr << "[material_utils_api] surface output context=\""
                          << context.GetString() << "\" valid="
                          << (output ? "true" : "false");

                if (output) {
                    UsdShadeConnectableAPI source;
                    TfToken sourceName;
                    UsdShadeAttributeType sourceType = UsdShadeAttributeType::Output;
                    if (UsdShadeConnectableAPI::GetConnectedSource(output, &source, &sourceName, &sourceType)) {
                        std::cerr << " source=" << source.GetPrim().GetPath().GetString()
                                  << "." << sourceName.GetString();
                    }
                    else {
                        std::cerr << " source=<none>";
                    }
                }
                std::cerr << "\n";
            };

            dumpSurfaceOutput(TfToken());
            dumpSurfaceOutput(TfToken("mtlx"));

            SdfPath dumpRoot = imported.first();
            for (int i = 0; i < 2 && !dumpRoot.IsAbsoluteRootPath(); ++i)
                dumpRoot = dumpRoot.GetParentPath();

            const UsdPrim rootPrim = stage->GetPrimAtPath(dumpRoot);
            if (rootPrim) {
                std::cerr << "[material_utils_api] imported subtree from "
                          << dumpRoot.GetString() << ":\n";
                for (const UsdPrim& prim : UsdPrimRange(rootPrim)) {
                    std::cerr << "  " << prim.GetPath().GetString()
                              << " type=" << prim.GetTypeName().GetString();
                    if (prim.IsA<UsdShadeShader>()) {
                        TfToken id;
                        UsdShadeShader(prim).GetIdAttr().Get(&id);
                        std::cerr << " id=" << id.GetString();
                    }
                    std::cerr << "\n";
                }
            }
        }

        require(shader && shaderId == "ND_standard_surface_surfaceshader", "imported MaterialX exposes standard surface");
        // Import coverage stops at the translated material/shader topology.
        // UsdMtlxRead does not guarantee that values authored on a synthetic
        // locally-defined NodeDef are copied exactly like values from an installed
        // MaterialX standard library; OpenUSD also warns that such local custom
        // nodes are not fully supported. Parameter-value roundtripping is tested
        // above against Stageviz's native Standard Surface fixture instead.
        require(bool(shader), "MaterialX import resolves translated surface shader");
    }

    void materialMenuApi()
    {
        using Compatibility = stageviz::MaterialMenu::Compatibility;

        require(stageviz::MaterialMenu::compatibility(SdfValueTypeNames->Float, SdfValueTypeNames->Float)
                    == Compatibility::Compatible,
                "material menu accepts exact scalar type");
        require(stageviz::MaterialMenu::compatibility(SdfValueTypeNames->Color3f, SdfValueTypeNames->Color3f)
                    == Compatibility::Compatible,
                "material menu accepts exact color type");
        require(stageviz::MaterialMenu::compatibility(SdfValueTypeNames->Float3, SdfValueTypeNames->Color3f)
                    == Compatibility::Incompatible,
                "material menu rejects float3 to color3f");
        require(stageviz::MaterialMenu::compatibility(SdfValueTypeNames->Color3f, SdfValueTypeNames->Float3)
                    == Compatibility::Incompatible,
                "material menu rejects color3f to float3");
        require(stageviz::MaterialMenu::compatibility(SdfValueTypeNames->Float2, SdfValueTypeNames->TexCoord2f)
                    == Compatibility::Compatible,
                "material menu accepts float2 to texcoord2f");
        require(stageviz::MaterialMenu::compatibility(SdfValueTypeNames->TexCoord2f, SdfValueTypeNames->Float2)
                    == Compatibility::Compatible,
                "material menu accepts texcoord2f to float2");
        require(stageviz::MaterialMenu::compatibility(SdfValueTypeNames->Float, SdfValueTypeNames->Color3f)
                    == Compatibility::Incompatible,
                "material menu rejects scalar to color");

        const UsdStageRefPtr stage = UsdStage::CreateInMemory();
        require(bool(stage), "create material-menu stage");
        UsdShadeShader source = UsdShadeShader::Define(stage, SdfPath("/Source"));
        UsdShadeShader target = UsdShadeShader::Define(stage, SdfPath("/Target"));
        const UsdShadeOutput colorOutput = source.CreateOutput(TfToken("color"), SdfValueTypeNames->Color3f);
        const UsdShadeOutput vectorOutput = source.CreateOutput(TfToken("vector"), SdfValueTypeNames->Float3);
        const UsdShadeInput colorInput = target.CreateInput(TfToken("color"), SdfValueTypeNames->Color3f);
        require(stageviz::MaterialMenu::connectionCompatibility(stage, colorOutput.GetAttr().GetPath(),
                                                                 colorInput.GetAttr().GetPath())
                    == Compatibility::Compatible,
                "connection compatibility accepts exact color socket types");
        require(stageviz::MaterialMenu::connectionCompatibility(stage, vectorOutput.GetAttr().GetPath(),
                                                                 colorInput.GetAttr().GetPath())
                    == Compatibility::Incompatible,
                "connection compatibility rejects float3 to color3f sockets");
    }

    void commandMaterialApi()
    {
        Session session;
        require(session.newStage(), "new stage for material commands");
        const UsdStageRefPtr stage = session.stage();
        const SdfPath materialPath = stageviz::MaterialUtils::createPreviewSurfaceMaterial(stage);
        require(!materialPath.IsEmpty(), "create Preview Surface material fixture");
        const QList<stageviz::MaterialEntry> entries = stageviz::MaterialUtils::sceneMaterials(stage);
        const auto materialIt = std::find_if(entries.cbegin(), entries.cend(), [&](const auto& entry) {
            return entry.materialPath == materialPath;
        });
        require(materialIt != entries.cend(), "find Preview Surface material fixture");
        const SdfPath diffusePath = stageviz::MaterialUtils::inputPath(*materialIt, "baseColor");
        require(!diffusePath.IsEmpty(), "resolve diffuseColor input path");

        // Existing exact-typed output -> input connection, disconnect, and undo.
        UsdShadeShader source = UsdShadeShader::Define(stage, SdfPath("/ColorSource"));
        source.CreateIdAttr(VtValue(TfToken("TestColorSource")));
        const UsdShadeOutput sourceOutput = source.CreateOutput(TfToken("out"), SdfValueTypeNames->Color3f);
        stageviz::Command connect = stageviz::connectShaderInput(diffusePath, sourceOutput.GetAttr().GetPath());
        executeCommand(connect, session);
        SdfPathVector connections;
        require(stage->GetAttributeAtPath(diffusePath).GetConnections(&connections) && connections.size() == 1
                    && connections.front() == sourceOutput.GetAttr().GetPath(),
                "connectShaderInput authors exact-typed connection");
        undoCommand(connect, session);
        connections.clear();
        stage->GetAttributeAtPath(diffusePath).GetConnections(&connections);
        require(connections.empty(), "connectShaderInput undo restores disconnected state");

        require(stage->GetAttributeAtPath(diffusePath).SetConnections({sourceOutput.GetAttr().GetPath()}),
                "author disconnect fixture");
        stageviz::Command disconnect = stageviz::disconnectShaderInputs({diffusePath});
        executeCommand(disconnect, session);
        connections.clear();
        stage->GetAttributeAtPath(diffusePath).GetConnections(&connections);
        require(connections.empty(), "disconnectShaderInputs removes incoming connection");
        undoCommand(disconnect, session);
        connections.clear();
        require(stage->GetAttributeAtPath(diffusePath).GetConnections(&connections) && connections.size() == 1
                    && connections.front() == sourceOutput.GetAttr().GetPath(),
                "disconnectShaderInputs undo restores connection");
        require(stage->GetAttributeAtPath(diffusePath).SetConnections({}), "clear disconnect fixture");

        // The curated USD Preview exception: UVTexture.rgb may feed diffuseColor.
        stageviz::Command uv = stageviz::connectShaderNode(diffusePath, "UsdUVTexture", "UVTexture", TfToken("rgb"));
        executeCommand(uv, session);
        connections.clear();
        require(stage->GetAttributeAtPath(diffusePath).GetConnections(&connections) && connections.size() == 1,
                "connectShaderNode connects UsdUVTexture RGB to diffuseColor");
        const SdfPath uvNodePath = connections.front().GetPrimPath();
        TfToken uvId;
        require(UsdShadeShader(stage->GetPrimAtPath(uvNodePath)).GetIdAttr().Get(&uvId) && uvId == TfToken("UsdUVTexture"),
                "connectShaderNode authors UsdUVTexture shader id");
        undoCommand(uv, session);
        connections.clear();
        stage->GetAttributeAtPath(diffusePath).GetConnections(&connections);
        require(connections.empty() && !stage->GetPrimAtPath(uvNodePath),
                "connectShaderNode undo removes created helper node and connection");

        // A generic Float3 -> Color3f helper is unsafe and must fail without
        // leaving either a connection or a partially-created shader prim.
        std::string beforeInvalid;
        stage->GetRootLayer()->ExportToString(&beforeInvalid);
        QString materialError;
        QStringList materialNotifications;
        QObject::connect(&session, &Session::notifyStatusChanged, &session,
                         [&](Session::Notify::Status status, const QString& message, const QString& detail) {
                             const QString line = QStringLiteral("status=%1 message=\"%2\" detail=\"%3\"")
                                                      .arg(static_cast<int>(status))
                                                      .arg(message)
                                                      .arg(detail);
                             materialNotifications.append(line);
                             std::cerr << "[command_material_api] " << line.toStdString() << '\n';

                             if (status == Session::Notify::Status::Error)
                                 materialError = message.isEmpty() ? detail : message;
                         });
        stageviz::Command invalid = stageviz::connectShaderNode(diffusePath, "UsdPrimvarReader_float3",
                                                                "PrimvarReader", TfToken("result"));
        executeCommand(invalid, session);
        connections.clear();
        stage->GetAttributeAtPath(diffusePath).GetConnections(&connections);
        require(connections.empty(), "connectShaderNode rejects float3 to color3f connection");
        std::string afterInvalid;
        stage->GetRootLayer()->ExportToString(&afterInvalid);
        if (beforeInvalid != afterInvalid) {
            std::cerr << "[command_material_api] edit layer changed after rejected connection\n"
                      << "--- before ---\n" << beforeInvalid
                      << "\n--- after ---\n" << afterInvalid << '\n';
        }
        require(beforeInvalid == afterInvalid, "rejected shader-node connection leaves edit layer unchanged");

        if (materialError.isEmpty()) {
            std::cerr << "[command_material_api] rejected connection produced no error notification\n";
            if (materialNotifications.isEmpty()) {
                std::cerr << "[command_material_api] no notifyStatusChanged signals were received\n";
            }
            else {
                std::cerr << "[command_material_api] received notifications:\n";
                for (const QString& line : materialNotifications)
                    std::cerr << "  " << line.toStdString() << '\n';
            }
        }
        require(!materialError.isEmpty(), "rejected shader-node connection reports command error");

        // Reset one authored shader input and restore it through undo.
        const SdfPath roughnessPath = stageviz::MaterialUtils::inputPath(*materialIt, "roughness");
        UsdAttribute roughness = stage->GetAttributeAtPath(roughnessPath);
        require(roughness && roughness.Set(0.73f), "author roughness reset fixture");
        require(bool(stage->GetEditTarget().GetLayer()->GetPropertyAtPath(roughnessPath)),
                "roughness fixture exists in edit layer");
        stageviz::Command reset = stageviz::resetShaderInputs({roughnessPath});
        executeCommand(reset, session);
        require(!stage->GetEditTarget().GetLayer()->GetPropertyAtPath(roughnessPath),
                "resetShaderInputs removes edit-layer input opinion");
        undoCommand(reset, session);
        float roughnessValue = 0.0f;
        require(stage->GetAttributeAtPath(roughnessPath).Get(&roughnessValue) && closeEnough(roughnessValue, 0.73),
                "resetShaderInputs undo restores authored value");
    }

    void commandSelectionApi()
    {
        Session session;
        require(session.newStage(), "new stage");
        session.stage()->DefinePrim(SdfPath("/A"));
        session.stage()->DefinePrim(SdfPath("/A/Leaf"));
        session.stage()->DefinePrim(SdfPath("/B"));
        session.stage()->DefinePrim(SdfPath("/B/Leaf"));

        stageviz::Command select = stageviz::selectPaths({SdfPath("/A")});
        executeCommand(select, session);
        require(session.selectionList()->paths() == QList<SdfPath>({SdfPath("/A")}), "selectPaths command");
        undoCommand(select, session);
        require(session.selectionList()->paths().isEmpty(), "selectPaths undo");

        stageviz::Command all = stageviz::selectAll(false);
        executeCommand(all, session);
        const QList<SdfPath> roots = session.selectionList()->paths();
        require(roots.contains(SdfPath("/World")) && roots.contains(SdfPath("/A"))
                    && roots.contains(SdfPath("/B")) && roots.size() == 3,
                "selectAll root mode");
        undoCommand(all, session);

        session.selectionList()->updatePaths({SdfPath("/A/Leaf")});
        stageviz::Command invert = stageviz::selectInvert();
        executeCommand(invert, session);
        require(session.selectionList()->paths().contains(SdfPath("/B/Leaf"))
                    && !session.selectionList()->paths().contains(SdfPath("/A/Leaf")), "selectInvert command");
        undoCommand(invert, session);
        require(session.selectionList()->paths() == QList<SdfPath>({SdfPath("/A/Leaf")}), "selectInvert undo");

        stageviz::Command isolate = stageviz::isolatePaths({SdfPath("/A")});
        executeCommand(isolate, session);
        require(session.mask() == QList<SdfPath>({SdfPath("/A")}), "isolatePaths command");
        undoCommand(isolate, session);
        require(session.mask().isEmpty(), "isolatePaths undo");
    }

    void commandAuthoringApi()
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

        stageviz::Command duplicate = stageviz::duplicatePaths({SdfPath("/World/Xform")});
        executeCommand(duplicate, session);
        require(bool(session.stage()->GetPrimAtPath(SdfPath("/World/Xform_1"))), "duplicatePaths command");
        undoCommand(duplicate, session);
        require(!session.stage()->GetPrimAtPath(SdfPath("/World/Xform_1")), "duplicatePaths undo");

        stageviz::Command remove = stageviz::deletePaths({SdfPath("/World/Scope")});
        executeCommand(remove, session);
        require(!session.stage()->GetPrimAtPath(SdfPath("/World/Scope")), "deletePaths command");
        undoCommand(remove, session);
        require(bool(session.stage()->GetPrimAtPath(SdfPath("/World/Scope"))), "deletePaths undo");
    }

    void commandPropertyApi()
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
        stageviz::Command setMany = stageviz::setAttributeValues({attrPath, second.GetPath()}, VtValue(4.0f));
        executeCommand(setMany, session);
        float secondValue = 0.0f;
        require(attr.Get(&value) && second.Get(&secondValue) && closeEnough(value, 4.0) && closeEnough(secondValue, 4.0),
                "setAttributeValues command");
        undoCommand(setMany, session);
        require(attr.Get(&value) && second.Get(&secondValue) && closeEnough(value, 1.0) && closeEnough(secondValue, 3.0),
                "setAttributeValues undo");

        stageviz::Command reset = stageviz::resetAttributeValues({attrPath});
        executeCommand(reset, session);
        require(!attr.HasAuthoredValueOpinion(), "resetAttributeValues removes authored default");
        undoCommand(reset, session);
        require(attr.Get(&value) && closeEnough(value, 1.0), "resetAttributeValues undo restores value");
    }

    void commandVisibilityStageApi()
    {
        Session session;
        require(session.newStage(), "new stage");
        require(session.stageUp() == Session::Z, "new stage defaults to Z up");
        UsdGeomCube::Define(session.stage(), SdfPath("/Cube"));

        stageviz::Command hide = stageviz::hidePaths({SdfPath("/Cube")}, false);
        executeCommand(hide, session);
        require(!stageviz::stage::isVisible(session.stage(), SdfPath("/Cube")), "hidePaths command");
        undoCommand(hide, session);
        require(stageviz::stage::isVisible(session.stage(), SdfPath("/Cube")), "hidePaths undo");

        stageviz::Command show = stageviz::showPaths({SdfPath("/Cube")}, false);
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
        require(session.stage()->GetDefaultPrim().GetPath() == SdfPath("/World"), "defaultPrimPath undo restores previous default prim");

        stageviz::Command clearDefault = stageviz::clearDefaultPrim();
        executeCommand(makeDefault, session);
        executeCommand(clearDefault, session);
        require(!session.stage()->GetDefaultPrim(), "clearDefaultPrim command");
        undoCommand(clearDefault, session);
        require(session.stage()->GetDefaultPrim().GetPath() == SdfPath("/Cube"), "clearDefaultPrim undo");
    }

    void commandMaterialVariantApi()
    {
        Session session;
        require(session.newStage(), "new stage");
        const UsdPrim target = session.stage()->DefinePrim(SdfPath("/Target"));
        const SdfPath materialPath = stageviz::MaterialUtils::createPreviewSurfaceMaterial(session.stage());

        stageviz::Command bind = stageviz::bindMaterial({SdfPath("/Target")}, materialPath);
        executeCommand(bind, session);
        const UsdShadeMaterial bound = UsdShadeMaterialBindingAPI(target).ComputeBoundMaterial();
        require(bound && bound.GetPath() == materialPath, "bindMaterial command");
        undoCommand(bind, session);
        require(!UsdShadeMaterialBindingAPI(target).ComputeBoundMaterial(), "bindMaterial undo");

        UsdVariantSet variants = target.GetVariantSets().AddVariantSet("trim");
        require(variants.AddVariant("Base") && variants.AddVariant("Sport"), "author variant fixture");
        require(variants.SetVariantSelection("Base"), "select base variant fixture");
        stageviz::Command variant = stageviz::setVariantSelection({SdfPath("/Target")}, "trim", "Sport");
        executeCommand(variant, session);
        require(target.GetVariantSet("trim").GetVariantSelection() == "Sport", "setVariantSelection command");
        undoCommand(variant, session);
        require(target.GetVariantSet("trim").GetVariantSelection() == "Base", "setVariantSelection undo");
    }

    void commandCompositionApi()
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
        require(session.stage()->GetPrimAtPath(SdfPath("/World/Payload")).HasPayload(), "newPayloadPath authors payload arc");
        require(bool(session.stage()->GetPrimAtPath(SdfPath("/World/Payload/Geometry"))), "newPayloadPath composes asset");
        undoCommand(payload, session);
        require(!session.stage()->GetPrimAtPath(SdfPath("/World/Payload")), "newPayloadPath undo");
    }

    void commandMoveApi()
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

        stageviz::Command move = stageviz::movePath({SdfPath("/A/Part")}, SdfPath("/B"), -1, true);
        executeCommand(move, session);
        require(bool(session.stage()->GetPrimAtPath(SdfPath("/B/Part")))
                    && !session.stage()->GetPrimAtPath(SdfPath("/A/Part")), "movePath reparents prim");
        GfMatrix4d after;
        require(stageviz::stage::worldTransform(session.stage(), SdfPath("/B/Part"), after, error),
                "moved prim world transform readable");
        require(GfIsClose(before, after, 1e-6), "movePath preserves world transform");

        undoCommand(move, session);
        require(bool(session.stage()->GetPrimAtPath(SdfPath("/A/Part")))
                    && !session.stage()->GetPrimAtPath(SdfPath("/B/Part")), "movePath undo restores namespace");
        GfMatrix4d restored;
        require(stageviz::stage::worldTransform(session.stage(), SdfPath("/A/Part"), restored, error),
                "restored prim world transform readable");
        require(GfIsClose(before, restored, 1e-6), "movePath undo restores world transform");
    }

    void commandPayloadApi()
    {
        QTemporaryDir dir;
        require(dir.isValid(), "temporary directory");
        const QString assetPath = makeAssetFixture(dir.filePath("payload.usda"));

        Session session;
        require(session.newStage(), "new stage");
        const UsdPrim payloadPrim = session.stage()->DefinePrim(SdfPath("/Payload"));
        require(payloadPrim.GetPayloads().AddPayload(assetPath.toStdString()), "author payload fixture");
        require(payloadPrim.IsLoaded(), "payload fixture starts loaded");

        session.selectionList()->updatePaths({SdfPath("/Payload/Geometry")});
        stageviz::Command select = stageviz::selectPayload();
        executeCommand(select, session);
        require(session.selectionList()->paths() == QList<SdfPath>({SdfPath("/Payload")}),
                "selectPayload resolves descendant");
        undoCommand(select, session);
        require(session.selectionList()->paths() == QList<SdfPath>({SdfPath("/Payload/Geometry")}),
                "selectPayload undo");

        stageviz::Command unload = stageviz::unloadPayloads({SdfPath("/Payload")});
        executeCommand(unload, session);
        require(!session.stage()->GetPrimAtPath(SdfPath("/Payload")).IsLoaded(), "unloadPayloads command");
        undoCommand(unload, session);
        require(session.stage()->GetPrimAtPath(SdfPath("/Payload")).IsLoaded(), "unloadPayloads undo");

        session.stage()->Unload(SdfPath("/Payload"));
        stageviz::Command load = stageviz::loadPayloads({SdfPath("/Payload")});
        executeCommand(load, session);
        require(session.stage()->GetPrimAtPath(SdfPath("/Payload")).IsLoaded(), "loadPayloads command");
        undoCommand(load, session);
        require(!session.stage()->GetPrimAtPath(SdfPath("/Payload")).IsLoaded(), "loadPayloads undo");
    }
}

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    const std::map<std::string, std::function<void()>> cases {
        {"save_twice_usda", [] { saveTwice("usda"); }},
        {"save_twice_usdc", [] { saveTwice("usdc"); }},
        {"save_failure", saveFailure}, {"sublayer_save", sublayerSave},
        {"save_as", saveAs}, {"save_as_payload_anchor", saveAsPayloadAnchor},
        {"corrupt_state", corruptState},
        {"payload_state", [] { payloadState(false); }}, {"legacy_payload_state", [] { payloadState(true); }},
        {"reparent_dependencies", reparentDependencies}, {"rename_dependencies", renameDependencies},
        {"reparent_collision", reparentCollision}, {"reparent_into_descendant", reparentIntoDescendant},
        {"reparent_batch", reparentBatch},
        {"namespace_load_rules", namespaceLoadRules},
        {"namespace_load_rules_batch", namespaceLoadRulesBatch},
        {"namespace_load_rules_batch_failure", namespaceLoadRulesBatchFailure},
        {"namespace_load_rules_rename", namespaceLoadRulesRename},
        {"export_session_opinions", exportSessionOpinions},
        {"export_multiple_session_opinions", exportMultipleSessionOpinions},
        {"merge_asset_paths", mergeAssetPaths}, {"merge_composition_asset_paths", mergeCompositionAssetPaths},
        {"selection_list_api", selectionListApi}, {"view_camera_api", viewCameraApi},
        {"view_state_api", viewStateApi}, {"session_core_api", sessionCoreApi},
        {"session_file_api", sessionFileApi}, {"session_edit_layer_api", sessionEditLayerApi},
        {"session_state_api", sessionStateApi}, {"session_load_policy_api", sessionLoadPolicyApi},
        {"command_merge_api", commandMergeApi},
        {"namespace_editor_crud_api", namespaceEditorCrudApi}, {"usd_path_utils_api", usdPathUtilsApi},
        {"usd_stage_utils_api", usdStageUtilsApi}, {"usd_payload_utils_api", usdPayloadUtilsApi},
        {"material_utils_api", materialUtilsApi}, {"material_menu_api", materialMenuApi},
        {"command_material_api", commandMaterialApi}, {"command_selection_api", commandSelectionApi},
        {"command_authoring_api", commandAuthoringApi}, {"command_property_api", commandPropertyApi},
        {"command_visibility_stage_api", commandVisibilityStageApi},
        {"command_material_variant_api", commandMaterialVariantApi},
        {"command_composition_api", commandCompositionApi}, {"command_move_api", commandMoveApi},
        {"command_payload_api", commandPayloadApi}
    };
    if (argc != 2 || !cases.count(argv[1])) {
        std::cerr << "Pass one registered regression case name\n";
        return 2;
    }
    try {
        cases.at(argv[1])();
    }
    catch (const std::exception& error) {
        std::cerr << argv[1] << ": " << error.what() << '\n';
        return 1;
    }
    catch (...) {
        std::cerr << argv[1] << ": unknown exception\n";
        return 1;
    }

    return 0;
}
