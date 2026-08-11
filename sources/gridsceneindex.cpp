// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#include "gridsceneindex.h"
#include <cmath>
#include <pxr/base/gf/vec3f.h>
#include <pxr/base/vt/array.h>
#include <pxr/imaging/hd/basisCurvesSchema.h>
#include <pxr/imaging/hd/dataSourceLocator.h>
#include <pxr/imaging/hd/materialBindingSchema.h>
#include <pxr/imaging/hd/materialBindingsSchema.h>
#include <pxr/imaging/hd/materialConnectionSchema.h>
#include <pxr/imaging/hd/materialNetworkSchema.h>
#include <pxr/imaging/hd/materialNodeParameterSchema.h>
#include <pxr/imaging/hd/materialNodeSchema.h>
#include <pxr/imaging/hd/materialSchema.h>
#include <pxr/imaging/hd/primvarSchema.h>
#include <pxr/imaging/hd/primvarsSchema.h>
#include <pxr/imaging/hd/purposeSchema.h>
#include <pxr/imaging/hd/retainedDataSource.h>
#include <pxr/imaging/hd/tokens.h>
#include <pxr/imaging/hd/visibilitySchema.h>
#include <pxr/imaging/hd/xformSchema.h>
#include <pxr/usd/usdGeom/tokens.h>
#include <pxr/usdImaging/usdImaging/tokens.h>
#include <vector>

PXR_NAMESPACE_USING_DIRECTIVE

