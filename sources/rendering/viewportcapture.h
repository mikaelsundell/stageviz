// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 - present Mikael Sundell

#pragma once

#include <QColor>
#include <QImage>
#include <QList>
#include <QSize>
#include <cstdint>
#include <pxr/usd/sdf/path.h>
#include <vector>

PXR_NAMESPACE_USING_DIRECTIVE

namespace stageviz {

/**
 * @brief Owned object-ID image and USD path lookup for a single viewport frame.
 *
 * Indices are row-major with a top-left origin, matching QImage. Background
 * is -1; other values index paths. Native instances resolve to stage paths.
 */
struct ViewportObjectIds {
    QSize size;
    QList<SdfPath> paths;
    std::vector<int32_t> indices;
};

/**
 * @brief Durable viewport color and object data, independent of USD and GL lifetimes.
 */
struct ViewportCapture {
    QImage image;
    ViewportObjectIds objects;

    /**
     * @brief Returns the visible pixel count for each entry in the path lookup.
     */
    std::vector<size_t> pixelCounts() const;

    /**
     * @brief Finds objects with matching displayed RGB pixels.
     *
     * @param color Reference color in displayed 8-bit RGB.
     * @param tolerance Maximum difference in any RGB channel, from 0 to 255.
     * @param minPixels Minimum matching pixels for each object.
     * @param minCoverage Minimum matching fraction of the object's visible pixels.
     */
    QList<SdfPath> pathsByColor(const QColor& color, double tolerance, size_t minPixels, double minCoverage) const;

    /**
     * @brief Finds objects by displayed hue, with wraparound at red.
     *
     * Hue and tolerance use degrees; saturation, value and coverage use 0..1.
     * Pixel counts refer only to visible pixels in this captured frame.
     */
    QList<SdfPath> pathsByHue(double hue, double tolerance, double minSaturation, double minValue, size_t minPixels,
                              double minCoverage) const;
};

}  // namespace stageviz
