// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell

#include "application.h"
#include <fstream>
#include <QTreeWidgetItemIterator>
#include "bboxutils.h"
#include "commandlight.h"
#include "commandlayer.h"
#include "commandmaterial.h"
#include "commandpayload.h"
#include "commandpivot.h"
#include "commandselection.h"
#include "commandstack.h"
#include "commandtransform.h"
#include "indextree.h"
#include "layerdialog.h"
#include "layertree.h"
#include "materialbrowser.h"
#include "materialutils.h"
#include "outliner/outlinertree.h"
#include "outliner/outlinerview.h"
#include "outliner/primitem.h"
#include "paths.h"
#include "propertyitem.h"
#include "propertytree.h"
#include "propertyview.h"
#include "renderengine.h"
#include "session.h"
#include "settings.h"
#include "spinbox.h"
#include "treewidget.h"
#include "ui.h"
#include "viewcamera.h"
#include "viewcontext.h"
#include "viewportauxiliary.h"
#include "viewportgizmo.h"
#include "viewporthost.h"
#include "viewportlights.h"
#include "viewportselection.h"
#include "viewporttransform.h"
#include "viewportwidget.h"
#include "viewstate.h"
#include <QApplication>
#include <QColor>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QImage>
#include <QItemSelectionModel>
#include <QKeyEvent>
#include <QLineEdit>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLFramebufferObject>
#include <QPainter>
#include <QSet>
#include <QSettings>
#include <QSurfaceFormat>
#include <QTemporaryDir>
#include <QThread>
#include <QThreadPool>
#include <QTreeWidgetItem>
#include <functional>
#include <iostream>
#include <map>
#include <pxr/base/gf/camera.h>
#include <pxr/base/gf/matrix4d.h>
#include <pxr/base/gf/range1f.h>
#include <pxr/base/gf/rotation.h>
#include <pxr/base/gf/vec3d.h>
#include <pxr/usd/usd/payloads.h>
#include <pxr/usd/usd/prim.h>
#include <pxr/usd/usd/stage.h>
#include <pxr/usd/usdGeom/basisCurves.h>
#include <pxr/usd/usdGeom/cube.h>
#include <pxr/usd/usdGeom/mesh.h>
#include <pxr/usd/usdGeom/subset.h>
#include <pxr/usd/usdGeom/xform.h>
#include <pxr/usd/usdShade/material.h>
#include <pxr/usd/usdShade/materialBindingAPI.h>
#include <pxr/usd/usdShade/shader.h>
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
uiApi()
{
    stageviz::UI ui;
    int colorChanges = 0;
    QObject::connect(&ui, &stageviz::UI::colorChanged, &ui, [&](stageviz::UI::ColorRole) { ++colorChanges; });

    const QColor accent(11, 22, 33, 255);
    ui.setColor(stageviz::UI::Accent, accent);
    require(ui.color(stageviz::UI::Accent) == accent, "ui color roundtrip");
    require(colorChanges == 1, "ui color signal emitted");

    ui.setFontSize(stageviz::UI::Small, 11);
    ui.setFontSize(stageviz::UI::Medium, 13);
    ui.setFontSize(stageviz::UI::Large, 17);
    require(ui.fontSize(stageviz::UI::Small) == 11 && ui.fontSize(stageviz::UI::Medium) == 13
                && ui.fontSize(stageviz::UI::Large) == 17,
            "ui font size roundtrip");

    ui.setIconSize(stageviz::UI::Small, 16);
    ui.setIconSize(stageviz::UI::Medium, 24);
    ui.setIconSize(stageviz::UI::Large, 32);
    require(ui.iconSize(stageviz::UI::Small) == 16 && ui.iconSize(stageviz::UI::Medium) == 24
                && ui.iconSize(stageviz::UI::Large) == 32,
            "ui icon size roundtrip");

    require(ui.gizmoSize(stageviz::UI::Small) == 32 && ui.gizmoSize(stageviz::UI::Medium) == 64
                && ui.gizmoSize(stageviz::UI::Large) == 128,
            "UI gizmo size defaults");
    ui.setGizmoSize(stageviz::UI::Small, 24);
    ui.setGizmoSize(stageviz::UI::Medium, 48);
    ui.setGizmoSize(stageviz::UI::Large, 96);
    require(ui.gizmoSize(stageviz::UI::Small) == 24 && ui.gizmoSize(stageviz::UI::Medium) == 48
                && ui.gizmoSize(stageviz::UI::Large) == 96,
            "UI gizmo size roundtrip");

    require(ui.axisSize(stageviz::UI::Small) == 36 && ui.axisSize(stageviz::UI::Medium) == 72
                && ui.axisSize(stageviz::UI::Large) == 144,
            "UI corner-axis size defaults preserve the existing medium size");
    int axisSizeChanges = 0;
    QObject::connect(&ui, &stageviz::UI::axisSizeChanged, &ui, [&](stageviz::UI::UIScale) { ++axisSizeChanges; });
    ui.setAxisSize(stageviz::UI::Medium, 96);
    ui.setAxisSize(stageviz::UI::Medium, 96);
    ui.setAxisSize(stageviz::UI::Medium, 0);
    ui.setAxisSize(stageviz::UI::Medium, -1);
    require(ui.axisSize(stageviz::UI::Medium) == 96 && axisSizeChanges == 1,
            "axis size signals real changes and rejects non-positive sizes");

    const QString originalIcon = ui.iconPath(stageviz::UI::Open);
    require(!originalIcon.isEmpty(), "ui exposes icon resource path");
    ui.setIconPath(stageviz::UI::Open, originalIcon);
    require(ui.iconPath(stageviz::UI::Open) == originalIcon, "ui icon path roundtrip");
    require(!ui.icon(stageviz::UI::Open).isNull(), "ui resolves icon pixmap");

    const QString sheet = QStringLiteral("QWidget { color: rgba(1, 2, 3, 255); }");
    ui.setStyleSheet(sheet);
    require(ui.styleSheet() == sheet, "UI stylesheet roundtrip");
    ui.refresh();

    require(ui.transformMoveSnap() == 1.0 && ui.transformRotateSnap() == 1.0 && ui.transformScaleSnap() == 1.0,
            "UI transform snap defaults");
    ui.setTransformMoveSnap(0.25);
    ui.setTransformRotateSnap(5.0);
    ui.setTransformScaleSnap(0.5);
    require(ui.transformMoveSnap() == 0.25 && ui.transformRotateSnap() == 5.0 && ui.transformScaleSnap() == 0.5,
            "UI transform snap roundtrip");

    const QColorSpace srgb = QColorSpace::SRgb;
    ui.setColorSpace(srgb);
    require(ui.colorSpace() == srgb, "ui color space roundtrip");
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
    require(engine.isConverged(), "an uninitialized renderer has no pending maintenance");
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

    // Viewport outlines are intentionally disabled in offscreen export mode.
    // Exercise the viewport engine in a real current GL context and framebuffer.
    QSurfaceFormat format;
    format.setDepthBufferSize(24);
    format.setStencilBufferSize(8);
    QOpenGLContext context;
    context.setFormat(format);
    require(context.create(), "create viewport selection GL context");
    QOffscreenSurface surface;
    surface.setFormat(context.format());
    surface.create();
    require(surface.isValid() && context.makeCurrent(&surface), "activate viewport selection GL context");
    {
        const auto subset = UsdGeomSubset::Define(stage, SdfPath("/Cube/Faces"));
        subset.CreateElementTypeAttr().Set(UsdGeomTokens->face);
        subset.CreateIndicesAttr().Set(VtIntArray { 0 });
        stageviz::RenderEngine viewport(stageviz::RenderEngine::ContextMode::Current);
        viewport.setStage(stage);
        viewport.setCamera(camera);
        viewport.setSettings(settings);
        viewport.setSize(GfVec2i(96, 96));
        viewport.setViewport(GfVec4d(0, 0, 96, 96));
        viewport.setSelectionColor(QColor(255, 200, 0));
        QOpenGLFramebufferObjectFormat framebufferFormat;
        framebufferFormat.setAttachment(QOpenGLFramebufferObject::CombinedDepthStencil);
        QOpenGLFramebufferObject framebuffer(96, 96, framebufferFormat);
        const auto draw = [&](const QList<SdfPath>& paths) {
            viewport.setSelected(paths);
            require(framebuffer.bind(), "bind viewport selection framebuffer");
            require(viewport.renderToCurrentFramebuffer(), "render viewport selection");
            const QImage result = framebuffer.toImage();
            framebuffer.release();
            return result;
        };
        const QImage parentImage = draw({ SdfPath("/Cube") });
        require(draw({ subset.GetPath() }) == parentImage,
                "viewport projects selected subset to the same whole-object outline as its parent");
        require(draw({}) != parentImage, "whole-object selection has a visible viewport outline");
        viewport.reset();
    }
    context.doneCurrent();
}

