// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#pragma once

#include "stageviz.h"
#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>
#include <pxr/base/gf/vec3f.h>
#include <pxr/base/vt/value.h>
#include <pxr/usd/sdf/path.h>
#include <pxr/usd/sdf/types.h>
#include <pxr/usd/usd/stage.h>
#include <pxr/usd/usdShade/connectableAPI.h>
#include <pxr/usd/usdShade/material.h>
#include <pxr/usd/usdShade/output.h>
#include <pxr/usd/usdShade/shader.h>

PXR_NAMESPACE_USING_DIRECTIVE

namespace stageviz {

/**
 * @struct MaterialParameters
 * @brief Canonical material parameters used by Stageviz material editing and fallback rendering.
 *
 * The structure provides a common parameter set across supported preview and
 * MaterialX surface shaders. Values are initialized to neutral defaults.
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
 * @struct MaterialInputInfo
 * @brief Describes one shader input exposed by the material inspector.
 *
 * The structure combines composed USD state with NodeDef metadata so the UI can
 * distinguish authored values, defaults, declared-but-unauthored inputs,
 * connections, and enum options.
 */
struct MaterialInputInfo {
    QString parameter;
    QString label;
    QString group;
    SdfPath inputPath;
    TfToken inputName;
    SdfValueTypeName typeName;
    VtValue value;
    bool hasValue = false;

    /** @brief NodeDef-declared default kept separately from the effective value. */
    VtValue defaultValue;
    bool hasDefaultValue = false;

    /** @brief True when the input has an authored or composed USD value opinion. */
    bool hasAuthoredValue = false;

    /** @brief True when the input is declared by the shader definition. */
    bool declared = false;

    bool connected = false;
    SdfPath sourcePrimPath;
    TfToken sourceName;
    QString sourceShaderId;
    QStringList options;

    /** @brief Returns true when the input type is float. */
    bool isFloat() const { return typeName == SdfValueTypeNames->Float; }

    /** @brief Returns true when the input type is color3f. */
    bool isColor3() const { return typeName == SdfValueTypeNames->Color3f; }
};

/**
 * @struct MaterialNodeInfo
 * @brief Lightweight description of one node in a material network.
 */
struct MaterialNodeInfo {
    SdfPath path;
    QString name;
    QString shaderId;
    QString typeLabel;
    QList<MaterialInputInfo> inputs;
};

/**
 * @struct MaterialXPortDefinition
 * @brief Describes one input or output declared by a MaterialX NodeDef.
 */
struct MaterialXPortDefinition {
    QString name;
    QString type;
    QString value;
    QString label;
    QString group;
    QStringList enumValues;
};

/**
 * @struct MaterialXNodeDefinition
 * @brief MaterialX NodeDef discovered from the installed MaterialX libraries.
 */
struct MaterialXNodeDefinition {
    QString nodeDef;
    QString node;
    QString group;
    QString outputType;
    QList<MaterialXPortDefinition> inputs;
    QList<MaterialXPortDefinition> outputs;
};

/**
 * @struct ShaderNodeDefinition
 * @brief Curated built-in USD Preview Surface helper-node definition.
 */
struct ShaderNodeDefinition {
    QString shaderId;
    QString node;
    QString group;
    TfToken outputName;
    SdfValueTypeName outputType;
};

/**
 * @struct MaterialEntry
 * @brief Describes one material discovered on the current USD stage.
 *
 * The entry identifies the material and its connected surface shader, stores
 * canonical material parameters, and keeps resolved input metadata used by the
 * browser and property editor.
 */
struct MaterialEntry {
    SdfPath materialPath;
    SdfPath shaderPath;
    QString name;
    QString shaderId;
    MaterialParameters parameters;
    QHash<QString, MaterialInputInfo> inputs;
};

/**
 * @class MaterialUtils
 * @brief Shared USD, UsdShade, MaterialX, and material-network utilities.
 *
 * MaterialUtils centralizes material discovery, shader inspection, NodeDef
 * metadata, type/default conversion, node compatibility helpers, material
 * creation, and MaterialX import. The functions are stateless from the caller's
 * perspective; internal lookup caches may be used for static shader definitions.
 */
class MaterialUtils {
public:
    /**
     * @brief Returns the material's connected surface shader.
     * @param material Material to inspect.
     * @param shaderId Optional destination for the authored shader identifier.
     */
    static UsdShadeShader surfaceShader(const UsdShadeMaterial& material, QString* shaderId = nullptr);

    /**
     * @brief Reads canonical Stageviz material parameters from a shader.
     */
    static MaterialParameters readParameters(const UsdShadeShader& shader, const QString& shaderId);

    /**
     * @brief Collects materials available on a stage.
     */
    static QList<MaterialEntry> sceneMaterials(UsdStageRefPtr stage);

    /**
     * @brief Returns a human-readable shader type label.
     */
    static QString shaderTypeLabel(const QString& shaderId);

