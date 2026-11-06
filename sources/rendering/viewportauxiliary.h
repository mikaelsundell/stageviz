// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#pragma once

#include "stageviz.h"
#include <QImage>
#include <QPointF>
#include <QScopedPointer>
#include <pxr/base/gf/camera.h>
#include <pxr/base/gf/vec3d.h>
#include <pxr/base/tf/token.h>
#include <pxr/usd/usd/stage.h>

PXR_NAMESPACE_USING_DIRECTIVE

namespace stageviz {

class ViewportAuxiliaryPrivate;
class ViewState;

/**
 * @brief Owns auxiliary grid, inspection materials, and camera-axis presentation.
 */
class ViewportAuxiliary {
public:
    /**
     * @brief Constructs the auxiliary presentation.
     */
    ViewportAuxiliary();

    /**
     * @brief Destroys the auxiliary presentation.
     */
    ~ViewportAuxiliary();

    /**
     * @brief Sets the shared auxiliary stage.
     */
    void setStage(UsdStageRefPtr stage);

    /**
     * @brief Returns the shared auxiliary stage.
     */
    UsdStageRefPtr stage() const;

    /**
     * @brief Sets the grid up axis.
     */
    void setUpAxis(const TfToken& axis);

    /**
     * @brief Returns the grid up axis.
     */
    TfToken upAxis() const;

    /**
     * @brief Updates the grid from the current display settings.
     */
    void updateGrid(ViewState* state);

    /**
     * @brief Creates the built-in inspection materials.
     */
    void ensureMaterials();

    /**
     * @brief Authors an emission material for auxiliary geometry; caller holds the auxiliary lock.
     */
    static void authorGridMaterial(UsdStageRefPtr stage, const SdfPath& path, const GfVec3f& color);

    /**
     * @brief Updates the camera-axis image.
     */
    void updateAxis(const GfCamera& camera, bool hasStage, qreal devicePixelRatio);

    /**
     * @brief Returns the camera-axis index at a point in the axis image.
     *
     * @param position Point in logical axis-image coordinates.
     * @param camera Camera used to orient the displayed axes.
     * @return X, Y, or Z axis index, or -1 when no axis bubble is hit.
     */
    int hitTestAxis(const QPointF& position, const GfCamera& camera) const;

    /**
     * @brief Returns the current camera-axis image.
     */
    const QImage& axisImage() const;

    /**
     * @brief Clears the camera-axis image.
     */
    void clearAxis();

private:
    QScopedPointer<ViewportAuxiliaryPrivate> p;
};

}  // namespace stageviz
