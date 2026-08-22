// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#ifndef STAGEVIZ_RENDERENGINE_H
#define STAGEVIZ_RENDERENGINE_H

#include <QColor>
#include <QImage>
#include <QList>
#include <QString>
#include <memory>
#include <pxr/base/gf/bbox3d.h>
#include <pxr/base/gf/camera.h>
#include <pxr/base/gf/frustum.h>
#include <pxr/base/gf/vec2i.h>
#include <pxr/base/gf/vec4d.h>
#include <pxr/base/gf/vec4f.h>
#include <pxr/base/tf/token.h>
#include <pxr/base/vt/dictionary.h>
#include <pxr/usd/sdf/path.h>
#include <pxr/usd/usd/stage.h>
#include <pxr/usdImaging/usdImagingGL/engine.h>
#include <vector>

PXR_NAMESPACE_USING_DIRECTIVE

namespace stageviz {

/**
 * @brief Reusable Hydra renderer for Stageviz viewports and scripted rendering.
 *
 * RenderEngine owns the Stageviz-specific UsdImagingGLEngine configuration and
 * hides Hydra scene-index setup from callers. It can render into an already
 * current OpenGL context, as used by ImagingGLWidget, or create an offscreen
 * context for scripted image rendering.
 */
class RenderEngine final {
public:
    /**
     * @brief Selects how the renderer obtains an OpenGL context.
     */
    enum class ContextMode { Current, Offscreen };

    /**
     * @brief Selects the material presentation applied by Stageviz.
     */
    enum class MaterialMode { Scene, Clay, Override };

    /**
     * @brief Selects the rasterized sidedness behavior.
     */
    enum class DoubleSidedMode { Primitive, SingleSided, DoubleSided };

    /**
     * @brief Rendering options independent of the Stageviz user interface.
     */
    struct Settings {
        QColor clearColor = Qt::black;
        TfToken aov = TfToken("color");
        UsdImagingGLDrawMode drawMode = UsdImagingGLDrawMode::DRAW_SHADED_SMOOTH;
        DoubleSidedMode doubleSidedMode = DoubleSidedMode::DoubleSided;
        MaterialMode materialMode = MaterialMode::Scene;
        SdfPath overrideMaterial;
        double complexity = 1.0;
        bool sceneLightsEnabled = true;
        bool sceneMaterialsEnabled = true;
        bool defaultCameraLightEnabled = true;
        bool gammaCorrectColors = true;
        bool sampleAlphaToCoverageEnabled = true;
        bool flipFrontFacing = true;
        bool showGuides = false;
        bool showProxy = true;
        bool showRender = true;
        float defaultAmbient = 0.4f;
        float defaultSpecular = 0.5f;
        float defaultShininess = 32.0f;
    };

    /**
     * @brief Creates a render engine.
     * @param mode OpenGL context mode used by the renderer.
     */
    explicit RenderEngine(ContextMode mode = ContextMode::Current);

    /**
     * @brief Destroys the render engine and its owned graphics resources.
     */
    ~RenderEngine();

    RenderEngine(const RenderEngine&) = delete;
    RenderEngine& operator=(const RenderEngine&) = delete;

    /**
     * @brief Initializes Hydra and the required OpenGL resources.
     * @return True if the renderer is ready for rendering.
     *
     * Current mode requires a current QOpenGLContext. Offscreen mode creates
     * and makes current an internal context before Hydra is initialized.
     */
    bool initialize();

    /**
     * @brief Releases Hydra and owned OpenGL resources.
     */
    void reset();

    /**
     * @brief Returns whether the Hydra renderer has been initialized.
     */
    bool isInitialized() const;

    /**
     * @brief Sets the primary USD stage.
     * @param stage Stage rendered by Hydra.
     */
    void setStage(UsdStageRefPtr stage);

    /**
     * @brief Returns the primary USD stage.
     */
    UsdStageRefPtr stage() const;

    /**
     * @brief Sets an optional auxiliary stage merged into the render scene.
     * @param stage Auxiliary stage, or null to disable it.
     */
    void setAuxiliaryStage(UsdStageRefPtr stage);

