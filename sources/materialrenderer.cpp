// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#include "materialrenderer.h"
#include "application.h"
#include "renderengine.h"
#include "style.h"
#include <QApplication>
#include <QColor>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QMetaObject>
#include <QPointer>
#include <QSet>
#include <QStandardPaths>
#include <QThread>
#include <algorithm>
#include <cmath>
#include <memory>
#include <pxr/base/gf/matrix4d.h>
#include <pxr/base/vt/value.h>
#include <pxr/usd/sdf/layer.h>
#include <pxr/usd/usdGeom/bboxCache.h>
#include <pxr/usd/usdGeom/camera.h>
#include <pxr/usd/usdGeom/metrics.h>
#include <pxr/usd/usdShade/materialBindingAPI.h>

PXR_NAMESPACE_USING_DIRECTIVE

namespace stageviz {

class MaterialRendererPrivate {
public:
    void init();
    void dispatch(const QString& path, const MaterialParameters& parameters, bool forceRender);
    void process(const QString& path);
    void receive(const QString& path, const QImage& image, const QString& error, quint64 serial);
    void reset();
    bool ensureRenderer(QString& error);
    QImage render(const MaterialParameters& parameters, QString& error);
    static QString materialCacheDirectory();
    static QString extractResource(const QString& resourcePath, const QString& relativePath);
    static UsdStageRefPtr createPreviewStage(QString& error);
    static bool updatePreviewMaterial(const UsdStageRefPtr& stage, const MaterialParameters& parameters);

public:
    struct Pending {
        MaterialParameters parameters;
        quint64 serial = 0;
        bool forceRender = false;
    };

    struct Data {
        QPointer<MaterialRenderer> renderer;

        UsdStageRefPtr stage;
        std::unique_ptr<RenderEngine> renderEngine;

        QHash<QString, QImage> cache;
        QHash<QString, Pending> pending;
        QSet<QString> active;

        quint64 serial = 0;
    };

