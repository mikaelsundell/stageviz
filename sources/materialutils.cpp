// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#include "materialutils.h"
#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>
#include <QXmlStreamReader>
#include <algorithm>
#include <pxr/base/gf/vec2f.h>
#include <pxr/base/gf/vec4f.h>
#include <pxr/base/tf/stringUtils.h>
#include <pxr/base/vt/value.h>
#include <pxr/usd/sdf/assetPath.h>
#include <pxr/usd/sdr/registry.h>
#include <pxr/usd/sdr/shaderNode.h>
#include <pxr/usd/sdr/shaderProperty.h>
#include <pxr/usd/usd/primRange.h>
#include <pxr/usd/usdGeom/metrics.h>
#include <pxr/usd/usdMtlx/reader.h>
#include <pxr/usd/usdMtlx/utils.h>
#include <pxr/usd/usdShade/connectableAPI.h>
#include <pxr/usd/usdShade/nodeGraph.h>
#include <pxr/usd/usdShade/output.h>
#include <utility>

PXR_NAMESPACE_USING_DIRECTIVE

namespace stageviz {

namespace {

    struct CanonicalParameter {
        const char* key;
        const char* label;
        const char* group;
        const char* previewName;
        const char* standardName;
        const char* openPbrName;
    };

    const CanonicalParameter kParameters[] = {
        { "baseColor", "Base Color", "Base", "diffuseColor", "base_color", "base_color" },
        { "metalness", "Metalness", "Base", "metallic", "metalness", "base_metalness" },
        { "roughness", "Roughness", "Base", "roughness", "roughness", "specular_roughness" },
        { "opacity", "Opacity", "Base", "opacity", "opacity", "opacity" },
        { "specular", "Specular", "Specular", nullptr, "specular", "specular_weight" },
        { "ior", "IOR", "Specular", "ior", "specular_IOR", "specular_ior" },
        { "coat", "Coat", "Coat", "clearcoat", "coat", "coat_weight" },
        { "coatRoughness", "Coat Roughness", "Coat", "clearcoatRoughness", "coat_roughness", "coat_roughness" },
        { "transmission", "Transmission", "Transmission", nullptr, "transmission", "transmission_weight" },
        { "transmissionColor", "Transmission Color", "Transmission", nullptr, "transmission_color",
          "transmission_color" },
    };

    template<typename T> T shaderInput(const UsdShadeShader& shader, const TfToken& name, const T& fallback)
    {
        const UsdShadeInput input = shader.GetInput(name);
        if (!input)
            return fallback;

        T value;
        return input.Get(&value) ? value : fallback;
    }

    QString sanitizeIdentifier(QString name)
    {
        name.replace(QRegularExpression("[^A-Za-z0-9_]"), "_");
        if (name.isEmpty() || name.front().isDigit())
            name.prepend('_');
        return name;
    }

    QString humanize(QString name)
    {
        name.replace('_', ' ');
        name.replace(QRegularExpression("([a-z0-9])([A-Z])"), "\\1 \\2");
        if (!name.isEmpty())
            name[0] = name[0].toUpper();
        return name;
    }

    TfToken mappedInputName(const QString& shaderId, const QString& parameter)
    {
        for (const CanonicalParameter& mapping : kParameters) {
            if (parameter != mapping.key)
                continue;

            const char* name = nullptr;
            if (shaderId == "UsdPreviewSurface")
                name = mapping.previewName;
            else if (shaderId == "ND_standard_surface_surfaceshader")
                name = mapping.standardName;
            else if (shaderId == "ND_open_pbr_surface_surfaceshader")
                name = mapping.openPbrName;

            return name ? TfToken(name) : TfToken();
        }
        return TfToken();
    }

    const CanonicalParameter* canonicalForInput(const QString& shaderId, const TfToken& inputName)
    {
        for (const CanonicalParameter& mapping : kParameters) {
            const char* name
                = shaderId == "UsdPreviewSurface"
                      ? mapping.previewName
                      : (shaderId == "ND_standard_surface_surfaceshader"
                             ? mapping.standardName
                             : (shaderId == "ND_open_pbr_surface_surfaceshader" ? mapping.openPbrName : nullptr));
            if (name && inputName == TfToken(name))
                return &mapping;
        }
        return nullptr;
    }

    QString shaderIdForPrim(const UsdPrim& prim)
    {
        if (!prim || !prim.IsA<UsdShadeShader>())
            return {};
        TfToken id;
        UsdShadeShader(prim).GetIdAttr().Get(&id);
        return QString::fromStdString(id.GetString());
    }

    bool connectedSource(const UsdShadeInput& input, SdfPath* sourcePrimPath, TfToken* sourceName,
                         QString* sourceShaderId)
    {
        if (!input)
            return false;

        UsdShadeConnectableAPI source;
        TfToken name;
        UsdShadeAttributeType sourceType = UsdShadeAttributeType::Output;
        if (!UsdShadeConnectableAPI::GetConnectedSource(input, &source, &name, &sourceType))
            return false;

        if (sourcePrimPath)
            *sourcePrimPath = source.GetPrim().GetPath();
        if (sourceName)
            *sourceName = name;
        if (sourceShaderId)
            *sourceShaderId = shaderIdForPrim(source.GetPrim());
        return true;
    }

    bool connectedSource(const UsdShadeOutput& output, UsdShadeConnectableAPI* source, TfToken* sourceName,
                         UsdShadeAttributeType* sourceType)
    {
        return output && UsdShadeConnectableAPI::GetConnectedSource(output, source, sourceName, sourceType);
    }

    bool resolveConnectedSource(const UsdShadeOutput& output, SdfPath* sourcePrimPath, TfToken* sourceName,
                                QString* sourceShaderId, int depth = 0)
    {
        if (!output || depth > 16)
            return false;

        UsdShadeConnectableAPI source;
        TfToken name;
        UsdShadeAttributeType sourceType = UsdShadeAttributeType::Output;
        if (!connectedSource(output, &source, &name, &sourceType))
            return false;

        const UsdPrim sourcePrim = source.GetPrim();
        if (sourcePrim.IsA<UsdShadeNodeGraph>()) {
            const UsdShadeOutput nested = UsdShadeNodeGraph(sourcePrim).GetOutput(name);
            if (nested && resolveConnectedSource(nested, sourcePrimPath, sourceName, sourceShaderId, depth + 1))
                return true;
        }

        if (sourcePrimPath)
            *sourcePrimPath = sourcePrim.GetPath();
        if (sourceName)
            *sourceName = name;
        if (sourceShaderId)
            *sourceShaderId = shaderIdForPrim(sourcePrim);
        return true;
    }

    bool resolveConnectedSource(const UsdShadeInput& input, SdfPath* sourcePrimPath, TfToken* sourceName,
                                QString* sourceShaderId)
    {
        if (!input)
            return false;

        UsdShadeConnectableAPI source;
        TfToken name;
        UsdShadeAttributeType sourceType = UsdShadeAttributeType::Output;
        if (!UsdShadeConnectableAPI::GetConnectedSource(input, &source, &name, &sourceType))
            return false;

        const UsdPrim sourcePrim = source.GetPrim();
        if (sourcePrim.IsA<UsdShadeNodeGraph>()) {
            const UsdShadeOutput output = UsdShadeNodeGraph(sourcePrim).GetOutput(name);
            if (output && resolveConnectedSource(output, sourcePrimPath, sourceName, sourceShaderId))
                return true;
        }

        if (sourcePrimPath)
            *sourcePrimPath = sourcePrim.GetPath();
        if (sourceName)
            *sourceName = name;
        if (sourceShaderId)
            *sourceShaderId = shaderIdForPrim(sourcePrim);
        return true;
    }

    UsdShadeShader resolveOutputToShader(const UsdShadeOutput& output, int depth = 0)
    {
        if (!output || depth > 16)
            return UsdShadeShader();

        UsdShadeConnectableAPI source;
        TfToken sourceName;
        UsdShadeAttributeType sourceType = UsdShadeAttributeType::Output;
        if (!connectedSource(output, &source, &sourceName, &sourceType))
            return UsdShadeShader();

        const UsdPrim sourcePrim = source.GetPrim();
        if (sourcePrim.IsA<UsdShadeShader>())
            return UsdShadeShader(sourcePrim);

        if (sourcePrim.IsA<UsdShadeNodeGraph>()) {
            const UsdShadeOutput nested = UsdShadeNodeGraph(sourcePrim).GetOutput(sourceName);
            return resolveOutputToShader(nested, depth + 1);
        }

        return UsdShadeShader();
    }

    QStringList materialXInputOptions(const QString& shaderId, const TfToken& inputName)
    {
        if (!shaderId.startsWith(QStringLiteral("ND_")) || inputName.IsEmpty())
            return {};

        static const QHash<QString, QHash<QString, QStringList>> optionsByNodeDef = []() {
            QHash<QString, QHash<QString, QStringList>> result;
            const QList<MaterialXNodeDefinition> defs = MaterialUtils::materialXNodeDefinitions();
            for (const MaterialXNodeDefinition& def : defs) {
                QHash<QString, QStringList> inputs;
                for (const MaterialXPortDefinition& port : def.inputs) {
                    if (!port.enumValues.isEmpty())
                        inputs.insert(port.name, port.enumValues);
                }
                if (!inputs.isEmpty())
                    result.insert(def.nodeDef, inputs);
            }
            return result;
        }();

        QStringList values = optionsByNodeDef.value(shaderId).value(QString::fromStdString(inputName.GetString()));

        // Older MaterialX standard-library files do not consistently expose
        // UI enum metadata on every NodeDef. Keep the common image enums
        // useful even with those libraries.
        if (values.isEmpty()) {
            const QString name = QString::fromStdString(inputName.GetString());
            if (name == QStringLiteral("filtertype"))
                values = { QStringLiteral("closest"), QStringLiteral("linear"), QStringLiteral("cubic") };
            else if (name == QStringLiteral("uaddressmode") || name == QStringLiteral("vaddressmode"))
                values = { QStringLiteral("constant"), QStringLiteral("clamp"), QStringLiteral("periodic"),
                           QStringLiteral("mirror") };
            else if (name == QStringLiteral("frameendaction"))
                values = { QStringLiteral("constant"), QStringLiteral("clamp"), QStringLiteral("periodic"),
                           QStringLiteral("mirror") };
        }
        return values;
    }