void
numericEditorApi()
{
    stageviz::SpinBox spin;
    spin.setLocale(QLocale::c());
    spin.setRange(-100., 100.);
    spin.setDecimals(2);
    spin.setValue(42.);
    auto* line = spin.findChild<QLineEdit*>();
    require(line && line->isReadOnly(), "numeric editor starts in scrub mode");
    for (const auto& key : { std::make_pair(Qt::Key_7, QString("7")), std::make_pair(Qt::Key_Period, QString(".")),
                             std::make_pair(Qt::Key_5, QString("5")) }) {
        QKeyEvent press(QEvent::KeyPress, key.first, Qt::NoModifier, key.second);
        QApplication::sendEvent(line, &press);
    }
    require(!line->isReadOnly() && qAbs(spin.value() - 7.5) < 1e-8, "typing replaces the old scrub-mode value");
    QKeyEvent enter(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
    QApplication::sendEvent(line, &enter);
    require(line->isReadOnly(), "Enter returns numeric editor to scrub mode");
}

void
renderEngineHdriApi()
{
    const auto stage = UsdStage::CreateInMemory();
    const auto cube = UsdGeomCube::Define(stage, SdfPath("/Cube"));
    const auto material = UsdShadeMaterial::Define(stage, SdfPath("/Material"));
    auto shader = UsdShadeShader::Define(stage, SdfPath("/Material/Surface"));
    shader.CreateIdAttr(VtValue(TfToken("UsdPreviewSurface")));
    shader.CreateInput(TfToken("diffuseColor"), SdfValueTypeNames->Color3f).Set(GfVec3f(.8f, .02f, .01f));
    auto opacity = shader.CreateInput(TfToken("opacity"), SdfValueTypeNames->Float);
    material.CreateSurfaceOutput().ConnectToSource(shader.CreateOutput(TfToken("surface"), SdfValueTypeNames->Token));
    UsdShadeMaterialBindingAPI::Apply(cube.GetPrim()).Bind(material);
    const auto auxiliary = UsdStage::CreateInMemory();
    UsdGeomXform::Define(auxiliary, stageviz::paths::auxiliary::display);

    QSurfaceFormat format;
    format.setDepthBufferSize(24);
    QOpenGLContext context;
    context.setFormat(format);
    require(context.create(), "create HDRI viewport context");
    QOffscreenSurface surface;
    surface.setFormat(context.format());
    surface.create();
    require(context.makeCurrent(&surface), "activate HDRI viewport context");
    {
        stageviz::RenderEngine engine(stageviz::RenderEngine::ContextMode::Current);
        engine.setStage(stage);
        engine.setAuxiliaryStage(auxiliary);
        GfCamera camera;
        camera.SetTransform(GfMatrix4d(1).SetTranslate(GfVec3d(0, 0, 5)));
        camera.SetPerspectiveFromAspectRatioAndFieldOfView(1., 45., GfCamera::FOVVertical);
        camera.SetClippingRange(GfRange1f(.1f, 100.f));
        engine.setCamera(camera);
        engine.setSize(GfVec2i(96));
        engine.setViewport(GfVec4d(0, 0, 96, 96));
        stageviz::RenderEngine::Settings settings;
        settings.defaultDomeLightEnabled = true;
        settings.defaultCameraLightEnabled = true;
        QOpenGLFramebufferObjectFormat fboFormat;
        fboFormat.setAttachment(QOpenGLFramebufferObject::CombinedDepthStencil);
        QOpenGLFramebufferObject fbo(96, 96, fboFormat);
        for (float alpha : { 1.f, .5f, 0.f, 1.f }) {
            opacity.Set(alpha);
            for (bool background : { false, true }) {
                settings.domeLightCameraVisibility = background;
                engine.setSettings(settings);
                require(fbo.bind() && engine.renderToCurrentFramebuffer(), "render HDRI and opacity change");
                const QImage image = fbo.toImage();
                int redPixels = 0;
                for (int y = 24; y < 72; ++y) {
                    for (int x = 24; x < 72; ++x) {
                        const QColor color = image.pixelColor(x, y);
                        redPixels += color.red() > 25 && color.red() > 1.5 * color.green()
                                     && color.red() > 1.5 * color.blue();
                    }
                }
                require(alpha > 0 ? redPixels > 100 : redPixels == 0,
                        "helper presentation preserves opaque/translucent objects and zero opacity");
            }
        }
        engine.reset();
    }
    context.doneCurrent();
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
    QTemporaryDir directory;
    const QString file = directory.filePath("subsets.usda");
    const auto fixture = UsdStage::CreateNew(file.toStdString());
    UsdGeomXform::Define(fixture, SdfPath("/World"));
    const auto mesh = UsdGeomMesh::Define(fixture, SdfPath("/World/Mesh"));
    mesh.CreateFaceVertexCountsAttr().Set(VtArray<int> { 3, 3 });
    mesh.CreateFaceVertexIndicesAttr().Set(VtArray<int> { 0, 1, 2, 0, 2, 3 });
    mesh.CreatePointsAttr().Set(
        VtArray<GfVec3f> { GfVec3f(0, 0, 0), GfVec3f(1, 0, 0), GfVec3f(1, 1, 0), GfVec3f(0, 1, 0) });
    mesh.AddTranslateOp().Set(GfVec3d(1, 2, 3));
    mesh.GetPrim().SetCustomDataByKey(TfToken("test"), VtValue(1));
    const auto front = UsdGeomSubset::CreateGeomSubset(mesh, TfToken("Front"), UsdGeomTokens->face, VtArray<int> { 0 },
                                                       TfToken("materialBind"));
    const auto back = UsdGeomSubset::CreateGeomSubset(mesh, TfToken("Back"), UsdGeomTokens->face, VtArray<int> { 1 },
                                                      TfToken("materialBind"));
    const auto red = UsdShadeMaterial::Define(fixture, SdfPath("/World/Looks/Red"));
    const auto blue = UsdShadeMaterial::Define(fixture, SdfPath("/World/Looks/Blue"));
    require(UsdShadeMaterialBindingAPI::Apply(front.GetPrim()).Bind(red), "bind front subset material");
    require(UsdShadeMaterialBindingAPI::Apply(back.GetPrim()).Bind(blue), "bind back subset material");
    const auto instance = UsdGeomXform::Define(fixture, SdfPath("/Instance")).GetPrim();
    require(instance.GetReferences().AddInternalReference(SdfPath("/World")), "reference subset fixture instance");
    instance.SetInstanceable(true);
    require(fixture->GetRootLayer()->Save(), "save subset presentation fixture");

    stageviz::OutlinerView outliner;
    stageviz::PropertyView properties;
    auto* current = stageviz::session();
    require(current->loadFromFile(file), "load subset presentation fixture");
    QCoreApplication::processEvents();
    auto* tree = outliner.findChild<stageviz::OutlinerTree*>("outlinerTree");
    auto* propertyTree = properties.findChild<stageviz::PropertyTree*>("propertyTree");
    require(tree && propertyTree, "find outliner and property trees");
    const auto findProperty = [&](const SdfPath& path) -> stageviz::PropertyItem* {
        std::function<stageviz::PropertyItem*(QTreeWidgetItem*)> find =
            [&](QTreeWidgetItem* parent) -> stageviz::PropertyItem* {
            for (int i = 0; i < parent->childCount(); ++i) {
                auto* item = dynamic_cast<stageviz::PropertyItem*>(parent->child(i));
                if (item && item->propertyPath() == path)
                    return item;
                if (auto* child = find(parent->child(i)))
                    return child;
            }
            return nullptr;
        };
        return find(propertyTree->invisibleRootItem());
    };
    const auto waitUntil = [](const std::function<bool()>& predicate) {
        QElapsedTimer timer;
        timer.start();
        do {
            QCoreApplication::processEvents();
            if (predicate())
                return true;
            QThread::msleep(10);
        } while (timer.elapsed() < 5000);
        return false;
    };
    std::function<void(QTreeWidgetItem*)> checkOutliner = [&](QTreeWidgetItem* parent) {
        for (int i = 0; i < parent->childCount(); ++i) {
            const auto* item = parent->child(i);
            const SdfPath path(item->data(0, stageviz::PrimItem::Path).toString().toStdString());
            const auto prim = current->stage()->GetPrimAtPath(path);
            require(!prim || !prim.IsA<UsdGeomSubset>(), "outliner excludes ordinary and proxy subset rows");
            checkOutliner(parent->child(i));
        }
    };
    checkOutliner(tree->invisibleRootItem());
    current->commandStack()->execute(new stageviz::Command(stageviz::selectPaths({ SdfPath("/World/Mesh") })));
    const auto sections = propertyTree->findItems("subsets", Qt::MatchExactly | Qt::MatchRecursive, 0);
    require(sections.size() == 1 && sections.first()->childCount() == 2 && sections.first()->text(1) == "2 values"
                && sections.first()->parent()->text(0) == "Attributes",
            "owning mesh presents both geometry subsets under Attributes");
    const auto topSection = [&](const QString& name) {
        const auto items = propertyTree->findItems(name, Qt::MatchExactly, 0);
        return items.isEmpty() ? static_cast<QTreeWidgetItem*>(nullptr) : items.first();
    };
    const auto* primSectionAtStart = topSection("Prim");
    const auto* compositionSectionAtStart = topSection("Composition");
    const auto* metadataSectionAtStart = topSection("Metadata");
    const auto* attributesSectionAtStart = topSection("Attributes");
    const auto* relationshipsSectionAtStart = topSection("Relationships");
    require(primSectionAtStart && primSectionAtStart->isExpanded() && compositionSectionAtStart
                && compositionSectionAtStart->isExpanded() && metadataSectionAtStart
                && metadataSectionAtStart->isExpanded() && attributesSectionAtStart
                && attributesSectionAtStart->isExpanded() && relationshipsSectionAtStart
                && relationshipsSectionAtStart->isExpanded(),
            "top-level property sections start expanded");
    require(!sections.first()->isExpanded(), "nested subsets starts collapsed");
    bool attributeCountOk = false;
    bool relationshipCountOk = false;
    attributesSectionAtStart->text(1).toInt(&attributeCountOk);
    relationshipsSectionAtStart->text(1).toInt(&relationshipCountOk);
    require(attributeCountOk && relationshipCountOk, "Attributes and Relationships show their item counts");
    propertyTree->expandItem(sections.first()->parent());
    auto* transform = findProperty(SdfPath("/World/Mesh.xformOp:translate"));
    require(transform, "mesh exposes its transform property");
    propertyTree->collapseItem(transform);
    propertyTree->expandItem(transform);
    auto* attributes = sections.first()->parent();
    propertyTree->collapseItem(attributes);
    propertyTree->expandItem(attributes);
    auto* primSection = propertyTree->findItems("Prim", Qt::MatchExactly, 0).first();
    propertyTree->collapseItem(primSection);
    const SdfPath indicesPath("/World/Mesh/Front.indices");
    auto* indices = findProperty(indicesPath);
    require(indices && indices->text(1) == "1 values", "subset indices show their array size");
    propertyTree->expandItem(indices);
    require(indices->childCount() == 1 && indices->child(0)->text(1) == "0", "expanding indices loads face indices");
    auto* binding = findProperty(SdfPath("/World/Mesh/Front.material:binding"));
    require(binding && binding->valuePath() == red.GetPath(), "subset property row exposes its resolved material");

    const auto drain = []() {
        for (int i = 0; i < 6; ++i) {
            QThreadPool::globalInstance()->waitForDone();
            QCoreApplication::sendPostedEvents(nullptr, 0);
            QCoreApplication::processEvents();
        }
    };
    // Editing the property uses the existing undoable array editor.
    current->commandStack()->clear();
    indices->child(0)->setText(1, "1");
    drain();
    const auto authoredIndices = [&]() {
        VtIntArray value;
        current->stage()->GetAttributeAtPath(indicesPath).Get(&value);
        return value;
    };
    require(waitUntil([&]() { return authoredIndices() == VtIntArray { 1 }; }),
            "subset indices edit authors the property");
    require(current->commandStack()->canUndo(), "subset property edit registers an undo command");
    current->commandStack()->undo();
    drain();
    require(waitUntil([&]() { return authoredIndices() == VtIntArray { 0 }; }), "undo restores subset indices");
    require(!current->commandStack()->canUndo(), "one array edit creates exactly one undo step");
    current->commandStack()->redo();
    drain();
    require(waitUntil([&]() { return authoredIndices() == VtIntArray { 1 }; }), "redo restores subset indices edit");
    current->commandStack()->execute(
        new stageviz::Command(stageviz::bindMaterial({ front.GetPath() }, blue.GetPath())));
    require(waitUntil([&]() {
                auto* item = findProperty(SdfPath("/World/Mesh/Front.material:binding"));
                return item && item->valuePath() == blue.GetPath();
            }),
            "material-binding notices refresh the owning mesh properties");
    checkOutliner(tree->invisibleRootItem());

    current->commandStack()->execute(new stageviz::Command(stageviz::selectPaths({ SdfPath("/Instance/Mesh") })));
    attributes = propertyTree->findItems("Attributes", Qt::MatchExactly, 0).first();
    transform = findProperty(SdfPath("/Instance/Mesh.xformOp:translate"));
    primSection = propertyTree->findItems("Prim", Qt::MatchExactly, 0).first();
    require(attributes->isExpanded() && transform && transform->isExpanded() && !primSection->isExpanded(),
            "property expansion state persists across prim selections");
    indices = findProperty(SdfPath("/Instance/Mesh/Front.indices"));
    require(indices, "proxy geometry exposes subset properties");
    propertyTree->expandItem(indices);
    auto* element = dynamic_cast<stageviz::PropertyItem*>(indices->child(0));
    require(element && !element->valueEditable(), "proxy subset indices are inspectable and read-only");

    tree->expandAll();
    QCoreApplication::processEvents();
    const auto selectedRows = [&]() {
        QList<SdfPath> result;
        std::function<void(QTreeWidgetItem*)> visit = [&](QTreeWidgetItem* parent) {
            for (int i = 0; i < parent->childCount(); ++i) {
                auto* child = parent->child(i);
                if (tree->isItemSelected(child))
                    result.append(SdfPath(child->data(0, stageviz::PrimItem::Path).toString().toStdString()));
                visit(child);
            }
        };
        visit(tree->invisibleRootItem());
        return result;
    };
    for (const SdfPath& owner : { SdfPath("/World/Mesh"), SdfPath("/Instance/Mesh") }) {
        const SdfPath subsetPath = owner.AppendChild(TfToken("Front"));
        current->commandStack()->execute(new stageviz::Command(stageviz::selectPaths({ subsetPath })));
        QCoreApplication::processEvents();
        require(current->selectionList()->paths() == QList<SdfPath> { subsetPath },
                "Session preserves selected ordinary and proxy subset paths after UI projection");
        require(selectedRows() == QList<SdfPath> { owner }, "Outliner represents subset selection on its parent mesh");
        auto* directIndices = findProperty(subsetPath.AppendProperty(TfToken("indices")));
        require(directIndices && directIndices->parent()->text(0) == "Attributes",
                "PropertyTree inspects selected subset directly");
        require(propertyTree->findItems("subsets", Qt::MatchExactly | Qt::MatchRecursive, 0).isEmpty(),
                "selected subset does not become its parent's property inspector");
        current->commandStack()->execute(
            new stageviz::Command(stageviz::selectPaths({ subsetPath, owner.AppendChild(TfToken("Back")) })));
        QCoreApplication::processEvents();
        require(current->selectionList()->paths().size() == 2 && selectedRows() == QList<SdfPath> { owner },
                "multiple stored subset paths project to one Outliner row without feedback");
    }
    current->commandStack()->execute(new stageviz::Command(stageviz::selectPaths({ mesh.GetPath() })));
    auto* subsetIndices = findProperty(front.GetPath().AppendProperty(TfToken("indices")));
    auto* subsetRow = dynamic_cast<stageviz::PropertyItem*>(subsetIndices ? subsetIndices->parent() : nullptr);
    require(subsetRow && subsetRow->valuePath() == front.GetPath(),
            "subset property group exposes its exact path for the Select action");
}

void
outlinerInstancePayloadResyncApi()
{
    QTemporaryDir dir;
    require(dir.isValid(), "temporary instance payload directory");

    const QString assetPath = dir.filePath(QStringLiteral("payload_asset.usda"));
    const auto assetStage = UsdStage::CreateNew(assetPath.toStdString());
    require(bool(assetStage), "create payload asset stage");
    assetStage->DefinePrim(SdfPath("/Geometry"), TfToken("Xform"));
    assetStage->SetDefaultPrim(assetStage->GetPrimAtPath(SdfPath("/Geometry")));
    require(assetStage->GetRootLayer()->Save(), "save payload asset stage");

    const QString scenePath = dir.filePath(QStringLiteral("instance_payload_scene.usda"));
    const auto sourceStage = UsdStage::CreateNew(scenePath.toStdString());
    require(bool(sourceStage), "create instance payload scene");
    sourceStage->DefinePrim(SdfPath("/World"), TfToken("Xform"));
    const UsdPrim source = sourceStage->DefinePrim(SdfPath("/World/Source"), TfToken("Xform"));
    source.SetInstanceable(true);

    const UsdPrim first = sourceStage->DefinePrim(SdfPath("/World/First"), TfToken("Xform"));
    require(first.GetReferences().AddInternalReference(source.GetPath()), "author first instance reference");
    first.SetInstanceable(true);
    const UsdPrim second = sourceStage->DefinePrim(SdfPath("/World/Second"), TfToken("Xform"));
    require(second.GetReferences().AddInternalReference(source.GetPath()), "author second instance reference");
    second.SetInstanceable(true);

    const UsdPrim payload = sourceStage->DefinePrim(SdfPath("/World/Source/GeometryPayload"), TfToken("Xform"));
    require(payload.GetPayloads().AddPayload(assetPath.toStdString(), SdfPath("/Geometry")),
            "author payload below instance source");
    require(sourceStage->GetRootLayer()->Save(), "save instance payload scene");

    stageviz::Session* currentSession = stageviz::session();
    require(currentSession, "application session is available");
    stageviz::OutlinerView view;
    require(currentSession->loadFromFile(scenePath, stageviz::Session::None), "load instance payload scene unloaded");
    QCoreApplication::processEvents();

    auto* tree = view.findChild<stageviz::OutlinerTree*>(QStringLiteral("outlinerTree"));
    require(tree, "outliner view contains its OutlinerTree");
    require(tree->payloadEnabled(), "Outliner enables payload rows for LoadNone stages");

    const SdfPath firstPayloadPath("/World/First/GeometryPayload");
    const SdfPath secondPayloadPath("/World/Second/GeometryPayload");
    const auto findPathItem = [](QTreeWidget* widget, const SdfPath& path) -> QTreeWidgetItem* {
        const QString wanted = QString::fromStdString(path.GetString());
        std::function<QTreeWidgetItem*(QTreeWidgetItem*)> find = [&](QTreeWidgetItem* parent) {
            const int count = parent ? parent->childCount() : widget->topLevelItemCount();
            for (int i = 0; i < count; ++i) {
                QTreeWidgetItem* item = parent ? parent->child(i) : widget->topLevelItem(i);
                if (item->data(0, stageviz::PrimItem::Path).toString() == wanted)
                    return item;
                if (QTreeWidgetItem* child = find(item))
                    return child;
            }
            return static_cast<QTreeWidgetItem*>(nullptr);
        };
        return find(nullptr);
    };
    const auto drainWorkers = []() {
        for (int i = 0; i < 6; ++i) {
            QThreadPool::globalInstance()->waitForDone();
            QCoreApplication::sendPostedEvents(nullptr, 0);
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        }
    };
    const auto waitUntil = [](const std::function<bool()>& predicate) {
        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < 10000) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
            if (predicate())
                return true;
            QThread::msleep(10);
        }
        return predicate();
    };

    const UsdStageRefPtr stage = currentSession->stage();
    require(stage->GetPrimAtPath(firstPayloadPath).IsInstanceProxy(), "first payload is an instance proxy");
    require(stage->GetPrimAtPath(secondPayloadPath).IsInstanceProxy(), "second payload is an instance proxy");
    require(!stage->GetPrimAtPath(firstPayloadPath).IsLoaded(), "first instance payload starts unloaded");
    require(!stage->GetPrimAtPath(secondPayloadPath).IsLoaded(), "sibling instance payload starts unloaded");
    require(findPathItem(tree, firstPayloadPath), "Outliner shows the unloaded first instance payload");
    require(findPathItem(tree, firstPayloadPath)->checkState(0) == Qt::Unchecked,
            "Outliner marks the first instance payload as unloaded");

    currentSession->commandStack()->execute(new stageviz::Command(stageviz::loadPayloads({ firstPayloadPath })));
    drainWorkers();
    require(waitUntil([&]() {
                QTreeWidgetItem* item = findPathItem(tree, firstPayloadPath);
                return stage->GetPrimAtPath(firstPayloadPath).IsLoaded() && item && item->checkState(0) == Qt::Checked;
            }),
            "Outliner marks the loaded instance payload row checked");
    require(!stage->GetPrimAtPath(secondPayloadPath).IsLoaded(), "loading one instance leaves its sibling unloaded");
    require(findPathItem(tree, secondPayloadPath)->checkState(0) == Qt::Unchecked,
            "Outliner leaves the sibling instance payload unchecked");

    currentSession->commandStack()->undo();
    drainWorkers();
    require(waitUntil([&]() {
                QTreeWidgetItem* item = findPathItem(tree, firstPayloadPath);
                return !stage->GetPrimAtPath(firstPayloadPath).IsLoaded() && item
                       && item->checkState(0) == Qt::Unchecked;
            }),
            "Outliner marks the unloaded instance payload row unchecked after undo");

    currentSession->commandStack()->redo();
    drainWorkers();
    require(waitUntil([&]() {
                QTreeWidgetItem* item = findPathItem(tree, firstPayloadPath);
                return stage->GetPrimAtPath(firstPayloadPath).IsLoaded() && item && item->checkState(0) == Qt::Checked;
            }),
            "Outliner restores the loaded instance payload row after redo");
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

