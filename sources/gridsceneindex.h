// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#pragma once

#include <memory>

#include <pxr/base/gf/vec3f.h>
#include <pxr/base/tf/refPtr.h>
#include <pxr/base/tf/token.h>
#include <pxr/imaging/hd/sceneIndex.h>

namespace stageviz {

class GridSceneIndexPrivate;

/**
 * @class GridSceneIndex
 * @brief Hydra scene index providing a viewport-only ground grid.
 *
 * Provides a finite Maya-style grid as synthetic Hydra content. The grid
 * can be enabled independently of the USD stage, remains centered at the
 * world origin, and can be oriented from the USD stage up-axis token.
 */
class GridSceneIndex final : public pxr::HdSceneIndexBase {
public:
    static pxr::TfRefPtr<GridSceneIndex> New();
    ~GridSceneIndex() override;

    /** @name Display */
    ///@{

    /**
    * @brief Returns whether the grid is enabled.
    */
    bool isEnabled() const;

    /**
    * @brief Enables or disables the grid.
    *
    * @param enabled Grid display state.
    */
    void setEnabled(bool enabled);

    /**
    * @brief Returns the regular grid line color.
    */
    pxr::GfVec3f color() const;

    /**
    * @brief Sets the regular grid line color.
    *
    * The center axes remain black.
    *
    * @param color Grid line color.
    */
    void setColor(const pxr::GfVec3f& color);

    ///@}

    /** @name Placement */
    ///@{

    /**
    * @brief Returns the grid up axis.
    */
    pxr::TfToken upAxis() const;

    /**
    * @brief Sets the grid up axis.
    *
    * The grid is oriented to the corresponding ground plane.
    *
    * @param upAxis Up-axis token.
    */
    void setUpAxis(const pxr::TfToken& upAxis);

    ///@}

    /** @name Scene Index */
    ///@{

    /**
    * @brief Returns the scene index prim at the specified path.
    *
    * @param primPath Prim path to query.
    * @return Scene index prim at the path.
    */
    pxr::HdSceneIndexPrim GetPrim(const pxr::SdfPath& primPath) const override;

    /**
    * @brief Returns the child prim paths for the specified path.
    *
    * @param primPath Parent prim path to query.
    * @return Child prim paths below the specified path.
    */
    pxr::SdfPathVector GetChildPrimPaths(const pxr::SdfPath& primPath) const override;

    ///@}

private:
    GridSceneIndex();

private:
    std::unique_ptr<GridSceneIndexPrivate> p;
};

}  // namespace stageviz
