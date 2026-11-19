// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell

#pragma once

#include <QList>
#include <cstdint>
#include <memory>
#include <pxr/usd/sdf/path.h>
#include <pxr/usd/usd/stage.h>

namespace stageviz {

class RenderIsolationPrivate;

/**
 * @brief Maintains a private render population for viewport isolation.
 *
 * Shares source layers without authoring visibility or changing the source
 * population. The private stage is reused across mask changes and retained
 * while inactive so clearing isolation can return to the original full stage.
 */
class RenderIsolation {
public:
    /**
     * @brief Creates an empty render-isolation controller.
     */
    RenderIsolation();

    /**
     * @brief Releases the private population and its source notice listener.
     */
    ~RenderIsolation();

    /**
     * @brief Sets the source stage and releases any previous render population.
     */
    void setStage(const pxr::UsdStageRefPtr& stage);

    /**
     * @brief Sets isolation roots; an empty list restores the complete population.
     */
    void setMask(const QList<pxr::SdfPath>& paths);

    /**
     * @brief Returns the synchronized source or private rendering stage.
     *
     * The caller must hold the source stage's read lock.
     */
    pxr::UsdStageRefPtr stage();

    /**
     * @brief Returns whether isolation is applied through a private population.
     */
    bool active() const;

    /**
     * @brief Returns the source topology and shader-dependency generation.
     */
    uint64_t sourceVersion() const;

private:
    std::unique_ptr<RenderIsolationPrivate> p;
};

}  // namespace stageviz