void
layerTreeRootRowApi()
{
    stageviz::Session* currentSession = stageviz::session();
    require(currentSession, "application session is available");
    require(currentSession->newStage(), "create stage for layer tree");

    stageviz::LayerDialog dialog;
    stageviz::LayerTree* tree = dialog.findChild<stageviz::LayerTree*>(QStringLiteral("layerTree"));
    require(tree, "layer dialog contains its LayerTree");
    require(tree->topLevelItemCount() == 1, "new stage root layer is attached as a top-level row");

    QTreeWidgetItem* rootItem = tree->topLevelItem(0);
    require(rootItem && rootItem->text(0).contains(QStringLiteral("(Root)")),
            "new stage root layer row is labeled as the root");
    require(rootItem->text(0).contains(QStringLiteral("Untitled")), "anonymous root layer is displayed as Untitled");
    require(rootItem->isExpanded(), "layer tree keeps the root row expanded");
    rootItem->setExpanded(false);
    QCoreApplication::processEvents();
    require(rootItem->isExpanded(), "layer tree restores root expansion after a collapse request");
}

// Supply camera and stage services without initializing a GL render delegate.
class ViewportTestHost : public stageviz::ViewportHost {
public:
    ViewportTestHost()
    {
        auto* current = stageviz::session();
        editContext.setStageLock(current->stageLock());
        editContext.setCommandStack(current->commandStack());
        editContext.setSelectionList(current->selectionList());
        editContext.setViewState(current->viewState());
        viewport.resize(640, 480);
    }

