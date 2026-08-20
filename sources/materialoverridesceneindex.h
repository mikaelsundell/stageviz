// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#pragma once

#include <memory>
#include <pxr/base/tf/refPtr.h>
#include <pxr/imaging/hd/filteringSceneIndex.h>
#include <pxr/usd/sdf/path.h>

namespace stageviz {

class MaterialOverrideSceneIndexPrivate;

/**
 * @class MaterialOverrideSceneIndex
 * @brief Hydra filtering scene index for viewport material presentation.
 *
 * Filters material bindings on document gprims after scene-index processing
 * and before the render delegate.
 *
 * The filter does not create or own materials. Stageviz-owned materials live
 * on Session::auxiliary() and are presented to Hydra by ImagingGLWidget.
 * Auxiliary display geometry below /Display is always passed through
 * unchanged.
 *
 * The filter can also override the Hydra mesh doubleSided property for
 * viewport diagnostics without modifying the authored USD stage.
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
     */
    static pxr::TfRefPtr<MaterialOverrideSceneIndex> New(const pxr::HdSceneIndexBaseRefPtr& inputSceneIndex);

    /**
     * @brief Destroys the material override scene index.
     */
    ~MaterialOverrideSceneIndex() override;

    /** @name Scene Materials */
    ///@{

    /**
     * @brief Returns whether authored document material bindings are enabled.
     */
    bool sceneMaterialsEnabled() const;

    /**
     * @brief Enables or disables authored document material bindings.
     *
     * When disabled and no explicit override mode is active, document material
     * bindings are blocked so renderer fallback/display color can be used.
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
     * @brief Sets the current material override mode.
     */
    void setMode(Mode mode);

    /**
     * @brief Returns the custom material path used by Custom mode.
     *
     * The path is an absolute prim path on Session::auxiliary().
     */
    pxr::SdfPath materialPath() const;

    /**
     * @brief Sets the custom material path used by Custom mode.
     *
     * The path must identify a material already presented from
     * Session::auxiliary().
     */
    void setMaterialPath(const pxr::SdfPath& materialPath);

    ///@}

    /** @name Double-Sided Override */
    ///@{

    /**
     * @brief Returns whether mesh doubleSided values are overridden.
     */
    bool doubleSidedOverrideEnabled() const;

    /**
     * @brief Enables or disables the mesh doubleSided override.
     *
     * This is a Hydra presentation override only and does not modify the
     * authored USD stage.
     */
    void setDoubleSidedOverrideEnabled(bool enabled);

    /**
     * @brief Returns the current mesh doubleSided override value.
     */
    bool doubleSidedOverride() const;

    /**
     * @brief Sets the mesh doubleSided override value.
     *
     * For front/back diagnostics this should normally be false while render
     * culling is disabled.
     */
    void setDoubleSidedOverride(bool doubleSided);

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
    explicit MaterialOverrideSceneIndex(const pxr::HdSceneIndexBaseRefPtr& inputSceneIndex);

    /**
     * @brief Dirties material binding data on affected gprims.
     */
    void dirtyMaterialBindings();

    /**
     * @brief Dirties mesh doubleSided data on affected meshes.
     */
    void dirtyDoubleSided();

private:
    std::unique_ptr<MaterialOverrideSceneIndexPrivate> p;
};

}  // namespace stageviz
