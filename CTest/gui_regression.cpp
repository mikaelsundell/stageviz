// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell

#include "application.h"
#include "indextree.h"
#include "materialbrowser.h"
#include "materialutils.h"
#include "renderengine.h"
#include "settings.h"
#include "style.h"
#include "treewidget.h"
#include <QApplication>
#include <QColor>
#include <QCoreApplication>
#include <QDir>
#include <QImage>
#include <QItemSelectionModel>
#include <QSet>
#include <QSettings>
#include <QTemporaryDir>
#include <QTreeWidgetItem>
#include <functional>
#include <iostream>
#include <map>
#include <pxr/base/gf/camera.h>
#include <pxr/base/gf/matrix4d.h>
#include <pxr/base/gf/range1f.h>
#include <pxr/base/gf/vec3d.h>
#include <pxr/usd/usd/stage.h>
#include <pxr/usd/usdGeom/cube.h>
#include <pxr/usd/usdGeom/mesh.h>
#include <pxr/usd/usdGeom/subset.h>
#include <pxr/usd/usdShade/material.h>
#include <pxr/usd/usdShade/materialBindingAPI.h>
#include <stdexcept>

PXR_NAMESPACE_USING_DIRECTIVE

namespace {
void
require(bool result, const char* message)
{
    if (!result)
        throw std::runtime_error(message);
}

void
settingsApi()
{
    QTemporaryDir dir;
    require(dir.isValid(), "temporary settings directory");

    const QSettings::Format previousFormat = QSettings::defaultFormat();
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, dir.path());

    {
        stageviz::Settings settings;

        // Use a unique key for this process so the default-value check
        // cannot collide with settings left behind by earlier test runs.
        const QString key = QStringLiteral("tests/native/value_%1").arg(QCoreApplication::applicationPid());

        require(settings.value(key, 42).toInt() == 42, "settings returns default value");

        settings.setValue(key, QStringLiteral("stored"));
        require(settings.value(key).toString() == QStringLiteral("stored"), "settings value roundtrip");
    }

    QSettings::setDefaultFormat(previousFormat);
}

void
styleApi()
{
    stageviz::Style style;
    int colorChanges = 0;
    QObject::connect(&style, &stageviz::Style::colorChanged, &style,
                     [&](stageviz::Style::ColorRole) { ++colorChanges; });

    const QColor accent(11, 22, 33, 255);
    style.setColor(stageviz::Style::Accent, accent);
    require(style.color(stageviz::Style::Accent) == accent, "style color roundtrip");
    require(colorChanges == 1, "style color signal emitted");

    style.setFontSize(stageviz::Style::Small, 11);
    style.setFontSize(stageviz::Style::Medium, 13);
    style.setFontSize(stageviz::Style::Large, 17);
    require(style.fontSize(stageviz::Style::Small) == 11 && style.fontSize(stageviz::Style::Medium) == 13
                && style.fontSize(stageviz::Style::Large) == 17,
            "style font size roundtrip");

    style.setIconSize(stageviz::Style::Small, 16);
    style.setIconSize(stageviz::Style::Medium, 24);
    style.setIconSize(stageviz::Style::Large, 32);
    require(style.iconSize(stageviz::Style::Small) == 16 && style.iconSize(stageviz::Style::Medium) == 24
                && style.iconSize(stageviz::Style::Large) == 32,
            "style icon size roundtrip");

    const QString originalIcon = style.iconPath(stageviz::Style::Open);
    require(!originalIcon.isEmpty(), "style exposes icon resource path");
    style.setIconPath(stageviz::Style::Open, originalIcon);
    require(style.iconPath(stageviz::Style::Open) == originalIcon, "style icon path roundtrip");
    require(!style.icon(stageviz::Style::Open).isNull(), "style resolves icon pixmap");

    const QString sheet = QStringLiteral("QWidget { color: rgba(1, 2, 3, 255); }");
    style.setStyleSheet(sheet);
    require(style.styleSheet() == sheet, "style stylesheet roundtrip");
    style.refresh();

    const QColorSpace srgb = QColorSpace::SRgb;
    style.setColorSpace(srgb);
    require(style.colorSpace() == srgb, "style color space roundtrip");
}

