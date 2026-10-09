// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#include "rendertask.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <pxr/base/gf/vec2i.h>
#include <pxr/base/gf/vec3i.h>
#include <pxr/base/gf/vec4d.h>
#include <pxr/base/gf/vec4f.h>
#include <pxr/base/vt/value.h>
#include <pxr/imaging/hd/aov.h>
#include <pxr/imaging/hd/changeTracker.h>
#include <pxr/imaging/hd/renderIndex.h>
#include <pxr/imaging/hd/renderPass.h>
#include <pxr/imaging/hd/renderPassState.h>
#include <pxr/imaging/hd/rprim.h>
#include <pxr/imaging/hd/tokens.h>
#include <pxr/imaging/hdSt/renderBuffer.h>
#include <pxr/imaging/hdSt/renderDelegate.h>
#include <pxr/imaging/hdSt/renderPassShader.h>
#include <pxr/imaging/hdSt/renderPassState.h>
#include <pxr/imaging/hdSt/resourceRegistry.h>
#include <pxr/imaging/hdSt/tokens.h>
#include <pxr/imaging/hdSt/volume.h>
#include <pxr/imaging/hdx/fullscreenShader.h>
#include <pxr/imaging/hgi/hgi.h>
#include <pxr/imaging/hgi/shaderFunctionDesc.h>
#include <pxr/imaging/hgi/texture.h>
#include <pxr/imaging/hio/glslfx.h>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

PXR_NAMESPACE_USING_DIRECTIVE

namespace stageviz {

namespace {

    constexpr HgiFormat kAOFormat = HgiFormatFloat16Vec4;
    constexpr int kSpiralTurnCount = 7;
    constexpr unsigned int kMaxSelectionRadius = 4;

    int sampleCount(ViewState::AmbientOcclusionQuality quality)
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

    struct SelectionConstants {
        GfVec4f color = GfVec4f(1.0f);
        GfVec2i screenSize = GfVec2i(1);
        GfVec2i selectedMaskSize = GfVec2i(1);
        GfVec2i selectedSubsetPrimMaskSize = GfVec2i(1);
        GfVec2i selectedSubsetElementMaskSize = GfVec2i(1);
        int selectedMaskCapacity = 0;
        int selectedSubsetPrimMaskCapacity = 0;
        int selectedSubsetElementCapacity = 0;
        int radius = 3;
        float softnessStrength = 0.85f;
        float softnessFalloff = 0.45f;
        float padding0 = 0.0f;
        float padding1 = 0.0f;
    };

    struct ResolvedSubsetSelection {
        int32_t primId = -1;
        std::vector<int32_t> elementIds;

        bool operator==(const ResolvedSubsetSelection& other) const
        {
            return primId == other.primId && elementIds == other.elementIds;
        }
    };

    HgiShaderFunctionTextureDesc textureDesc(const char* name, uint32_t bindIndex, HgiFormat format)
    {
        HgiShaderFunctionTextureDesc desc;
        desc.nameInShader = name;
        desc.bindIndex = bindIndex;
        desc.dimensions = 2;
        desc.format = format;
        return desc;
    }

    HgiShaderFunctionDesc baseFragmentDesc(const char* debugName)
    {
        HgiShaderFunctionDesc desc;
        desc.debugName = debugName;
        desc.shaderStage = HgiShaderStageFragment;
        HgiShaderFunctionAddStageInput(&desc, "uvOut", "vec2");
        HgiShaderFunctionAddStageOutput(&desc, "hd_FragColor", "vec4");
        return desc;
    }

    HgiShaderFunctionDesc rawDesc(HgiFormat depthFormat)
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

    HgiShaderFunctionDesc blurDesc()
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

    HgiShaderFunctionDesc compositeDesc(HgiFormat colorFormat)
    {
        HgiShaderFunctionDesc desc = baseFragmentDesc("Stageviz Ambient Occlusion Composite");
        desc.textures.push_back(textureDesc("colorIn", 0, colorFormat));
        desc.textures.push_back(textureDesc("aoIn", 1, kAOFormat));

        HgiShaderFunctionAddConstantParam(&desc, "uScreenSize", "ivec2");
        HgiShaderFunctionAddConstantParam(&desc, "uDebugMode", "int");
        HgiShaderFunctionAddConstantParam(&desc, "uPadding0", "int");
        return desc;
    }

    HgiShaderFunctionDesc selectionDesc(HgiFormat colorFormat, HgiFormat sceneIdFormat)
    {
        HgiShaderFunctionDesc desc = baseFragmentDesc("Stageviz Selection Outline Composite");
        desc.textures.push_back(textureDesc("colorIn", 0, colorFormat));
        desc.textures.push_back(textureDesc("scenePrimIdIn", 1, sceneIdFormat));
        desc.textures.push_back(textureDesc("sceneElementIdIn", 2, sceneIdFormat));
        desc.textures.push_back(textureDesc("selectedIdMaskIn", 3, HgiFormatInt32));
        desc.textures.push_back(textureDesc("selectedSubsetPrimMaskIn", 4, HgiFormatInt32));
        desc.textures.push_back(textureDesc("selectedSubsetElementMaskIn", 5, HgiFormatInt32));

        HgiShaderFunctionAddConstantParam(&desc, "uSelectionColor", "vec4");
        HgiShaderFunctionAddConstantParam(&desc, "uScreenSize", "ivec2");
        HgiShaderFunctionAddConstantParam(&desc, "uSelectedMaskSize", "ivec2");
        HgiShaderFunctionAddConstantParam(&desc, "uSelectedSubsetPrimMaskSize", "ivec2");
        HgiShaderFunctionAddConstantParam(&desc, "uSelectedSubsetElementMaskSize", "ivec2");
        HgiShaderFunctionAddConstantParam(&desc, "uSelectedMaskCapacity", "int");
        HgiShaderFunctionAddConstantParam(&desc, "uSelectedSubsetPrimMaskCapacity", "int");
        HgiShaderFunctionAddConstantParam(&desc, "uSelectedSubsetElementCapacity", "int");
        HgiShaderFunctionAddConstantParam(&desc, "uRadius", "int");
        HgiShaderFunctionAddConstantParam(&desc, "uSoftnessStrength", "float");
        HgiShaderFunctionAddConstantParam(&desc, "uSoftnessFalloff", "float");
        HgiShaderFunctionAddConstantParam(&desc, "uPadding0", "float");
        HgiShaderFunctionAddConstantParam(&desc, "uPadding1", "float");
        return desc;
    }

