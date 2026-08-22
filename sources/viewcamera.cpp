// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#include "viewcamera.h"

#include <algorithm>
#include <cmath>
#include <pxr/base/gf/frustum.h>
#include <pxr/base/gf/range1d.h>
#include <pxr/base/gf/range3d.h>
#include <pxr/base/gf/rotation.h>
#include <pxr/imaging/cameraUtil/conformWindow.h>
#include <pxr/usd/usdGeom/bboxCache.h>

namespace stageviz {

class ViewCameraPrivate {
public:
    void init();
    void frame(const GfRange3d& range);
    void tumble(double x, double y);
    void truck(double right, double up);
    void distance(double factor);
    double mapToFrustumHeight(int height);
    GfMatrix4d mapToCameraUp();
    GfCamera camera();
    GfMatrix4d rotateAxis(const GfVec3d& value, double angle);
    double effectiveFov() const;
    void reset();

public:
    struct Data {
        double aspectRatio;
        double fov;
        double focalLength;
        double sensorWidth;
        double sensorHeight;
        double nearClipping;
        double farClipping;
        double fit;
        double distance;
        GfMatrix4d inverseUp;
        GfBBox3d boundingBox;
        GfVec3d center;
        GfVec3d focusPoint;
        GfRange3d range;
        ViewCamera::CameraUp cameraUp;
        ViewCamera::CameraMode cameraMode;
        ViewCamera::FovDirection direction;
        ViewCamera::ProjectionMode projectionMode;
        bool aspectRatioLocked;
        bool letterboxEnabled;
        double letterboxOpacity;
        double axisyaw;
        double axispitch;
        double axisroll;
        GfCamera camera;
        bool valid = false;
        bool identity = true;
    };

