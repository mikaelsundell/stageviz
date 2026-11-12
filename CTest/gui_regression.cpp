// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell

#include "application.h"
#include "bboxutils.h"
#include "changesdialog.h"
#include "commandlayer.h"
#include "commandlight.h"
#include "commandmaterial.h"
#include "commandpayload.h"
#include "commandpivot.h"
#include "commandprim.h"
#include "commandproperty.h"
#include "commandselection.h"
#include "commandstack.h"
#include "commandtransform.h"
#include "commandvisibility.h"
#include "indextree.h"
#include "layerdialog.h"
#include "layeritem.h"
#include "layertree.h"
#include "materialbrowser.h"
#include "materialrenderer.h"
#include "materialtree.h"
#include "materialutils.h"
#include "mcp/mcpserver.h"
#include "outliner/outlinertree.h"
#include "outliner/outlinerview.h"
#include "outliner/primitem.h"
#include "paths.h"
#include "propertyitem.h"
#include "propertytree.h"
#include "propertyview.h"
#include "renderengine.h"
#include "renderresources.h"
#include "renderview.h"
#include "session.h"
#include "settings.h"
#include "spinbox.h"
#include "treewidget.h"
#include "ui.h"
#include "viewcamera.h"
#include "viewcontext.h"
#include "viewer.h"
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
#include <QComboBox>
#include <QContextMenuEvent>
#include <QCoreApplication>
#include <QDialog>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileDialog>
#include <QFont>
#include <QHeaderView>
#include <QImage>
#include <QItemSelectionModel>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QNetworkAccessManager>
#include <QNetworkProxy>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLFramebufferObject>
#include <QPainter>
#include <QPushButton>
#include <QScrollBar>
#include <QSet>
#include <QSettings>
#include <QSurfaceFormat>
#include <QTemporaryDir>
#include <QThread>
#include <QThreadPool>
#include <QTimer>
#include <QTreeWidgetItem>
#include <QTreeWidgetItemIterator>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <pxr/base/gf/camera.h>
#include <pxr/base/gf/matrix4d.h>
#include <pxr/base/gf/range1f.h>
#include <pxr/base/gf/rotation.h>
#include <pxr/base/gf/vec2f.h>
#include <pxr/base/gf/vec3d.h>
#include <pxr/base/gf/vec4f.h>
#include <pxr/usd/sdf/primSpec.h>
#include <pxr/usd/sdf/schema.h>
#include <pxr/usd/usd/payloads.h>
#include <pxr/usd/usd/prim.h>
#include <pxr/usd/usd/stage.h>
#include <pxr/usd/usdGeom/basisCurves.h>
#include <pxr/usd/usdGeom/cube.h>
#include <pxr/usd/usdGeom/mesh.h>
#include <pxr/usd/usdGeom/subset.h>
#include <pxr/usd/usdGeom/xform.h>
#include <pxr/usd/usdLux/domeLight.h>
#include <pxr/usd/usdLux/domeLight_1.h>
#include <pxr/usd/usdLux/lightFilter.h>
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
        const auto organizations = QDir(dir.path()).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        require(!organizations.isEmpty(), "settings honors the isolated default format and directory");
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
renderPrimitiveSelectionApi()
{
    QSurfaceFormat format;
    format.setDepthBufferSize(24);
    format.setStencilBufferSize(8);
    QOpenGLContext context;
    context.setFormat(format);
    require(context.create(), "create primitive selection context");
    QOffscreenSurface surface;
    surface.setFormat(context.format());
    surface.create();
    require(surface.isValid() && context.makeCurrent(&surface), "activate primitive selection context");
    {
        QOpenGLFramebufferObjectFormat framebufferFormat;
        framebufferFormat.setAttachment(QOpenGLFramebufferObject::CombinedDepthStencil);
        QOpenGLFramebufferObject framebuffer(128, 128, framebufferFormat);
        for (const char* type : { "Cone", "Cube", "Sphere", "Cylinder", "Capsule" }) {
            const auto stage = UsdStage::CreateInMemory();
            UsdGeomXform::Define(stage, SdfPath("/Group"));
            stage->DefinePrim(SdfPath("/Group/Shape"), TfToken(type));
            stageviz::RenderEngine viewport(stageviz::RenderEngine::ContextMode::Current);
            viewport.setStage(stage);
            GfCamera camera;
            GfMatrix4d view(1.);
            view.SetLookAt(GfVec3d(4, 3, 6), GfVec3d(0), GfVec3d(0, 1, 0));
            camera.SetTransform(view.GetInverse());
            camera.SetPerspectiveFromAspectRatioAndFieldOfView(1., 45., GfCamera::FOVVertical);
            camera.SetClippingRange(GfRange1f(.1f, 100.f));
            viewport.setCamera(camera);
            viewport.setSize(GfVec2i(128));
            viewport.setViewport(GfVec4d(0, 0, 128, 128));
            viewport.setSelectionColor(QColor(255, 200, 0));
            const auto draw = [&](const QList<SdfPath>& paths) {
                viewport.setSelected(paths);
                require(framebuffer.bind(), "bind primitive selection framebuffer");
                for (int i = 0; i < 3; ++i)
                    require(viewport.renderToCurrentFramebuffer(), "render primitive selection");
                const QImage image = framebuffer.toImage();
                framebuffer.release();
                return image;
            };
            const auto orangePixels = [](const QImage& image) {
                int count = 0;
                for (int y = 0; y < image.height(); ++y) {
                    for (int x = 0; x < image.width(); ++x) {
                        const QColor color = image.pixelColor(x, y);
                        count += color.red() > 200 && color.green() > 100 && color.green() < 230 && color.blue() < 60;
                    }
                }
                return count;
            };
            require(orangePixels(draw({})) == 0, "unselected primitive has no selection outline");
            const QImage parent = draw({ SdfPath("/Group") });
            require(orangePixels(parent) > 8, "parent selection outlines every analytic primitive type");
            require(draw({ SdfPath("/Group/Shape") }) == parent,
                    "direct and parent selection outline the same analytic primitive");
            viewport.reset();
        }
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
    QKeyEvent firstDigit(QEvent::ShortcutOverride, Qt::Key_7, Qt::NoModifier, "7");
    firstDigit.ignore();
    QApplication::sendEvent(line, &firstDigit);
    require(firstDigit.isAccepted(), "numeric entry overrides application shortcuts before the first key press");
    for (const auto& key : { std::make_pair(Qt::Key_7, QString("7")), std::make_pair(Qt::Key_Period, QString(".")),
                             std::make_pair(Qt::Key_5, QString("5")) }) {
        QKeyEvent press(QEvent::KeyPress, key.first, Qt::NoModifier, key.second);
        QApplication::sendEvent(line, &press);
    }
    require(!line->isReadOnly() && qAbs(spin.value() - 7.5) < 1e-8, "typing replaces the old scrub-mode value");
    QKeyEvent enter(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
    QApplication::sendEvent(line, &enter);
    require(line->isReadOnly(), "Enter returns numeric editor to scrub mode");

    spin.setValue(42.);
    QMouseEvent doubleClick(QEvent::MouseButtonDblClick, QPointF(10, 10), QPointF(10, 10), Qt::LeftButton,
                            Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(line, &doubleClick);
    require(spin.isTextEditing() && line->selectedText() == line->text(), "double-click selects the complete value");
    for (QWidget* target : { static_cast<QWidget*>(line), static_cast<QWidget*>(&spin) }) {
        for (auto key : { Qt::Key_5, Qt::Key_A, Qt::Key_Delete }) {
            QKeyEvent shortcut(QEvent::ShortcutOverride, key, Qt::NoModifier);
            shortcut.ignore();
            QApplication::sendEvent(target, &shortcut);
            require(shortcut.isAccepted(), "direct entry keeps application shortcuts in the numeric editor");
        }
    }
    QKeyEvent five(QEvent::KeyPress, Qt::Key_5, Qt::NoModifier, "5");
    QApplication::sendEvent(line, &five);
    require(spin.value() == 5., "first digit replaces the double-clicked value");
    QApplication::sendEvent(line, &enter);
    QKeyEvent shortcut(QEvent::ShortcutOverride, Qt::Key_A, Qt::NoModifier, "a");
    shortcut.ignore();
    QApplication::sendEvent(line, &shortcut);
    require(!shortcut.isAccepted(), "unrelated application shortcuts remain available outside text editing");
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
        const auto renderDome = [&](const QString& texture) {
            settings.defaultCameraLightEnabled = false;
            settings.domeLightTexture = texture;
            engine.setSettings(settings);
            require(fbo.bind() && engine.renderToCurrentFramebuffer(), "render dome texture override");
            return fbo.toImage();
        };
        const QImage defaultDome = renderDome(QString());
        require(renderDome(stageviz::renderresources::defaultDomeTexture()) == defaultDome,
                "empty viewport HDRI selects the HDx Stinson Beach texture");
        engine.reset();
    }
    context.doneCurrent();
}

void
propertyMaterialLightDropdownApi()
{
    auto* current = stageviz::session();
    require(current->newStage(), "new material/light dropdown fixture");
    const auto stage = current->stage();
    auto texture = UsdShadeShader::Define(stage, SdfPath("/Texture"));
    texture.CreateIdAttr(VtValue(TfToken("UsdUVTexture")));
    auto image = UsdShadeShader::Define(stage, SdfPath("/Image"));
    image.CreateIdAttr(VtValue(TfToken("ND_image_color3")));
    struct Choice {
        UsdAttribute attribute;
        QStringList expected;
    };
    QList<Choice> choices;
    for (const auto& name : { "wrapS", "wrapT" })
        choices.append({ texture.CreateInput(TfToken(name), SdfValueTypeNames->Token).GetAttr(),
                         { "black", "clamp", "repeat", "mirror", "useMetadata" } });
    choices.append({ texture.CreateInput(TfToken("sourceColorSpace"), SdfValueTypeNames->Token).GetAttr(),
                     { "raw", "sRGB", "auto" } });
    for (const auto& name : { "uaddressmode", "vaddressmode", "frameendaction" })
        choices.append({ image.CreateInput(TfToken(name), SdfValueTypeNames->String).GetAttr(),
                         { "constant", "clamp", "periodic", "mirror" } });
    choices.append({ image.CreateInput(TfToken("filtertype"), SdfValueTypeNames->String).GetAttr(),
                     { "closest", "linear", "cubic" } });
    for (const auto& choice : choices) {
        if (choice.attribute.GetTypeName() == SdfValueTypeNames->String)
            choice.attribute.Set(choice.expected.first().toStdString());
        else
            choice.attribute.Set(TfToken(choice.expected.first().toStdString()));
    }
    const auto dome = UsdLuxDomeLight_1::Define(stage, SdfPath("/Dome"));
    const auto filter = UsdLuxLightFilter::Define(stage, SdfPath("/Filter"));
    for (const auto& prim : { dome.GetPrim(), filter.GetPrim() }) {
        for (const auto& attribute : prim.GetAttributes()) {
            VtTokenArray allowed;
            if (attribute.GetMetadata(SdfFieldKeys->AllowedTokens, &allowed) && !allowed.empty()) {
                QStringList expected;
                for (const auto& value : allowed)
                    expected.append(QString::fromStdString(value.GetString()));
                choices.append({ attribute, expected });
            }
        }
    }
    texture.CreateInput(TfToken("label"), SdfValueTypeNames->String).Set(std::string("free text"));
    QThreadPool::globalInstance()->waitForDone();
    stageviz::PropertyTree tree;
    tree.updateStage(stage);
    for (const auto& choice : choices) {
        tree.updateSelection({ choice.attribute.GetPrimPath() });
        stageviz::PropertyItem* found = nullptr;
        QTreeWidgetItemIterator it(&tree);
        while (*it) {
            auto* item = dynamic_cast<stageviz::PropertyItem*>(*it);
            if (item && item->propertyPath() == choice.attribute.GetPath())
                found = item;
            ++it;
        }
        require(found && found->editor() == stageviz::PropertyItem::TokenEditor,
                "material and light enum attributes use dropdowns");
        for (const auto& value : choice.expected)
            require(found->editorOptions().contains(value), "dropdown contains the shader/schema enum value");
        std::cout << choice.attribute.GetPath() << ": " << found->editorOptions().join(", ").toStdString() << '\n';
    }
    tree.updateSelection({ texture.GetPath() });
    QTreeWidgetItemIterator it(&tree);
    while (*it) {
        auto* item = dynamic_cast<stageviz::PropertyItem*>(*it);
        if (item && item->propertyPath() == texture.GetPath().AppendProperty(TfToken("inputs:label")))
            require(item->editor() == stageviz::PropertyItem::TextEditor,
                    "unconstrained shader strings retain text editing");
        ++it;
    }
    tree.updateSelection({ image.GetPath() });
    tree.expandAll();
    tree.resize(640, 600);
    tree.show();
    auto* address = tree.findItems("inputs:uaddressmode", Qt::MatchExactly | Qt::MatchRecursive, 0).value(0);
    require(address, "find string-valued MaterialX address input");
    tree.setCurrentItem(address, stageviz::PropertyItem::Value);
    tree.editItem(address, stageviz::PropertyItem::Value);
    const auto drain = []() {
        for (int i = 0; i < 6; ++i) {
            QThreadPool::globalInstance()->waitForDone();
            QCoreApplication::sendPostedEvents(nullptr, 0);
            QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
        }
    };
    drain();
    auto* combo = tree.findChild<QComboBox*>();
    require(combo && combo->findText("mirror") >= 0, "MaterialX string opens a dropdown");
    current->commandStack()->clear();
    combo->setCurrentText("mirror");
    QKeyEvent enter(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
    QApplication::sendEvent(combo, &enter);
    drain();
    std::string value;
    const auto attr = image.GetInput(TfToken("uaddressmode")).GetAttr();
    require(attr.Get(&value) && value == "mirror", "dropdown preserves the MaterialX string type");
    current->commandStack()->undo();
    drain();
    require(attr.Get(&value) && value == "constant", "undo restores the MaterialX string choice");
    current->commandStack()->redo();
    drain();
    require(attr.Get(&value) && value == "mirror", "redo restores the MaterialX string choice");
}

void
propertyAssetEditorApi()
{
    auto* current = stageviz::session();
    require(current->newStage(), "new property asset fixture");
    const auto dome = UsdLuxDomeLight::Define(current->stage(), SdfPath("/Dome"));
    const auto asset = dome.CreateTextureFileAttr(VtValue(SdfAssetPath("original.exr")));
    const auto format = dome.GetTextureFormatAttr();
    const auto sync = current->stage()->GetPrimAtPath(SdfPath("/Dome")).GetAttribute(TfToken("light:materialSyncMode"));
    const auto drain = []() {
        for (int i = 0; i < 6; ++i) {
            QThreadPool::globalInstance()->waitForDone();
            QCoreApplication::sendPostedEvents(nullptr, 0);
            QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
        }
    };
    drain();
    stageviz::PropertyTree tree;
    tree.updateStage(current->stage());
    tree.updateSelection({ SdfPath("/Dome") });
    tree.expandAll();
    tree.resize(640, 600);
    tree.show();
    drain();
    const auto find = [&](const SdfPath& path) -> stageviz::PropertyItem* {
        QTreeWidgetItemIterator it(&tree);
        while (*it) {
            auto* item = dynamic_cast<stageviz::PropertyItem*>(*it);
            if (item && item->propertyPath() == path)
                return item;
            ++it;
        }
        return nullptr;
    };
    auto* fileItem = find(asset.GetPath());
    require(fileItem && !fileItem->icon(stageviz::PropertyItem::Browse).isNull(), "asset attribute has an Open helper");
    require(fileItem->editor() == stageviz::PropertyItem::TextEditor, "asset text editor remains unchanged");
    for (const auto& attr : { format, sync }) {
        auto* item = find(attr.GetPath());
        VtTokenArray allowed;
        require(item && attr.GetMetadata(SdfFieldKeys->AllowedTokens, &allowed), "USD provides token choices");
        require(item->editor() == stageviz::PropertyItem::TokenEditor, "schema token attributes use dropdowns");
        for (const auto& token : allowed)
            require(item->editorOptions().contains(QString::fromStdString(token.GetString())),
                    "dropdown includes USD allowed token");
    }
    tree.setCurrentItem(find(format.GetPath()), stageviz::PropertyItem::Value);
    tree.editItem(find(format.GetPath()), stageviz::PropertyItem::Value);
    drain();
    auto* combo = tree.findChild<QComboBox*>();
    require(combo && combo->findText("angular") >= 0, "texture format opens the allowed-token dropdown");
    combo->setCurrentText("angular");
    QKeyEvent enter(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
    QApplication::sendEvent(combo, &enter);
    drain();
    TfToken token;
    require(format.Get(&token) && token == TfToken("angular"), "dropdown choice authors the token");
    current->commandStack()->undo();
    drain();
    require(format.Get(&token) && token == TfToken("automatic"), "undo restores the previous token");
    current->commandStack()->redo();
    drain();
    require(format.Get(&token) && token == TfToken("angular"), "redo restores the dropdown choice");
    QTemporaryDir directory;
    const QString chosen = directory.filePath("chosen.exr");
    QFile file(chosen);
    require(file.open(QIODevice::WriteOnly), "create asset chooser fixture");
    file.close();
    const bool previousNative = QCoreApplication::testAttribute(Qt::AA_DontUseNativeDialogs);
    QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs, true);
    QSettings preferences(PROJECT_IDENTIFIER, PROJECT_NAME);
    const bool hadAssetDir = preferences.contains("assetDir");
    const QVariant previousAssetDir = preferences.value("assetDir");
    bool dialogSeen = false;
    const auto browse = [&](bool cancel) {
        QTimer watchdog;
        watchdog.setSingleShot(true);
        bool timedOut = false;
        QObject::connect(&watchdog, &QTimer::timeout, &tree, [&]() {
            timedOut = true;
            for (auto* widget : QApplication::topLevelWidgets()) {
                if (auto* dialog = qobject_cast<QFileDialog*>(widget))
                    dialog->reject();
            }
        });
        watchdog.start(5000);
        QTimer::singleShot(50, &tree, [&, cancel]() {
            for (auto* widget : QApplication::topLevelWidgets()) {
                if (auto* dialog = qobject_cast<QFileDialog*>(widget)) {
                    if (!dialog->isVisible())
                        continue;
                    dialogSeen = true;
                    if (cancel)
                        dialog->reject();
                    else {
                        dialog->setDirectory(directory.path());
                        QTimer::singleShot(100, dialog, [&, dialog]() {
                            auto* filename = dialog->findChild<QLineEdit*>("fileNameEdit");
                            require(filename, "Qt file chooser exposes its filename editor");
                            filename->setText(chosen);
                            QMetaObject::invokeMethod(dialog, "accept", Qt::QueuedConnection);
                        });
                    }
                }
            }
        });
        tree.scrollToItem(fileItem);
        const QPoint position(tree.header()->sectionViewportPosition(stageviz::PropertyItem::Browse) + 16,
                              tree.visualItemRect(fileItem).center().y());
        QMouseEvent press(QEvent::MouseButtonPress, QPointF(position), QPointF(tree.viewport()->mapToGlobal(position)),
                          Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(tree.viewport(), &press);
        QMouseEvent release(QEvent::MouseButtonRelease, QPointF(position),
                            QPointF(tree.viewport()->mapToGlobal(position)), Qt::LeftButton, Qt::NoButton,
                            Qt::NoModifier);
        QApplication::sendEvent(tree.viewport(), &release);
        watchdog.stop();
        drain();
        require(!timedOut, "file chooser automation completes without manual intervention");
    };
    current->commandStack()->clear();
    browse(true);
    SdfAssetPath value;
    require(dialogSeen && asset.Get(&value) && value.GetAssetPath() == "original.exr", "cancel preserves asset path");
    dialogSeen = false;
    browse(false);
    asset.Get(&value);
    if (!dialogSeen || value.GetAssetPath() != chosen.toStdString())
        std::cerr << "Asset chooser: dialog=" << dialogSeen << ", value=" << value.GetAssetPath()
                  << ", expected=" << chosen.toStdString() << '\n';
    require(dialogSeen && asset.Get(&value) && value.GetAssetPath() == chosen.toStdString(),
            "Open helper authors chosen asset");
    current->commandStack()->undo();
    drain();
    require(asset.Get(&value) && value.GetAssetPath() == "original.exr", "undo restores original asset path");
    current->commandStack()->redo();
    drain();
    require(asset.Get(&value) && value.GetAssetPath() == chosen.toStdString(), "redo restores chosen asset path");
    if (hadAssetDir)
        preferences.setValue("assetDir", previousAssetDir);
    else
        preferences.remove("assetDir");
    QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs, previousNative);

    current->selectionList()->updatePaths({ SdfPath("/Dome") });
    stageviz::PropertyView view;
    view.resize(640, 600);
    view.show();
    drain();
    auto* embedded = view.findChild<stageviz::PropertyTree*>("propertyTree");
    require(embedded && embedded->columnCount() == 3, "Designer Property View preserves the Browse column");
    require(!embedded->isColumnHidden(stageviz::PropertyItem::Browse)
                && embedded->columnWidth(stageviz::PropertyItem::Browse) == 32,
            "Property View keeps the file helper column visible at its fixed width");
    stageviz::PropertyItem* embeddedFile = nullptr;
    QTreeWidgetItemIterator iterator(embedded);
    while (*iterator) {
        auto* item = dynamic_cast<stageviz::PropertyItem*>(*iterator);
        if (item && item->propertyPath() == asset.GetPath())
            embeddedFile = item;
        ++iterator;
    }
    require(embeddedFile && !embeddedFile->icon(stageviz::PropertyItem::Browse).isNull(),
            "embedded dome-light asset displays the Open icon");
    embedded->expandAll();
    embedded->scrollToItem(embeddedFile);
    embedded->setCurrentItem(embeddedFile, stageviz::PropertyItem::Value);
    embedded->editItem(embeddedFile, stageviz::PropertyItem::Value);
    drain();
    auto* editor = embedded->findChild<QLineEdit*>();
    const int helperLeft = embedded->header()->sectionViewportPosition(stageviz::PropertyItem::Browse);
    require(editor && editor->isVisible() && editor->geometry().right() < helperLeft,
            "asset text editor leaves the Open helper visible");
}

void
materialNumericEditorApi()
{
    auto* current = stageviz::session();
    const auto drain = []() {
        for (int i = 0; i < 6; ++i) {
            QThreadPool::globalInstance()->waitForDone();
            QCoreApplication::sendPostedEvents(nullptr, 0);
            QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
        }
    };
    struct Fixture {
        SdfValueTypeName type;
        VtValue initial;
        VtValue typed;
    };
    for (const auto& fixture :
         { Fixture { SdfValueTypeNames->Int, VtValue(1), VtValue(5) },
           Fixture { SdfValueTypeNames->Float2, VtValue(GfVec2f(1)), VtValue(GfVec2f(5, 1)) },
           Fixture { SdfValueTypeNames->Float3, VtValue(GfVec3f(1)), VtValue(GfVec3f(5, 1, 1)) },
           Fixture { SdfValueTypeNames->Float4, VtValue(GfVec4f(1)), VtValue(GfVec4f(5, 1, 1, 1)) } }) {
        require(current->newStage(), "new material numeric fixture");
        QList<stageviz::MaterialEntry> entries;
        QList<UsdAttribute> attributes;
        for (const auto& name : { "One", "Two" }) {
            stageviz::MaterialEntry entry;
            entry.materialPath = SdfPath(std::string("/Materials/") + name);
            entry.shaderPath = entry.materialPath.AppendChild(TfToken("Surface"));
            auto shader = UsdShadeShader::Define(current->stage(), entry.shaderPath);
            auto input = shader.CreateInput(TfToken("test"), fixture.type);
            input.Set(fixture.initial);
            attributes.append(input.GetAttr());
            stageviz::MaterialInputInfo info;
            info.parameter = "test";
            info.label = "Test";
            info.group = "Inputs";
            info.inputPath = input.GetAttr().GetPath();
            info.typeName = fixture.type;
            info.hasValue = true;
            info.value = fixture.initial;
            entry.inputs.insert("test", info);
            entries.append(entry);
        }
        drain();
        current->commandStack()->clear();
        stageviz::MaterialTree tree;
        tree.setMaterials(entries);
        tree.resize(640, 480);
        tree.expandAll();
        tree.show();
        drain();
        auto* item = tree.topLevelItem(0)->child(0);
        if (item->childCount())
            item = item->child(0);
        auto* spin = qobject_cast<stageviz::SpinBox*>(tree.itemWidget(item, 1));
        require(spin, "material numeric inputs use the shared SpinBox");
        auto* line = spin->findChild<QLineEdit*>();
        require(line && line->isReadOnly(), "material numeric input starts in scrub mode");
        QMouseEvent doubleClick(QEvent::MouseButtonDblClick, QPointF(10, 10), QPointF(10, 10), Qt::LeftButton,
                                Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(line, &doubleClick);
        QKeyEvent shortcut(QEvent::ShortcutOverride, Qt::Key_5, Qt::NoModifier, "5");
        shortcut.ignore();
        QApplication::sendEvent(line, &shortcut);
        require(shortcut.isAccepted(), "material numeric entry claims application shortcuts");
        QKeyEvent five(QEvent::KeyPress, Qt::Key_5, Qt::NoModifier, "5");
        QApplication::sendEvent(line, &five);
        require(spin->value() == 5., "typing replaces the entire material component value");
        QKeyEvent enter(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
        QApplication::sendEvent(line, &enter);
        drain();
        const auto authoredValuesEqual = [&](const VtValue& expected) {
            for (const auto& attribute : attributes) {
                VtValue value;
                if (!attribute.Get(&value) || value != expected)
                    return false;
            }
            return true;
        };
        require(authoredValuesEqual(fixture.typed),
                "numeric commit updates both materials and preserves other components");
        current->commandStack()->undo();
        drain();
        require(authoredValuesEqual(fixture.initial), "undo restores material numeric values");
        current->commandStack()->redo();
        drain();
        require(authoredValuesEqual(fixture.typed), "redo restores typed material numeric values");

        QMouseEvent press(QEvent::MouseButtonPress, QPointF(10, 10), QPointF(10, 10), Qt::LeftButton, Qt::LeftButton,
                          Qt::NoModifier);
        QApplication::sendEvent(line, &press);
        QMouseEvent move(QEvent::MouseMove, QPointF(40, 10), QPointF(40, 10), Qt::NoButton, Qt::LeftButton,
                         Qt::NoModifier);
        QApplication::sendEvent(line, &move);
        QMouseEvent release(QEvent::MouseButtonRelease, QPointF(40, 10), QPointF(40, 10), Qt::LeftButton, Qt::NoButton,
                            Qt::NoModifier);
        QApplication::sendEvent(line, &release);
        drain();
        if (!(spin->value() > 5. && !authoredValuesEqual(fixture.typed)))
            std::cerr << "Material numeric fixture " << fixture.type.GetAsToken().GetString()
                      << ": scrub value=" << spin->value() << ", text editing=" << spin->isTextEditing()
                      << ", USD unchanged=" << authoredValuesEqual(fixture.typed) << '\n';
        require(spin->value() > 5. && !authoredValuesEqual(fixture.typed), "scrub release commits the material edit");
        QApplication::sendEvent(line, &enter);
        drain();
        current->commandStack()->undo();
        drain();
        require(authoredValuesEqual(fixture.typed), "scrub release and editingFinished create one undoable edit");
    }
}

void
materialSwatchAlphaApi()
{
    stageviz::MaterialRenderer renderer;
    QImage image;
    QString error;
    QObject::connect(&renderer, &stageviz::MaterialRenderer::rendered, &renderer,
                     [&](const QString&, const QImage& result) { image = result; });
    QObject::connect(&renderer, &stageviz::MaterialRenderer::error, &renderer,
                     [&](const QString&, const QString& message) { error = message; });
    const auto checkRender = [&](const std::function<void()>& request) {
        image = {};
        error.clear();
        request();
        QElapsedTimer timer;
        timer.start();
        while (image.isNull() && error.isEmpty() && timer.elapsed() < 60000) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
            QThread::msleep(1);
        }
        require(error.isEmpty() && !image.isNull(), "render a material swatch");
        require(image.hasAlphaChannel(), "material swatch retains alpha");
        for (const QPoint corner : { QPoint(0, 0), QPoint(image.width() - 1, 0), QPoint(0, image.height() - 1),
                                     QPoint(image.width() - 1, image.height() - 1) })
            require(image.pixelColor(corner).alpha() == 0, "swatch background is fully transparent");
        require(image.pixelColor(image.width() / 2, image.height() / 2).alpha() == 255,
                "opaque shaderball remains visible against the transparent background");
    };

    const SdfPath path("/Materials/Swatch");
    checkRender([&]() { renderer.request(path, stageviz::MaterialParameters()); });

    const auto stage = UsdStage::CreateInMemory();
    const auto material = UsdShadeMaterial::Define(stage, path);
    auto shader = UsdShadeShader::Define(stage, path.AppendChild(TfToken("Surface")));
    shader.CreateIdAttr(VtValue(TfToken("UsdPreviewSurface")));
    const auto color = shader.CreateInput(TfToken("diffuseColor"), SdfValueTypeNames->Color3f);
    color.Set(GfVec3f(.5f));
    material.CreateSurfaceOutput().ConnectToSource(shader.CreateOutput(TfToken("surface"), SdfValueTypeNames->Token));
    checkRender([&]() { renderer.request(path, stage->GetRootLayer()); });
    checkRender([&]() {
        renderer.preview(path, stage->GetRootLayer(), color.GetAttr().GetPath(), VtValue(GfVec3f(.8f, .2f, .1f)));
    });
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
    require(browser.swatch(0).hasAlphaChannel() && browser.swatch(0).pixelColor(0, 0).alpha() == 0,
            "pending swatches leave the browser card background visible");
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
    browser.selectRow(0);
    browser.setEntries({ second, first });
    require(browser.selectedRows() == QList<int> { 1 }, "entry reorder retains selection by native USD path");
    require(browser.swatch(1) == swatch, "entry reorder retains the matching material swatch");
    browser.setEntries({ first });
    require(browser.selectedRows() == QList<int> { 0 } && browser.swatch(0) == swatch,
            "entry removal retains the surviving selection and swatch");
    browser.setEntries({ first, second });
    require(browser.selectedRows() == QList<int> { 0 } && browser.swatch(0) == swatch,
            "append-only refresh retains the existing selection and swatch");
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
propertyRevealFileApi()
{
    QTemporaryDir dir;
    require(dir.isValid(), "temporary property file reveal directory");
    require(QDir(dir.path()).mkdir("nested"), "create nested layer directory");
    const QString assetFile = dir.filePath("nested/box.usda");
    const auto asset = UsdStage::CreateNew(assetFile.toStdString());
    const auto box = UsdGeomXform::Define(asset, SdfPath("/Box")).GetPrim();
    asset->SetDefaultPrim(box);
    require(asset->GetRootLayer()->Save(), "save reveal asset");
    const QString childFile = dir.filePath("nested/child.usda");
    const auto child = UsdStage::CreateNew(childFile.toStdString());
    const auto payload = UsdGeomXform::Define(child, SdfPath("/Payload")).GetPrim();
    require(payload.GetPayloads().AddPayload("box.usda"), "author relative payload in sublayer");
    payload.CreateAttribute(TfToken("texture"), SdfValueTypeNames->Asset).Set(SdfAssetPath("box.usda"));
    payload.CreateAttribute(TfToken("missingTexture"), SdfValueTypeNames->Asset).Set(SdfAssetPath("missing.exr"));
    const auto reference = UsdGeomXform::Define(child, SdfPath("/Reference")).GetPrim();
    require(reference.GetReferences().AddReference("box.usda"), "author relative reference in sublayer");
    require(child->GetRootLayer()->Save(), "save reveal sublayer");
    const QString rootFile = dir.filePath("root.usda");
    const auto fixture = UsdStage::CreateNew(rootFile.toStdString());
    fixture->GetRootLayer()->SetSubLayerPaths({ "nested/child.usda" });
    // A stronger prim spec must not change the anchor of the weaker payload.
    fixture->OverridePrim(SdfPath("/Payload")).SetCustomDataByKey(TfToken("stronger"), VtValue(true));
    require(fixture->GetRootLayer()->Save(), "save reveal scene");

    stageviz::PropertyView view;
    view.resize(640, 800);
    view.show();
    auto* current = stageviz::session();
    require(current->loadFromFile(rootFile, stageviz::Session::None), "load reveal scene");
    auto* tree = view.findChild<stageviz::PropertyTree*>("propertyTree");
    require(tree, "find reveal property tree");
    const auto select = [&](const char* path) {
        current->commandStack()->execute(
            new stageviz::Command(stageviz::selectPaths(*path ? QList<SdfPath> { SdfPath(path) } : QList<SdfPath>())));
        for (int i = 0; i < 6; ++i) {
            QThreadPool::globalInstance()->waitForDone();
            QCoreApplication::sendPostedEvents(nullptr, 0);
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        }
        tree->expandAll();
    };
    const auto find = [&](const QString& name, const QString& parent = QString()) -> QTreeWidgetItem* {
        QTreeWidgetItemIterator it(tree);
        while (*it) {
            if ((*it)->text(0) == name && (parent.isEmpty() || ((*it)->parent() && (*it)->parent()->text(0) == parent)))
                return *it;
            ++it;
        }
        return nullptr;
    };
    const auto menuTarget = [&](QTreeWidgetItem* row) {
        require(row, "find file reveal row");
        tree->scrollToItem(row);
        const QPoint point = tree->visualItemRect(row).center();
        QString target;
        bool inspected = false;
        QTimer::singleShot(0, tree, [&]() {
            for (QWidget* widget : QApplication::topLevelWidgets()) {
                auto* menu = qobject_cast<QMenu*>(widget);
                if (!menu || !menu->isVisible())
                    continue;
                for (QAction* action : menu->actions()) {
                    if (action->objectName() == "propertyRevealFile") {
                        require(action->isEnabled(), "file reveal action enabled");
                        target = action->data().toString();
                    }
                }
                inspected = true;
                menu->close();
            }
        });
        QContextMenuEvent event(QContextMenuEvent::Mouse, point, tree->viewport()->mapToGlobal(point));
        QApplication::sendEvent(tree->viewport(), &event);
        require(inspected, "inspect property context menu");
        return target;
    };
    select("/Payload");
    require(menuTarget(find("Asset", "Payload")) == assetFile,
            "payload reveal resolves relative to its authored sublayer");
    require(menuTarget(find("texture")) == assetFile, "asset attribute reveals its resolved file");
    require(menuTarget(find("missingTexture")).isEmpty(), "missing asset has no reveal action");
    require(menuTarget(find("Prim Path", "Payload")).isEmpty(), "USD prim paths are not filesystem paths");
    require(menuTarget(find("Strongest Layer")) == rootFile, "strongest layer row reveals the layer file");
    select("/Reference");
    require(menuTarget(find("Asset", "References")) == assetFile,
            "reference reveal resolves relative to its authored sublayer");
    select("");
    require(menuTarget(find("filePath")) == rootFile, "stage filename row reveals its full path");
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
outlinerResetOpinionsMenuApi()
{
    stageviz::OutlinerView view;
    view.resize(500, 400);
    view.show();
    auto* current = stageviz::session();
    require(current->newStage(), "new reset menu fixture");
    const auto stage = current->stage();
    const auto root = stage->GetRootLayer();
    const auto weak = SdfLayer::CreateAnonymous("reset-menu-base.usda");
    require(weak->ImportFromString("#usda 1.0\ndef Sphere \"Sphere\" { }\n"), "base menu geometry");
    root->SetSubLayerPaths({ weak->GetIdentifier() });
    stage->GetPrimAtPath(SdfPath("/Sphere")).GetAttribute(TfToken("visibility")).Set(TfToken("invisible"));
    current->commandStack()->execute(new stageviz::Command(stageviz::selectPaths({ SdfPath("/Sphere") })));
    for (int i = 0; i < 6; ++i) {
        QThreadPool::globalInstance()->waitForDone();
        QCoreApplication::sendPostedEvents(nullptr, 0);
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    }
    auto* tree = view.findChild<stageviz::OutlinerTree*>("outlinerTree");
    require(tree, "find reset menu tree");
    tree->expandAll();
    auto* row = tree->findItems("Sphere", Qt::MatchExactly | Qt::MatchRecursive).value(0);
    require(row, "find reset menu prim row");
    const auto inspect = [&](bool locked) {
        QStringList labels;
        bool enabled = false;
        QTimer::singleShot(0, tree, [&]() {
            for (auto* widget : QApplication::allWidgets()) {
                auto* menu = qobject_cast<QMenu*>(widget);
                if (!menu || menu->objectName() != "resetOpinionsMenu")
                    continue;
                enabled = menu->menuAction()->isEnabled();
                for (auto* action : menu->actions())
                    labels.append(action->isSeparator() ? "separator" : action->text());
            }
            for (auto* widget : QApplication::topLevelWidgets()) {
                if (auto* menu = qobject_cast<QMenu*>(widget); menu && menu->isVisible())
                    menu->close();
            }
        });
        const QPoint point = tree->visualItemRect(row).center();
        QContextMenuEvent event(QContextMenuEvent::Mouse, point, tree->viewport()->mapToGlobal(point));
        QApplication::sendEvent(tree->viewport(), &event);
        require(labels == (locked ? QStringList { "All" } : QStringList { "All", "separator", "Visibility" }),
                "reset menu shows only edit-layer property groups below All and divider");
        require(enabled != locked, "locked edit layer disables reset menu");
    };
    inspect(false);
    root->SetPermissionToEdit(false);
    inspect(true);
    root->SetPermissionToEdit(true);
}

void
outlinerLayerColorsApi()
{
    stageviz::LayerTree layers;
    layers.resize(420, 300);
    layers.show();
    stageviz::OutlinerView view;
    view.resize(420, 300);
    view.show();
    auto* current = stageviz::session();
    require(current->newStage(), "new colored Outliner fixture");
    const auto stage = current->stage();
    const auto root = stage->GetRootLayer();
    const auto child = SdfLayer::CreateAnonymous("colored-child.usda");
    root->SetSubLayerPaths({ child->GetIdentifier() });
    UsdGeomXform::Define(stage, SdfPath("/RootOwned"));
    require(current->setEditLayer(child), "select colored child layer");
    UsdGeomXform::Define(stage, SdfPath("/ChildOwned"));
    require(current->setEditLayer(root), "restore colored root layer");
    auto* tree = view.findChild<stageviz::OutlinerTree*>("outlinerTree");
    require(tree, "find colored Outliner tree");
    const auto drain = []() {
        for (int i = 0; i < 6; ++i) {
            QThreadPool::globalInstance()->waitForDone();
            QCoreApplication::sendPostedEvents(nullptr, 0);
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        }
    };
    const auto find = [&](const QString& path) -> QTreeWidgetItem* {
        QTreeWidgetItemIterator it(tree);
        while (*it) {
            if ((*it)->data(0, stageviz::PrimItem::Path).toString() == path)
                return *it;
            ++it;
        }
        return nullptr;
    };
    const auto strip = [&](const QString& path, int column = 0) {
        auto* row = find(path);
        require(row, "find colored row");
        tree->scrollToItem(row);
        drain();
        const QRect rect = tree->visualItemRect(row);
        const QImage image = tree->viewport()->grab().toImage();
        return image.pixelColor(qRound((column * 3 + 1) * image.devicePixelRatio()),
                                qRound(rect.center().y() * image.devicePixelRatio()));
    };
    const auto layerStrip = [&](const QString& identifier) {
        QTreeWidgetItemIterator it(&layers);
        while (*it && static_cast<stageviz::LayerItem*>(*it)->identifier() != identifier)
            ++it;
        require(*it, "find colored layer row");
        auto* row = *it;
        layers.setCurrentItem(row);
        layers.scrollToItem(row);
        drain();
        const QImage image = layers.viewport()->grab().toImage();
        return image.pixelColor(qRound(image.devicePixelRatio()),
                                qRound(layers.visualItemRect(row).center().y() * image.devicePixelRatio()));
    };
    const QString rootId = QString::fromStdString(root->GetIdentifier());
    const QString childId = QString::fromStdString(child->GetIdentifier());
    const QColor red("#d04030");
    const QColor blue("#3060c0");
    current->setLayerColor(rootId, red);
    current->setLayerColor(childId, blue);
    drain();
    tree->expandAll();
    require(find("/ChildOwned")->data(0, stageviz::PrimItem::StrongestLayer).toString() == childId,
            "row identifies its strongest prim spec layer");
    require(strip("/RootOwned") == red && strip("/ChildOwned") == blue, "row starts show assigned layer colors");
    UsdGeomXform::Define(stage, SdfPath("/SameLayerSource"));
    const auto sameLayer = UsdGeomXform::Define(stage, SdfPath("/SameLayerCopy")).GetPrim();
    sameLayer.GetReferences().AddInternalReference(SdfPath("/SameLayerSource"));
    drain();
    require(find("/SameLayerCopy")->data(0, stageviz::PrimItem::CompositionLayers).toStringList()
                == QStringList { rootId },
            "multiple specs from one layer contribute one color strip");
    layers.refresh();
    layers.expandAll();
    require(layerStrip(rootId) == red && layerStrip(childId) == blue,
            "Layer Editor root and sublayer strips match colors while selected");
    current->commandStack()->execute(new stageviz::Command(stageviz::selectPaths({ SdfPath("/ChildOwned") })));
    drain();
    QElapsedTimer selectedPaint;
    selectedPaint.start();
    QColor selectedColor;
    do {
        selectedColor = strip("/ChildOwned");
        if (selectedColor == blue)
            break;
        QThread::msleep(5);
    } while (selectedPaint.elapsed() < 2000);
    require(selectedColor == blue, "color strip remains visible over selected row");
    current->commandStack()->execute(new stageviz::Command(stageviz::hidePaths({ SdfPath("/ChildOwned") }, false)));
    drain();
    const QStringList expectedLayers { childId, rootId };
    for (int i = 0; i < 100; ++i)
        require(find("/ChildOwned")->data(0, stageviz::PrimItem::CompositionLayers).toStringList() == expectedLayers,
                "cached composition lists contributing layers from weak to strong");
    require(find("/ChildOwned")->data(0, stageviz::PrimItem::StrongestLayer).toString() == rootId,
            "strongest-layer role still identifies the override layer");
    require(strip("/ChildOwned") == blue && strip("/ChildOwned", 1) == red,
            "override adds its color to the right of the base layer while selected");
    current->commandStack()->execute(new stageviz::Command(stageviz::resetOverrides({ SdfPath("/ChildOwned") })));
    drain();
    require(strip("/ChildOwned") == blue && strip("/ChildOwned", 1) != red,
            "reset override restores the single underlying layer color");
    current->commandStack()->undo();
    drain();
    require(strip("/ChildOwned") == blue && strip("/ChildOwned", 1) == red,
            "undo restores the full composition marker");
    current->setLayerColor(rootId, QColor());
    drain();
    require(strip("/RootOwned") != red && strip("/ChildOwned") == blue && strip("/ChildOwned", 1) != red,
            "clearing a layer color removes only its composition strip");
    require(layerStrip(rootId) != red && layerStrip(childId) == blue,
            "clearing root color removes its Layer Editor strip and keeps the sublayer color");
}

void
outlinerEditLayerFontApi()
{
    stageviz::OutlinerView view;
    auto* current = stageviz::session();
    require(current->newStage(), "new Outliner font fixture");
    const auto stage = current->stage();
    const auto root = stage->GetRootLayer();
    const auto child = SdfLayer::CreateAnonymous("font-child.usda");
    root->SetSubLayerPaths({ child->GetIdentifier() });
    UsdGeomXform::Define(stage, SdfPath("/RootOwned"));
    const auto payloadStage = UsdStage::CreateInMemory();
    UsdGeomXform::Define(payloadStage, SdfPath("/Asset"));
    UsdGeomCube::Define(payloadStage, SdfPath("/Asset/Cube"));
    const auto payload = UsdGeomXform::Define(stage, SdfPath("/Payload")).GetPrim();
    require(payload.GetPayloads().AddPayload(payloadStage->GetRootLayer()->GetIdentifier(), SdfPath("/Asset")),
            "add font fixture payload");
    require(current->setEditLayer(child), "select font fixture sublayer");
    UsdGeomXform::Define(stage, SdfPath("/ChildOwned"));
    require(current->setEditLayer(root), "select root edit layer");
    auto* tree = view.findChild<stageviz::OutlinerTree*>("outlinerTree");
    require(tree, "find Outliner font tree");
    const auto drain = []() {
        for (int i = 0; i < 6; ++i) {
            QThreadPool::globalInstance()->waitForDone();
            QCoreApplication::sendPostedEvents(nullptr, 0);
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        }
    };
    const auto italic = [&](const QString& path) {
        QTreeWidgetItemIterator it(tree);
        while (*it) {
            if ((*it)->data(0, stageviz::PrimItem::Path).toString() == path)
                return (*it)->font(0).italic();
            ++it;
        }
        throw std::runtime_error("missing Outliner font row");
    };
    drain();
    require(!italic("/") && !italic("/RootOwned") && italic("/ChildOwned"),
            "only prims absent from the root edit layer are italic");
    require(!italic("/Payload") && italic("/Payload/Cube"),
            "payload root uses normal font while external contents are italic");
    require(current->setEditLayer(child), "switch to child edit layer");
    drain();
    require(!italic("/") && italic("/RootOwned") && !italic("/ChildOwned"),
            "italics refresh when the edit layer changes");
    require(current->setEditLayer(root), "restore root edit layer");
    drain();
    current->commandStack()->execute(new stageviz::Command(stageviz::hidePaths({ SdfPath("/ChildOwned") }, false)));
    drain();
    require(root->GetPrimAtPath(SdfPath("/ChildOwned")) && !italic("/ChildOwned"),
            "authoring an edit-layer override removes italics");
    current->commandStack()->execute(new stageviz::Command(stageviz::resetOverrides({ SdfPath("/ChildOwned") })));
    drain();
    require(!root->GetPrimAtPath(SdfPath("/ChildOwned")) && italic("/ChildOwned"),
            "resetting the override restores italics");
    current->commandStack()->undo();
    drain();
    require(!italic("/ChildOwned"), "undo restores the override and its normal font");
    current->commandStack()->redo();
    drain();
    require(italic("/ChildOwned"), "redo removes the override and restores italics");
}

void
outlinerDefinedPrimsApi()
{
    QTemporaryDir dir;
    require(dir.isValid(), "temporary defined prims directory");
    const auto fixture = UsdStage::CreateInMemory();
    fixture->DefinePrim(SdfPath("/Source"), TfToken("Xform"));
    UsdGeomCube::Define(fixture, SdfPath("/Source/Cube"));
    const UsdPrim instance = fixture->DefinePrim(SdfPath("/Instance"), TfToken("Xform"));
    require(instance.GetReferences().AddInternalReference(SdfPath("/Source")), "reference instance source");
    require(instance.SetInstanceable(true), "enable fixture instancing");
    fixture->DefinePrim(SdfPath("/UntypedGroup"));
    fixture->DefinePrim(SdfPath("/Flattened_Prototype_UserGroup"));

    const QString assetPath = dir.filePath("asset.usda");
    const auto asset = UsdStage::CreateNew(assetPath.toStdString());
    UsdGeomCube::Define(asset, SdfPath("/Geometry"));
    require(asset->GetRootLayer()->Save(), "save payload fixture");
    const QString file = dir.filePath("flattened.usda");
    require(fixture->Export(file.toStdString()), "flatten instance fixture");
    const auto flattened = UsdStage::Open(file.toStdString());
    require(flattened->DefinePrim(SdfPath("/Inactive"), TfToken("Xform")).SetActive(false), "deactivate fixture prim");
    flattened->OverridePrim(SdfPath("/UnresolvedOver/Nested"));
    const UsdPrim payload = flattened->DefinePrim(SdfPath("/Payload"), TfToken("Xform"));
    require(payload.GetPayloads().AddPayload(assetPath.toStdString(), SdfPath("/Geometry")), "add payload fixture");
    require(flattened->GetRootLayer()->Save(), "save unloaded payload fixture");

    stageviz::OutlinerView view;
    auto* current = stageviz::session();
    require(current->loadFromFile(file, stageviz::Session::None), "load flattened stage with unloaded payload");
    auto* tree = view.findChild<stageviz::OutlinerTree*>("outlinerTree");
    require(tree, "find defined prim Outliner");
    const auto drain = []() {
        for (int i = 0; i < 6; ++i) {
            QThreadPool::globalInstance()->waitForDone();
            QCoreApplication::sendPostedEvents(nullptr, 0);
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        }
    };
    const auto find = [&](const SdfPath& path) -> QTreeWidgetItem* {
        QTreeWidgetItemIterator it(tree);
        while (*it) {
            if ((*it)->data(0, stageviz::PrimItem::Path).toString() == QString::fromStdString(path.GetString()))
                return *it;
            ++it;
        }
        return nullptr;
    };
    drain();
    const auto stage = current->stage();
    int helperCount = 0;
    for (const UsdPrim& prim : stage->GetPseudoRoot().GetAllChildren()) {
        if (!prim.IsDefined()) {
            require(!find(prim.GetPath()), "undefined root excluded from Outliner");
            if (prim.GetName().GetString().find("Flattened_Prototype_") == 0)
                ++helperCount;
        }
    }
    require(helperCount > 0, "USD flattening produced undefined prototype roots");
    require(!find(SdfPath("/UnresolvedOver/Nested")), "undefined descendants excluded");
    require(find(SdfPath("/UntypedGroup")), "defined untyped group remains visible");
    require(find(SdfPath("/Flattened_Prototype_UserGroup")), "defined group with prototype-like name remains visible");
    require(find(SdfPath("/Inactive")), "inactive defined prim remains visible");
    require(find(SdfPath("/Payload")) && !stage->GetPrimAtPath(SdfPath("/Payload")).IsLoaded(),
            "unloaded payload remains visible");
    require(stage->GetPrimAtPath(SdfPath("/Instance")).IsInstance() && find(SdfPath("/Instance/Cube")),
            "instance and its proxy geometry remain visible");

    const SdfPath changing("/Changing");
    stage->OverridePrim(changing);
    drain();
    require(!find(changing), "new undefined prim stays hidden during resync");
    stage->DefinePrim(changing);
    stage->DefinePrim(changing.AppendChild(TfToken("Child")));
    drain();
    require(find(changing) && find(changing.AppendChild(TfToken("Child"))), "newly defined subtree becomes visible");
    stage->GetRootLayer()->GetPrimAtPath(changing)->SetSpecifier(SdfSpecifierOver);
    drain();
    require(!find(changing) && !find(changing.AppendChild(TfToken("Child"))),
            "undefined subtree removed during resync");
    stage->DefinePrim(changing);
    drain();
    require(find(changing) && find(changing.AppendChild(TfToken("Child"))), "redefined subtree returns during resync");
}

void
outlinerEmptyGroupCheckboxApi()
{
    QTemporaryDir dir;
    require(dir.isValid(), "temporary empty group checkbox directory");
    const auto asset = UsdStage::CreateNew(dir.filePath("asset.usda").toStdString());
    UsdGeomXform::Define(asset, SdfPath("/Asset"));
    require(asset->GetRootLayer()->Save(), "save checkbox asset");
    const QString layerFile = dir.filePath("branch.usda");
    const auto branch = UsdStage::CreateNew(layerFile.toStdString());
    UsdGeomXform::Define(branch, SdfPath("/World/Branch"));
    const auto branchGate = UsdGeomXform::Define(branch, SdfPath("/World/Branch/Gate")).GetPrim();
    require(branchGate.GetPayloads().AddPayload("asset.usda", SdfPath("/Asset")), "author branch payload");
    require(branch->GetRootLayer()->Save(), "save checkbox branch");
    const QString file = dir.filePath("scene.usda");
    const auto fixture = UsdStage::CreateNew(file.toStdString());
    UsdGeomXform::Define(fixture, SdfPath("/World/Branch"));
    const auto sibling = UsdGeomXform::Define(fixture, SdfPath("/World/Sibling")).GetPrim();
    require(sibling.GetPayloads().AddPayload("asset.usda", SdfPath("/Asset")), "author sibling payload");
    UsdGeomCube::Define(fixture, SdfPath("/World/AlwaysVisible"));
    fixture->GetRootLayer()->SetSubLayerPaths({ "branch.usda" });
    require(fixture->GetRootLayer()->Save(), "save checkbox scene");

    stageviz::OutlinerView view;
    auto* current = stageviz::session();
    require(current->loadFromFile(file, stageviz::Session::None), "load checkbox scene");
    auto* tree = view.findChild<stageviz::OutlinerTree*>("outlinerTree");
    require(tree, "find empty group checkbox tree");
    const auto drain = []() {
        for (int i = 0; i < 6; ++i) {
            QThreadPool::globalInstance()->waitForDone();
            QCoreApplication::sendPostedEvents(nullptr, 0);
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        }
    };
    const auto state = [&](const QString& path) {
        QTreeWidgetItemIterator it(tree);
        while (*it) {
            if ((*it)->data(0, stageviz::PrimItem::Path).toString() == path)
                return (*it)->checkState(0);
            ++it;
        }
        throw std::runtime_error("missing empty group checkbox row");
    };
    const auto run = [&](stageviz::Command command) {
        current->commandStack()->execute(new stageviz::Command(std::move(command)));
        drain();
    };
    run(stageviz::loadPayloads({ SdfPath("/World/Branch/Gate"), SdfPath("/World/Sibling") }));
    for (int i = 0; i < 100; ++i)
        require(state("/") == Qt::Checked, "loaded payloads stay checked; ordinary geometry is excluded");
    run(stageviz::setLayerMuted(layerFile, true));
    require(state("/World/Branch") == Qt::Unchecked && state("/World") == Qt::PartiallyChecked
                && state("/") == Qt::PartiallyChecked,
            "empty group contributes unchecked after its payload layer is muted");
    current->commandStack()->undo();
    drain();
    require(state("/") == Qt::Checked, "unmuting restores cached parent state");
    current->commandStack()->redo();
    drain();
    require(state("/") == Qt::PartiallyChecked, "redo invalidates cached aggregate");
    current->commandStack()->undo();
    drain();
    run(stageviz::newPrimPath(SdfPath("/World"), "Target", TfToken("Xform"), false));
    require(state("/World/Target") == Qt::Unchecked && state("/") == Qt::PartiallyChecked,
            "inserting an empty group invalidates checked parent cache");
    run(stageviz::movePath({ SdfPath("/World/Branch") }, SdfPath("/World/Target"), -1));
    require(state("/World/Target") == Qt::Checked && state("/") == Qt::Checked,
            "moving populated children into the empty group invalidates both branches");
    current->commandStack()->undo();
    drain();
    require(state("/") == Qt::PartiallyChecked, "undoing the move restores the empty group");
    current->commandStack()->undo();
    drain();
    require(state("/") == Qt::Checked, "removing the empty group invalidates the parent cache");
    run(stageviz::unloadPayloads({ SdfPath("/World/Branch/Gate") }));
    require(state("/") == Qt::PartiallyChecked, "unloading a gate invalidates cached loaded state");
    current->commandStack()->undo();
    drain();
    require(state("/") == Qt::Checked, "undo restores cached gate load state");
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
outlinerPayloadCheckboxApi(bool rapid)
{
    QTemporaryDir dir;
    require(dir.isValid(), "temporary checkbox fixture directory");
    const QString asset = dir.filePath("asset.usda");
    const QString scene = dir.filePath("scene.usda");
    {
        const auto stage = UsdStage::CreateNew(asset.toStdString());
        stage->DefinePrim(SdfPath("/Asset"), TfToken("Xform"));
        stage->DefinePrim(SdfPath("/Asset/Body"), TfToken("Cube"));
        require(stage->GetRootLayer()->Save(), "save checkbox payload asset");
    }
    {
        const auto stage = UsdStage::CreateNew(scene.toStdString());
        stage->DefinePrim(SdfPath("/World"), TfToken("Xform"));
        stage->DefinePrim(SdfPath("/World/Ordinary"), TfToken("Cube"));
        for (const char* path : { "/World/A", "/World/B" }) {
            const auto prim = stage->DefinePrim(SdfPath(path), TfToken("Xform"));
            require(prim.GetPayloads().AddPayload(asset.toStdString(), SdfPath("/Asset")), "add checkbox payload");
        }
        require(stage->GetRootLayer()->Save(), "save checkbox scene");
    }
    auto* current = stageviz::session();
    stageviz::OutlinerView view;
    require(current->loadFromFile(scene, stageviz::Session::None), "load checkbox scene in Payloads mode");
    const auto drain = []() {
        for (int i = 0; i < 8; ++i) {
            QCoreApplication::sendPostedEvents(nullptr, 0);
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
            QThreadPool::globalInstance()->waitForDone();
        }
    };
    drain();
    auto* tree = view.findChild<stageviz::OutlinerTree*>(QStringLiteral("outlinerTree"));
    require(tree && tree->payloadEnabled(), "payload checkbox tree available");
    const auto row = [&](const char* path) {
        for (QTreeWidgetItemIterator it(tree); *it; ++it) {
            if ((*it)->data(0, stageviz::PrimItem::Path).toString() == QLatin1String(path))
                return *it;
        }
        return static_cast<QTreeWidgetItem*>(nullptr);
    };
    const auto loaded = [&](const char* path) { return current->stage()->GetPrimAtPath(SdfPath(path)).IsLoaded(); };
    auto* stack = current->commandStack();
    stack->clear();
    if (rapid) {
        row("/World/A")->setCheckState(0, Qt::Checked);
        row("/World/A")->setCheckState(0, Qt::Unchecked);
        drain();
        require(!loaded("/World/A") && row("/World/A")->checkState(0) == Qt::Unchecked,
                "queued opposite clicks retain the last requested state");
        require(!stack->canUndo(), "cancelled checkbox batch adds no command");
        row("/World/A")->setCheckState(0, Qt::Checked);
        row("/World/A")->setCheckState(0, Qt::Unchecked);
        row("/World/A")->setCheckState(0, Qt::Checked);
        drain();
        require(loaded("/World/A") && !loaded("/World/B"), "odd click burst loads only its target");
        stack->undo();
        drain();
        require(!loaded("/World/A"), "coalesced checkbox batch is undoable");
        stack->redo();
        drain();
        require(loaded("/World/A"), "coalesced checkbox batch is redoable");
        stack->clear();
        row("/World/B")->setCheckState(0, Qt::Checked);
        require(current->loadFromFile(scene, stageviz::Session::None), "replace stage with a checkbox batch pending");
        drain();
        require(!loaded("/World/A") && !loaded("/World/B"), "stage replacement cancels old checkbox timer");
    }
    else {
        row("/")->setCheckState(0, Qt::Checked);
        drain();
        require(loaded("/World/A") && loaded("/World/B"), "root checkbox loads all payload gates");
        require(row("/")->checkState(0) == Qt::Checked, "root is checked when all payload gates load");
        stack->undo();
        drain();
        require(!loaded("/World/A") && !loaded("/World/B"), "root checkbox undo unloads both gates");
        require(row("/")->checkState(0) == Qt::Unchecked && row("/World")->checkState(0) == Qt::Unchecked,
                "ordinary geometry does not leave grouping rows partially checked after undo");
        stack->redo();
        drain();
        row("/")->setCheckState(0, Qt::Unchecked);
        drain();
        stack->undo();
        drain();
        require(row("/")->checkState(0) == Qt::Checked, "undo root unload restores checked aggregation");
        stack->clear();
        current->commandStack()->execute(new stageviz::Command(stageviz::unloadPayloads({ SdfPath("/World/A") })));
        drain();
        require(row("/")->checkState(0) == Qt::PartiallyChecked, "mixed gates produce partial grouping state");
        stack->undo();
        drain();
        require(row("/")->checkState(0) == Qt::Checked, "command undo restores checked grouping state");
    }
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
layerTreeMultiSelectionApi()
{
    auto* current = stageviz::session();
    require(current->newStage(), "new multi-layer selection fixture");
    const auto stage = current->stage();
    const auto root = stage->GetRootLayer();
    const auto first = SdfLayer::CreateAnonymous("first.usda");
    const auto second = SdfLayer::CreateAnonymous("second.usda");
    require(first->ImportFromString("#usda 1.0\ndef Xform \"First\" { }\ndef Xform \"Shared\" { }\n"),
            "first layer prims");
    require(second->ImportFromString("#usda 1.0\ndef Xform \"Second\" { }\nover \"Shared\" { }\n"),
            "second layer prims");
    root->SetSubLayerPaths({ first->GetIdentifier(), second->GetIdentifier() });
    stageviz::LayerDialog dialog;
    dialog.resize(600, 450);
    dialog.show();
    auto* tree = dialog.findChild<stageviz::LayerTree*>("layerTree");
    require(tree && tree->selectionMode() == QAbstractItemView::ExtendedSelection,
            "layer tree enables extended selection");
    const auto find = [&](const SdfLayerHandle& layer) -> QTreeWidgetItem* {
        QTreeWidgetItemIterator it(tree);
        while (*it) {
            if (static_cast<stageviz::LayerItem*>(*it)->identifier() == QString::fromStdString(layer->GetIdentifier()))
                return *it;
            ++it;
        }
        return nullptr;
    };
    auto* firstRow = find(first);
    auto* secondRow = find(second);
    require(firstRow && secondRow, "both layer rows present");
    tree->setCurrentItem(firstRow);
    secondRow->setSelected(true);
    require(tree->selectedItems().size() == 2, "two layers selected");
    require(current->setEditLayer(second), "switch edit target while multi-selected");
    tree->refresh();
    require(tree->selectedItems().size() == 2 && firstRow->isSelected() && secondRow->isSelected(),
            "layer refresh and edit target changes preserve multi-selection");
    require(current->setEditLayer(root), "restore edit target");
    tree->selectCurrentLayer();
    const auto selectedPaths = current->selectionList()->paths();
    require(selectedPaths.size() == 3 && selectedPaths.contains(SdfPath("/First"))
                && selectedPaths.contains(SdfPath("/Second")) && selectedPaths.contains(SdfPath("/Shared")),
            "Select unions and deduplicates prim paths from all selected layers");
    const auto context = [&](const QString& actionName) {
        bool inspected = false;
        QTimer::singleShot(0, tree, [&]() {
            for (auto* widget : QApplication::topLevelWidgets()) {
                auto* menu = qobject_cast<QMenu*>(widget);
                if (!menu || !menu->isVisible())
                    continue;
                QAction* chosen = nullptr;
                QAction* previous = nullptr;
                for (auto* action : menu->actions()) {
                    if (action->objectName() == "layerSave") {
                        const auto actions = menu->actions();
                        const qsizetype index = actions.indexOf(action);
                        require(index >= 2 && actions[index - 1]->isSeparator()
                                    && actions[index - 2]->objectName() == "layerSetEdit",
                                "divider separates edit-layer selection from layer saves");
                    }
                    if (action->objectName() == "layerUpdate") {
                        require(previous && previous->objectName() == "layerSave", "Update layers follows Save layers");
                        require(action->text() == "Update layers" && !action->isEnabled(),
                                "anonymous selected layers cannot be updated from disk");
                    }
                    if (action->objectName() == "layerSetEdit")
                        require(!action->isEnabled(), "multiple selected layers cannot become one edit target");
                    if (action->objectName() == actionName)
                        chosen = action;
                    previous = action;
                }
                if (!chosen)
                    continue;
                require(tree->selectedItems().size() == 2, "right-click preserves existing multi-selection");
                require(chosen->isEnabled(), "batch action enabled");
                inspected = true;
                menu->setActiveAction(chosen);
                QKeyEvent key(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
                QApplication::sendEvent(menu, &key);
            }
        });
        const QPoint point = tree->visualItemRect(firstRow).center();
        QContextMenuEvent event(QContextMenuEvent::Mouse, point, tree->viewport()->mapToGlobal(point));
        QApplication::sendEvent(tree->viewport(), &event);
        require(inspected, "inspect and trigger layer batch action");
    };
    context("layerLock");
    require(!first->PermissionToEdit() && !second->PermissionToEdit(), "context action locks both selected layers");
    current->commandStack()->undo();
    require(first->PermissionToEdit() && second->PermissionToEdit(), "one undo restores both locks");
    context("layerMute");
    require(stage->IsLayerMuted(first->GetIdentifier()) && stage->IsLayerMuted(second->GetIdentifier()),
            "context action mutes both selected layers");
    require(tree->selectedItems().size() == 2, "muting retains selected rows");
    current->commandStack()->undo();
    require(!stage->IsLayerMuted(first->GetIdentifier()) && !stage->IsLayerMuted(second->GetIdentifier()),
            "one undo restores both mute states");
    require(current->newStage(), "replace multi-selected stage");
    tree->refresh();
    require(tree->selectedItems().size() == 1 && tree->selectedItems().first() == tree->topLevelItem(0),
            "new stage selects its root after old selected layers disappear");
}

void
mcpServerApi()
{
    const QByteArray previous = qgetenv("STAGEVIZ_TEST_MCP_PORT");
    qputenv("STAGEVIZ_TEST_MCP_PORT", "0");
    stageviz::McpServer server;
    require(server.start() && server.isRunning(), "MCP starts on an isolated loopback port");
    const QUrl url = server.url();
    require(url.host() == "127.0.0.1" && url.port() > 0 && url.path() == "/mcp",
            "MCP reports the actual assigned port");
    QNetworkAccessManager network;
    network.setProxy(QNetworkProxy(QNetworkProxy::NoProxy));
    const auto request = [&](const QByteArray& host, const QByteArray& origin) {
        QNetworkRequest request(url);
        request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
        request.setRawHeader("Host", host);
        if (!origin.isEmpty())
            request.setRawHeader("Origin", origin);
        auto* reply = network.post(request, R"JSON({"jsonrpc":"2.0","id":1,"method":"tools/list","params":{}})JSON");
        QEventLoop loop;
        QTimer timer;
        timer.setSingleShot(true);
        QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
        QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        timer.start(5000);
        if (!reply->isFinished())
            loop.exec();
        require(reply->isFinished(), "MCP HTTP request completes");
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray body = reply->readAll();
        reply->deleteLater();
        return std::make_pair(status, body);
    };
    const QByteArray host = "127.0.0.1:" + QByteArray::number(url.port());
    const auto valid = request(host, url.adjusted(QUrl::RemovePath).toEncoded());
    require(valid.first == 200 && valid.second.contains("stageviz_get_stage_info"),
            "isolated port accepts matching local Host and Origin and serves native tools");
    require(request("example.com", {}).first == 403, "MCP rejects non-local Host headers");
    const auto wrongPort = QByteArray::number(url.port() == 65535 ? 65534 : url.port() + 1);
    require(request("127.0.0.1:" + wrongPort, {}).first == 403, "MCP rejects a mismatched Host port");
    require(request(host, "http://127.0.0.1:" + wrongPort).first == 403, "MCP rejects a mismatched Origin port");
    server.stop();
    require(!server.isRunning(), "MCP listener stops");
    qputenv("STAGEVIZ_TEST_MCP_PORT", "65536");
    require(!server.start() && !server.errorString().isEmpty(), "invalid test ports fail explicitly");
    if (previous.isNull())
        qunsetenv("STAGEVIZ_TEST_MCP_PORT");
    else
        qputenv("STAGEVIZ_TEST_MCP_PORT", previous);
}

void
changesDialogApi()
{
    using Dialog = stageviz::ChangesDialog;
    const QList<Dialog::Entry> entries {
        { "root", "Untitled", Dialog::Type::RootLayer, "New layer", "No filename", "Choose filename on save", true },
        { "layer", "model.usda", Dialog::Type::Sublayer, "USD edits", "/example/model.usda", "Modified", true, "root" },
        { "state", "State", Dialog::Type::CompanionState, "View / layer state", "No filename",
          "Choose filename on save", true }
    };
    for (const auto result : { Dialog::Result::Save, Dialog::Result::Discard, Dialog::Result::Cancel }) {
        Dialog dialog;
        dialog.setDocumentName("Untitled");
        dialog.setEntries(entries);
        auto* tree = dialog.findChild<QTreeWidget*>("changesTree");
        require(tree && tree->columnCount() == 5 && tree->topLevelItemCount() == 2
                    && tree->topLevelItem(0)->childCount() == 1 && tree->topLevelItem(0)->isExpanded(),
                "changes table nests sublayers beneath their expanded root and keeps State separate");
        const QStringList headers { "Name", "Type", "Changes", "Location", "Status" };
        for (int column = 0; column < headers.size(); ++column)
            require(tree->headerItem()->text(column) == headers[column], "changes table headers");
        require(tree->topLevelItem(0)->child(0)->text(3) == "model.usda"
                    && tree->topLevelItem(0)->child(0)->toolTip(3) == "/example/model.usda",
                "locations show filenames with full paths available in tooltips");
        require(tree->rootIsDecorated() && tree->uniformRowHeights(), "layer hierarchy uses uniform rows");
        QTimer::singleShot(0, &dialog, [&]() {
            auto* header = tree->header();
            require(header->stretchLastSection(), "Status fills remaining dialog width");
            const int statusWidth = header->sectionSize(4);
            dialog.resize(dialog.width() + 120, dialog.height());
            QCoreApplication::processEvents();
            require(header->sectionSize(4) > statusWidth, "Status expands when the dialog widens");
            for (int column = 0; column < 5; ++column) {
                require(header->sectionResizeMode(column) == QHeaderView::Interactive,
                        "every changes-table header remains resizable");
                const int width = header->sectionSize(column);
                header->resizeSection(column, width + 24);
                require(header->sectionSize(column) >= width + 24, "every changes-table column can grow");
            }
            const char* button = result == Dialog::Result::Save      ? "saveAllButton"
                                 : result == Dialog::Result::Discard ? "discardButton"
                                                                     : "cancelButton";
            dialog.findChild<QPushButton*>(button)->click();
        });
        dialog.exec();
        require(dialog.choice() == result, "explicit Save all, Don't save and Cancel results");
    }
    Dialog blocked;
    auto unavailable = entries;
    unavailable[1].canSave = false;
    unavailable[1].status = "Read-only";
    blocked.setEntries(unavailable);
    blocked.setDiscardAllowed(false);
    require(!blocked.findChild<QPushButton*>("saveAllButton")->isEnabled(),
            "known unavailable writes disable Save all");
    require(blocked.findChild<QPushButton*>("discardButton")->isHidden(), "confirmation can hide discard");
    QTimer::singleShot(0, &blocked, [&]() {
        QKeyEvent key(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
        QApplication::sendEvent(&blocked, &key);
    });
    blocked.exec();
    require(blocked.choice() == Dialog::Result::Cancel, "Escape cancels the changes dialog");
    QTemporaryDir files;
    const QString filename = files.filePath("model.usda");
    QFile file(filename);
    require(file.open(QIODevice::WriteOnly), "create reveal fixture");
    file.close();
    Dialog revealDialog;
    auto fileEntry = entries[1];
    fileEntry.location = filename;
    fileEntry.parentIdentifier.clear();
    revealDialog.setEntries({ fileEntry });
    revealDialog.show();
    QCoreApplication::processEvents();
    auto* revealTree = revealDialog.findChild<QTreeWidget*>("changesTree");
    bool revealInspected = false;
    QTimer::singleShot(0, &revealDialog, [&]() {
        for (auto* widget : QApplication::topLevelWidgets()) {
            if (auto* menu = qobject_cast<QMenu*>(widget); menu && menu->isVisible()) {
                for (auto* action : menu->actions()) {
                    if (action->objectName() == "changesReveal") {
                        require(action->isEnabled() && action->data().toString() == filename,
                                "Reveal context action uses the original full path");
                        revealInspected = true;
                        menu->close();
                    }
                }
            }
        }
    });
    revealTree->customContextMenuRequested(revealTree->visualItemRect(revealTree->topLevelItem(0)).center());
    require(revealInspected, "existing save-target files expose Reveal in the context menu");
    revealDialog.reject();
    QList<Dialog::Entry> many;
    for (int i = 0; i < 1000; ++i) {
        auto entry = entries[1];
        entry.identifier = QString::number(i);
        entry.name = QString("Layer %1").arg(i);
        many.append(entry);
    }
    Dialog scrollable;
    scrollable.setEntries(many);
    scrollable.show();
    QCoreApplication::processEvents();
    const auto* tree = scrollable.findChild<QTreeWidget*>("changesTree");
    require(tree->topLevelItemCount() == 1000 && tree->verticalScrollBar()->maximum() > 0,
            "large change lists scroll inside the dialog");
    scrollable.reject();
}

void
preserveStateMenuApi()
{
    QTemporaryDir dir;
    stageviz::Viewer viewer;
    auto* current = stageviz::session();
    const bool originalPreserve = viewer.findChild<QAction*>("filePreserveState")->isChecked();
    const auto drain = []() {
        for (int i = 0; i < 6; ++i) {
            QThreadPool::globalInstance()->waitForDone();
            QCoreApplication::sendPostedEvents(nullptr, 0);
            QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
        }
    };
    require(current->newStage(), "new Save state menu fixture");
    current->setPreserveState(true);
    current->viewState()->camera()->setCameraDistance(4);
    drain();
    require(!current->isStateDirty() && !viewer.findChild<QAction*>("fileStateSave")->isEnabled()
                && !viewer.windowTitle().contains('*'),
            "untouched Untitled camera changes do not enable Save state or dirty its title");
    viewer.findChild<QAction*>("fileNew")->trigger();
    drain();
    require(!current->isStateDirty(), "New discards pristine view changes without a review dialog");
    current->stage()->DefinePrim(SdfPath("/World/Cube"), TfToken("Cube"));
    current->viewState()->camera()->setCameraDistance(5);
    drain();
    bool anonymousReviewed = false;
    QTimer::singleShot(0, &viewer, [&]() {
        for (auto* widget : QApplication::topLevelWidgets()) {
            auto* dialog = qobject_cast<QDialog*>(widget);
            if (!dialog || !dialog->isVisible() || dialog->objectName() != "changesDialog")
                continue;
            const auto* tree = dialog->findChild<QTreeWidget*>("changesTree");
            require(tree && tree->topLevelItem(0)->text(4) == "Choose filename on save"
                        && dialog->findChild<QPushButton*>("saveAllButton")->isEnabled(),
                    "anonymous root can be named and saved despite lacking an in-place save path");
            anonymousReviewed = true;
            dialog->reject();
        }
    });
    viewer.findChild<QAction*>("fileNew")->trigger();
    require(anonymousReviewed, "review anonymous root companion changes");
    const QString filename = dir.filePath("root.usda");
    require(current->saveToFile(filename), "save root baseline");
    drain();
    auto* save = viewer.findChild<QAction*>("fileStateSave");
    require(save && save->isVisible(), "Save state is visible with preservation on");
    auto* fileMenu = viewer.findChild<QMenu*>("menuFile");
    require(fileMenu, "Save state belongs to the File menu");
    const auto fileActions = fileMenu->actions();
    const qsizetype stateIndex = fileActions.indexOf(save);
    require(stateIndex >= 0 && stateIndex + 2 < fileActions.size() && fileActions[stateIndex + 1]->isSeparator()
                && fileActions[stateIndex + 2]->objectName() == "fileLayerSave",
            "Save state precedes Save layer with a divider");
    current->viewState()->camera()->setCameraDistance(current->viewState()->camera()->cameraDistance() + 2);
    drain();
    require(save->isEnabled() && viewer.windowTitle().contains('*'),
            "camera enables Save state and dirties root title");
    auto* regularSave = viewer.findChild<QAction*>("fileLayerSave");
    require(regularSave->isEnabled(), "ordinary Save is enabled for state-only changes");
    regularSave->trigger();
    drain();
    require(!save->isEnabled() && !current->isStateDirty() && !viewer.windowTitle().contains('*'),
            "Save state action clears state dirtiness and title");
    current->setPreserveState(false);
    current->viewState()->camera()->setCameraDistance(current->viewState()->camera()->cameraDistance() + 1);
    drain();
    require(save->isVisible() && !save->isEnabled() && !viewer.windowTitle().contains('*'),
            "preservation off keeps Save state visible but disabled and ignores camera dirtiness in UI");
    current->setPreserveState(true);
    drain();
    require(save->isVisible() && save->isEnabled(), "enabling preservation exposes pending state changes");
    bool prompted = false;
    const auto scheduleCancel = [&]() {
        QTimer::singleShot(0, &viewer, [&]() {
            for (auto* widget : QApplication::topLevelWidgets()) {
                auto* dialog = qobject_cast<QDialog*>(widget);
                if (!dialog || !dialog->isVisible() || dialog->objectName() != "changesDialog")
                    continue;
                const auto* tree = dialog->findChild<QTreeWidget*>("changesTree");
                require(tree && tree->topLevelItemCount() == 1 && tree->topLevelItem(0)->text(1) == "State"
                            && tree->topLevelItem(0)->text(3) == QFileInfo(filename + ".session").fileName()
                            && tree->topLevelItem(0)->toolTip(3) == filename + ".session",
                        "state-only changes table names the root companion");
                prompted = true;
                dialog->reject();
            }
        });
    };
    scheduleCancel();
    viewer.findChild<QAction*>("fileNew")->trigger();
    require(prompted && current->filename() == filename && current->isStateDirty(),
            "Cancel preserves current dirty state");
    prompted = false;
    scheduleCancel();
    viewer.openFile(filename);
    require(prompted && current->isStateDirty(), "Open also asks before discarding root companion state");
    save->trigger();
    drain();
    current->close();
    current->setPreserveState(originalPreserve);
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
    for (const char* mode : { "Move", "Rotate", "Scale" }) {
        for (const char* axis : { "X", "Y", "Z" }) {
            const UsdGeomMesh mesh(
                host.auxiliary->GetPrimAtPath(gizmoPath.AppendChild(TfToken(mode)).AppendChild(TfToken(axis))));
            TfToken scheme;
            require(mesh && mesh.GetSubdivisionSchemeAttr().Get(&scheme) && scheme == UsdGeomTokens->none,
                    "fully tessellated gizmo handles do not enter the subdivision rendering path");
        }
    }
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
viewportCapturePathsApi()
{
    auto* current = stageviz::session();
    stageviz::RenderView view;
    view.resize(640, 480);
    view.show();
    require(current->newStage(), "create visible path capture stage");
    const auto stage = current->stage();
    const auto source = UsdGeomXform::Define(stage, SdfPath("/World/Left"));
    source.AddTranslateOp().Set(GfVec3d(-2, 0, 0));
    UsdGeomCube::Define(stage, SdfPath("/World/Left/Body")).CreateSizeAttr(VtValue(1.0));
    const auto instance = UsdGeomXform::Define(stage, SdfPath("/World/Right"));
    instance.GetPrim().GetReferences().AddInternalReference(source.GetPath());
    instance.GetPrim().SetInstanceable(true);
    instance.GetPrim().GetAttribute(TfToken("xformOp:translate")).Set(GfVec3d(2, 0, 0));
    auto* camera = current->viewState()->camera();
    camera->setCameraUp(stageviz::ViewCamera::CameraUp::Y);
    camera->setAxisYaw(0);
    camera->setAxisPitch(0);
    camera->setAxisRoll(0);
    camera->frame(GfBBox3d(GfRange3d(GfVec3d(-3, -1, -1), GfVec3d(3, 1, 1))));
    auto* viewport = view.findChild<stageviz::ViewportWidget*>();
    require(viewport, "render view owns the capture viewport");
    QElapsedTimer timer;
    timer.start();
    while (!viewport->isValid() && timer.elapsed() < 10000) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
        QThread::msleep(5);
    }
    require(viewport->isValid(), "initialize on-screen capture viewport");
    int ready = 0;
    QObject::connect(&view, &stageviz::RenderView::captureReady, &view, [&ready](qint64) { ++ready; });
    std::string before;
    stage->GetRootLayer()->ExportToString(&before);
    const bool canUndo = current->commandStack()->canUndo();

    const SdfPath left("/World/Left/Body"), right("/World/Right/Body");
    QOpenGLContext* previousContext = QOpenGLContext::currentContext();
    const auto scan = view.capturePaths(false);
    require(QOpenGLContext::currentContext() == previousContext, "capture restores the caller's GL context");
    require(scan.size() == 2 && scan.contains(left) && scan.contains(right),
            "tiled scan resolves source and instance proxy");
    require(view.visibleCapturePaths().isEmpty(), "fresh scan leaves menu collection empty");
    current->setMask({ left });
    require(view.capturePaths() == QList<SdfPath> { left }, "capture immediately synchronizes the left mask");
    current->setMask({});
    const auto unmasked = view.capturePaths(false);
    require(unmasked.size() == 2 && unmasked.contains(left) && unmasked.contains(right),
            "fresh scan immediately synchronizes the cleared mask, including instance proxies");
    require(view.visibleCapturePaths() == QList<SdfPath> { left }, "fresh scan preserves earlier collection");
    view.captureVisible();  // Existing native callers delegate to the same implementation.
    require(view.visibleCapturePaths() == QList<SdfPath> { left, right }, "legacy capture shares ordered collection");
    require(view.capturePaths() == QList<SdfPath> { left, right }, "repeated capture suppresses duplicates");
    require(ready == 5, "both native entry points publish the capture completion signal");
    view.clearVisibleCapture();
    view.clearVisibleCapture();
    require(view.visibleCapturePaths().isEmpty(), "clearing the collection is idempotent");
    require(current->selectionList()->paths().isEmpty(), "path captures leave selection unchanged");
    require(current->commandStack()->canUndo() == canUndo, "path captures do not create command history");
    std::string after;
    stage->GetRootLayer()->ExportToString(&after);
    require(after == before, "path captures do not author USD");
    require(current->close(), "close captured stage");
    require(view.visibleCapturePaths().isEmpty() && view.capturePaths().isEmpty(), "closed stage has no captures");
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
viewportTransformSelectionRefreshApi()
{
    auto* current = stageviz::session();
    require(current->newStage(), "new selection refresh stage");
    const auto first = UsdGeomXform::Define(current->stage(), SdfPath("/World/First"));
    first.AddTranslateOp().Set(GfVec3d(1, 2, 3));
    const auto second = UsdGeomXform::Define(current->stage(), SdfPath("/World/Second"));
    second.AddTranslateOp().Set(GfVec3d(4, 5, 6));
    ViewportTestHost host;
    stageviz::ViewportSelection selection(host);
    host.selection = &selection;
    host.selected = { first.GetPath() };
    stageviz::ViewportTransform transform(host);
    GfVec3d shown(0);
    int refreshes = 0;
    QObject::connect(&host.viewport, &stageviz::ViewportWidget::transformInputValuesChanged,
                     [&shown, &refreshes](const GfVec3d& values) {
                         shown = values;
                         ++refreshes;
                     });
    transform.setMode(stageviz::ViewportTransform::Mode::Move, true);
    require(shown == GfVec3d(1, 2, 3), "enabling a tool resolves the current selection");
    host.selected = { first.GetPath(), second.GetPath() };
    refreshes = 0;
    transform.selectionChanged(false);
    require(refreshes == 1 && shown == GfVec3d(4, 5, 6), "selection refresh publishes the lead object's values once");
    host.selected = { second.GetPath(), first.GetPath() };
    refreshes = 0;
    transform.selectionChanged(false);
    require(refreshes == 1 && shown == GfVec3d(1, 2, 3), "reordering selection refreshes the new lead object once");
    host.selected.clear();
    refreshes = 0;
    transform.selectionChanged(true);
    require(refreshes == 1 && shown == GfVec3d(0), "clearing selection resets numeric values once");
    require(!transform.hasActiveHandle(), "clearing selection hides the handle");
    transform.setEnabled(false);
    host.selected = { second.GetPath() };
    transform.selectionChanged(false);
    transform.setMode(stageviz::ViewportTransform::Mode::Move, true);
    require(shown == GfVec3d(4, 5, 6) && transform.hasActiveHandle(),
            "enabling after a disabled-tool selection resolves fresh values and handle");
    second.GetTranslateOp().Set(GfVec3d(7, 8, 9));
    refreshes = 0;
    transform.invalidate();
    require(refreshes == 1 && shown == GfVec3d(7, 8, 9),
            "stage invalidation still refreshes numeric values without a selection change");
}

void
viewportGizmoDefaultFrameApi()
{
    const stageviz::ViewportGizmo::Frame frame;
    require(frame.referenceFrame == stageviz::ViewportGizmo::ReferenceFrame::Object,
            "transform gizmo defaults to the object reference frame");
}

void
outlinerLayerMuteApi()
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
    info.path = SdfPath("/World/Keep.visibility");
    info.changedInfoOnly = true;
    mixed.entries.append(info);
    tree->updatePrims(mixed);
    require(find("/World/Keep") == keep && keep->childCount() == 500, "mixed deletes preserve surviving hierarchy");
    session->commandStack()->undo();
    drain();
    require(!session->stage()->IsLayerMuted(layerPath.toStdString()) && find("/World/Delete19"),
            "undo restores composed prims and Qt rows");
    session->commandStack()->redo();
    drain();
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
        { "viewport_capture_paths_api", viewportCapturePathsApi },
        { "viewport_lights_api", viewportLightsApi },
        { "viewport_transform_pivot_api", viewportTransformPivotApi },
        { "viewport_transform_hierarchy_sync_api", viewportTransformHierarchySyncApi },
        { "viewport_transform_individual_rotation_api", viewportTransformIndividualRotationApi },
        { "viewport_gizmo_default_frame_api", viewportGizmoDefaultFrameApi },
        { "viewport_transform_selection_refresh_api", viewportTransformSelectionRefreshApi },
        { "transform_operation_applied_api", transformOperationAppliedApi },
        { "pivot_operation_applied_api", pivotOperationAppliedApi },
        { "settings_api", settingsApi },
        { "treewidget_selection_api", treeWidgetSelectionApi },
        { "indextree_selection_api", indexTreeSelectionApi },
        { "indextree_selection_modes_api", indexTreeSelectionModesApi },
        { "ui_api", uiApi },
        { "numeric_editor_api", numericEditorApi },
        { "renderengine_api", renderEngineApi },
        { "render_primitive_selection_api", renderPrimitiveSelectionApi },
        { "renderengine_hdri_api", renderEngineHdriApi },
        { "material_browser_api", materialBrowserApi },
        { "property_reveal_file_api", propertyRevealFileApi },
        { "material_numeric_editor_api", materialNumericEditorApi },
        { "property_asset_editor_api", propertyAssetEditorApi },
        { "property_material_light_dropdown_api", propertyMaterialLightDropdownApi },
        { "material_swatch_alpha_api", materialSwatchAlphaApi },
        { "geometry_subset_gui_api", geometrySubsetGuiApi },
        { "outliner_reset_opinions_menu_api", outlinerResetOpinionsMenuApi },
        { "outliner_layer_colors_api", outlinerLayerColorsApi },
        { "outliner_edit_layer_font_api", outlinerEditLayerFontApi },
        { "outliner_defined_prims_api", outlinerDefinedPrimsApi },
        { "outliner_empty_group_checkbox_api", outlinerEmptyGroupCheckboxApi },
        { "outliner_instance_payload_resync_api", outlinerInstancePayloadResyncApi },
        { "outliner_payload_group_checkbox_api", []() { outlinerPayloadCheckboxApi(false); } },
        { "outliner_payload_queued_checkbox_api", []() { outlinerPayloadCheckboxApi(true); } },
        { "changes_dialog_api", changesDialogApi },
        { "mcp_server_api", mcpServerApi },
        { "preserve_state_menu_api", preserveStateMenuApi },
        { "layer_tree_root_row_api", layerTreeRootRowApi },
        { "layer_tree_multi_selection_api", layerTreeMultiSelectionApi },
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
