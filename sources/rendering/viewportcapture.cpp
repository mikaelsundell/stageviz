// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 - present Mikael Sundell

#include "viewportcapture.h"
#include <algorithm>
#include <cmath>

namespace stageviz {
namespace {
    template<typename Predicate>
    QList<SdfPath> matchingPaths(const ViewportCapture& capture, size_t minPixels, double minCoverage,
                                 Predicate matches)
    {
        if (capture.image.isNull() || capture.image.size() != capture.objects.size
            || capture.objects.indices.size() != size_t(capture.image.width()) * capture.image.height())
            return {};
        const auto counts = capture.pixelCounts();
        std::vector<size_t> hits(counts.size(), 0);
        for (int y = 0; y < capture.image.height(); ++y) {
            const auto* row = reinterpret_cast<const QRgb*>(capture.image.constScanLine(y));
            for (int x = 0; x < capture.image.width(); ++x) {
                const int32_t id = capture.objects.indices[size_t(y) * capture.image.width() + x];
                if (id >= 0 && size_t(id) < hits.size() && matches(QColor::fromRgb(row[x])))
                    ++hits[size_t(id)];
            }
        }
        QList<SdfPath> paths;
        for (size_t i = 0; i < hits.size(); ++i)
            if (counts[i] && hits[i] >= minPixels && double(hits[i]) / counts[i] >= minCoverage)
                paths.append(capture.objects.paths.at(qsizetype(i)));
        return paths;
    }
}  // namespace

std::vector<size_t>
ViewportCapture::pixelCounts() const
{
    std::vector<size_t> counts(size_t(objects.paths.size()), 0);
    for (const int32_t id : objects.indices)
        if (id >= 0 && size_t(id) < counts.size())
            ++counts[size_t(id)];
    return counts;
}

QList<SdfPath>
ViewportCapture::pathsByColor(const QColor& color, double tolerance, size_t minPixels, double minCoverage) const
{
    return matchingPaths(*this, minPixels, minCoverage, [&](const QColor& pixel) {
        return std::abs(pixel.red() - color.red()) <= tolerance && std::abs(pixel.green() - color.green()) <= tolerance
               && std::abs(pixel.blue() - color.blue()) <= tolerance;
    });
}

QList<SdfPath>
ViewportCapture::pathsByHue(double hue, double tolerance, double minSaturation, double minValue, size_t minPixels,
                            double minCoverage) const
{
    return matchingPaths(*this, minPixels, minCoverage, [&](const QColor& pixel) {
        const double value = pixel.hsvHueF();
        if (value < 0 || pixel.hsvSaturationF() < minSaturation || pixel.valueF() < minValue)
            return false;
        const double distance = std::abs(value * 360.0 - hue);
        return std::min(distance, 360.0 - distance) <= tolerance;
    });
}
}  // namespace stageviz