    stageviz::ViewportWidget* widget() const override { return const_cast<stageviz::ViewportWidget*>(&viewport); }
    stageviz::ViewContext* context() const override { return const_cast<stageviz::ViewContext*>(&editContext); }
    UsdStageRefPtr stage() const override { return stageviz::session()->stageUnsafe(); }
    UsdStageRefPtr auxiliaryStage() const override { return auxiliary; }
    stageviz::RenderEngine* renderEngine() const override { return nullptr; }
    stageviz::ViewCamera* viewCamera() override { return viewState()->camera(); }
    stageviz::ViewState* viewState() override { return stageviz::session()->viewState(); }
    const QList<SdfPath>& selectedPaths() const override { return selected; }
    const QList<SdfPath>& maskPaths() const override { return mask; }
    bool pathMaskedIn(const SdfPath& path) const override { return selection->isPathMaskedIn(path); }
    QRectF cameraGateRect() override { return QRectF(0, 0, 640, 480); }
    GfVec4d widgetViewport() override { return GfVec4d(0, 0, 640, 480); }
    GfVec2i widgetSize() const override { return GfVec2i(640, 480); }
    bool projectWorldToScreen(const GfVec3d& world, QPointF& screen) override
    {
        screen = QPointF(320 + world[0] * 10, 240 - world[1] * 10);
        return true;
    }
    void updateOverlay() override {}
    void syncSelection(const QList<SdfPath>& paths) override
    {
        selected = paths;
        selection->invalidateVisibleSelection();
    }

    stageviz::ViewportWidget viewport;
    stageviz::ViewContext editContext;
    UsdStageRefPtr auxiliary = UsdStage::CreateInMemory();
    QList<SdfPath> selected;
    QList<SdfPath> mask;
    stageviz::ViewportSelection* selection = nullptr;
};