    const MaterialXNodeDefinition* materialXNodeDefinition(const QString& shaderId)
    {
        if (shaderId.isEmpty())
            return nullptr;

        static const QHash<QString, MaterialXNodeDefinition> definitions = []() {
            QHash<QString, MaterialXNodeDefinition> result;
            const QList<MaterialXNodeDefinition> nodeDefs = MaterialUtils::materialXNodeDefinitions();
            result.reserve(nodeDefs.size());
            for (const MaterialXNodeDefinition& def : nodeDefs)
                result.insert(def.nodeDef, def);
            return result;
        }();

        const auto it = definitions.constFind(shaderId);
        return it == definitions.constEnd() ? nullptr : &it.value();
    }

    MaterialInputInfo inspectInput(const UsdShadeInput& input, const QString& shaderId)
    {
        MaterialInputInfo info;
        if (!input)
            return info;

        info.inputPath = input.GetAttr().GetPath();
        info.inputName = input.GetBaseName();
        info.typeName = input.GetTypeName();
        info.parameter = QString::fromStdString(info.inputName.GetString());
        info.label = humanize(info.parameter);
        info.group = QStringLiteral("Inputs");

        if (const CanonicalParameter* canonical = canonicalForInput(shaderId, info.inputName)) {
            info.parameter = QString::fromUtf8(canonical->key);
            info.label = QString::fromUtf8(canonical->label);
            info.group = QString::fromUtf8(canonical->group);
        }

        info.hasValue = input.Get(&info.value);
        info.hasAuthoredValue = input.GetAttr().HasAuthoredValueOpinion();
        info.declared = false;
        info.options = materialXInputOptions(shaderId, info.inputName);
        // Resolve through UsdShadeNodeGraph outputs so the inspector navigates
        // to the actual upstream shader instead of stopping at the node graph.
        info.connected = resolveConnectedSource(input, &info.sourcePrimPath, &info.sourceName, &info.sourceShaderId);
        return info;
    }

    SdfValueTypeName materialXSdfType(const QString& type)
    {
        if (type == QStringLiteral("float"))
            return SdfValueTypeNames->Float;
        if (type == QStringLiteral("color3"))
            return SdfValueTypeNames->Color3f;
        if (type == QStringLiteral("color4"))
            return SdfValueTypeNames->Color4f;
        if (type == QStringLiteral("vector2") || type == QStringLiteral("color2"))
            return SdfValueTypeNames->Float2;
        if (type == QStringLiteral("vector3"))
            return SdfValueTypeNames->Float3;
        if (type == QStringLiteral("vector4"))
            return SdfValueTypeNames->Float4;
        if (type == QStringLiteral("integer"))
            return SdfValueTypeNames->Int;
        if (type == QStringLiteral("boolean"))
            return SdfValueTypeNames->Bool;
        if (type == QStringLiteral("string"))
            return SdfValueTypeNames->String;
        if (type == QStringLiteral("filename"))
            return SdfValueTypeNames->Asset;
        return SdfValueTypeNames->Token;
    }

    QStringList splitMaterialXValue(QString text)
    {
        text.replace(',', ' ');
        return text.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
    }

    VtValue materialXDefault(const QString& type, const QString& text)
    {
        // Empty filename/string values are still meaningful MaterialX defaults.
        // In particular, resetting an image node's filename should author an
        // empty asset path rather than exposing a weaker imported filename.
        if (type == QStringLiteral("filename"))
            return VtValue(SdfAssetPath(text.toStdString()));
        if (type == QStringLiteral("string"))
            return VtValue(text.toStdString());

        if (text.isEmpty())
            return {};

        bool ok = false;
        if (type == QStringLiteral("float")) {
            const float value = text.toFloat(&ok);
            return ok ? VtValue(value) : VtValue();
        }
        if (type == QStringLiteral("integer")) {
            const int value = text.toInt(&ok);
            return ok ? VtValue(value) : VtValue();
        }
        if (type == QStringLiteral("boolean")) {
            const QString value = text.trimmed().toLower();
            if (value == QStringLiteral("true") || value == QStringLiteral("1"))
                return VtValue(true);
            if (value == QStringLiteral("false") || value == QStringLiteral("0"))
                return VtValue(false);
            return {};
        }

        const QStringList values = splitMaterialXValue(text);
        if (type == QStringLiteral("vector2") || type == QStringLiteral("color2")) {
            if (values.size() < 2)
                return {};
            bool a = false, b = false;
            const float x = values[0].toFloat(&a);
            const float y = values[1].toFloat(&b);
            return a && b ? VtValue(GfVec2f(x, y)) : VtValue();
        }
        if (type == QStringLiteral("vector3") || type == QStringLiteral("color3")) {
            if (values.size() < 3)
                return {};
            bool a = false, b = false, c = false;
            const float x = values[0].toFloat(&a);
            const float y = values[1].toFloat(&b);
            const float z = values[2].toFloat(&c);
            return a && b && c ? VtValue(GfVec3f(x, y, z)) : VtValue();
        }
        if (type == QStringLiteral("vector4") || type == QStringLiteral("color4")) {
            if (values.size() < 4)
                return {};
            bool a = false, b = false, c = false, d = false;
            const float x = values[0].toFloat(&a);
            const float y = values[1].toFloat(&b);
            const float z = values[2].toFloat(&c);
            const float w = values[3].toFloat(&d);
            return a && b && c && d ? VtValue(GfVec4f(x, y, z, w)) : VtValue();
        }
        return VtValue(TfToken(text.toStdString()));
    }

    QString standardSurfaceGroup(const QString& name)
    {
        if (name == QStringLiteral("base") || name == QStringLiteral("base_color")
            || name == QStringLiteral("diffuse_roughness") || name == QStringLiteral("metalness"))
            return QStringLiteral("Base");
        if (name == QStringLiteral("specular") || name.startsWith(QStringLiteral("specular_")))
            return QStringLiteral("Specular");
        if (name == QStringLiteral("transmission") || name.startsWith(QStringLiteral("transmission_")))
            return QStringLiteral("Transmission");
        if (name == QStringLiteral("subsurface") || name.startsWith(QStringLiteral("subsurface_")))
            return QStringLiteral("Subsurface");
        if (name == QStringLiteral("sheen") || name.startsWith(QStringLiteral("sheen_")))
            return QStringLiteral("Sheen");
        if (name == QStringLiteral("coat") || name.startsWith(QStringLiteral("coat_")))
            return QStringLiteral("Coat");
        if (name.startsWith(QStringLiteral("thin_film_")))
            return QStringLiteral("Thin Film");
        if (name == QStringLiteral("emission") || name == QStringLiteral("emission_color"))
            return QStringLiteral("Emission");
        if (name == QStringLiteral("opacity") || name == QStringLiteral("thin_walled")
            || name == QStringLiteral("normal") || name == QStringLiteral("tangent"))
            return QStringLiteral("Geometry");
        return QStringLiteral("Inputs");
    }

    QString openPbrSurfaceGroup(const QString& name)
    {
        if (name == QStringLiteral("base_weight") || name.startsWith(QStringLiteral("base_")))
            return QStringLiteral("Base");
        if (name == QStringLiteral("specular_weight") || name.startsWith(QStringLiteral("specular_")))
            return QStringLiteral("Specular");
        if (name == QStringLiteral("transmission_weight") || name.startsWith(QStringLiteral("transmission_")))
            return QStringLiteral("Transmission");
        if (name == QStringLiteral("subsurface_weight") || name.startsWith(QStringLiteral("subsurface_")))
            return QStringLiteral("Subsurface");
        if (name == QStringLiteral("fuzz_weight") || name.startsWith(QStringLiteral("fuzz_")))
            return QStringLiteral("Fuzz");
        if (name == QStringLiteral("coat_weight") || name.startsWith(QStringLiteral("coat_")))
            return QStringLiteral("Coat");
        if (name == QStringLiteral("thin_film_weight") || name.startsWith(QStringLiteral("thin_film_")))
            return QStringLiteral("Thin Film");
        if (name == QStringLiteral("emission_luminance") || name == QStringLiteral("emission_color"))
            return QStringLiteral("Emission");
        if (name == QStringLiteral("opacity") || name == QStringLiteral("thin_walled")
            || name == QStringLiteral("normal") || name == QStringLiteral("tangent")
            || name == QStringLiteral("coat_normal") || name == QStringLiteral("coat_tangent"))
            return QStringLiteral("Geometry");
        return QStringLiteral("Inputs");
    }

    QString materialXPortGroup(const QString& shaderId, const MaterialXPortDefinition& port)
    {
        if (!port.group.isEmpty())
            return port.group;
        if (shaderId == QStringLiteral("ND_standard_surface_surfaceshader"))
            return standardSurfaceGroup(port.name);
        if (shaderId == QStringLiteral("ND_open_pbr_surface_surfaceshader"))
            return openPbrSurfaceGroup(port.name);
        return QStringLiteral("Inputs");
    }

    void authorStandardSurface(const UsdStageRefPtr& stage, const SdfPath& path, const MaterialParameters& p)
    {
        UsdShadeMaterial material = UsdShadeMaterial::Define(stage, path);
        UsdShadeShader shader = UsdShadeShader::Define(stage, path.AppendChild(TfToken("StandardSurface")));

        shader.CreateIdAttr(VtValue(TfToken("ND_standard_surface_surfaceshader")));
        shader.CreateInput(TfToken("base"), SdfValueTypeNames->Float).Set(1.0f);
        shader.CreateInput(TfToken("base_color"), SdfValueTypeNames->Color3f).Set(p.baseColor);
        shader.CreateInput(TfToken("metalness"), SdfValueTypeNames->Float).Set(p.metalness);
        shader.CreateInput(TfToken("roughness"), SdfValueTypeNames->Float).Set(p.roughness);
        shader.CreateInput(TfToken("specular"), SdfValueTypeNames->Float).Set(p.specular);
        shader.CreateInput(TfToken("specular_IOR"), SdfValueTypeNames->Float).Set(p.ior);
        shader.CreateInput(TfToken("coat"), SdfValueTypeNames->Float).Set(p.coat);
        shader.CreateInput(TfToken("coat_roughness"), SdfValueTypeNames->Float).Set(p.coatRoughness);
        shader.CreateInput(TfToken("opacity"), SdfValueTypeNames->Color3f).Set(GfVec3f(p.opacity));
        shader.CreateInput(TfToken("transmission"), SdfValueTypeNames->Float).Set(p.transmission);
        shader.CreateInput(TfToken("transmission_color"), SdfValueTypeNames->Color3f).Set(p.transmissionColor);

        const UsdShadeOutput output = shader.CreateOutput(TfToken("out"), SdfValueTypeNames->Token);
        material.CreateSurfaceOutput(TfToken("mtlx")).ConnectToSource(output);
    }