    HdRprimCollection sceneCollection(SdfPathVector roots, const HdReprSelector& reprSelector)
    {
        if (roots.empty())
            roots.push_back(SdfPath::AbsoluteRootPath());
        SdfPath::RemoveDescendentPaths(&roots);

        HdRprimCollection collection(HdTokens->geometry, reprSelector, SdfPath::AbsoluteRootPath(), false);
        collection.SetRootPaths(roots);
        return collection;
    }

}  // namespace

class RenderTask::Shader {
public:
    explicit Shader(Hgi* value)
        : hgi(value)
        , raw(value, "Stageviz Ambient Occlusion Raw")
        , blur(value, "Stageviz Ambient Occlusion Blur")
        , composite(value, "Stageviz Ambient Occlusion Composite")
        , selection(value, "Stageviz Selection Outline Composite")
    {
        HgiDepthStencilState depthState;
        depthState.depthTestEnabled = false;
        depthState.depthWriteEnabled = false;
        raw.SetDepthState(depthState);
        blur.SetDepthState(depthState);
        composite.SetDepthState(depthState);
        selection.SetDepthState(depthState);
    }

    ~Shader()
    {
        destroyAoTextures();
        destroySelectedMask();
        destroySelectedSubsetMasks();
    }

    void destroyAoTextures()
    {
        if (!hgi)
            return;
        if (aoA)
            hgi->DestroyTexture(&aoA);
        if (aoB)
            hgi->DestroyTexture(&aoB);
        aoSize = GfVec2i(0);
    }

    void destroySelectedMask()
    {
        if (hgi && selectedIdMask)
            hgi->DestroyTexture(&selectedIdMask);
        selectedMaskSize = GfVec2i(0);
        selectedMaskCapacity = 0;
        selectedIds.clear();
    }

    bool ensureAoTextures(const GfVec2i& newSize)
    {
        if (!hgi || newSize[0] <= 0 || newSize[1] <= 0)
            return false;

        if (aoA && aoB && aoSize == newSize)
            return true;

        destroyAoTextures();

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
            destroyAoTextures();
            return false;
        }

        aoSize = newSize;
        return true;
    }

    bool ensureSelectedMask(const std::vector<int32_t>& ids)
    {
        if (!hgi)
            return false;

        if (selectedIdMask && selectedIds == ids)
            return true;

        destroySelectedMask();

        // The selection shader always binds this texture. Keep a valid 1x1
        // zero mask when selection consists only of GeomSubset elements.
        if (ids.empty()) {
            const int32_t zero = 0;
            HgiTextureDesc desc;
            desc.debugName = "Stageviz Selected PrimId Mask";
            desc.type = HgiTextureType2D;
            desc.dimensions = GfVec3i(1, 1, 1);
            desc.layerCount = 1;
            desc.mipLevels = 1;
            desc.sampleCount = HgiSampleCount1;
            desc.format = HgiFormatInt32;
            desc.usage = HgiTextureUsageBitsShaderRead;
            desc.pixelsByteSize = sizeof(zero);
            desc.initialData = &zero;
            selectedIdMask = hgi->CreateTexture(desc);
            selectedMaskSize = GfVec2i(1);
            selectedMaskCapacity = 0;
            selectedIds.clear();
            return static_cast<bool>(selectedIdMask);
        }

        const int32_t maxId = *std::max_element(ids.begin(), ids.end());
        if (maxId < 0)
            return false;

        selectedMaskCapacity = maxId + 1;
        constexpr int kMaskWidth = 4096;
        const int width = std::min(kMaskWidth, selectedMaskCapacity);
        const int height = (selectedMaskCapacity + width - 1) / width;
        selectedMaskSize = GfVec2i(width, std::max(1, height));

        std::vector<int32_t> mask(static_cast<size_t>(selectedMaskSize[0]) * selectedMaskSize[1], 0);
        for (const int32_t id : ids) {
            if (id >= 0 && id < selectedMaskCapacity)
                mask[static_cast<size_t>(id)] = 1;
        }

        HgiTextureDesc desc;
        desc.debugName = "Stageviz Selected PrimId Mask";
        desc.type = HgiTextureType2D;
        desc.dimensions = GfVec3i(selectedMaskSize[0], selectedMaskSize[1], 1);
        desc.layerCount = 1;
        desc.mipLevels = 1;
        desc.sampleCount = HgiSampleCount1;
        desc.format = HgiFormatInt32;
        desc.usage = HgiTextureUsageBitsShaderRead;
        desc.pixelsByteSize = mask.size() * sizeof(int32_t);
        desc.initialData = mask.data();

        selectedIdMask = hgi->CreateTexture(desc);
        if (!selectedIdMask) {
            destroySelectedMask();
            return false;
        }

        selectedIds = ids;
        return true;
    }

    void destroySelectedSubsetMasks()
    {
        if (hgi && selectedSubsetPrimMask)
            hgi->DestroyTexture(&selectedSubsetPrimMask);
        if (hgi && selectedSubsetElementMask)
            hgi->DestroyTexture(&selectedSubsetElementMask);

        selectedSubsetPrimMaskSize = GfVec2i(0);
        selectedSubsetElementMaskSize = GfVec2i(0);
        selectedSubsetPrimMaskCapacity = 0;
        selectedSubsetElementCapacity = 0;
        selectedSubsets.clear();
    }

