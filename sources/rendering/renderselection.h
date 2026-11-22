// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 - present Mikael Sundell
#pragma once

#include "rendertask.h"
#include <memory>
#include <pxr/imaging/hd/sceneIndex.h>
#include <pxr/usd/usd/stage.h>

namespace stageviz {
class RenderSelectionPrivate;

/**
 * @brief Maps USD selections to Hydra drawables and nested instance indices.
 *
 * Owns topology-dependent caches and their scene-index observer. Call after
 * PrepareBatch, on the rendering thread while holding the stage read lock.
 */
class RenderSelection {
public:
    /**
     * @brief Creates an empty mapper.
     */
    RenderSelection();

    /**
     * @brief Detaches the observer and releases cached mappings.
     */
    ~RenderSelection();

    /**
     * @brief Observes the scene index before presentation overrides.
     */
    void setSceneIndex(const pxr::HdSceneIndexBaseRefPtr& sceneIndex);

    /**
     * @brief Returns the mapping generation, changed by topology notifications.
     */
    uint64_t generation() const;

    /**
     * @brief Returns the observed input for read-only render diagnostics.
     */
    const pxr::HdSceneIndexBaseRefPtr& sceneIndex() const;

    /**
     * @brief Separates direct leaf paths from resolved hierarchy/instance selections.
     */
    bool resolve(const pxr::UsdStageRefPtr& stage, const pxr::SdfPathVector& paths, pxr::SdfPathVector* directPaths,
                 std::vector<HydraSelectionRprim>* rprims);

private:
    std::unique_ptr<RenderSelectionPrivate> p;
};
}  // namespace stageviz
