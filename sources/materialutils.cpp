// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#include "materialutils.h"
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QXmlStreamReader>
#include <algorithm>
#include <pxr/base/vt/value.h>
#include <pxr/usd/usd/primRange.h>
#include <pxr/usd/usdShade/output.h>

PXR_NAMESPACE_USING_DIRECTIVE

namespace stageviz {

namespace {

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

}  // namespace

UsdShadeShader
MaterialUtils::surfaceShader(const UsdShadeMaterial& material, QString* shaderId)
{
    if (shaderId)
        shaderId->clear();

    if (!material)
        return UsdShadeShader();

    const UsdPrim materialPrim = material.GetPrim();
    for (const UsdPrim& prim : UsdPrimRange(materialPrim)) {
        if (prim == materialPrim || !prim.IsA<UsdShadeShader>())
            continue;

        const UsdShadeShader shader(prim);
        TfToken id;
        shader.GetIdAttr().Get(&id);

        const QString value = QString::fromStdString(id.GetString());
        if (value == "ND_standard_surface_surfaceshader" || value == "UsdPreviewSurface") {
            if (shaderId)
                *shaderId = value;
            return shader;
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
    if (shaderId == "UsdPreviewSurface")
        return QStringLiteral("UsdPreviewSurface");
    return shaderId;
}

bool
MaterialUtils::isSupportedParameter(const MaterialEntry& entry, const QString& parameter)
{
    if (entry.shaderId == "ND_standard_surface_surfaceshader")
        return true;

    if (entry.shaderId == "UsdPreviewSurface") {
        return parameter == "baseColor" || parameter == "metalness" || parameter == "roughness" || parameter == "ior"
               || parameter == "coat" || parameter == "coatRoughness" || parameter == "opacity";
    }
    return false;
}

TfToken
MaterialUtils::inputName(const MaterialEntry& entry, const QString& parameter)
{
    if (entry.shaderId == "ND_standard_surface_surfaceshader") {
        if (parameter == "baseColor")
            return TfToken("base_color");
        if (parameter == "metalness")
            return TfToken("metalness");
        if (parameter == "roughness")
            return TfToken("roughness");
        if (parameter == "specular")
            return TfToken("specular");
        if (parameter == "ior")
            return TfToken("specular_IOR");
        if (parameter == "coat")
            return TfToken("coat");
        if (parameter == "coatRoughness")
            return TfToken("coat_roughness");
        if (parameter == "opacity")
            return TfToken("opacity");
        if (parameter == "transmission")
            return TfToken("transmission");
        if (parameter == "transmissionColor")
            return TfToken("transmission_color");
    }

    if (entry.shaderId == "UsdPreviewSurface") {
        if (parameter == "baseColor")
            return TfToken("diffuseColor");
        if (parameter == "metalness")
            return TfToken("metallic");
        if (parameter == "roughness")
            return TfToken("roughness");
        if (parameter == "ior")
            return TfToken("ior");
        if (parameter == "coat")
            return TfToken("clearcoat");
        if (parameter == "coatRoughness")
            return TfToken("clearcoatRoughness");
        if (parameter == "opacity")
            return TfToken("opacity");
    }

    return TfToken();
}

SdfPath
MaterialUtils::inputPath(const MaterialEntry& entry, const QString& parameter)
{
    const TfToken name = inputName(entry, parameter);
    if (name.IsEmpty())
        return {};
    return entry.shaderPath.AppendProperty(TfToken("inputs:" + name.GetString()));
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

    QFile file(filename);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        error = QString("Could not open %1").arg(filename);
        return false;
    }

    struct Imported {
        QString name;
        MaterialParameters parameters;
    };

    QList<Imported> imports;
    QXmlStreamReader xml(&file);
    Imported current;
    bool inSurface = false;

    auto parseFloat = [](const QString& text, float fallback) {
        bool ok = false;
        const float value = text.toFloat(&ok);
        return ok ? value : fallback;
    };

    auto parseColor = [](QString text, const GfVec3f& fallback) {
        text.replace(',', ' ');
        const QStringList parts = text.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
        if (parts.size() < 3)
            return fallback;

        bool a = false, b = false, c = false;
        const float x = parts[0].toFloat(&a);
        const float y = parts[1].toFloat(&b);
        const float z = parts[2].toFloat(&c);
        return (a && b && c) ? GfVec3f(x, y, z) : fallback;
    };

    while (!xml.atEnd()) {
        xml.readNext();

        if (xml.isStartElement() && xml.name() == QStringLiteral("standard_surface")) {
            inSurface = true;
            current = Imported();
            current.name = xml.attributes().value("name").toString();
            if (current.name.isEmpty())
                current.name = QFileInfo(filename).completeBaseName();
        }
        else if (inSurface && xml.isStartElement() && xml.name() == QStringLiteral("input")) {
            const QString name = xml.attributes().value("name").toString();
            const QString value = xml.attributes().value("value").toString();

            if (name == "base_color")
                current.parameters.baseColor = parseColor(value, current.parameters.baseColor);
            else if (name == "metalness")
                current.parameters.metalness = parseFloat(value, current.parameters.metalness);
            else if (name == "roughness")
                current.parameters.roughness = parseFloat(value, current.parameters.roughness);
            else if (name == "specular")
                current.parameters.specular = parseFloat(value, current.parameters.specular);
            else if (name == "specular_IOR")
                current.parameters.ior = parseFloat(value, current.parameters.ior);
            else if (name == "coat")
                current.parameters.coat = parseFloat(value, current.parameters.coat);
            else if (name == "coat_roughness")
                current.parameters.coatRoughness = parseFloat(value, current.parameters.coatRoughness);
            else if (name == "transmission")
                current.parameters.transmission = parseFloat(value, current.parameters.transmission);
            else if (name == "transmission_color")
                current.parameters.transmissionColor = parseColor(value, current.parameters.transmissionColor);
        }
        else if (xml.isEndElement() && xml.name() == QStringLiteral("standard_surface") && inSurface) {
            imports.append(current);
            inSurface = false;
        }
    }

    if (xml.hasError()) {
        error = QString("MaterialX parse error: %1").arg(xml.errorString());
        return false;
    }

    if (imports.isEmpty()) {
        error = QStringLiteral("No direct standard_surface nodes were found in the MaterialX document.");
        return false;
    }

    for (const Imported& imported : imports) {
        const SdfPath path = uniqueMaterialPath(stage, sanitizeIdentifier(imported.name));
        if (path.IsEmpty())
            continue;

        authorStandardSurface(stage, path, imported.parameters);
        createdPaths.append(path);
    }

    return !createdPaths.isEmpty();
}

}  // namespace stageviz
