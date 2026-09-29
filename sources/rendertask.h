// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#pragma once

#include "viewstate.h"
#include <memory>
#include <ostream>
#include <pxr/base/gf/matrix4f.h>
#include <pxr/base/tf/token.h>
#include <pxr/imaging/hdx/task.h>
#include <pxr/usd/sdf/path.h>

PXR_NAMESPACE_USING_DIRECTIVE

namespace stageviz {

/**
 * @brief Parameters for the Stageviz post-process render task.
 *
 * The task consumes Storm AOVs and runs Stageviz-specific full-screen render
 * effects. Ambient occlusion currently uses raw AO, depth-aware separable blur,
 * and final composition into the color AOV.
 */
struct RenderTaskParams {
    ViewState::AmbientOcclusionSettings ambientOcclusion;
    float nearClip = 0.1f;
    float farClip = 10000.0f;
    bool orthographic = false;
    GfMatrix4f projectionMatrix = GfMatrix4f(1.0f);
    TfToken shaderPath;

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
 * The implementation is backend-independent and uses HdxFullscreenShader,
 * so Hgi provides the Metal/OpenGL backend. Individual effects are implemented
 * as techniques in the shared Stageviz Render.glslfx resource. Ambient
 * occlusion is the first effect; additional look/inspection passes can be added
 * without introducing a separate Hydra task for each one.
 */
class RenderTask final : public HdxTask {
public:
    RenderTask(HdSceneDelegate* delegate, const SdfPath& id);
    ~RenderTask() override;

    void Prepare(HdTaskContext* ctx, HdRenderIndex* renderIndex) override;
    void Execute(HdTaskContext* ctx) override;

    static const TfToken& token();

protected:
    void _Sync(HdSceneDelegate* delegate, HdTaskContext* ctx, HdDirtyBits* dirtyBits) override;

private:
    class Shader;

    RenderTaskParams m_params;
    std::unique_ptr<Shader> m_shader;
};

}  // namespace stageviz