    Data d;
};

void
ViewCameraPrivate::init()
{
    d.aspectRatio = 1.0;
    d.fov = 40.0;
    d.focalLength = 50.0;
    d.sensorWidth = 36.0;
    d.sensorHeight = 24.0;
    d.nearClipping = 1.0;
    d.farClipping = 2000000.0;
    d.fit = 1.25;
    d.distance = 1.0;
    d.inverseUp = GfMatrix4d(1.0);
    d.boundingBox = GfBBox3d();
    d.center = GfVec3d(0.0);
    d.focusPoint = d.center;
    d.range = GfRange3d();
    d.cameraUp = ViewCamera::Y;
    d.cameraMode = ViewCamera::None;
    d.direction = ViewCamera::Vertical;
    d.projectionMode = ViewCamera::FieldOfView;
    d.aspectRatioLocked = false;
    d.letterboxEnabled = false;
    d.letterboxOpacity = 0.9;
    d.axisyaw = 0.0;
    d.axispitch = 0.0;
    d.axisroll = 0.0;
    d.inverseUp = mapToCameraUp();
    d.valid = false;
    d.identity = true;
}

void
ViewCameraPrivate::frame(const GfRange3d& range)
{
    if (range.IsEmpty())
        return;

    const GfVec3d size = range.GetSize();
    const double maxsize = std::max(size[0], std::max(size[1], size[2]));

    if (!std::isfinite(maxsize) || maxsize <= 0.0)
        return;

    double fovangle = effectiveFov() * 0.5;
    if (!std::isfinite(fovangle) || fovangle <= 0.0)
        fovangle = 0.5;

    const double radians = fovangle * M_PI / 180.0;
    const double tangent = std::tan(radians);
    if (!std::isfinite(tangent) || tangent <= 0.0)
        return;

    const double length = maxsize * d.fit * 0.5;
    double distance = length / tangent;

    if (!std::isfinite(distance))
        return;

    if (distance < d.nearClipping + maxsize * 0.5)
        distance = d.nearClipping + length;

    d.distance = distance;
    d.center = range.GetMidpoint();
    d.focusPoint = d.center;
    d.valid = false;
}

void
ViewCameraPrivate::tumble(double x, double y)
{
    d.axisroll += x;
    d.axispitch += y;
    d.valid = false;
    d.identity = false;
}

void
ViewCameraPrivate::truck(double right, double up)
{
    const GfFrustum frustum = camera().GetFrustum();
    const GfVec3d cameraUp = frustum.ComputeUpVector();
    const GfVec3d cameraRight = GfCross(frustum.ComputeViewDirection(), cameraUp);
    const GfVec3d delta = right * cameraRight + up * cameraUp;

    d.center += delta;
    d.focusPoint += delta;
    d.valid = false;
    d.identity = false;
}

void
ViewCameraPrivate::distance(double factor)
{
    if (!std::isfinite(factor) || factor <= 0.0)
        return;

    double newDistance = d.distance * factor;

    if (factor > 1.0 && d.distance < 2.0 && !d.range.IsEmpty()) {
        const GfVec3d size = d.range.GetSize();
        const double maxsize = std::max(size[0], std::max(size[1], size[2]));

        if (std::isfinite(maxsize) && maxsize > 0.0)
            newDistance = d.distance + std::min(maxsize / 25.0, factor);
    }

    if (!std::isfinite(newDistance))
        return;

    constexpr double minDistance = 0.01;
    d.distance = std::max(minDistance, newDistance);
    d.valid = false;
    d.identity = false;
}

double
ViewCameraPrivate::mapToFrustumHeight(int height)
{
    if (height <= 0)
        return 0.0;

    const GfFrustum frustum = camera().GetFrustum();
    return frustum.GetWindow().GetSize()[1] * d.distance / static_cast<double>(height);
}

GfMatrix4d
ViewCameraPrivate::mapToCameraUp()
{
    GfMatrix4d matrix(1.0);

    if (d.cameraUp == ViewCamera::Z) {
        matrix.SetRotate(GfRotation(GfVec3d::XAxis(), -90.0));
    }
    else if (d.cameraUp == ViewCamera::X) {
        matrix.SetRotate(GfRotation(GfVec3d::YAxis(), -90.0));
    }

    return matrix.GetInverse();
}

double
ViewCameraPrivate::effectiveFov() const
{
    if (d.projectionMode != ViewCamera::Physical)
        return d.fov;

    const double aperture = d.direction == ViewCamera::Horizontal ? d.sensorWidth : d.sensorHeight;
    if (!std::isfinite(aperture) || aperture <= 0.0 || !std::isfinite(d.focalLength) || d.focalLength <= 0.0)
        return d.fov;

    return 2.0 * std::atan(aperture / (2.0 * d.focalLength)) * 180.0 / M_PI;
}

GfCamera
ViewCameraPrivate::camera()
{
    if (!d.valid) {
        GfMatrix4d matrix = GfMatrix4d().SetTranslate(GfVec3d().ZAxis() * d.distance);
        matrix *= rotateAxis(GfVec3d().ZAxis(), -d.axisyaw);
        matrix *= rotateAxis(GfVec3d().XAxis(), -d.axispitch);
        matrix *= rotateAxis(GfVec3d().YAxis(), -d.axisroll);
        matrix *= d.inverseUp;
        matrix *= GfMatrix4d().SetTranslate(d.focusPoint);

        d.camera.SetTransform(matrix);
        d.camera.SetFocusDistance(d.distance);

        const GfCamera::FOVDirection direction = d.direction == ViewCamera::Horizontal ? GfCamera::FOVHorizontal
                                                                                       : GfCamera::FOVVertical;

        if (d.projectionMode == ViewCamera::Physical) {
            d.camera.SetHorizontalAperture(d.sensorWidth);
            d.camera.SetVerticalAperture(d.sensorHeight);
            d.camera.SetFocalLength(d.focalLength);

            CameraUtilConformWindowPolicy policy = CameraUtilConformWindowPolicy::CameraUtilFit;
            CameraUtilConformWindow(&d.camera, policy, d.aspectRatio);
        }
        else {
            d.camera.SetPerspectiveFromAspectRatioAndFieldOfView(d.aspectRatio, d.fov, direction);
        }

        d.camera.SetClippingRange(GfRange1f(d.nearClipping, d.farClipping));

        d.valid = true;
    }

    return d.camera;
}

GfMatrix4d
ViewCameraPrivate::rotateAxis(const GfVec3d& value, double angle)
{
    return GfMatrix4d(1.0).SetRotate(GfRotation(value, angle));
}

void
ViewCameraPrivate::reset()
{
    d.cameraMode = ViewCamera::None;
    d.axisyaw = 0.0;
    d.axispitch = 30.0;
    d.axisroll = -45.0;
    d.inverseUp = mapToCameraUp();
    d.center = GfVec3d(0.0);
    d.focusPoint = d.center;
    d.distance = 60.0;
    d.valid = false;
    d.identity = true;
}

ViewCamera::ViewCamera(QObject* parent)
    : QObject(parent)
    , p(new ViewCameraPrivate())
{
    p->init();
}

ViewCamera::ViewCamera(double aspectratio, double fov, ViewCamera::FovDirection direction)
    : QObject(nullptr)
    , p(new ViewCameraPrivate())
{
    p->init();
    p->d.aspectRatio = aspectratio;
    p->d.fov = fov;
    p->d.direction = direction;
    p->d.valid = false;
    p->d.identity = false;
}

ViewCamera::~ViewCamera() = default;

bool
ViewCamera::isIdentity() const
{
    return p->d.identity;
}

void
ViewCamera::frame(const GfBBox3d& bbox)
{
    p->frame(bbox.ComputeAlignedRange());
    p->d.identity = false;
    Q_EMIT cameraChanged(camera());
}

void
ViewCamera::resetView()
{
    p->reset();
    Q_EMIT cameraChanged(camera());
}

void
ViewCamera::tumble(double x, double y)
{
    p->tumble(x, y);
    Q_EMIT cameraChanged(camera());
}

void
ViewCamera::truck(double right, double up)
{
    p->truck(right, up);
    Q_EMIT cameraChanged(camera());
}

void
ViewCamera::distance(double factor)
{
    p->distance(factor);
    Q_EMIT cameraChanged(camera());
}

double
ViewCamera::mapToFrustumHeight(int height)
{
    return p->mapToFrustumHeight(height);
}

GfCamera
ViewCamera::camera() const
{
    return p->camera();
}

double
ViewCamera::aspectRatio() const
{
    return p->d.aspectRatio;
}

void
ViewCamera::setAspectRatio(double aspectRatio)
{
    if (p->d.aspectRatio == aspectRatio)
        return;

    p->d.aspectRatio = aspectRatio;
    p->d.valid = false;
    p->d.identity = false;
    Q_EMIT cameraChanged(camera());
}

bool
ViewCamera::aspectRatioLocked() const
{
    return p->d.aspectRatioLocked;
}

void
ViewCamera::setAspectRatioLocked(bool locked)
{
    if (p->d.aspectRatioLocked == locked)
        return;

    p->d.aspectRatioLocked = locked;
    p->d.valid = false;
    p->d.identity = false;
    Q_EMIT cameraChanged(camera());
}

bool
ViewCamera::letterboxEnabled() const
{
    return p->d.letterboxEnabled;
}

void
ViewCamera::setLetterboxEnabled(bool enabled)
{
    if (p->d.letterboxEnabled == enabled)
        return;

    p->d.letterboxEnabled = enabled;
    Q_EMIT cameraChanged(camera());
}

double
ViewCamera::letterboxOpacity() const
{
    return p->d.letterboxOpacity;
}

void
ViewCamera::setLetterboxOpacity(double opacity)
{
    opacity = std::clamp(opacity, 0.0, 1.0);
    if (p->d.letterboxOpacity == opacity)
        return;

    p->d.letterboxOpacity = opacity;
    Q_EMIT cameraChanged(camera());
}

ViewCamera::ProjectionMode
ViewCamera::projectionMode() const
{
    return p->d.projectionMode;
}

void
ViewCamera::setProjectionMode(ViewCamera::ProjectionMode mode)
{
    if (p->d.projectionMode == mode)
        return;

    p->d.projectionMode = mode;
    p->d.valid = false;
    p->d.identity = false;
    Q_EMIT cameraChanged(camera());
}

double
ViewCamera::fov() const
{
    return p->d.fov;
}

void
ViewCamera::setFov(double fov)
{
    if (p->d.fov == fov)
        return;

    p->d.fov = fov;
    p->d.valid = false;
    p->d.identity = false;
    Q_EMIT cameraChanged(camera());
}

ViewCamera::FovDirection
ViewCamera::fovDirection() const
{
    return p->d.direction;
}

void
ViewCamera::setFovDirection(ViewCamera::FovDirection direction)
{
    if (p->d.direction == direction)
        return;

    p->d.direction = direction;
    p->d.valid = false;
    p->d.identity = false;
    Q_EMIT cameraChanged(camera());
}

double
ViewCamera::focalLength() const
{
    return p->d.focalLength;
}

void
ViewCamera::setFocalLength(double focalLength)
{
    if (!std::isfinite(focalLength) || focalLength <= 0.0 || p->d.focalLength == focalLength)
        return;

    p->d.focalLength = focalLength;
    p->d.valid = false;
    p->d.identity = false;
    Q_EMIT cameraChanged(camera());
}

double
ViewCamera::sensorWidth() const
{
    return p->d.sensorWidth;
}

void
ViewCamera::setSensorWidth(double width)
{
    if (!std::isfinite(width) || width <= 0.0 || p->d.sensorWidth == width)
        return;

    p->d.sensorWidth = width;
    p->d.valid = false;
    p->d.identity = false;
    Q_EMIT cameraChanged(camera());
}

double
ViewCamera::sensorHeight() const
{
    return p->d.sensorHeight;
}

void
ViewCamera::setSensorHeight(double height)
{
    if (!std::isfinite(height) || height <= 0.0 || p->d.sensorHeight == height)
        return;

    p->d.sensorHeight = height;
    p->d.valid = false;
    p->d.identity = false;
    Q_EMIT cameraChanged(camera());
}

GfVec3d
ViewCamera::focusPoint() const
{
    return p->d.focusPoint;
}

void
ViewCamera::setFocusPoint(const GfVec3d& point)
{
    if (p->d.focusPoint == point)
        return;

    const GfCamera cam = p->camera();
    const GfVec3d camPos = cam.GetTransform().ExtractTranslation();

    p->d.focusPoint = point;
    p->d.center = point;
    p->d.distance = (camPos - point).GetLength();
    p->d.valid = false;
    p->d.identity = false;

    Q_EMIT cameraChanged(camera());
}

GfBBox3d
ViewCamera::boundingBox() const
{
    return p->d.boundingBox;
}

void
ViewCamera::setBoundingBox(const GfBBox3d& boundingBox)
{
    if (p->d.boundingBox == boundingBox)
        return;

    p->d.boundingBox = boundingBox;
    p->d.center = boundingBox.ComputeCentroid();
    p->d.focusPoint = p->d.center;
    p->d.range = boundingBox.ComputeAlignedRange();
    p->d.valid = false;
    p->d.identity = false;

    Q_EMIT cameraChanged(camera());
}

double
ViewCamera::fit() const
{
    return p->d.fit;
}

void
ViewCamera::setFit(double fit)
{
    if (p->d.fit == fit)
        return;

    p->d.fit = fit;
    p->d.valid = false;
    p->d.identity = false;
    Q_EMIT cameraChanged(camera());
}

ViewCamera::CameraUp
ViewCamera::cameraUp() const
{
    return p->d.cameraUp;
}

void
ViewCamera::setCameraUp(ViewCamera::CameraUp cameraUp)
{
    if (p->d.cameraUp == cameraUp)
        return;

    p->d.cameraUp = cameraUp;
    p->d.inverseUp = p->mapToCameraUp();
    p->d.valid = false;
    p->d.identity = false;
    Q_EMIT cameraChanged(camera());
}

double
ViewCamera::axisYaw() const
{
    return p->d.axisyaw;
}

void
ViewCamera::setAxisYaw(double yaw)
{
    if (p->d.axisyaw == yaw)
        return;

    p->d.axisyaw = yaw;
    p->d.valid = false;
    p->d.identity = false;
    Q_EMIT cameraChanged(camera());
}

double
ViewCamera::axisPitch() const
{
    return p->d.axispitch;
}

void
ViewCamera::setAxisPitch(double pitch)
{
    if (p->d.axispitch == pitch)
        return;

    p->d.axispitch = pitch;
    p->d.valid = false;
    p->d.identity = false;
    Q_EMIT cameraChanged(camera());
}

double
ViewCamera::axisRoll() const
{
    return p->d.axisroll;
}

void
ViewCamera::setAxisRoll(double roll)
{
    if (p->d.axisroll == roll)
        return;

    p->d.axisroll = roll;
    p->d.valid = false;
    p->d.identity = false;
    Q_EMIT cameraChanged(camera());
}

ViewCamera::CameraMode
ViewCamera::cameraMode() const
{
    return p->d.cameraMode;
}

void
ViewCamera::setCameraMode(ViewCamera::CameraMode cameraMode)
{
    if (p->d.cameraMode == cameraMode)
        return;

    p->d.cameraMode = cameraMode;
    p->d.valid = false;
    p->d.identity = false;
    Q_EMIT cameraChanged(camera());
}

double
ViewCamera::nearClipping() const
{
    return p->d.nearClipping;
}

void
ViewCamera::setNearClipping(double nearClipping)
{
    if (p->d.nearClipping == nearClipping)
        return;

    p->d.nearClipping = nearClipping;
    p->d.valid = false;
    p->d.identity = false;
    Q_EMIT cameraChanged(camera());
}

double
ViewCamera::farClipping() const
{
    return p->d.farClipping;
}

void
ViewCamera::setFarClipping(double farClipping)
{
    if (p->d.farClipping == farClipping)
        return;

    p->d.farClipping = farClipping;
    p->d.valid = false;
    p->d.identity = false;
    Q_EMIT cameraChanged(camera());
}

double
ViewCamera::cameraDistance() const
{
    return p->d.distance;
}

void
ViewCamera::setCameraDistance(double distance)
{
    if (p->d.distance == distance)
        return;

    p->d.distance = distance;
    p->d.valid = false;
    p->d.identity = false;
    Q_EMIT cameraChanged(camera());
}

void
ViewCamera::reset()
{
    resetView();
}

}  // namespace stageviz