    bool ensureSelectedSubsetMask(const std::vector<ResolvedSubsetSelection>& subsets)
    {
        if (!hgi)
            return false;

        if (selectedSubsetPrimMask && selectedSubsetElementMask && selectedSubsets == subsets)
            return true;

        destroySelectedSubsetMasks();

        // As with the whole-prim mask, keep valid dummy textures so the shader
        // binding layout is identical for ordinary and subset-only selection.
        if (subsets.empty()) {
            const int32_t zero = 0;
            HgiTextureDesc desc;
            desc.type = HgiTextureType2D;
            desc.dimensions = GfVec3i(1, 1, 1);
            desc.layerCount = 1;
            desc.mipLevels = 1;
            desc.sampleCount = HgiSampleCount1;
            desc.format = HgiFormatInt32;
            desc.usage = HgiTextureUsageBitsShaderRead;
            desc.pixelsByteSize = sizeof(zero);
            desc.initialData = &zero;

            desc.debugName = "Stageviz Selected GeomSubset Prim Rows";
            selectedSubsetPrimMask = hgi->CreateTexture(desc);
            desc.debugName = "Stageviz Selected GeomSubset Elements";
            selectedSubsetElementMask = hgi->CreateTexture(desc);

            selectedSubsetPrimMaskSize = GfVec2i(1);
            selectedSubsetElementMaskSize = GfVec2i(1);
            selectedSubsetPrimMaskCapacity = 0;
            selectedSubsetElementCapacity = 0;
            selectedSubsets.clear();
            if (!selectedSubsetPrimMask || !selectedSubsetElementMask) {
                destroySelectedSubsetMasks();
                return false;
            }
            return true;
        }

        int32_t maxPrimId = -1;
        int32_t maxElementId = -1;
        for (const ResolvedSubsetSelection& subset : subsets) {
            maxPrimId = std::max(maxPrimId, subset.primId);
            for (const int32_t elementId : subset.elementIds)
                maxElementId = std::max(maxElementId, elementId);
        }
        if (maxPrimId < 0 || maxElementId < 0)
            return ensureSelectedSubsetMask({});

        constexpr int kMaskWidth = 4096;
        selectedSubsetPrimMaskCapacity = maxPrimId + 1;
        const int primWidth = std::min(kMaskWidth, selectedSubsetPrimMaskCapacity);
        const int primHeight = (selectedSubsetPrimMaskCapacity + primWidth - 1) / primWidth;
        selectedSubsetPrimMaskSize = GfVec2i(primWidth, std::max(1, primHeight));

        selectedSubsetElementCapacity = maxElementId + 1;
        const size_t totalElementSlots = static_cast<size_t>(selectedSubsetElementCapacity) * subsets.size();
        const int elementWidth = static_cast<int>(std::min<size_t>(kMaskWidth, std::max<size_t>(1, totalElementSlots)));
        const int elementHeight = static_cast<int>((totalElementSlots + elementWidth - 1) / elementWidth);
        selectedSubsetElementMaskSize = GfVec2i(elementWidth, std::max(1, elementHeight));

        std::vector<int32_t> primRows(static_cast<size_t>(selectedSubsetPrimMaskSize[0])
                                          * selectedSubsetPrimMaskSize[1],
                                      0);
        std::vector<int32_t> elementMask(static_cast<size_t>(selectedSubsetElementMaskSize[0])
                                             * selectedSubsetElementMaskSize[1],
                                         0);

        for (size_t row = 0; row < subsets.size(); ++row) {
            const ResolvedSubsetSelection& subset = subsets[row];
            if (subset.primId < 0 || subset.primId >= selectedSubsetPrimMaskCapacity)
                continue;

            // 0 means no subset row; row+1 keeps row zero representable.
            primRows[static_cast<size_t>(subset.primId)] = static_cast<int32_t>(row + 1);
            for (const int32_t elementId : subset.elementIds) {
                if (elementId < 0 || elementId >= selectedSubsetElementCapacity)
                    continue;
                const size_t flat = row * static_cast<size_t>(selectedSubsetElementCapacity)
                                    + static_cast<size_t>(elementId);
                if (flat < elementMask.size())
                    elementMask[flat] = 1;
            }
        }

        HgiTextureDesc desc;
        desc.type = HgiTextureType2D;
        desc.layerCount = 1;
        desc.mipLevels = 1;
        desc.sampleCount = HgiSampleCount1;
        desc.format = HgiFormatInt32;
        desc.usage = HgiTextureUsageBitsShaderRead;

        desc.debugName = "Stageviz Selected GeomSubset Prim Rows";
        desc.dimensions = GfVec3i(selectedSubsetPrimMaskSize[0], selectedSubsetPrimMaskSize[1], 1);
        desc.pixelsByteSize = primRows.size() * sizeof(int32_t);
        desc.initialData = primRows.data();
        selectedSubsetPrimMask = hgi->CreateTexture(desc);

        desc.debugName = "Stageviz Selected GeomSubset Elements";
        desc.dimensions = GfVec3i(selectedSubsetElementMaskSize[0], selectedSubsetElementMaskSize[1], 1);
        desc.pixelsByteSize = elementMask.size() * sizeof(int32_t);
        desc.initialData = elementMask.data();
        selectedSubsetElementMask = hgi->CreateTexture(desc);

        if (!selectedSubsetPrimMask || !selectedSubsetElementMask) {
            destroySelectedSubsetMasks();
            return false;
        }

        selectedSubsets = subsets;
        return true;
    }

    bool ensureAoPrograms(const TfToken& path, HgiFormat colorFormat, HgiFormat depthFormat)
    {
        if (path.IsEmpty())
            return false;

        if (aoShaderPath == path && aoColorFormat == colorFormat && aoDepthFormat == depthFormat)
            return true;

        HgiShaderFunctionDesc rawProgram = rawDesc(depthFormat);
        HgiShaderFunctionDesc blurProgram = blurDesc();
        HgiShaderFunctionDesc compositeProgram = compositeDesc(colorFormat);

        raw.SetProgram(path, TfToken("Render::AmbientOcclusion::Raw"), rawProgram);
        blur.SetProgram(path, TfToken("Render::AmbientOcclusion::Blur"), blurProgram);
        composite.SetProgram(path, TfToken("Render::AmbientOcclusion::Composite"), compositeProgram);

        aoShaderPath = path;
        aoColorFormat = colorFormat;
        aoDepthFormat = depthFormat;
        return true;
    }

    bool ensureSelectionProgram(const TfToken& path, HgiFormat colorFormat, HgiFormat sceneIdFormat)
    {
        if (path.IsEmpty())
            return false;

        if (selectionShaderPath == path && selectionColorFormat == colorFormat && this->sceneIdFormat == sceneIdFormat)
            return true;

        HgiShaderFunctionDesc selectionProgram = selectionDesc(colorFormat, sceneIdFormat);
        selection.SetProgram(path, TfToken("Render::Selection::Outline"), selectionProgram);

        selectionShaderPath = path;
        selectionColorFormat = colorFormat;
        this->sceneIdFormat = sceneIdFormat;
        return true;
    }

    Hgi* hgi = nullptr;
    HdxFullscreenShader raw;
    HdxFullscreenShader blur;
    HdxFullscreenShader composite;
    HdxFullscreenShader selection;

    HgiTextureHandle aoA;
    HgiTextureHandle aoB;
    GfVec2i aoSize = GfVec2i(0);

