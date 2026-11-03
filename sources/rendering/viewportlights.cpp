// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 - present Mikael Sundell

#include "viewportlights.h"
#include "application.h"
#include "tracelocks.h"
#include "ui.h"
#include "viewcontext.h"
#include "viewporthost.h"
#include <QLineF>
#include <QPainter>
#include <algorithm>
#include <cmath>
#include <pxr/usd/usd/primRange.h>
#include <pxr/usd/usdGeom/imageable.h>
#include <pxr/usd/usdGeom/xformCache.h>
#include <pxr/usd/usdLux/lightAPI.h>
#include <pxr/usd/usdLux/shapingAPI.h>

namespace stageviz {
class ViewportLightsPrivate {
public:
    struct Guide {
        SdfPath path;
        QPointF center;
        QList<QLineF> lines;
    };
    QList<Guide> guides();
    struct Data {
        ViewportHost* host = nullptr;
        QList<SdfPath> paths;
        bool dirty = true;
        double time = 0.;
    };
    Data d;
};

QList<ViewportLightsPrivate::Guide>
ViewportLightsPrivate::guides()
{
    QList<Guide> result;
    if (!d.host->context())
        return result;
    READ_LOCKER(locker, d.host->context()->stageLock(), "stageLock");
    const auto stage = d.host->stage();
    if (!stage) {
        d.paths.clear();
        d.dirty = true;
        return result;
    }
    if (d.dirty) {
        d.paths.clear();
        for (const auto& prim : UsdPrimRange::Stage(stage, UsdTraverseInstanceProxies()))
            if (UsdLuxLightAPI(prim))
                d.paths.append(prim.GetPath());
        d.dirty = false;
    }
    const UsdTimeCode time(d.time);
    UsdGeomXformCache transforms(time);
    for (const auto& path : d.paths) {
        const auto prim = stage->GetPrimAtPath(path);
        if (!prim || !d.host->pathMaskedIn(path))
            continue;
        const UsdGeomImageable imageable(prim);
        if (imageable && imageable.ComputeVisibility(time) == UsdGeomTokens->invisible)
            continue;
        const auto world = transforms.GetLocalToWorldTransform(prim);
        Guide guide;
        guide.path = path;
        if (!d.host->projectWorldToScreen(world.ExtractTranslation(), guide.center))
            continue;
        auto line = [&](const GfVec3d& a, const GfVec3d& b) {
            QPointF start, end;
            if (d.host->projectWorldToScreen(world.Transform(a), start)
                && d.host->projectWorldToScreen(world.Transform(b), end))
                guide.lines.append(QLineF(start, end));
        };
        auto circle = [&](int axis, double radius, const GfVec3d& offset = GfVec3d(0.)) {
            for (int i = 0; i < 48; ++i) {
                GfVec3d points[2] { offset, offset };
                for (int j = 0; j < 2; ++j) {
                    const double angle = (i + j) * 6.283185307179586 / 48.;
                    points[j][(axis + 1) % 3] += radius * std::cos(angle);
                    points[j][(axis + 2) % 3] += radius * std::sin(angle);
                }
                line(points[0], points[1]);
            }
        };
        auto value = [&](const char* name, float fallback) {
            float result = fallback;
            prim.GetAttribute(TfToken(name)).Get(&result, time);
            return std::isfinite(result) ? std::max(0.f, result) : fallback;
        };
        auto arrow = [&](const GfVec3d& start, double length) {
            const auto end = start + GfVec3d(0, 0, -length);
            line(start, end);
            line(end, end + GfVec3d(.15, 0, .3));
            line(end, end + GfVec3d(-.15, 0, .3));
        };
        const auto type = prim.GetTypeName();
        const UsdLuxShapingAPI shaping(prim);
        if (shaping && prim.HasAPI<UsdLuxShapingAPI>()) {
            const double angle = std::min(85.f, value("inputs:shaping:cone:angle", 45.f));
            const double radius = 2. * std::tan(angle * 3.141592653589793 / 180.);
            circle(2, radius, GfVec3d(0, 0, -2));
            for (const auto& direction : { GfVec3d(1, 0, 0), GfVec3d(-1, 0, 0), GfVec3d(0, 1, 0), GfVec3d(0, -1, 0) })
                line(GfVec3d(0), direction * radius + GfVec3d(0, 0, -2));
        }
        else if (type == TfToken("RectLight")) {
            const double w = value("inputs:width", 2.f) / 2.;
            const double h = value("inputs:height", 2.f) / 2.;
            const GfVec3d corners[] = { { -w, -h, 0 }, { w, -h, 0 }, { w, h, 0 }, { -w, h, 0 } };
            for (int i = 0; i < 4; ++i)
                line(corners[i], corners[(i + 1) % 4]);
            arrow(GfVec3d(0), 1.5);
        }
        else if (type == TfToken("DiskLight")) {
            circle(2, value("inputs:radius", 1.f));
            arrow(GfVec3d(0), 1.5);
        }
        else if (type == TfToken("CylinderLight")) {
            const double radius = value("inputs:radius", .5f);
            const double length = value("inputs:length", 2.f) / 2.;
            circle(0, radius, GfVec3d(-length, 0, 0));
            circle(0, radius, GfVec3d(length, 0, 0));
            for (auto offset :
                 { GfVec3d(0, radius, 0), GfVec3d(0, -radius, 0), GfVec3d(0, 0, radius), GfVec3d(0, 0, -radius) })
                line(offset + GfVec3d(-length, 0, 0), offset + GfVec3d(length, 0, 0));
        }
        else if (type == TfToken("DistantLight")) {
            circle(2, .5);
            for (double x : { -.4, 0., .4 })
                arrow(GfVec3d(x, 0, 0), 1.8);
        }
        else {
            const double radius = type == TfToken("DomeLight") ? 1.25 : std::max(.2f, value("inputs:radius", .2f));
            for (int axis = 0; axis < 3; ++axis)
                circle(axis, radius);
            if (type == TfToken("DomeLight"))
                arrow(GfVec3d(0), 1.5);
        }
        // A small screen-space marker remains pickable when a shape is edge-on.
        guide.lines.append(QLineF(guide.center - QPointF(4, 0), guide.center + QPointF(4, 0)));
        guide.lines.append(QLineF(guide.center - QPointF(0, 4), guide.center + QPointF(0, 4)));
        result.append(std::move(guide));
    }
    return result;
}

ViewportLights::ViewportLights(ViewportHost& host)
    : p(new ViewportLightsPrivate())
{
    p->d.host = &host;
}
ViewportLights::~ViewportLights() = default;
void
ViewportLights::invalidate()
{
    p->d.dirty = true;
}
void
ViewportLights::setTimeCode(double time)
{
    p->d.time = time;
}
void
ViewportLights::draw(QPainter& painter)
{
    painter.save();
    painter.setClipRect(p->d.host->cameraGateRect(), Qt::IntersectClip);
    painter.setRenderHint(QPainter::Antialiasing);
    for (const auto& guide : p->guides()) {
        bool selected = false;
        for (const auto& path : p->d.host->selectedPaths())
            selected |= guide.path.HasPrefix(path.GetPrimPath());
        painter.setPen(QPen(ui()->color(selected ? UI::Selection : UI::Guide), selected ? 2. : 1.));
        painter.drawLines(guide.lines);
    }
    painter.restore();
}
SdfPath
ViewportLights::pick(const QPointF& pos)
{
    if (!p->d.host->cameraGateRect().contains(pos))
        return {};
    double nearest = 7.;
    SdfPath hit;
    for (const auto& guide : p->guides()) {
        for (const auto& line : guide.lines) {
            const QPointF v = line.p2() - line.p1();
            const double length2 = QPointF::dotProduct(v, v);
            const double t = length2 > 0 ? std::clamp(QPointF::dotProduct(pos - line.p1(), v) / length2, 0., 1.) : 0.;
            const double distance = QLineF(pos, line.p1() + v * t).length();
            if (distance < nearest) {
                nearest = distance;
                hit = guide.path;
            }
        }
    }
    return hit;
}
QList<SdfPath>
ViewportLights::pathsInRect(const QRectF& rect)
{
    QList<SdfPath> result;
    const QRectF clipped = rect.intersected(p->d.host->cameraGateRect());
    if (clipped.isEmpty())
        return result;
    for (const auto& guide : p->guides()) {
        if (clipped.contains(guide.center)) {
            result.append(guide.path);
            continue;
        }
        for (const auto& line : guide.lines) {
            const QLineF edges[] = { { clipped.topLeft(), clipped.topRight() },
                                     { clipped.topRight(), clipped.bottomRight() },
                                     { clipped.bottomRight(), clipped.bottomLeft() },
                                     { clipped.bottomLeft(), clipped.topLeft() } };
            bool intersects = clipped.contains(line.p1()) || clipped.contains(line.p2());
            for (const auto& edge : edges)
                intersects |= line.intersects(edge) == QLineF::BoundedIntersection;
            if (intersects) {
                result.append(guide.path);
                break;
            }
        }
    }
    return result;
}
}  // namespace stageviz