void
renderEngineApi()
{
    const UsdStageRefPtr stage = UsdStage::CreateInMemory();
    require(bool(stage), "create render stage");
    UsdGeomCube::Define(stage, SdfPath("/Cube")).CreateSizeAttr(VtValue(2.0));

    const UsdStageRefPtr auxiliary = UsdStage::CreateInMemory();
    require(bool(auxiliary), "create auxiliary render stage");
    UsdGeomCube::Define(auxiliary, SdfPath("/Guide")).CreateSizeAttr(VtValue(0.25));

    GfCamera camera;
    GfMatrix4d view(1.0);
    view.SetLookAt(GfVec3d(0.0, 0.0, 5.0), GfVec3d(0.0, 0.0, 0.0), GfVec3d(0.0, 1.0, 0.0));
    camera.SetTransform(view.GetInverse());
    camera.SetPerspectiveFromAspectRatioAndFieldOfView(1.0, 45.0, GfCamera::FOVVertical);
    camera.SetClippingRange(GfRange1f(0.1f, 1000.0f));

    stageviz::RenderEngine engine(stageviz::RenderEngine::ContextMode::Offscreen);
    engine.setStage(stage);
    engine.setAuxiliaryStage(auxiliary);
    engine.setCamera(camera);
    engine.setSize(GfVec2i(96, 96));
    engine.setViewport(GfVec4d(0.0, 0.0, 96.0, 96.0));

    stageviz::RenderEngine::Settings settings;
    settings.sceneLightsEnabled = false;
    settings.sceneMaterialsEnabled = true;
    settings.defaultCameraLightEnabled = true;
    settings.defaultDomeLightEnabled = false;
    settings.complexity = 1.0;
    engine.setSettings(settings);
    engine.setMask({ SdfPath("/Cube") });
    engine.setSelected({ SdfPath("/Cube") });
    engine.setSelectionColor(QColor(255, 200, 0));

    require(engine.stage() == stage, "render engine stage roundtrip");
    require(engine.auxiliaryStage() == auxiliary, "render engine auxiliary roundtrip");
    require(engine.size() == GfVec2i(96, 96), "render engine size roundtrip");
    require(engine.viewport() == GfVec4d(0.0, 0.0, 96.0, 96.0), "render engine viewport roundtrip");
    require(engine.settings().defaultCameraLightEnabled, "render engine settings roundtrip");
    require(engine.camera().GetTransform() == camera.GetTransform(), "render engine camera roundtrip");

    require(engine.initialize(), "initialize offscreen render engine");
    require(engine.isInitialized(), "render engine reports initialized");
    engine.refreshAuxiliaryStage();

    const QImage image = engine.renderImage();
    require(!image.isNull(), "offscreen render returns image");
    require(image.width() == 96 && image.height() == 96, "offscreen render respects requested size");
    require(!engine.hgiApiName().isEmpty(), "render engine exposes Hgi API");
    require(!engine.rendererAovs().isEmpty(), "render engine exposes renderer AOVs");
    (void)engine.renderStats();
    (void)engine.isColorCorrectionCapable();

    engine.setMask({});
    engine.setSelected({});
    engine.setAuxiliaryStage(nullptr);
    require(!engine.auxiliaryStage(), "render engine clears auxiliary stage");
    engine.reset();
    require(!engine.isInitialized(), "render engine reset releases renderer");
}

void
materialBrowserApi()
{
    stageviz::MaterialBrowser browser;

    stageviz::MaterialEntry first;
    first.materialPath = SdfPath("/Materials/First");
    first.shaderPath = SdfPath("/Materials/First/PreviewSurface");
    first.name = QStringLiteral("First");
    first.shaderId = QStringLiteral("UsdPreviewSurface");

    stageviz::MaterialEntry second;
    second.materialPath = SdfPath("/Materials/Second");
    second.shaderPath = SdfPath("/Materials/Second/StandardSurface");
    second.name = QStringLiteral("Second");
    second.shaderId = QStringLiteral("ND_standard_surface_surfaceshader");

    browser.setEntries({ first, second });
    require(browser.entries().size() == 2, "material browser stores entries");
    require(browser.entry(0) && browser.entry(0)->materialPath == first.materialPath,
            "material browser exposes entry by row");
    require(browser.entry(-1) == nullptr && browser.entry(99) == nullptr,
            "material browser rejects invalid entry rows");
    require(browser.rowForMaterialPath(second.materialPath) == 1, "material browser finds row from material path");

    int selectionChanges = 0;
    QObject::connect(&browser, &stageviz::MaterialBrowser::selectionChanged, &browser, [&]() { ++selectionChanges; });
    browser.selectRow(1);
    require(browser.selectedRows() == QList<int>({ 1 }), "material browser selects one source row");
    require(browser.selectedEntries().size() == 1
                && browser.selectedEntries().first().materialPath == second.materialPath,
            "material browser returns selected entry");
    browser.selectRows({ 0, 1 });
    const QList<int> selectedRows = browser.selectedRows();
    require(selectedRows.contains(0) && selectedRows.contains(1) && selectedRows.size() == 2,
            "material browser supports multi-selection");
    require(selectionChanges > 0, "material browser emits selection changes");

    browser.setViewMode(stageviz::MaterialBrowser::List);
    require(browser.viewMode() == stageviz::MaterialBrowser::List, "material browser switches to list mode");
    browser.setViewMode(stageviz::MaterialBrowser::Details);
    require(browser.viewMode() == stageviz::MaterialBrowser::Details, "material browser switches to details mode");
    browser.setViewMode(stageviz::MaterialBrowser::Icons);
    require(browser.viewMode() == stageviz::MaterialBrowser::Icons, "material browser switches back to icon mode");

    browser.setFilter(QStringLiteral("Second"));
    require(browser.filter() == QStringLiteral("Second"), "material browser filter roundtrip");
    browser.setFilter(QString());
    require(browser.filter().isEmpty(), "material browser filter clears");

    browser.setSwatchSize(144);
    require(browser.swatchSize() == 144, "material browser swatch size roundtrip");
    QImage swatch(32, 32, QImage::Format_RGBA8888);
    swatch.fill(QColor(20, 40, 60, 255));
    browser.setSwatch(0, swatch);
    require(!browser.swatch(0).isNull() && browser.swatch(0).size() == QSize(32, 32),
            "material browser caches swatch image");
    browser.invalidateSwatch(0);
    require(!browser.swatch(0).isNull(), "invalidating swatch keeps previous image visible");

    const SdfPath renamed("/Materials/Renamed");
    browser.remapEntryPath(first.materialPath, renamed);
    require(browser.rowForMaterialPath(first.materialPath) == -1 && browser.rowForMaterialPath(renamed) == 0,
            "material browser remaps renamed material path");

    stageviz::MaterialEntry updated = *browser.entry(0);
    updated.name = QStringLiteral("Updated");
    require(browser.updateEntry(0, updated), "material browser updates one row");
    require(browser.entry(0) && browser.entry(0)->name == QStringLiteral("Updated"),
            "material browser exposes updated row");
    require(!browser.updateEntry(10, updated), "material browser rejects invalid row update");
}