    HgiTextureHandle selectedIdMask;
    GfVec2i selectedMaskSize = GfVec2i(0);
    int selectedMaskCapacity = 0;
    std::vector<int32_t> selectedIds;

    HgiTextureHandle selectedSubsetPrimMask;
    HgiTextureHandle selectedSubsetElementMask;
    GfVec2i selectedSubsetPrimMaskSize = GfVec2i(0);
    GfVec2i selectedSubsetElementMaskSize = GfVec2i(0);
    int selectedSubsetPrimMaskCapacity = 0;
    int selectedSubsetElementCapacity = 0;
    std::vector<ResolvedSubsetSelection> selectedSubsets;

    TfToken aoShaderPath;
    HgiFormat aoColorFormat = HgiFormatInvalid;
    HgiFormat aoDepthFormat = HgiFormatInvalid;

    TfToken selectionShaderPath;
    HgiFormat selectionColorFormat = HgiFormatInvalid;
    HgiFormat sceneIdFormat = HgiFormatInvalid;
};

class RenderTask::SceneIdPass {
public:
    explicit SceneIdPass(HdRenderIndex* renderIndex)
        : m_renderIndex(renderIndex)
    {}

    ~SceneIdPass() { cleanup(); }

    bool sync(const SceneIdSettings& settings, const TfToken& sceneIdShaderPath)
    {
        m_settings = settings;

        if (!m_renderIndex || !m_settings.enabled)
            return true;

        if (!dynamic_cast<HdStRenderDelegate*>(m_renderIndex->GetRenderDelegate()))
            return false;

        if (!ensureResources(m_settings.size, sceneIdShaderPath))
            return false;

        configure();
        return true;
    }

    void prepare(HdRenderIndex* renderIndex)
    {
        if (!enabled() || !m_renderPassState)
            return;

        m_renderPassState->SetAovBindings(m_aovBindings);
        m_renderPassState->Prepare(renderIndex->GetResourceRegistry());
    }

    HgiTextureHandle execute()
    {
        if (!enabled() || !m_renderPass || !m_renderPassState)
            return {};

        m_renderPassState->SetAovBindings(m_aovBindings);
        m_renderPass->Execute(m_renderPassState, m_settings.renderTags);
        return textureHandle(0);
    }

    HgiTextureHandle elementIdTexture() const { return textureHandle(2); }

    // A picking-only collection: exclude previously picked rprims without
    // touching USD visibility, scene indices, or the regular outline ID image.
    GpuPickResult pickAt(const GfVec2i& pixel, const SdfPathVector& excludes)
    {
        GpuPickResult result;
        if (!enabled() || !m_renderPass || !m_renderIndex || m_aovBuffers.size() < 6)
            return result;

        HdRprimCollection pickCollection = sceneCollection(m_settings.roots, m_settings.reprSelector);
        pickCollection.SetExcludePaths(excludes);
        m_renderPass->SetRprimCollection(pickCollection);
        m_renderPass->Sync();
        prepare(m_renderIndex);
        execute();

        auto readInt = [&](size_t slot) -> int32_t {
            HdStRenderBuffer* buffer = m_aovBuffers[slot].get();
            if (!buffer || buffer->GetFormat() != HdFormatInt32 || pixel[0] < 0 || pixel[1] < 0
                || pixel[0] >= static_cast<int>(buffer->GetWidth())
                || pixel[1] >= static_cast<int>(buffer->GetHeight()))
                return -1;
            const auto* values = static_cast<const int32_t*>(buffer->Map());
            if (!values)
                return -1;
            const size_t offset = static_cast<size_t>(pixel[1]) * buffer->GetWidth() + pixel[0];
            const int32_t value = values[offset];
            buffer->Unmap();
            return value;
        };
        result.primId = readInt(0);
        if (result.primId >= 0) {
            result.instanceId = readInt(1);
            result.elementId = readInt(2);
            HdStRenderBuffer* depthBuffer = m_aovBuffers[5].get();
            if (depthBuffer && depthBuffer->GetFormat() == HdFormatFloat32) {
                const auto* depths = static_cast<const float*>(depthBuffer->Map());
                if (depths) {
                    result.depth = depths[static_cast<size_t>(pixel[1]) * depthBuffer->GetWidth() + pixel[0]];
                    depthBuffer->Unmap();
                }
            }
            refreshRprimPathCache();
            const auto it = m_rprimPathsById.find(result.primId);
            if (it != m_rprimPathsById.end())
                result.path = it->second;
        }

        // Restore collection before the next regular scene-ID render.
        m_renderPass->SetRprimCollection(sceneCollection(m_settings.roots, m_settings.reprSelector));
        m_renderPass->Sync();
        return result;
    }


    int32_t elementIdAt(const GfVec2i& pixel)
    {
        if (m_aovBuffers.size() <= 2 || !m_aovBuffers[2])
            return -1;

        HdStRenderBuffer* buffer = m_aovBuffers[2].get();
        const int width = static_cast<int>(buffer->GetWidth());
        const int height = static_cast<int>(buffer->GetHeight());
        if (width <= 0 || height <= 0 || buffer->GetFormat() != HdFormatInt32 || pixel[0] < 0 || pixel[1] < 0
            || pixel[0] >= width || pixel[1] >= height)
            return -1;

        const auto* values = static_cast<const int32_t*>(buffer->Map());
        if (!values)
            return -1;

        const size_t index = static_cast<size_t>(pixel[1]) * static_cast<size_t>(width) + static_cast<size_t>(pixel[0]);
        const int32_t value = values[index];
        buffer->Unmap();
        return value;
    }

    std::vector<int32_t> selectedPrimIds(SdfPathVector paths)
    {
        if (!m_renderIndex || paths.empty())
            return {};

        for (SdfPath& path : paths) {
            if (path.IsPropertyPath())
                path = path.GetPrimPath();
        }
        SdfPath::RemoveDescendentPaths(&paths);

        const unsigned rprimIndexVersion = m_renderIndex->GetChangeTracker().GetRprimIndexVersion();
        if (paths == m_selectedPaths && rprimIndexVersion == m_selectedRprimIndexVersion)
            return m_selectedPrimIds;

        std::vector<int32_t> ids;
        std::unordered_set<int32_t> seen;

        auto appendRprim = [&](const SdfPath& path) {
            const HdRprim* rprim = m_renderIndex->GetRprim(path);
            if (!rprim)
                return;
            const int32_t id = rprim->GetPrimId();
            if (id >= 0 && seen.insert(id).second)
                ids.push_back(id);
        };

        for (const SdfPath& root : paths) {
            appendRprim(root);
            for (const SdfPath& child : m_renderIndex->GetRprimSubtree(root))
                appendRprim(child);
        }

        std::sort(ids.begin(), ids.end());
        m_selectedPaths = paths;
        m_selectedPrimIds = ids;
        m_selectedRprimIndexVersion = rprimIndexVersion;
        return ids;
    }

