// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#include "viewportgizmo.h"
#include "application.h"
#include "paths.h"
#include "renderengine.h"
#include "session.h"
#include "tracelocks.h"
#include "ui.h"
#include "viewcamera.h"
#include "viewcontext.h"
#include "viewportauxiliary.h"
#include "viewporthost.h"
#include "viewportwidget.h"
#include <QPainter>
#include <QPainterPath>
#include <algorithm>
#include <cmath>
#include <pxr/base/gf/math.h>
#include <pxr/base/vt/array.h>
#include <pxr/usd/usdGeom/imageable.h>
#include <pxr/usd/usdGeom/mesh.h>
#include <pxr/usd/usdGeom/scope.h>
#include <pxr/usd/usdGeom/xform.h>
#include <pxr/usd/usdShade/materialBindingAPI.h>
#include <string>

PXR_NAMESPACE_USING_DIRECTIVE

namespace stageviz {

class ViewportGizmoPrivate {
public:
    using Mode = ViewportGizmo::Mode;
    QPointF axisDirection(int axis);
    GfVec3d axisVector(int axis);
    GfVec3d scaleAxisVector(int axis);
    double worldPerPixel();
    double worldPerPixel(const GfVec3d& pivot);
    bool rotationScreenBasis(int axis, QPointF& center, QPointF& basisA, QPointF& basisB);
    bool rotationPoint(int axis, double angle, QPointF& screen);
    bool rotationAngle(int axis, const QPointF& pos, double& angle, double* distance);
    int hitTest(const QPointF& pos);
    void clearGeometry();
    void updateGeometry();
    void draw(QPainter& painter);
    void drawRotationFeedback(QPainter& painter);

