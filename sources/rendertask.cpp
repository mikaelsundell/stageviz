// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#include "rendertask.h"

#include <algorithm>
#include <cmath>
#include <pxr/base/gf/vec2i.h>
#include <pxr/base/gf/vec3i.h>
#include <pxr/base/gf/vec4f.h>
#include <pxr/imaging/hd/changeTracker.h>
#include <pxr/imaging/hd/tokens.h>
#include <pxr/imaging/hdx/fullscreenShader.h>
#include <pxr/imaging/hgi/hgi.h>
#include <pxr/imaging/hgi/shaderFunctionDesc.h>
#include <pxr/imaging/hgi/texture.h>

PXR_NAMESPACE_USING_DIRECTIVE

namespace stageviz {

namespace {

constexpr HgiFormat kAOFormat = HgiFormatFloat16Vec4;
constexpr int kSpiralTurnCount = 7;

int
sampleCount(ViewState::AmbientOcclusionQuality quality)
{
    switch (quality) {
    case ViewState::AmbientOcclusionLow: return 8;
    case ViewState::AmbientOcclusionMedium: return 16;
    case ViewState::AmbientOcclusionUltra: return 32;
    case ViewState::AmbientOcclusionHigh:
    default: return 24;
    }
}

struct RawConstants {
    GfVec4f clipInfo = GfVec4f(0.0f);
    GfVec4f projInfo = GfVec4f(0.0f);
    GfVec2i screenSize = GfVec2i(1);
    float contactAmount = 1.0f;
    float contactRadius = 8.0f;
    float broadAmount = 0.4f;
    float broadRadius = 48.0f;
    float normalBias = 0.04f;
    float falloff = 2.0f;
    float contrast = 0.5f;
    int sampleCount = 24;
    int spiralTurnCount = kSpiralTurnCount;
    int prefilterEnabled = 1;
    int orthographic = 0;
};

struct BlurConstants {
    GfVec2i screenSize = GfVec2i(1);
    GfVec2i offset = GfVec2i(1, 0);
    float edgeSharpness = 1.0f;
    float blurRadius = 8.0f;
    float padding0 = 0.0f;
    float padding1 = 0.0f;
};

struct CompositeConstants {
    GfVec2i screenSize = GfVec2i(1);
    int debugMode = 0;
    int padding0 = 0;
};

HgiShaderFunctionTextureDesc
textureDesc(const char* name, uint32_t bindIndex, HgiFormat format)
{
    HgiShaderFunctionTextureDesc desc;
    desc.nameInShader = name;
    desc.bindIndex = bindIndex;
    desc.dimensions = 2;
    desc.format = format;
    return desc;
}

HgiShaderFunctionDesc
baseFragmentDesc(const char* debugName)
{
    HgiShaderFunctionDesc desc;
    desc.debugName = debugName;
    desc.shaderStage = HgiShaderStageFragment;
    HgiShaderFunctionAddStageInput(&desc, "uvOut", "vec2");
    HgiShaderFunctionAddStageOutput(&desc, "hd_FragColor", "vec4");
    return desc;
}

HgiShaderFunctionDesc
rawDesc(HgiFormat depthFormat)
{
    HgiShaderFunctionDesc desc = baseFragmentDesc("Stageviz Ambient Occlusion Raw");
    desc.textures.push_back(textureDesc("depthIn", 0, depthFormat));

    HgiShaderFunctionAddConstantParam(&desc, "uClipInfo", "vec4");
    HgiShaderFunctionAddConstantParam(&desc, "uProjInfo", "vec4");
    HgiShaderFunctionAddConstantParam(&desc, "uScreenSize", "ivec2");
    HgiShaderFunctionAddConstantParam(&desc, "uContactAmount", "float");
    HgiShaderFunctionAddConstantParam(&desc, "uContactRadius", "float");
    HgiShaderFunctionAddConstantParam(&desc, "uBroadAmount", "float");
    HgiShaderFunctionAddConstantParam(&desc, "uBroadRadius", "float");
    HgiShaderFunctionAddConstantParam(&desc, "uNormalBias", "float");
    HgiShaderFunctionAddConstantParam(&desc, "uFalloff", "float");
    HgiShaderFunctionAddConstantParam(&desc, "uContrast", "float");
    HgiShaderFunctionAddConstantParam(&desc, "uSampleCount", "int");
    HgiShaderFunctionAddConstantParam(&desc, "uSpiralTurnCount", "int");
    HgiShaderFunctionAddConstantParam(&desc, "uIsPrefilterEnabled", "int");
    HgiShaderFunctionAddConstantParam(&desc, "uIsOrthographic", "int");
    return desc;
}

HgiShaderFunctionDesc
blurDesc()
{
    HgiShaderFunctionDesc desc = baseFragmentDesc("Stageviz Ambient Occlusion Blur");
    desc.textures.push_back(textureDesc("aoIn", 0, kAOFormat));

    HgiShaderFunctionAddConstantParam(&desc, "uScreenSize", "ivec2");
    HgiShaderFunctionAddConstantParam(&desc, "uOffset", "ivec2");
    HgiShaderFunctionAddConstantParam(&desc, "uEdgeSharpness", "float");
    HgiShaderFunctionAddConstantParam(&desc, "uBlurRadius", "float");
    HgiShaderFunctionAddConstantParam(&desc, "uPadding0", "float");
    HgiShaderFunctionAddConstantParam(&desc, "uPadding1", "float");
    return desc;
}

HgiShaderFunctionDesc
compositeDesc(HgiFormat colorFormat)
{
    HgiShaderFunctionDesc desc = baseFragmentDesc("Stageviz Ambient Occlusion Composite");
    desc.textures.push_back(textureDesc("colorIn", 0, colorFormat));
    desc.textures.push_back(textureDesc("aoIn", 1, kAOFormat));

    HgiShaderFunctionAddConstantParam(&desc, "uScreenSize", "ivec2");
    HgiShaderFunctionAddConstantParam(&desc, "uDebugMode", "int");
    HgiShaderFunctionAddConstantParam(&desc, "uPadding0", "int");
    return desc;
}

}  // namespace

class RenderTask::Shader {
public:
    explicit Shader(Hgi* value)
        : hgi(value)
        , raw(value, "Stageviz Ambient Occlusion Raw")
        , blur(value, "Stageviz Ambient Occlusion Blur")
        , composite(value, "Stageviz Ambient Occlusion Composite")
    {
        HgiDepthStencilState depthState;
        depthState.depthTestEnabled = false;
        depthState.depthWriteEnabled = false;
        raw.SetDepthState(depthState);
        blur.SetDepthState(depthState);
        composite.SetDepthState(depthState);
    }

