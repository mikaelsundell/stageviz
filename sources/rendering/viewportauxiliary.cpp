// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#include "viewportauxiliary.h"
#include "application.h"
#include "paths.h"
#include "session.h"
#include "ui.h"
#include "tracelocks.h"
#include "viewstate.h"
#include <QColor>
#include <QFontMetrics>
#include <QPainter>
#include <QVector>
#include <algorithm>
#include <pxr/base/gf/frustum.h>
#include <pxr/base/vt/array.h>
#include <pxr/usd/usdGeom/basisCurves.h>
#include <pxr/usd/usdGeom/scope.h>
#include <pxr/usd/usdGeom/tokens.h>
#include <pxr/usd/usdShade/material.h>
#include <pxr/usd/usdShade/materialBindingAPI.h>
#include <pxr/usd/usdShade/shader.h>

PXR_NAMESPACE_USING_DIRECTIVE

namespace stageviz {

class ViewportAuxiliaryPrivate {
public:
    void updateGrid(ViewState* state);
    void ensureMaterials();
    void authorStandardSurface(const SdfPath& materialPath, const GfVec3f& baseColor, float metalness, float roughness,
                               float specular);
    void updateAxis(const GfCamera& camera, bool hasStage, qreal devicePixelRatio);

    struct Data {
        UsdStageRefPtr stage;
        TfToken upAxis = UsdGeomTokens->y;
        QImage axis;
    };
    Data d;
};

void
ViewportAuxiliaryPrivate::updateGrid(ViewState* state)
{
    if (!d.stage)
        return;

    WRITE_LOCKER(locker, session()->auxiliaryLock(), "auxiliaryLock");
    // Keep implementation-specific children below the shared auxiliary roots.
    const SdfPath gridRootPath = paths::auxiliary::grid;
    const SdfPath gridPath = gridRootPath.AppendChild(TfToken("Lines"));
    const SdfPath centerPath = gridRootPath.AppendChild(TfToken("Center"));
    const SdfPath gridMaterialPath = paths::auxiliary::materials.AppendChild(TfToken("Grid"));
    const SdfPath centerMaterialPath = paths::auxiliary::materials.AppendChild(TfToken("GridCenter"));
    if (!state->gridEnabled()) {
        d.stage->RemovePrim(gridRootPath);
        return;
    }

    UsdGeomScope::Define(d.stage, paths::auxiliary::display);
    UsdGeomScope::Define(d.stage, gridRootPath);
    UsdGeomScope::Define(d.stage, paths::auxiliary::materials);
    const TfToken& upAxis = d.upAxis;
    constexpr int lines = 12;
    constexpr float spacing = 1.0f;
    constexpr float extent = lines * spacing;
    auto point = [&](float a, float b) {
        return upAxis == UsdGeomTokens->z ? GfVec3f(a, b, 0.0f) : GfVec3f(a, 0.0f, b);
    };

    VtVec3fArray points;
    VtIntArray counts;
    for (int i = 1; i <= lines; ++i) {
        const float offset = spacing * static_cast<float>(i);
        points.push_back(point(-offset, -extent));
        points.push_back(point(-offset, extent));
        points.push_back(point(offset, -extent));
        points.push_back(point(offset, extent));
        points.push_back(point(-extent, -offset));
        points.push_back(point(extent, -offset));
        points.push_back(point(-extent, offset));
        points.push_back(point(extent, offset));
        for (int j = 0; j < 4; ++j)
            counts.push_back(2);
    }

    UsdGeomBasisCurves grid = UsdGeomBasisCurves::Define(d.stage, gridPath);
    grid.CreateTypeAttr(VtValue(UsdGeomTokens->linear));
    grid.CreateBasisAttr(VtValue(UsdGeomTokens->bezier));
    grid.CreateWrapAttr(VtValue(UsdGeomTokens->nonperiodic));
    grid.CreatePointsAttr(VtValue(points));
    grid.CreateCurveVertexCountsAttr(VtValue(counts));
    UsdAttribute gridWidths = grid.CreateWidthsAttr(VtValue(VtFloatArray { 1.0f }));
    gridWidths.SetMetadata(TfToken("interpolation"), VtValue(UsdGeomTokens->constant));
    VtVec3fArray centerPoints;
    VtIntArray centerCounts;
    centerPoints.push_back(point(0.0f, -extent));
    centerPoints.push_back(point(0.0f, extent));
    centerCounts.push_back(2);
    centerPoints.push_back(point(-extent, 0.0f));
    centerPoints.push_back(point(extent, 0.0f));
    centerCounts.push_back(2);
    UsdGeomBasisCurves center = UsdGeomBasisCurves::Define(d.stage, centerPath);
    center.CreateTypeAttr(VtValue(UsdGeomTokens->linear));
    center.CreateBasisAttr(VtValue(UsdGeomTokens->bezier));
    center.CreateWrapAttr(VtValue(UsdGeomTokens->nonperiodic));
    center.CreatePointsAttr(VtValue(centerPoints));
    center.CreateCurveVertexCountsAttr(VtValue(centerCounts));
    UsdAttribute centerWidths = center.CreateWidthsAttr(VtValue(VtFloatArray { 1.0f }));
    centerWidths.SetMetadata(TfToken("interpolation"), VtValue(UsdGeomTokens->constant));
    const QColor gridColor = state->gridColor();
    const GfVec3f color = gridColor.isValid() ? GfVec3f(gridColor.redF(), gridColor.greenF(), gridColor.blueF())
                                              : GfVec3f(0.34f);
    ViewportAuxiliary::authorGridMaterial(d.stage, gridMaterialPath, color);
    ViewportAuxiliary::authorGridMaterial(d.stage, centerMaterialPath, GfVec3f(0.0f));
    UsdShadeMaterial gridMaterial(d.stage->GetPrimAtPath(gridMaterialPath));
    UsdShadeMaterial centerMaterial(d.stage->GetPrimAtPath(centerMaterialPath));
    UsdShadeMaterialBindingAPI::Apply(grid.GetPrim()).Bind(gridMaterial);
    UsdShadeMaterialBindingAPI::Apply(center.GetPrim()).Bind(centerMaterial);
}

void
ViewportAuxiliaryPrivate::ensureMaterials()
{
    if (!d.stage)
        return;

    WRITE_LOCKER(locker, session()->auxiliaryLock(), "auxiliaryLock");
    // Derive implementation-specific materials from the shared auxiliary root.
    const SdfPath clayPath = paths::auxiliary::materials.AppendChild(TfToken("Clay"));
    const SdfPath chromePath = paths::auxiliary::materials.AppendChild(TfToken("Chrome"));
    const SdfPath glossyPath = paths::auxiliary::materials.AppendChild(TfToken("Glossy"));
    const SdfPath reflectionPath = paths::auxiliary::materials.AppendChild(TfToken("Reflection"));
    const SdfPath selectionPath = paths::auxiliary::materials.AppendChild(TfToken("Selection"));
    UsdGeomScope::Define(d.stage, paths::auxiliary::materials);

    // Built-in Stageviz inspection materials live on the shared auxiliary
    // stage and never modify the document stage.
    authorStandardSurface(clayPath, GfVec3f(0.55f, 0.18f, 0.16f), 0.0f, 0.68f, 0.35f);
    authorStandardSurface(chromePath, GfVec3f(0.62f), 1.0f, 0.06f, 1.0f);
    authorStandardSurface(glossyPath, GfVec3f(0.18f), 0.0f, 0.12f, 1.0f);
    authorStandardSurface(reflectionPath, GfVec3f(0.16f), 1.0f, 0.025f, 1.0f);

    const QColor selectionColor = ui()->color(UI::ColorRole::Selection);
    const GfVec3f color(selectionColor.redF(), selectionColor.greenF(), selectionColor.blueF());
    authorStandardSurface(selectionPath, color, 0.0f, 0.72f, 0.20f);
}

void
ViewportAuxiliaryPrivate::authorStandardSurface(const SdfPath& materialPath, const GfVec3f& baseColor, float metalness,
                                                float roughness, float specular)
{
    if (!d.stage)
        return;

    UsdShadeMaterial material = UsdShadeMaterial::Define(d.stage, materialPath);
    UsdShadeShader surface = UsdShadeShader::Define(d.stage, materialPath.AppendChild(TfToken("Surface")));
    surface.CreateIdAttr(VtValue(TfToken("ND_standard_surface_surfaceshader")));
    surface.CreateInput(TfToken("base"), SdfValueTypeNames->Float).Set(1.0f);
    surface.CreateInput(TfToken("base_color"), SdfValueTypeNames->Color3f).Set(baseColor);
    surface.CreateInput(TfToken("metalness"), SdfValueTypeNames->Float).Set(metalness);
    surface.CreateInput(TfToken("roughness"), SdfValueTypeNames->Float).Set(roughness);
    surface.CreateInput(TfToken("specular"), SdfValueTypeNames->Float).Set(specular);
    UsdShadeOutput output = surface.CreateOutput(TfToken("out"), SdfValueTypeNames->Token);
    material.CreateSurfaceOutput().ConnectToSource(output);
}

void
ViewportAuxiliaryPrivate::updateAxis(const GfCamera& camera, bool hasStage, qreal devicePixelRatio)
{
    GfFrustum frustum = camera.GetFrustum();
    GfMatrix4d viewMatrix = frustum.ComputeViewMatrix();
    const GfVec3d xCam = viewMatrix.TransformDir(GfVec3d(1.0, 0.0, 0.0));
    const GfVec3d yCam = viewMatrix.TransformDir(GfVec3d(0.0, 1.0, 0.0));
    const GfVec3d zCam = viewMatrix.TransformDir(GfVec3d(0.0, 0.0, 1.0));
    const int margin = 18;
    const int radius = 30;
    const int bubbleRadius = 10;
    const int width = margin + radius * 2 + margin;
    const int height = margin + radius * 2 + margin;
    const QPoint center(margin + radius, margin + radius);
    auto toPoint = [&](const GfVec3d& dir) -> QPoint {
        return QPoint(qRound(center.x() + dir[0] * radius), qRound(center.y() - dir[1] * radius));
    };
    struct AxisLine {
        QString label;
        QColor color;
        GfVec3d dir;
    };
    QVector<AxisLine> axes { { "X", ui()->color(UI::ColorRole::AxisX), xCam },
                             { "Y", ui()->color(UI::ColorRole::AxisY), yCam },
                             { "Z", ui()->color(UI::ColorRole::AxisZ), zCam } };
    std::sort(axes.begin(), axes.end(), [](const AxisLine& a, const AxisLine& b) { return a.dir[2] < b.dir[2]; });

    const qreal opacity = hasStage ? 1.0 : 0.35;
    const qreal dpr = devicePixelRatio;

    d.axis = QImage(qRound(width * dpr), qRound(height * dpr), QImage::Format_ARGB32_Premultiplied);
    d.axis.setDevicePixelRatio(dpr);
    d.axis.fill(Qt::transparent);
    QPainter painter(&d.axis);
    painter.setOpacity(opacity);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);
    QFont font = app()->font();
    font.setPixelSize(ui()->fontSize(UI::UIScale::Small));
    font.setBold(true);
    painter.setFont(font);
    QFontMetrics fm(font);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(0, 0, 0, 20));
    painter.drawEllipse(center, radius - 10, radius - 10);

    for (const AxisLine& axis : axes) {
        const QPoint end = toPoint(axis.dir);
        painter.setPen(QPen(axis.color, 2.0, Qt::SolidLine, Qt::RoundCap));
        painter.drawLine(center, end);
        painter.setPen(Qt::NoPen);
        painter.setBrush(axis.color);
        painter.drawEllipse(end, bubbleRadius, bubbleRadius);
        const int textWidth = fm.horizontalAdvance(axis.label);
        const int textX = end.x() - textWidth / 2;
        const int textY = end.y() + (fm.ascent() - fm.descent()) / 2;
        painter.setPen(QColor(0, 0, 0, 160));
        painter.drawText(textX + 1, textY + 1, axis.label);
        painter.setPen(Qt::white);
        painter.drawText(textX, textY, axis.label);
    }
}