void
viewportLightsApi()
{
    auto* current = stageviz::session();
    require(current->newStage(), "new light guide stage");
    const auto drainWorkers = []() {
        for (int i = 0; i < 6; ++i) {
            QThreadPool::globalInstance()->waitForDone();
            QCoreApplication::sendPostedEvents(nullptr, 0);
            QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
        }
    };
    const stageviz::LightType types[] = { stageviz::LightType::Point,   stageviz::LightType::Spot,
                                          stageviz::LightType::Distant, stageviz::LightType::Rect,
                                          stageviz::LightType::Disk,    stageviz::LightType::Cylinder,
                                          stageviz::LightType::Dome };
    QList<SdfPath> paths;
    for (int i = 0; i < 7; ++i) {
        auto command = stageviz::newLightPath(SdfPath::AbsoluteRootPath(), QString("Light%1").arg(i), types[i]);
        command.execute(current);
        drainWorkers();
        const SdfPath path(std::string("/Light") + std::to_string(i));
        paths.append(path);
        UsdGeomXformable(current->stage()->GetPrimAtPath(path)).AddTranslateOp().Set(GfVec3d(i * 4 - 12, 0, 0));
    }
    ViewportTestHost host;
    stageviz::ViewportSelection selection(host);
    host.selection = &selection;
    stageviz::ViewportLights guides(host);
    std::string before, after;
    current->stage()->GetRootLayer()->ExportToString(&before);
    QImage image(640, 480, QImage::Format_ARGB32);
    image.fill(Qt::black);
    {
        QPainter painter(&image);
        guides.draw(painter);
    }
    for (int i = 0; i < paths.size(); ++i) {
        const QPointF center(320 + (i * 4 - 12) * 10, 240);
        require(guides.pick(center) == paths[i], "light guide pick returns its USD path");
        require(guides.pathsInRect(QRectF(center - QPointF(1, 1), QSizeF(2, 2))).contains(paths[i]),
                "sweep hits light guide");
    }
    require(guides.pathsInRect(QRectF(0, 0, 640, 480)).size() == 7, "all standard light guides visible");
    host.mask = { paths.first() };
    require(guides.pathsInRect(QRectF(0, 0, 640, 480)) == host.mask, "light guides respect isolate masks");
    host.mask.clear();
    UsdGeomImageable(current->stage()->GetPrimAtPath(paths.first())).MakeInvisible();
    require(guides.pick(QPointF(200, 240)).IsEmpty(), "hidden light is not pickable");
    UsdGeomImageable(current->stage()->GetPrimAtPath(paths.first())).MakeVisible();
    current->stage()->GetRootLayer()->ExportToString(&after);
    require(before != after, "visibility fixture changed");
    // The guides themselves must never author helper geometry in the document.
    before = after;
    guides.invalidate();
    guides.pathsInRect(QRectF(0, 0, 640, 480));
    current->stage()->GetRootLayer()->ExportToString(&after);
    require(before == after, "light guide operations do not author document USD");
}

void
viewportComponentsApi()
{
    auto* current = stageviz::session();
    require(current->newStage(), "new viewport test stage");
    UsdGeomXform::Define(current->stage(), SdfPath("/World"));
    UsdGeomCube::Define(current->stage(), SdfPath("/World/Cube"));
    std::string documentBefore;
    current->stage()->GetRootLayer()->ExportToString(&documentBefore);

    ViewportTestHost host;
    stageviz::ViewportSelection selection(host);
    host.selection = &selection;
    host.selected = { SdfPath("/World"), SdfPath("/World/A/Child"), SdfPath("/World/B") };
    host.mask = { SdfPath("/World/A") };
    require(!selection.isPathMaskedIn(SdfPath("/World")) && selection.isPathMaskedIn(SdfPath("/World/A/Child")),
            "selection keeps ancestor highlighting separate from pick masks");
    require(selection.visiblePaths() == QList<SdfPath>({ SdfPath("/World"), SdfPath("/World/A/Child") }),
            "visible-selection cache includes ancestors and masked descendants");
    host.mask.clear();
    selection.invalidateVisibleSelection();
    require(selection.visiblePaths() == host.selected, "mask changes invalidate visible-selection cache");
    host.selected.clear();
    selection.reset();
    require(selection.visiblePaths().isEmpty(), "stage reset clears visible-selection cache");

    stageviz::ViewportAuxiliary auxiliary;
    auxiliary.setStage(host.auxiliary);
    auxiliary.ensureMaterials();
    host.viewState()->setGridEnabled(true);
    auxiliary.updateGrid(host.viewState());
    const SdfPath linesPath = stageviz::paths::auxiliary::grid.AppendChild(TfToken("Lines"));
    const auto gridPoints = [&]() {
        VtVec3fArray points;
        require(UsdGeomBasisCurves(host.auxiliary->GetPrimAtPath(linesPath)).GetPointsAttr().Get(&points),
                "auxiliary grid has authored points");
        return points;
    };
    for (const auto& point : gridPoints())
        require(point[1] == 0, "Y-up grid lies in the XZ plane");
    auxiliary.setUpAxis(UsdGeomTokens->z);
    auxiliary.updateGrid(host.viewState());
    for (const auto& point : gridPoints())
        require(point[2] == 0, "Z-up grid lies in the XY plane");
    host.viewState()->setGridEnabled(false);
    auxiliary.updateGrid(host.viewState());
    require(!host.auxiliary->GetPrimAtPath(stageviz::paths::auxiliary::grid),
            "disabling the grid removes auxiliary geometry");
    auxiliary.updateAxis(host.viewCamera()->camera(), true, 2.0);
    require(auxiliary.axisImage().size() == QSize(144, 144) && auxiliary.axisImage().devicePixelRatio() == 2.0,
            "camera-axis image preserves logical size on high-DPI displays");
    const GfMatrix4d axisView = host.viewCamera()->camera().GetFrustum().ComputeViewMatrix();
    const GfVec3d zDirection = axisView.TransformDir(GfVec3d::ZAxis());
    require(auxiliary.hitTestAxis(QPointF(36.0 + zDirection[0] * 22.0, 36.0 - zDirection[1] * 22.0),
                                  host.viewCamera()->camera())
                == 2,
            "camera-axis bubble hit testing follows the resized axis image");

    const int originalAxisSize = stageviz::ui()->axisSize(stageviz::UI::Medium);
    const GfCamera axisCamera;
    for (int size : { 36, 72, 144, 85 }) {
        stageviz::ui()->setAxisSize(stageviz::UI::Medium, size);
        for (qreal dpr : { 1.0, 2.0 }) {
            auxiliary.updateAxis(axisCamera, true, dpr);
            require(auxiliary.axisImage().size() == QSize(qRound(size * dpr), qRound(size * dpr))
                        && auxiliary.axisImage().devicePixelRatio() == dpr,
                    "UI axis size determines logical image extent independently of DPI");
            const qreal center = size / 2.0;
            const qreal radius = size * (11.0 / 36.0);
            const qreal hitRadius = size / 6.0;
            require(auxiliary.hitTestAxis(QPointF(center + radius, center), axisCamera) == 0,
                    "axis hit testing follows scaled X bubble");
            require(auxiliary.hitTestAxis(QPointF(center + radius + hitRadius * .9, center), axisCamera) == 0
                        && auxiliary.hitTestAxis(QPointF(center + radius + hitRadius + 1, center), axisCamera) == -1,
                    "axis hit padding scales with the drawn bubble");
        }
    }
    stageviz::ui()->setAxisSize(stageviz::UI::Medium, originalAxisSize);

    stageviz::ViewportGizmo gizmo(host);
    stageviz::ViewportGizmo::Frame frame;
    frame.enabled = true;
    frame.pivotValid = true;
    frame.orientationValid = true;
    frame.mode = stageviz::ViewportGizmo::Mode::Move;
    require(gizmo.worldPerPixel(frame.pivot) > 0, "gizmo uses a valid camera-space size");
    gizmo.update(frame);
    require(gizmo.hitTest(frame, QPointF(320, 240)) == 11, "move center maps to free translation");
    const SdfPath gizmoPath = stageviz::paths::auxiliary::transform;
    require(UsdGeomImageable(host.auxiliary->GetPrimAtPath(gizmoPath)).ComputeVisibility() == UsdGeomTokens->inherited,
            "gizmo geometry is visible after update");
    frame.mode = stageviz::ViewportGizmo::Mode::Rotate;
    gizmo.update(frame);
    require(gizmo.hitTest(frame, QPointF(320, 240)) == 10, "rotate center maps to free rotation");
    require(UsdGeomImageable(host.auxiliary->GetPrimAtPath(gizmoPath.AppendChild(TfToken("Move")))).ComputeVisibility()
                == UsdGeomTokens->invisible,
            "switching modes hides inactive handle geometry");
    frame.mode = stageviz::ViewportGizmo::Mode::Scale;
    gizmo.update(frame);
    require(gizmo.hitTest(frame, QPointF(320, 240)) == 12, "scale center maps to uniform scaling");
    gizmo.clear();
    require(UsdGeomImageable(host.auxiliary->GetPrimAtPath(gizmoPath)).ComputeVisibility() == UsdGeomTokens->invisible,
            "clearing selection hides handle geometry");
    host.auxiliary = UsdStage::CreateInMemory();
    gizmo.reset();
    gizmo.update(frame);
    require(bool(host.auxiliary->GetPrimAtPath(gizmoPath.AppendChild(TfToken("Scale")).AppendChild(TfToken("X")))),
            "auxiliary stage replacement rebuilds handle resources");

    host.viewport.setMoveEnabled(true);
    require(host.viewport.transformEnabled() && host.viewport.moveEnabled(), "widget forwards move mode to the tool");
    host.viewport.setRotateEnabled(true);
    require(host.viewport.rotateEnabled() && !host.viewport.moveEnabled(),
            "widget forwards mutually exclusive rotate mode");
    host.viewport.setPivotEnabled(true);
    require(host.viewport.pivotEnabled() && host.viewport.rotateEnabled(), "pivot toggle preserves the current tool");
    host.viewport.setPivotEnabled(false);
    require(!host.viewport.pivotEnabled(), "pivot toggle is independent of transform mode");
    host.viewport.setScaleEnabled(true);
    require(host.viewport.scaleEnabled() && !host.viewport.rotateEnabled(), "widget forwards scale mode to the tool");
    host.viewport.setTransformEnabled(false);
    require(!host.viewport.transformEnabled() && !host.viewport.scaleEnabled(),
            "widget disables manipulation through the tool");
    std::string documentAfter;
    current->stage()->GetRootLayer()->ExportToString(&documentAfter);
    require(documentBefore == documentAfter,
            "auxiliary presentation and mode changes never author into the document stage");
}