    Data d;
};

QString
MaterialRendererPrivate::materialCacheDirectory()
{
    QString path = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    if (path.isEmpty())
        path = QDir::tempPath() + "/stageviz";

    path += "/materials";
    QDir().mkpath(path + "/textures");
    return path;
}

QString
MaterialRendererPrivate::extractResource(const QString& resourcePath, const QString& relativePath)
{
    QFile source(resourcePath);
    if (!source.open(QIODevice::ReadOnly))
        return {};

    const QString filename = materialCacheDirectory() + "/" + relativePath;
    QDir().mkpath(QFileInfo(filename).absolutePath());

    QFile target(filename);
    if (!target.open(QIODevice::WriteOnly))
        return {};

    target.write(source.readAll());
    return filename;
}

UsdStageRefPtr
MaterialRendererPrivate::createPreviewStage(QString& error)
{
    const QString shaderball = extractResource(":/materials/shaderball.usda", "shaderball.usda");
    if (shaderball.isEmpty()) {
        error = QStringLiteral("Missing resource :/materials/shaderball.usda");
        return {};
    }

    const SdfLayerRefPtr root = SdfLayer::CreateAnonymous("stageviz_material_preview.usda");
    root->GetSubLayerPaths().push_back(shaderball.toStdString());

    const UsdStageRefPtr stage = UsdStage::Open(root);
    if (!stage) {
        error = QStringLiteral("Could not open material preview stage");
        return {};
    }

    UsdGeomSetStageUpAxis(stage, UsdGeomTokens->z);
    UsdGeomSetStageMetersPerUnit(stage, 0.001);

    const SdfPath materialPath("/Stageviz/Materials/PreviewMaterial");

    UsdShadeMaterial material = UsdShadeMaterial::Define(stage, materialPath);
    UsdShadeShader shader = UsdShadeShader::Define(stage, materialPath.AppendChild(TfToken("PreviewSurface")));

    shader.CreateIdAttr(VtValue(TfToken("UsdPreviewSurface")));
    shader.CreateInput(TfToken("diffuseColor"), SdfValueTypeNames->Color3f).Set(GfVec3f(0.5f));
    shader.CreateInput(TfToken("metallic"), SdfValueTypeNames->Float).Set(0.0f);
    shader.CreateInput(TfToken("roughness"), SdfValueTypeNames->Float).Set(0.4f);
    shader.CreateInput(TfToken("opacity"), SdfValueTypeNames->Float).Set(1.0f);
    shader.CreateInput(TfToken("ior"), SdfValueTypeNames->Float).Set(1.5f);
    shader.CreateInput(TfToken("clearcoat"), SdfValueTypeNames->Float).Set(0.0f);
    shader.CreateInput(TfToken("clearcoatRoughness"), SdfValueTypeNames->Float).Set(0.1f);

    const UsdShadeOutput surface = shader.CreateOutput(TfToken("surface"), SdfValueTypeNames->Token);
    material.CreateSurfaceOutput().ConnectToSource(surface);

    const UsdPrim preview = stage->GetPrimAtPath(SdfPath("/root/Preview_Mesh/Preview_Mesh"));
    if (!preview) {
        error = QStringLiteral("shaderball.usda is missing /root/Preview_Mesh/Preview_Mesh");
        return {};
    }

    UsdShadeMaterialBindingAPI::Apply(preview).Bind(material);
    const UsdPrim rootPrim = stage->GetPrimAtPath(SdfPath("/root"));
    const TfTokenVector purposes { UsdGeomTokens->default_, UsdGeomTokens->proxy, UsdGeomTokens->render };

    UsdGeomBBoxCache cache(UsdTimeCode::Default(), purposes, true);
    const GfRange3d range = cache.ComputeWorldBound(rootPrim).ComputeAlignedRange();
    const GfVec3d center = range.GetMidpoint();
    const double radius = std::max(1.0, range.GetSize().GetLength() * 0.5);

    constexpr double pi = 3.14159265358979323846;
    const double azimuth = 18.0 * pi / 180.0;
    const double elevation = 18.0 * pi / 180.0;
    const double distance = radius * 3.8;
    const double horizontal = std::cos(elevation) * distance;

    const GfVec3d eye = center
                        + GfVec3d(std::sin(azimuth) * horizontal, -std::cos(azimuth) * horizontal,
                                  std::sin(elevation) * distance);

    GfVec3d target = center;
    target[2] -= radius * 0.06;

    UsdGeomCamera camera = UsdGeomCamera::Define(stage, SdfPath("/Stageviz/Camera"));
    camera.CreateFocalLengthAttr(VtValue(55.0f));
    camera.CreateHorizontalApertureAttr(VtValue(20.955f));
    camera.CreateVerticalApertureAttr(VtValue(20.955f));

    GfMatrix4d view(1.0);
    view.SetLookAt(eye, target, GfVec3d(0.0, 0.0, 1.0));
    camera.MakeMatrixXform().Set(view.GetInverse());
    return stage;
}

bool
MaterialRendererPrivate::updatePreviewMaterial(const UsdStageRefPtr& stage, const MaterialParameters& parameters)
{
    if (!stage)
        return false;

    const UsdPrim shaderPrim = stage->GetPrimAtPath(SdfPath("/Stageviz/Materials/PreviewMaterial/PreviewSurface"));
    if (!shaderPrim || !shaderPrim.IsA<UsdShadeShader>())
        return false;

    UsdShadeShader shader(shaderPrim);
    shader.GetInput(TfToken("diffuseColor")).Set(parameters.baseColor);
    shader.GetInput(TfToken("metallic")).Set(parameters.metalness);
    shader.GetInput(TfToken("roughness")).Set(parameters.roughness);
    shader.GetInput(TfToken("opacity")).Set(parameters.opacity);
    shader.GetInput(TfToken("ior")).Set(parameters.ior);
    shader.GetInput(TfToken("clearcoat")).Set(parameters.coat);
    shader.GetInput(TfToken("clearcoatRoughness")).Set(parameters.coatRoughness);
    return true;
}

void
MaterialRendererPrivate::init()
{
    // RenderEngine::ContextMode::Offscreen may create a QOffscreenSurface.
    //
    // On Windows Qt implements that surface using a QWindow-backed platform
    // surface, which must be created on the GUI thread. MaterialRenderer itself
    // lives on the GUI thread, so all RenderEngine creation and rendering is
    // queued back through this object.
}

bool
MaterialRendererPrivate::ensureRenderer(QString& error)
{
    if (d.renderEngine && d.stage)
        return true;

    if (!qApp || QThread::currentThread() != qApp->thread()) {
        error = QStringLiteral("Material preview renderer must be created on the GUI thread");
        return false;
    }

    d.stage = createPreviewStage(error);
    if (!d.stage)
        return false;

    RenderEngine::Settings settings;
    settings.clearColor = style()->color(Style::ColorRole::Render);
    settings.sceneMaterialsEnabled = true;
    settings.sceneLightsEnabled = true;
    settings.defaultCameraLightEnabled = true;

    d.renderEngine = std::make_unique<RenderEngine>(RenderEngine::ContextMode::Offscreen);

    d.renderEngine->setStage(d.stage);
    d.renderEngine->setSettings(settings);
    d.renderEngine->setSize(GfVec2i(512, 512));

    const UsdGeomCamera camera(d.stage->GetPrimAtPath(SdfPath("/Stageviz/Camera")));

    if (!camera) {
        error = QStringLiteral("Material preview camera is missing");
        reset();
        return false;
    }

    d.renderEngine->setCamera(camera.GetCamera(UsdTimeCode::Default()));

    return true;
}

QImage
MaterialRendererPrivate::render(const MaterialParameters& parameters, QString& error)
{
    error.clear();

    if (!ensureRenderer(error))
        return {};

    if (!updatePreviewMaterial(d.stage, parameters)) {
        error = QStringLiteral("Could not update material preview shader");
        return {};
    }

    QImage image = d.renderEngine->renderImage();

    if (image.isNull()) {
        error = QStringLiteral("Material preview render failed");
    }

    return image;
}

void
MaterialRendererPrivate::reset()
{
    // This function is called on MaterialRenderer's GUI thread. Destroying the
    // RenderEngine here also guarantees that its Qt/OpenGL offscreen resources
    // are destroyed on the same thread where they were created.
    d.renderEngine.reset();
    d.stage = nullptr;
}

void
MaterialRendererPrivate::dispatch(const QString& path, const MaterialParameters& parameters, bool forceRender)
{
    if (!forceRender) {
        const auto cached = d.cache.constFind(path);

        if (cached != d.cache.cend()) {
            Q_EMIT d.renderer->rendered(path, cached.value());
            return;
        }
    }
    else {
        d.cache.remove(path);
    }

    Pending pending;
    pending.parameters = parameters;
    pending.serial = ++d.serial;
    pending.forceRender = forceRender;

    // Replacing this entry coalesces rapid slider updates. If a render has not
    // started yet, only the newest parameters will actually be rendered.
    d.pending.insert(path, pending);

    if (d.active.contains(path))
        return;

    d.active.insert(path);

    QPointer<MaterialRenderer> renderer = d.renderer;

    QMetaObject::invokeMethod(
        d.renderer,
        [this, renderer, path]() {
            if (!renderer)
                return;

            process(path);
        },
        Qt::QueuedConnection);
}

void
MaterialRendererPrivate::process(const QString& path)
{
    const auto it = d.pending.find(path);

    if (it == d.pending.end()) {
        d.active.remove(path);
        return;
    }

    const Pending request = it.value();
    d.pending.erase(it);

    QString error;
    const QImage image = render(request.parameters, error);

    receive(path, image, error, request.serial);
}

void
MaterialRendererPrivate::receive(const QString& path, const QImage& image, const QString& error, quint64 serial)
{
    const auto pending = d.pending.constFind(path);

    const bool newerRequestExists = pending != d.pending.cend() && pending->serial > serial;

    if (!newerRequestExists) {
        if (!image.isNull()) {
            d.cache.insert(path, image);

            Q_EMIT d.renderer->rendered(path, image);
        }
        else {
            Q_EMIT d.renderer->error(path, error);
        }
    }

    if (d.pending.contains(path)) {
        // Keep this path active and process only the newest queued value.
        QPointer<MaterialRenderer> renderer = d.renderer;

        QMetaObject::invokeMethod(
            d.renderer,
            [this, renderer, path]() {
                if (!renderer)
                    return;

                process(path);
            },
            Qt::QueuedConnection);

        return;
    }

    d.active.remove(path);
}

MaterialRenderer::MaterialRenderer(QObject* parent)
    : QObject(parent)
    , p(new MaterialRendererPrivate())
{
    p->d.renderer = this;
    p->init();
}

MaterialRenderer::~MaterialRenderer() { p->reset(); }

void
MaterialRenderer::request(const SdfPath& materialPath, const MaterialParameters& parameters, bool forceRender)
{
    if (materialPath.IsEmpty())
        return;

    const QString path = QString::fromStdString(materialPath.GetString());

    // Normally MaterialRenderer is called directly from MaterialDialog on the
    // GUI thread. Keep this guard so future callers from worker threads cannot
    // accidentally recreate the Windows QOffscreenSurface problem.
    if (qApp && QThread::currentThread() != qApp->thread()) {
        QPointer<MaterialRenderer> renderer(this);

        QMetaObject::invokeMethod(
            this,
            [renderer, path, parameters, forceRender]() {
                if (!renderer)
                    return;

                renderer->p->dispatch(path, parameters, forceRender);
            },
            Qt::QueuedConnection);

        return;
    }

    p->dispatch(path, parameters, forceRender);
}

void
MaterialRenderer::invalidate(const SdfPath& materialPath)
{
    if (materialPath.IsEmpty())
        return;

    const QString path = QString::fromStdString(materialPath.GetString());

    p->d.cache.remove(path);
    p->d.pending.remove(path);
}

void
MaterialRenderer::clear()
{
    p->d.cache.clear();
    p->d.pending.clear();

    // A queued process() call may still exist. It will see that its pending
    // request has disappeared and simply remove the active flag.
    p->reset();
}

}  // namespace stageviz