    void authorOpenPBRSurface(const UsdStageRefPtr& stage, const SdfPath& path)
    {
        UsdShadeMaterial material = UsdShadeMaterial::Define(stage, path);
        UsdShadeShader shader = UsdShadeShader::Define(stage, path.AppendChild(TfToken("OpenPBRSurface")));

        // OpenPBR is part of the installed MaterialX library. Keep the authored
        // USD shader minimal and let its NodeDef provide the complete interface
        // and defaults. Inputs are authored lazily when the user edits them.
        shader.CreateIdAttr(VtValue(TfToken("ND_open_pbr_surface_surfaceshader")));
        const UsdShadeOutput output = shader.CreateOutput(TfToken("out"), SdfValueTypeNames->Token);
        material.CreateSurfaceOutput(TfToken("mtlx")).ConnectToSource(output);
    }

    QString materialXTypeForSdf(const SdfValueTypeName& type)
    {
        if (type == SdfValueTypeNames->Float)
            return "float";
        if (type == SdfValueTypeNames->Color3f)
            return "color3";
        if (type == SdfValueTypeNames->Color4f)
            return "color4";
        if (type == SdfValueTypeNames->Float2)
            return "vector2";
        if (type == SdfValueTypeNames->Float3)
            return "vector3";
        if (type == SdfValueTypeNames->Float4)
            return "vector4";
        if (type == SdfValueTypeNames->Int)
            return "integer";
        if (type == SdfValueTypeNames->Bool)
            return "boolean";
        if (type == SdfValueTypeNames->String)
            return "string";
        if (type == SdfValueTypeNames->Asset)
            return "filename";
        return {};
    }

    QStringList materialXLibraryRoots()
    {
        QStringList roots;

        auto appendPath = [&](const QString& path) {
            if (path.isEmpty())
                return;
            const QString clean = QDir::cleanPath(path);
            if (QDir(clean).exists() && !roots.contains(clean))
                roots.append(clean);
        };

        // Use OpenUSD's own MaterialX discovery paths first. This is important
        // for packaged Stageviz builds: the standard library path discovered by
        // OpenUSD at build/install time is not necessarily present in an
        // environment variable or next to the Stageviz executable.
        for (const std::string& path : UsdMtlxSearchPaths())
            appendPath(QString::fromStdString(path));

        auto appendList = [&](const QByteArray& env) {
            const QString value = qEnvironmentVariable(env.constData());
            for (const QString& part : value.split(QDir::listSeparator(), Qt::SkipEmptyParts))
                appendPath(part);
        };

        // Keep the explicit environment/application fallbacks so custom
        // MaterialX libraries remain discoverable even when they are outside
        // OpenUSD's plugin search path.
        appendList("MATERIALX_SEARCH_PATH");
        appendList("PXR_MTLX_STDLIB_SEARCH_PATHS");
        appendList("PXR_MTLX_PLUGIN_SEARCH_PATHS");

        const QString app = QCoreApplication::applicationDirPath();
        const QStringList candidates = {
            app + "/libraries",
            app + "/MaterialX/libraries",
            app + "/../Resources/libraries",
            app + "/../Resources/MaterialX/libraries",
            app + "/../share/MaterialX/libraries",
            app + "/../../share/MaterialX/libraries",
        };
        for (const QString& candidate : candidates)
            appendPath(candidate);

        return roots;
    }

    QString sourceLabel(const UsdPrim& prim)
    {
        if (!prim)
            return {};
        const QString id = shaderIdForPrim(prim);
        const QString name = QString::fromStdString(prim.GetName().GetString());
        return id.isEmpty() ? name : QString("%1 (%2)").arg(name, id);
    }

