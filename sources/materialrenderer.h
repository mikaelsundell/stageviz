// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#pragma once

#include "materialutils.h"
#include "stageviz.h"
#include <QImage>
#include <QObject>
#include <pxr/base/vt/value.h>
#include <pxr/usd/sdf/layer.h>

namespace stageviz {

class MaterialRendererPrivate;

/**
 * @class MaterialRenderer
 * @brief Persistent renderer for material swatches and interactive previews.
 *
 * MaterialRenderer keeps warm Hydra/Storm render contexts for complete
 * UsdShade and MaterialX networks. Final swatches use the high-quality render
 * path, while interactive edits use a smaller preview context. Non-structural
 * edits are mirrored through lightweight attribute overrides so the contexts
 * can remain alive between updates.
 */
class MaterialRenderer : public QObject {
    Q_OBJECT
public:
    /**
     * @brief Constructs a material renderer.
     */
    explicit MaterialRenderer(QObject* parent = nullptr);

    /**
     * @brief Releases the renderer and its private render contexts.
     */
    virtual ~MaterialRenderer();

    /**
     * @brief Renders a material from canonical fallback parameters.
     *
     * This path is retained for fallback rendering. Prefer the source-layer
     * overload for complete authored material networks.
     */
    void request(const SdfPath& materialPath, const MaterialParameters& parameters, bool forceRender = false);

    /**
     * @brief Renders the complete authored material network.
     *
     * @param materialPath Material prim to render.
     * @param sourceLayer Composed source layer containing the material network.
     * @param forceRender If true, bypass cached render state.
     */
    void request(const SdfPath& materialPath, const SdfLayerRefPtr& sourceLayer, bool forceRender = false);

    /**
     * @brief Renders an interactive preview with one temporary input override.
     *
     * Repeated preview requests are coalesced and rendered through the smaller
     * warm preview context.
     *
     * @param materialPath Material prim to render.
     * @param sourceLayer Composed source layer containing the material network.
     * @param inputPath Shader input property to override.
     * @param value Temporary preview value.
     */
    void preview(const SdfPath& materialPath, const SdfLayerRefPtr& sourceLayer, const SdfPath& inputPath,
                 const VtValue& value);

    /**
     * @brief Mirrors a committed non-structural attribute edit into the warm contexts.
     */
    void syncAttribute(const SdfPath& inputPath, const VtValue& value);

    /**
     * @brief Clears incremental input overrides after a complete source refresh.
     */
    void clearOverrides();

    /**
     * @brief Invalidates the cached render state for a material.
     */
    void invalidate(const SdfPath& materialPath);

    /**
     * @brief Clears renderer state and cached material results.
     */
    void clear();

Q_SIGNALS:
    /**
     * @brief Emitted when a material image has been rendered successfully.
     */
    void rendered(const QString& materialPath, const QImage& image);

    /**
     * @brief Emitted when rendering a material fails.
     */
    void error(const QString& materialPath, const QString& message);

private:
    QScopedPointer<MaterialRendererPrivate> p;
};

}  // namespace stageviz