namespace stageviz {

class GridSceneIndexPrivate {
public:
    void init();
    void rebuildGeometry();
    TfToken normalizedUpAxis(const TfToken& upAxis);
    HdContainerDataSourceHandle createMaterialBindings(const SdfPath& materialPath) const;
    HdSceneIndexPrim createGridPrim() const;
    HdSceneIndexPrim createCenterPrim() const;
    HdSceneIndexPrim createPreviewSurfacePrim(const SdfPath& previewSurfacePath, const GfVec3f& color) const;

public:
    struct Data {
        bool enabled = false;
        SdfPath gridPath;
        SdfPath gridMaterialPath;
        SdfPath gridPreviewSurfacePath;
        SdfPath centerPath;
        SdfPath centerMaterialPath;
        SdfPath centerPreviewSurfacePath;
        TfToken upAxis;
        GfVec3f gridColor;
        GfVec3f centerColor;
        HdSceneIndexPrim gridPrim;
        HdSceneIndexPrim gridMaterialPrim;
        HdSceneIndexPrim centerPrim;
        HdSceneIndexPrim centerMaterialPrim;
        GridSceneIndex* sceneIndex = nullptr;
    };
    Data d;
};

void
GridSceneIndexPrivate::init()
{
    d.gridPath = SdfPath("/Grid");
    d.gridMaterialPath = d.gridPath.AppendPath(SdfPath("Material"));
    d.gridPreviewSurfacePath = d.gridMaterialPath.AppendPath(SdfPath("PreviewSurface"));
    d.centerPath = SdfPath("/GridCenter");
    d.centerMaterialPath = d.centerPath.AppendPath(SdfPath("Material"));
    d.centerPreviewSurfacePath = d.centerMaterialPath.AppendPath(SdfPath("PreviewSurface"));
    d.upAxis = UsdGeomTokens->y;
    d.gridColor = GfVec3f(0.34f, 0.34f, 0.34f);
    d.centerColor = GfVec3f(0.0f, 0.0f, 0.0f);
    d.gridMaterialPrim = createPreviewSurfacePrim(d.gridPreviewSurfacePath, d.gridColor);
    d.centerMaterialPrim = createPreviewSurfacePrim(d.centerPreviewSurfacePath, d.centerColor);
    rebuildGeometry();
    d.sceneIndex->setEnabled(true);
}


void
GridSceneIndexPrivate::rebuildGeometry()
{
    d.gridPrim = createGridPrim();
    d.centerPrim = createCenterPrim();
}

TfToken
GridSceneIndexPrivate::normalizedUpAxis(const TfToken& upAxis)
{
    return upAxis == UsdGeomTokens->z ? UsdGeomTokens->z : UsdGeomTokens->y;
}

HdContainerDataSourceHandle
GridSceneIndexPrivate::createMaterialBindings(const SdfPath& materialPath) const
{
    using PathDataSource = HdRetainedTypedSampledDataSource<SdfPath>;
    TfTokenVector purposes;
    std::vector<HdDataSourceBaseHandle> bindings;
    purposes.push_back(HdMaterialBindingsSchemaTokens->allPurpose);
    bindings.push_back(HdMaterialBindingSchema::Builder().SetPath(PathDataSource::New(materialPath)).Build());
    return HdMaterialBindingsSchema::BuildRetained(purposes.size(), purposes.data(), bindings.data());
}

TfRefPtr<GridSceneIndex>
GridSceneIndex::New()
{
    return TfCreateRefPtr(new GridSceneIndex());
}

GridSceneIndex::GridSceneIndex()
    : p(new GridSceneIndexPrivate())
{
    SetDisplayName("GridSceneIndex");
    p->d.sceneIndex = this;
    p->init();
}

GridSceneIndex::~GridSceneIndex() = default;

bool
GridSceneIndex::isEnabled() const
{
    return p->d.enabled;
}

void
GridSceneIndex::setEnabled(bool enabled)
{
    if (enabled == p->d.enabled)
        return;

    if (enabled) {
        _SendPrimsAdded({
            { p->d.gridMaterialPath, HdPrimTypeTokens->material },
            { p->d.centerMaterialPath, HdPrimTypeTokens->material },
            { p->d.gridPath, HdPrimTypeTokens->basisCurves },
            { p->d.centerPath, HdPrimTypeTokens->basisCurves },
        });
    }
    else {
        _SendPrimsRemoved({
            { p->d.gridPath },
            { p->d.centerPath },
            { p->d.gridMaterialPath },
            { p->d.centerMaterialPath },
        });
    }

    p->d.enabled = enabled;
}

GfVec3f
GridSceneIndex::color() const
{
    return p->d.gridColor;
}

void
GridSceneIndex::setColor(const GfVec3f& color)
{
    if (color == p->d.gridColor)
        return;

    p->d.gridColor = color;
    p->d.gridMaterialPrim = p->createPreviewSurfacePrim(p->d.gridPreviewSurfacePath, p->d.gridColor);

    if (p->d.enabled) {
        HdDataSourceLocatorSet dirtyLocators;
        dirtyLocators.insert(HdMaterialSchema::GetDefaultLocator());
        _SendPrimsDirtied({
            { p->d.gridMaterialPath, dirtyLocators },
        });
    }
}

TfToken
GridSceneIndex::upAxis() const
{
    return p->d.upAxis;
}

void
GridSceneIndex::setUpAxis(const TfToken& upAxis)
{
    const TfToken normalized = p->normalizedUpAxis(upAxis);
    if (normalized == p->d.upAxis)
        return;

    p->d.upAxis = normalized;
    p->rebuildGeometry();

    if (p->d.enabled) {
        HdDataSourceLocatorSet dirtyLocators;
        dirtyLocators.insert(HdXformSchema::GetDefaultLocator());
        dirtyLocators.insert(HdPrimvarsSchema::GetDefaultLocator());

        _SendPrimsDirtied({
            { p->d.gridPath, dirtyLocators },
            { p->d.centerPath, dirtyLocators },
        });
    }
}


HdSceneIndexPrim
GridSceneIndex::GetPrim(const SdfPath& primPath) const
{
    if (!p->d.enabled)
        return {};

    if (primPath == p->d.gridPath)
        return p->d.gridPrim;
    if (primPath == p->d.gridMaterialPath)
        return p->d.gridMaterialPrim;
    if (primPath == p->d.centerPath)
        return p->d.centerPrim;
    if (primPath == p->d.centerMaterialPath)
        return p->d.centerMaterialPrim;

    return {};
}

SdfPathVector
GridSceneIndex::GetChildPrimPaths(const SdfPath& primPath) const
{
    if (!p->d.enabled)
        return {};

    if (primPath == SdfPath::AbsoluteRootPath())
        return { p->d.gridPath, p->d.centerPath };
    if (primPath == p->d.gridPath)
        return { p->d.gridMaterialPath };
    if (primPath == p->d.centerPath)
        return { p->d.centerMaterialPath };

    return {};
}

HdSceneIndexPrim
GridSceneIndexPrivate::createGridPrim() const
{
    using IntArrayDataSource = HdRetainedTypedSampledDataSource<VtIntArray>;
    using TokenDataSource = HdRetainedTypedSampledDataSource<TfToken>;
    using PointDataSource = HdRetainedTypedSampledDataSource<VtVec3fArray>;
    using FloatDataSource = HdRetainedTypedSampledDataSource<VtFloatArray>;
    using BoolDataSource = HdRetainedTypedSampledDataSource<bool>;

    constexpr int lines = 12;
    constexpr float spacing = 1.0f;
    constexpr float extent = lines * spacing;

    VtVec3fArray points;
    VtIntArray vertexCounts;

    auto point = [&](float a, float b) {
        if (d.upAxis == UsdGeomTokens->z)
            return GfVec3f(a, b, 0.0f);
        return GfVec3f(a, 0.0f, b);
    };

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
            vertexCounts.push_back(2);
    }


