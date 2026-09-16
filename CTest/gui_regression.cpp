// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell

#include "renderengine.h"
#include "settings.h"
#include "style.h"
#include <QApplication>
#include <QColor>
#include <QCoreApplication>
#include <QDir>
#include <QSettings>
#include <QTemporaryDir>
#include <functional>
#include <iostream>
#include <map>
#include <stdexcept>
#include <pxr/base/gf/camera.h>
#include <pxr/base/gf/matrix4d.h>
#include <pxr/base/gf/range1f.h>
#include <pxr/base/gf/vec3d.h>
#include <pxr/usd/usd/stage.h>
#include <pxr/usd/usdGeom/cube.h>

PXR_NAMESPACE_USING_DIRECTIVE

namespace {
    void require(bool result, const char* message)
    {
        if (!result)
            throw std::runtime_error(message);
    }

    void settingsApi()
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
            const QString key = QStringLiteral("tests/native/value_%1")
                                    .arg(QCoreApplication::applicationPid());

            require(settings.value(key, 42).toInt() == 42, "settings returns default value");

            settings.setValue(key, QStringLiteral("stored"));
            require(settings.value(key).toString() == QStringLiteral("stored"), "settings value roundtrip");
        }

        QSettings::setDefaultFormat(previousFormat);
    }

    void styleApi()
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
                    && style.fontSize(stageviz::Style::Large) == 17, "style font size roundtrip");

        style.setIconSize(stageviz::Style::Small, 16);
        style.setIconSize(stageviz::Style::Medium, 24);
        style.setIconSize(stageviz::Style::Large, 32);
        require(style.iconSize(stageviz::Style::Small) == 16 && style.iconSize(stageviz::Style::Medium) == 24
                    && style.iconSize(stageviz::Style::Large) == 32, "style icon size roundtrip");

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

    void renderEngineApi()
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
        engine.setMask({SdfPath("/Cube")});
        engine.setSelected({SdfPath("/Cube")});
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
}

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    const std::map<std::string, std::function<void()>> cases {
        {"settings_api", settingsApi},
        {"style_api", styleApi},
        {"renderengine_api", renderEngineApi},
    };

    if (argc != 2 || !cases.count(argv[1])) {
        std::cerr << "Pass one registered GUI regression case name\n";
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