    struct Data {
        ViewportHost* host = nullptr;
        ViewportGizmo::Frame frame;
        bool geometryValid = false;
        bool geometryVisible = false;
        bool meshesBuilt = false;
        GfVec3d geometryPivot = GfVec3d(0.0);
        GfMatrix4d geometryOrientation = GfMatrix4d(1.0);
        double geometryRadius = 0.0;
        Mode geometryMode = Mode::None;
        int geometryHover = -1;
        int geometryActive = -1;
    };
    Data d;
};

QPointF
ViewportGizmoPrivate::axisDirection(int axis)
{
    const GfMatrix4d view = d.host->viewCamera()->camera().GetFrustum().ComputeViewMatrix();
    const GfVec3d worldAxis = d.frame.mode == Mode::Scale ? scaleAxisVector(axis) : axisVector(axis);
    if (worldAxis.GetLengthSq() < 1e-12)
        return {};

    const GfVec3d cameraAxis = view.TransformDir(worldAxis);
    QPointF direction(cameraAxis[0], -cameraAxis[1]);
    const double length = std::hypot(direction.x(), direction.y());
    if (length < 1e-4)
        return {};

    return direction / length;
}

GfVec3d
ViewportGizmoPrivate::axisVector(int axis)
{
    const int component = ((axis - 1) % 3) + 1;
    GfVec3d direction(0.0);
    if (component == 1)
        direction = GfVec3d::XAxis();
    else if (component == 2)
        direction = GfVec3d::YAxis();
    else if (component == 3)
        direction = GfVec3d::ZAxis();
    else
        return direction;
    if (d.frame.orientationValid)
        direction = d.frame.orientation.TransformDir(direction);
    return direction.GetLengthSq() > 1e-12 ? direction.GetNormalized() : GfVec3d(0.0);
}

GfVec3d
ViewportGizmoPrivate::scaleAxisVector(int axis)
{
    GfVec3d direction = axisVector(axis);
    if (direction.GetLengthSq() < 1e-12 || !d.frame.orientationValid)
        return direction;

    return direction;
}

double
ViewportGizmoPrivate::worldPerPixel()
{
    return worldPerPixel(d.frame.pivot);
}

double
ViewportGizmoPrivate::worldPerPixel(const GfVec3d& pivot)
{
    if (!d.host->viewCamera())
        return 0.0;

    // Gizmo drawing and hit testing use Qt logical pixels. Use the pivot's
    // camera-space depth for perspective sizing so off-axis selections do not
    // get oversized from their Euclidean distance to the camera.
    const int height = std::max(1, d.host->widget()->height());
    const GfCamera camera = d.host->viewCamera()->camera();
    const GfFrustum frustum = camera.GetFrustum();
    const double windowHeight = frustum.GetWindow().GetSize()[1];
    if (!std::isfinite(windowHeight) || windowHeight <= 0.0)
        return 0.0;

    if (camera.GetProjection() == GfCamera::Orthographic)
        return windowHeight / static_cast<double>(height);

    const GfVec3d cameraPoint = frustum.ComputeViewMatrix().Transform(pivot);
    const double depth = -cameraPoint[2];
    const double nearClip = std::max(1e-8, static_cast<double>(d.host->viewCamera()->nearClipping()));
    const double farClip = d.host->viewCamera()->farClipping();
    if (!std::isfinite(depth) || depth <= nearClip || (std::isfinite(farClip) && depth >= farClip))
        return 0.0;

    const double worldPerPixel = windowHeight * depth / static_cast<double>(height);
    return std::isfinite(worldPerPixel) ? worldPerPixel : 0.0;
}

bool
ViewportGizmoPrivate::rotationScreenBasis(int axis, QPointF& center, QPointF& basisA, QPointF& basisB)
{
    const GfVec3d normal = axisVector(axis);
    if (normal.GetLengthSq() < 1e-12)
        return false;

    const GfVec3d worldA = axisVector(axis % 3 + 1);
    // The ring mesh is built from a local right-handed basis and then the
    // complete object frame is applied. Transform its local B axis directly:
    // recomputing B with a world-space cross product reverses it for reflected
    // transform hierarchies.
    const GfVec3d worldB = axisVector((axis + 1) % 3 + 1);

    if (!d.host->projectWorldToScreen(d.frame.pivot, center))
        return false;

    // Match the world-space radius used by the rendered rings. Normalizing
    // the projected axes would enlarge oblique rings and overshoot the mesh.
    const double gizmoSize = ui()->gizmoSize(UI::UIScale::Medium);
    const double probeRadius = worldPerPixel(d.frame.pivot) * gizmoSize;
    if (!std::isfinite(probeRadius) || probeRadius <= 0.0)
        return false;

    QPointF projectedA;
    QPointF projectedB;
    if (!d.host->projectWorldToScreen(d.frame.pivot + worldA * probeRadius, projectedA)
        || !d.host->projectWorldToScreen(d.frame.pivot + worldB * probeRadius, projectedB))
        return false;

    basisA = projectedA - center;
    basisB = projectedB - center;

    const double lengthA = std::hypot(basisA.x(), basisA.y());
    const double lengthB = std::hypot(basisB.x(), basisB.y());
    const double maxLength = std::max(lengthA, lengthB);
    if (!std::isfinite(maxLength) || maxLength < 1e-6)
        return false;

    return true;
}

bool
ViewportGizmoPrivate::rotationPoint(int axis, double angle, QPointF& screen)
{
    const GfVec3d a = axisVector(axis % 3 + 1);
    const GfVec3d b = axisVector((axis + 1) % 3 + 1);
    const double radius = worldPerPixel(d.frame.pivot) * ui()->gizmoSize(UI::UIScale::Medium);
    return d.host->projectWorldToScreen(d.frame.pivot + radius * (a * std::cos(angle) + b * std::sin(angle)), screen);
}

bool
ViewportGizmoPrivate::rotationAngle(int axis, const QPointF& pos, double& angle, double* distance)
{
    QPointF center;
    QPointF basisA;
    QPointF basisB;
    if (!rotationScreenBasis(axis, center, basisA, basisB))
        return false;

    // Use the projected basis to get a close initial angle. Perspective
    // projection makes an oblique circle a conic rather than an affine ellipse,
    // so refine against the actual projected ring to keep the feedback aligned
    // for deep or complex transform hierarchies.
    const QPointF delta = pos - center;
    const double determinant = basisA.x() * basisB.y() - basisA.y() * basisB.x();
    if (std::abs(determinant) < 1e-8)
        return false;

    const double cosComponent = (delta.x() * basisB.y() - delta.y() * basisB.x()) / determinant;
    const double sinComponent = (basisA.x() * delta.y() - basisA.y() * delta.x()) / determinant;
    if (!std::isfinite(cosComponent) || !std::isfinite(sinComponent))
        return false;

    angle = std::atan2(sinComponent, cosComponent);

    constexpr double epsilon = 1e-3;
    for (int iteration = 0; iteration < 8; ++iteration) {
        QPointF projected, projectedBefore, projectedAfter;
        if (!rotationPoint(axis, angle, projected) || !rotationPoint(axis, angle - epsilon, projectedBefore)
            || !rotationPoint(axis, angle + epsilon, projectedAfter))
            return false;

        const QPointF firstDerivative = (projectedAfter - projectedBefore) / (2.0 * epsilon);
        const QPointF secondDerivative = (projectedAfter - 2.0 * projected + projectedBefore) / (epsilon * epsilon);
        const QPointF residual = projected - pos;
        const double gradient = QPointF::dotProduct(residual, firstDerivative);
        const double curvature = QPointF::dotProduct(firstDerivative, firstDerivative)
                                 + QPointF::dotProduct(residual, secondDerivative);
        if (!std::isfinite(gradient) || !std::isfinite(curvature) || std::abs(curvature) < 1e-10)
            break;

        // Bound each Newton step so a poor local curvature estimate cannot
        // jump to a distant part of the ring.
        const double step = std::clamp(gradient / curvature, -0.5, 0.5);
        angle -= step;
        if (std::abs(step) < 1e-7)
            break;
    }

    if (distance) {
        QPointF nearest;
        if (!rotationPoint(axis, angle, nearest))
            return false;
        *distance = std::hypot(pos.x() - nearest.x(), pos.y() - nearest.y());
    }

    return true;
}

int
ViewportGizmoPrivate::hitTest(const QPointF& pos)
{
    if (!d.frame.enabled || d.frame.mode == Mode::None || !d.host->context())
        return 0;

    if (!d.frame.pivotValid || !d.frame.orientationValid)
        return 0;

    QPointF center;
    if (!d.host->projectWorldToScreen(d.frame.pivot, center))
        return 0;

    // Handles are encoded as:
    // 1..3  = translate X/Y/Z
    // 4..6  = rotate X/Y/Z
    // 7..9  = scale X/Y/Z
    // 10    = free rotation
    // 11    = free translation in the camera plane
    // 12    = uniform scale
    constexpr double centerHitRadius = 8.0;
    if (std::hypot(pos.x() - center.x(), pos.y() - center.y()) <= centerHitRadius) {
        if (d.frame.mode == Mode::Move)
            return 11;
        if (d.frame.mode == Mode::Rotate)
            return 10;
        if (d.frame.mode == Mode::Scale)
            return 12;
    }

    const double radius = worldPerPixel(d.frame.pivot) * ui()->gizmoSize(UI::UIScale::Medium);
    if (!std::isfinite(radius) || radius <= 0.0)
        return 0;
    constexpr double hitWidth = 8.0;
    const double fullCircle = GfDegreesToRadians(360.0);
    int bestHandle = 0;
    double bestDistance = hitWidth;
    for (int axis = 1; axis <= 3; ++axis) {
        const GfVec3d normal = d.frame.mode == Mode::Scale ? scaleAxisVector(axis) : axisVector(axis);
        if (d.frame.mode == Mode::Rotate) {
            const GfVec3d a = axisVector(axis % 3 + 1);
            const GfVec3d b = axisVector((axis + 1) % 3 + 1);
            QPointF previous;
            bool previousValid = false;
            for (int i = 0; i <= 96; ++i) {
                const double angle = fullCircle * (i % 96) / 96.0;
                QPointF current;
                const bool valid
                    = d.host->projectWorldToScreen(d.frame.pivot + radius * (a * std::cos(angle) + b * std::sin(angle)),
                                                   current);
                if (valid && previousValid) {
                    const QPointF segment = current - previous;
                    const double length2 = QPointF::dotProduct(segment, segment);
                    if (length2 > 1e-10) {
                        const double t = std::clamp(QPointF::dotProduct(pos - previous, segment) / length2, 0.0, 1.0);
                        const QPointF closest = previous + t * segment;
                        const double distance = std::hypot(pos.x() - closest.x(), pos.y() - closest.y());
                        if (distance < bestDistance) {
                            bestDistance = distance;
                            bestHandle = axis + 3;
                        }
                    }
                }
                previous = current;
                previousValid = valid;
            }
            continue;
        }
        QPointF end;
        if (!d.host->projectWorldToScreen(d.frame.pivot + normal * radius, end))
            continue;
        if (d.frame.mode == Mode::Scale) {
            const double distance = std::hypot(pos.x() - end.x(), pos.y() - end.y());
            if (distance < bestDistance) {
                bestDistance = distance;
                bestHandle = axis + 6;
            }
        }
        else {
            const QPointF segment = end - center;
            const double length2 = QPointF::dotProduct(segment, segment);
            if (length2 <= 1e-10)
                continue;
            const double t = std::clamp(QPointF::dotProduct(pos - center, segment) / length2, 0.0, 1.0);
            const QPointF closest = center + t * segment;
            const double distance = std::hypot(pos.x() - closest.x(), pos.y() - closest.y());
            if (distance < bestDistance) {
                bestDistance = distance;
                bestHandle = axis;
            }
        }
    }
    return bestHandle;
}

void
ViewportGizmoPrivate::clearGeometry()
{
    if (!d.host->auxiliaryStage() || !d.meshesBuilt || !d.geometryVisible) {
        d.geometryValid = false;
        return;
    }
    // Preserve the Hydra mesh resources when the gizmo is switched off.
    WRITE_LOCKER(locker, session()->auxiliaryLock(), "auxiliaryLock");
    UsdGeomImageable imageable(d.host->auxiliaryStage()->GetPrimAtPath(paths::auxiliary::transform));
    if (imageable)
        imageable.MakeInvisible();
    d.geometryValid = false;
    d.geometryVisible = false;
    if (d.host->renderEngine())
        d.host->renderEngine()->refreshAuxiliaryStage();
}

void
ViewportGizmoPrivate::updateGeometry()
{
    const double fullCircle = GfDegreesToRadians(360.0);
    if (!d.host->auxiliaryStage())
        return;
    if (!d.frame.enabled || d.frame.mode == Mode::None || !d.host->stage() || !d.host->context()) {
        clearGeometry();
        return;
    }
    if (!d.frame.pivotValid || !d.frame.orientationValid) {
        clearGeometry();
        return;
    }
    if (d.frame.pivotEditing) {
        clearGeometry();
        return;
    }
    const double radius = worldPerPixel(d.frame.pivot) * ui()->gizmoSize(UI::UIScale::Medium);
    if (!std::isfinite(radius) || radius <= 1e-10) {
        clearGeometry();
        return;
    }
    if (d.geometryValid && d.geometryPivot == d.frame.pivot && d.geometryOrientation == d.frame.orientation
        && std::abs(d.geometryRadius - radius) <= radius * 1e-5 && d.geometryMode == d.frame.mode
        && d.geometryHover == d.frame.hoverAxis && d.geometryActive == d.frame.activeAxis)
        return;

    const SdfPath root = paths::auxiliary::transform;
    const GfVec3f colors[] = { GfVec3f(0.0f), GfVec3f(0.80f, 0.18f, 0.20f), GfVec3f(0.16f, 0.75f, 0.24f),
                               GfVec3f(0.22f, 0.40f, 0.91f) };
    const QColor selected = ui()->color(UI::ColorRole::Selection);
    const GfVec3f highlight(selected.redF(), selected.greenF(), selected.blueF());
    constexpr int sides = 12;
    constexpr int ringSegments = 96;
    {
        WRITE_LOCKER(locker, session()->auxiliaryLock(), "auxiliaryLock");
        if (!d.meshesBuilt) {
            UsdGeomScope::Define(d.host->auxiliaryStage(), paths::auxiliary::display);
            UsdGeomScope::Define(d.host->auxiliaryStage(), paths::auxiliary::materials);
            UsdGeomXform::Define(d.host->auxiliaryStage(), root);
            // Build all three modes in local unit coordinates once. Hydra keeps
            // the meshes across mode switches and pivot/zoom updates.
            for (const auto mode : { Mode::Move, Mode::Rotate, Mode::Scale }) {
                const char* modeName = mode == Mode::Move ? "Move" : (mode == Mode::Rotate ? "Rotate" : "Scale");
                const SdfPath modePath = root.AppendChild(TfToken(modeName));
                UsdGeomXform::Define(d.host->auxiliaryStage(), modePath);
                constexpr double radius = 1.0;
                for (int axis = 1; axis <= 3; ++axis) {
                    const GfVec3d normal = ViewportGizmo::axisVector(axis);
                    const GfVec3d a = ViewportGizmo::axisVector(axis % 3 + 1);
                    const GfVec3d b = GfCross(normal, a);
                    VtVec3fArray points;
                    VtIntArray counts;
                    VtIntArray indices;
                    auto vertex = [&](const GfVec3d& point) {
                        points.push_back(GfVec3f(static_cast<float>(point[0]), static_cast<float>(point[1]),
                                                 static_cast<float>(point[2])));
                    };
                    auto face = [&](int i, int j, int k, int l) {
                        counts.push_back(4);
                        indices.push_back(i);
                        indices.push_back(j);
                        indices.push_back(k);
                        indices.push_back(l);
                    };
                    auto tube = [&](const GfVec3d& from, const GfVec3d& to, double r, int segments) {
                        const GfVec3d direction = (to - from).GetNormalized();
                        const GfVec3d u = std::abs(GfDot(direction, GfVec3d::ZAxis())) < 0.9
                                              ? GfCross(direction, GfVec3d::ZAxis()).GetNormalized()
                                              : GfCross(direction, GfVec3d::YAxis()).GetNormalized();
                        const GfVec3d v = GfCross(direction, u);
                        const int base = static_cast<int>(points.size());
                        for (int i = 0; i < 2; ++i)
                            for (int j = 0; j < segments; ++j) {
                                const double angle = fullCircle * j / segments;
                                vertex((i ? to : from) + r * (u * std::cos(angle) + v * std::sin(angle)));
                            }
                        for (int j = 0; j < segments; ++j) {
                            const int next = (j + 1) % segments;
                            face(base + j, base + next, base + segments + next, base + segments + j);
                        }
                    };
                    if (mode == Mode::Rotate) {
                        const double ringRadius = radius;
                        const double tubeRadius = radius * 0.018;
                        for (int i = 0; i < ringSegments; ++i) {
                            const double theta = fullCircle * i / ringSegments;
                            const GfVec3d radial = a * std::cos(theta) + b * std::sin(theta);
                            for (int j = 0; j < sides; ++j) {
                                const double phi = fullCircle * j / sides;
                                vertex((ringRadius + tubeRadius * std::cos(phi)) * radial
                                       + tubeRadius * std::sin(phi) * normal);
                            }
                        }
                        for (int i = 0; i < ringSegments; ++i)
                            for (int j = 0; j < sides; ++j) {
                                const int nextI = (i + 1) % ringSegments;
                                const int nextJ = (j + 1) % sides;
                                face(i * sides + j, nextI * sides + j, nextI * sides + nextJ, i * sides + nextJ);
                            }
                    }
                    else {
                        // Scale shafts terminate at the near face of the endpoint cube.
                        // Move shafts extend into the cone so there is no visible gap.
                        const double shaftEnd = mode == Mode::Scale ? 0.925 : 0.82;
                        tube(GfVec3d(0.0), normal * (radius * shaftEnd), radius * 0.023, sides);
                        if (mode == Mode::Move) {
                            const int base = static_cast<int>(points.size());
                            const GfVec3d bottom = normal * (radius * 0.77);
                            for (int j = 0; j < sides; ++j) {
                                const double angle = fullCircle * j / sides;
                                vertex(bottom + radius * 0.095 * (a * std::cos(angle) + b * std::sin(angle)));
                            }
                            vertex(normal * radius);
                            const int tip = static_cast<int>(points.size()) - 1;
                            for (int j = 0; j < sides; ++j) {
                                counts.push_back(3);
                                indices.push_back(base + j);
                                indices.push_back(base + (j + 1) % sides);
                                indices.push_back(tip);
                            }
                        }
                        else {
                            const GfVec3d center = normal * radius;
                            const double half = radius * 0.075;
                            const int base = static_cast<int>(points.size());
                            for (int sign = -1; sign <= 1; sign += 2)
                                for (int j = 0; j < 4; ++j)
                                    vertex(center + normal * (half * sign)
                                           + a * (half * (j == 0 || j == 3 ? -1.0 : 1.0))
                                           + b * (half * (j < 2 ? -1.0 : 1.0)));
                            face(base, base + 3, base + 2, base + 1);
                            face(base + 4, base + 5, base + 6, base + 7);
                            for (int j = 0; j < 4; ++j)
                                face(base + j, base + (j + 1) % 4, base + 4 + (j + 1) % 4, base + 4 + j);
                        }
                    }
                    const SdfPath meshPath = modePath.AppendChild(TfToken(axis == 1 ? "X" : (axis == 2 ? "Y" : "Z")));
                    UsdGeomMesh mesh = UsdGeomMesh::Define(d.host->auxiliaryStage(), meshPath);
                    mesh.CreatePointsAttr(VtValue(points));
                    mesh.CreateFaceVertexCountsAttr(VtValue(counts));
                    mesh.CreateFaceVertexIndicesAttr(VtValue(indices));
                    // Handles are fully tessellated polygon meshes. USD's
                    // default Catmull-Clark scheme would refine them again.
                    mesh.CreateSubdivisionSchemeAttr(VtValue(UsdGeomTokens->none));
                    mesh.CreateDoubleSidedAttr(VtValue(true));
                    const GfVec3f color = colors[axis];
                    const SdfPath materialPath = paths::auxiliary::materials.AppendChild(
                        TfToken((mode == Mode::Move ? "Move" : (mode == Mode::Rotate ? "Rotate" : "Scale"))
                                + std::string(axis == 1 ? "X" : (axis == 2 ? "Y" : "Z"))));
                    ViewportAuxiliary::authorGridMaterial(d.host->auxiliaryStage(), materialPath, color);
                    UsdShadeMaterialBindingAPI::Apply(mesh.GetPrim())
                        .Bind(UsdShadeMaterial(d.host->auxiliaryStage()->GetPrimAtPath(materialPath)));
                }
            }
            d.meshesBuilt = true;
        }
        UsdGeomXform gizmo(d.host->auxiliaryStage()->GetPrimAtPath(root));
        GfMatrix4d matrix(1.0);
        matrix.SetScale(radius);
        matrix *= d.frame.orientation;
        matrix.SetTranslateOnly(d.frame.pivot);
        // Only the parent transform changes with camera distance or selection.
        UsdGeomXformOp matrixOp;
        bool resetsXformStack = false;
        const auto ops = gizmo.GetOrderedXformOps(&resetsXformStack);
        if (!ops.empty())
            matrixOp = ops.front();
        else
            matrixOp = gizmo.MakeMatrixXform();
        matrixOp.Set(matrix);
        gizmo.MakeVisible();
        for (const auto mode : { Mode::Move, Mode::Rotate, Mode::Scale }) {
            const char* modeName = mode == Mode::Move ? "Move" : (mode == Mode::Rotate ? "Rotate" : "Scale");
            UsdGeomImageable modePrim(d.host->auxiliaryStage()->GetPrimAtPath(root.AppendChild(TfToken(modeName))));
            if (mode == d.frame.mode)
                modePrim.MakeVisible();
            else
                modePrim.MakeInvisible();
        }
        for (int axis = 1; axis <= 3; ++axis) {
            const int handle = axis + (d.frame.mode == Mode::Rotate ? 3 : (d.frame.mode == Mode::Scale ? 6 : 0));
            const GfVec3f color = (d.frame.hoverAxis == handle || d.frame.activeAxis == handle) ? highlight
                                                                                                : colors[axis];
            const char* modeName = d.frame.mode == Mode::Move ? "Move"
                                                              : (d.frame.mode == Mode::Rotate ? "Rotate" : "Scale");
            const std::string name = std::string(modeName) + (axis == 1 ? "X" : (axis == 2 ? "Y" : "Z"));
            ViewportAuxiliary::authorGridMaterial(d.host->auxiliaryStage(),
                                                  paths::auxiliary::materials.AppendChild(TfToken(name)), color);
        }
    }
    d.geometryValid = true;
    d.geometryVisible = true;
    d.geometryPivot = d.frame.pivot;
    d.geometryOrientation = d.frame.orientation;
    d.geometryRadius = radius;
    d.geometryMode = d.frame.mode;
    d.geometryHover = d.frame.hoverAxis;
    d.geometryActive = d.frame.activeAxis;
    if (d.host->renderEngine())
        d.host->renderEngine()->refreshAuxiliaryStage();
}

void
ViewportGizmoPrivate::draw(QPainter& painter)
{
    // The axis geometry is rendered by Hydra. Keep only the center control
    // in the QPainter overlay, so it stays readable and clickable.
    if (!d.frame.enabled || d.frame.mode == Mode::None || !d.host->context())
        return;
    if (!d.frame.pivotValid)
        return;
    QPointF center;
    if (!d.host->projectWorldToScreen(d.frame.pivot, center))
        return;
    const int centerHandle = d.frame.mode == Mode::Move ? 11 : (d.frame.mode == Mode::Rotate ? 10 : 12);
    QColor centerColor(35, 35, 35, 220);
    if (d.frame.hoverAxis == centerHandle || d.frame.activeAxis == centerHandle)
        centerColor = ui()->color(UI::ColorRole::Selection);
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    if (d.frame.pivotEditing) {
        const double radius = worldPerPixel() * ui()->gizmoSize(UI::UIScale::Medium);
        const UI::ColorRole roles[] = { UI::ColorRole::AxisX, UI::ColorRole::AxisY, UI::ColorRole::AxisZ };
        for (int axis = 1; axis <= 3; ++axis) {
            const int handle = axis + (d.frame.mode == Mode::Rotate ? 3 : (d.frame.mode == Mode::Scale ? 6 : 0));
            const QColor color = d.frame.activeAxis == handle || d.frame.hoverAxis == handle
                                     ? ui()->color(UI::ColorRole::Selection)
                                     : ui()->color(roles[axis - 1]);
            painter.setPen(QPen(color, 2.0, Qt::DotLine, Qt::RoundCap));
            painter.setBrush(Qt::NoBrush);
            if (d.frame.mode == Mode::Rotate) {
                QPainterPath path;
                bool previous = false;
                for (int segment = 0; segment <= 96; ++segment) {
                    QPointF point;
                    const bool valid = rotationPoint(axis, segment * GfDegreesToRadians(360.0) / 96.0, point);
                    if (valid) {
                        if (previous)
                            path.lineTo(point);
                        else
                            path.moveTo(point);
                    }
                    previous = valid;
                }
                painter.drawPath(path);
            }
            else {
                const GfVec3d direction = d.frame.mode == Mode::Scale ? scaleAxisVector(axis) : axisVector(axis);
                QPointF end;
                if (!d.host->projectWorldToScreen(d.frame.pivot + direction * radius, end))
                    continue;
                painter.drawLine(center, end);
                painter.setPen(QPen(color, 1.5));
                if (d.frame.mode == Mode::Scale)
                    painter.drawRect(QRectF(end - QPointF(4, 4), QSizeF(8, 8)));
                else
                    painter.drawEllipse(end, 4, 4);
            }
        }
    }
    painter.setPen(QPen(ui()->color(UI::ColorRole::Text), 1.5));
    if (d.frame.pivotEditing)
        painter.setBrush(Qt::NoBrush);
    else
        painter.setBrush(centerColor);
    painter.drawEllipse(center, 5.0, 5.0);
    painter.restore();
}

void
ViewportGizmoPrivate::drawRotationFeedback(QPainter& painter)
{
    if (!d.frame.enabled || !d.frame.pivotValid || d.frame.mode != Mode::Rotate || d.frame.activeAxis < 4
        || d.frame.activeAxis > 6)
        return;
    const ViewportGizmo::Frame liveFrame = d.frame;
    if (d.frame.rotationStartOrientationValid) {
        // Keep the solid start ray fixed while the dashed ray follows the
        // cursor angle in the coordinate frame captured when dragging began.
        d.frame.orientation = d.frame.rotationStartOrientation;
        d.frame.orientationValid = true;
    }
    const int axis = d.frame.activeAxis - 3;
    QPointF center, basisA, basisB;
    if (!rotationScreenBasis(axis, center, basisA, basisB)) {
        d.frame = liveFrame;
        return;
    }
    const auto point = [&](double angle) {
        QPointF projected = center;
        rotationPoint(axis, angle, projected);
        return projected;
    };
    const double start = d.frame.rotationStartAngle;
    const double delta = d.frame.rotationDeltaAngle;
    const QPointF initial = point(start);
    const QPointF current = point(start + delta);
    QPainterPath arc(initial);
    const int segments = std::max(1, static_cast<int>(std::ceil(std::abs(GfRadiansToDegrees(delta)) / 3.0)));
    for (int i = 1; i <= segments; ++i)
        arc.lineTo(point(start + delta * i / segments));
    QPainterPath sector(center);
    sector.lineTo(initial);
    sector.connectPath(arc);
    sector.closeSubpath();

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    const QColor color = ui()->color(UI::ColorRole::Selection);
    QColor fill = color;
    fill.setAlpha(35);
    painter.fillPath(sector, fill);
    painter.setPen(QPen(color, 2.0));
    painter.drawLine(center, initial);
    painter.setPen(QPen(color, 1.5, Qt::DashLine));
    painter.drawLine(center, current);
    painter.drawPath(arc);

    painter.restore();
    d.frame = liveFrame;
}

ViewportGizmo::ViewportGizmo(ViewportHost& host)
    : p(new ViewportGizmoPrivate())
{
    p->d.host = &host;
}

ViewportGizmo::~ViewportGizmo() = default;

void
ViewportGizmo::invalidate()
{
    p->d.geometryValid = false;
}

void
ViewportGizmo::reset()
{
    p->d.meshesBuilt = false;
    p->d.geometryVisible = false;
    invalidate();
}

void
ViewportGizmo::clear()
{
    p->clearGeometry();
}

void
ViewportGizmo::update(const Frame& frame)
{
    p->d.frame = frame;
    p->updateGeometry();
}

void
ViewportGizmo::draw(const Frame& frame, QPainter& painter)
{
    p->d.frame = frame;
    p->drawRotationFeedback(painter);
    p->draw(painter);
}

int
ViewportGizmo::hitTest(const Frame& frame, const QPointF& pos)
{
    p->d.frame = frame;
    return p->hitTest(pos);
}

QPointF
ViewportGizmo::axisDirection(const Frame& frame, int axis)
{
    p->d.frame = frame;
    return p->axisDirection(axis);
}

bool
ViewportGizmo::rotationAngle(const Frame& frame, int axis, const QPointF& pos, double& angle)
{
    p->d.frame = frame;
    return p->rotationAngle(axis, pos, angle, nullptr);
}

double
ViewportGizmo::worldPerPixel(const GfVec3d& pivot)
{
    return p->worldPerPixel(pivot);
}

GfVec3d
ViewportGizmo::axisVector(int axis)
{
    const int component = ((axis - 1) % 3) + 1;
    if (component == 1)
        return GfVec3d::XAxis();
    if (component == 2)
        return GfVec3d::YAxis();
    if (component == 3)
        return GfVec3d::ZAxis();
    return GfVec3d(0.0);
}

}  // namespace stageviz