    return {
        HdPrimTypeTokens->basisCurves,
        HdRetainedContainerDataSource::New(
            HdBasisCurvesSchemaTokens->basisCurves,
            HdBasisCurvesSchema::Builder()
                .SetTopology(HdBasisCurvesTopologySchema::Builder()
                                 .SetCurveVertexCounts(IntArrayDataSource::New(vertexCounts))
                                 .SetBasis(TokenDataSource::New(HdTokens->bezier))
                                 .SetType(TokenDataSource::New(HdTokens->linear))
                                 .SetWrap(TokenDataSource::New(HdTokens->nonperiodic))
                                 .Build())
                .Build(),

            HdPrimvarsSchemaTokens->primvars,
            HdRetainedContainerDataSource::New(
                HdPrimvarsSchemaTokens->points,
                HdPrimvarSchema::Builder()
                    .SetPrimvarValue(PointDataSource::New(points))
                    .SetRole(HdPrimvarSchema::BuildRoleDataSource(HdPrimvarSchemaTokens->point))
                    .SetInterpolation(HdPrimvarSchema::BuildInterpolationDataSource(HdPrimvarSchemaTokens->vertex))
                    .Build(),

                HdPrimvarsSchemaTokens->widths,
                HdPrimvarSchema::Builder()
                    .SetPrimvarValue(FloatDataSource::New({ 1.0f }))
                    .SetInterpolation(HdPrimvarSchema::BuildInterpolationDataSource(HdPrimvarSchemaTokens->constant))
                    .Build()),

            HdPurposeSchemaTokens->purpose,
            HdPurposeSchema::Builder().SetPurpose(TokenDataSource::New(HdRenderTagTokens->geometry)).Build(),

            HdVisibilitySchemaTokens->visibility,
            HdVisibilitySchema::Builder().SetVisibility(BoolDataSource::New(true)).Build(),


            HdMaterialBindingsSchemaTokens->materialBindings, createMaterialBindings(d.gridMaterialPath))
    };
}