    std::vector<ResolvedSubsetSelection> selectedSubsetElements(const std::vector<SelectionSubsetSettings>& subsets)
    {
        if (!m_renderIndex || subsets.empty())
            return {};

        const unsigned rprimIndexVersion = m_renderIndex->GetChangeTracker().GetRprimIndexVersion();
        if (subsets == m_selectedSubsetSettings && rprimIndexVersion == m_selectedSubsetRprimIndexVersion)
            return m_selectedSubsetElements;

        std::unordered_map<int32_t, std::unordered_set<int32_t>> grouped;
        for (const SelectionSubsetSettings& subset : subsets) {
            const HdRprim* rprim = m_renderIndex->GetRprim(subset.meshPath);
            if (!rprim)
                continue;

            const int32_t primId = rprim->GetPrimId();
            if (primId < 0)
                continue;

            std::unordered_set<int32_t>& elements = grouped[primId];
            for (const int32_t elementId : subset.elementIds) {
                if (elementId >= 0)
                    elements.insert(elementId);
            }
        }

        std::vector<ResolvedSubsetSelection> result;
        result.reserve(grouped.size());
        for (auto& entry : grouped) {
            ResolvedSubsetSelection subset;
            subset.primId = entry.first;
            subset.elementIds.assign(entry.second.begin(), entry.second.end());
            std::sort(subset.elementIds.begin(), subset.elementIds.end());
            result.push_back(std::move(subset));
        }
        std::sort(result.begin(), result.end(), [](const ResolvedSubsetSelection& a, const ResolvedSubsetSelection& b) {
            return a.primId < b.primId;
        });

        m_selectedSubsetSettings = subsets;
        m_selectedSubsetElements = result;
        m_selectedSubsetRprimIndexVersion = rprimIndexVersion;
        return result;
    }

    SdfPathVector captureVisiblePaths(const std::vector<GfMatrix4d>& captureProjectionMatrices,
                                      bool includeViewportPass)
    {
        SdfPathVector result;
        if (!enabled() || m_aovBuffers.empty() || !m_aovBuffers[0] || !m_renderIndex)
            return result;

        std::unordered_set<int32_t> visibleIds;
        visibleIds.reserve(512);

        // Include the normal viewport ID image when it was already needed for
        // selection, or when gridSize=1 requested the quick capture path. A
        // capture-only tiled scan can skip that extra full-view geometry pass.
        if (includeViewportPass)
            appendVisibleIds(visibleIds);

        if (!captureProjectionMatrices.empty()) {
            const SceneIdSettings viewportSettings = m_settings;

            // Reuse the exact same non-MSAA primId/depth allocation for every
            // tile. Only the narrowed projection changes, so memory use stays
            // constant even for a 4x4 or larger thorough scan.
            for (const GfMatrix4d& projectionMatrix : captureProjectionMatrices) {
                m_settings.projectionMatrix = projectionMatrix;
                configure();
                prepare(m_renderIndex);
                if (execute())
                    appendVisibleIds(visibleIds);
            }

            // Restore the viewport camera state before leaving the task. The
            // RenderTask parameters may be unchanged next frame, so relying on
            // a future Sync() to restore this would be incorrect.
            m_settings = viewportSettings;
            configure();
            prepare(m_renderIndex);
        }

        if (visibleIds.empty())
            return result;

        // Build the primId -> rprim path table only when Hydra changes its
        // rprim index. Repeated captures then pay only the ID-buffer readbacks
        // and set lookups, not a full render-index walk.
        refreshRprimPathCache();

        result.reserve(visibleIds.size());
        for (const int32_t id : visibleIds) {
            const auto it = m_rprimPathsById.find(id);
            if (it != m_rprimPathsById.end())
                result.push_back(it->second);
        }
        std::sort(result.begin(), result.end());
        return result;
    }

    bool enabled() const { return m_settings.enabled && m_settings.size[0] > 0 && m_settings.size[1] > 0; }

private:
    void appendVisibleIds(std::unordered_set<int32_t>& visibleIds)
    {
        if (m_aovBuffers.empty() || !m_aovBuffers[0])
            return;

        HdStRenderBuffer* primIdBuffer = m_aovBuffers[0].get();
        const int width = static_cast<int>(primIdBuffer->GetWidth());
        const int height = static_cast<int>(primIdBuffer->GetHeight());
        if (width <= 0 || height <= 0 || primIdBuffer->GetFormat() != HdFormatInt32)
            return;

        const auto* pixels = static_cast<const int32_t*>(primIdBuffer->Map());
        if (!pixels)
            return;

        const size_t pixelCount = static_cast<size_t>(width) * static_cast<size_t>(height);
        for (size_t i = 0; i < pixelCount; ++i) {
            if (pixels[i] >= 0)
                visibleIds.insert(pixels[i]);
        }
        primIdBuffer->Unmap();
    }

    void refreshRprimPathCache()
    {
        if (!m_renderIndex)
            return;

        const unsigned version = m_renderIndex->GetChangeTracker().GetRprimIndexVersion();
        if (version == m_rprimPathCacheVersion)
            return;

        m_rprimPathsById.clear();
        const SdfPathVector& rprimIds = m_renderIndex->GetRprimIds();
        m_rprimPathsById.reserve(rprimIds.size());
        for (const SdfPath& path : rprimIds) {
            const HdRprim* rprim = m_renderIndex->GetRprim(path);
            if (!rprim)
                continue;

            const int32_t id = rprim->GetPrimId();
            if (id >= 0)
                m_rprimPathsById.emplace(id, path);
        }

        m_rprimPathCacheVersion = version;
    }

