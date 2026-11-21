// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell

#include "application.h"
#include "bboxutils.h"
#include "changesdialog.h"
#include "colormanagement.h"
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
#include "console.h"
#include "gizmoprojection.h"
#include "indextree.h"
#include "layerdialog.h"
#include "layeritem.h"
#include "layertree.h"
#include "materialbrowser.h"
#include "materialdialog.h"
#include "materialgraph.h"
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
#include "qtutils.h"
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
#include <OpenColorIO/OpenColorIO.h>
#include <QApplication>
#include <QColor>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QCoreApplication>
#include <QCursor>
#include <QDialog>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileDialog>
#include <QFont>
#include <QGraphicsRectItem>
#include <QGraphicsScene>
#include <QGraphicsSimpleTextItem>
#include <QGraphicsView>
#include <QHash>
#include <QHeaderView>
#include <QImage>
#include <QItemSelectionModel>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
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
#include <QSplitter>
#include <QStyle>
#include <QStyleOptionSpinBox>
#include <QSurfaceFormat>
#include <QTemporaryDir>
#include <QTextDocument>
#include <QThread>
#include <QThreadPool>
#include <QTimer>
#include <QToolButton>
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
#include <pxr/base/tf/errorMark.h>
#include <pxr/imaging/hgi/blitCmds.h>
#include <pxr/imaging/hgi/blitCmdsOps.h>
#include <pxr/imaging/hgiGL/hgi.h>
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
#include <set>
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
    {
        QSplitter splitter;
        splitter.addWidget(new QWidget());
        splitter.addWidget(new QWidget());
        auto* handle = splitter.handle(1);
        handle->ensurePolished();
        QApplication::setOverrideCursor(Qt::CrossCursor);
        QMouseEvent press(QEvent::MouseButtonPress, QPointF(2, 2), QPointF(2, 2), Qt::LeftButton, Qt::LeftButton,
                          Qt::NoModifier);
        QApplication::sendEvent(handle, &press);
        require(QApplication::overrideCursor() && QApplication::overrideCursor()->shape() == Qt::SplitHCursor,
                "splitter drag pins the resize cursor");
        handle->setCursor(Qt::ArrowCursor);
        require(QApplication::overrideCursor()->shape() == Qt::SplitHCursor,
                "widget cursor changes do not replace an active splitter drag cursor");
        QMouseEvent release(QEvent::MouseButtonRelease, QPointF(2, 2), QPointF(2, 2), Qt::LeftButton, Qt::NoButton,
                            Qt::NoModifier);
        QApplication::sendEvent(handle, &release);
        require(QApplication::overrideCursor() && QApplication::overrideCursor()->shape() == Qt::CrossCursor,
                "splitter release restores the prior cursor override");
        QApplication::restoreOverrideCursor();

        QTreeWidget tree;
        tree.setColumnCount(2);
        tree.ensurePolished();
        auto* header = tree.header();
        header->ensurePolished();
        header->setStretchLastSection(false);
        header->resizeSection(0, 100);
        const int edge = header->sectionViewportPosition(0) + header->sectionSize(0);
        for (int offset : { -7, 7 }) {
            const QPointF position(edge + offset, 4);
            QMouseEvent hover(QEvent::MouseMove, position, position, Qt::NoButton, Qt::NoButton, Qt::NoModifier);
            QApplication::sendEvent(header->viewport(), &hover);
            require(header->viewport()->cursor().shape() == Qt::SplitHCursor,
                    "resize hover cursor covers both sides of the widened header grip");
        }
        for (int offset : { -8, 8, 10 }) {
            const QPointF outside(edge + offset, 4);
            QMouseEvent hoverOutside(QEvent::MouseMove, outside, outside, Qt::NoButton, Qt::NoButton, Qt::NoModifier);
            QApplication::sendEvent(header->viewport(), &hoverOutside);
            require(header->viewport()->cursor().shape() != Qt::SplitHCursor,
                    "header hover cursor returns to normal outside Qt's actual grip");
        }
        const QPointF position(edge - 7, 4);
        QMouseEvent headerPress(QEvent::MouseButtonPress, position, position, Qt::LeftButton, Qt::LeftButton,
                                Qt::NoModifier);
        QApplication::sendEvent(header->viewport(), &headerPress);
        require(QApplication::overrideCursor() && QApplication::overrideCursor()->shape() == Qt::SplitHCursor,
                "column resize pins the header cursor");
        header->unsetCursor();
        require(QApplication::overrideCursor()->shape() == Qt::SplitHCursor,
                "header cursor changes do not replace an active column drag cursor");
        QApplication::sendEvent(header->viewport(), &release);
        require(!QApplication::overrideCursor(), "column release clears its temporary cursor override");
    }
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
    require(ui.iconPath(stageviz::UI::Arrow) == QStringLiteral(":/icons/resources/Arrow.png")
                && !ui.icon(stageviz::UI::Arrow).isNull(),
            "ui resolves the Arrow icon resource");
    require(ui.iconPath(stageviz::UI::Display) == QStringLiteral(":/icons/resources/Display.png")
                && !ui.icon(stageviz::UI::Display).isNull(),
            "ui resolves the Display icon resource");
    require(ui.iconPath(stageviz::UI::Instance) == QStringLiteral(":/icons/resources/Instance.png")
                && !ui.icon(stageviz::UI::Instance).isNull(),
            "ui resolves the instance-parent icon resource");
    require(ui.iconPath(stageviz::UI::Proxy) == QStringLiteral(":/icons/resources/Proxy.png")
                && !ui.icon(stageviz::UI::Proxy).isNull(),
            "ui resolves the separate instance-proxy icon resource");

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
renderColorManagementApi(bool useOpenGL)
{
    namespace OCIO = OCIO_NAMESPACE;
    QOpenGLContext context;
    if (useOpenGL) {
        auto format = context.format();
        format.setVersion(4, 5);
        format.setProfile(QSurfaceFormat::CoreProfile);
        context.setFormat(format);
    }
    require(context.create(), "create color transform context");
    QOffscreenSurface surface;
    surface.setFormat(context.format());
    surface.create();
    require(surface.isValid() && context.makeCurrent(&surface), "activate color transform context");
    {
        HgiUniquePtr hgi = useOpenGL ? HgiUniquePtr(new HgiGL()) : Hgi::CreatePlatformDefaultHgi();
        require(bool(hgi), "create color transform Hgi");
        hgi->StartFrame();
        // Include negatives, HDR values, alpha coverage and a neutral gray.
        const std::vector<float> pixels { .18f, .18f, .18f, 1, 0,     0,   0,   1, 1,    1,     1,   1,
                                          4,    .5f,  .02f, 1, -.01f, .1f, .5f, 1, .09f, .045f, .2f, .5f };
        HgiTextureDesc desc;
        desc.debugName = "Color regression input";
        desc.dimensions = GfVec3i(6, 1, 1);
        desc.format = HgiFormatFloat32Vec4;
        desc.usage = HgiTextureUsageBitsShaderRead | HgiTextureUsageBitsColorTarget;
        desc.pixelsByteSize = pixels.size() * sizeof(float);
        desc.initialData = pixels.data();
        auto input = hgi->CreateTexture(desc);
        desc.initialData = nullptr;
        auto output = hgi->CreateTexture(desc);
        require(bool(input) && bool(output), "allocate color regression textures");
        {
            stageviz::ColorTransform transform(hgi.get());
            const auto config = OCIO::Config::CreateFromBuiltinConfig(
                stageviz::colorManagementConfig().toUtf8().constData());
            for (const QString& view : stageviz::colorManagementViews()) {
                for (const auto& adjustment : { std::pair<float, float>(0, 1), { 1, 1 }, { -1, 1.5f } }) {
                    stageviz::ColorManagementSettings settings;
                    settings.view = view;
                    settings.exposure = adjustment.first;
                    settings.gamma = adjustment.second;
                    require(transform.prepare(settings, desc.format), "prepare OCIO GPU processor");
                    transform.draw(input, output, settings);
                    std::vector<float> actual(pixels.size(), -99);
                    HgiTextureGpuToCpuOp read;
                    read.gpuSourceTexture = output;
                    read.cpuDestinationBuffer = actual.data();
                    read.destinationBufferByteSize = actual.size() * sizeof(float);
                    auto blit = hgi->CreateBlitCmds();
                    blit->CopyTextureGpuToCpu(read);
                    hgi->SubmitCmds(blit.get(), HgiSubmitWaitTypeWaitUntilCompleted);
                    OCIO::ConstCPUProcessorRcPtr cpu;
                    if (view != "Raw") {
                        auto display = OCIO::DisplayViewTransform::Create();
                        display->setSrc("Linear Rec.709 (sRGB)");
                        display->setDisplay("sRGB - Display");
                        display->setView(view.toUtf8().constData());
                        cpu = config->getProcessor(display)->getDefaultCPUProcessor();
                    }
                    for (size_t i = 0; i < pixels.size(); i += 4) {
                        const float alpha = pixels[i + 3];
                        float expected[4] { pixels[i] / alpha, pixels[i + 1] / alpha, pixels[i + 2] / alpha, alpha };
                        for (int c = 0; c < 3; ++c)
                            expected[c] *= std::exp2(settings.exposure);
                        if (cpu)
                            cpu->applyRGBA(expected);
                        for (int c = 0; c < 3; ++c) {
                            const float value = expected[c];
                            expected[c] = std::copysign(std::pow(std::abs(value), 1.0f / settings.gamma), value)
                                          * alpha;
                            require(std::isfinite(actual[i + c]) && std::abs(actual[i + c] - expected[c]) < .003f,
                                    "OCIO GPU agrees with CPU reference for SDR/HDR/negative/alpha values");
                        }
                        require(std::abs(actual[i + 3] - alpha) < .00001f, "display transform preserves alpha");
                    }
                }
            }
        }
        hgi->DestroyTexture(&input);
        hgi->DestroyTexture(&output);
        hgi->EndFrame();
    }
    context.doneCurrent();
}

void
renderDisplayModeWarmupApi()
{
    const auto warmupCount = []() {
        QFile trace(qEnvironmentVariable("STAGEVIZ_PERF_LOG"));
        require(trace.open(QIODevice::ReadOnly), "display mode initialization trace exists");
        int count = 0;
        for (const QByteArray& line : trace.readAll().split('\n'))
            count += QJsonDocument::fromJson(line).object().value("phase").toString()
                     == "viewport.display_modes.warmup";
        return count;
    };
    QOpenGLContext context;
    require(context.create(), "create display mode context");
    QOffscreenSurface surface;
    surface.setFormat(context.format());
    surface.create();
    require(surface.isValid() && context.makeCurrent(&surface), "activate display mode context");
    {
        QOpenGLFramebufferObjectFormat format;
        format.setAttachment(QOpenGLFramebufferObject::CombinedDepthStencil);
        QOpenGLFramebufferObject framebuffer(128, 128, format);
        const auto stage = UsdStage::CreateInMemory();
        UsdGeomCube::Define(stage, SdfPath("/Cube"));
        stageviz::RenderEngine viewport(stageviz::RenderEngine::ContextMode::Current);
        viewport.setStage(stage);
        GfCamera camera;
        GfMatrix4d view(1.);
        view.SetLookAt(GfVec3d(4, 3, 8), GfVec3d(0), GfVec3d(0, 1, 0));
        camera.SetTransform(view.GetInverse());
        camera.SetPerspectiveFromAspectRatioAndFieldOfView(1., 45., GfCamera::FOVVertical);
        camera.SetClippingRange(GfRange1f(.1f, 100.f));
        viewport.setCamera(camera);
        viewport.setSize(GfVec2i(128));
        viewport.setViewport(GfVec4d(0, 0, 128, 128));
        auto settings = viewport.settings();
        settings.ambientOcclusion.enabled = false;
        viewport.setSettings(settings);
        const auto draw = [&](UsdImagingGLDrawMode mode) {
            auto current = viewport.settings();
            current.drawMode = mode;
            viewport.setSettings(current);
            require(framebuffer.bind(), "bind display mode framebuffer");
            TfErrorMark mark;
            for (int i = 0; i < 3; ++i)
                require(viewport.renderToCurrentFramebuffer(), "render requested display mode");
            require(mark.IsClean(), "display mode warmup renders without USD errors");
            require(viewport.settings().drawMode == mode, "hidden preparation preserves requested display mode");
            const QImage image = framebuffer.toImage();
            framebuffer.release();
            return image;
        };
        const QImage shaded = draw(UsdImagingGLDrawMode::DRAW_SHADED_SMOOTH);
        require(warmupCount() == 1, "both display modes warm once before first switch");
        const QImage wireframe = draw(UsdImagingGLDrawMode::DRAW_WIREFRAME_ON_SURFACE);
        require(wireframe != shaded, "wireframe switch changes the displayed image");
        require(warmupCount() == 1, "first wireframe switch reuses warmed display modes");
        require(draw(UsdImagingGLDrawMode::DRAW_SHADED_SMOOTH) == shaded,
                "returning to shaded restores the image without a hidden wireframe frame");
        require(warmupCount() == 1, "ordinary redraw and mode switches do not repeat warmup");
        settings.complexity = 1.1;
        viewport.setSettings(settings);
        draw(UsdImagingGLDrawMode::DRAW_SHADED_SMOOTH);
        require(warmupCount() == 2, "quality changes prepare both new representations once");
        draw(UsdImagingGLDrawMode::DRAW_WIREFRAME_ON_SURFACE);
        require(warmupCount() == 2, "wireframe at the new quality is already prepared");
        UsdGeomCube::Define(stage, SdfPath("/Other"));
        draw(UsdImagingGLDrawMode::DRAW_SHADED_SMOOTH);
        require(warmupCount() == 3, "new geometry refreshes display mode preparation once");
        UsdGeomXformable(stage->GetPrimAtPath(SdfPath("/Other"))).AddTranslateOp().Set(GfVec3d(.1, 0, 0));
        draw(UsdImagingGLDrawMode::DRAW_SHADED_SMOOTH);
        require(warmupCount() == 3, "ordinary transform edits reuse prepared representations");
        const auto auxiliary = UsdStage::CreateInMemory();
        UsdGeomXform::Define(auxiliary, SdfPath("/Display"));
        const auto helper = UsdGeomCube::Define(auxiliary, SdfPath("/Display/Helper"));
        UsdGeomXformable(helper).AddTranslateOp().Set(GfVec3d(3, 0, 0));
        viewport.setAuxiliaryStage(auxiliary);
        const QImage shadedWithHelper = draw(UsdImagingGLDrawMode::DRAW_SHADED_SMOOTH);
        const int withHelper = warmupCount();
        require(withHelper > 3, "support geometry prepares its display modes before switching");
        draw(UsdImagingGLDrawMode::DRAW_WIREFRAME_ON_SURFACE);
        require(warmupCount() == withHelper, "support geometry also reuses warmed display modes");
        require(draw(UsdImagingGLDrawMode::DRAW_SHADED_SMOOTH) == shadedWithHelper,
                "hidden support geometry preparation preserves the presented image");
        viewport.setMask({ SdfPath("/Cube") });
        const QImage isolatedShaded = draw(UsdImagingGLDrawMode::DRAW_SHADED_SMOOTH);
        require(draw(UsdImagingGLDrawMode::DRAW_WIREFRAME_ON_SURFACE) != isolatedShaded,
                "isolated geometry supports the prepared wireframe representation");
        viewport.setMask({ SdfPath("/Other") });
        draw(UsdImagingGLDrawMode::DRAW_SHADED_SMOOTH);
        require(warmupCount() == withHelper, "mask changes reuse full-scene GPU preparation");
        viewport.setMask({});
        require(draw(UsdImagingGLDrawMode::DRAW_SHADED_SMOOTH) == shadedWithHelper,
                "clearing isolation restores the cached full-scene image");
        require(warmupCount() == withHelper, "clearing isolation retains full-scene preparation");
        viewport.setMask({ SdfPath("/Cube") });
        draw(UsdImagingGLDrawMode::DRAW_SHADED_SMOOTH);
        UsdGeomCube::Define(stage, SdfPath("/AddedWhileIsolated"));
        viewport.setMask({ SdfPath("/AddedWhileIsolated") });
        draw(UsdImagingGLDrawMode::DRAW_SHADED_SMOOTH);
        require(warmupCount() == withHelper + 1, "new source geometry invalidates shared preparation");
        draw(UsdImagingGLDrawMode::DRAW_WIREFRAME_ON_SURFACE);
        require(warmupCount() == withHelper + 1, "new isolated geometry prepares both representations once");
        viewport.setMask({});
        draw(UsdImagingGLDrawMode::DRAW_SHADED_SMOOTH);
        require(warmupCount() == withHelper + 2, "restoring an edited full scene refreshes its preparation");
        viewport.reset();
    }
    context.doneCurrent();
}