    void describeInputRecursive(UsdStageRefPtr stage, const UsdShadeInput& input, int depth, int maxDepth,
                                QStringList& lines, QSet<QString>& visited)
    {
        if (!stage || !input || depth > maxDepth)
            return;

        SdfPath sourcePath;
        TfToken sourceName;
        QString sourceId;
        if (!connectedSource(input, &sourcePath, &sourceName, &sourceId))
            return;

        const QString key = QString::fromStdString(sourcePath.GetString());
        const QString indent(depth * 2, ' ');
        const UsdPrim sourcePrim = stage->GetPrimAtPath(sourcePath);
        lines.append(QString("%1%2 -> %3.%4")
                         .arg(indent, QString::fromStdString(input.GetBaseName().GetString()), sourceLabel(sourcePrim),
                              QString::fromStdString(sourceName.GetString())));

        if (depth == maxDepth || visited.contains(key))
            return;
        visited.insert(key);

        const UsdShadeConnectableAPI connectable(sourcePrim);
        if (!connectable)
            return;

        for (const UsdShadeInput& child : connectable.GetInputs()) {
            SdfPath childSource;
            if (connectedSource(child, &childSource, nullptr, nullptr))
                describeInputRecursive(stage, child, depth + 1, maxDepth, lines, visited);
        }
    }

}  // namespace

QString
MaterialUtils::shaderId(const UsdPrim& prim)
{
    return shaderIdForPrim(prim);
}

bool
MaterialUtils::resolveOutput(const UsdShadeOutput& output, UsdShadeConnectableAPI* source, TfToken* sourceName,
                             UsdShadeAttributeType* sourceType, int depth)
{
    if (!output || depth > 24)
        return false;

    UsdShadeConnectableAPI direct;
    TfToken name;
    UsdShadeAttributeType type = UsdShadeAttributeType::Output;
    if (!UsdShadeConnectableAPI::GetConnectedSource(output, &direct, &name, &type))
        return false;

    const UsdPrim prim = direct.GetPrim();
    if (prim.IsA<UsdShadeNodeGraph>()) {
        const UsdShadeOutput nested = UsdShadeNodeGraph(prim).GetOutput(name);
        return resolveOutput(nested, source, sourceName, sourceType, depth + 1);
    }

    if (source)
        *source = direct;
    if (sourceName)
        *sourceName = name;
    if (sourceType)
        *sourceType = type;
    return true;
}

bool
MaterialUtils::resolveSource(const UsdShadeInput& input, UsdShadeConnectableAPI* source, TfToken* sourceName,
                             UsdShadeAttributeType* sourceType)
{
    if (!input)
        return false;

    UsdShadeConnectableAPI direct;
    TfToken name;
    UsdShadeAttributeType type = UsdShadeAttributeType::Output;
    if (!UsdShadeConnectableAPI::GetConnectedSource(input, &direct, &name, &type))
        return false;

    const UsdPrim prim = direct.GetPrim();
    if (prim.IsA<UsdShadeNodeGraph>()) {
        const UsdShadeOutput output = UsdShadeNodeGraph(prim).GetOutput(name);
        return resolveOutput(output, source, sourceName, sourceType);
    }

    if (source)
        *source = direct;
    if (sourceName)
        *sourceName = name;
    if (sourceType)
        *sourceType = type;
    return true;
}

SdfValueTypeName
MaterialUtils::sdfTypeForMaterialX(const QString& type)
{
    return materialXSdfType(type);
}

VtValue
MaterialUtils::materialXDefaultValue(const QString& type, const QString& text)
{
    return materialXDefault(type, text);
}

void
MaterialUtils::authorUsdNodeDefaults(UsdShadeShader& shader, const QString& shaderId)
{
    if (!shader || shaderId.isEmpty())
        return;

    SdrRegistry& registry = SdrRegistry::GetInstance();
    SdrShaderNodeConstPtr node;
    size_t bestPropertyCount = 0;

    const TfToken identifier(shaderId.toStdString());
    const auto candidates = registry.GetShaderNodesByIdentifier(identifier);
    for (const auto& candidate : candidates) {
        if (!candidate || !candidate->IsValid())
            continue;

        const size_t propertyCount = candidate->GetShaderInputNames().size() + candidate->GetShaderOutputNames().size();
        if (!node || propertyCount > bestPropertyCount) {
            node = candidate;
            bestPropertyCount = propertyCount;
        }
    }

    if (!node)
        node = registry.GetShaderNodeByIdentifier(identifier);

    if (node && node->IsValid()) {
        for (const TfToken& inputName : node->GetShaderInputNames()) {
            const SdrShaderPropertyConstPtr property = node->GetShaderInput(inputName);
            if (!property)
                continue;

            const SdrSdfTypeIndicator type = property->GetTypeAsSdfType();
            const SdfValueTypeName sdfType = type.GetSdfType();
            if (sdfType.GetAsToken().IsEmpty())
                continue;

            UsdShadeInput input = shader.CreateInput(inputName, sdfType);
            const VtValue& defaultValue = property->GetDefaultValueAsSdfType();
            if (input && !defaultValue.IsEmpty())
                input.Set(defaultValue);
        }

        for (const TfToken& outputName : node->GetShaderOutputNames()) {
            const SdrShaderPropertyConstPtr property = node->GetShaderOutput(outputName);
            if (!property)
                continue;

            const SdrSdfTypeIndicator type = property->GetTypeAsSdfType();
            const SdfValueTypeName sdfType = type.GetSdfType();
            if (!sdfType.GetAsToken().IsEmpty())
                shader.CreateOutput(outputName, sdfType);
        }
        return;
    }

    if (shaderId == QStringLiteral("UsdUVTexture")) {
        shader.CreateInput(TfToken("file"), SdfValueTypeNames->Asset);
        shader.CreateInput(TfToken("st"), SdfValueTypeNames->Float2);
        shader.CreateInput(TfToken("fallback"), SdfValueTypeNames->Float4);
        shader.CreateInput(TfToken("wrapS"), SdfValueTypeNames->Token);
        shader.CreateInput(TfToken("wrapT"), SdfValueTypeNames->Token);
        shader.CreateOutput(TfToken("rgb"), SdfValueTypeNames->Float3);
        shader.CreateOutput(TfToken("r"), SdfValueTypeNames->Float);
        shader.CreateOutput(TfToken("g"), SdfValueTypeNames->Float);
        shader.CreateOutput(TfToken("b"), SdfValueTypeNames->Float);
    }
    else if (shaderId.startsWith(QStringLiteral("UsdPrimvarReader_"))) {
        shader.CreateInput(TfToken("varname"), SdfValueTypeNames->Token);

        SdfValueTypeName valueType;
        if (shaderId.endsWith(QStringLiteral("_float")))
            valueType = SdfValueTypeNames->Float;
        else if (shaderId.endsWith(QStringLiteral("_float2")))
            valueType = SdfValueTypeNames->Float2;
        else if (shaderId.endsWith(QStringLiteral("_float3")))
            valueType = SdfValueTypeNames->Float3;
        else if (shaderId.endsWith(QStringLiteral("_float4")))
            valueType = SdfValueTypeNames->Float4;
        else if (shaderId.endsWith(QStringLiteral("_int")))
            valueType = SdfValueTypeNames->Int;
        else if (shaderId.endsWith(QStringLiteral("_string")))
            valueType = SdfValueTypeNames->String;

        if (!valueType.GetAsToken().IsEmpty()) {
            shader.CreateInput(TfToken("fallback"), valueType);
            shader.CreateOutput(TfToken("result"), valueType);
        }
    }
    else if (shaderId == QStringLiteral("UsdTransform2d")) {
        shader.CreateInput(TfToken("in"), SdfValueTypeNames->Float2);
        shader.CreateInput(TfToken("rotation"), SdfValueTypeNames->Float);
        shader.CreateInput(TfToken("scale"), SdfValueTypeNames->Float2);
        shader.CreateInput(TfToken("translation"), SdfValueTypeNames->Float2);
        shader.CreateOutput(TfToken("result"), SdfValueTypeNames->Float2);
    }
}

UsdShadeShader
MaterialUtils::surfaceShader(const UsdShadeMaterial& material, QString* shaderId)
{
    if (shaderId)
        shaderId->clear();
    if (!material)
        return UsdShadeShader();

    // Prefer the material's actual connected surface outputs. This avoids
    // choosing an unrelated descendant shader merely because it appears first.
    for (const TfToken& context : { TfToken(), TfToken("mtlx") }) {
        const UsdShadeOutput output = context.IsEmpty() ? material.GetSurfaceOutput()
                                                        : material.GetSurfaceOutput(context);
        const UsdShadeShader shader = resolveOutputToShader(output);
        if (shader) {
            const QString id = shaderIdForPrim(shader.GetPrim());
            if (shaderId)
                *shaderId = id;
            return shader;
        }
    }

    // Compatibility fallback for older/simple files with incomplete output wiring.
    const UsdPrim materialPrim = material.GetPrim();
    for (const UsdPrim& prim : UsdPrimRange(materialPrim)) {
        if (prim == materialPrim || !prim.IsA<UsdShadeShader>())
            continue;
        const QString id = shaderIdForPrim(prim);
        if (id == "ND_standard_surface_surfaceshader" || id == "ND_open_pbr_surface_surfaceshader"
            || id == "UsdPreviewSurface") {
            if (shaderId)
                *shaderId = id;
            return UsdShadeShader(prim);
        }
    }
    return UsdShadeShader();
}

MaterialParameters
MaterialUtils::readParameters(const UsdShadeShader& shader, const QString& shaderId)
{
    MaterialParameters p;
    if (!shader)
        return p;

    if (shaderId == "ND_standard_surface_surfaceshader") {
        p.baseColor = shaderInput(shader, TfToken("base_color"), p.baseColor);
        p.metalness = shaderInput(shader, TfToken("metalness"), p.metalness);
        p.roughness = shaderInput(shader, TfToken("roughness"), p.roughness);
        p.specular = shaderInput(shader, TfToken("specular"), p.specular);
        p.ior = shaderInput(shader, TfToken("specular_IOR"), p.ior);
        p.coat = shaderInput(shader, TfToken("coat"), p.coat);
        p.coatRoughness = shaderInput(shader, TfToken("coat_roughness"), p.coatRoughness);
        p.transmission = shaderInput(shader, TfToken("transmission"), p.transmission);
        p.transmissionColor = shaderInput(shader, TfToken("transmission_color"), p.transmissionColor);
        const GfVec3f opacity = shaderInput(shader, TfToken("opacity"), GfVec3f(1.0f));
        p.opacity = opacity[0];
    }
    else if (shaderId == "ND_open_pbr_surface_surfaceshader") {
        p.baseColor = shaderInput(shader, TfToken("base_color"), p.baseColor);
        p.metalness = shaderInput(shader, TfToken("base_metalness"), p.metalness);
        p.roughness = shaderInput(shader, TfToken("specular_roughness"), p.roughness);
        p.specular = shaderInput(shader, TfToken("specular_weight"), p.specular);
        p.ior = shaderInput(shader, TfToken("specular_ior"), p.ior);
        p.coat = shaderInput(shader, TfToken("coat_weight"), p.coat);
        p.coatRoughness = shaderInput(shader, TfToken("coat_roughness"), p.coatRoughness);
        p.opacity = shaderInput(shader, TfToken("opacity"), p.opacity);
        p.transmission = shaderInput(shader, TfToken("transmission_weight"), p.transmission);
        p.transmissionColor = shaderInput(shader, TfToken("transmission_color"), p.transmissionColor);
    }
    else if (shaderId == "UsdPreviewSurface") {
        p.baseColor = shaderInput(shader, TfToken("diffuseColor"), p.baseColor);
        p.metalness = shaderInput(shader, TfToken("metallic"), p.metalness);
        p.roughness = shaderInput(shader, TfToken("roughness"), p.roughness);
        p.ior = shaderInput(shader, TfToken("ior"), p.ior);
        p.coat = shaderInput(shader, TfToken("clearcoat"), p.coat);
        p.coatRoughness = shaderInput(shader, TfToken("clearcoatRoughness"), p.coatRoughness);
        p.opacity = shaderInput(shader, TfToken("opacity"), p.opacity);
    }
    return p;
}

QList<MaterialEntry>
MaterialUtils::sceneMaterials(UsdStageRefPtr stage)
{
    QList<MaterialEntry> entries;
    if (!stage)
        return entries;

    for (const UsdPrim& prim : stage->Traverse()) {
        if (!prim.IsA<UsdShadeMaterial>())
            continue;

        const UsdShadeMaterial material(prim);
        QString shaderId;
        const UsdShadeShader shader = surfaceShader(material, &shaderId);
        if (!shader)
            continue;

        MaterialEntry entry;
        entry.materialPath = prim.GetPath();
        entry.shaderPath = shader.GetPath();
        entry.name = QString::fromStdString(prim.GetName().GetString());
        entry.shaderId = shaderId;
        entry.parameters = readParameters(shader, shaderId);

        for (const CanonicalParameter& mapping : kParameters) {
            const TfToken name = mappedInputName(shaderId, QString::fromUtf8(mapping.key));
            if (name.IsEmpty())
                continue;
            const UsdShadeInput input = shader.GetInput(name);
            if (!input)
                continue;
            MaterialInputInfo info = inspectInput(input, shaderId);
            info.parameter = QString::fromUtf8(mapping.key);
            info.label = QString::fromUtf8(mapping.label);
            info.group = QString::fromUtf8(mapping.group);
            entry.inputs.insert(info.parameter, info);
        }
        entries.append(entry);
    }

    std::sort(entries.begin(), entries.end(), [](const MaterialEntry& a, const MaterialEntry& b) {
        return a.materialPath.GetString() < b.materialPath.GetString();
    });
    return entries;
}

QString
MaterialUtils::shaderTypeLabel(const QString& shaderId)
{
    if (shaderId == "ND_standard_surface_surfaceshader")
        return QStringLiteral("MaterialX Standard Surface");
    if (shaderId == "ND_open_pbr_surface_surfaceshader")
        return QStringLiteral("MaterialX OpenPBR Surface");
    if (shaderId == "UsdPreviewSurface")
        return QStringLiteral("USD Preview Surface");
    return shaderId.isEmpty() ? QStringLiteral("Shader") : shaderId;
}

bool
MaterialUtils::isSupportedParameter(const MaterialEntry& entry, const QString& parameter)
{
    return entry.inputs.contains(parameter);
}

TfToken
MaterialUtils::inputName(const MaterialEntry& entry, const QString& parameter)
{
    const auto it = entry.inputs.constFind(parameter);
    return it == entry.inputs.cend() ? TfToken() : it->inputName;
}

SdfPath
MaterialUtils::inputPath(const MaterialEntry& entry, const QString& parameter)
{
    const auto it = entry.inputs.constFind(parameter);
    return it == entry.inputs.cend() ? SdfPath() : it->inputPath;
}

const MaterialInputInfo*
MaterialUtils::inputInfo(const MaterialEntry& entry, const QString& parameter)
{
    const auto it = entry.inputs.constFind(parameter);
    return it == entry.inputs.cend() ? nullptr : &it.value();
}

MaterialNodeInfo
MaterialUtils::nodeInfo(UsdStageRefPtr stage, const SdfPath& nodePath)
{
    MaterialNodeInfo result;
    if (!stage || nodePath.IsEmpty())
        return result;

    const UsdPrim prim = stage->GetPrimAtPath(nodePath);
    if (!prim)
        return result;

    result.path = nodePath;
    result.name = QString::fromStdString(prim.GetName().GetString());
    result.shaderId = shaderIdForPrim(prim);
    result.typeLabel = shaderTypeLabel(result.shaderId);

    const UsdShadeConnectableAPI connectable(prim);
    if (!connectable)
        return result;

    // MaterialX property editors should expose the complete NodeDef interface,
    // not only attributes already authored on the USD prim. Start with the
    // declared interface and then overlay the composed USD values/connections.
    if (result.shaderId.startsWith(QStringLiteral("ND_"))) {
        const MaterialXNodeDefinition* definition = materialXNodeDefinition(result.shaderId);

        if (definition) {
            QHash<QString, int> byName;
            for (const MaterialXPortDefinition& port : definition->inputs) {
                if (port.name.isEmpty() || byName.contains(port.name))
                    continue;

                MaterialInputInfo info;
                info.parameter = port.name;
                info.label = port.label.isEmpty() ? humanize(port.name) : port.label;
                info.group = materialXPortGroup(result.shaderId, port);
                info.inputName = TfToken(port.name.toStdString());
                info.inputPath = nodePath.AppendProperty(TfToken(std::string("inputs:") + port.name.toStdString()));
                info.typeName = materialXSdfType(port.type);
                info.options = port.enumValues;
                info.declared = true;
                info.hasAuthoredValue = false;

                info.defaultValue = materialXDefault(port.type, port.value);
                info.hasDefaultValue = !info.defaultValue.IsEmpty();
                info.value = info.defaultValue;
                info.hasValue = info.hasDefaultValue;

                byName.insert(port.name, static_cast<int>(result.inputs.size()));
                result.inputs.append(info);
            }

            // Overlay authored/composed USD state while preserving NodeDef UI
            // metadata and defaults. Inputs authored outside the NodeDef are
            // appended as custom inputs rather than hidden.
            for (const UsdShadeInput& input : connectable.GetInputs()) {
                MaterialInputInfo authored = inspectInput(input, result.shaderId);
                const QString name = QString::fromStdString(authored.inputName.GetString());
                const auto it = byName.constFind(name);
                if (it == byName.constEnd()) {
                    result.inputs.append(authored);
                    continue;
                }

                MaterialInputInfo& declared = result.inputs[*it];
                declared.inputPath = authored.inputPath;
                declared.typeName = authored.typeName;
                declared.hasAuthoredValue = authored.hasAuthoredValue;
                declared.connected = authored.connected;
                declared.sourcePrimPath = authored.sourcePrimPath;
                declared.sourceName = authored.sourceName;
                declared.sourceShaderId = authored.sourceShaderId;
                if (!authored.options.isEmpty())
                    declared.options = authored.options;
                if (authored.hasValue) {
                    declared.value = authored.value;
                    declared.hasValue = true;
                }
            }

            // Preserve declaration order. MaterialX standard-library NodeDefs
            // intentionally arrange inputs by UI folder, so this gives the tree
            // the same Base / Specular / Transmission / ... flow as MaterialX
            // property editors without another hard-coded sort pass.
            return result;
        }
    }

    for (const UsdShadeInput& input : connectable.GetInputs())
        result.inputs.append(inspectInput(input, result.shaderId));

    std::stable_sort(result.inputs.begin(), result.inputs.end(),
                     [](const MaterialInputInfo& a, const MaterialInputInfo& b) {
                         if (a.group != b.group)
                             return a.group < b.group;
                         return a.label < b.label;
                     });
    return result;
}

bool
MaterialUtils::ensureShaderInput(UsdStageRefPtr stage, const SdfPath& inputPath)
{
    if (!stage || !inputPath.IsPropertyPath())
        return false;

    // Already authored/composed: nothing to do.
    if (stage->GetAttributeAtPath(inputPath))
        return true;

    const SdfPath nodePath = inputPath.GetPrimPath();
    const MaterialNodeInfo node = nodeInfo(stage, nodePath);
    if (node.path.IsEmpty())
        return false;

    const MaterialInputInfo* declared = nullptr;
    for (const MaterialInputInfo& info : node.inputs) {
        if (info.inputPath == inputPath) {
            declared = &info;
            break;
        }
    }
    if (!declared || declared->inputName.IsEmpty() || declared->typeName.GetAsToken().IsEmpty())
        return false;

    const UsdPrim prim = stage->GetPrimAtPath(nodePath);
    UsdShadeShader shader(prim);
    if (!shader)
        return false;

    return static_cast<bool>(shader.CreateInput(declared->inputName, declared->typeName));
}

QStringList
MaterialUtils::networkDescription(UsdStageRefPtr stage, const SdfPath& inputPath, int maxDepth)
{
    QStringList lines;
    if (!stage || !inputPath.IsPropertyPath())
        return lines;

    const UsdAttribute attr = stage->GetAttributeAtPath(inputPath);
    if (!attr)
        return lines;

    const UsdShadeInput input(attr);
    QSet<QString> visited;
    describeInputRecursive(stage, input, 0, std::max(0, maxDepth), lines, visited);
    return lines;
}

namespace {