void
ViewportAuxiliary::authorGridMaterial(UsdStageRefPtr stage, const SdfPath& materialPath, const GfVec3f& color)
{
    if (!stage)
        return;

    UsdShadeMaterial material = UsdShadeMaterial::Define(stage, materialPath);
    UsdShadeShader surface = UsdShadeShader::Define(stage, materialPath.AppendChild(TfToken("Surface")));

    // Use the same MaterialX Standard Surface path as the working viewport
    // overrides. The grid is intentionally emission-only so its appearance is
    // stable and independent of viewport lighting.
    surface.CreateIdAttr(VtValue(TfToken("ND_standard_surface_surfaceshader")));
    surface.CreateInput(TfToken("base"), SdfValueTypeNames->Float).Set(0.0f);
    surface.CreateInput(TfToken("base_color"), SdfValueTypeNames->Color3f).Set(GfVec3f(0.0f));
    surface.CreateInput(TfToken("emission"), SdfValueTypeNames->Float).Set(1.0f);
    surface.CreateInput(TfToken("emission_color"), SdfValueTypeNames->Color3f).Set(color);
    surface.CreateInput(TfToken("metalness"), SdfValueTypeNames->Float).Set(0.0f);
    surface.CreateInput(TfToken("roughness"), SdfValueTypeNames->Float).Set(1.0f);
    surface.CreateInput(TfToken("specular"), SdfValueTypeNames->Float).Set(0.0f);
    UsdShadeOutput output = surface.CreateOutput(TfToken("out"), SdfValueTypeNames->Token);
    material.CreateSurfaceOutput().ConnectToSource(output);
}

ViewportAuxiliary::ViewportAuxiliary()
    : p(new ViewportAuxiliaryPrivate())
{}
ViewportAuxiliary::~ViewportAuxiliary() = default;
void
ViewportAuxiliary::setStage(UsdStageRefPtr stage)
{
    p->d.stage = stage;
}
UsdStageRefPtr
ViewportAuxiliary::stage() const
{
    return p->d.stage;
}
void
ViewportAuxiliary::setUpAxis(const TfToken& axis)
{
    p->d.upAxis = axis;
}
TfToken
ViewportAuxiliary::upAxis() const
{
    return p->d.upAxis;
}
void
ViewportAuxiliary::updateGrid(ViewState* state)
{
    if (state)
        p->updateGrid(state);
}
void
ViewportAuxiliary::ensureMaterials()
{
    p->ensureMaterials();
}
void
ViewportAuxiliary::updateAxis(const GfCamera& camera, bool hasStage, qreal devicePixelRatio)
{
    p->updateAxis(camera, hasStage, devicePixelRatio);
}
const QImage&
ViewportAuxiliary::axisImage() const
{
    return p->d.axis;
}
void
ViewportAuxiliary::clearAxis()
{
    p->d.axis = QImage();
}

}  // namespace stageviz