    bool ensureResources(const GfVec2i& size, const TfToken& sceneIdShaderPath)
    {
        if (size[0] <= 0 || size[1] <= 0 || sceneIdShaderPath.IsEmpty())
            return false;

        if (m_size != size) {
            cleanupBuffers();
            m_size = GfVec2i(0);
        }

        if (m_aovBuffers.empty() && !createBuffers(size))
            return false;

        if (!ensureRenderPass(sceneIdShaderPath))
            return false;

        m_size = size;
        m_sceneIdShaderPath = sceneIdShaderPath;
        return true;
    }

    bool createBuffers(const GfVec2i& size)
    {
        auto registry = std::static_pointer_cast<HdStResourceRegistry>(m_renderIndex->GetResourceRegistry());
        if (!registry)
            return false;

        // SceneId.glslfx is the same shallow-pick shader used by Hydra-style
        // picking. Its integer fragment outputs are declared in this order:
        // primId, instanceId, elementId, edgeId, pointId. Keep the bound color
        // AOVs in the exact same order. Binding only primId + elementId causes
        // the second attachment to receive instanceId (normally 0), which is
        // why every face previously appeared to have elementId 0.
        const TfToken aovs[] = { HdAovTokens->primId, HdAovTokens->instanceId, HdAovTokens->elementId,
                                 HdAovTokens->edgeId, HdAovTokens->pointId,    HdAovTokens->depth };
        const char* names[] = { "primId", "instanceId", "elementId", "edgeId", "pointId", "depth" };

        for (size_t i = 0; i < 6; ++i) {
            HdAovDescriptor desc = m_renderIndex->GetRenderDelegate()->GetDefaultAovDescriptor(aovs[i]);

            // The five picking IDs are signed integer render targets. Be
            // explicit because these buffers are single-sample on Metal.
            if (i < 5) {
                desc.format = HdFormatInt32;
                desc.multiSampled = false;
                desc.clearValue = VtValue(int32_t(-1));
            }
            else {
                desc.format = HdFormatFloat32;
                desc.multiSampled = false;
                desc.clearValue = VtValue(1.0f);
            }

            const SdfPath id(std::string("/__Stageviz/SceneIds/") + names[i]);
            auto buffer = std::make_unique<HdStRenderBuffer>(registry.get(), id);
            if (!buffer->Allocate(GfVec3i(size[0], size[1], 1), desc.format, false)) {
                cleanupBuffers();
                return false;
            }

            HdRenderPassAovBinding binding;
            binding.aovName = aovs[i];
            binding.renderBufferId = id;
            binding.renderBuffer = buffer.get();
            binding.aovSettings = desc.aovSettings;
            binding.clearValue = desc.clearValue;

            m_aovBuffers.push_back(std::move(buffer));
            m_aovBindings.push_back(binding);
        }
        return true;
    }

    bool ensureRenderPass(const TfToken& sceneIdShaderPath)
    {
        if (!m_renderPass) {
            m_renderPass = m_renderIndex->GetRenderDelegate()->CreateRenderPass(
                m_renderIndex, sceneCollection(m_settings.roots, m_settings.reprSelector));
            if (!m_renderPass)
                return false;
        }

        if (!m_renderPassState || m_sceneIdShaderPath != sceneIdShaderPath) {
            m_renderPassState = m_renderIndex->GetRenderDelegate()->CreateRenderPassState();
            auto* state = dynamic_cast<HdStRenderPassState*>(m_renderPassState.get());
            if (!state)
                return false;

            auto glslfx = std::make_shared<HioGlslfx>(sceneIdShaderPath, HioGlslfxTokens->defVal);
            state->SetRenderPassShader(std::make_shared<HdStRenderPassShader>(glslfx));
        }
        return true;
    }

    void configure()
    {
        auto* state = dynamic_cast<HdStRenderPassState*>(m_renderPassState.get());
        if (!state || !m_renderPass)
            return;

        state->SetStencilEnabled(false);
        state->SetEnableDepthTest(true);
        state->SetEnableDepthMask(true);
        state->SetDepthFunc(HdCmpFuncLEqual);
        state->SetAlphaThreshold(0.0001f);
        state->SetAlphaToCoverageEnabled(false);
        state->SetBlendEnabled(false);
        // HVT's OutlinePrimIdsTask uses HdCullStyleNothing for the same reason:
        // reversed single-sided geometry must still generate an ID silhouette.
        state->SetCullStyle(HdCullStyleNothing);
        state->SetLightingEnabled(false);
        state->SetConservativeRasterizationEnabled(false);
        state->SetMultiSampleEnabled(false);

        const float stepSize = m_renderIndex->GetRenderDelegate()->GetRenderSetting<float>(
            HdStRenderSettingsTokens->volumeRaymarchingStepSize, HdStVolume::defaultStepSize);
        const float stepSizeLighting = m_renderIndex->GetRenderDelegate()->GetRenderSetting<float>(
            HdStRenderSettingsTokens->volumeRaymarchingStepSizeLighting, HdStVolume::defaultStepSizeLighting);
        state->SetVolumeRenderingConstants(stepSize, stepSizeLighting);

        std::vector<GfVec4d> clipPlanes;
        state->SetCameraFramingState(m_settings.viewMatrix, m_settings.projectionMatrix, m_settings.viewport,
                                     clipPlanes);

        m_renderPass->SetRprimCollection(sceneCollection(m_settings.roots, m_settings.reprSelector));
        m_renderPass->Sync();
    }

    void cleanupBuffers()
    {
        if (m_renderIndex) {
            HdRenderParam* renderParam = m_renderIndex->GetRenderDelegate()->GetRenderParam();
            for (const auto& buffer : m_aovBuffers) {
                if (buffer)
                    buffer->Finalize(renderParam);
            }
        }
        m_aovBuffers.clear();
        m_aovBindings.clear();
    }

    void cleanup()
    {
        cleanupBuffers();
        m_renderPass.reset();
        m_renderPassState.reset();
        m_size = GfVec2i(0);
    }

    HgiTextureHandle textureHandle(size_t index) const
    {
        if (index >= m_aovBindings.size() || !m_aovBindings[index].renderBuffer)
            return {};

        const VtValue resource = m_aovBindings[index].renderBuffer->GetResource(false);
        if (!resource.IsHolding<HgiTextureHandle>())
            return {};
        return resource.UncheckedGet<HgiTextureHandle>();
    }