    QString materialXDefaultString(const VtValue& value)
    {
        if (value.IsEmpty())
            return {};
        if (value.IsHolding<float>())
            return QString::number(value.UncheckedGet<float>(), 'g', 9);
        if (value.IsHolding<double>())
            return QString::number(value.UncheckedGet<double>(), 'g', 17);
        if (value.IsHolding<int>())
            return QString::number(value.UncheckedGet<int>());
        if (value.IsHolding<bool>())
            return value.UncheckedGet<bool>() ? QStringLiteral("true") : QStringLiteral("false");
        if (value.IsHolding<std::string>())
            return QString::fromStdString(value.UncheckedGet<std::string>());
        if (value.IsHolding<TfToken>())
            return QString::fromStdString(value.UncheckedGet<TfToken>().GetString());
        if (value.IsHolding<SdfAssetPath>())
            return QString::fromStdString(value.UncheckedGet<SdfAssetPath>().GetAssetPath());
        if (value.IsHolding<GfVec2f>()) {
            const GfVec2f v = value.UncheckedGet<GfVec2f>();
            return QStringLiteral("%1, %2").arg(v[0], 0, 'g', 9).arg(v[1], 0, 'g', 9);
        }
        if (value.IsHolding<GfVec3f>()) {
            const GfVec3f v = value.UncheckedGet<GfVec3f>();
            return QStringLiteral("%1, %2, %3").arg(v[0], 0, 'g', 9).arg(v[1], 0, 'g', 9).arg(v[2], 0, 'g', 9);
        }
        if (value.IsHolding<GfVec4f>()) {
            const GfVec4f v = value.UncheckedGet<GfVec4f>();
            return QStringLiteral("%1, %2, %3, %4")
                .arg(v[0], 0, 'g', 9)
                .arg(v[1], 0, 'g', 9)
                .arg(v[2], 0, 'g', 9)
                .arg(v[3], 0, 'g', 9);
        }
        return {};
    }

