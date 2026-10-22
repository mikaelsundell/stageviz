// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#pragma once

#include <mutex>
#include <pxr/usd/usdGeom/bboxCache.h>

PXR_NAMESPACE_USING_DIRECTIVE

namespace stageviz::bbox {

/**
     * @brief Provides synchronized access to the shared bounding box cache.
     *
     * Acquires an internal mutex for the lifetime of the guard, allowing
     * bounding boxes to be computed using a shared UsdGeomBBoxCache.
     *
     * The cache evaluates bounds at the default USD time and may retain
     * previously computed results to improve performance.
     *
     * The caller must acquire the appropriate stage lock before creating
     * a CacheGuard. The internal mutex protects the shared cache but does
     * not synchronize access to the USD stage itself.
     *
     * Keep the guard alive only for the duration of the bounding box
     * operations to avoid unnecessarily blocking other callers.
     */
class CacheGuard {
public:
    /**
         * @brief Acquires exclusive access to the shared bounding box cache.
         *
         * Blocks until the internal cache mutex becomes available.
         * The caller must already hold the appropriate stage lock.
         */
    CacheGuard();

    /**
         * @brief Releases exclusive access to the shared bounding box cache.
         */
    ~CacheGuard();

    CacheGuard(const CacheGuard&) = delete;
    CacheGuard& operator=(const CacheGuard&) = delete;

    /**
         * @brief Computes the world-space bounding box of a USD prim.
         *
         * Uses the shared default-time bounding box cache to evaluate
         * the prim's bounds, including its composed transforms.
         *
         * The returned GfBBox3d represents the computed world-space
         * bounds and may retain an oriented representation.
         *
         * @param prim USD prim whose world-space bounds are requested.
         *
         * @return Computed world-space bounding box of the prim.
         */
    GfBBox3d ComputeWorldBound(const UsdPrim& prim);

private:
    std::unique_lock<std::mutex> lock_;
};

/**
     * @brief Clears cached bounding box results.
     *
     * Invalidates previously computed bounds so subsequent queries
     * reflect changes to geometry, transforms, visibility, or other
     * properties affecting bounding box evaluation.
     *
     * Call after relevant USD modifications while holding the
     * appropriate stage write lock.
     *
     * The cache is shared across callers, so clearing it affects
     * subsequent bounding box computations globally.
     */
void
clear();

}  // namespace stageviz::bbox