void
transformOperationAppliedApi()
{
    auto* current = stageviz::session();
    require(current->newStage(), "new applied transform operation stage");
    const auto stage = current->stage();
    auto source = UsdGeomXform::Define(stage, SdfPath("/Source"));
    auto mesh = UsdGeomMesh::Define(stage, SdfPath("/Source/Mesh"));
    mesh.AddTranslateOp().Set(GfVec3d(2, 3, 4));
    UsdGeomSubset::CreateGeomSubset(mesh, TfToken("A"), UsdGeomTokens->face, VtIntArray { 0 });
    UsdGeomSubset::CreateGeomSubset(mesh, TfToken("B"), UsdGeomTokens->face, VtIntArray { 1 });
    auto instance = UsdGeomXform::Define(stage, SdfPath("/Instance"));
    instance.GetPrim().GetReferences().AddInternalReference(source.GetPath());
    instance.GetPrim().SetInstanceable(true);
    instance.AddTranslateOp().Set(GfVec3d(11, 7, 1));
    const QList<SdfPath> selected { SdfPath("/Instance/Mesh/A"), SdfPath("/Instance/Mesh/B") };
    current->selectionList()->updatePaths(selected);
    auto* stack = current->commandStack();
    stack->clear();
    const auto drain = []() {
        for (int i = 0; i < 6; ++i) {
            QThreadPool::globalInstance()->waitForDone();
            QCoreApplication::sendPostedEvents(nullptr, 0);
            QCoreApplication::processEvents();
        }
    };
    std::string original;
    stage->GetRootLayer()->ExportToString(&original);
    stageviz::TransformOperation operation;
    QString error;
    require(stageviz::beginTransformOperation(stage, selected, operation, error), "begin resolved Applied operation");
    require(operation.paths == QList<SdfPath> { SdfPath("/Instance") }, "proxy subsets share one instance target");
    QStringList errors;
    const auto prepared = stageviz::prepareTransforms(stage, operation.paths, operation.edit.before, &errors);
    const GfMatrix4d delta = GfMatrix4d(1).SetTranslate(GfVec3d(3, -2, 9));
    require(stageviz::applyTransformOperation(stage, operation, prepared, delta, &errors),
            "author interactive preview");
    std::string preview;
    stage->GetRootLayer()->ExportToString(&preview);
    require(preview != original, "preview authors a transform before history registration");
    stack->execute(new stageviz::Command(stageviz::setTransforms(operation)),
                   stageviz::CommandStack::ExecutionMode::Applied);
    drain();
    std::string registered;
    stage->GetRootLayer()->ExportToString(&registered);
    require(registered == preview && stack->canUndo(), "Applied records history without executing the transform twice");
    stack->undo();
    drain();
    std::string restored;
    stage->GetRootLayer()->ExportToString(&restored);
    require(restored == original && !stack->canUndo(), "one undo restores exact original opinions");
    require(current->selectionList()->paths() == selected, "Applied undo preserves original subset selection");
    stack->redo();
    drain();
    stage->GetRootLayer()->ExportToString(&registered);
    require(registered == preview, "Applied redo reproduces the preview exactly");
    require(current->selectionList()->paths() == selected, "Applied redo preserves original subset selection");
}

void
pivotOperationAppliedApi()
{
    auto* current = stageviz::session();
    require(current->newStage(), "new applied transform operation stage");
    const auto stage = current->stage();
    auto source = UsdGeomXform::Define(stage, SdfPath("/Source"));
    auto mesh = UsdGeomMesh::Define(stage, SdfPath("/Source/Mesh"));
    mesh.AddTranslateOp().Set(GfVec3d(2, 3, 4));
    UsdGeomSubset::CreateGeomSubset(mesh, TfToken("A"), UsdGeomTokens->face, VtIntArray { 0 });
    UsdGeomSubset::CreateGeomSubset(mesh, TfToken("B"), UsdGeomTokens->face, VtIntArray { 1 });
    auto instance = UsdGeomXform::Define(stage, SdfPath("/Instance"));
    instance.GetPrim().GetReferences().AddInternalReference(source.GetPath());
    instance.GetPrim().SetInstanceable(true);
    instance.AddTranslateOp().Set(GfVec3d(11, 7, 1));
    const QList<SdfPath> selected { SdfPath("/Instance/Mesh/A"), SdfPath("/Instance/Mesh/B") };
    current->selectionList()->updatePaths(selected);
    auto* stack = current->commandStack();
    stack->clear();
    const auto drain = []() {
        for (int i = 0; i < 6; ++i) {
            QThreadPool::globalInstance()->waitForDone();
            QCoreApplication::sendPostedEvents(nullptr, 0);
            QCoreApplication::processEvents();
        }
    };
    std::string original;
    stage->GetRootLayer()->ExportToString(&original);
    stageviz::PivotOperation operation;
    QString error;
    require(stageviz::beginPivotOperation(stage, selected, operation, error), "begin resolved Applied operation");
    require(operation.paths == QList<SdfPath> { SdfPath("/Instance") }, "proxy subsets share one instance target");
    QStringList errors;
    const GfMatrix4d delta = GfMatrix4d(1).SetTranslate(GfVec3d(3, -2, 9));
    require(stageviz::applyPivotOperation(stage, operation, delta, &errors), "author interactive preview");
    std::string preview;
    stage->GetRootLayer()->ExportToString(&preview);
    require(preview != original, "preview authors a transform before history registration");
    stack->execute(new stageviz::Command(stageviz::setPivotTransforms(operation)),
                   stageviz::CommandStack::ExecutionMode::Applied);
    drain();
    std::string registered;
    stage->GetRootLayer()->ExportToString(&registered);
    require(registered == preview && stack->canUndo(), "Applied records history without executing the transform twice");
    stack->undo();
    drain();
    std::string restored;
    stage->GetRootLayer()->ExportToString(&restored);
    require(restored == original && !stack->canUndo(), "one undo restores exact original opinions");
    require(current->selectionList()->paths() == selected, "Applied undo preserves original subset selection");
    stack->redo();
    drain();
    stage->GetRootLayer()->ExportToString(&registered);
    require(registered == preview, "Applied redo reproduces the preview exactly");
    require(current->selectionList()->paths() == selected, "Applied redo preserves original subset selection");
}

