// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 - present Mikael Sundell

#include "commandlight.h"
#include "commandprimprivate.h"
#include "renderresources.h"
#include <pxr/usd/usdLux/cylinderLight.h>
#include <pxr/usd/usdLux/diskLight.h>
#include <pxr/usd/usdLux/distantLight.h>
#include <pxr/usd/usdLux/domeLight.h>
#include <pxr/usd/usdLux/lightAPI.h>
#include <pxr/usd/usdLux/rectLight.h>
#include <pxr/usd/usdLux/shapingAPI.h>
#include <pxr/usd/usdLux/sphereLight.h>

namespace stageviz {
Command
newLightPath(const SdfPath& parentPath, const QString& nameInput, LightType type, bool selectCreated)
{
    TfToken schema;
    switch (type) {
    case LightType::Point:
    case LightType::Spot: schema = TfToken("SphereLight"); break;
    case LightType::Distant: schema = TfToken("DistantLight"); break;
    case LightType::Rect: schema = TfToken("RectLight"); break;
    case LightType::Disk: schema = TfToken("DiskLight"); break;
    case LightType::Cylinder: schema = TfToken("CylinderLight"); break;
    case LightType::Dome: schema = TfToken("DomeLight"); break;
    }
    return command::createPrimCommand(
        parentPath, nameInput, schema, selectCreated, [type](const UsdPrim& prim, QString& error) {
            UsdLuxLightAPI light(prim);
            if (!light) {
                error = QStringLiteral("Could not create USD light");
                return false;
            }
            light.CreateColorAttr().Set(GfVec3f(1.f));
            light.CreateIntensityAttr().Set(type == LightType::Dome || type == LightType::Distant ? 1.f : 100.f);
            light.CreateExposureAttr().Set(0.f);
            if (type != LightType::Dome)
                light.CreateNormalizeAttr().Set(true);
            switch (type) {
            case LightType::Point:
            case LightType::Spot: {
                UsdLuxSphereLight sphere(prim);
                sphere.CreateRadiusAttr().Set(0.f);
                sphere.CreateTreatAsPointAttr().Set(true);
                if (type == LightType::Spot) {
                    auto shaping = UsdLuxShapingAPI::Apply(prim);
                    shaping.CreateShapingConeAngleAttr().Set(45.f);
                    shaping.CreateShapingConeSoftnessAttr().Set(.1f);
                }
                break;
            }
            case LightType::Distant: UsdLuxDistantLight(prim).CreateAngleAttr().Set(.53f); break;
            case LightType::Rect:
                UsdLuxRectLight(prim).CreateWidthAttr().Set(2.f);
                UsdLuxRectLight(prim).CreateHeightAttr().Set(2.f);
                break;
            case LightType::Disk: UsdLuxDiskLight(prim).CreateRadiusAttr().Set(1.f); break;
            case LightType::Cylinder:
                UsdLuxCylinderLight(prim).CreateRadiusAttr().Set(.5f);
                UsdLuxCylinderLight(prim).CreateLengthAttr().Set(2.f);
                break;
            case LightType::Dome: {
                const auto texture = renderresources::defaultDomeTexture();
                if (texture.isEmpty()) {
                    error = QStringLiteral("Could not find the bundled Scandinavia dome light texture");
                    return false;
                }
                UsdLuxDomeLight dome(prim);
                dome.CreateTextureFileAttr().Set(SdfAssetPath(texture.toStdString()));
                dome.CreateTextureFormatAttr().Set(TfToken("latlong"));
                break;
            }
            }
            return true;
        });
}
}  // namespace stageviz