    HdRenderIndex* m_renderIndex = nullptr;
    SceneIdSettings m_settings;
    HdRenderPassSharedPtr m_renderPass;
    HdRenderPassStateSharedPtr m_renderPassState;
    std::vector<std::unique_ptr<HdStRenderBuffer>> m_aovBuffers;
    HdRenderPassAovBindingVector m_aovBindings;
    GfVec2i m_size = GfVec2i(0);
    TfToken m_sceneIdShaderPath;

    SdfPathVector m_selectedPaths;
    std::vector<int32_t> m_selectedPrimIds;
    unsigned m_selectedRprimIndexVersion = std::numeric_limits<unsigned>::max();

    std::vector<SelectionSubsetSettings> m_selectedSubsetSettings;
    std::vector<ResolvedSubsetSelection> m_selectedSubsetElements;
    unsigned m_selectedSubsetRprimIndexVersion = std::numeric_limits<unsigned>::max();

    std::unordered_map<int32_t, SdfPath> m_rprimPathsById;
    unsigned m_rprimPathCacheVersion = std::numeric_limits<unsigned>::max();
};

bool
RenderTaskParams::enabled() const
{
    return ambientOcclusion.enabled || sceneIds.enabled || selectionOutline.enabled || captureGpuPick || captureVisible
           || captureElementId;
}