void
viewportTransformPivotApi()
{
    auto* current = stageviz::session();
    require(current->newStage(), "new stage for selection pivot cache regression");
    const SdfPath leftPath("/World/Left");
    const SdfPath rightPath("/World/Right");
    const auto left = UsdGeomCube::Define(current->stage(), leftPath);
    const auto right = UsdGeomCube::Define(current->stage(), rightPath);
    const auto leftTranslate = left.AddTranslateOp();
    const auto rightTranslate = right.AddTranslateOp();
    leftTranslate.Set(GfVec3d(-2, 0, 0));
    rightTranslate.Set(GfVec3d(2, 0, 0));
    stageviz::bbox::clear();
    {
        stageviz::bbox::CacheGuard cache;
        cache.ComputeWorldBound(left.GetPrim());
        cache.ComputeWorldBound(right.GetPrim());
    }
    // Fixture mutations deliberately leave the shared cache stale, as happens
    // when transform notices defer the expensive complete-stage bound refresh.
    leftTranslate.Set(GfVec3d(8, 0, 0));
    rightTranslate.Set(GfVec3d(12, 0, 0));
    ViewportTestHost host;
    stageviz::ViewportSelection selection(host);
    host.selection = &selection;
    host.selected = { leftPath, rightPath };
    stageviz::ViewportTransform transform(host);
    transform.setReferenceFrame(stageviz::ViewportTransform::ReferenceFrame::World);
    transform.setMode(stageviz::ViewportTransform::Mode::Rotate, true);
    // The active (last selected) object is freshly at (12, 0, 0),
    // projecting to (440, 240). Its stale cached bounds still place it at 2.
    std::string beforeNoMotion;
    current->stage()->GetRootLayer()->ExportToString(&beforeNoMotion);
    require(transform.beginDrag(QPointF(440, 240)), "rotation uses the fresh active object pivot");
    require(transform.dragging(), "fresh pivot starts the rotation gesture");
    transform.endDrag();
    require(!transform.dragging() && !transform.deferringUpdates(), "ending the gesture restores session updates");
    std::string afterNoMotion;
    current->stage()->GetRootLayer()->ExportToString(&afterNoMotion);
    require(afterNoMotion == beforeNoMotion, "zero-motion drag restores temporary transform normalization");
}

void
viewportTransformIndividualRotationApi()
{
    auto* current = stageviz::session();
    require(current->newStage(), "new stage for individual rotation regression");
    const SdfPath leftPath("/Left");
    const SdfPath rightPath("/Right");
    const auto left = UsdGeomCube::Define(current->stage(), leftPath);
    const auto right = UsdGeomCube::Define(current->stage(), rightPath);
    left.AddTranslateOp().Set(GfVec3d(-2.0, 0.0, 0.0));
    right.AddTranslateOp().Set(GfVec3d(2.0, 0.0, 0.0));

    ViewportTestHost host;
    stageviz::ViewportSelection selection(host);
    host.selection = &selection;
    host.selected = { leftPath, rightPath };
    stageviz::ViewportTransform transform(host);
    transform.setReferenceFrame(stageviz::ViewportTransform::ReferenceFrame::World);
    transform.setMode(stageviz::ViewportTransform::Mode::Rotate, true);

    QString error;
    require(transform.applyNumeric(GfVec3d(0.0, 0.0, 90.0), error), "numeric rotation applies to each selected object");

    GfMatrix4d leftAfter(1.0);
    GfMatrix4d rightAfter(1.0);
    bool resetsStack = false;
    require(UsdGeomXformable(left.GetPrim()).GetLocalTransformation(&leftAfter, &resetsStack),
            "read left object transform after rotation");
    require(UsdGeomXformable(right.GetPrim()).GetLocalTransformation(&rightAfter, &resetsStack),
            "read right object transform after rotation");
    require(std::abs(leftAfter.ExtractTranslation()[0] + 2.0) < 1e-6
                && std::abs(rightAfter.ExtractTranslation()[0] - 2.0) < 1e-6,
            "each object keeps its own position instead of orbiting the lead object");
    require(std::abs(leftAfter[0][1]) > 0.9 && std::abs(rightAfter[0][1]) > 0.9,
            "each object receives the rotation delta independently");
}

void
viewportTransformHierarchySyncApi()
{
    auto* current = stageviz::session();
    require(current->newStage(), "new hierarchy sync stage");
    const auto parent = UsdGeomXform::Define(current->stage(), SdfPath("/World"));
    parent.AddRotateXYZOp().Set(GfVec3f(17, 31, -12));
    parent.AddScaleOp().Set(GfVec3f(1.4, .8, 1.1));
    const auto child = UsdGeomCube::Define(current->stage(), SdfPath("/World/Cube"));
    child.AddRotateXYZOp().Set(GfVec3f(13, -21, 9));
    ViewportTestHost host;
    stageviz::ViewportSelection selection(host);
    host.selection = &selection;
    host.selected = { child.GetPath() };
    stageviz::ViewportTransform transform(host);
    GfVec3d shown(0.0);
    QObject::connect(&host.viewport, &stageviz::ViewportWidget::transformInputValuesChanged,
                     [&shown](const GfVec3d& value) { shown = value; });
    transform.setMode(stageviz::ViewportTransform::Mode::Rotate, true);
    const auto local = [&]() {
        GfMatrix4d matrix(1.0);
        bool reset = false;
        UsdGeomXformable(child.GetPrim()).GetLocalTransformation(&matrix, &reset);
        return matrix;
    };
    require(GfIsClose(shown, GfVec3d(13, -21, 9), 1e-6),
            "panel displays authored USD rotateXYZ angles under rotated scaled parent");
    const auto expectedStage = UsdStage::CreateInMemory();
    const auto expectedOp
        = UsdGeomXform::Define(expectedStage, SdfPath("/Expected")).AddRotateXYZOp(UsdGeomXformOp::PrecisionDouble);
    const auto eulerMatrix = [&](const GfVec3d& angles) {
        expectedOp.Set(angles);
        return expectedOp.GetOpTransform(UsdTimeCode::Default());
    };
    const auto drain = []() {
        for (int i = 0; i < 6; ++i) {
            QThreadPool::globalInstance()->waitForDone();
            QCoreApplication::sendPostedEvents(nullptr, 0);
            QCoreApplication::processEvents();
        }
    };
    auto* stack = current->commandStack();
    for (const auto frame :
         { stageviz::ViewportTransform::ReferenceFrame::World, stageviz::ViewportTransform::ReferenceFrame::Object,
           stageviz::ViewportTransform::ReferenceFrame::Parent }) {
        transform.setReferenceFrame(frame);
        transform.selectionChanged(false);
        for (const auto& requested : { GfVec3d(-60, 56, 70), GfVec3d(24, 89.99, -33), GfVec3d(24, 90.01, -33) }) {
            const auto before = local();
            stack->clear();
            QString error;
            require(transform.beginNumericEdit(error) && transform.updateNumericEdit(requested, error),
                    "absolute numeric rotation previews under a rotated scaled parent");
            const auto preview = local();
            require(GfIsClose(preview, eulerMatrix(requested), 1e-6),
                    "numeric values reproduce the displayed local Euler convention in every handle frame");
            transform.finishNumericEdit();
            drain();
            require(GfIsClose(local(), preview, 1e-6) && GfIsClose(shown, requested, 1e-6),
                    "Applied commit preserves preview and numeric values");
            stack->undo();
            drain();
            transform.invalidate();
            require(GfIsClose(local(), before, 1e-6) && GfIsClose(eulerMatrix(shown), before, 1e-6),
                    "undo restores both the local transform and displayed values");
            stack->redo();
            drain();
            transform.invalidate();
            require(GfIsClose(local(), preview, 1e-6) && GfIsClose(shown, requested, 1e-6),
                    "redo reproduces the preview and displayed values");
            stack->undo();
            drain();
        }
    }
    for (const auto mode : { stageviz::ViewportTransform::Mode::Move, stageviz::ViewportTransform::Mode::Scale }) {
        transform.setMode(mode, true);
        transform.selectionChanged(false);
        stack->clear();
        const auto before = local();
        QString error;
        const GfVec3d requested = mode == stageviz::ViewportTransform::Mode::Move ? GfVec3d(3, -2, 5)
                                                                                  : GfVec3d(1.5, .7, 2);
        require(transform.updateNumericEdit(requested, error), "local numeric move/scale previews in hierarchy");
        const auto preview = local();
        transform.finishNumericEdit();
        drain();
        require(GfIsClose(shown, requested, 1e-6) && GfIsClose(local(), preview, 1e-6),
                "move/scale commit retains absolute local values under rotated scaled parent");
        stack->undo();
        drain();
        require(GfIsClose(local(), before, 1e-6), "numeric move/scale undo restores hierarchy");
        stack->redo();
        drain();
        require(GfIsClose(local(), preview, 1e-6), "numeric move/scale redo reproduces preview");
        stack->undo();
        drain();
    }
    transform.setMode(stageviz::ViewportTransform::Mode::Rotate, true);
    // World Z ring: this test host projects the XY plane at ten pixels/unit.
    transform.setReferenceFrame(stageviz::ViewportTransform::ReferenceFrame::World);
    transform.invalidate();
    stack->clear();
    const auto before = local();
    stageviz::ViewportGizmo probe(host);
    const double radius = probe.worldPerPixel(GfVec3d(0)) * stageviz::ui()->gizmoSize(stageviz::UI::UIScale::Medium)
                          * 10;
    require(transform.beginDrag(QPointF(320 + radius * .8, 240 - radius * .6)), "begin hierarchy rotation ring drag");
    transform.updateDrag(QPointF(320 + radius * .6, 240 - radius * .8));
    const auto preview = local();
    const auto displayedPreview = shown;
    require(preview != before && displayedPreview != GfVec3d(0), "drag updates USD and absolute panel values");
    transform.endDrag();
    drain();
    require(GfIsClose(local(), preview, 1e-6) && GfIsClose(shown, displayedPreview, 1e-6),
            "ring release does not jump from a delta to absolute numeric values");
    stack->undo();
    drain();
    require(GfIsClose(local(), before, 1e-6), "ring drag undo restores hierarchy");
    stack->redo();
    drain();
    require(GfIsClose(local(), preview, 1e-6), "ring drag redo exactly reproduces preview");

    // An absolute world-Z snap must preserve the other XYZ components.
    parent.GetRotateXYZOp().Set(GfVec3f(0));
    parent.GetScaleOp().Set(GfVec3f(1));
    UsdGeomXformable(child.GetPrim()).MakeMatrixXform().Set(eulerMatrix(GfVec3d(-60, 56, 70)));
    drain();
    transform.selectionChanged(false);
    stack->clear();
    require(transform.beginDrag(QPointF(320 + radius * .8, 240 - radius * .6)), "begin compound XYZ snap drag");
    transform.setSnap(true);
    transform.updateDrag(QPointF(320 + radius * .6, 240 - radius * .8));
    const auto snapped = local();
    const auto snappedValues = shown;
    require(std::abs(shown[0] + 60) < 1e-6 && std::abs(shown[1] - 56) < 1e-6,
            "world Z ring preserves compound XYZ X and Y values");
    const double snap = stageviz::ui()->transformRotateSnap();
    require(std::abs(shown[2] / snap - std::round(shown[2] / snap)) < 1e-6,
            "rotation snapping uses the panel XYZ convention");
    transform.endDrag();
    drain();
    require(GfIsClose(shown, snappedValues, 1e-6) && GfIsClose(local(), snapped, 1e-6),
            "snapped XYZ values survive Applied registration");
    stack->undo();
    drain();
    require(GfIsClose(local(), eulerMatrix(GfVec3d(-60, 56, 70)), 1e-6), "snapped XYZ undo restores baseline");
    stack->redo();
    drain();
    require(GfIsClose(local(), snapped, 1e-6), "snapped XYZ redo reproduces preview");
}

