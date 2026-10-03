// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#pragma once

#include "viewstate.h"
#include <memory>
#include <ostream>
#include <pxr/base/gf/matrix4d.h>
#include <pxr/base/gf/matrix4f.h>
#include <pxr/base/gf/vec2i.h>
#include <pxr/base/gf/vec4d.h>
#include <pxr/base/gf/vec4f.h>
#include <pxr/base/tf/token.h>
#include <pxr/imaging/hd/rprimCollection.h>
#include <pxr/imaging/hd/tokens.h>
#include <pxr/imaging/hdx/task.h>
#include <pxr/usd/sdf/path.h>
#include <vector>

PXR_NAMESPACE_USING_DIRECTIVE

namespace stageviz {

/**
 * @brief Settings for the shared single-sample scene ID pass.
 *
 * Renders primId + depth once into dedicated non-MSAA Storm buffers.
 * The same pass feeds selection outlines and visible-prim capture, avoiding a
 * second selected-geometry render and avoiding integer MSAA resolve targets on
 * Metal. Back-face culling is disabled deliberately so reversed single-sided
 * meshes still receive a usable primId.
 */
struct SceneIdSettings {
    bool enabled = false;
    SdfPathVector roots;
    TfTokenVector renderTags;
    HdReprSelector reprSelector = HdReprSelector(HdReprTokens->smoothHull);
    GfVec2i size = GfVec2i(0);
    GfVec4d viewport = GfVec4d(0.0);
    GfMatrix4d viewMatrix = GfMatrix4d(1.0);
    GfMatrix4d projectionMatrix = GfMatrix4d(1.0);
};

/**
 * @brief Settings for the screen-space selection outline.
 *
 * Selected USD paths are resolved to Storm HdRprim IDs. The outline shader
 * tests the shared scene primId image against a compact GPU lookup texture,
 * then edge-detects the resulting selected-pixel mask. Selection changes no
 * longer require a second geometry pass.
 */
struct SelectionOutlineSettings {
    bool enabled = false;
    SdfPathVector paths;
    GfVec4f color = GfVec4f(1.0f, 0.82f, 0.0f, 1.0f);
    unsigned int radius = 3;
};

/**
 * @brief Parameters for the Stageviz post-process render task.
 *
 * The task owns Stageviz screen-space effects and the shared scene-ID pass used
 * by selection/capture. Ambient occlusion consumes the beauty depth AOV;
 * selection consumes the scene primId AOV plus a selected-ID lookup texture.
 */
struct RenderTaskParams {
    ViewState::AmbientOcclusionSettings ambientOcclusion;
    SceneIdSettings sceneIds;
    SelectionOutlineSettings selectionOutline;
    bool captureVisible = false;

    // Projection matrices for the thorough visible-capture scan. Each matrix
    // represents one narrowed camera tile rendered into the same full-size
    // single-sample primId/depth buffers. An empty list falls back to the
    // already-rendered viewport ID image.
    std::vector<GfMatrix4d> captureProjectionMatrices;
    float nearClip = 0.1f;
    float farClip = 10000.0f;
    bool orthographic = false;
    GfMatrix4f projectionMatrix = GfMatrix4f(1.0f);
    TfToken shaderPath;
    TfToken sceneIdShaderPath;

    bool enabled() const;
};
bool
operator==(const RenderTaskParams& lhs, const RenderTaskParams& rhs);
bool
operator!=(const RenderTaskParams& lhs, const RenderTaskParams& rhs);
std::ostream&
operator<<(std::ostream& out, const RenderTaskParams& params);

/**
 * @brief Hydra/Hgi post-process task used by the Stageviz viewport.
 *
 * The task is backend-independent. Storm renders one dedicated single-sample
 * primId/depth layer; HdxFullscreenShader/Hgi performs AO and outline
 * composition. Visible capture starts with the already-rendered primId image
 * and can then sweep narrowed camera tiles through the same fixed-size ID
 * buffers to detect sub-pixel distant geometry without deep-pick recursion.
 */
class RenderTask final : public HdxTask {
public:
    RenderTask(HdSceneDelegate* delegate, const SdfPath& id);
    ~RenderTask() override;

    void Prepare(HdTaskContext* ctx, HdRenderIndex* renderIndex) override;
    void Execute(HdTaskContext* ctx) override;

    /**
     * @brief Returns and clears the paths captured by the most recent capture request.
     */
    SdfPathVector takeCapturedVisiblePaths();

    static const TfToken& token();

protected:
    void _Sync(HdSceneDelegate* delegate, HdTaskContext* ctx, HdDirtyBits* dirtyBits) override;

private:
    class Shader;
    class SceneIdPass;

    RenderTaskParams m_params;
    std::unique_ptr<Shader> m_shader;
    std::unique_ptr<SceneIdPass> m_sceneIdPass;
    SdfPathVector m_capturedVisiblePaths;
};

}  // namespace stageviz
