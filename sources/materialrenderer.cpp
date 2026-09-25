// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#include "materialrenderer.h"
#include "application.h"
#include "renderengine.h"
#include "style.h"
#include <QApplication>
#include <QColor>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QMetaObject>
#include <QPointer>
#include <QSet>
#include <QStandardPaths>
#include <QThread>
#include <QTimer>
#include <algorithm>
#include <cmath>
#include <memory>
#include <pxr/base/gf/matrix4d.h>
#include <pxr/base/vt/value.h>
#include <pxr/usd/sdf/layer.h>
#include <pxr/usd/usd/primRange.h>
#include <pxr/usd/usdGeom/bboxCache.h>
#include <pxr/usd/usdGeom/camera.h>
#include <pxr/usd/usdGeom/mesh.h>
#include <pxr/usd/usdGeom/metrics.h>
#include <pxr/usd/usdGeom/primvarsAPI.h>
#include <pxr/usd/usdShade/connectableAPI.h>
#include <pxr/usd/usdShade/input.h>
#include <pxr/usd/usdShade/materialBindingAPI.h>
#include <pxr/usd/usdShade/output.h>
#include <pxr/usd/usdShade/shader.h>

PXR_NAMESPACE_USING_DIRECTIVE

namespace stageviz {

class MaterialRendererPrivate {
public:
    enum class RequestKind { Parameters, MaterialNetwork, InteractiveNetwork };
    struct RenderContext;

    void init();
    void dispatchParameters(const QString& path, const MaterialParameters& parameters, bool forceRender);
    void dispatchNetwork(const QString& path, const SdfLayerRefPtr& sourceLayer, bool forceRender);
    void dispatchInteractive(const QString& path, const SdfLayerRefPtr& sourceLayer, const SdfPath& inputPath,
                             const VtValue& value);
    void process(const QString& path);
    void receive(const QString& path, const QImage& image, const QString& error, quint64 serial, RequestKind kind);
    void reset();
    void resetContext(RenderContext& context);

    bool ensureParameterRenderer(QString& error);
    bool ensureNetworkRenderer(QString& error, const SdfLayerRefPtr& sourceLayer);
    bool ensureInteractiveNetworkRenderer(QString& error, const SdfLayerRefPtr& sourceLayer);
    bool initializeContext(RenderContext& context, QString& error, const SdfLayerRefPtr& sourceLayer,
                           const GfVec2i& size);
    QImage renderParameters(const MaterialParameters& parameters, QString& error);
    QImage renderNetwork(const SdfPath& materialPath, const SdfLayerRefPtr& sourceLayer, QString& error);
    QImage renderInteractiveNetwork(const SdfPath& materialPath, const SdfLayerRefPtr& sourceLayer,
                                    const SdfPath& inputPath, const VtValue& value, QString& error);
    bool applyOverride(RenderContext& context, const SdfPath& inputPath, const VtValue& value);
    void applyOverrides(RenderContext& context);
    void clearContextOverrides(RenderContext& context);

    static QString materialCacheDirectory();
    static QString extractResource(const QString& resourcePath, const QString& relativePath);
    static UsdStageRefPtr createPreviewStage(QString& error, const SdfLayerRefPtr& sourceLayer = {});
    static bool createFallbackMaterial(const UsdStageRefPtr& stage);
    static bool updatePreviewMaterial(const UsdStageRefPtr& stage, const MaterialParameters& parameters);
    static bool bindPreviewMaterial(const UsdStageRefPtr& stage, const SdfPath& materialPath);

public:
    struct Pending {
        RequestKind kind = RequestKind::Parameters;
        MaterialParameters parameters;
        SdfLayerRefPtr sourceLayer;
        SdfPath inputPath;
        VtValue value;
        quint64 serial = 0;
        bool forceRender = false;
    };

