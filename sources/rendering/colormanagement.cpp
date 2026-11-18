// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 - present Mikael Sundell
#include "colormanagement.h"
#include "perfscope.h"
#include <OpenColorIO/OpenColorIO.h>
#include <QDebug>
#include <QHash>
#include <QMutex>
#include <QMutexLocker>
#include <cmath>
#include <pxr/imaging/hdx/fullscreenShader.h>
#include <pxr/imaging/hgi/hgi.h>
#include <pxr/imaging/hgi/sampler.h>
#include <pxr/imaging/hgi/shaderFunctionDesc.h>
#include <pxr/imaging/hgi/tokens.h>
#include <string>
#include <vector>

namespace OCIO = OCIO_NAMESPACE;

namespace stageviz {
class ColorTransformPrivate {
public:
    explicit ColorTransformPrivate(Hgi* hgi);
    ~ColorTransformPrivate();
    void clear();
    void addTexture(HgiShaderFunctionDesc& fragment, std::string& code, std::string& arguments, const char* textureName,
                    const char* samplerName, const float* values, const GfVec3i& size, int dimensions, int channels,
                    OCIO::Interpolation interpolation);

    static void replaceAll(std::string& text, const std::string& from, const std::string& to);

    static OCIO::ConstConfigRcPtr config();
    static QString configName();
    static QStringList views();
    static OCIO::ConstCPUProcessorRcPtr displayProcessor(const QString& view);
    static QImage displayImage(const QImage& linear, const QString& view);

