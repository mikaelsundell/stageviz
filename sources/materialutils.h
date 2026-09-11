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
    static UsdShadeShader surfaceShader(const UsdShadeMaterial& material, QString* shaderId = nullptr);
    static MaterialParameters readParameters(const UsdShadeShader& shader, const QString& shaderId);
    static QList<MaterialEntry> sceneMaterials(UsdStageRefPtr stage);

    static QString shaderTypeLabel(const QString& shaderId);
    static bool isSupportedParameter(const MaterialEntry& entry, const QString& parameter);
    static TfToken inputName(const MaterialEntry& entry, const QString& parameter);
    static SdfPath inputPath(const MaterialEntry& entry, const QString& parameter);

    static SdfPath uniqueMaterialPath(UsdStageRefPtr stage, const QString& baseName = QStringLiteral("Material"));
    static SdfPath createPreviewSurfaceMaterial(UsdStageRefPtr stage);
    static SdfPath createStandardSurfaceMaterial(UsdStageRefPtr stage);

    static bool importMaterialX(UsdStageRefPtr stage, const QString& filename, QList<SdfPath>& createdPaths,
                                QString& error);
};

}  // namespace stageviz