    struct RenderContext {
        UsdStageRefPtr stage;
        SdfLayerRefPtr sourceLayer;
        std::unique_ptr<RenderEngine> renderEngine;
        GfVec2i size { 0, 0 };
    };

    struct Data {
        QPointer<MaterialRenderer> renderer;

        // Keep two Hydra/Storm contexts warm. Interactive parameter previews
        // must never evict the real MaterialX network renderer and vice versa.
        RenderContext parameterContext;
        RenderContext interactiveNetworkContext;
        RenderContext networkContext;

        QHash<QString, QImage> cache;
        QHash<QString, Pending> pending;
        QSet<QString> active;
        QHash<QString, VtValue> attributeOverrides;

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

    const QFileInfo existing(filename);
    if (existing.exists() && existing.size() == source.size())
        return filename;

    QFile target(filename);
    if (!target.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return {};

    target.write(source.readAll());
    return filename;
}

bool
MaterialRendererPrivate::createFallbackMaterial(const UsdStageRefPtr& stage)
{
    if (!stage)
        return false;

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
    return bindPreviewMaterial(stage, materialPath);
}

UsdStageRefPtr
MaterialRendererPrivate::createPreviewStage(QString& error, const SdfLayerRefPtr& sourceLayer)
{
    const UsdStageRefPtr stage = UsdStage::CreateInMemory("stageviz_material_preview.usda");
    if (!stage) {
        error = QStringLiteral("Could not create material preview stage");
        return {};
    }

    // Compose only the material-network snapshot. Preview geometry, UVs,
    // lighting and camera are generated directly below, so there is no external
    // shaderball .usda to extract, parse or open.
    if (sourceLayer)
        stage->GetRootLayer()->GetSubLayerPaths().push_back(sourceLayer->GetIdentifier());

    UsdGeomSetStageUpAxis(stage, UsdGeomTokens->z);
    UsdGeomSetStageMetersPerUnit(stage, 0.001);

    // Generate a simple UV sphere as a subdivided UsdGeomMesh. The topology is
    // intentionally lightweight; Catmull-Clark gives us the smooth preview while
    // face-varying st coordinates preserve a clean 0..1 seam for texture nodes.
    constexpr int longitudeSegments = 48;
    constexpr int latitudeSegments = 32;
    constexpr float sphereRadius = 100.0f;
    constexpr double pi = 3.14159265358979323846;

    VtArray<GfVec3f> points;
    points.reserve(2 + (latitudeSegments - 1) * longitudeSegments);
    points.push_back(GfVec3f(0.0f, 0.0f, sphereRadius));

    for (int lat = 1; lat < latitudeSegments; ++lat) {
        const double v = static_cast<double>(lat) / latitudeSegments;
        const double theta = v * pi;
        const float z = sphereRadius * static_cast<float>(std::cos(theta));
        const float ringRadius = sphereRadius * static_cast<float>(std::sin(theta));
        for (int lon = 0; lon < longitudeSegments; ++lon) {
            const double u = static_cast<double>(lon) / longitudeSegments;
            const double phi = u * 2.0 * pi;
            points.push_back(GfVec3f(ringRadius * static_cast<float>(std::cos(phi)),
                                     ringRadius * static_cast<float>(std::sin(phi)), z));
        }
    }
    const int southPole = static_cast<int>(points.size());
    points.push_back(GfVec3f(0.0f, 0.0f, -sphereRadius));

    auto ringIndex = [](int lat, int lon) { return 1 + (lat - 1) * longitudeSegments + (lon % longitudeSegments); };

    VtArray<int> faceVertexCounts;
    VtArray<int> faceVertexIndices;
    VtArray<GfVec2f> st;
    faceVertexCounts.reserve(longitudeSegments * latitudeSegments);
    faceVertexIndices.reserve(longitudeSegments * latitudeSegments * 4);
    st.reserve(longitudeSegments * latitudeSegments * 4);

    // North cap.
    for (int lon = 0; lon < longitudeSegments; ++lon) {
        const int next = (lon + 1) % longitudeSegments;
        const float u0 = static_cast<float>(lon) / longitudeSegments;
        const float u1 = static_cast<float>(lon + 1) / longitudeSegments;
        faceVertexCounts.push_back(3);
        faceVertexIndices.push_back(0);
        faceVertexIndices.push_back(ringIndex(1, lon));
        faceVertexIndices.push_back(ringIndex(1, next));
        st.push_back(GfVec2f((u0 + u1) * 0.5f, 1.0f));
        st.push_back(GfVec2f(u0, 1.0f - 1.0f / latitudeSegments));
        st.push_back(GfVec2f(u1, 1.0f - 1.0f / latitudeSegments));
    }

    // Middle quads.
    for (int lat = 1; lat < latitudeSegments - 1; ++lat) {
        const float v0 = 1.0f - static_cast<float>(lat) / latitudeSegments;
        const float v1 = 1.0f - static_cast<float>(lat + 1) / latitudeSegments;
        for (int lon = 0; lon < longitudeSegments; ++lon) {
            const int next = (lon + 1) % longitudeSegments;
            const float u0 = static_cast<float>(lon) / longitudeSegments;
            const float u1 = static_cast<float>(lon + 1) / longitudeSegments;
            faceVertexCounts.push_back(4);
            faceVertexIndices.push_back(ringIndex(lat, lon));
            faceVertexIndices.push_back(ringIndex(lat + 1, lon));
            faceVertexIndices.push_back(ringIndex(lat + 1, next));
            faceVertexIndices.push_back(ringIndex(lat, next));
            st.push_back(GfVec2f(u0, v0));
            st.push_back(GfVec2f(u0, v1));
            st.push_back(GfVec2f(u1, v1));
            st.push_back(GfVec2f(u1, v0));
        }
    }

    // South cap.
    const int lastRing = latitudeSegments - 1;
    for (int lon = 0; lon < longitudeSegments; ++lon) {
        const int next = (lon + 1) % longitudeSegments;
        const float u0 = static_cast<float>(lon) / longitudeSegments;
        const float u1 = static_cast<float>(lon + 1) / longitudeSegments;
        faceVertexCounts.push_back(3);
        faceVertexIndices.push_back(ringIndex(lastRing, lon));
        faceVertexIndices.push_back(southPole);
        faceVertexIndices.push_back(ringIndex(lastRing, next));
        st.push_back(GfVec2f(u0, 1.0f / latitudeSegments));
        st.push_back(GfVec2f((u0 + u1) * 0.5f, 0.0f));
        st.push_back(GfVec2f(u1, 1.0f / latitudeSegments));
    }

    const SdfPath previewPath("/Stageviz/Preview/Sphere");
    UsdGeomMesh sphere = UsdGeomMesh::Define(stage, previewPath);
    sphere.CreatePointsAttr().Set(points);
    sphere.CreateFaceVertexCountsAttr().Set(faceVertexCounts);
    sphere.CreateFaceVertexIndicesAttr().Set(faceVertexIndices);
    sphere.CreateSubdivisionSchemeAttr().Set(UsdGeomTokens->catmullClark);
    sphere.CreateDoubleSidedAttr().Set(false);

    UsdGeomPrimvarsAPI primvars(sphere.GetPrim());
    UsdGeomPrimvar stPrimvar = primvars.CreatePrimvar(TfToken("st"), SdfValueTypeNames->TexCoord2fArray,
                                                      UsdGeomTokens->faceVarying);
    stPrimvar.Set(st);

    if (!sourceLayer && !createFallbackMaterial(stage)) {
        error = QStringLiteral("Could not create fallback material preview shader");
        return {};
    }

    // Lighting is provided entirely by RenderEngine's default dome/HDRI path.
    // Keeping the preview stage free of authored lights makes the swatch neutral,
    // avoids a second specular hotspot, and matches the main Stageviz viewport
    // environment-lighting path. The dome itself remains camera-invisible.

    // Straight, quiet framing with deliberate margin around the sphere. With a
    // 55 mm lens and 20.955 mm aperture, 4.4 radii is too close and clips the
    // sphere. 6.4 radii leaves roughly 15 percent breathing room.
    const double radius = sphereRadius;
    const GfVec3d eye(0.0, -radius * 6.4, radius * 0.10);

    UsdGeomCamera camera = UsdGeomCamera::Define(stage, SdfPath("/Stageviz/Camera"));
    camera.CreateFocalLengthAttr(VtValue(55.0f));
    camera.CreateHorizontalApertureAttr(VtValue(20.955f));
    camera.CreateVerticalApertureAttr(VtValue(20.955f));

    const GfVec3d target(0.0, 0.0, 0.0);
    GfMatrix4d view(1.0);
    view.SetLookAt(eye, target, GfVec3d(0.0, 0.0, 1.0));
    camera.MakeMatrixXform().Set(view.GetInverse());

    return stage;
}

bool
MaterialRendererPrivate::bindPreviewMaterial(const UsdStageRefPtr& stage, const SdfPath& materialPath)
{
    if (!stage || materialPath.IsEmpty())
        return false;

    const UsdPrim materialPrim = stage->GetPrimAtPath(materialPath);
    const UsdPrim previewPrim = stage->GetPrimAtPath(SdfPath("/Stageviz/Preview/Sphere"));
    if (!materialPrim || !materialPrim.IsA<UsdShadeMaterial>() || !previewPrim)
        return false;

    UsdShadeMaterialBindingAPI::Apply(previewPrim).Bind(UsdShadeMaterial(materialPrim));
    return true;
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
    // Keep all rendering on MaterialRenderer's GUI thread so Windows creates
    // and destroys the platform surface on the correct thread.
}

bool
MaterialRendererPrivate::initializeContext(RenderContext& context, QString& error, const SdfLayerRefPtr& sourceLayer,
                                           const GfVec2i& size)
{
    if (!qApp || QThread::currentThread() != qApp->thread()) {
        error = QStringLiteral("Material preview renderer must be created on the GUI thread");
        return false;
    }

    resetContext(context);

    context.sourceLayer = sourceLayer;
    context.stage = createPreviewStage(error, sourceLayer);
    if (!context.stage)
        return false;

    RenderEngine::Settings settings;
    settings.clearColor = style()->color(Style::ColorRole::Render);
    settings.sceneMaterialsEnabled = true;
    settings.sceneLightsEnabled = true;
    settings.defaultCameraLightEnabled = false;
    settings.defaultDomeLightEnabled = true;
    settings.domeLightTexture.clear();  // Empty = OpenUSD/Stageviz default dome HDRI.
    settings.domeLightCameraVisibility = false;

    context.renderEngine = std::make_unique<RenderEngine>(RenderEngine::ContextMode::Offscreen);
    context.renderEngine->setStage(context.stage);
    context.renderEngine->setSettings(settings);
    context.renderEngine->setSize(size);
    context.size = size;

    // Render only the generated preview hierarchy. The flattened source layer is
    // composed only so its materials/shaders can be resolved; none of its scene
    // geometry should ever appear in a swatch.
    context.renderEngine->setMask({ SdfPath("/Stageviz/Preview") });

    const UsdGeomCamera camera(context.stage->GetPrimAtPath(SdfPath("/Stageviz/Camera")));
    if (!camera) {
        error = QStringLiteral("Material preview camera is missing");
        resetContext(context);
        return false;
    }

    context.renderEngine->setCamera(camera.GetCamera(UsdTimeCode::Default()));
    return true;
}

bool
MaterialRendererPrivate::ensureParameterRenderer(QString& error)
{
    constexpr int previewSize = 384;
    if (d.parameterContext.renderEngine && d.parameterContext.stage) {
        return true;
    }


    return initializeContext(d.parameterContext, error, {}, GfVec2i(previewSize, previewSize));
}

bool
MaterialRendererPrivate::ensureNetworkRenderer(QString& error, const SdfLayerRefPtr& sourceLayer)
{
    constexpr int finalSize = 512;

    // A swatch snapshot is immutable for one structural generation. Reuse the
    // warm Storm context for every material that references the same snapshot.
    // MaterialDialog replaces the snapshot layer object when topology changes,
    // so pointer identity is the safe invalidation boundary.
    if (d.networkContext.renderEngine && d.networkContext.stage && d.networkContext.sourceLayer == sourceLayer) {
        return true;
    }

    const bool ok = initializeContext(d.networkContext, error, sourceLayer, GfVec2i(finalSize, finalSize));
    return ok;
}

bool
MaterialRendererPrivate::ensureInteractiveNetworkRenderer(QString& error, const SdfLayerRefPtr& sourceLayer)
{
    constexpr int previewSize = 384;
    if (d.interactiveNetworkContext.renderEngine && d.interactiveNetworkContext.stage
        && d.interactiveNetworkContext.sourceLayer == sourceLayer) {
        return true;
    }


    if (!initializeContext(d.interactiveNetworkContext, error, sourceLayer, GfVec2i(previewSize, previewSize))) {
        return false;
    }
    applyOverrides(d.interactiveNetworkContext);
    return true;
}

bool
MaterialRendererPrivate::applyOverride(RenderContext& context, const SdfPath& inputPath, const VtValue& value)
{
    if (!context.stage || inputPath.IsEmpty() || !inputPath.IsPropertyPath() || value.IsEmpty())
        return false;

    UsdAttribute attribute = context.stage->GetAttributeAtPath(inputPath);

    // MaterialTree exposes the complete MaterialX NodeDef interface, including
    // inputs that have not been authored into USD yet. Interactive swatch edits
    // must still work for those slots. Materialize the declared input only in
    // the preview stage, then author the transient override there. The real stage
    // remains lazy and is authored by the normal command path on commit.
    if (!attribute) {
        const SdfPath primPath = inputPath.GetPrimPath();
        const MaterialNodeInfo node = MaterialUtils::nodeInfo(context.stage, primPath);

        const MaterialInputInfo* declared = nullptr;
        for (const MaterialInputInfo& input : node.inputs) {
            if (input.inputPath == inputPath) {
                declared = &input;
                break;
            }
        }

        if (!declared || declared->typeName.GetAsToken().IsEmpty())
            return false;

        UsdShadeShader shader(context.stage->GetPrimAtPath(primPath));
        if (!shader)
            return false;

        const UsdShadeInput input = shader.CreateInput(declared->inputName, declared->typeName);
        if (!input)
            return false;
        attribute = input.GetAttr();
    }

    return attribute && attribute.Set(value);
}

void
MaterialRendererPrivate::applyOverrides(RenderContext& context)
{
    if (!context.stage)
        return;

    for (auto it = d.attributeOverrides.cbegin(); it != d.attributeOverrides.cend(); ++it)
        applyOverride(context, SdfPath(it.key().toStdString()), it.value());
}

void
MaterialRendererPrivate::clearContextOverrides(RenderContext& context)
{
    if (!context.stage)
        return;

    for (auto it = d.attributeOverrides.cbegin(); it != d.attributeOverrides.cend(); ++it) {
        const UsdAttribute attribute = context.stage->GetAttributeAtPath(SdfPath(it.key().toStdString()));
        if (attribute)
            attribute.Clear();
    }
}

QImage
MaterialRendererPrivate::renderParameters(const MaterialParameters& parameters, QString& error)
{
    error.clear();

    if (!ensureParameterRenderer(error))
        return {};

    RenderContext& context = d.parameterContext;
    if (!updatePreviewMaterial(context.stage, parameters)) {
        error = QStringLiteral("Could not update material preview shader");
        return {};
    }

    // The stage remains attached to the same RenderEngine for the lifetime of
    // this context. USD notices propagate the authored parameter changes to Hydra.
    QImage image = context.renderEngine->renderImage();
    if (image.isNull())
        error = QStringLiteral("Material preview render failed");
    return image;
}

namespace {