void
geometrySubsetGuiApi()
{
    const auto stage = UsdStage::CreateInMemory();
    const auto mesh = UsdGeomMesh::Define(stage, SdfPath("/Mesh"));
    mesh.CreateFaceVertexCountsAttr().Set(VtArray<int> { 3, 3 });
    mesh.CreateFaceVertexIndicesAttr().Set(VtArray<int> { 0, 1, 2, 0, 2, 3 });
    mesh.CreatePointsAttr().Set(
        VtArray<GfVec3f> { GfVec3f(0, 0, 0), GfVec3f(1, 0, 0), GfVec3f(1, 1, 0), GfVec3f(0, 1, 0) });
    const auto subset = UsdGeomSubset::CreateGeomSubset(mesh, TfToken("Front"), UsdGeomTokens->face, VtArray<int> { 0 },
                                                        TfToken("materialBind"));
    const auto material = UsdShadeMaterial::Define(stage, SdfPath("/FrontMaterial"));
    require(bool(subset) && bool(material), "GUI subset fixture created");
    require(UsdShadeMaterialBindingAPI::Apply(subset.GetPrim()).Bind(material), "GUI subset material bound");
    require(UsdShadeMaterialBindingAPI(subset.GetPrim()).ComputeBoundMaterial().GetPath() == material.GetPath(),
            "GUI subset resolves material");
    QCoreApplication::processEvents();
    require(bool(stage->GetPrimAtPath(SdfPath("/Mesh/Front"))), "GUI event processing retains subset");
}

void
treeWidgetSelectionApi()
{
    stageviz::TreeWidget tree;
    tree.setColumnCount(1);
    auto* root = new QTreeWidgetItem(&tree, QStringList { "root" });
    auto* a = new QTreeWidgetItem(root, QStringList { "a" });
    auto* b = new QTreeWidgetItem(root, QStringList { "b" });
    tree.expandAll();
    tree.setSelectionMode(QAbstractItemView::ExtendedSelection);

    // TreeWidget deliberately uses Qt's native selection model.
    a->setSelected(true);
    require(tree.isItemSelected(a), "TreeWidget recognizes native selection");
    require(tree.selectedItems().size() == 1, "TreeWidget selects one row");
    require(tree.selectionModel()->hasSelection(), "TreeWidget updates native selection model");

    b->setSelected(true);
    require(tree.selectedItems().size() == 2, "TreeWidget supports native multi-selection");
    require(!tree.hasExternalSelection(), "TreeWidget does not use external selection");

    tree.clearSelection();
    require(tree.selectedItems().isEmpty(), "TreeWidget clears native selection");
    require(!tree.selectionModel()->hasSelection(), "TreeWidget native model is cleared");
}

