// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#include "materialrenderer.h"
#include "renderengine.h"
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
#include <functional>
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
    void receive(const QString& path, const QImage& image, const QString& error, quint64 serial);

    static QString materialCacheDirectory();
    static QString extractResource(const QString& resourcePath, const QString& relativePath);
    static UsdStageRefPtr createPreviewStage(QString& error);
    static bool updatePreviewMaterial(const UsdStageRefPtr& stage, const MaterialParameters& parameters);

public:
    class MaterialRenderWorker : public QObject {
    public:
        using Result = std::function<void(const QString&, const QImage&, const QString&, quint64)>;

        void render(const QString& path, const MaterialParameters& parameters, quint64 serial, Result result)
        {
            QString error;
            QImage image;

            if (!ensureRenderer(error) || !MaterialRendererPrivate::updatePreviewMaterial(m_stage, parameters)) {
                if (error.isEmpty())
                    error = QStringLiteral("Could not update material preview shader");
            }
            else {
                image = m_renderEngine->renderImage();
                if (image.isNull())
                    error = QStringLiteral("Material preview render failed");
            }

            result(path, image, error, serial);
        }

        void reset()
        {
            // Called on this worker thread so RenderEngine/Hgi teardown happens on
            // the same thread that created and used it.
            m_renderEngine.reset();
            m_stage = nullptr;
        }

    private:
        bool ensureRenderer(QString& error)
        {
            if (m_renderEngine && m_stage)
                return true;

            m_stage = MaterialRendererPrivate::createPreviewStage(error);
            if (!m_stage)
                return false;

            m_renderEngine = std::make_unique<RenderEngine>(RenderEngine::ContextMode::Offscreen);

            RenderEngine::Settings settings;
            settings.clearColor = QColor::fromRgbF(0.14, 0.14, 0.15, 1.0);
            settings.sceneMaterialsEnabled = true;
            settings.sceneLightsEnabled = true;
            settings.defaultCameraLightEnabled = true;

            m_renderEngine->setStage(m_stage);
            m_renderEngine->setSettings(settings);
            m_renderEngine->setSize(GfVec2i(256, 256));

            const UsdGeomCamera camera(m_stage->GetPrimAtPath(SdfPath("/Stageviz/Camera")));
            if (!camera) {
                error = QStringLiteral("Material preview camera is missing");
                reset();
                return false;
            }

            m_renderEngine->setCamera(camera.GetCamera(UsdTimeCode::Default()));
            return true;
        }

        UsdStageRefPtr m_stage;
        std::unique_ptr<RenderEngine> m_renderEngine;
    };

    struct Pending {
        MaterialParameters parameters;
        quint64 serial = 0;
        bool forceRender = false;
    };

    struct Data {
        QPointer<MaterialRenderer> renderer;
        QThread* thread = nullptr;
        MaterialRenderWorker* worker = nullptr;
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
    d.thread = new QThread(d.renderer.data());
    d.worker = new MaterialRenderWorker();
    d.worker->moveToThread(d.thread);

    QObject::connect(d.thread, &QThread::finished, d.worker, &QObject::deleteLater);
    d.thread->start();
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
    d.pending.insert(path, pending);

    // One render per path at a time. Slider movement merely replaces the
    // pending parameters; it never builds a long queue of obsolete renders.
    if (d.active.contains(path))
        return;

    d.active.insert(path);

    const Pending request = d.pending.take(path);
    QPointer<MaterialRenderer> renderer = d.renderer;

    QMetaObject::invokeMethod(
        d.worker,
        [this, renderer, path, request]() {
            d.worker->render(path, request.parameters, request.serial,
                             [this, renderer](const QString& resultPath, const QImage& image, const QString& error,
                                              quint64 serial) {
                                 if (!renderer)
                                     return;

                                 QMetaObject::invokeMethod(
                                     renderer,
                                     [this, resultPath, image, error, serial]() {
                                         receive(resultPath, image, error, serial);
                                     },
                                     Qt::QueuedConnection);
                             });
        },
        Qt::QueuedConnection);
}

void
MaterialRendererPrivate::receive(const QString& path, const QImage& image, const QString& error, quint64 serial)
{
    d.active.remove(path);

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

    // If the user moved a slider again while this render was running, only
    // render the newest pending value.
    const auto next = d.pending.find(path);
    if (next != d.pending.end()) {
        const Pending request = next.value();
        d.pending.erase(next);
        dispatch(path, request.parameters, true);
    }
}

MaterialRenderer::MaterialRenderer(QObject* parent)
    : QObject(parent)
    , p(new MaterialRendererPrivate())
{
    p->d.renderer = this;
    p->init();
}

MaterialRenderer::~MaterialRenderer()
{
    if (p->d.worker && p->d.thread && p->d.thread->isRunning()) {
        QMetaObject::invokeMethod(
            p->d.worker, [worker = p->d.worker]() { worker->reset(); }, Qt::BlockingQueuedConnection);

        p->d.thread->quit();
        p->d.thread->wait();
    }
}

void
MaterialRenderer::request(const SdfPath& materialPath, const MaterialParameters& parameters, bool forceRender)
{
    if (materialPath.IsEmpty())
        return;

    p->dispatch(QString::fromStdString(materialPath.GetString()), parameters, forceRender);
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

    if (p->d.worker) {
        QMetaObject::invokeMethod(
            p->d.worker, [worker = p->d.worker]() { worker->reset(); }, Qt::QueuedConnection);
    }
}

}  // namespace stageviz