    void dumpMaterialNetwork(const UsdStageRefPtr& stage, const SdfPath& materialPath)
    {
        if (!stage || materialPath.IsEmpty())
            return;


        const UsdPrim materialPrim = stage->GetPrimAtPath(materialPath);
        if (!materialPrim) {
            return;
        }

        for (const UsdPrim& prim : UsdPrimRange(materialPrim)) {
            if (!prim.IsA<UsdShadeShader>())
                continue;

            const UsdShadeShader shader(prim);
            TfToken shaderId;
            shader.GetIdAttr().Get(&shaderId);


            for (const UsdShadeInput& input : shader.GetInputs()) {
                UsdShadeConnectableAPI source;
                TfToken sourceName;
                UsdShadeAttributeType sourceType = UsdShadeAttributeType::Output;
                if (input.GetConnectedSource(&source, &sourceName, &sourceType)) {}
            }

            for (const UsdShadeOutput& output : shader.GetOutputs()) {}
        }
    }

}  // namespace

QImage
MaterialRendererPrivate::renderNetwork(const SdfPath& materialPath, const SdfLayerRefPtr& sourceLayer, QString& error)
{
    error.clear();


    if (!sourceLayer) {
        error = QStringLiteral("Material preview source layer is missing");
        return {};
    }

    if (!ensureNetworkRenderer(error, sourceLayer))
        return {};

    RenderContext& context = d.networkContext;
    applyOverrides(context);
    if (!bindPreviewMaterial(context.stage, materialPath)) {
        error = QStringLiteral("Could not bind the material network to the preview shaderball");
        return {};
    }

    dumpMaterialNetwork(context.stage, materialPath);

    QImage image = context.renderEngine->renderImage();
    if (image.isNull())
        error = QStringLiteral("Material network preview render failed");
    return image;
}

QImage
MaterialRendererPrivate::renderInteractiveNetwork(const SdfPath& materialPath, const SdfLayerRefPtr& sourceLayer,
                                                  const SdfPath& inputPath, const VtValue& value, QString& error)
{
    error.clear();
    if (!sourceLayer) {
        error = QStringLiteral("Material preview source layer is missing");
        return {};
    }

    // Reuse the already-warm full network context for interaction. Keeping a
    // second Storm context caused the first slider drag to pay another complete
    // shader/resource warm-up (~2 s in Debug) even though the same material
    // network had just been rendered. Apply the preview value transiently, render,
    // then restore the committed value so interactive edits cannot contaminate the
    // final swatch context.
    if (!ensureNetworkRenderer(error, sourceLayer))
        return {};

    RenderContext& context = d.networkContext;
    applyOverrides(context);
    if (!bindPreviewMaterial(context.stage, materialPath)) {
        error = QStringLiteral("Could not bind the material network to the interactive preview shaderball");
        return {};
    }

    UsdAttribute attribute = context.stage ? context.stage->GetAttributeAtPath(inputPath) : UsdAttribute();
    bool hadValue = false;
    VtValue previousValue;
    if (attribute)
        hadValue = attribute.Get(&previousValue) && !previousValue.IsEmpty();

    if (!applyOverride(context, inputPath, value)) {
        error = QStringLiteral("Could not apply interactive material override");
        return {};
    }

    QImage image = context.renderEngine->renderImage();
    attribute = context.stage ? context.stage->GetAttributeAtPath(inputPath) : UsdAttribute();
    if (attribute) {
        if (hadValue)
            attribute.Set(previousValue);
        else
            attribute.Clear();
    }

    if (image.isNull())
        error = QStringLiteral("Interactive material network preview render failed");
    return image;
}

void
MaterialRendererPrivate::resetContext(RenderContext& context)
{
    context.renderEngine.reset();
    context.stage = nullptr;
    context.sourceLayer = nullptr;
    context.size = GfVec2i(0, 0);
}

void
MaterialRendererPrivate::reset()
{
    resetContext(d.parameterContext);
    resetContext(d.interactiveNetworkContext);
    resetContext(d.networkContext);
    d.attributeOverrides.clear();
}

void
MaterialRendererPrivate::dispatchParameters(const QString& path, const MaterialParameters& parameters, bool forceRender)
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
    pending.kind = RequestKind::Parameters;
    pending.parameters = parameters;
    pending.serial = ++d.serial;
    pending.forceRender = forceRender;
    d.pending.insert(path, pending);