    struct Data {
        Hgi* hgi = nullptr;
        std::unique_ptr<HdxFullscreenShader> shader;
        HgiTextureHandleVector textures;
        HgiSamplerHandleVector samplers;
        QString view;
        HgiFormat format = HgiFormatInvalid;
        bool valid = false;
    } d;
};

ColorTransformPrivate::ColorTransformPrivate(Hgi* hgi) { d.hgi = hgi; }

ColorTransformPrivate::~ColorTransformPrivate() { clear(); }

void
ColorTransformPrivate::clear()
{
    d.shader.reset();
    for (auto& texture : d.textures)
        d.hgi->DestroyTexture(&texture);
    for (auto& sampler : d.samplers)
        d.hgi->DestroySampler(&sampler);
    d.textures.clear();
    d.samplers.clear();
}

void
ColorTransformPrivate::addTexture(HgiShaderFunctionDesc& fragment, std::string& code, std::string& arguments,
                                  const char* textureName, const char* samplerName, const float* values,
                                  const GfVec3i& size, int dimensions, int channels, OCIO::Interpolation interpolation)
{
    const size_t pixels = size_t(size[0]) * size[1] * size[2];
    std::vector<float> rgba;
    if (channels == 3) {
        rgba.resize(pixels * 4);
        for (size_t i = 0; i < pixels; ++i) {
            for (int c = 0; c < 3; ++c)
                rgba[i * 4 + c] = values[i * 3 + c];
            rgba[i * 4 + 3] = 1.0f;
        }
        values = rgba.data();
        channels = 4;
    }
    HgiTextureDesc texture;
    texture.debugName = textureName;
    texture.type = dimensions == 3 ? HgiTextureType3D : dimensions == 1 ? HgiTextureType1D : HgiTextureType2D;
    texture.dimensions = size;
    texture.format = channels == 1 ? HgiFormatFloat32 : HgiFormatFloat32Vec4;
    texture.usage = HgiTextureUsageBitsShaderRead;
    texture.initialData = values;
    texture.pixelsByteSize = pixels * channels * sizeof(float);
    d.textures.push_back(d.hgi->CreateTexture(texture));
    HgiSamplerDesc sampler;
    sampler.debugName = samplerName;
    sampler.magFilter = sampler.minFilter = interpolation == OCIO::INTERP_NEAREST ? HgiSamplerFilterNearest
                                                                                  : HgiSamplerFilterLinear;
    sampler.addressModeU = sampler.addressModeV = sampler.addressModeW = HgiSamplerAddressModeClampToEdge;
    d.samplers.push_back(d.hgi->CreateSampler(sampler));
    HgiShaderFunctionAddTexture(&fragment, textureName, uint32_t(d.textures.size()), dimensions, texture.format);
    if (d.hgi->GetAPIName() == HgiTokens->Metal) {
        arguments += "textureBind_" + std::string(textureName) + ", samplerBind_" + textureName + ", ";
    }
    else {
        // OCIO declares unbound samplers; Hgi supplies declarations with explicit bindings.
        const auto at = code.find(samplerName);
        if (at != std::string::npos) {
            const auto begin = code.rfind('\n', at);
            const auto end = code.find('\n', at);
            code.erase(begin == std::string::npos ? 0 : begin,
                       (end == std::string::npos ? code.size() : end) - (begin == std::string::npos ? 0 : begin));
        }
        replaceAll(code, samplerName, textureName);
    }
}


OCIO::ConstConfigRcPtr
ColorTransformPrivate::config()
{
    static const auto value = OCIO::Config::CreateFromBuiltinConfig(configName().toUtf8().constData());
    return value;
}

QString
ColorTransformPrivate::configName()
{
    return QStringLiteral("studio-config-v2.2.0_aces-v1.3_ocio-v2.4");
}

QStringList
ColorTransformPrivate::views()
{
    QStringList result;
    const auto value = config();
    for (int i = 0; i < value->getNumViews("sRGB - Display"); ++i)
        result.append(QString::fromUtf8(value->getView("sRGB - Display", i)));
    return result;
}

void
ColorTransformPrivate::replaceAll(std::string& text, const std::string& from, const std::string& to)
{
    size_t offset = 0;
    while ((offset = text.find(from, offset)) != std::string::npos) {
        text.replace(offset, from.size(), to);
        offset += to.size();
    }
}

OCIO::ConstCPUProcessorRcPtr
ColorTransformPrivate::displayProcessor(const QString& view)
{
    // Share the four immutable view processors across material renderers. OCIO's
    // CPU optimization/LUT setup must not repeat for every visible shaderball.
    static QMutex mutex;
    static QHash<QString, OCIO::ConstCPUProcessorRcPtr> processors;
    QMutexLocker lock(&mutex);
    const auto cached = processors.constFind(view);
    if (cached != processors.cend())
        return cached.value();
    const auto transform = OCIO::DisplayViewTransform::Create();
    transform->setSrc("Linear Rec.709 (sRGB)");
    transform->setDisplay("sRGB - Display");
    transform->setView(view.toUtf8().constData());
    const auto processor = config()->getProcessor(transform)->getDefaultCPUProcessor();
    processors.insert(view, processor);
    return processor;
}

QImage
ColorTransformPrivate::displayImage(const QImage& linear, const QString& view)
{
    if (linear.isNull() || linear.format() != QImage::Format_RGBA32FPx4_Premultiplied)
        return {};
    PerfScope scope("material.swatch.display_transform");
    try {
        QImage image = linear.copy();
        for (int y = 0; y < image.height(); ++y) {
            auto* row = reinterpret_cast<float*>(image.scanLine(y));
            for (int x = 0; x < image.width(); ++x) {
                float* pixel = row + x * 4;
                for (int c = 0; c < 3; ++c)
                    pixel[c] = pixel[3] > 0.0f ? pixel[c] / pixel[3] : 0.0f;
            }
        }
        if (view != QStringLiteral("Raw")) {
            const auto processor = displayProcessor(view);
            OCIO::PackedImageDesc pixels(image.bits(), image.width(), image.height(), 4, OCIO::BIT_DEPTH_F32,
                                         sizeof(float), 4 * sizeof(float), image.bytesPerLine());
            processor->apply(pixels);
        }
        for (int y = 0; y < image.height(); ++y) {
            auto* row = reinterpret_cast<float*>(image.scanLine(y));
            for (int x = 0; x < image.width(); ++x)
                for (int c = 0; c < 3; ++c)
                    row[x * 4 + c] *= row[x * 4 + 3];
        }
        return image.convertToFormat(QImage::Format_RGBA8888_Premultiplied);
    } catch (const std::exception& error) {
        qWarning() << "Material OCIO transform failed:" << error.what();
        return {};
    }
}

QImage
colorManagementImage(const QImage& linear, const QString& view)
{
    return ColorTransformPrivate::displayImage(linear, view);
}

QString
colorManagementConfig()
{
    return ColorTransformPrivate::configName();
}

QStringList
colorManagementViews()
{
    return ColorTransformPrivate::views();
}

ColorTransform::ColorTransform(Hgi* hgi)
    : p(new ColorTransformPrivate(hgi))
{}
ColorTransform::~ColorTransform() = default;

bool
ColorTransform::prepare(const ColorManagementSettings& settings, HgiFormat format)
{
    if (p->d.view == settings.view && p->d.format == format)
        return p->d.valid;
    PerfScope scope("color_management.program.create");
    p->clear();
    p->d.view = settings.view;
    p->d.format = format;
    p->d.valid = false;
    try {
        HgiShaderFunctionDesc fragment;
        fragment.debugName = "Stageviz OCIO Display";
        fragment.shaderStage = HgiShaderStageFragment;
        HgiShaderFunctionAddStageInput(&fragment, "uvOut", "vec2");
        HgiShaderFunctionAddStageOutput(&fragment, "hd_FragColor", "vec4");
        HgiShaderFunctionAddTexture(&fragment, "colorIn", 0, 2, format);
        HgiShaderFunctionAddConstantParam(&fragment, "uExposureScale", "float");
        HgiShaderFunctionAddConstantParam(&fragment, "uInverseGamma", "float");
        std::string code;
        std::string arguments;
        if (settings.view != QStringLiteral("Raw")) {
            const auto transform = OCIO::DisplayViewTransform::Create();
            transform->setSrc("Linear Rec.709 (sRGB)");
            transform->setDisplay("sRGB - Display");
            transform->setView(settings.view.toUtf8().constData());
            const auto processor = p->config()->getProcessor(transform)->getDefaultGPUProcessor();
            auto desc = OCIO::GpuShaderDesc::CreateShaderDesc();
            desc->setFunctionName("stagevizDisplay");
            desc->setResourcePrefix("stagevizOcio");
            desc->setLanguage(p->d.hgi->GetAPIName() == HgiTokens->Metal ? OCIO::GPU_LANGUAGE_MSL_2_0
                                                                         : OCIO::GPU_LANGUAGE_GLSL_4_0);
            processor->extractGpuShaderInfo(desc);
            // The pinned built-in views contain static transforms. Exposure/gamma
            // are separate shader constants so adjusting them never recompiles OCIO.
            if (desc->getNumUniforms() != 0)
                throw OCIO::Exception("Unexpected dynamic uniforms in bundled OCIO view");
            code = desc->getShaderText();
            for (unsigned i = 0; i < desc->getNum3DTextures(); ++i) {
                const char *name, *sampler;
                unsigned edge;
                OCIO::Interpolation interpolation;
                const float* values;
                desc->get3DTexture(i, name, sampler, edge, interpolation);
                desc->get3DTextureValues(i, values);
                p->addTexture(fragment, code, arguments, name, sampler, values, GfVec3i(edge), 3, 3, interpolation);
            }
            for (unsigned i = 0; i < desc->getNumTextures(); ++i) {
                const char *name, *sampler;
                unsigned width, height;
                OCIO::GpuShaderCreator::TextureType channels;
                OCIO::GpuShaderCreator::TextureDimensions dimensions;
                OCIO::Interpolation interpolation;
                const float* values;
                desc->getTexture(i, name, sampler, width, height, channels, dimensions, interpolation);
                desc->getTextureValues(i, values);
                p->addTexture(fragment, code, arguments, name, sampler, values, GfVec3i(width, height, 1),
                              dimensions == OCIO::GpuShaderCreator::TEXTURE_1D ? 1 : 2,
                              channels == OCIO::GpuShaderCreator::TEXTURE_RED_CHANNEL ? 1 : 3, interpolation);
            }
        }
        code += "\nvoid main() {\nvec4 color = HgiGet_colorIn(uvOut);\nfloat alpha = color.a;\n";
        // Hydra resolves coverage into premultiplied RGBA. Transform straight
        // RGB, then restore coverage so transparent surfaces do not get dark rims.
        code += "if (alpha > 0.0) { color.rgb /= alpha; }\ncolor.rgb *= uExposureScale;\n";
        if (settings.view != QStringLiteral("Raw"))
            code += "color = stagevizDisplay(" + arguments + "color);\n";
        code
            += "if (uInverseGamma != 1.0) { color.rgb = sign(color.rgb) * pow(abs(color.rgb), vec3(uInverseGamma)); }\n";
        code += "hd_FragColor = vec4(color.rgb * alpha, alpha);\n}\n";
        fragment.shaderCode = code.c_str();
        p->d.shader = std::make_unique<HdxFullscreenShader>(p->d.hgi, "Stageviz OCIO Display");
        HgiDepthStencilState depth;
        depth.depthTestEnabled = depth.depthWriteEnabled = false;
        p->d.shader->SetDepthState(depth);
        p->d.shader->SetProgram(fragment);
        p->d.valid = true;
    } catch (const std::exception& error) {
        qWarning() << "Viewport OCIO transform failed:" << error.what();
    }
    return p->d.valid;
}

void
ColorTransform::draw(const HgiTextureHandle& input, const HgiTextureHandle& output,
                     const ColorManagementSettings& settings)
{
    const float constants[] = { std::exp2(settings.exposure), 1.0f / settings.gamma };
    HgiTextureHandleVector textures { input };
    textures.insert(textures.end(), p->d.textures.begin(), p->d.textures.end());
    HgiSamplerHandleVector samplers(1);
    samplers.insert(samplers.end(), p->d.samplers.begin(), p->d.samplers.end());
    p->d.shader->BindTextures(textures, samplers);
    p->d.shader->SetShaderConstants(sizeof(constants), constants);
    p->d.shader->Draw(output, HgiTextureHandle());
}
}  // namespace stageviz