void
indexTreeSelectionApi()
{
    stageviz::IndexTree tree;
    tree.setColumnCount(1);
    auto* root = new QTreeWidgetItem(&tree, QStringList { "root" });
    auto* a = new QTreeWidgetItem(root, QStringList { "a" });
    auto* b = new QTreeWidgetItem(root, QStringList { "b" });
    auto* nested = new QTreeWidgetItem(a, QStringList { "nested" });
    root->setData(0, Qt::UserRole, "/root");
    a->setData(0, Qt::UserRole, "/root/a");
    b->setData(0, Qt::UserRole, "/root/b");
    nested->setData(0, Qt::UserRole, "/root/a/nested");
    tree.expandAll();
    tree.setSelectionMode(QAbstractItemView::ExtendedSelection);

    // Exercise the concrete IndexTree rather than its TreeWidget base.
    tree.setSelection(QSet<QString> { "/root/a/nested", "/missing" }, QSet<QString> { "/root", "/root/a" },
                      Qt::UserRole);
    require(tree.selectedItemCount() == 2, "IndexTree retains uninstantiated selected paths");
    require(tree.isItemSelected(nested) && !tree.isItemSelected(b), "IndexTree resolves path selection");
    require(tree.hasSelectedDescendants(root) && tree.hasSelectedDescendants(a), "IndexTree tracks selected ancestors");
    require(!tree.selectionModel()->hasSelection(), "IndexTree does not populate native selection");

    tree.setItemSelected(b, true);
    require(tree.isItemSelected(b) && tree.isItemSelected(nested), "IndexTree edits path selection");
    require(tree.selectedItemCount() == 2, "IndexTree converts paths to instantiated pointers");
    tree.setItemSelected(nested, false);
    require(tree.selectedItemCount() == 1 && tree.isItemSelected(b), "IndexTree deselects one item");
    tree.clearSelection();
    require(tree.selectedItemCount() == 0 && !tree.hasSelectedDescendants(root),
            "IndexTree clears selection and ancestor cache");

    tree.setSelectionItems(QSet<QTreeWidgetItem*> { a, b }, QSet<QTreeWidgetItem*> { root });
    require(tree.selectedItemCount() == 2 && tree.hasSelectedDescendants(root), "IndexTree pointer-backed selection");
    tree.selectAll();
    require(tree.selectedItemCount() == 4, "IndexTree selectAll includes descendants");
    tree.clearSelection();
    require(!tree.selectionModel()->hasSelection(), "IndexTree native selection remains empty");
}

void
indexTreeSelectionModesApi()
{
    stageviz::IndexTree tree;
    tree.setColumnCount(2);
    tree.setColumnSelectable(0, true);
    tree.setColumnSelectable(1, false);
    require(tree.columnSelectable(0) && !tree.columnSelectable(1), "selection respects selectable columns");
    auto* root = new QTreeWidgetItem(&tree, QStringList { "root", "state" });
    auto* a = new QTreeWidgetItem(root, QStringList { "a", "state" });
    auto* b = new QTreeWidgetItem(root, QStringList { "b", "state" });
    tree.expandAll();
    tree.setSelectionMode(QAbstractItemView::ExtendedSelection);

    a->setSelected(true);
    require(!tree.hasExternalSelection() && tree.isItemSelected(a), "native mode uses Qt selection");
    tree.setSelectionItems({ a }, { root });
    require(tree.hasExternalSelection() && tree.isItemSelected(a), "pointer mode activates");
    require(tree.hasSelectedDescendants(root), "pointer mode ancestor highlight");
    require(!tree.selectionModel()->hasSelection(), "pointer mode clears native selection");
    tree.setItemSelected(b, true);
    require(tree.selectedItemCount() == 2, "pointer mode adds second selection");
    tree.setSelection({ "/root/b" }, { "/root" }, Qt::UserRole);
    root->setData(0, Qt::UserRole, "/root");
    b->setData(0, Qt::UserRole, "/root/b");
    require(tree.isItemSelected(b) && !tree.isItemSelected(a), "path mode replaces pointer mode");
    require(tree.selectedItemCount() == 1, "path mode counts selected paths");
    tree.clearSelection();
    require(!tree.hasExternalSelection() && tree.selectedItemCount() == 0, "clearSelection exits external modes");
}

}  // namespace

int
main(int argc, char** argv)
{
    stageviz::Application app(argc, argv);
    const std::map<std::string, std::function<void()>> cases {
        { "settings_api", settingsApi },
        { "treewidget_selection_api", treeWidgetSelectionApi },
        { "indextree_selection_api", indexTreeSelectionApi },
        { "indextree_selection_modes_api", indexTreeSelectionModesApi },
        { "style_api", styleApi },
        { "renderengine_api", renderEngineApi },
        { "material_browser_api", materialBrowserApi },
        { "geometry_subset_gui_api", geometrySubsetGuiApi },
    };

    if (argc != 2 || !cases.count(argv[1])) {
        std::cerr << "Pass one registered GUI regression case name\n";
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