    /**
     * @brief Returns whether a canonical parameter is supported by a material entry.
     */
    static bool isSupportedParameter(const MaterialEntry& entry, const QString& parameter);

    /**
     * @brief Returns the shader input name mapped to a canonical parameter.
     */
    static TfToken inputName(const MaterialEntry& entry, const QString& parameter);

    /**
     * @brief Returns the USD property path mapped to a canonical parameter.
     */
    static SdfPath inputPath(const MaterialEntry& entry, const QString& parameter);

    /**
     * @brief Returns metadata for a canonical material parameter.
     */
    static const MaterialInputInfo* inputInfo(const MaterialEntry& entry, const QString& parameter);

    /**
     * @brief Inspects any UsdShade connectable node.
     *
     * The result combines authored USD state with declared shader-definition
     * inputs so unauthored NodeDef inputs can still be displayed and edited.
     */
    static MaterialNodeInfo nodeInfo(UsdStageRefPtr stage, const SdfPath& nodePath);

    /**
     * @brief Returns the authored shader identifier for a UsdShadeShader prim.
     */
    static QString shaderId(const UsdPrim& prim);

    /**
     * @brief Resolves an input connection through intermediate node graphs.
     */
    static bool resolveSource(const UsdShadeInput& input, UsdShadeConnectableAPI* source, TfToken* sourceName,
                              UsdShadeAttributeType* sourceType);

    /**
     * @brief Resolves an output connection through intermediate node graphs.
     * @param depth Current recursion depth used to prevent unbounded traversal.
     */
    static bool resolveOutput(const UsdShadeOutput& output, UsdShadeConnectableAPI* source, TfToken* sourceName,
                              UsdShadeAttributeType* sourceType, int depth = 0);

    /**
     * @brief Converts a MaterialX type name to the corresponding USD value type.
     */
    static SdfValueTypeName sdfTypeForMaterialX(const QString& type);

    /**
     * @brief Converts a textual MaterialX default into a typed VtValue.
     */
    static VtValue materialXDefaultValue(const QString& type, const QString& text);

    /**
     * @brief Authors the known interface and defaults for a USD Preview helper node.
     */
    static void authorUsdNodeDefaults(UsdShadeShader& shader, const QString& shaderId);

    /**
     * @brief Ensures a declared shader input has a USD attribute.
     *
     * This is used before authoring a value or connection to a NodeDef-declared
     * input that does not yet exist in the stage.
     */
    static bool ensureShaderInput(UsdStageRefPtr stage, const SdfPath& inputPath);

    /**
     * @brief Returns a compact recursive description of the network feeding an input.
     */
    static QStringList networkDescription(UsdStageRefPtr stage, const SdfPath& inputPath, int maxDepth = 4);

    /**
     * @brief Returns MaterialX NodeDefs discovered from installed library search paths.
     */
    static QList<MaterialXNodeDefinition> materialXNodeDefinitions();

    /**
     * @brief Returns MaterialX NodeDefs whose outputs are compatible with a target type.
     */
    static QList<MaterialXNodeDefinition> compatibleMaterialXNodes(const SdfValueTypeName& targetType);

    /**
     * @brief Returns the curated built-in USD Preview Surface helper nodes.
     */
    static QList<ShaderNodeDefinition> usdShaderNodes();

    /**
     * @brief Returns USD Preview helper nodes compatible with a target input type.
     */
    static QList<ShaderNodeDefinition> compatibleUsdShaderNodes(const SdfValueTypeName& targetType);

    /**
     * @brief Returns a unique material path below the stage's material location.
     */
    static SdfPath uniqueMaterialPath(UsdStageRefPtr stage, const QString& baseName = QStringLiteral("Material"));

    /**
     * @brief Creates a USD Preview Surface material.
     * @return Path of the created material, or an empty path on failure.
     */
    static SdfPath createPreviewSurfaceMaterial(UsdStageRefPtr stage);

    /**
     * @brief Creates a MaterialX Standard Surface material.
     * @return Path of the created material, or an empty path on failure.
     */
    static SdfPath createStandardSurfaceMaterial(UsdStageRefPtr stage);

    /**
     * @brief Creates a MaterialX OpenPBR Surface material.
     * @return Path of the created material, or an empty path on failure.
     */
    static SdfPath createOpenPBRSurfaceMaterial(UsdStageRefPtr stage);

    /**
     * @brief Imports materials from a MaterialX file into the stage.
     *
     * @param stage Destination stage.
     * @param filename MaterialX document to import.
     * @param createdPaths Receives paths of created material prims.
     * @param error Receives a failure reason.
     * @return True when the import succeeds.
     */
    static bool importMaterialX(UsdStageRefPtr stage, const QString& filename, QList<SdfPath>& createdPaths,
                                QString& error);
};

}  // namespace stageviz