void
viewportGizmoDefaultFrameApi()
{
    const stageviz::ViewportGizmo::Frame frame;
    require(frame.referenceFrame == stageviz::ViewportGizmo::ReferenceFrame::Object,
            "transform gizmo defaults to the object reference frame");
}

void outlinerLayerMuteApi()
{
    QTemporaryDir dir;
    require(dir.isValid(), "temporary layer-mute directory");
    const QString layerPath = dir.filePath("front.usda");
    const auto front = UsdStage::CreateNew(layerPath.toStdString());
    for (int i = 0; i < 20; ++i)
        front->DefinePrim(SdfPath("/World/Delete" + std::to_string(i)), TfToken("Xform"));
    require(front->GetRootLayer()->Save(), "save front fixture");
    const QString rootPath = dir.filePath("root.usda");
    const auto root = UsdStage::CreateNew(rootPath.toStdString());
    root->DefinePrim(SdfPath("/World/Keep"), TfToken("Xform"));
    for (int i = 0; i < 500; ++i)
        root->DefinePrim(SdfPath("/World/Keep/Child" + std::to_string(i)), TfToken("Xform"));
    root->GetRootLayer()->SetSubLayerPaths({ layerPath.toStdString() });
    require(root->GetRootLayer()->Save(), "save root fixture");
    auto* session = stageviz::session();
    stageviz::OutlinerView view;
    require(session->loadFromFile(rootPath, stageviz::Session::None), "load layer-mute fixture");
    auto* tree = view.findChild<stageviz::OutlinerTree*>("outlinerTree");
    require(tree, "find mute-test Outliner");
    const auto drain = []() {
        for (int i = 0; i < 6; ++i) {
            QThreadPool::globalInstance()->waitForDone();
            QCoreApplication::sendPostedEvents(nullptr, 0);
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        }
    };
    const auto find = [&](const QString& path) {
        QTreeWidgetItemIterator iterator(tree);
        while (*iterator) {
            if ((*iterator)->data(0, stageviz::PrimItem::Path).toString() == path)
                return *iterator;
            ++iterator;
        }
        return static_cast<QTreeWidgetItem*>(nullptr);
    };
    drain();
    auto* keep = find("/World/Keep");
    require(keep && find("/World/Delete0"), "initial front and surviving rows exist");
    keep->setExpanded(true);
    session->commandStack()->execute(new stageviz::Command(stageviz::setLayerMuted(layerPath, true)));
    drain();
    require(session->stage()->IsLayerMuted(layerPath.toStdString()) && !find("/World/Delete0"),
            "mute removes the front rows");
    require(find("/World/Keep") == keep && keep->isExpanded() && keep->childCount() == 500,
            "mute preserves surviving row identity, expansion and children");
    // Exercise the ordinary mixed delete path rather than the large,
    // delete-only fast path, including already absent Qt rows.
    stageviz::NoticeBatch mixed;
    for (int i = 0; i < 20; ++i) {
        stageviz::NoticeEntry entry;
        entry.path = SdfPath("/World/Delete" + std::to_string(i));
        entry.primResyncType = UsdNotice::ObjectsChanged::PrimResyncType::Delete;
        mixed.entries.append(entry);
    }
    stageviz::NoticeEntry info;
    info.path = SdfPath("/World/Keep.visibility"); info.changedInfoOnly = true;
    mixed.entries.append(info);
    tree->updatePrims(mixed);
    require(find("/World/Keep") == keep && keep->childCount() == 500, "mixed deletes preserve surviving hierarchy");
    session->commandStack()->undo(); drain();
    require(!session->stage()->IsLayerMuted(layerPath.toStdString()) && find("/World/Delete19"),
            "undo restores composed prims and Qt rows");
    session->commandStack()->redo(); drain();
    require(session->stage()->IsLayerMuted(layerPath.toStdString()) && !find("/World/Delete19"),
            "redo removes composed prims and Qt rows");
}

}  // namespace

int
main(int argc, char** argv)
{
    stageviz::Application app(argc, argv);
    const std::map<std::string, std::function<void()>> cases {
        { "outliner_layer_mute_api", outlinerLayerMuteApi },
        { "viewport_components_api", viewportComponentsApi },
        { "viewport_lights_api", viewportLightsApi },
        { "viewport_transform_pivot_api", viewportTransformPivotApi },
        { "viewport_transform_hierarchy_sync_api", viewportTransformHierarchySyncApi },
        { "viewport_transform_individual_rotation_api", viewportTransformIndividualRotationApi },
        { "viewport_gizmo_default_frame_api", viewportGizmoDefaultFrameApi },
        { "transform_operation_applied_api", transformOperationAppliedApi },
        { "pivot_operation_applied_api", pivotOperationAppliedApi },
        { "settings_api", settingsApi },
        { "treewidget_selection_api", treeWidgetSelectionApi },
        { "indextree_selection_api", indexTreeSelectionApi },
        { "indextree_selection_modes_api", indexTreeSelectionModesApi },
        { "ui_api", uiApi },
        { "numeric_editor_api", numericEditorApi },
        { "renderengine_api", renderEngineApi },
        { "renderengine_hdri_api", renderEngineHdriApi },
        { "material_browser_api", materialBrowserApi },
        { "geometry_subset_gui_api", geometrySubsetGuiApi },
        { "outliner_instance_payload_resync_api", outlinerInstancePayloadResyncApi },
        { "layer_tree_root_row_api", layerTreeRootRowApi },
    };

    if (argc != 2 || !cases.count(argv[1])) {
        std::cerr << "Pass one registered GUI regression case name\n";
        return 2;
    }

    try {
        cases.at(argv[1])();
    } catch (const std::exception& error) {
        const QString log = qEnvironmentVariable("STAGEVIZ_TEST_FAILURE_LOG");
        if (!log.isEmpty())
            std::ofstream(log.toStdString(), std::ios::app) << argv[1] << ": " << error.what() << '\n';
        std::cerr << argv[1] << ": " << error.what() << '\n';
        return 1;
    } catch (...) {
        std::cerr << argv[1] << ": unknown exception\n";
        return 1;
    }

    return 0;
}