HdSceneIndexPrim
GridSceneIndexPrivate::createCenterPrim() const
{
    using IntArrayDataSource = HdRetainedTypedSampledDataSource<VtIntArray>;
    using TokenDataSource = HdRetainedTypedSampledDataSource<TfToken>;
    using PointDataSource = HdRetainedTypedSampledDataSource<VtVec3fArray>;
    using FloatDataSource = HdRetainedTypedSampledDataSource<VtFloatArray>;
    using BoolDataSource = HdRetainedTypedSampledDataSource<bool>;

    constexpr float extent = 12.0f;

    VtVec3fArray points;
    VtIntArray vertexCounts;

    auto point = [&](float a, float b) {
        if (d.upAxis == UsdGeomTokens->z)
            return GfVec3f(a, b, 0.0f);
        return GfVec3f(a, 0.0f, b);
    };

    // One center curve per ground-plane axis.
    points.push_back(point(0.0f, -extent));
    points.push_back(point(0.0f, extent));
    vertexCounts.push_back(2);

    points.push_back(point(-extent, 0.0f));
    points.push_back(point(extent, 0.0f));
    vertexCounts.push_back(2);


    return {
        HdPrimTypeTokens->basisCurves,
        HdRetainedContainerDataSource::New(
            HdBasisCurvesSchemaTokens->basisCurves,
            HdBasisCurvesSchema::Builder()
                .SetTopology(HdBasisCurvesTopologySchema::Builder()
                                 .SetCurveVertexCounts(IntArrayDataSource::New(vertexCounts))
                                 .SetBasis(TokenDataSource::New(HdTokens->bezier))
                                 .SetType(TokenDataSource::New(HdTokens->linear))
                                 .SetWrap(TokenDataSource::New(HdTokens->nonperiodic))
                                 .Build())
                .Build(),

            HdPrimvarsSchemaTokens->primvars,
            HdRetainedContainerDataSource::New(
                HdPrimvarsSchemaTokens->points,
                HdPrimvarSchema::Builder()
                    .SetPrimvarValue(PointDataSource::New(points))
                    .SetRole(HdPrimvarSchema::BuildRoleDataSource(HdPrimvarSchemaTokens->point))
                    .SetInterpolation(HdPrimvarSchema::BuildInterpolationDataSource(HdPrimvarSchemaTokens->vertex))
                    .Build(),

                HdPrimvarsSchemaTokens->widths,
                HdPrimvarSchema::Builder()
                    .SetPrimvarValue(FloatDataSource::New({ 1.0f }))
                    .SetInterpolation(HdPrimvarSchema::BuildInterpolationDataSource(HdPrimvarSchemaTokens->constant))
                    .Build()),

            HdPurposeSchemaTokens->purpose,
            HdPurposeSchema::Builder().SetPurpose(TokenDataSource::New(HdRenderTagTokens->geometry)).Build(),

            HdVisibilitySchemaTokens->visibility,
            HdVisibilitySchema::Builder().SetVisibility(BoolDataSource::New(true)).Build(),


            HdMaterialBindingsSchemaTokens->materialBindings, createMaterialBindings(d.centerMaterialPath))
    };
}

HdSceneIndexPrim
GridSceneIndexPrivate::createPreviewSurfacePrim(const SdfPath& previewSurfacePath, const GfVec3f& color) const
{
    using TokenDataSource = HdRetainedTypedSampledDataSource<TfToken>;
    using FloatDataSource = HdRetainedTypedSampledDataSource<float>;
    using Vec3fDataSource = HdRetainedTypedSampledDataSource<GfVec3f>;

    const auto one = HdMaterialNodeParameterSchema::Builder().SetValue(FloatDataSource::New(1.0f)).Build();
    const auto zero = HdMaterialNodeParameterSchema::Builder().SetValue(FloatDataSource::New(0.0f)).Build();
    const auto black = HdMaterialNodeParameterSchema::Builder().SetValue(Vec3fDataSource::New(GfVec3f(0.0f))).Build();
    const auto materialColor = HdMaterialNodeParameterSchema::Builder().SetValue(Vec3fDataSource::New(color)).Build();

    const auto surfaceNode
        = HdMaterialNodeSchema::Builder()
              .SetNodeIdentifier(TokenDataSource::New(UsdImagingTokens->UsdPreviewSurface))
              .SetParameters(HdRetainedContainerDataSource::New(TfToken("emissiveColor"), materialColor,
                                                                TfToken("diffuseColor"), black, TfToken("specular"),
                                                                zero, TfToken("metallic"), zero, TfToken("roughness"),
                                                                one, TfToken("opacity"), one))
              .SetInputConnections(HdRetainedContainerDataSource::New())
              .Build();

    const auto terminals = HdRetainedContainerDataSource::New(
        HdMaterialTerminalTokens->surface,
        HdMaterialConnectionSchema::Builder()
            .SetUpstreamNodePath(TokenDataSource::New(previewSurfacePath.GetToken()))
            .SetUpstreamNodeOutputName(TokenDataSource::New(HdMaterialTerminalTokens->surface))
            .Build());

    std::vector<TfToken> networkNames;
    std::vector<HdDataSourceBaseHandle> networks;
    networkNames.push_back(HdMaterialSchemaTokens->universalRenderContext);
    networks.push_back(HdMaterialNetworkSchema::Builder()
                           .SetNodes(HdRetainedContainerDataSource::New(previewSurfacePath.GetToken(), surfaceNode))
                           .SetTerminals(terminals)
                           .Build());

    const auto material = HdRetainedContainerDataSource::New(HdMaterialSchemaTokens->material,
                                                             HdMaterialSchema::BuildRetained(networkNames.size(),
                                                                                             networkNames.data(),
                                                                                             networks.data()));

    return { HdPrimTypeTokens->material, material };
}

}  // namespace stageviz
