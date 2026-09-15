// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell

#include "session.h"
#include "usdedit.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <functional>
#include <iostream>
#include <map>
#include <stdexcept>
#include <pxr/base/tf/errorMark.h>
#include <pxr/usd/sdf/assetPath.h>
#include <pxr/usd/sdf/types.h>
#include <pxr/usd/usd/attribute.h>
#include <pxr/usd/usd/payloads.h>
#include <pxr/usd/usd/references.h>
#include <pxr/usd/usd/relationship.h>

PXR_NAMESPACE_USING_DIRECTIVE
using stageviz::Session;

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
        TfErrorMark mark;
        require(!session.saveToFile(target), "save must fail");
        QCoreApplication::processEvents();
        mark.Clear();
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
        require(session.mergeFromFile(sourceFile), "merge source");
        SdfAssetPath texture;
        require(session.stage()->GetPrimAtPath(SdfPath("/Part")).GetAttribute(TfToken("texture")).Get(&texture),
                "merged asset attribute");
        require(texture.GetAssetPath() == dir.filePath("source/textures/paint.<UDIM>.exr").toStdString(),
                "merged texture retains source anchor");
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
}

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    const std::map<std::string, std::function<void()>> cases {
        {"save_twice_usda", [] { saveTwice("usda"); }},
        {"save_twice_usdc", [] { saveTwice("usdc"); }},
        {"save_failure", saveFailure}, {"sublayer_save", sublayerSave},
        {"save_as", saveAs}, {"corrupt_state", corruptState},
        {"payload_state", [] { payloadState(false); }}, {"legacy_payload_state", [] { payloadState(true); }},
        {"reparent_dependencies", reparentDependencies}, {"reparent_batch", reparentBatch},
        {"namespace_load_rules", namespaceLoadRules},
        {"namespace_load_rules_batch", namespaceLoadRulesBatch},
        {"namespace_load_rules_rename", namespaceLoadRulesRename},
        {"export_session_opinions", exportSessionOpinions},
        {"merge_asset_paths", mergeAssetPaths}
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
