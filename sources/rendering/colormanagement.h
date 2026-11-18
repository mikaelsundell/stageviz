// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 - present Mikael Sundell
#pragma once

#include <QImage>
#include <QStringList>
#include <memory>
#include <pxr/imaging/hgi/hgi.h>
#include <pxr/imaging/hgi/texture.h>

PXR_NAMESPACE_USING_DIRECTIVE

namespace stageviz {

class ColorTransformPrivate;

/**
 * @brief View-only adjustments of scene-linear Rec.709 rendering for an sRGB display.
 */
struct ColorManagementSettings {
    float exposure = 0.0f;
    float gamma = 1.0f;
    QString view = QStringLiteral("Un-tone-mapped");

    bool operator==(const ColorManagementSettings& other) const
    {
        return exposure == other.exposure && gamma == other.gamma && view == other.view;
    }
};

/**
 * @brief Returns views from Stageviz's bundled OCIO configuration for the sRGB display.
 */
QStringList
colorManagementViews();

/**
 * @brief Returns the pinned built-in OCIO configuration used by the viewport.
 */
QString
colorManagementConfig();

/**
 * @brief Converts premultiplied floating-point linear RGBA to a display swatch.
 *
 * Uses the selected OCIO view with neutral exposure and gamma, preserving alpha.
 * Returns a null image for unsupported input formats or a failed transform.
 */
QImage
colorManagementImage(const QImage& linear, const QString& view);

/**
 * @brief Applies OCIO's generated GPU processor with cached Hgi shader and LUT resources.
 *
 * The alpha channel is preserved. Raw bypasses the view transform while retaining
 * exposure and viewing gamma. The caller owns the Hgi and input/output textures.
 */
class ColorTransform final {
public:
    /**
     * @brief Creates a transform for a caller-owned graphics interface.
     */
    explicit ColorTransform(Hgi* hgi);

    /**
     * @brief Releases cached shader, textures and samplers before Hgi destruction.
     */
    ~ColorTransform();

    /**
     * @brief Prepares a view processor, reusing resources for adjustment-only changes.
     */
    bool prepare(const ColorManagementSettings& settings, HgiFormat format);

    /**
     * @brief Transforms premultiplied color into a distinct output texture.
     */
    void draw(const HgiTextureHandle& input, const HgiTextureHandle& output, const ColorManagementSettings& settings);

private:
    std::unique_ptr<ColorTransformPrivate> p;
};

}  // namespace stageviz