    void mergeMaterialXDefinitionFromSdr(MaterialXNodeDefinition& def)
    {
        if (def.nodeDef.isEmpty())
            return;

        SdrRegistry& registry = SdrRegistry::GetInstance();

        // OpenUSD 25.11 has no public ParseAll() on SdrRegistry.
        // GetShaderNodesByIdentifier() parses the matching discovered nodes on demand,
        // which is exactly what we need here and avoids front-loading the whole registry.

        // A NodeDef identifier may be represented by more than one Sdr source type.
        // Do not accept the registry's arbitrary first match if it happens to be the
        // sparse representation. Prefer the parsed representation with the richest
        // interface so the authored UsdShade node gets the complete MaterialX ports.
        SdrShaderNodeConstPtr node;
        size_t bestInputCount = 0;
        const auto candidates = registry.GetShaderNodesByIdentifier(TfToken(def.nodeDef.toStdString()));
        for (const auto& candidate : candidates) {
            if (!candidate)
                continue;
            const size_t inputCount = candidate->GetShaderInputNames().size();
            if (!node || inputCount > bestInputCount) {
                node = candidate;
                bestInputCount = inputCount;
            }
        }

        if (!node)
            node = registry.GetShaderNodeByIdentifier(TfToken(def.nodeDef.toStdString()));
        if (!node)
            return;

        auto mergePort = [](QList<MaterialXPortDefinition>& ports, const MaterialXPortDefinition& incoming) {
            auto it = std::find_if(ports.begin(), ports.end(),
                                   [&](const MaterialXPortDefinition& port) { return port.name == incoming.name; });
            if (it == ports.end()) {
                ports.append(incoming);
                return;
            }
            if (it->type.isEmpty())
                it->type = incoming.type;
            if (it->value.isEmpty())
                it->value = incoming.value;
            if (it->label.isEmpty())
                it->label = incoming.label;
            if (it->group.isEmpty())
                it->group = incoming.group;
            for (const QString& option : incoming.enumValues) {
                if (!it->enumValues.contains(option))
                    it->enumValues.append(option);
            }
        };

        for (const TfToken& name : node->GetShaderInputNames()) {
            const SdrShaderPropertyConstPtr property = node->GetShaderInput(name);
            if (!property)
                continue;

            MaterialXPortDefinition port;
            port.name = QString::fromStdString(name.GetString());
            port.type = QString::fromStdString(property->GetType().GetString());
            port.value = materialXDefaultString(property->GetDefaultValue());

            mergePort(def.inputs, port);
        }

        for (const TfToken& name : node->GetShaderOutputNames()) {
            const SdrShaderPropertyConstPtr property = node->GetShaderOutput(name);
            if (!property)
                continue;

            MaterialXPortDefinition port;
            port.name = QString::fromStdString(name.GetString());
            port.type = QString::fromStdString(property->GetType().GetString());
            mergePort(def.outputs, port);
        }

        if (def.outputType.isEmpty() && !def.outputs.isEmpty()) {
            const auto outIt
                = std::find_if(def.outputs.cbegin(), def.outputs.cend(),
                               [](const MaterialXPortDefinition& port) { return port.name == QStringLiteral("out"); });
            def.outputType = outIt != def.outputs.cend() ? outIt->type : def.outputs.first().type;
        }
    }

}  // namespace

QList<MaterialXNodeDefinition>
MaterialUtils::materialXNodeDefinitions()
{
    static const QList<MaterialXNodeDefinition> definitions = []() {
        QElapsedTimer timer;
        timer.start();

        QList<MaterialXNodeDefinition> result;
        QHash<QString, int> definitionIndex;
        QHash<QString, QString> inheritByNodeDef;

        auto mergePort = [](QList<MaterialXPortDefinition>& ports, const MaterialXPortDefinition& incoming) {
            auto it = std::find_if(ports.begin(), ports.end(),
                                   [&](const MaterialXPortDefinition& port) { return port.name == incoming.name; });
            if (it == ports.end()) {
                ports.append(incoming);
                return;
            }

            if (it->type.isEmpty())
                it->type = incoming.type;
            if (it->value.isEmpty())
                it->value = incoming.value;
            if (it->label.isEmpty())
                it->label = incoming.label;
            if (it->group.isEmpty())
                it->group = incoming.group;
            for (const QString& value : incoming.enumValues) {
                if (!it->enumValues.contains(value))
                    it->enumValues.append(value);
            }
        };

        auto mergeDefinition = [&](MaterialXNodeDefinition& existing, const MaterialXNodeDefinition& incoming) {
            if (existing.node.isEmpty())
                existing.node = incoming.node;
            if (existing.group.isEmpty() || existing.group == QStringLiteral("Other"))
                existing.group = incoming.group;
            if (existing.outputType.isEmpty())
                existing.outputType = incoming.outputType;

            for (const MaterialXPortDefinition& port : incoming.inputs)
                mergePort(existing.inputs, port);
            for (const MaterialXPortDefinition& port : incoming.outputs)
                mergePort(existing.outputs, port);
        };

        for (const QString& root : materialXLibraryRoots()) {
            QDirIterator it(root, { QStringLiteral("*.mtlx") }, QDir::Files, QDirIterator::Subdirectories);
            while (it.hasNext()) {
                QFile file(it.next());
                if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
                    continue;

                QXmlStreamReader xml(&file);
                while (!xml.atEnd()) {
                    xml.readNext();
                    if (!xml.isStartElement() || xml.name() != QStringLiteral("nodedef"))
                        continue;

                    const auto attrs = xml.attributes();
                    MaterialXNodeDefinition def;
                    def.nodeDef = attrs.value("name").toString();
                    def.node = attrs.value("node").toString();
                    def.group = attrs.value("nodegroup").toString();
                    def.outputType = attrs.value("type").toString();
                    const QString inheritedNodeDef = attrs.value("inherit").toString();

                    // Capture the complete NodeDef interface. A UsdShadeShader does
                    // not automatically materialize its NodeDef inputs/outputs when
                    // only info:id is authored, so the graph editor needs these
                    // declarations when it creates a free MaterialX node.
                    while (!xml.atEnd()) {
                        xml.readNext();
                        if (xml.isEndElement() && xml.name() == QStringLiteral("nodedef"))
                            break;
                        if (!xml.isStartElement())
                            continue;

                        const auto element = xml.name();
                        if (element != QStringLiteral("input") && element != QStringLiteral("output"))
                            continue;

                        const auto portAttrs = xml.attributes();
                        MaterialXPortDefinition port;
                        port.name = portAttrs.value("name").toString();
                        port.type = portAttrs.value("type").toString();
                        port.value = portAttrs.value("value").toString();
                        port.label = portAttrs.value("uiname").toString();
                        port.group = portAttrs.value("uifolder").toString();

                        QString enumText = portAttrs.value("enum").toString();
                        if (enumText.isEmpty())
                            enumText = portAttrs.value("enumvalues").toString();
                        if (!enumText.isEmpty()) {
                            for (const QString& value : enumText.split(',', Qt::SkipEmptyParts)) {
                                const QString trimmed = value.trimmed();
                                if (!trimmed.isEmpty() && !port.enumValues.contains(trimmed))
                                    port.enumValues.append(trimmed);
                            }
                        }

                        if (port.name.isEmpty())
                            continue;

                        if (element == QStringLiteral("input"))
                            def.inputs.append(port);
                        else
                            def.outputs.append(port);
                    }

                    // Newer MaterialX libraries commonly declare the NodeDef output
                    // on an <output> child rather than on the <nodedef type=...>
                    // attribute. Normalize that here so typed overloads such as
                    // ND_image_color3 / ND_image_color4 / ND_image_vector3 are
                    // represented correctly in the editor menus.

                    if (def.outputType.isEmpty() && !def.outputs.isEmpty()) {
                        const auto outIt = std::find_if(def.outputs.cbegin(), def.outputs.cend(),
                                                        [](const MaterialXPortDefinition& port) {
                                                            return port.name == QStringLiteral("out");
                                                        });
                        def.outputType = outIt != def.outputs.cend() ? outIt->type : def.outputs.first().type;
                    }

                    if (def.nodeDef.isEmpty() || def.node.isEmpty())
                        continue;
                    if (def.group.isEmpty())
                        def.group = QStringLiteral("Other");

                    if (!inheritedNodeDef.isEmpty() && !inheritByNodeDef.contains(def.nodeDef))
                        inheritByNodeDef.insert(def.nodeDef, inheritedNodeDef);

                    const auto existing = definitionIndex.constFind(def.nodeDef);
                    if (existing == definitionIndex.constEnd()) {
                        definitionIndex.insert(def.nodeDef, result.size());
                        result.append(def);
                    }
                    else {
                        // The same NodeDef can be visible through more than one
                        // MaterialX/USD search root. Some copies contain only the
                        // identity/output while another contains the complete
                        // interface. Merge them instead of letting the first copy
                        // win, otherwise newly-created nodes can end up with no
                        // authored inputs.
                        mergeDefinition(result[*existing], def);
                    }
                }
            }
        }

        // MaterialX NodeDefs may inherit their interface from another NodeDef.
        // The raw XML parser above only sees ports authored directly on the child,
        // so resolve inheritance before consulting Sdr. This is especially
        // important for standard-library nodes such as image variants in builds
        // where the typed NodeDef carries little or no local interface data.
        QSet<QString> resolving;
        QSet<QString> resolved;
        std::function<void(const QString&)> resolveInheritedInterface;
        resolveInheritedInterface = [&](const QString& nodeDef) {
            if (nodeDef.isEmpty() || resolved.contains(nodeDef) || resolving.contains(nodeDef))
                return;

            const auto indexIt = definitionIndex.constFind(nodeDef);
            if (indexIt == definitionIndex.constEnd())
                return;

            resolving.insert(nodeDef);
            const QString parentName = inheritByNodeDef.value(nodeDef);
            if (!parentName.isEmpty()) {
                resolveInheritedInterface(parentName);
                const auto parentIt = definitionIndex.constFind(parentName);
                if (parentIt != definitionIndex.constEnd()) {
                    MaterialXNodeDefinition& child = result[*indexIt];
                    const MaterialXNodeDefinition& parent = result[*parentIt];

                    for (const MaterialXPortDefinition& port : parent.inputs)
                        mergePort(child.inputs, port);
                    for (const MaterialXPortDefinition& port : parent.outputs)
                        mergePort(child.outputs, port);

                    if (child.outputType.isEmpty())
                        child.outputType = parent.outputType;
                    if (child.group.isEmpty() || child.group == QStringLiteral("Other"))
                        child.group = parent.group;
                }
            }
            resolving.remove(nodeDef);
            resolved.insert(nodeDef);
        };

        for (const MaterialXNodeDefinition& def : std::as_const(result))
            resolveInheritedInterface(def.nodeDef);

        // The raw MaterialX XML scan is useful for grouping and metadata, but the
        // authoritative node interface in an OpenUSD application is Sdr. In
        // particular, some packaged MaterialX library layouts expose discovery
        // entries without all inherited/default inputs in the file we happen to
        // scan. Merge the parsed Sdr interface so newly-authored nodes always get
        // the same inputs/outputs OpenUSD knows for the NodeDef.
        for (MaterialXNodeDefinition& def : result)
            mergeMaterialXDefinitionFromSdr(def);

        std::sort(result.begin(), result.end(), [](const MaterialXNodeDefinition& a, const MaterialXNodeDefinition& b) {
            if (a.group != b.group)
                return a.group.localeAwareCompare(b.group) < 0;
            if (a.node != b.node)
                return a.node.localeAwareCompare(b.node) < 0;
            return a.nodeDef < b.nodeDef;
        });
        qDebug().noquote() << "[MaterialPerf][Utils] MaterialX definitions cache" << result.size() << "definitions"
                           << timer.elapsed() << "ms";
        return result;
    }();

    return definitions;
}

QList<MaterialXNodeDefinition>
MaterialUtils::compatibleMaterialXNodes(const SdfValueTypeName& targetType)
{
    const QString target = materialXTypeForSdf(targetType);
    if (target.isEmpty())
        return {};

    QList<MaterialXNodeDefinition> result;
    for (const MaterialXNodeDefinition& def : materialXNodeDefinitions()) {
        if (def.outputType == target)
            result.append(def);
    }
    return result;
}

QList<ShaderNodeDefinition>
MaterialUtils::usdShaderNodes()
{
    QList<ShaderNodeDefinition> result;
    QSet<QString> seen;

    auto append = [&](const QString& shaderId, const QString& node, const QString& group, const TfToken& outputName,
                      const SdfValueTypeName& outputType) {
        if (shaderId.isEmpty() || outputName.IsEmpty() || outputType.GetAsToken().IsEmpty())
            return;

        const QString key = QStringLiteral("%1\n%2").arg(shaderId, QString::fromStdString(outputName.GetString()));
        if (seen.contains(key))
            return;
        seen.insert(key);

        ShaderNodeDefinition def;
        def.shaderId = shaderId;
        def.node = node;
        def.group = group.isEmpty() ? QStringLiteral("Other") : group;
        def.outputName = outputName;
        def.outputType = outputType;
        result.append(def);
    };

    auto displayGroup = [](const QString& identifier, const SdrShaderNodeConstPtr& node) {
        if (node) {
            const QString category = QString::fromStdString(node->GetCategory().GetString());
            if (!category.isEmpty())
                return category;

            const QString family = QString::fromStdString(node->GetFamily().GetString());
            if (!family.isEmpty())
                return family;
        }

        if (identifier.contains(QStringLiteral("Primvar"), Qt::CaseInsensitive))
            return QStringLiteral("Geometry");
        if (identifier.contains(QStringLiteral("Texture"), Qt::CaseInsensitive)
            || identifier.contains(QStringLiteral("Transform2d"), Qt::CaseInsensitive))
            return QStringLiteral("Texture");

        return QStringLiteral("Other");
    };

    auto displayName = [](const QString& identifier, const SdrShaderNodeConstPtr& node) {
        if (node) {
            const QString label = QString::fromStdString(node->GetLabel().GetString());
            if (!label.isEmpty())
                return label;

            const QString name = QString::fromStdString(node->GetName());
            if (!name.isEmpty())
                return humanize(name);
        }

        QString name = identifier;
        if (name.startsWith(QStringLiteral("Usd")))
            name.remove(0, 3);
        return humanize(name);
    };

    SdrRegistry& registry = SdrRegistry::GetInstance();
    const SdrIdentifierVec identifiers = registry.GetShaderNodeIdentifiers();

    for (const SdrIdentifier& identifierToken : identifiers) {
        const QString identifier = QString::fromStdString(identifierToken.GetString());

        if (!identifier.startsWith(QStringLiteral("Usd")))
            continue;
        if (identifier == QStringLiteral("UsdPreviewSurface"))
            continue;

        SdrShaderNodeConstPtr node;
        size_t bestPropertyCount = 0;

        const auto candidates = registry.GetShaderNodesByIdentifier(identifierToken);
        for (const auto& candidate : candidates) {
            if (!candidate || !candidate->IsValid())
                continue;

            const size_t propertyCount = candidate->GetShaderInputNames().size()
                                         + candidate->GetShaderOutputNames().size();

            if (!node || propertyCount > bestPropertyCount) {
                node = candidate;
                bestPropertyCount = propertyCount;
            }
        }

        if (!node)
            node = registry.GetShaderNodeByIdentifier(identifierToken);
        if (!node || !node->IsValid())
            continue;

        const SdrTokenVec& outputs = node->GetShaderOutputNames();
        if (outputs.empty())
            continue;

        const QString baseName = displayName(identifier, node);
        const QString group = displayGroup(identifier, node);

        for (const TfToken& outputName : outputs) {
            const SdrShaderPropertyConstPtr output = node->GetShaderOutput(outputName);
            if (!output)
                continue;

            const SdrSdfTypeIndicator type = output->GetTypeAsSdfType();
            const SdfValueTypeName outputType = type.GetSdfType();
            if (outputType.GetAsToken().IsEmpty())
                continue;

            QString nodeName = baseName;
            if (outputs.size() > 1) {
                nodeName += QStringLiteral(" %1").arg(QString::fromStdString(outputName.GetString()).toUpper());
            }

            append(identifier, nodeName, group, outputName, outputType);
        }
    }

    append(QStringLiteral("UsdUVTexture"), QStringLiteral("UV Texture"), QStringLiteral("Texture"), TfToken("rgb"),
           SdfValueTypeNames->Float3);

    append(QStringLiteral("UsdTransform2d"), QStringLiteral("Transform 2D"), QStringLiteral("Texture"),
           TfToken("result"), SdfValueTypeNames->Float2);

    append(QStringLiteral("UsdPrimvarReader_float"), QStringLiteral("Primvar Reader float"), QStringLiteral("Geometry"),
           TfToken("result"), SdfValueTypeNames->Float);

    append(QStringLiteral("UsdPrimvarReader_float2"), QStringLiteral("Primvar Reader float2"),
           QStringLiteral("Geometry"), TfToken("result"), SdfValueTypeNames->Float2);

    append(QStringLiteral("UsdPrimvarReader_float3"), QStringLiteral("Primvar Reader float3"),
           QStringLiteral("Geometry"), TfToken("result"), SdfValueTypeNames->Float3);

    append(QStringLiteral("UsdPrimvarReader_float4"), QStringLiteral("Primvar Reader float4"),
           QStringLiteral("Geometry"), TfToken("result"), SdfValueTypeNames->Float4);

    append(QStringLiteral("UsdPrimvarReader_int"), QStringLiteral("Primvar Reader int"), QStringLiteral("Geometry"),
           TfToken("result"), SdfValueTypeNames->Int);

    append(QStringLiteral("UsdPrimvarReader_string"), QStringLiteral("Primvar Reader string"),
           QStringLiteral("Geometry"), TfToken("result"), SdfValueTypeNames->String);

    std::stable_sort(result.begin(), result.end(), [](const ShaderNodeDefinition& a, const ShaderNodeDefinition& b) {
        if (a.group != b.group)
            return a.group.localeAwareCompare(b.group) < 0;
        if (a.node != b.node)
            return a.node.localeAwareCompare(b.node) < 0;
        return a.shaderId < b.shaderId;
    });

    return result;
}

QList<ShaderNodeDefinition>
MaterialUtils::compatibleUsdShaderNodes(const SdfValueTypeName& targetType)
{
    QList<ShaderNodeDefinition> result;

    for (const ShaderNodeDefinition& def : usdShaderNodes()) {
        const bool vec2Compatible
            = (targetType == SdfValueTypeNames->Float2 && def.outputType == SdfValueTypeNames->TexCoord2f)
              || (targetType == SdfValueTypeNames->TexCoord2f && def.outputType == SdfValueTypeNames->Float2);

        // USD Preview Surface does not treat every storage-equivalent Float3
        // output as a safe Color3f source. In particular, Storm can crash when
        // UsdPrimvarReader_float3.result is wired directly to diffuseColor.
        // Keep the well-established UsdUVTexture.rgb -> Color3f exception, but
        // otherwise require an exact type match for 3-component values.
        const bool uvTextureColorCompatible = targetType == SdfValueTypeNames->Color3f
                                              && def.shaderId == QStringLiteral("UsdUVTexture")
                                              && def.outputName == TfToken("rgb")
                                              && def.outputType == SdfValueTypeNames->Float3;

        if (def.outputType == targetType || vec2Compatible || uvTextureColorCompatible)
            result.append(def);
    }

    return result;
}

SdfPath
MaterialUtils::uniqueMaterialPath(UsdStageRefPtr stage, const QString& baseName)
{
    if (!stage)
        return {};

    const SdfPath root("/Materials");
    if (!stage->GetPrimAtPath(root))
        stage->DefinePrim(root, TfToken("Scope"));

    const QString cleanBase = sanitizeIdentifier(baseName);
    QString name = cleanBase;
    SdfPath path = root.AppendChild(TfToken(name.toStdString()));
    int suffix = 1;
    while (stage->GetPrimAtPath(path)) {
        name = QString("%1%2").arg(cleanBase).arg(suffix++);
        path = root.AppendChild(TfToken(name.toStdString()));
    }
    return path;
}

SdfPath
MaterialUtils::createPreviewSurfaceMaterial(UsdStageRefPtr stage)
{
    const SdfPath path = uniqueMaterialPath(stage);
    if (path.IsEmpty())
        return {};

    UsdShadeMaterial material = UsdShadeMaterial::Define(stage, path);
    UsdShadeShader shader = UsdShadeShader::Define(stage, path.AppendChild(TfToken("PreviewSurface")));
    shader.CreateIdAttr(VtValue(TfToken("UsdPreviewSurface")));
    shader.CreateInput(TfToken("diffuseColor"), SdfValueTypeNames->Color3f).Set(GfVec3f(0.18f));
    shader.CreateInput(TfToken("metallic"), SdfValueTypeNames->Float).Set(0.0f);
    shader.CreateInput(TfToken("roughness"), SdfValueTypeNames->Float).Set(0.5f);
    shader.CreateInput(TfToken("opacity"), SdfValueTypeNames->Float).Set(1.0f);
    shader.CreateInput(TfToken("ior"), SdfValueTypeNames->Float).Set(1.5f);
    shader.CreateInput(TfToken("clearcoat"), SdfValueTypeNames->Float).Set(0.0f);
    shader.CreateInput(TfToken("clearcoatRoughness"), SdfValueTypeNames->Float).Set(0.01f);

    const UsdShadeOutput output = shader.CreateOutput(TfToken("surface"), SdfValueTypeNames->Token);
    material.CreateSurfaceOutput().ConnectToSource(output);
    return path;
}

SdfPath
MaterialUtils::createStandardSurfaceMaterial(UsdStageRefPtr stage)
{
    const SdfPath path = uniqueMaterialPath(stage);
    if (!path.IsEmpty())
        authorStandardSurface(stage, path, MaterialParameters());
    return path;
}

SdfPath
MaterialUtils::createOpenPBRSurfaceMaterial(UsdStageRefPtr stage)
{
    const SdfPath path = uniqueMaterialPath(stage);
    if (!path.IsEmpty())
        authorOpenPBRSurface(stage, path);
    return path;
}

namespace {