    ~Shader()
    {
        destroyTextures();
    }

    void destroyTextures()
    {
        if (!hgi)
            return;
        if (aoA)
            hgi->DestroyTexture(&aoA);
        if (aoB)
            hgi->DestroyTexture(&aoB);
        size = GfVec2i(0);
    }

    bool ensureTextures(const GfVec2i& newSize)
    {
        if (!hgi || newSize[0] <= 0 || newSize[1] <= 0)
            return false;

        if (aoA && aoB && size == newSize)
            return true;

        destroyTextures();

        HgiTextureDesc desc;
        desc.type = HgiTextureType2D;
        desc.dimensions = GfVec3i(newSize[0], newSize[1], 1);
        desc.layerCount = 1;
        desc.mipLevels = 1;
        desc.sampleCount = HgiSampleCount1;
        desc.format = kAOFormat;
        desc.usage = HgiTextureUsageBitsColorTarget | HgiTextureUsageBitsShaderRead;

        desc.debugName = "Stageviz Ambient Occlusion A";
        aoA = hgi->CreateTexture(desc);
        desc.debugName = "Stageviz Ambient Occlusion B";
        aoB = hgi->CreateTexture(desc);

        if (!aoA || !aoB) {
            destroyTextures();
            return false;
        }

        size = newSize;
        return true;
    }

    bool ensurePrograms(const TfToken& path, HgiFormat colorFormat, HgiFormat depthFormat)
    {
        if (path.IsEmpty())
            return false;

        if (shaderPath == path && this->colorFormat == colorFormat && this->depthFormat == depthFormat)
            return true;

        HgiShaderFunctionDesc rawProgram = rawDesc(depthFormat);
        HgiShaderFunctionDesc blurProgram = blurDesc();
        HgiShaderFunctionDesc compositeProgram = compositeDesc(colorFormat);

        raw.SetProgram(path, TfToken("Render::AmbientOcclusion::Raw"), rawProgram);
        blur.SetProgram(path, TfToken("Render::AmbientOcclusion::Blur"), blurProgram);
        composite.SetProgram(path, TfToken("Render::AmbientOcclusion::Composite"), compositeProgram);

        shaderPath = path;
        this->colorFormat = colorFormat;
        this->depthFormat = depthFormat;
        return true;
    }