    if (d.active.contains(path))
        return;

    d.active.insert(path);
    QPointer<MaterialRenderer> renderer = d.renderer;
    QMetaObject::invokeMethod(
        d.renderer,
        [this, renderer, path]() {
            if (renderer)
                process(path);
        },
        Qt::QueuedConnection);
}

void
MaterialRendererPrivate::dispatchNetwork(const QString& path, const SdfLayerRefPtr& sourceLayer, bool forceRender)
{
    if (!sourceLayer)
        return;

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
    pending.kind = RequestKind::MaterialNetwork;
    pending.sourceLayer = sourceLayer;
    pending.serial = ++d.serial;
    pending.forceRender = forceRender;
    d.pending.insert(path, pending);

    if (d.active.contains(path))
        return;

    d.active.insert(path);
    QPointer<MaterialRenderer> renderer = d.renderer;
    QMetaObject::invokeMethod(
        d.renderer,
        [this, renderer, path]() {
            if (renderer)
                process(path);
        },
        Qt::QueuedConnection);
}

void
MaterialRendererPrivate::dispatchInteractive(const QString& path, const SdfLayerRefPtr& sourceLayer,
                                             const SdfPath& inputPath, const VtValue& value)
{
    if (!sourceLayer || inputPath.IsEmpty() || value.IsEmpty())
        return;

    Pending pending;
    pending.kind = RequestKind::InteractiveNetwork;
    pending.sourceLayer = sourceLayer;
    pending.inputPath = inputPath;
    pending.value = value;
    pending.serial = ++d.serial;
    pending.forceRender = true;
    d.pending.insert(path, pending);

    const quint64 scheduledSerial = pending.serial;
    QPointer<MaterialRenderer> renderer = d.renderer;
    QTimer::singleShot(35, d.renderer.data(), [this, renderer, path, scheduledSerial]() {
        if (!renderer || d.active.contains(path))
            return;
        const auto it = d.pending.constFind(path);
        if (it == d.pending.cend() || it->serial != scheduledSerial || it->kind != RequestKind::InteractiveNetwork) {
            return;
        }
        d.active.insert(path);
        process(path);
    });
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
    QImage image;
    if (request.kind == RequestKind::MaterialNetwork)
        image = renderNetwork(SdfPath(path.toStdString()), request.sourceLayer, error);
    else if (request.kind == RequestKind::InteractiveNetwork)
        image = renderInteractiveNetwork(SdfPath(path.toStdString()), request.sourceLayer, request.inputPath,
                                         request.value, error);
    else
        image = renderParameters(request.parameters, error);

    receive(path, image, error, request.serial, request.kind);
}

void
MaterialRendererPrivate::receive(const QString& path, const QImage& image, const QString& error, quint64 serial,
                                 RequestKind kind)
{
    const auto pending = d.pending.constFind(path);
    const bool newerRequestExists = pending != d.pending.cend() && pending->serial > serial;

    if (!newerRequestExists) {
        if (!image.isNull()) {
            // Only full-quality real-network renders become reusable material
            // cache entries. The 384px parameter path is a transient interaction
            // preview and must never satisfy a later 512px network request.
            if (kind == RequestKind::MaterialNetwork)
                d.cache.insert(path, image);
            Q_EMIT d.renderer->rendered(path, image);
        }
        else {
            Q_EMIT d.renderer->error(path, error);
        }
    }

    if (d.pending.contains(path)) {
        QPointer<MaterialRenderer> renderer = d.renderer;
        QMetaObject::invokeMethod(
            d.renderer,
            [this, renderer, path]() {
                if (renderer)
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
    if (qApp && QThread::currentThread() != qApp->thread()) {
        QPointer<MaterialRenderer> renderer(this);
        QMetaObject::invokeMethod(
            this,
            [renderer, path, parameters, forceRender]() {
                if (renderer)
                    renderer->p->dispatchParameters(path, parameters, forceRender);
            },
            Qt::QueuedConnection);
        return;
    }

    p->dispatchParameters(path, parameters, forceRender);
}

void
MaterialRenderer::request(const SdfPath& materialPath, const SdfLayerRefPtr& sourceLayer, bool forceRender)
{
    if (materialPath.IsEmpty() || !sourceLayer)
        return;

    const QString path = QString::fromStdString(materialPath.GetString());
    if (qApp && QThread::currentThread() != qApp->thread()) {
        QPointer<MaterialRenderer> renderer(this);
        QMetaObject::invokeMethod(
            this,
            [renderer, path, sourceLayer, forceRender]() {
                if (renderer)
                    renderer->p->dispatchNetwork(path, sourceLayer, forceRender);
            },
            Qt::QueuedConnection);
        return;
    }

    p->dispatchNetwork(path, sourceLayer, forceRender);
}

void
MaterialRenderer::preview(const SdfPath& materialPath, const SdfLayerRefPtr& sourceLayer, const SdfPath& inputPath,
                          const VtValue& value)
{
    if (materialPath.IsEmpty() || !sourceLayer || inputPath.IsEmpty() || value.IsEmpty())
        return;

    const QString path = QString::fromStdString(materialPath.GetString());
    if (qApp && QThread::currentThread() != qApp->thread()) {
        QPointer<MaterialRenderer> renderer(this);
        QMetaObject::invokeMethod(
            this,
            [renderer, path, sourceLayer, inputPath, value]() {
                if (renderer)
                    renderer->p->dispatchInteractive(path, sourceLayer, inputPath, value);
            },
            Qt::QueuedConnection);
        return;
    }

    p->dispatchInteractive(path, sourceLayer, inputPath, value);
}

void
MaterialRenderer::syncAttribute(const SdfPath& inputPath, const VtValue& value)
{
    if (inputPath.IsEmpty() || value.IsEmpty())
        return;

    if (qApp && QThread::currentThread() != qApp->thread()) {
        QPointer<MaterialRenderer> renderer(this);
        QMetaObject::invokeMethod(
            this,
            [renderer, inputPath, value]() {
                if (renderer)
                    renderer->syncAttribute(inputPath, value);
            },
            Qt::QueuedConnection);
        return;
    }

    const QString key = QString::fromStdString(inputPath.GetString());
    p->d.attributeOverrides.insert(key, value);
    p->applyOverride(p->d.interactiveNetworkContext, inputPath, value);
    p->applyOverride(p->d.networkContext, inputPath, value);
}

void
MaterialRenderer::clearOverrides()
{
    if (qApp && QThread::currentThread() != qApp->thread()) {
        QPointer<MaterialRenderer> renderer(this);
        QMetaObject::invokeMethod(
            this,
            [renderer]() {
                if (renderer)
                    renderer->clearOverrides();
            },
            Qt::QueuedConnection);
        return;
    }

    p->clearContextOverrides(p->d.interactiveNetworkContext);
    p->clearContextOverrides(p->d.networkContext);
    p->d.attributeOverrides.clear();
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
    p->reset();
}

}  // namespace stageviz