    struct MtlxInputDesc {
        QString name;
        QString type;
        QString value;
        QString nodeName;
        QString nodeGraph;
        QString output;
    };

    struct MtlxNodeDesc {
        QString category;
        QString name;
        QString type;
        QList<MtlxInputDesc> inputs;
    };

    struct MtlxGraphOutputDesc {
        QString name;
        QString type;
        QString nodeName;
        QString output;
    };

    struct MtlxGraphDesc {
        QString name;
        QList<MtlxNodeDesc> nodes;
        QList<MtlxGraphOutputDesc> outputs;
    };

    struct MtlxSurfaceDesc {
        QString name;
        QList<MtlxInputDesc> inputs;
    };

    struct MtlxMaterialDesc {
        QString name;
        QString surfaceNode;
    };

    MtlxInputDesc readMtlxInput(const QXmlStreamAttributes& attrs)
    {
        MtlxInputDesc input;
        input.name = attrs.value(QStringLiteral("name")).toString();
        input.type = attrs.value(QStringLiteral("type")).toString();
        input.value = attrs.value(QStringLiteral("value")).toString();
        input.nodeName = attrs.value(QStringLiteral("nodename")).toString();
        input.nodeGraph = attrs.value(QStringLiteral("nodegraph")).toString();
        input.output = attrs.value(QStringLiteral("output")).toString();
        return input;
    }

    MtlxNodeDesc readMtlxNode(QXmlStreamReader& xml)
    {
        MtlxNodeDesc node;
        node.category = xml.name().toString();
        const auto attrs = xml.attributes();
        node.name = attrs.value(QStringLiteral("name")).toString();
        node.type = attrs.value(QStringLiteral("type")).toString();
        const QString element = node.category;

        while (!xml.atEnd()) {
            xml.readNext();
            if (xml.isStartElement() && xml.name() == QStringLiteral("input"))
                node.inputs.append(readMtlxInput(xml.attributes()));
            else if (xml.isEndElement() && xml.name() == element)
                break;
        }
        return node;
    }