bool
operator==(const RenderTaskParams& lhs, const RenderTaskParams& rhs)
{
    const auto& a = lhs.ambientOcclusion;
    const auto& b = rhs.ambientOcclusion;
    const auto& ia = lhs.sceneIds;
    const auto& ib = rhs.sceneIds;
    const auto& sa = lhs.selectionOutline;
    const auto& sb = rhs.selectionOutline;

    return a.enabled == b.enabled && a.contactAmount == b.contactAmount && a.contactRadius == b.contactRadius
           && a.broadAmount == b.broadAmount && a.broadRadius == b.broadRadius && a.normalBias == b.normalBias
           && a.falloff == b.falloff && a.contrast == b.contrast && a.edgeSharpness == b.edgeSharpness
           && a.blurEnabled == b.blurEnabled && a.blurRadius == b.blurRadius && a.quality == b.quality
           && a.debugMode == b.debugMode && ia.enabled == ib.enabled && ia.roots == ib.roots
           && ia.renderTags == ib.renderTags && ia.reprSelector == ib.reprSelector && ia.size == ib.size
           && ia.viewport == ib.viewport && ia.viewMatrix == ib.viewMatrix && ia.projectionMatrix == ib.projectionMatrix
           && sa.enabled == sb.enabled && sa.paths == sb.paths && sa.subsets == sb.subsets && sa.color == sb.color
           && sa.radius == sb.radius && lhs.captureVisible == rhs.captureVisible
           && lhs.captureElementId == rhs.captureElementId && lhs.captureElementPixel == rhs.captureElementPixel
           && lhs.captureGpuPick == rhs.captureGpuPick && lhs.gpuPickPixel == rhs.gpuPickPixel
           && lhs.gpuPickExcludes == rhs.gpuPickExcludes
           && lhs.captureProjectionMatrices == rhs.captureProjectionMatrices && lhs.nearClip == rhs.nearClip
           && lhs.farClip == rhs.farClip && lhs.orthographic == rhs.orthographic
           && lhs.projectionMatrix == rhs.projectionMatrix && lhs.shaderPath == rhs.shaderPath
           && lhs.sceneIdShaderPath == rhs.sceneIdShaderPath;
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
        << ao.contactRadius << "px, broad=" << ao.broadAmount << "@" << ao.broadRadius
        << "px, quality=" << static_cast<int>(ao.quality) << ", sceneIds=" << params.sceneIds.enabled
        << ", selectionOutline=" << params.selectionOutline.enabled
        << ", selectedPaths=" << params.selectionOutline.paths.size()
        << ", selectedSubsets=" << params.selectionOutline.subsets.size()
        << ", selectionRadius=" << params.selectionOutline.radius << ", captureVisible=" << params.captureVisible
        << ", captureElementId=" << params.captureElementId
        << ", captureTiles=" << params.captureProjectionMatrices.size() << ", nearClip=" << params.nearClip
        << ", farClip=" << params.farClip << ", orthographic=" << params.orthographic << ")";
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

SdfPathVector
RenderTask::takeCapturedVisiblePaths()
{
    SdfPathVector paths;
    paths.swap(m_capturedVisiblePaths);
    return paths;
}

GpuPickResult
RenderTask::takeGpuPickResult()
{
    GpuPickResult result = m_gpuPickResult;
    m_gpuPickResult = GpuPickResult {};
    return result;
}

int32_t
RenderTask::takeCapturedElementId()
{
    const int32_t value = m_capturedElementId;
    m_capturedElementId = -1;
    return value;
}

void
RenderTask::_Sync(HdSceneDelegate* delegate, HdTaskContext* ctx, HdDirtyBits* dirtyBits)
{
    (void)ctx;

    if ((*dirtyBits) & HdChangeTracker::DirtyParams) {
        RenderTaskParams params;
        if (_GetTaskParams(delegate, &params)) {
            m_params = params;

            if (!m_sceneIdPass)
                m_sceneIdPass = std::make_unique<SceneIdPass>(&delegate->GetRenderIndex());

            if (m_sceneIdPass && !m_sceneIdPass->sync(m_params.sceneIds, m_params.sceneIdShaderPath)) {
                m_params.sceneIds.enabled = false;
                m_params.selectionOutline.enabled = false;
                m_params.captureVisible = false;
                m_params.captureElementId = false;
                m_params.captureGpuPick = false;
            }
        }
    }

    *dirtyBits = HdChangeTracker::Clean;
}

void
RenderTask::Prepare(HdTaskContext* ctx, HdRenderIndex* renderIndex)
{
    (void)ctx;

    if (!m_params.enabled())
        return;

    if ((m_params.ambientOcclusion.enabled || m_params.selectionOutline.enabled) && !m_shader) {
        if (Hgi* hgi = _GetHgi())
            m_shader = std::make_unique<Shader>(hgi);
    }

    if (m_sceneIdPass)
        m_sceneIdPass->prepare(renderIndex);

    if (m_params.selectionOutline.enabled && m_sceneIdPass && m_shader) {
        const std::vector<int32_t> ids = m_sceneIdPass->selectedPrimIds(m_params.selectionOutline.paths);
        const std::vector<ResolvedSubsetSelection> subsets = m_sceneIdPass->selectedSubsetElements(
            m_params.selectionOutline.subsets);
        m_shader->ensureSelectedMask(ids);
        m_shader->ensureSelectedSubsetMask(subsets);
    }
    else if (m_shader) {
        m_shader->ensureSelectedMask({});
        m_shader->ensureSelectedSubsetMask({});
    }
}

void
RenderTask::Execute(HdTaskContext* ctx)
{
    if (!ctx || !m_params.enabled())
        return;

    HgiTextureHandle scenePrimId;
    HgiTextureHandle sceneElementId;
    const bool needsViewportSceneIds = m_params.selectionOutline.enabled || m_params.captureElementId
                                       || (m_params.captureVisible && m_params.captureProjectionMatrices.empty());
    if (m_params.sceneIds.enabled && m_sceneIdPass && needsViewportSceneIds) {
        scenePrimId = m_sceneIdPass->execute();
        sceneElementId = m_sceneIdPass->elementIdTexture();
    }

    if (m_params.captureElementId && m_sceneIdPass && scenePrimId)
        m_capturedElementId = m_sceneIdPass->elementIdAt(m_params.captureElementPixel);
    else if (m_params.captureElementId)
        m_capturedElementId = -1;

    if (m_params.ambientOcclusion.enabled && m_shader && !m_params.shaderPath.IsEmpty()) {
        HgiTextureHandle inputColor;
        HgiTextureHandle depth;
        if (_GetTaskContextData(ctx, HdAovTokens->color, &inputColor) && inputColor
            && _GetTaskContextData(ctx, HdAovTokens->depth, &depth) && depth) {
            const HgiTextureDesc& colorDesc = inputColor->GetDescriptor();
            const HgiTextureDesc& depthDesc = depth->GetDescriptor();
            const GfVec2i screenSize(std::max(1, colorDesc.dimensions[0]), std::max(1, colorDesc.dimensions[1]));

            if (m_shader->ensureAoTextures(screenSize)
                && m_shader->ensureAoPrograms(m_params.shaderPath, colorDesc.format, depthDesc.format)) {
                const float nearClip = std::max(1e-5f, m_params.nearClip);
                const float farClip = std::max(nearClip + 1e-4f, m_params.farClip);
                const GfMatrix4f& p = m_params.projectionMatrix;
                const float p00 = p[0][0];
                const float p11 = p[1][1];

                if (std::abs(p00) >= 1e-8f && std::abs(p11) >= 1e-8f) {
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
                    if (_GetTaskContextData(ctx, HdAovTokens->color, &outputColor) && outputColor
                        && outputColor != inputColor) {
                        CompositeConstants compositeConstants;
                        compositeConstants.screenSize = screenSize;
                        compositeConstants.debugMode = static_cast<int>(ao.debugMode);

                        m_shader->composite.BindTextures({ inputColor, m_shader->aoA });
                        m_shader->composite.SetShaderConstants(sizeof(compositeConstants), &compositeConstants);
                        m_shader->composite.Draw(outputColor, HgiTextureHandle());
                    }
                    else {
                        _ToggleRenderTarget(ctx);
                    }
                }
            }
        }
    }

    if (m_params.selectionOutline.enabled && m_shader && scenePrimId && sceneElementId && m_shader->selectedIdMask
        && m_shader->selectedSubsetPrimMask && m_shader->selectedSubsetElementMask && !m_params.shaderPath.IsEmpty()) {
        HgiTextureHandle inputColor;
        if (_GetTaskContextData(ctx, HdAovTokens->color, &inputColor) && inputColor) {
            const HgiTextureDesc& colorDesc = inputColor->GetDescriptor();
            const HgiTextureDesc& sceneIdDesc = scenePrimId->GetDescriptor();
            const GfVec2i screenSize(std::max(1, colorDesc.dimensions[0]), std::max(1, colorDesc.dimensions[1]));

            if (m_shader->ensureSelectionProgram(m_params.shaderPath, colorDesc.format, sceneIdDesc.format)) {
                _ToggleRenderTarget(ctx);

                HgiTextureHandle outputColor;
                if (_GetTaskContextData(ctx, HdAovTokens->color, &outputColor) && outputColor
                    && outputColor != inputColor) {
                    SelectionConstants constants;
                    constants.color = m_params.selectionOutline.color;
                    constants.screenSize = screenSize;
                    constants.selectedMaskSize = m_shader->selectedMaskSize;
                    constants.selectedSubsetPrimMaskSize = m_shader->selectedSubsetPrimMaskSize;
                    constants.selectedSubsetElementMaskSize = m_shader->selectedSubsetElementMaskSize;
                    constants.selectedMaskCapacity = m_shader->selectedMaskCapacity;
                    constants.selectedSubsetPrimMaskCapacity = m_shader->selectedSubsetPrimMaskCapacity;
                    constants.selectedSubsetElementCapacity = m_shader->selectedSubsetElementCapacity;
                    constants.radius = static_cast<int>(
                        std::clamp(m_params.selectionOutline.radius, 1u, kMaxSelectionRadius));

                    m_shader->selection.BindTextures({ inputColor, scenePrimId, sceneElementId,
                                                       m_shader->selectedIdMask, m_shader->selectedSubsetPrimMask,
                                                       m_shader->selectedSubsetElementMask });
                    m_shader->selection.SetShaderConstants(sizeof(constants), &constants);
                    m_shader->selection.Draw(outputColor, HgiTextureHandle());
                }
                else {
                    _ToggleRenderTarget(ctx);
                }
            }
        }
    }

    // Run the expensive thorough visibility scan only after the normal scene-ID
    // texture has been consumed by the outline. The scan reuses and overwrites
    // that ID buffer for each narrowed tile, so doing it earlier would corrupt
    // the current frame's selection mask.
    if (m_params.captureVisible && m_sceneIdPass) {
        m_capturedVisiblePaths = m_sceneIdPass->captureVisiblePaths(m_params.captureProjectionMatrices,
                                                                    static_cast<bool>(scenePrimId));
    }
    else if (m_params.captureVisible) {
        m_capturedVisiblePaths.clear();
    }

    // Execute picking after the outline has consumed the unmodified ID image.
    if (m_params.captureGpuPick && m_sceneIdPass && m_params.sceneIds.enabled)
        m_gpuPickResult = m_sceneIdPass->pickAt(m_params.gpuPickPixel, m_params.gpuPickExcludes);
    else if (m_params.captureGpuPick)
        m_gpuPickResult = GpuPickResult {};
}

}  // namespace stageviz
