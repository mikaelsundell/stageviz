// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#pragma once

#include <memory>

#include <pxr/base/gf/vec3f.h>
#include <pxr/base/tf/refPtr.h>
#include <pxr/imaging/hd/filteringSceneIndex.h>
#include <pxr/usd/sdf/path.h>

namespace stageviz {

class MaterialOverrideSceneIndexPrivate;

/**
 * @class MaterialOverrideSceneIndex
 * @brief Hydra filtering scene index for viewport material presentation.
 *
 * Filters the composed Hydra scene after scene-index merging and before the
 * render delegate. Authored USD material bindings can be blocked so renderer
 * fallback display colors are used, replaced with a built-in clay material,
 * or replaced with a caller-provided material path.
 *
 * Stageviz-owned viewport content under /__stageviz is passed through
 * unchanged so viewport materials such as the grid remain active.
 */
class MaterialOverrideSceneIndex final : public pxr::HdSingleInputFilteringSceneIndexBase {
public:
    /**
     * @brief Material override modes.
     */
    enum Mode {
        None,
        Clay,
        Custom,
    };

    /**
     * @brief Creates a material override filter around an input scene index.
     *
     * @param inputSceneIndex Scene index to filter.
     * @return Reference-counted material override scene index.
     */
    static pxr::TfRefPtr<MaterialOverrideSceneIndex> New(const pxr::HdSceneIndexBaseRefPtr& inputSceneIndex);

    /**
     * @brief Destroys the material override scene index.
     */
    ~MaterialOverrideSceneIndex() override;

    /** @name Scene Materials */
    ///@{

    /**
     * @brief Returns whether authored scene material bindings are enabled.
     */
    bool sceneMaterialsEnabled() const;

    /**
     * @brief Enables or disables authored scene material bindings.
     *
     * When disabled and no explicit override is active, material bindings on
     * non-Stageviz gprims are blocked so renderer fallback display colors can
     * be used.
     *
     * @param enabled Scene material state.
     */
    void setSceneMaterialsEnabled(bool enabled);

    ///@}

    /** @name Override */
    ///@{

    /**
     * @brief Returns the current material override mode.
     */
    Mode mode() const;

    /**
     * @brief Sets the material override mode.
     *
     * @param mode Override mode.
     */
    void setMode(Mode mode);

    /**
     * @brief Returns the custom material path.
     */
    pxr::SdfPath materialPath() const;

    /**
     * @brief Sets the custom material used by Custom mode.
     *
     * The material must exist in the composed Hydra scene. This allows Python
     * to author a material in the USD session layer and pass its prim path to
     * the viewport.
     *
     * @param materialPath Material prim path.
     */
    void setMaterialPath(const pxr::SdfPath& materialPath);

    ///@}

    /** @name Scene Index */
    ///@{

    pxr::HdSceneIndexPrim GetPrim(const pxr::SdfPath& primPath) const override;
    pxr::SdfPathVector GetChildPrimPaths(const pxr::SdfPath& primPath) const override;

    ///@}

protected:
    void _PrimsAdded(const pxr::HdSceneIndexBase& sender,
                     const pxr::HdSceneIndexObserver::AddedPrimEntries& entries) override;

    void _PrimsRemoved(const pxr::HdSceneIndexBase& sender,
                       const pxr::HdSceneIndexObserver::RemovedPrimEntries& entries) override;

    void _PrimsDirtied(const pxr::HdSceneIndexBase& sender,
                       const pxr::HdSceneIndexObserver::DirtiedPrimEntries& entries) override;

    void _PrimsRenamed(const pxr::HdSceneIndexBase& sender,
                       const pxr::HdSceneIndexObserver::RenamedPrimEntries& entries) override;

private:
    void dirtyMaterialBindings();
    void updateClayTopology(bool wasUsingClay);

private:
    explicit MaterialOverrideSceneIndex(const pxr::HdSceneIndexBaseRefPtr& inputSceneIndex);

private:
    std::unique_ptr<MaterialOverrideSceneIndexPrivate> p;
};

}  // namespace stageviz