void
renderCoincidentSelectionApi()
{
    const auto traceCounts = []() {
        QFile trace(qEnvironmentVariable("STAGEVIZ_PERF_LOG"));
        require(trace.open(QIODevice::ReadOnly), "selection initialization trace exists");
        std::pair<int, int> counts { 0, 0 };
        for (const QByteArray& line : trace.readAll().split('\n')) {
            if (line.isEmpty())
                continue;
            const QString phase = QJsonDocument::fromJson(line).object().value("phase").toString();
            counts.first += phase.startsWith("selection.") && phase.endsWith(".create");
            counts.second += phase == "selection.warmup";
        }
        return counts;
    };
    QOpenGLContext context;
    require(context.create(), "create coincident selection context");
    QOffscreenSurface surface;
    surface.setFormat(context.format());
    surface.create();
    require(surface.isValid() && context.makeCurrent(&surface), "activate coincident selection context");
    {
        QOpenGLFramebufferObjectFormat format;
        format.setAttachment(QOpenGLFramebufferObject::CombinedDepthStencil);
        QOpenGLFramebufferObject framebuffer(128, 128, format);
        const auto asset = UsdStage::CreateInMemory();
        UsdGeomXform::Define(asset, SdfPath("/Shape"));
        UsdGeomCube::Define(asset, SdfPath("/Shape/Body"));
        for (bool instanced : { false, true }) {
            const auto stage = UsdStage::CreateInMemory();
            for (const char* name : { "/A", "/B" }) {
                const auto prim = UsdGeomXform::Define(stage, SdfPath(name)).GetPrim();
                prim.GetReferences().AddReference(asset->GetRootLayer()->GetIdentifier(), SdfPath("/Shape"));
                prim.SetInstanceable(instanced);
            }
            stageviz::RenderEngine viewport(stageviz::RenderEngine::ContextMode::Current);
            viewport.setStage(stage);
            GfCamera camera;
            GfMatrix4d view(1.);
            view.SetLookAt(GfVec3d(0, 0, 8), GfVec3d(0), GfVec3d(0, 1, 0));
            camera.SetTransform(view.GetInverse());
            camera.SetPerspectiveFromAspectRatioAndFieldOfView(1., 45., GfCamera::FOVVertical);
            camera.SetClippingRange(GfRange1f(.1f, 100.f));
            viewport.setCamera(camera);
            viewport.setSize(GfVec2i(128));
            viewport.setViewport(GfVec4d(0, 0, 128, 128));
            viewport.setSelectionColor(QColor(255, 0, 255));
            viewport.setSecondarySelectionColor(QColor(255, 0, 255));
            const auto draw = [&](const QList<SdfPath>& paths) {
                viewport.setSelected(paths);
                require(framebuffer.bind(), "bind coincident selection framebuffer");
                TfErrorMark mark;
                for (int i = 0; i < 3; ++i)
                    require(viewport.renderToCurrentFramebuffer(), "render coincident selection");
                for (const auto& error : mark)
                    std::cerr << "RENDER ERROR " << error.GetCommentary() << '\n';
                require(mark.IsClean(), "coincident selection renders without USD errors");
                QImage image = framebuffer.toImage();
                framebuffer.release();
                int count = 0;
                for (int y = 0; y < image.height(); ++y) {
                    for (int x = 0; x < image.width(); ++x) {
                        const QColor color = image.pixelColor(x, y);
                        count += color.red() > 220 && color.green() < 70 && color.blue() > 220;
                    }
                }
                return count;
            };
            require(draw({}) == 0, "unselected warmup produces no outline");
            const auto initialized = traceCounts();
            require(initialized.first > 0 && initialized.second > 0, "selection resources warm before any selection");
            require(draw({}) == 0 && traceCounts() == initialized, "idle redraw does not repeat selection warmup");
            require(draw({ SdfPath("/A/Body") }) > 20, "first coincident mesh or instance proxy stays outlined");
            require(traceCounts() == initialized, "first selection reuses initialized resources and pipelines");
            require(draw({ SdfPath("/B/Body") }) > 20, "second coincident mesh or instance proxy stays outlined");
            require(traceCounts() == initialized, "next selection reuses initialized resources and pipelines");
            require(draw({ SdfPath("/A"), SdfPath("/B") }) > 20, "coincident parent selection stays outlined");
            UsdGeomXformable(stage->GetPrimAtPath(SdfPath("/A"))).AddTranslateOp().Set(GfVec3d(0, 0, -3));
            require(draw({ SdfPath("/A/Body") }) == 0, "selected geometry behind an occluder stays hidden");
            UsdGeomImageable(stage->GetPrimAtPath(SdfPath("/B"))).MakeInvisible();
            require(draw({ SdfPath("/A/Body") }) > 20, "removing an occluder restores the outline");
            require(draw({ SdfPath("/B/Body") }) == 0, "invisible selected geometry stays hidden");
            viewport.setSize(GfVec2i(96));
            viewport.setViewport(GfVec4d(0, 0, 96, 96));
            require(draw({ SdfPath("/A") }) > 20, "resizing reallocates selection buffers");
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
    require(line->cursor().shape() == Qt::SizeHorCursor, "scrub mode advertises horizontal dragging");
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
    require(line->cursor().shape() == Qt::IBeamCursor, "direct entry uses the text cursor");
    QKeyEvent enter(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
    QApplication::sendEvent(line, &enter);
    require(line->isReadOnly(), "Enter returns numeric editor to scrub mode");
    require(line->cursor().shape() == Qt::SizeHorCursor, "committing restores the scrub cursor");

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

    class ArrowSpinBox : public stageviz::SpinBox {
    public:
        using QDoubleSpinBox::initStyleOption;
    } arrows;
    arrows.setRange(-100000., 100000.);
    arrows.resize(180, 40);
    arrows.show();
    QCoreApplication::processEvents();
    QStyleOptionSpinBox option;
    arrows.initStyleOption(&option);
    const int repeatDelay = arrows.style()->styleHint(QStyle::SH_SpinBox_ClickAutoRepeatThreshold, &option, &arrows)
                            + 3 * arrows.style()->styleHint(QStyle::SH_SpinBox_ClickAutoRepeatRate, &option, &arrows)
                            + 100;
    for (const auto control : { QStyle::SC_SpinBoxUp, QStyle::SC_SpinBoxDown }) {
        const QPoint start = arrows.style()->subControlRect(QStyle::CC_SpinBox, &option, control, &arrows).center();
        for (const QPoint end : { start, start - QPoint(0, QApplication::startDragDistance() + 30) }) {
            arrows.setValue(0.);
            QMouseEvent press(QEvent::MouseButtonPress, start, arrows.mapToGlobal(start), Qt::LeftButton,
                              Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(&arrows, &press);
            require(arrows.value() == (control == QStyle::SC_SpinBoxUp ? arrows.singleStep() : -arrows.singleStep()),
                    "arrow press retains normal step adjustment");
            QMouseEvent move(QEvent::MouseMove, end, arrows.mapToGlobal(end), Qt::NoButton, Qt::LeftButton,
                             Qt::NoModifier);
            QApplication::sendEvent(&arrows, &move);
            const bool dragged = end != start;
            require(arrows.isScrubbing() == dragged, "dragging from either arrow starts value scrubbing");
            if (dragged) {
                const auto checkValue = [&](const QPoint& position) {
                    const QPoint delta = position - start;
                    const double expected = (delta.x() - delta.y()) * arrows.singleStep() * arrows.scrubSensitivity();
                    require(qAbs(arrows.value() - expected) < 1e-8, "arrow scrub follows pointer displacement");
                };
                checkValue(end);
                const double heldValue = arrows.value();
                QEventLoop held;
                QTimer::singleShot(repeatDelay, &held, &QEventLoop::quit);
                held.exec();
                require(arrows.value() == heldValue, "native repeat stops as soon as scrubbing takes over");
                for (const QPoint position : { end + QPoint(12, 0), end - QPoint(8, 0), end }) {
                    QMouseEvent drag(QEvent::MouseMove, position, arrows.mapToGlobal(position), Qt::NoButton,
                                     Qt::LeftButton, Qt::NoModifier);
                    QApplication::sendEvent(&arrows, &drag);
                    checkValue(position);
                }
            }
            QMouseEvent release(QEvent::MouseButtonRelease, end, arrows.mapToGlobal(end), Qt::LeftButton, Qt::NoButton,
                                Qt::NoModifier);
            QApplication::sendEvent(&arrows, &release);
            const double releasedValue = arrows.value();
            QEventLoop idle;
            QTimer::singleShot(repeatDelay, &idle, &QEventLoop::quit);
            idle.exec();
            require(arrows.value() == releasedValue, "arrow stepping stops after release, including a drag outside");
            require(!arrows.isScrubbing(), "release ends arrow scrubbing");
        }
    }
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
        QElapsedTimer elapsed;
        elapsed.start();
        int settled = 0;
        while (settled < 6 && elapsed.elapsed() < 5000) {
            QCoreApplication::sendPostedEvents(nullptr, 0);
            QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
            settled = QThreadPool::globalInstance()->waitForDone(10) ? settled + 1 : 0;
        }
        require(settled == 6, "asset editor workers settle while Qt events remain responsive");
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
        QElapsedTimer elapsed;
        elapsed.start();
        bool timedOut = false;
        QObject::connect(&watchdog, &QTimer::timeout, &tree, [&]() {
            if (elapsed.elapsed() < 5000)
                return;
            timedOut = true;
            for (auto* widget : QApplication::topLevelWidgets()) {
                if (auto* dialog = qobject_cast<QFileDialog*>(widget); dialog && dialog->isVisible())
                    dialog->reject();
            }
        });
        watchdog.start(100);
        QTimer chooserDriver;
        QObject::connect(&chooserDriver, &QTimer::timeout, &tree, [&]() {
            for (auto* widget : QApplication::topLevelWidgets()) {
                auto* dialog = qobject_cast<QFileDialog*>(widget);
                if (!dialog || !dialog->isVisible())
                    continue;
                dialogSeen = true;
                if (cancel || timedOut) {
                    dialog->reject();
                    continue;
                }
                // Construction/model loading may process events before the
                // chooser becomes visible. Keep polling rather than losing a
                // single timer callback during that work.
                auto* filename = dialog->findChild<QLineEdit*>("fileNameEdit");
                if (!filename)
                    continue;
                dialog->setDirectory(directory.path());
                filename->setText(chosen);
                QMetaObject::invokeMethod(dialog, "accept", Qt::QueuedConnection);
            }
        });
        chooserDriver.start(25);
        fileItem = find(asset.GetPath());
        require(fileItem, "reacquire the asset row after previous edits");
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
        chooserDriver.stop();
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
materialSwatchColorViewApi()
{
    auto* state = stageviz::session()->viewState();
    const QString originalView = state->colorView();
    const float originalExposure = state->exposure(), originalGamma = state->gamma();
    state->setColorView(QStringLiteral("Raw"));
    const auto stage = UsdStage::CreateInMemory();
    const SdfPath path("/Materials/HDR");
    const auto material = UsdShadeMaterial::Define(stage, path);
    auto shader = UsdShadeShader::Define(stage, path.AppendChild(TfToken("Surface")));
    shader.CreateIdAttr(VtValue(TfToken("UsdPreviewSurface")));
    shader.CreateInput(TfToken("diffuseColor"), SdfValueTypeNames->Color3f).Set(GfVec3f(0));
    shader.CreateInput(TfToken("emissiveColor"), SdfValueTypeNames->Color3f).Set(GfVec3f(4, .18f, .02f));
    material.CreateSurfaceOutput().ConnectToSource(shader.CreateOutput(TfToken("surface"), SdfValueTypeNames->Token));
    const auto cube = UsdGeomCube::Define(stage, SdfPath("/Cube"));
    UsdShadeMaterialBindingAPI::Apply(cube.GetPrim()).Bind(material);
    {
        stageviz::RenderEngine engine(stageviz::RenderEngine::ContextMode::Offscreen);
        engine.setStage(stage);
        engine.setSize(GfVec2i(64));
        GfCamera camera;
        GfMatrix4d view(1.);
        view.SetLookAt(GfVec3d(0, 0, 8), GfVec3d(0), GfVec3d(0, 1, 0));
        camera.SetTransform(view.GetInverse());
        camera.SetPerspectiveFromAspectRatioAndFieldOfView(1., 45., GfCamera::FOVVertical);
        engine.setCamera(camera);
        const QImage linear = engine.renderLinearImage();
        require(!linear.isNull() && linear.format() == QImage::Format_RGBA32FPx4_Premultiplied,
                "linear swatch capture retains floating point RGBA");
        const auto* center = reinterpret_cast<const float*>(linear.constScanLine(32)) + 32 * 4;
        require(center[0] > 1.0f && center[3] > .99f, "linear capture preserves HDR values above one");
        const QImage converted = stageviz::colorManagementImage(linear, QStringLiteral("Un-tone-mapped"));
        const QImage gpu = engine.renderImage();
        const QColor a = converted.pixelColor(32, 32), b = gpu.pixelColor(32, 32);
        require(std::abs(a.red() - b.red()) <= 2 && std::abs(a.green() - b.green()) <= 2
                    && std::abs(a.blue() - b.blue()) <= 2,
                "cached linear CPU conversion agrees with direct GPU display output");
    }
    const auto renderCount = []() {
        QFile trace(qEnvironmentVariable("STAGEVIZ_PERF_LOG"));
        require(trace.open(QIODevice::ReadOnly), "material render trace exists");
        int count = 0;
        for (const auto& line : trace.readAll().split('\n'))
            count += QJsonDocument::fromJson(line).object().value("phase").toString()
                     == "material.swatch.linear_render";
        return count;
    };
    stageviz::MaterialRenderer renderer;
    QImage image;
    QString error;
    int viewChanges = 0;
    QObject::connect(&renderer, &stageviz::MaterialRenderer::rendered, &renderer,
                     [&](const QString&, const QImage& value) { image = value; });
    QObject::connect(&renderer, &stageviz::MaterialRenderer::error, &renderer,
                     [&](const QString&, const QString& value) { error = value; });
    QObject::connect(&renderer, &stageviz::MaterialRenderer::colorViewChanged, &renderer, [&]() { ++viewChanges; });
    const auto request = [&]() {
        image = {};
        renderer.request(path, stage->GetRootLayer());
        QElapsedTimer timer;
        timer.start();
        while (image.isNull() && error.isEmpty() && timer.elapsed() < 60000) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
            QThread::msleep(1);
        }
        require(error.isEmpty() && !image.isNull(), "render color-managed material swatch");
    };
    request();
    const QImage raw = image;
    const int renders = renderCount();
    require(renders == 1, "one geometry render populates the swatch cache");
    for (int sweep = 0; sweep < 2; ++sweep) {
        for (const QString& colorView : state->colorViews()) {
            state->setColorView(colorView);
            request();
            require(renderCount() == renders, "view changes reuse the cached linear shaderball");
            require(image.pixelColor(0, 0).alpha() == 0, "view changes preserve transparent swatch background");
            if (colorView == "Raw")
                require(image == raw, "returning to Raw restores identical pixels without cumulative transforms");
            if (colorView == "ACES 1.0 - SDR Video")
                require(image != raw, "tone mapping updates the displayed swatch");
        }
    }
    const int changes = viewChanges;
    const QImage neutral = image;
    state->setExposure(3);
    state->setGamma(2);
    request();
    require(viewChanges == changes && image == neutral && renderCount() == renders,
            "viewport exposure and gamma do not affect material swatches");
    renderer.invalidate(path);
    renderer.syncAttribute(shader.GetInput(TfToken("emissiveColor")).GetAttr().GetPath(),
                           VtValue(GfVec3f(.02f, 4, .18f)));
    request();
    require(renderCount() == renders + 1 && image != neutral,
            "material edits invalidate the linear cache and rerender once");
    state->setColorView(QStringLiteral("ACES 1.0 - SDR Video"));
    request();
    require(renderCount() == renders + 1, "view conversion reuses the newly authored material image");
    state->setColorView(QStringLiteral("Raw"));
    renderer.invalidateNetwork(path, true);
    request();
    require(image == raw && renderCount() == renders + 1,
            "returning to an identical authored composition reuses its linear image");
    shader.GetInput(TfToken("emissiveColor")).Set(GfVec3f(.02f, .18f, 4));
    renderer.invalidateNetwork(path, true);
    request();
    require(image != raw && renderCount() == renders + 2, "new composition content renders once");
    shader.GetInput(TfToken("emissiveColor")).Set(GfVec3f(4, .18f, .02f));
    renderer.invalidateNetwork(path, true);
    request();
    require(image == raw && renderCount() == renders + 2,
            "switching back uses content rather than source-layer identity");
    renderer.invalidateNetwork(path);
    request();
    require(renderCount() == renders + 3, "explicit network invalidation discards cached compositions");
    state->setColorView(originalView);
    state->setExposure(originalExposure);
    state->setGamma(originalGamma);
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

    checkRender([&]() { renderer.requestPlaceholder(SdfPath("/Materials/Unknown")); });
    const QColor placeholderCenter = image.pixelColor(image.width() / 2, image.height() / 2);
    const QColor placeholderNearby = image.pixelColor(image.width() / 2 + 12, image.height() / 2 + 7);
    require(std::abs(placeholderCenter.red() - placeholderCenter.green()) <= 2
                && std::abs(placeholderCenter.green() - placeholderCenter.blue()) <= 2,
            "unsupported materials use a neutral gray shaderball placeholder");
    require(placeholderCenter == placeholderNearby, "unsupported-material placeholder has no lighting gradient");
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
materialBrowserDisplayMenuApi()
{
    stageviz::MaterialBrowser browser;

    stageviz::MaterialEntry preview;
    preview.materialPath = SdfPath("/Materials/Preview");
    preview.shaderPath = SdfPath("/Materials/Preview/Surface");
    preview.name = QStringLiteral("Preview");
    preview.shaderId = QStringLiteral("UsdPreviewSurface");
    preview.layerIdentifiers = { QStringLiteral("layer-a.usda") };

    stageviz::MaterialEntry standard;
    standard.materialPath = SdfPath("/Materials/Standard");
    standard.shaderPath = SdfPath("/Materials/Standard/Surface");
    standard.name = QStringLiteral("Standard");
    standard.shaderId = QStringLiteral("ND_standard_surface_surfaceshader");
    standard.layerIdentifiers = { QStringLiteral("layer-a.usda") };

    stageviz::MaterialEntry openPbr;
    openPbr.materialPath = SdfPath("/Materials/OpenPBR");
    openPbr.shaderPath = SdfPath("/Materials/OpenPBR/Surface");
    openPbr.name = QStringLiteral("OpenPBR");
    openPbr.shaderId = QStringLiteral("ND_open_pbr_surface_surfaceshader");
    openPbr.layerIdentifiers = { QStringLiteral("layer-b.usda") };

    stageviz::MaterialEntry vray;
    vray.materialPath = SdfPath("/Materials/VRay");
    vray.shaderPath = SdfPath("/Materials/VRay/Surface");
    vray.name = QStringLiteral("VRay");
    vray.shaderId = QStringLiteral("vray:MtlSingleBRDF");
    vray.layerIdentifiers = { QStringLiteral("layer-b.usda") };

    browser.setEntries({ preview, standard, openPbr, vray });
    browser.show();
    QCoreApplication::processEvents(QEventLoop::AllEvents, 25);

    auto* display = browser.findChild<QToolButton*>("display");
    auto* icons = browser.findChild<QListWidget*>("icons");
    auto* list = browser.findChild<QListWidget*>("list");
    auto* details = browser.findChild<QTreeWidget*>("details");
    require(display && icons, "MaterialBrowser exposes Display button and icon list");

    const auto openMenu = [&](const std::function<void(const QList<QAction*>&)>& inspect) {
        bool menuFound = false;
        bool filtered = false;
        QTimer inspection;
        inspection.setSingleShot(true);
        QObject::connect(&inspection, &QTimer::timeout, &browser, [&]() {
            auto* menu = qobject_cast<QMenu*>(QApplication::activePopupWidget());
            if (!menu)
                return;
            menuFound = true;
            require(display->isChecked(), "material Display button stays highlighted while its menu is open");
            QList<QAction*> actions;
            const auto collect = [&](auto&& self, QMenu* parent) -> void {
                for (auto* action : parent->actions()) {
                    actions.append(action);
                    if (action->menu())
                        self(self, action->menu());
                }
            };
            collect(collect, menu);
            inspect(actions);
            for (auto* action : actions)
                filtered |= action->isCheckable() && !action->isChecked();
            menu->close();
        });
        inspection.start(0);
        display->click();
        inspection.stop();
        require(menuFound, "material Display popup remains open for filter inspection");
        require(display->isChecked() == filtered, "material Display highlight follows active filters after closing");
        return menuFound;
    };

    bool layerAndTypeEntriesFound = false;
    const bool firstMenuFound = openMenu([&](const QList<QAction*>& actions) {
        require(std::any_of(actions.begin(), actions.end(),
                            [](auto* action) { return action->menu() && action->text() == "Referenced assets"; }),
                "external layers appear in Referenced assets submenu");
        require(std::none_of(actions.begin(), actions.end(),
                             [](auto* action) { return action->menu() && action->text() == "Packages"; }),
                "empty Packages group is omitted");
        bool layerFound = false;
        bool previewFound = false;
        bool standardFound = false;
        bool openPbrFound = false;
        bool customFoundAfterDivider = false;
        bool standardTypesBeforeDivider = false;
        bool materialTypeDividerFound = false;
        bool inMaterialTypes = false;
        bool inCustomTypes = false;
        for (QAction* action : actions) {
            if (action->text() == "layer-a.usda" && action->isCheckable())
                layerFound = true;
            if (action->text() == "Material types") {
                inMaterialTypes = true;
                continue;
            }
            if (!inMaterialTypes)
                continue;
            if (action->isSeparator()) {
                materialTypeDividerFound = previewFound && standardFound && openPbrFound;
                inCustomTypes = true;
                continue;
            }
            if (action->text() == "USD Preview Surface" && action->isCheckable()) {
                previewFound = true;
                standardTypesBeforeDivider = !inCustomTypes;
                action->trigger();
            }
            if (action->text() == "MaterialX Standard Surface" && action->isCheckable()) {
                standardFound = true;
                standardTypesBeforeDivider = standardTypesBeforeDivider && !inCustomTypes;
            }
            if (action->text() == "MaterialX OpenPBR Surface" && action->isCheckable()) {
                openPbrFound = true;
                standardTypesBeforeDivider = standardTypesBeforeDivider && !inCustomTypes;
            }
            if (action->text() == "vray:MtlSingleBRDF" && action->isCheckable())
                customFoundAfterDivider = inCustomTypes;
        }
        layerAndTypeEntriesFound = layerFound && previewFound && standardFound && openPbrFound
                                   && materialTypeDividerFound && standardTypesBeforeDivider && customFoundAfterDivider;
    });
    require(firstMenuFound && layerAndTypeEntriesFound,
            "Display menu groups standard material types above nonstandard shader types");
    require(icons->item(0)->isHidden() && !icons->item(1)->isHidden() && !icons->item(2)->isHidden()
                && !icons->item(3)->isHidden(),
            "material type filter hides only the selected shader type");

    const bool secondMenuFound = openMenu([&](const QList<QAction*>& actions) {
        for (QAction* action : actions) {
            if (action->text() == "layer-a.usda" && action->isCheckable())
                action->trigger();
        }
    });
    require(secondMenuFound, "Display menu reopens with the active material filters");
    require(icons->item(0)->isHidden() && icons->item(1)->isHidden() && !icons->item(2)->isHidden()
                && !icons->item(3)->isHidden(),
            "material layer filter hides only materials from the selected layer");

    const QSet<QString> typeIds { preview.shaderId, standard.shaderId, openPbr.shaderId, vray.shaderId };
    const QSet<QString> layerIds { QStringLiteral("layer-a.usda"), QStringLiteral("layer-b.usda") };
    const auto setGroup = [&](const QSet<QString>& keys, bool checked) {
        require(openMenu([&](const QList<QAction*>& actions) {
                    for (QAction* action : actions) {
                        if (action->isCheckable() && keys.contains(action->data().toString())
                            && action->isChecked() != checked)
                            action->trigger();
                    }
                }),
                "open material Display menu to change a filter group");
    };
    const auto checkGroup = [&](const QSet<QString>& keys, bool checked) {
        require(openMenu([&](const QList<QAction*>& actions) {
                    int found = 0;
                    for (QAction* action : actions) {
                        if (action->isCheckable() && keys.contains(action->data().toString())) {
                            ++found;
                            require(action->isChecked() == checked, "Display menu preserves all/none checkbox state");
                        }
                    }
                    require(found == keys.size(), "Display menu retains every available filter option");
                }),
                "reopen material Display menu to verify filter state");
    };
    const auto checkRows = [&](const QSet<int>& visible) {
        require(list && details, "material browser exposes every presentation mode");
        for (int row = 0; row < icons->count(); ++row) {
            const bool hidden = !visible.contains(row);
            require(icons->item(row)->isHidden() == hidden && list->item(row)->isHidden() == hidden
                        && details->topLevelItem(row)->isHidden() == hidden,
                    "material filters agree in icon, list and detail views");
        }
    };

    setGroup(layerIds, true);
    setGroup(typeIds, false);
    checkRows({});
    checkGroup(typeIds, false);
    browser.setEntries({ preview, standard, openPbr, vray });
    checkRows({});
    checkGroup(typeIds, false);
    require(openMenu([&](const QList<QAction*>& actions) {
                for (QAction* action : actions)
                    if (action->isCheckable() && action->data().toString() == standard.shaderId)
                        action->trigger();
            }),
            "select one material type from an empty filter");
    checkRows({ 1 });
    setGroup(typeIds, true);
    checkRows({ 0, 1, 2, 3 });
    checkGroup(typeIds, true);

    setGroup(layerIds, false);
    checkRows({});
    checkGroup(layerIds, false);
    require(openMenu([&](const QList<QAction*>& actions) {
                for (QAction* action : actions)
                    if (action->isCheckable() && action->data().toString() == "layer-a.usda")
                        action->trigger();
            }),
            "select one material layer from an empty filter");
    checkRows({ 0, 1 });
    setGroup(layerIds, true);
    checkRows({ 0, 1, 2, 3 });
    require(!display->isChecked(), "fully enabled material display has no filtering indicator");
    require(openMenu([&](const QList<QAction*>& actions) {
                for (auto* action : actions)
                    if (action->data().toString() == preview.shaderId)
                        action->trigger();
            }),
            "hide preview material type before creating another preview");
    auto created = preview;
    created.materialPath = SdfPath("/Materials/NewPreview");
    created.shaderPath = SdfPath("/Materials/NewPreview/Surface");
    created.name = "NewPreview";
    browser.setEntries({ preview, standard, openPbr, vray, created });
    require(display->isChecked() && icons->item(4)->isHidden(),
            "filter indicator warns that a newly added material is hidden by its type");
    bool contextFound = false;
    QTimer::singleShot(0, &browser, [&]() {
        auto* menu = qobject_cast<QMenu*>(QApplication::activePopupWidget());
        require(menu && display->isChecked(), "context-opened material Display menu stays highlighted");
        contextFound = true;
        for (auto* action : menu->actions())
            if (action->data().toString() == preview.shaderId && !action->isChecked())
                action->trigger();
        menu->close();
    });
    const QPoint point = display->rect().center();
    QContextMenuEvent context(QContextMenuEvent::Mouse, point, display->mapToGlobal(point));
    QApplication::sendEvent(display, &context);
    require(contextFound && !display->isChecked() && !icons->item(4)->isHidden(),
            "clearing filtering through the context menu clears the indicator and reveals the new material");
    auto packaged = preview;
    packaged.layerIdentifiers = { QStringLiteral("/assets/car.usdz[0/paint.usdc]") };
    browser.setEntries({ packaged });
    require(openMenu([&](const QList<QAction*>& actions) {
                int matches = 0;
                for (auto* action : actions) {
                    require(action->text() != "Referenced assets", "empty Referenced assets group is omitted");
                    if (action->data().toString() == packaged.layerIdentifiers.front()) {
                        ++matches;
                        require(action->text() == "paint.usdc"
                                    && action->toolTip() == packaged.layerIdentifiers.front(),
                                "package entries show internal filename and full identifier tooltip");
                        action->trigger();
                    }
                }
                require(matches == 1, "packaged source appears exactly once");
            }),
            "open grouped package filters");
    require(icons->item(0)->isHidden() && display->isChecked(), "nested package filter updates rows and indicator");
}

void
materialUnknownSurfaceContextApi()
{
    const UsdStageRefPtr stage = UsdStage::CreateInMemory();
    const SdfPath materialPath("/Materials/VRay");
    UsdShadeMaterial material = UsdShadeMaterial::Define(stage, materialPath);
    UsdShadeShader materialId = UsdShadeShader::Define(stage, materialPath.AppendChild(TfToken("MaterialID")));
    materialId.CreateIdAttr(VtValue(TfToken("vray:MtlMaterialID")));
    UsdShadeShader singleBrdf = UsdShadeShader::Define(stage, materialPath.AppendChild(TfToken("SingleBRDF")));
    singleBrdf.CreateIdAttr(VtValue(TfToken("vray:MtlSingleBRDF")));
    UsdShadeShader brdf = UsdShadeShader::Define(stage, materialPath.AppendChild(TfToken("BRDF")));
    brdf.CreateIdAttr(VtValue(TfToken("vray:BRDFVRayMtl")));

    materialId.CreateInput(TfToken("base_mtl"), SdfValueTypeNames->Token)
        .ConnectToSource(singleBrdf.CreateOutput(TfToken("out"), SdfValueTypeNames->Token));
    singleBrdf.CreateInput(TfToken("brdf"), SdfValueTypeNames->Token)
        .ConnectToSource(brdf.CreateOutput(TfToken("out"), SdfValueTypeNames->Token));
    material.CreateSurfaceOutput(TfToken("vray"))
        .ConnectToSource(materialId.CreateOutput(TfToken("surface"), SdfValueTypeNames->Token));

    const QList<stageviz::MaterialEntry> entries = stageviz::MaterialUtils::sceneMaterials(stage);
    require(entries.size() == 1, "material discovery includes custom renderer surface contexts");
    require(entries.first().materialPath == materialPath && entries.first().shaderPath == materialId.GetPath()
                && entries.first().shaderId == QStringLiteral("vray:MtlMaterialID"),
            "custom renderer material keeps its connected root shader for graph display");

    QString error;
    const SdfLayerRefPtr network = stageviz::MaterialUtils::materialNetworkLayer(stage, materialPath, error);
    require(network && error.isEmpty(), "custom renderer material network snapshot is available");
    if (!network)
        return;
    std::string serialized;
    require(network->ExportToString(&serialized), "custom renderer network snapshot can be inspected");
    require(serialized.find("vray:MtlMaterialID") != std::string::npos
                && serialized.find("vray:MtlSingleBRDF") != std::string::npos
                && serialized.find("vray:BRDFVRayMtl") != std::string::npos,
            "material graph snapshot retains every custom shader node");
}

void
materialSelectMultiSelectionApi()
{
    stageviz::Session* current = stageviz::session();
    require(current->newStage(), "new multi-material selection fixture");
    const UsdStageRefPtr stage = current->stage();

    QList<SdfPath> expectedPaths;
    QList<int> materialRows;
    constexpr int materialCount = 10;
    for (int index = 0; index < materialCount; ++index) {
        const QString suffix = QString::number(index);
        const SdfPath materialPath((QStringLiteral("/Materials/Select") + suffix).toStdString());
        const SdfPath geometryPath((QStringLiteral("/World/Geometry") + suffix).toStdString());
        const UsdShadeMaterial material = UsdShadeMaterial::Define(stage, materialPath);
        UsdShadeShader surface = UsdShadeShader::Define(stage, materialPath.AppendChild(TfToken("Surface")));
        surface.CreateIdAttr(VtValue(TfToken("UsdPreviewSurface")));
        material.CreateSurfaceOutput().ConnectToSource(
            surface.CreateOutput(TfToken("surface"), SdfValueTypeNames->Token));

        const UsdGeomCube geometry = UsdGeomCube::Define(stage, geometryPath);
        require(UsdShadeMaterialBindingAPI::Apply(geometry.GetPrim()).Bind(material),
                "bind fixture geometry to its material");
        expectedPaths.append(geometryPath);
        materialRows.append(index);
    }

    stageviz::MaterialDialog dialog;
    dialog.show();
    auto* browser = dialog.findChild<stageviz::MaterialBrowser*>("browserWidget");
    require(browser && browser->entries().size() == materialCount, "Material Editor lists all fixture materials");
    if (!browser || browser->entries().size() != materialCount)
        return;

    browser->selectRows(materialRows);
    require(browser->selectedEntries().size() == materialCount,
            "browser retains every selected material before context menu");
    auto* icons = browser->findChild<QListWidget*>("icons");
    require(icons && icons->item(0), "Material Editor exposes the selected material cards");
    if (!icons || !icons->item(0))
        return;

    const QPoint position = icons->visualItemRect(icons->item(0)).center();
    const QPoint globalPosition = icons->viewport()->mapToGlobal(position);
    bool selectActionFound = false;
    QTimer::singleShot(0, &dialog, [&]() {
        auto* menu = qobject_cast<QMenu*>(QApplication::activePopupWidget());
        if (!menu)
            return;
        for (QAction* action : menu->actions()) {
            if (action->text() == "Select") {
                selectActionFound = true;
                const QPoint menuPosition = menu->actionGeometry(action).center();
                const QPoint menuGlobalPosition = menu->mapToGlobal(menuPosition);
                QMouseEvent press(QEvent::MouseButtonPress, QPointF(menuPosition), QPointF(menuGlobalPosition),
                                  Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
                QApplication::sendEvent(menu, &press);
                QMouseEvent release(QEvent::MouseButtonRelease, QPointF(menuPosition), QPointF(menuGlobalPosition),
                                    Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
                QApplication::sendEvent(menu, &release);
                return;
            }
        }
    });

    QMouseEvent press(QEvent::MouseButtonPress, QPointF(position), QPointF(globalPosition), Qt::RightButton,
                      Qt::RightButton, Qt::NoModifier);
    QApplication::sendEvent(icons->viewport(), &press);
    QMouseEvent release(QEvent::MouseButtonRelease, QPointF(position), QPointF(globalPosition), Qt::RightButton,
                        Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(icons->viewport(), &release);

    require(selectActionFound, "material context menu Select action is available");
    const QList<SdfPath> selectedPaths = current->selectionList()->paths();
    std::set<SdfPath> selectedSet;
    for (const SdfPath& path : selectedPaths)
        selectedSet.insert(path);
    require(selectedPaths.size() == materialCount, "Select gathers one prim for every selected material");
    for (const SdfPath& path : std::as_const(expectedPaths))
        require(selectedSet.find(path) != selectedSet.end(), "Select includes every prim bound to a selected material");
}

void
materialFollowSelectionApi()
{
    auto* current = stageviz::session();
    require(current->newStage(), "new material follow fixture");
    const auto stage = current->stage();
    for (int i = 0; i < 30; ++i) {
        const auto path = SdfPath((QStringLiteral("/Looks/Material%1").arg(i, 2, 10, QChar('0'))).toStdString());
        const auto material = UsdShadeMaterial::Define(stage, path);
        auto shader = UsdShadeShader::Define(stage, path.AppendChild(TfToken("Surface")));
        shader.CreateIdAttr(VtValue(TfToken("UsdPreviewSurface")));
        material.CreateSurfaceOutput().ConnectToSource(
            shader.CreateOutput(TfToken("surface"), SdfValueTypeNames->Token));
    }
    stageviz::MaterialDialog dialog;
    dialog.resize(600, 550);
    dialog.show();
    const auto drain = []() {
        for (int i = 0; i < 4; ++i) {
            QCoreApplication::sendPostedEvents(nullptr, 0);
            QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
        }
    };
    drain();
    auto* browser = dialog.findChild<stageviz::MaterialBrowser*>("browserWidget");
    auto* follow = browser->findChild<QToolButton*>("follow");
    auto* clear = browser->findChild<QToolButton*>("clear");
    auto* display = browser->findChild<QToolButton*>("display");
    require(follow && clear && display && !follow->isChecked() && !follow->icon().isNull(),
            "Follow starts off with native icon");
    require(clear->geometry().right() < follow->geometry().left()
                && follow->geometry().right() < display->geometry().left(),
            "Follow appears immediately between Clear and Display");
    browser->selectRow(0);
    const SdfPath last("/Looks/Material29"), first("/Looks/Material00");
    current->selectionList()->updatePaths({ last });
    drain();
    require(browser->selectedRows() == QList<int> { 0 }, "disabled follow preserves editor selection");
    follow->setChecked(true);
    drain();
    require(browser->selectedEntries().size() == 1 && browser->selectedEntries().first().materialPath == last,
            "enabling Follow immediately selects current stage material");
    const auto mesh = UsdGeomMesh::Define(stage, SdfPath("/World/Mesh"));
    mesh.CreatePointsAttr().Set(VtVec3fArray { { 0, 0, 0 }, { 1, 0, 0 }, { 0, 1, 0 } });
    mesh.CreateFaceVertexCountsAttr().Set(VtIntArray { 3 });
    mesh.CreateFaceVertexIndicesAttr().Set(VtIntArray { 0, 1, 2 });
    const auto subset = UsdGeomSubset::CreateGeomSubset(mesh, TfToken("Face"), UsdGeomTokens->face, VtIntArray { 0 },
                                                        TfToken("materialBind"));
    const auto material = UsdShadeMaterial(stage->GetPrimAtPath(first));
    UsdShadeMaterialBindingAPI::Apply(mesh.GetPrim()).Bind(material);
    UsdShadeMaterialBindingAPI::Apply(subset.GetPrim()).Bind(material);
    for (const auto& path : { SdfPath("/World"), mesh.GetPath(), subset.GetPath() }) {
        current->selectionList()->updatePaths({ path });
        drain();
        require(browser->selectedEntries().size() == 1 && browser->selectedEntries().first().materialPath == last,
                "following geometry, groups and subsets does not infer their bound materials");
        require(current->selectionList()->paths() == QList<SdfPath> { path },
                "following never adds materials to stage selection");
    }
    for (auto mode :
         { stageviz::MaterialBrowser::Icons, stageviz::MaterialBrowser::List, stageviz::MaterialBrowser::Details }) {
        browser->setViewMode(mode);
        current->selectionList()->updatePaths({ first });
        drain();
        current->selectionList()->updatePaths({ last });
        drain();
        const int row = browser->rowForMaterialPath(last);
        if (mode == stageviz::MaterialBrowser::Details) {
            auto* view = browser->findChild<QTreeWidget*>("details");
            require(view->viewport()->rect().intersects(view->visualItemRect(view->topLevelItem(row))),
                    "Follow scrolls detail view");
        }
        else {
            auto* view = browser->findChild<QListWidget*>(mode == stageviz::MaterialBrowser::Icons ? "icons" : "list");
            require(view->viewport()->rect().intersects(view->visualItemRect(view->item(row))),
                    "Follow scrolls icon and list views");
        }
    }
    dialog.hide();
    current->selectionList()->updatePaths({ first });
    dialog.show();
    drain();
    require(browser->selectedEntries().size() == 1 && browser->selectedEntries().first().materialPath == first,
            "opening editor follows the material selected while closed");
    follow->setChecked(false);
    current->selectionList()->updatePaths({ last });
    drain();
    require(browser->selectedEntries().first().materialPath == first, "turning Follow off stops synchronization");
}

void
materialGraphDisconnectedNetworkLayoutApi()
{
    stageviz::Session* current = stageviz::session();
    require(current->newStage(), "new disconnected material graph fixture");

    const UsdStageRefPtr stage = current->stage();
    UsdShadeMaterial material = UsdShadeMaterial::Define(stage, SdfPath("/Material"));

    UsdShadeShader primarySurface = UsdShadeShader::Define(stage, SdfPath("/Material/PrimarySurface"));
    primarySurface.CreateIdAttr(VtValue(TfToken("UsdPreviewSurface")));
    UsdShadeShader primaryInput = UsdShadeShader::Define(stage, SdfPath("/Material/PrimaryInput"));
    primaryInput.CreateIdAttr(VtValue(TfToken("UsdUVTexture")));
    primarySurface.CreateInput(TfToken("diffuseColor"), SdfValueTypeNames->Color3f)
        .ConnectToSource(primaryInput.CreateOutput(TfToken("rgb"), SdfValueTypeNames->Color3f));
    material.CreateSurfaceOutput().ConnectToSource(
        primarySurface.CreateOutput(TfToken("surface"), SdfValueTypeNames->Token));

    UsdShadeShader alternateSurface = UsdShadeShader::Define(stage, SdfPath("/Material/AlternateSurface"));
    alternateSurface.CreateIdAttr(VtValue(TfToken("vray:MtlMaterialID")));
    UsdShadeShader alternateInput = UsdShadeShader::Define(stage, SdfPath("/Material/AlternateInput"));
    alternateInput.CreateIdAttr(VtValue(TfToken("vray:MtlSingleBRDF")));
    alternateSurface.CreateInput(TfToken("base_mtl"), SdfValueTypeNames->Token)
        .ConnectToSource(alternateInput.CreateOutput(TfToken("out"), SdfValueTypeNames->Token));

    const QList<stageviz::MaterialEntry> materials = stageviz::MaterialUtils::sceneMaterials(stage);
    require(materials.size() == 1 && materials.first().shaderPath == primarySurface.GetPath(),
            "primary surface remains the selected material network");

    stageviz::MaterialGraph graph;
    graph.resize(900, 600);
    graph.setMaterial(materials.first());
    QCoreApplication::processEvents();
    const QGraphicsScene* scene = graph.graphicsView()->scene();
    require(scene, "material graph scene exists");

    QHash<QString, QPointF> nodePositions;
    for (QGraphicsItem* item : scene->items()) {
        auto* node = dynamic_cast<QGraphicsRectItem*>(item);
        if (!node || node->parentItem() || node->rect().width() < 200.0)
            continue;
        for (QGraphicsItem* child : node->childItems()) {
            auto* title = dynamic_cast<QGraphicsSimpleTextItem*>(child);
            if (!title)
                continue;
            nodePositions.insert(title->text(), node->pos());
            break;
        }
    }

    require(nodePositions.contains("PrimarySurface") && nodePositions.contains("PrimaryInput")
                && nodePositions.contains("AlternateSurface") && nodePositions.contains("AlternateInput"),
            "both primary and disconnected material nodes are present");
    require(nodePositions.value("PrimaryInput").x() == 0.0 && nodePositions.value("PrimarySurface").x() == 330.0,
            "primary network keeps its established left-to-right ordering");
    require(nodePositions.value("AlternateSurface").x() == nodePositions.value("PrimarySurface").x()
                && nodePositions.value("AlternateInput").x() < nodePositions.value("AlternateSurface").x(),
            "disconnected network aligns its root and retains connection-directed columns");
}

void
materialGraphSharedBranchLayoutApi()
{
    auto* current = stageviz::session();
    require(current->newStage(), "new shared-source material graph fixture");
    const auto stage = current->stage();
    const auto material = UsdShadeMaterial::Define(stage, SdfPath("/Material"));
    const auto shader = [&](const char* name, const char* id) {
        auto result = UsdShadeShader::Define(stage, SdfPath(std::string("/Material/") + name));
        result.CreateIdAttr(VtValue(TfToken(id)));
        result.CreateOutput(TfToken("out"), SdfValueTypeNames->Color3f);
        return result;
    };
    const auto connect = [](UsdShadeShader target, const char* input, const UsdShadeShader& source) {
        require(target.CreateInput(TfToken(input), SdfValueTypeNames->Color3f)
                    .ConnectToSource(source.GetOutput(TfToken("out"))),
                "connect graph layout fixture");
    };
    const auto primary = shader("Primary", "UsdPreviewSurface");
    material.CreateSurfaceOutput().ConnectToSource(primary.GetOutput(TfToken("out")));
    const auto shared = shader("SharedUV", "UsdPrimvarReader_float2");
    auto textureA = shader("TextureA", "UsdUVTexture");
    const auto textureB = shader("TextureB", "UsdUVTexture");
    const auto textureC = shader("TextureC", "UsdUVTexture");
    const auto extra = shader("Extra", "UsdUVTexture");
    connect(textureA, "st", shared);
    connect(textureB, "st", shared);
    connect(textureC, "st", shared);
    connect(extra, "rgb", textureB);
    connect(primary, "diffuseColor", textureA);
    connect(primary, "emissiveColor", extra);
    connect(primary, "normal", textureC);
    // A tall sibling exercises size-aware packing rather than equal row spacing.
    for (int i = 0; i < 20; ++i)
        textureA.CreateInput(TfToken("extra" + std::to_string(i)), SdfValueTypeNames->Float);
    shader("Standard", "ND_standard_surface_surfaceshader");
    shader("OpenPBR", "ND_open_pbr_surface_surfaceshader");
    const auto unknown = shader("AUnknownRoot", "vendor:Surface");
    const auto unknownInput = shader("AUnknownInput", "vendor:Texture");
    connect(unknown, "color", unknownInput);
    const auto cycleA = shader("CycleA", "vendor:Cycle");
    const auto cycleB = shader("CycleB", "vendor:Cycle");
    connect(cycleA, "color", cycleB);
    connect(cycleB, "color", cycleA);

    std::string before;
    stage->GetRootLayer()->ExportToString(&before);
    stageviz::MaterialGraph graph;
    graph.setMaterial(stageviz::MaterialUtils::sceneMaterials(stage).first());
    const auto nodes = [&]() {
        QHash<QString, QGraphicsRectItem*> result;
        for (QGraphicsItem* item : graph.graphicsView()->scene()->items()) {
            auto* node = dynamic_cast<QGraphicsRectItem*>(item);
            if (!node || node->parentItem() || node->rect().width() < 200.0)
                continue;
            for (QGraphicsItem* child : node->childItems()) {
                if (auto* title = dynamic_cast<QGraphicsSimpleTextItem*>(child)) {
                    result.insert(title->text(), node);
                    break;
                }
            }
        }
        return result;
    };
    auto items = nodes();
    require(items.size() == 12, "all supported, unknown and cyclic nodes are laid out");
    const qreal rootX = items.value("Primary")->pos().x();
    for (const char* root : { "Standard", "OpenPBR", "AUnknownRoot", "CycleA" })
        require(items.value(root)->pos().x() == rootX, "network roots share the same right-hand column");
    for (const auto& key : items.keys()) {
        const auto* node = items.value(key);
        require(std::isfinite(node->pos().x()) && std::isfinite(node->pos().y()),
                "cyclic networks receive finite positions");
        for (const auto& other : items.keys()) {
            if (key != other)
                require(!node->sceneBoundingRect().intersects(items.value(other)->sceneBoundingRect()),
                        "shared sources and variable node heights do not overlap node bodies");
        }
    }
    for (const char* texture : { "TextureA", "TextureB", "TextureC" })
        require(items.value("SharedUV")->pos().x() < items.value(texture)->pos().x(),
                "shared UV source is left of every consumer, including the longer branch");
    require(items.value("TextureA")->pos().y() != items.value("TextureC")->pos().y(),
            "branches sharing a source receive distinct vertical positions");
    qreal supportedBottom = 0.0;
    for (const char* name :
         { "Primary", "SharedUV", "TextureA", "TextureB", "TextureC", "Extra", "Standard", "OpenPBR" })
        supportedBottom = std::max(supportedBottom, items.value(name)->sceneBoundingRect().bottom());
    require(items.value("AUnknownRoot")->sceneBoundingRect().top() > supportedBottom
                && items.value("AUnknownInput")->sceneBoundingRect().top() > supportedBottom,
            "supported network blocks precede unknown networks regardless of path ordering");
    QHash<QString, QPointF> positions;
    for (auto it = items.cbegin(); it != items.cend(); ++it)
        positions.insert(it.key(), it.value()->pos());
    graph.rebuild();
    items = nodes();
    for (auto it = items.cbegin(); it != items.cend(); ++it)
        require(it.value()->pos() == positions.value(it.key()), "automatic layout is deterministic");
    const QPointF userPosition = items.value("TextureA")->pos() + QPointF(13, 27);
    items.value("TextureA")->setPos(userPosition);
    graph.refresh();
    require(nodes().value("TextureA")->pos() == userPosition, "notice refresh preserves manual node positioning");
    std::string after;
    stage->GetRootLayer()->ExportToString(&after);
    require(before == after, "automatic graph layout does not author USD");
}

void
materialDialogRefreshAfterLayerRemovalApi()
{
    stageviz::Session* current = stageviz::session();
    require(current->newStage(), "new material layer-removal fixture");

    const UsdStageRefPtr stage = current->stage();
    const SdfLayerHandle root = stage->GetRootLayer();
    const SdfLayerRefPtr materialLayer = SdfLayer::CreateAnonymous("material-layer.usda");
    root->SetSubLayerPaths({ materialLayer->GetIdentifier() });
    require(current->setEditLayer(materialLayer), "set material fixture edit layer");

    UsdShadeMaterial material = UsdShadeMaterial::Define(stage, SdfPath("/Materials/LayerMaterial"));
    UsdShadeShader surface = UsdShadeShader::Define(stage, SdfPath("/Materials/LayerMaterial/PreviewSurface"));
    surface.CreateIdAttr(VtValue(TfToken("UsdPreviewSurface")));
    material.CreateSurfaceOutput().ConnectToSource(surface.CreateOutput(TfToken("surface"), SdfValueTypeNames->Token));
    require(current->setEditLayer(root), "restore root edit layer after fixture creation");

    stageviz::MaterialDialog dialog;
    dialog.show();
    auto* browser = dialog.findChild<stageviz::MaterialBrowser*>("browserWidget");
    require(browser && browser->entries().size() == 1, "material from sublayer is listed in the dialog");
    Q_EMIT browser->materialActivated(material.GetPath());
    QCoreApplication::processEvents();
    require(dialog.findChildren<stageviz::MaterialGraph*>().size() == 1, "material graph tab is open");

    current->commandStack()->execute(
        new stageviz::Command(stageviz::removeLayer(QString::fromStdString(materialLayer->GetIdentifier()))));

    const auto waitUntil = [](const std::function<bool()>& predicate) {
        QElapsedTimer timer;
        timer.start();
        do {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
            if (predicate())
                return true;
            QThread::msleep(1);
        } while (timer.elapsed() < 10000);
        return false;
    };
    // Swatch rendering can delay both worker notices and the debounced refresh.
    // Observe the actual browser/tab result instead of racing a fixed 300 ms timer.
    require(waitUntil([&]() {
                return browser->entries().isEmpty() && dialog.findChildren<stageviz::MaterialGraph*>().isEmpty();
            }),
            "removing a material's owning layer refreshes browser entries and closes its graph tab");
    require(root->GetSubLayerPaths().empty() && !stage->GetPrimAtPath(SdfPath("/Materials/LayerMaterial")),
            "layer removal is reflected in composed USD as well as the material dialog");
    current->commandStack()->undo();
    require(waitUntil([&]() { return browser->entries().size() == 1; }),
            "undoing layer removal restores the material browser entry");
    current->commandStack()->redo();
    require(waitUntil([&]() { return browser->entries().isEmpty(); }),
            "redoing layer removal removes the restored browser entry");
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
materialRevealFileApi()
{
    QTemporaryDir dir;
    require(dir.isValid() && QDir(dir.path()).mkdir("nested"), "temporary material reveal fixture");
    const QString texture = dir.filePath("nested/texture.png");
    QFile file(texture);
    require(file.open(QIODevice::WriteOnly) && file.write("fixture") == 7, "create reveal texture file");
    file.close();
    const auto child = UsdStage::CreateNew(dir.filePath("nested/material.usda").toStdString());
    UsdShadeMaterial::Define(child, SdfPath("/Material"));
    auto shader = UsdShadeShader::Define(child, SdfPath("/Material/Image"));
    shader.CreateIdAttr(VtValue(TfToken("ND_image_color3")));
    shader.CreateInput(TfToken("file"), SdfValueTypeNames->Asset).Set(SdfAssetPath("texture.png"));
    shader.CreateInput(TfToken("missing"), SdfValueTypeNames->Asset).Set(SdfAssetPath("missing.png"));
    shader.CreateInput(TfToken("empty"), SdfValueTypeNames->Asset).Set(SdfAssetPath());
    shader.CreateInput(TfToken("text"), SdfValueTypeNames->String).Set(texture.toStdString());
    require(child->GetRootLayer()->Save(), "save material reveal sublayer");
    const QString scene = dir.filePath("root.usda");
    const auto root = UsdStage::CreateNew(scene.toStdString());
    root->GetRootLayer()->SetSubLayerPaths({ "nested/material.usda" });
    require(root->GetRootLayer()->Save(), "save material reveal root");
    auto* current = stageviz::session();
    require(current->loadFromFile(scene, stageviz::Session::None), "load material reveal fixture");
    stageviz::MaterialEntry entry;
    entry.materialPath = SdfPath("/Material");
    entry.shaderPath = SdfPath("/Material/Image");
    entry.shaderId = "ND_image_color3";
    stageviz::MaterialTree tree;
    tree.resize(640, 800);
    tree.setMaterials({ entry });
    tree.show();
    tree.expandAll();
    QCoreApplication::processEvents();
    const auto menuTarget = [&](const char* input) {
        QTreeWidgetItem* row = nullptr;
        const QString path = QStringLiteral("/Material/Image.inputs:%1").arg(input);
        QTreeWidgetItemIterator it(&tree);
        while (*it) {
            if ((*it)->data(0, Qt::UserRole).toString() == path)
                row = *it;
            ++it;
        }
        require(row, "find material file input row");
        tree.scrollToItem(row);
        const QPoint point = tree.visualItemRect(row).center();
        QString target;
        bool inspected = false;
        QTimer::singleShot(0, &tree, [&]() {
            auto* menu = qobject_cast<QMenu*>(QApplication::activePopupWidget());
            require(menu, "open material input context menu");
            const auto actions = menu->actions();
            for (int i = 0; i < actions.size(); ++i) {
                QAction* action = actions[i];
                if (action->objectName() != "materialRevealFile")
                    continue;
                require(i > 0 && actions[i - 1]->isSeparator(), "file reveal follows a divider");
                require(action->isEnabled(), "material file reveal is enabled");
#ifdef Q_OS_MAC
                require(action->text() == "Reveal in Finder", "macOS file reveal label");
#elif defined(Q_OS_WIN)
                require(action->text() == "Reveal in Explorer", "Windows file reveal label");
#else
                require(action->text() == "Reveal in folder", "file reveal label");
#endif
                target = action->data().toString();
                require(action->toolTip() == target, "file reveal tooltip contains full path");
            }
            inspected = true;
            menu->close();
        });
        QContextMenuEvent event(QContextMenuEvent::Mouse, point, tree.viewport()->mapToGlobal(point));
        QApplication::sendEvent(tree.viewport(), &event);
        require(inspected, "inspect material file context menu");
        return target;
    };
    require(menuTarget("file") == texture, "material texture resolves relative to its authored sublayer");
    require(menuTarget("missing").isEmpty(), "missing material asset has no reveal action");
    require(menuTarget("empty").isEmpty(), "empty material asset has no reveal action");
    require(menuTarget("text").isEmpty(), "string inputs are not treated as files");
    const auto attr = current->stage()->GetAttributeAtPath(SdfPath("/Material/Image.inputs:file"));
    require(attr.Set(SdfAssetPath(texture.toStdString())), "author absolute texture path");
    require(menuTarget("file") == texture, "material reveal reads the current absolute asset value");
    require(attr.Set(SdfAssetPath(dir.path().toStdString())), "author directory asset path");
    require(menuTarget("file").isEmpty(), "directory assets are not offered as revealable files");
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
outlinerClearOpinionsMenuApi()
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
                if (!menu || menu->objectName() != "clearOpinionsMenu")
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
        require(labels
                    == (locked ? QStringList { "Clear", "All" }
                               : QStringList { "Clear", "All", "separator", "Visibility" }),
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
    current->commandStack()->execute(new stageviz::Command(stageviz::clearOpinions({ SdfPath("/ChildOwned") })));
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
    current->commandStack()->execute(new stageviz::Command(stageviz::clearOpinions({ SdfPath("/ChildOwned") })));
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
    tree->setDisplayFilters({}, {}, false);
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
    SdfPath helperPath;
    for (const UsdPrim& prim : stage->GetPseudoRoot().GetAllChildren()) {
        if (!prim.IsDefined()) {
            require(!find(prim.GetPath()), "undefined root excluded from Outliner");
            if (prim.GetName().GetString().find("Flattened_Prototype_") == 0) {
                ++helperCount;
                helperPath = prim.GetPath();
            }
        }
    }
    require(helperCount > 0, "USD flattening produced undefined prototype roots");
    require(!find(SdfPath("/UnresolvedOver/Nested")), "undefined descendants excluded");
    require(find(SdfPath("/UntypedGroup")) && find(SdfPath("/UntypedGroup"))->isHidden(),
            "defined untyped group remains in the model but is hidden by default");
    require(find(SdfPath("/Flattened_Prototype_UserGroup")),
            "defined group with prototype-like name remains in the model");
    require(find(SdfPath("/Inactive")), "inactive defined prim remains visible");
    require(find(SdfPath("/Payload")) && !stage->GetPrimAtPath(SdfPath("/Payload")).IsLoaded(),
            "unloaded payload remains visible");
    require(stage->GetPrimAtPath(SdfPath("/Instance")).IsInstance() && find(SdfPath("/Instance/Cube")),
            "instance and its proxy geometry remain visible");

    tree->setDisplayFilters({}, {}, true);
    drain();
    require(find(helperPath) && find(SdfPath("/UnresolvedOver/Nested")),
            "Untyped display option includes typeless over prims");

    tree->setDisplayFilters({}, {}, false);
    drain();
    const SdfPath changing("/Changing");
    stage->OverridePrim(changing);
    drain();
    require(!find(changing), "new undefined prim stays hidden during resync");
    stage->DefinePrim(changing);
    stage->DefinePrim(changing.AppendChild(TfToken("Child")));
    drain();
    require(find(changing) && find(changing.AppendChild(TfToken("Child"))), "newly defined subtree enters the model");
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
    // This fixture tests checkbox/load policy behavior, independently of the
    // remembered-state autosaves covered by the dedicated state regressions.
    current->setPreserveState(false);
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
    const auto iconImage = [](const QIcon& icon) { return icon.pixmap(24, 24).toImage(); };
    const auto* ui = stageviz::ui();
    require(!ui->icon(stageviz::UI::EditLayer).isNull(), "current edit-layer resource is available");
    require(iconImage(secondRow->icon(stageviz::LayerItem::Name))
                == iconImage(QIcon(ui->icon(stageviz::UI::EditLayer))),
            "active sublayer uses the edit-layer icon");
    require(iconImage(firstRow->icon(stageviz::LayerItem::Name)) == iconImage(QIcon(ui->icon(stageviz::UI::Layer))),
            "inactive layer retains the ordinary layer icon");
    require(iconImage(secondRow->icon(stageviz::LayerItem::Visibility))
                    == iconImage(QIcon(ui->icon(stageviz::UI::Visible, stageviz::UI::Medium, stageviz::UI::Disabled)))
                && iconImage(secondRow->icon(stageviz::LayerItem::Visibility))
                       != iconImage(firstRow->icon(stageviz::LayerItem::Visibility)),
            "active layer eye is visibly disabled while other layer eyes remain enabled");
    require(secondRow->toolTip(stageviz::LayerItem::Visibility).contains("cannot be muted"),
            "active eye explains why muting is unavailable");
    const QPoint eye(tree->header()->sectionViewportPosition(stageviz::LayerItem::Visibility)
                         + tree->columnWidth(stageviz::LayerItem::Visibility) / 2,
                     tree->visualItemRect(secondRow).center().y());
    QMouseEvent eyePress(QEvent::MouseButtonPress, QPointF(eye), QPointF(tree->viewport()->mapToGlobal(eye)),
                         Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(tree->viewport(), &eyePress);
    require(!stage->IsLayerMuted(second->GetIdentifier()), "disabled active-layer eye does not mute the layer");
    require(tree->selectedItems().size() == 2 && firstRow->isSelected() && secondRow->isSelected(),
            "layer refresh and edit target changes preserve multi-selection");
    require(current->setEditLayer(root), "restore edit target");
    tree->refresh();
    require(iconImage(tree->topLevelItem(0)->icon(stageviz::LayerItem::Name))
                    == iconImage(QIcon(ui->icon(stageviz::UI::EditLayer)))
                && iconImage(secondRow->icon(stageviz::LayerItem::Visibility))
                       == iconImage(firstRow->icon(stageviz::LayerItem::Visibility)),
            "edit icon follows the active root and previous layer eye becomes enabled");
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
        { "layer", "model.usda", Dialog::Type::Sublayer, "USD edits", "/example/model.usda", "Modified", true, "root" }
    };
    for (const auto result : { Dialog::Result::Save, Dialog::Result::Discard, Dialog::Result::Cancel }) {
        Dialog dialog;
        dialog.setDocumentName("Untitled");
        dialog.setEntries(entries);
        auto* tree = dialog.findChild<QTreeWidget*>("changesTree");
        require(tree && tree->columnCount() == 5 && tree->topLevelItemCount() == 1
                    && tree->topLevelItem(0)->childCount() == 1 && tree->topLevelItem(0)->isExpanded(),
                "changes table nests sublayers beneath their expanded root");
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
    require(!current->isStateDirty() && !viewer.findChild<QAction*>("fileStateSave")
                && !viewer.windowTitle().contains('*'),
            "untouched Untitled camera changes do not dirty its title or expose a separate state action");
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
            if (!dialog || !dialog->isVisible() || dialog->objectName() != "MessageDialog")
                continue;
            require(!dialog->findChild<QTreeWidget*>("changesTree")
                        && dialog->findChild<QPushButton*>("accept")->text() == "Save"
                        && dialog->findChild<QPushButton*>("reject")->isVisible(),
                    "one authored anonymous root uses the compact save question");
            anonymousReviewed = true;
            dialog->reject();
        }
    });
    viewer.findChild<QAction*>("fileNew")->trigger();
    require(anonymousReviewed, "prompt before closing one authored anonymous root");
    const QString filename = dir.filePath("root.usda");
    require(current->saveToFile(filename), "save root baseline");
    drain();
    require(!viewer.findChild<QAction*>("fileStateSave"), "Save state is not a separate action");
    auto* save = viewer.findChild<QAction*>("fileLayerSave");
    current->viewState()->camera()->setCameraDistance(current->viewState()->camera()->cameraDistance() + 2);
    drain();
    require(!save->isEnabled() && !viewer.windowTitle().contains('*'),
            "camera state does not enable root Save or dirty the document title");
    QFile sessionFile(filename + ".session");
    require(sessionFile.open(QIODevice::ReadOnly), "open baseline remembered state");
    const QByteArray baselineState = sessionFile.readAll();
    sessionFile.close();
    QEventLoop idleLoop;
    QTimer::singleShot(1100, &idleLoop, &QEventLoop::quit);
    idleLoop.exec();
    require(current->isStateDirty(), "remembered state stays dirty while the app is idle");
    require(sessionFile.open(QIODevice::ReadOnly) && sessionFile.readAll() == baselineState,
            "idle time does not rewrite the companion file");
    sessionFile.close();
    require(current->saveToFile(filename), "save the root stage and remembered state");
    drain();
    require(!current->isStateDirty(), "saving the stage clears remembered-state dirtiness");
    require(sessionFile.open(QIODevice::ReadOnly) && sessionFile.readAll() != baselineState,
            "stage Save updates the remembered-state companion");
    sessionFile.close();
    current->setPreserveState(false);
    current->viewState()->camera()->setCameraDistance(current->viewState()->camera()->cameraDistance() + 1);
    drain();
    require(!save->isEnabled() && !viewer.windowTitle().contains('*'),
            "preservation off ignores camera dirtiness in the save UI");
    current->setPreserveState(true);
    drain();
    require(!save->isEnabled() && !viewer.windowTitle().contains('*'),
            "enabling preservation keeps state changes out of the document save UI");
    current->viewState()->camera()->setCameraDistance(current->viewState()->camera()->cameraDistance() + 1);
    require(current->newStage(), "replace stage while remembered state is pending");
    require(!current->isStateDirty(), "stage replacement flushes remembered state without a document prompt");
    require(current->newStage(), "new anonymous root for first-save action refresh");
    current->stage()->DefinePrim(SdfPath("/World/FirstSave"), TfToken("Cube"));
    drain();
    require(current->stage()->GetRootLayer()->IsDirty(), "anonymous USD edits dirty the document");
    // Authored Untitled documents can track remembered view state, and viewport
    // updates may legitimately dirty it. Establish the clean-state baseline
    // explicitly to test first-save UI refresh without a state-dirty transition.
    require(current->saveState(dir.filePath("anonymous-baseline.session")) && !current->isStateDirty(),
            "establish clean remembered state before first root save");
    require(current->saveLayerAs(QString::fromStdString(current->stage()->GetRootLayer()->GetIdentifier()),
                                 dir.filePath("first-save.usda")),
            "save anonymous root without a preceding dirty-state transition");
    drain();
    require(QFileInfo::exists(dir.filePath("first-save.usda.session")) && !save->isEnabled()
                && !current->isStateDirty(),
            "first root save refreshes Save layer after writing a previously missing companion");
    const QString firstSaveSession = dir.filePath("first-save.usda.session");
    QFile firstSaveSessionFile(firstSaveSession);
    require(firstSaveSessionFile.open(QIODevice::ReadOnly), "read first-save remembered state");
    const QByteArray firstSaveState = firstSaveSessionFile.readAll();
    firstSaveSessionFile.close();
    current->viewState()->camera()->setCameraDistance(current->viewState()->camera()->cameraDistance() + 1);
    require(viewer.close(), "close the application with remembered state pending");
    require(!current->isStateDirty(), "app close clears remembered-state dirtiness after saving");
    require(firstSaveSessionFile.open(QIODevice::ReadOnly) && firstSaveSessionFile.readAll() != firstSaveState,
            "app close writes the latest remembered state to the companion");
    firstSaveSessionFile.close();
    current->setPreserveState(originalPreserve);
}

void
layerCreateReplaceDialogApi()
{
    QTemporaryDir dir;
    require(dir.isValid(), "temporary layer editor replacement directory");
    const QString path = dir.filePath("replace.usda");
    const auto old = SdfLayer::CreateNew(path.toStdString());
    SdfCreatePrimInLayer(old, SdfPath("/Old"))->SetSpecifier(SdfSpecifierDef);
    require(old->Save(), "save existing layer editor destination");
    auto* current = stageviz::session();
    require(current->newStage(), "new layer editor replacement stage");
    const bool previousNative = QCoreApplication::testAttribute(Qt::AA_DontUseNativeDialogs);
    QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs, true);
    stageviz::LayerDialog editor;
    editor.show();
    auto* create = editor.findChild<QToolButton*>("newMaterial");
    require(create, "layer editor create control exists");
    bool saveSeen = false;
    bool replaceSeen = false;
    bool timedOut = false;
    QElapsedTimer elapsed;
    elapsed.start();
    QTimer driver;
    QObject::connect(&driver, &QTimer::timeout, &editor, [&]() {
        for (auto* widget : QApplication::topLevelWidgets()) {
            if (auto* message = qobject_cast<QMessageBox*>(widget); message && message->isVisible()) {
                replaceSeen = true;
                auto* replace = message->button(QMessageBox::Yes);
                require(replace, "overwrite confirmation has a Yes button");
                replace->click();
            }
            if (auto* chooser = qobject_cast<QFileDialog*>(widget); chooser && chooser->isVisible()) {
                if (elapsed.elapsed() > 10000) {
                    timedOut = true;
                    chooser->reject();
                }
                else if (!replaceSeen) {
                    saveSeen = true;
                    require(chooser->defaultSuffix() == "usda",
                            "default extension participates in overwrite confirmation");
                    auto* filename = chooser->findChild<QLineEdit*>("fileNameEdit");
                    if (!filename)
                        continue;
                    chooser->setDirectory(dir.path());
                    filename->setText(path);
                    QMetaObject::invokeMethod(chooser, "accept", Qt::QueuedConnection);
                }
            }
        }
    });
    driver.start(25);
    create->click();
    driver.stop();
    const auto confirmation = "save chooser accepts explicit replacement confirmation (save=" + std::to_string(saveSeen)
                              + ", replace=" + std::to_string(replaceSeen) + ", timeout=" + std::to_string(timedOut)
                              + ")";
    require(saveSeen && replaceSeen && !timedOut, confirmation.c_str());
    require(current->stage()->HasLocalLayer(old) && !old->GetPrimAtPath(SdfPath("/Old")),
            "confirmed replacement creates and attaches the empty destination");
    require(SdfLayer::OpenAsAnonymous(path.toStdString())->GetRootPrims().empty(), "replacement reaches disk");
    current->commandStack()->undo();
    require(old->GetPrimAtPath(SdfPath("/Old")) && current->stage()->GetRootLayer()->GetSubLayerPaths().empty(),
            "one undo restores cached contents and removes the new arc");
    current->commandStack()->redo();
    require(current->stage()->HasLocalLayer(old) && old->GetRootPrims().empty(), "one redo recreates the empty layer");
    QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs, previousNative);
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
    const std::string documentBeforeWarmup = [&]() {
        std::string text;
        host.stage()->GetRootLayer()->ExportToString(&text);
        return text;
    }();
    gizmo.update(frame);
    const SdfPath warmupRoot = stageviz::paths::auxiliary::transform;
    require(bool(host.auxiliary->GetPrimAtPath(warmupRoot.AppendChild(TfToken("Rotate")).AppendChild(TfToken("X"))))
                && UsdGeomImageable(host.auxiliary->GetPrimAtPath(warmupRoot)).ComputeVisibility()
                       == UsdGeomTokens->invisible,
            "disabled gizmo prepares hidden meshes before the first interaction");
    std::string preparedAuxiliary;
    host.auxiliary->GetRootLayer()->ExportToString(&preparedAuxiliary);
    gizmo.update(frame);
    std::string repeatedAuxiliary;
    host.auxiliary->GetRootLayer()->ExportToString(&repeatedAuxiliary);
    std::string documentAfterWarmup;
    host.stage()->GetRootLayer()->ExportToString(&documentAfterWarmup);
    require(preparedAuxiliary == repeatedAuxiliary && documentBeforeWarmup == documentAfterWarmup,
            "preparing handles is idempotent and leaves document USD unchanged");
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
viewportGizmoClippingApi()
{
    const QRectF gate(10, 20, 640, 480);
    for (const auto projection : { GfCamera::Perspective, GfCamera::Orthographic }) {
        GfCamera document;
        document.SetProjection(projection);
        document.SetClippingRange(GfRange1f(5, 6));
        const auto original = document.GetClippingRange();
        for (double depth : { 0.05, 2.0, 12.0 }) {
            GfMatrix4d transform(1.0);
            transform.SetScale(depth * 0.02);
            transform.SetTranslateOnly(GfVec3d(0, 0, -depth));
            const auto gizmo = stageviz::gizmoprojection::camera(document, transform);
            require(document.GetClippingRange() == original, "gizmo projection leaves document clipping unchanged");
            require(gizmo.GetTransform() == document.GetTransform()
                        && gizmo.GetHorizontalAperture() == document.GetHorizontalAperture()
                        && gizmo.GetVerticalAperture() == document.GetVerticalAperture(),
                    "gizmo projection preserves the viewport view and field of view");
            for (int corner = 0; corner < 8; ++corner) {
                const GfVec3d local(corner & 1 ? 1.2 : -1.2, corner & 2 ? 1.2 : -1.2, corner & 4 ? 1.2 : -1.2);
                const auto world = transform.Transform(local);
                QPointF screen;
                require(stageviz::gizmoprojection::project(gizmo, gate, world, screen),
                        "all handle bounds remain projectable outside document clipping");
                const auto expected = document.GetFrustum().ComputeProjectionMatrix().Transform(world);
                require(std::abs(screen.x() - (gate.left() + (expected[0] * .5 + .5) * gate.width())) < 1e-6
                            && std::abs(screen.y() - (gate.top() + (.5 - expected[1] * .5) * gate.height())) < 1e-6,
                        "independent near/far planes do not move the projected handles");
            }
        }
    }
    GfCamera document;
    GfMatrix4d behind(1.0);
    behind.SetTranslateOnly(GfVec3d(0, 0, 2));
    QPointF screen;
    require(!stageviz::gizmoprojection::project(stageviz::gizmoprojection::camera(document, behind), gate,
                                                behind.ExtractTranslation(), screen),
            "perspective gizmos behind the eye remain unprojectable");
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

void
richTooltipsApi()
{
    const QString rich = stageviz::qt::toolTip("Name <unsafe>",
                                               { { "Path", "/World/Thing" }, { "Value", "<b>plain text</b>" } });
    require(rich.startsWith("<qt>") && rich.contains("&lt;unsafe&gt;")
                && rich.contains("&lt;b&gt;plain text&lt;/b&gt;"),
            "tooltip headings and values are escaped, not interpreted as markup");
    const QString longPath = "/World/" + QString(150, 'A') + "/Shape";
    QTextDocument document;
    document.setHtml(stageviz::qt::toolTip("Long path", { { "Path", longPath } }));
    QString plain = document.toPlainText();
    plain.remove(QChar(0x200b));
    require(plain.contains(longPath), "wrapping retains the complete long path");

    stageviz::MaterialBrowser browser;
    stageviz::MaterialEntry entry;
    entry.materialPath = SdfPath("/Materials/Paint");
    entry.name = "Paint <blue>";
    entry.shaderId = "UsdPreviewSurface";
    browser.setEntries({ entry });
    auto* icons = browser.findChild<QListWidget*>("icons");
    auto* list = browser.findChild<QListWidget*>("list");
    auto* details = browser.findChild<QTreeWidget*>("details");
    require(icons && list && details, "material tooltip presentations available");
    const auto tooltip = icons->item(0)->toolTip();
    require(tooltip.startsWith("<qt>") && tooltip.contains("Paint &lt;blue&gt;") && tooltip.contains("Shader type")
                && tooltip.contains("USD Preview Surface") && tooltip.contains("/Materials/Paint"),
            "material tooltip includes formatted title, shader type and full USD path");
    require(tooltip == list->item(0)->toolTip() && tooltip == details->topLevelItem(0)->toolTip(0),
            "all material browser views share the same tooltip");
    entry.name = "Updated";
    browser.updateEntry(0, entry);
    require(icons->item(0)->toolTip().contains("Updated"), "material tooltip title follows entry updates");

    auto* current = stageviz::session();
    require(current->newStage(), "new override tooltip fixture");
    const auto stage = current->stage();
    const auto root = stage->GetRootLayer();
    UsdGeomXform::Define(stage, SdfPath("/World/Group"));
    UsdGeomCube::Define(stage, SdfPath("/World/Group/Shape"));
    const auto material = UsdShadeMaterial::Define(stage, SdfPath("/Materials/Paint"));
    const auto source = SdfLayer::CreateAnonymous("tooltip_base.usda");
    source->TransferContent(root);
    root->Clear();
    root->SetSubLayerPaths({ source->GetIdentifier() });
    const auto group = stage->GetPrimAtPath(SdfPath("/World/Group"));
    UsdShadeMaterialBindingAPI::Apply(group).Bind(UsdShadeMaterial(stage->GetPrimAtPath(SdfPath("/Materials/Paint"))),
                                                  UsdShadeTokens->strongerThanDescendants);
    group.CreateAttribute(TfToken("note"), SdfValueTypeNames->String, true).Set(std::string("<b>literal</b>"));
    group.CreateAttribute(TfToken("large"), SdfValueTypeNames->FloatArray, true).Set(VtFloatArray(1000, 1.0f));
    stage->GetPrimAtPath(SdfPath("/World/Group/Shape"))
        .GetAttribute(UsdGeomTokens->visibility)
        .Set(UsdGeomTokens->invisible);
    stageviz::OutlinerView view;
    view.show();
    auto* tree = view.findChild<stageviz::OutlinerTree*>("outlinerTree");
    require(tree, "override tooltip tree available");
    tree->updateStage(stage);
    auto* shape = tree->findItems("Shape", Qt::MatchExactly | Qt::MatchRecursive).value(0);
    require(shape, "tooltip shape row available");
    const auto name = shape->toolTip(stageviz::PrimItem::Name);
    require(name.startsWith("<qt>") && name.contains("<b>Shape</b>") && name.contains("Type") && name.contains("Cube")
                && name.contains("/World/Group/Shape"),
            "prim tooltip has clear name, type and path fields");
    const auto direct = tree->overrideToolTip(SdfPath("/World/Group"));
    require(direct.contains("This prim") && direct.contains("material:binding") && direct.contains("/Materials/Paint")
                && direct.contains("Stronger than descendants") && direct.contains("&lt;b&gt;literal&lt;/b&gt;")
                && direct.contains("Array (1000 values)") && direct.contains("Descendants (1)")
                && direct.contains("visibility") && direct.contains("invisible"),
            "override tooltip describes local properties and descendant opinions");
    const auto ancestor = tree->overrideToolTip(SdfPath::AbsoluteRootPath());
    require(ancestor.contains("Descendants (2)") && ancestor.contains("Layer") && ancestor.contains("Untitled (Root)"),
            "ancestor indicator describes affected descendant prims in the active layer");
    require(shape->toolTip(stageviz::PrimItem::Override).contains("visibility"),
            "override column uses formatted detail tooltip");
    current->commandStack()->execute(
        new stageviz::Command(stageviz::clearRelationships({ SdfPath("/World/Group.material:binding") })));
    for (int i = 0; i < 6; ++i) {
        QThreadPool::globalInstance()->waitForDone();
        QCoreApplication::sendPostedEvents(nullptr, 0);
        QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
    }
    require(!tree->overrideToolTip(SdfPath("/World/Group")).contains("material:binding"),
            "tooltip refreshes after a binding opinion is cleared");
    require(current->setEditLayer(source), "switch tooltip edit layer");
    QCoreApplication::processEvents();
    require(tree->overrideToolTip(SdfPath("/World/Group")).isEmpty(), "override details track the current edit layer");
    require(current->setEditLayer(root), "restore tooltip edit layer");
    const auto ownership = stageviz::qt::toolTip("Ownership", stageviz::qt::primLayerToolTipFields(group));
    require(ownership.contains("tooltip_base.usda") && ownership.contains("Strongest opinion")
                && ownership.contains("Untitled (Root)"),
            "tooltip distinguishes defining and override layers");
    QTreeWidget layerTree;
    stageviz::LayerItem layerItem(&layerTree);
    stageviz::LayerItem::State state;
    state.identifier = "/tmp/look_layer.usda";
    state.name = "*look_layer.usda";
    state.editTarget = true;
    state.dirty = true;
    state.canReorder = true;
    state.primSpecCount = 3;
    layerItem.setState(state);
    const auto layerTip = layerItem.toolTip(stageviz::LayerItem::Name);
    require(layerTip.startsWith("<qt>") && layerTip.contains("<b>look_layer.usda</b>")
                && layerTip.contains("Current edit layer") && layerTip.contains("Unsaved changes")
                && layerTip.contains("/tmp/look_layer.usda"),
            "layer names use the shared structured tooltip");
    require(layerItem.toolTip(stageviz::LayerItem::Permission).contains("Permission")
                && layerItem.toolTip(stageviz::LayerItem::Opinions).contains("3 authored prim spec")
                && layerItem.toolTip(stageviz::LayerItem::Visibility).contains("Visibility"),
            "layer status icons include context and action information");
}

void
outlinerPurposeFilterApi()
{
    auto* current = stageviz::session();
    require(current->newStage(), "new Outliner purpose fixture");
    const auto stage = current->stage();
    UsdGeomXform::Define(stage, SdfPath("/World"));
    const auto render = UsdGeomXform::Define(stage, SdfPath("/World/Render"));
    render.CreatePurposeAttr().Set(UsdGeomTokens->render);
    UsdGeomCube::Define(stage, SdfPath("/World/Render/RenderCube"));
    const auto explicitDefault = UsdGeomCube::Define(stage, SdfPath("/World/Render/DefaultCube"));
    explicitDefault.CreatePurposeAttr().Set(UsdGeomTokens->default_);
    stage->DefinePrim(SdfPath("/World/Render/Bridge"));
    UsdGeomCube::Define(stage, SdfPath("/World/Render/Bridge/Child"));
    const auto guide = UsdGeomXform::Define(stage, SdfPath("/World/Guide"));
    guide.CreatePurposeAttr().Set(UsdGeomTokens->guide);
    UsdGeomCube::Define(stage, SdfPath("/World/Guide/Cube"));
    UsdGeomCube::Define(stage, SdfPath("/World/DefaultCube"));
    const auto source = UsdGeomXform::Define(stage, SdfPath("/Library/Source"));
    UsdGeomCube::Define(stage, SdfPath("/Library/Source/Cube"));
    const auto instance = UsdGeomXform::Define(stage, SdfPath("/World/Instance"));
    instance.GetPrim().GetReferences().AddInternalReference(source.GetPath());
    instance.GetPrim().SetInstanceable(true);
    instance.CreatePurposeAttr().Set(UsdGeomTokens->proxy);
    UsdShadeMaterial::Define(stage, SdfPath("/Looks/Material"));
    std::string original;
    stage->GetRootLayer()->ExportToString(&original);
    stageviz::OutlinerView view;
    view.resize(500, 600);
    view.show();
    const auto drain = []() {
        for (int i = 0; i < 6; ++i) {
            QThreadPool::globalInstance()->waitForDone();
            QCoreApplication::sendPostedEvents(nullptr, 0);
            QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
        }
    };
    drain();
    auto* tree = view.findChild<stageviz::OutlinerTree*>("outlinerTree");
    auto* display = view.findChild<QToolButton*>("display");
    require(tree && display, "purpose controls available");
    tree->updateStage(stage);
    tree->setDisplayFilters({}, {}, true);
    require(display->isChecked(), "showing Untyped differs from the default display state");
    tree->setDisplayFilters({}, {}, false);
    require(!display->isChecked(), "default display state leaves Untyped hidden without a highlight");
    tree->setDisplayPurposes({ "default" });
    require(display->isChecked(), "purpose restrictions update the Outliner indicator immediately");
    tree->setDisplayPurposes({ "default", "render", "proxy", "guide" });
    require(!display->isChecked(), "restoring all purposes clears the Outliner indicator");
    bool contextFound = false;
    QTimer::singleShot(0, &view, [&]() {
        auto* menu = qobject_cast<QMenu*>(QApplication::activePopupWidget());
        require(menu && display->isChecked(), "context-opened Outliner Display menu stays highlighted");
        contextFound = true;
        menu->close();
    });
    const QPoint point = display->rect().center();
    QContextMenuEvent context(QContextMenuEvent::Mouse, point, display->mapToGlobal(point));
    QApplication::sendEvent(display, &context);
    require(contextFound && !display->isChecked(), "closing the default Outliner menu clears its highlight");
    tree->setDisplayFilters({}, {}, true);
    require(display->isChecked(), "including Untyped highlights customized display settings");
    tree->setDisplayFilters({}, {}, false);
    require(!display->isChecked(), "restoring default Untyped exclusion clears the highlight");

    tree->setDisplayFilters({}, {}, true);

    const auto row = [&](const char* path) -> QTreeWidgetItem* {
        QTreeWidgetItemIterator it(tree);
        while (*it) {
            if ((*it)->data(0, stageviz::PrimItem::Path).toString() == QString::fromUtf8(path))
                return *it;
            ++it;
        }
        return nullptr;
    };
    const auto visible = [&](const char* path) {
        auto* item = row(path);
        require(item, (std::string("expected purpose row exists: ") + path).c_str());
        for (; item; item = item->parent())
            if (item->isHidden())
                return false;
        return true;
    };
    const QStringList all { "default", "render", "proxy", "guide" };
    require(tree->displayPurposes() == all, "all purposes are initially included");
    tree->setDisplayPurposes({ "render" });
    require(visible("/World"), "render filter retains World ancestor");
    require(visible("/World/Render/RenderCube"), "render filter includes inherited cube");
    require(visible("/World/Render/Bridge/Child"), "render inheritance crosses non-imageable prims");
    require(visible("/World/Render/Bridge"), "render filter retains untyped ancestor");
    require(!visible("/World/Render/DefaultCube") && !visible("/World/Guide/Cube") && !visible("/World/Instance/Cube"),
            "explicit default purpose overrides ancestor render purpose");
    tree->setDisplayPurposes({ "proxy" });
    require(visible("/World/Instance/Cube") && !visible("/World/Render/RenderCube"),
            "instance proxy children inherit purpose from their scene instance root");
    tree->setDisplayPurposes({});
    require(!visible("/World/DefaultCube") && !visible("/World/Instance/Cube") && visible("/Looks/Material"),
            "empty purpose selection hides all geometry without hiding non-imageable materials");
    tree->setDisplayPurposes({ "render" });
    tree->setDisplayFilters({}, {}, false, false, true);
    require(!visible("/World/Render/RenderCube"), "purpose and explicit empty type filters intersect");
    tree->setDisplayFilters({}, {});
    require(tree->displayPurposes() == QStringList { "render" } && visible("/World/Render/RenderCube"),
            "changing other display filters preserves purpose selection");
    tree->setFilter("RenderCube");
    require(visible("/World/Render/RenderCube") && !visible("/World/Render/Bridge/Child"),
            "text and purpose filters intersect");
    tree->setFilter({});
    auto* retained = row("/World/Render/RenderCube");
    current->commandStack()->execute(new stageviz::Command(
        stageviz::setAttributeValue(SdfPath("/World/Render.purpose"), VtValue(UsdGeomTokens->guide))));
    drain();
    require(!visible("/World/Render/RenderCube") && row("/World/Render/RenderCube") == retained,
            "purpose info-only changes update inherited descendants without rebuilding rows");
    current->commandStack()->undo();
    drain();
    require(visible("/World/Render/RenderCube"), "purpose undo restores inherited filtering");
    std::string restored;
    stage->GetRootLayer()->ExportToString(&restored);
    require(original == restored, "display filters do not author USD opinions");
    tree->setDisplayPurposes(all);
    bool inspected = false;
    QTimer::singleShot(0, &view, [&]() {
        auto* menu = qobject_cast<QMenu*>(QApplication::activePopupWidget());
        require(menu, "purpose display menu opens");
        const auto actions = menu->actions();
        require(actions.size() >= 6 && actions[actions.size() - 6]->isSeparator()
                    && actions[actions.size() - 5]->text() == "Purpose" && !actions[actions.size() - 5]->isEnabled(),
                "Purpose is the final separated category");
        for (int i = 0; i < 4; ++i) {
            auto* action = actions[actions.size() - 4 + i];
            require(action->isCheckable() && action->isChecked() && action->data().toString() == "purpose:" + all[i],
                    "purpose choices use distinct stable keys and start checked");
            action->trigger();
        }
        require(tree->displayPurposes().isEmpty() && !visible("/World/DefaultCube"),
                "unchecking the final purpose retains explicit none");
        inspected = true;
        menu->close();
    });
    display->click();
    require(inspected, "purpose display menu inspected");
    tree->updateStage(stage);
    require(tree->displayPurposes().isEmpty() && !visible("/World/DefaultCube"), "none survives tree refresh");
}

void
outlinerDisplayMenuApi()
{
    auto* current = stageviz::session();
    stageviz::OutlinerView view;
    view.show();
    require(current->newStage(), "new Outliner display-filter fixture");
    const UsdStageRefPtr stage = current->stage();
    stage->DefinePrim(SdfPath("/World"), TfToken("Xform"));
    stage->DefinePrim(SdfPath("/World/Shape"), TfToken("Cube"));
    stage->DefinePrim(SdfPath("/World/Group"), TfToken("Scope"));
    stage->DefinePrim(SdfPath("/World/Untyped"));
    for (int i = 0; i < 4; ++i) {
        QThreadPool::globalInstance()->waitForDone();
        QCoreApplication::sendPostedEvents(nullptr, 0);
        QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
    }

    auto* display = view.findChild<QToolButton*>("display");
    auto* tree = view.findChild<stageviz::OutlinerTree*>("outlinerTree");
    require(display && tree, "Outliner exposes Display button and tree");
    const auto find = [&](const QString& path) {
        QTreeWidgetItemIterator iterator(tree);
        while (*iterator) {
            if ((*iterator)->data(0, stageviz::PrimItem::Path).toString() == path)
                return *iterator;
            ++iterator;
        }
        return static_cast<QTreeWidgetItem*>(nullptr);
    };
    auto* untypedPrim = find("/World/Untyped");
    require(untypedPrim && untypedPrim->isHidden() && !tree->showUntyped(),
            "Untyped starts disabled and defined untyped prims are hidden by default");

    bool sectionsValid = false;
    bool cubeFilterApplied = false;
    bool untypedOptionFound = false;
    bool untypedFilterApplied = false;
    QTimer::singleShot(0, &view, [&]() {
        auto* menu = qobject_cast<QMenu*>(QApplication::activePopupWidget());
        if (!menu)
            return;

        const QList<QAction*> actions = menu->actions();
        const int divider = [&]() {
            for (int index = 0; index < actions.size(); ++index) {
                if (actions[index]->isSeparator())
                    return index;
            }
            return -1;
        }();
        auto* sources = menu->findChild<QMenu*>("displaySourcesMenu");
        auto* layers = menu->findChild<QMenu*>("displayLayersMenu");
        sectionsValid = actions.size() > 7 && actions.first()->text() == "Sources" && sources && layers
                        && layers->actions().size() == 1 && layers->actions().front()->text() == "Untitled  (Root)"
                        && !menu->findChild<QMenu*>("displayReferencedAssetsMenu")
                        && !menu->findChild<QMenu*>("displayPackagesMenu") && divider == 1
                        && actions[divider + 1]->text() == "Prim types";

        for (QAction* action : actions) {
            if (action->text() == "Cube" && action->isCheckable()) {
                action->trigger();
                cubeFilterApplied = !tree->displayTypes().isEmpty() && !tree->displayTypes().contains("Cube")
                                    && tree->displayTypes().contains("Scope");
            }
            else if (action->text() == "Untyped" && action->isCheckable()) {
                untypedOptionFound = !action->isChecked();
                action->trigger();
                untypedFilterApplied = tree->showUntyped() && !find("/World/Untyped")->isHidden();
                action->trigger();
                untypedFilterApplied = untypedFilterApplied && !tree->showUntyped()
                                       && find("/World/Untyped")->isHidden();
            }
        }
        sectionsValid = sectionsValid && untypedOptionFound;
        menu->close();
    });
    display->click();

    require(sectionsValid, "Display menu separates layer and prim-type options");
    require(cubeFilterApplied, "prim-type menu option filters Outliner rows");
    require(untypedOptionFound && untypedFilterApplied, "Untyped starts unchecked and can show and hide untyped prims");

    const QSet<QString> typeIds { QStringLiteral("Cube"), QStringLiteral("Scope"), QStringLiteral("Xform") };
    const QString rootLayer = QString::fromStdString(stage->GetRootLayer()->GetIdentifier());
    const auto openMenu = [&](const std::function<void(const QList<QAction*>&)>& inspect) {
        bool found = false;
        QTimer::singleShot(0, &view, [&]() {
            auto* menu = qobject_cast<QMenu*>(QApplication::activePopupWidget());
            if (!menu)
                return;
            found = true;
            QList<QAction*> actions;
            const auto collect = [&](auto&& self, QMenu* parent) -> void {
                for (auto* action : parent->actions()) {
                    actions.append(action);
                    if (action->menu())
                        self(self, action->menu());
                }
            };
            collect(collect, menu);
            inspect(actions);
            menu->close();
        });
        display->click();
        require(found, "open Outliner Display menu for all/none checks");
    };
    const auto setGroup = [&](const QSet<QString>& keys, bool checked) {
        openMenu([&](const QList<QAction*>& actions) {
            for (QAction* action : actions)
                if (action->isCheckable() && keys.contains(action->data().toString()) && action->isChecked() != checked)
                    action->trigger();
        });
    };
    const auto checkGroup = [&](const QSet<QString>& keys, bool checked) {
        openMenu([&](const QList<QAction*>& actions) {
            int count = 0;
            for (QAction* action : actions) {
                if (action->isCheckable() && keys.contains(action->data().toString())) {
                    ++count;
                    require(action->isChecked() == checked, "Outliner menu preserves explicit all/none selections");
                }
            }
            require(count == keys.size(), "Outliner keeps all available filter choices");
        });
    };
    const auto setUntyped = [&](bool checked) {
        openMenu([&](const QList<QAction*>& actions) {
            for (QAction* action : actions)
                if (action->text() == "Untyped" && action->isCheckable() && action->isChecked() != checked)
                    action->trigger();
        });
    };
    const auto checkEmpty = [&]() {
        QTreeWidgetItemIterator it(tree);
        while (*it) {
            require((*it)->isHidden(), "Outliner has no visible rows when its filter selection is empty");
            ++it;
        }
    };

    setUntyped(true);
    setGroup(typeIds, false);
    checkGroup(typeIds, false);
    require(tree->displayTypesFiltered() && tree->displayTypes().isEmpty(),
            "unchecking the last type retains an explicit empty type filter");
    require(find("/World/Shape")->isHidden() && find("/World/Group")->isHidden() && !find("/World/Untyped")->isHidden(),
            "untyped prims remain an independent display option");
    setUntyped(false);
    checkEmpty();
    tree->updateStage(stage);
    checkEmpty();
    checkGroup(typeIds, false);
    setUntyped(true);
    require(tree->displayTypesFiltered() && tree->displayTypes().isEmpty() && find("/World/Shape")->isHidden(),
            "changing Untyped does not reset an empty named-type selection");
    setUntyped(false);
    checkEmpty();
    setGroup({ QStringLiteral("Cube") }, true);
    require(!find("/World/Shape")->isHidden() && find("/World/Group")->isHidden(),
            "one type can be re-enabled from an empty Outliner filter");
    setGroup(typeIds, true);
    require(!tree->displayTypesFiltered(), "selecting every type restores the unfiltered state");

    setGroup({ rootLayer }, false);
    checkEmpty();
    checkGroup({ rootLayer }, false);
    require(tree->displayLayersFiltered() && tree->displayLayers().isEmpty(),
            "unchecking the only layer retains an explicit empty layer filter");
    setUntyped(true);
    checkEmpty();
    require(tree->displayLayersFiltered(), "changing Untyped preserves the layer filter");
    setGroup({ rootLayer }, true);
    require(!tree->displayLayersFiltered() && !find("/World/Shape")->isHidden() && !find("/World/Untyped")->isHidden(),
            "a layer can be re-enabled after all layers were unchecked");
}

}  // namespace

int
main(int argc, char** argv)
{
    std::unique_ptr<QTemporaryDir> selectionTrace;
    if (argc == 2
        && (std::string(argv[1]) == "render_coincident_selection_api"
            || std::string(argv[1]) == "render_display_mode_warmup_api"
            || std::string(argv[1]) == "material_swatch_color_view_api")) {
        selectionTrace = std::make_unique<QTemporaryDir>();
        require(selectionTrace->isValid(), "create selection warmup trace directory");
        qputenv("STAGEVIZ_PERF_LOG", (selectionTrace->path() + "/selection.jsonl").toUtf8());
    }
    QTemporaryDir testSettings;
    require(testSettings.isValid(), "create isolated native GUI settings");
    if (qEnvironmentVariableIsEmpty("STAGEVIZ_TEST_SETTINGS_DIR"))
        qputenv("STAGEVIZ_TEST_SETTINGS_DIR", testSettings.path().toUtf8());
    stageviz::Application app(argc, argv);
    // Release builds capture stderr in the application's console. Native test
    // assertions and timeout diagnostics must remain visible to CTest instead.
    app.console()->stop();
    const std::map<std::string, std::function<void()>> cases {
        { "outliner_layer_mute_api", outlinerLayerMuteApi },
        { "outliner_display_menu_api", outlinerDisplayMenuApi },
        { "outliner_purpose_filter_api", outlinerPurposeFilterApi },
        { "rich_tooltips_api", richTooltipsApi },
        { "viewport_components_api", viewportComponentsApi },
        { "viewport_capture_paths_api", viewportCapturePathsApi },
        { "viewport_lights_api", viewportLightsApi },
        { "viewport_transform_pivot_api", viewportTransformPivotApi },
        { "viewport_transform_hierarchy_sync_api", viewportTransformHierarchySyncApi },
        { "viewport_transform_individual_rotation_api", viewportTransformIndividualRotationApi },
        { "viewport_gizmo_default_frame_api", viewportGizmoDefaultFrameApi },
        { "viewport_gizmo_clipping_api", viewportGizmoClippingApi },
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
        { "render_coincident_selection_api", renderCoincidentSelectionApi },
        { "render_display_mode_warmup_api", renderDisplayModeWarmupApi },
        { "render_color_management_api", []() { renderColorManagementApi(false); } },
        { "render_color_management_gl_api", []() { renderColorManagementApi(true); } },
        { "renderengine_hdri_api", renderEngineHdriApi },
        { "material_browser_api", materialBrowserApi },
        { "material_browser_display_menu_api", materialBrowserDisplayMenuApi },
        { "material_unknown_surface_context_api", materialUnknownSurfaceContextApi },
        { "material_select_multi_selection_api", materialSelectMultiSelectionApi },
        { "material_follow_selection_api", materialFollowSelectionApi },
        { "material_graph_disconnected_network_layout_api", materialGraphDisconnectedNetworkLayoutApi },
        { "material_graph_shared_branch_layout_api", materialGraphSharedBranchLayoutApi },
        { "material_dialog_refresh_after_layer_removal_api", materialDialogRefreshAfterLayerRemovalApi },
        { "property_reveal_file_api", propertyRevealFileApi },
        { "material_reveal_file_api", materialRevealFileApi },
        { "material_numeric_editor_api", materialNumericEditorApi },
        { "property_asset_editor_api", propertyAssetEditorApi },
        { "property_material_light_dropdown_api", propertyMaterialLightDropdownApi },
        { "material_swatch_alpha_api", materialSwatchAlphaApi },
        { "material_swatch_color_view_api", materialSwatchColorViewApi },
        { "geometry_subset_gui_api", geometrySubsetGuiApi },
        { "outliner_clear_opinions_menu_api", outlinerClearOpinionsMenuApi },
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
        { "layer_create_replace_dialog_api", layerCreateReplaceDialogApi },
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