    Hgi* hgi = nullptr;
    HdxFullscreenShader raw;
    HdxFullscreenShader blur;
    HdxFullscreenShader composite;
    HgiTextureHandle aoA;
    HgiTextureHandle aoB;
    GfVec2i size = GfVec2i(0);
    TfToken shaderPath;
    HgiFormat colorFormat = HgiFormatInvalid;
    HgiFormat depthFormat = HgiFormatInvalid;
};

bool
RenderTaskParams::enabled() const
{
    return ambientOcclusion.enabled;
}

bool
operator==(const RenderTaskParams& lhs, const RenderTaskParams& rhs)
{
    const auto& a = lhs.ambientOcclusion;
    const auto& b = rhs.ambientOcclusion;
    return a.enabled == b.enabled && a.contactAmount == b.contactAmount && a.contactRadius == b.contactRadius
           && a.broadAmount == b.broadAmount && a.broadRadius == b.broadRadius && a.normalBias == b.normalBias
           && a.falloff == b.falloff && a.contrast == b.contrast && a.edgeSharpness == b.edgeSharpness
           && a.blurEnabled == b.blurEnabled && a.blurRadius == b.blurRadius && a.quality == b.quality
           && a.debugMode == b.debugMode && lhs.nearClip == rhs.nearClip && lhs.farClip == rhs.farClip
           && lhs.orthographic == rhs.orthographic && lhs.projectionMatrix == rhs.projectionMatrix
           && lhs.shaderPath == rhs.shaderPath;
}

bool
operator!=(const RenderTaskParams& lhs, const RenderTaskParams& rhs)
{
    return !(lhs == rhs);
}

std::ostream&
operator<<(std::ostream& out, const RenderTaskParams& params)
{
    const auto& ao = params.ambientOcclusion;
    out << "RenderTaskParams(ambientOcclusion=" << ao.enabled << ", contact=" << ao.contactAmount << "@"
        << ao.contactRadius << "px, broad=" << ao.broadAmount << "@" << ao.broadRadius << "px, quality="
        << static_cast<int>(ao.quality) << ", debug=" << static_cast<int>(ao.debugMode) << ", nearClip="
        << params.nearClip << ", farClip=" << params.farClip << ", orthographic=" << params.orthographic
        << ", shaderPath=" << params.shaderPath.GetString() << ")";
    return out;
}

RenderTask::RenderTask(HdSceneDelegate* delegate, const SdfPath& id)
    : HdxTask(id)
{
    (void)delegate;
}

RenderTask::~RenderTask() = default;

const TfToken&
RenderTask::token()
{
    static const TfToken value("stagevizRenderTask");
    return value;
}

void
RenderTask::_Sync(HdSceneDelegate* delegate, HdTaskContext* ctx, HdDirtyBits* dirtyBits)
{
    (void)ctx;

    if ((*dirtyBits) & HdChangeTracker::DirtyParams) {
        RenderTaskParams params;
        if (_GetTaskParams(delegate, &params))
            m_params = params;
    }

    *dirtyBits = HdChangeTracker::Clean;
}

void
RenderTask::Prepare(HdTaskContext* ctx, HdRenderIndex* renderIndex)
{
    (void)ctx;
    (void)renderIndex;

    if (!m_params.enabled())
        return;

    Hgi* hgi = _GetHgi();
    if (hgi && !m_shader)
        m_shader = std::make_unique<Shader>(hgi);
}

void
RenderTask::Execute(HdTaskContext* ctx)
{
    if (!ctx || !m_params.enabled() || !m_shader || m_params.shaderPath.IsEmpty())
        return;

    HgiTextureHandle inputColor;
    HgiTextureHandle depth;
    if (!_GetTaskContextData(ctx, HdAovTokens->color, &inputColor) || !inputColor)
        return;
    if (!_GetTaskContextData(ctx, HdAovTokens->depth, &depth) || !depth)
        return;

    const HgiTextureDesc& colorDesc = inputColor->GetDescriptor();
    const HgiTextureDesc& depthDesc = depth->GetDescriptor();
    const GfVec2i screenSize(std::max(1, colorDesc.dimensions[0]), std::max(1, colorDesc.dimensions[1]));

    if (!m_shader->ensureTextures(screenSize))
        return;
    if (!m_shader->ensurePrograms(m_params.shaderPath, colorDesc.format, depthDesc.format))
        return;

    const float nearClip = std::max(1e-5f, m_params.nearClip);
    const float farClip = std::max(nearClip + 1e-4f, m_params.farClip);
    const GfMatrix4f& p = m_params.projectionMatrix;
    const float p00 = p[0][0];
    const float p11 = p[1][1];
    if (std::abs(p00) < 1e-8f || std::abs(p11) < 1e-8f)
        return;

    const auto& ao = m_params.ambientOcclusion;

    RawConstants rawConstants;
    rawConstants.clipInfo = GfVec4f(nearClip * farClip, nearClip - farClip, farClip,
                                    0.5f * static_cast<float>(screenSize[0]) * std::abs(p00));
    rawConstants.projInfo = GfVec4f(-2.0f / (static_cast<float>(screenSize[0]) * p00),
                                    -2.0f / (static_cast<float>(screenSize[1]) * p11),
                                    (1.0f - p[2][0]) / p00, (1.0f + p[2][1]) / p11);
    rawConstants.screenSize = screenSize;
    rawConstants.contactAmount = std::max(0.0f, ao.contactAmount);
    rawConstants.contactRadius = std::max(1.0f, ao.contactRadius);
    rawConstants.broadAmount = std::max(0.0f, ao.broadAmount);
    rawConstants.broadRadius = std::max(1.0f, ao.broadRadius);
    rawConstants.normalBias = std::max(0.0f, ao.normalBias);
    rawConstants.falloff = std::max(0.1f, ao.falloff);
    rawConstants.contrast = std::max(0.1f, ao.contrast);
    rawConstants.sampleCount = sampleCount(ao.quality);
    rawConstants.prefilterEnabled = ao.blurEnabled ? 1 : 0;
    rawConstants.orthographic = m_params.orthographic ? 1 : 0;

    // R/G carry contact and broad visibility. B/A preserve the depth key for
    // the bilateral filter, keeping the two AO terms independent until compose.
    m_shader->raw.BindTextures({ depth });
    m_shader->raw.SetShaderConstants(sizeof(rawConstants), &rawConstants);
    m_shader->raw.Draw(m_shader->aoA, HgiTextureHandle());

    if (ao.blurEnabled) {
        BlurConstants blurConstants;
        blurConstants.screenSize = screenSize;
        blurConstants.edgeSharpness = std::max(0.0f, ao.edgeSharpness);
        blurConstants.blurRadius = std::max(1.0f, ao.blurRadius);

        blurConstants.offset = GfVec2i(1, 0);
        m_shader->blur.BindTextures({ m_shader->aoA });
        m_shader->blur.SetShaderConstants(sizeof(blurConstants), &blurConstants);
        m_shader->blur.Draw(m_shader->aoB, HgiTextureHandle());

        blurConstants.offset = GfVec2i(0, 1);
        m_shader->blur.BindTextures({ m_shader->aoB });
        m_shader->blur.SetShaderConstants(sizeof(blurConstants), &blurConstants);
        m_shader->blur.Draw(m_shader->aoA, HgiTextureHandle());
    }

    _ToggleRenderTarget(ctx);

    HgiTextureHandle outputColor;
    if (!_GetTaskContextData(ctx, HdAovTokens->color, &outputColor) || !outputColor || outputColor == inputColor) {
        _ToggleRenderTarget(ctx);
        return;
    }

    CompositeConstants compositeConstants;
    compositeConstants.screenSize = screenSize;
    compositeConstants.debugMode = static_cast<int>(ao.debugMode);

    m_shader->composite.BindTextures({ inputColor, m_shader->aoA });
    m_shader->composite.SetShaderConstants(sizeof(compositeConstants), &compositeConstants);
    m_shader->composite.Draw(outputColor, HgiTextureHandle());
}

}  // namespace stageviz