    SdfValueTypeName mtlxSdfType(const QString& type)
    {
        if (type == QStringLiteral("float"))
            return SdfValueTypeNames->Float;
        if (type == QStringLiteral("color3"))
            return SdfValueTypeNames->Color3f;
        if (type == QStringLiteral("color4"))
            return SdfValueTypeNames->Color4f;
        if (type == QStringLiteral("vector2"))
            return SdfValueTypeNames->Float2;
        if (type == QStringLiteral("vector3"))
            return SdfValueTypeNames->Float3;
        if (type == QStringLiteral("vector4"))
            return SdfValueTypeNames->Float4;
        if (type == QStringLiteral("integer"))
            return SdfValueTypeNames->Int;
        if (type == QStringLiteral("boolean"))
            return SdfValueTypeNames->Bool;
        if (type == QStringLiteral("filename"))
            return SdfValueTypeNames->Asset;
        if (type == QStringLiteral("string"))
            return SdfValueTypeNames->String;
        if (type == QStringLiteral("surfaceshader") || type == QStringLiteral("material"))
            return SdfValueTypeNames->Token;
        return SdfValueTypeNames->Token;
    }

    QStringList splitMtlxValue(QString value)
    {
        value.replace(',', ' ');
        return value.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
    }

    bool setMtlxValue(const UsdShadeInput& input, const QString& type, const QString& text, const QDir& documentDir)
    {
        if (!input || text.isNull())
            return false;

        bool ok = false;
        if (type == QStringLiteral("float")) {
            const float value = text.toFloat(&ok);
            return ok && input.GetAttr().Set(VtValue(value));
        }
        if (type == QStringLiteral("integer")) {
            const int value = text.toInt(&ok);
            return ok && input.GetAttr().Set(VtValue(value));
        }
        if (type == QStringLiteral("boolean")) {
            const QString value = text.trimmed().toLower();
            if (value != QStringLiteral("true") && value != QStringLiteral("false"))
                return false;
            return input.GetAttr().Set(VtValue(value == QStringLiteral("true")));
        }
        if (type == QStringLiteral("color3") || type == QStringLiteral("vector3")) {
            const QStringList values = splitMtlxValue(text);
            if (values.size() < 3)
                return false;
            bool a = false, b = false, c = false;
            const float x = values[0].toFloat(&a);
            const float y = values[1].toFloat(&b);
            const float z = values[2].toFloat(&c);
            return a && b && c && input.GetAttr().Set(VtValue(GfVec3f(x, y, z)));
        }
        if (type == QStringLiteral("color4") || type == QStringLiteral("vector4")) {
            const QStringList values = splitMtlxValue(text);
            if (values.size() < 4)
                return false;
            bool a = false, b = false, c = false, d = false;
            const float x = values[0].toFloat(&a);
            const float y = values[1].toFloat(&b);
            const float z = values[2].toFloat(&c);
            const float w = values[3].toFloat(&d);
            return a && b && c && d && input.GetAttr().Set(VtValue(GfVec4f(x, y, z, w)));
        }
        if (type == QStringLiteral("vector2")) {
            const QStringList values = splitMtlxValue(text);
            if (values.size() < 2)
                return false;
            bool a = false, b = false;
            const float x = values[0].toFloat(&a);
            const float y = values[1].toFloat(&b);
            return a && b && input.GetAttr().Set(VtValue(GfVec2f(x, y)));
        }
        if (type == QStringLiteral("filename")) {
            QString path = text.trimmed();
            if (!path.isEmpty() && QFileInfo(path).isRelative())
                path = QDir::cleanPath(documentDir.absoluteFilePath(path));
            return input.GetAttr().Set(VtValue(SdfAssetPath(path.toStdString())));
        }
        if (type == QStringLiteral("string"))
            return input.GetAttr().Set(VtValue(text.toStdString()));

        return input.GetAttr().Set(VtValue(TfToken(text.toStdString())));
    }

    QString mtlxShaderId(const QString& category, const QString& type)
    {
        if (category == QStringLiteral("standard_surface"))
            return QStringLiteral("ND_standard_surface_surfaceshader");
        return QStringLiteral("ND_%1_%2").arg(category, type);
    }

}  // namespace

bool
MaterialUtils::importMaterialX(UsdStageRefPtr stage, const QString& filename, QList<SdfPath>& createdPaths,
                               QString& error)
{
    createdPaths.clear();
    error.clear();

    if (!stage) {
        error = QStringLiteral("No USD stage");
        return false;
    }

    const QFileInfo fileInfo(filename);
    const QString resolvedFilename = fileInfo.canonicalFilePath().isEmpty() ? fileInfo.absoluteFilePath()
                                                                            : fileInfo.canonicalFilePath();
    if (!QFileInfo::exists(resolvedFilename)) {
        error = QString("Could not open %1").arg(filename);
        return false;
    }

    // Let OpenUSD parse and translate the MaterialX document. This is the
    // important distinction from the previous importer: UsdMtlxRead preserves
    // MaterialX node definitions, node graphs, source-URI/file semantics and
    // the shading network conventions expected by Hydra/Storm.
    const MaterialX::DocumentPtr document = UsdMtlxReadDocument(resolvedFilename.toStdString());
    if (!document) {
        error = QString("Could not read MaterialX document: %1").arg(resolvedFilename);
        return false;
    }

    // Keep each import self-contained so repeated imports never overwrite an
    // existing MaterialX network. UsdMtlxRead creates its own Materials scope
    // below internalPath, so the resulting material will typically live at:
    //
    //   /Materials/<file>_MaterialX/Materials/<material>
    //
    // MaterialBrowser traverses the complete stage, therefore the extra scope
    // is intentionally invisible to normal material browsing while preserving
    // every path authored by the official reader.
    if (!stage->GetPrimAtPath(SdfPath("/Materials")))
        stage->DefinePrim(SdfPath("/Materials"), TfToken("Scope"));

    QString base = sanitizeIdentifier(fileInfo.completeBaseName());
    if (base.isEmpty())
        base = QStringLiteral("MaterialX");

    const SdfPath materialsRoot("/Materials");
    QString importName = base + QStringLiteral("_MaterialX");
    SdfPath importRoot = materialsRoot.AppendChild(TfToken(importName.toStdString()));
    int suffix = 1;
    while (stage->GetPrimAtPath(importRoot)) {
        importName = QStringLiteral("%1_MaterialX%2").arg(base).arg(suffix++);
        importRoot = materialsRoot.AppendChild(TfToken(importName.toStdString()));
    }

    const SdfPath externalRoot = importRoot.AppendChild(TfToken("__ModelRoot"));

    const bool hadMetersPerUnit = UsdGeomStageHasAuthoredMetersPerUnit(stage);
    const double metersPerUnit = UsdGeomGetStageMetersPerUnit(stage);
    const TfToken upAxis = UsdGeomGetStageUpAxis(stage);
    const UsdPrim defaultPrim = stage->GetDefaultPrim();
    const SdfPath defaultPrimPath = defaultPrim ? defaultPrim.GetPath() : SdfPath();

    UsdMtlxRead(document, stage, importRoot, externalRoot);

    // UsdMtlxRead authors MaterialX filename inputs into the destination USD
    // layer. Relative filenames are therefore no longer anchored by the .mtlx
    // document automatically. Anchor imported asset inputs to the MaterialX
    // document directory once at import time so Hydra, the material swatch and
    // the standalone image-node preview all resolve the same texture.
    //
    // Keep the original relative spelling when the file cannot be resolved;
    // custom resolver setups may still be able to satisfy it later.
    int anchoredTexturePaths = 0;
    const QDir materialXDir(fileInfo.absolutePath());
    const UsdPrim importedForAssets = stage->GetPrimAtPath(importRoot);
    if (importedForAssets) {
        for (const UsdPrim& prim : UsdPrimRange(importedForAssets)) {
            if (!prim || !prim.IsA<UsdShadeShader>())
                continue;

            const UsdShadeShader shader(prim);
            for (const UsdShadeInput& input : shader.GetInputs()) {
                if (!input || input.GetTypeName() != SdfValueTypeNames->Asset)
                    continue;

                SdfAssetPath asset;
                if (!input.Get(&asset))
                    continue;

                const QString authored = QString::fromStdString(asset.GetAssetPath()).trimmed();
                if (authored.isEmpty() || !QFileInfo(authored).isRelative())
                    continue;

                const QString anchored = QDir::cleanPath(materialXDir.absoluteFilePath(authored));
                if (!QFileInfo::exists(anchored))
                    continue;

                if (input.Set(SdfAssetPath(anchored.toStdString())))
                    ++anchoredTexturePaths;
            }
        }
    }

    qDebug().noquote() << "[MaterialPerf][Utils] importMaterialX anchored texture paths" << anchoredTexturePaths
                       << "from" << resolvedFilename;

    if (hadMetersPerUnit) {
        UsdGeomSetStageMetersPerUnit(stage, metersPerUnit);
    }
    else {
        stage->ClearMetadata(UsdGeomTokens->metersPerUnit);
    }

    UsdGeomSetStageUpAxis(stage, upAxis);

    if (!defaultPrimPath.IsEmpty()) {
        const UsdPrim restoredDefault = stage->GetPrimAtPath(defaultPrimPath);
        if (restoredDefault)
            stage->SetDefaultPrim(restoredDefault);
    }
    else {
        stage->ClearDefaultPrim();
    }

    // Discover exactly the materials created below this import root. Do not
    // rely on MaterialX element names here; the official reader is authoritative
    // about the USD namespace it generated.
    const UsdPrim importedRoot = stage->GetPrimAtPath(importRoot);
    if (importedRoot) {
        for (const UsdPrim& prim : UsdPrimRange(importedRoot)) {
            if (prim && prim.IsA<UsdShadeMaterial>())
                createdPaths.append(prim.GetPath());
        }
    }

    if (createdPaths.isEmpty()) {
        // Avoid leaving an empty import scope behind when the document does not
        // contain a translatable material.
        stage->RemovePrim(importRoot);
        error = QStringLiteral("MaterialX document did not produce any USD materials.");
        return false;
    }

    return true;
}

}  // namespace stageviz