    /**
     * @brief Returns the auxiliary USD stage.
     */
    UsdStageRefPtr auxiliaryStage() const;

    /**
     * @brief Flushes pending edits from the auxiliary USD scene-index chain.
     */
    void refreshAuxiliaryStage();

    /**
     * @brief Sets the camera used for rendering and intersection tests.
     * @param camera Camera state.
     */
    void setCamera(const GfCamera& camera);

    /**
     * @brief Returns the current rendering camera.
     */
    const GfCamera& camera() const;

    /**
     * @brief Sets the render resolution in device pixels.
     * @param size Width and height in pixels.
     */
    void setSize(const GfVec2i& size);

    /**
     * @brief Returns the render resolution in device pixels.
     */
    const GfVec2i& size() const;

    /**
     * @brief Sets the render viewport inside the current framebuffer.
     *
     * The value is expressed as x, y, width, height in device pixels. A
     * non-positive width or height selects the complete render buffer.
     *
     * @param viewport Render viewport in device pixels.
     */
    void setViewport(const GfVec4d& viewport);

    /**
     * @brief Returns the configured render viewport.
     */
    const GfVec4d& viewport() const;

    /**
     * @brief Sets render settings.
     * @param settings Render settings to apply.
     */
    void setSettings(const Settings& settings);

    /**
     * @brief Returns the current render settings.
     */
    const Settings& settings() const;

    /**
     * @brief Limits rendering to the supplied USD paths.
     * @param paths Root paths to render. An empty list renders the whole stage.
     */
    void setMask(const QList<SdfPath>& paths);

    /**
     * @brief Sets selected prim paths used by Hydra selection highlighting.
     * @param paths Selected prim paths.
     */
    void setSelected(const QList<SdfPath>& paths);

    /**
     * @brief Sets selection bounding boxes rendered by Hydra.
     * @param bboxes World-space selection bounds.
     */
    void setSelectionBBoxes(const std::vector<GfBBox3d>& bboxes);

    /**
     * @brief Sets the selection highlight color.
     * @param color Selection color.
     */
    void setSelectionColor(const QColor& color);

    /**
     * @brief Renders into the framebuffer of the current OpenGL context.
     * @return True if a render pass was submitted.
     *
     * This is the path used by ImagingGLWidget. The caller is responsible for
     * making its OpenGL context and target framebuffer current before calling.
     */
    bool renderToCurrentFramebuffer();

    /**
     * @brief Renders the current stage into an offscreen image.
     * @return Rendered image, or a null image if rendering failed.
     *
     * The engine must have been created with ContextMode::Offscreen. The
     * previously current OpenGL context and surface are restored before this
     * function returns, including on failure paths.
     */
    QImage renderImage();

    /**
     * @brief Tests the nearest intersection for a root prim.
     */
    bool testIntersection(const GfMatrix4d& viewMatrix, const GfMatrix4d& projectionMatrix, const UsdPrim& root,
                          GfVec3d* hitPoint, GfVec3d* hitNormal, SdfPath* hitPrimPath, SdfPath* hitInstancerPath);

    /**
     * @brief Tests intersections using Hydra pick parameters for a root prim.
     */
    bool testIntersection(const UsdImagingGLEngine::PickParams& pickParams, const GfMatrix4d& viewMatrix,
                          const GfMatrix4d& projectionMatrix, const UsdPrim& root,
                          UsdImagingGLEngine::IntersectionResultVector* results);

    /**
     * @brief Returns renderer AOV names supported by the active delegate.
     */
    QList<QString> rendererAovs() const;

    /**
     * @brief Returns renderer statistics from Hydra.
     */
    VtDictionary renderStats() const;

    /**
     * @brief Returns the active Hgi API name.
     */
    QString hgiApiName() const;

    /**
     * @brief Returns whether Hydra supports its own color correction path.
     */
    bool isColorCorrectionCapable() const;

private:
    class Private;
    std::unique_ptr<Private> p;
};

}  // namespace stageviz

#endif  // STAGEVIZ_RENDERENGINE_H
