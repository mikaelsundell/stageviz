// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#pragma once

#include "stageviz.h"
#include <QList>
#include <QString>
#include <pxr/base/gf/vec3f.h>
#include <pxr/usd/sdf/path.h>
#include <pxr/usd/usd/stage.h>
#include <pxr/usd/usdShade/material.h>
#include <pxr/usd/usdShade/shader.h>

PXR_NAMESPACE_USING_DIRECTIVE

namespace stageviz {

/**
 * @struct MaterialParameters
 * @brief Common material parameters exposed by the Stageviz material editor.
 *
 * Values are renderer-agnostic. MaterialUtils maps them to the corresponding
 * shader inputs for UsdPreviewSurface and MaterialX Standard Surface.
 */
struct MaterialParameters {
    GfVec3f baseColor { 0.5f, 0.5f, 0.5f };
    float metalness = 0.0f;
    float roughness = 0.4f;
    float specular = 0.5f;
    float ior = 1.5f;
    float coat = 0.0f;
    float coatRoughness = 0.1f;
    float opacity = 1.0f;
    float transmission = 0.0f;
    GfVec3f transmissionColor { 1.0f, 1.0f, 1.0f };
};

/**
 * @struct MaterialEntry
 * @brief Lightweight description of one material shown by MaterialBrowser.
 *
 * The entry contains composed USD paths and values only. It does not own USD
 * objects and does not author changes to the stage.
 */
struct MaterialEntry {
    SdfPath materialPath;
    SdfPath shaderPath;
    QString name;
    QString shaderId;
    MaterialParameters parameters;
};

/**
 * @class MaterialUtils
 * @brief Shared USD material inspection and authoring helpers.
 *
 * UI state, browser selection, rendering queues, and command execution remain
 * owned by their corresponding Stageviz classes.
 */
class MaterialUtils {
public:
    /**
     * @brief Finds the material's surface shader; optionally returns its shader identifier.
     */
    static UsdShadeShader surfaceShader(const UsdShadeMaterial& material, QString* shaderId = nullptr);
    /**
     * @brief Reads supported shader inputs, retaining defaults for missing values.
     */
    static MaterialParameters readParameters(const UsdShadeShader& shader, const QString& shaderId);
    /**
     * @brief Collects material descriptions from the composed stage.
     */
    static QList<MaterialEntry> sceneMaterials(UsdStageRefPtr stage);

    /**
     * @brief Returns a display label for a shader identifier.
     */
    static QString shaderTypeLabel(const QString& shaderId);
    /**
     * @brief Reports whether the editor can map a parameter for this shader.
     */
    static bool isSupportedParameter(const MaterialEntry& entry, const QString& parameter);
    /**
     * @brief Maps an editor parameter to its shader input name.
     */
    static TfToken inputName(const MaterialEntry& entry, const QString& parameter);
    /**
     * @brief Returns the shader input's property path, or an empty path if unavailable.
     */
    static SdfPath inputPath(const MaterialEntry& entry, const QString& parameter);

    /**
     * @brief Finds an unused material path under /Materials.
     *
     * Creates the /Materials scope in the active edit target if needed.
     * @param stage Stage in which to find a free path.
     * @param baseName Base name, sanitized to a USD identifier before uniquifying.
     * @return Unused material path, or an empty path for an invalid stage.
     */
    static SdfPath uniqueMaterialPath(UsdStageRefPtr stage, const QString& baseName = QStringLiteral("Material"));
    /**
     * @brief Authors a Preview Surface material in the active edit target; returns its path or empty on failure.
     */
    static SdfPath createPreviewSurfaceMaterial(UsdStageRefPtr stage);
    /**
     * @brief Authors a Standard Surface material in the active edit target; returns its path or empty on failure.
     */
    static SdfPath createStandardSurfaceMaterial(UsdStageRefPtr stage);

    /**
     * @brief Imports supported MaterialX surface parameters into USD materials. Clears output arguments first; returns false with error on failure.
     */
    static bool importMaterialX(UsdStageRefPtr stage, const QString& filename, QList<SdfPath>& createdPaths,
                                QString& error);
};

}  // namespace stageviz
