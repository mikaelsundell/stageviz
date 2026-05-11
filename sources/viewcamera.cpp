// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#include "viewcamera.h"
#include "qtutils.h"
#include <pxr/base/gf/frustum.h>
#include <pxr/base/gf/range1d.h>
#include <pxr/base/gf/rotation.h>
#include <pxr/usd/usdGeom/bboxCache.h>

namespace stageviz {

class ViewCameraPrivate {
public:
    void init();
    void frameAll();
    void tumble(double x, double y);
    void truck(double right, double up);
    void distance(double factor);
    double mapToFrustumHeight(int height);
    GfMatrix4d mapToCameraUp();
    GfCamera camera();
    GfMatrix4d rotateAxis(const GfVec3d& value, double angle);
    void reset();

public:
    struct Data {
        double aspectRatio;
        double fov;
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
        double axisyaw;
        double axispitch;
        double axisroll;
        GfCamera camera;
        bool valid = false;
    };
    Data d;
};

void
ViewCameraPrivate::init()
{
    d.aspectRatio = 1.0;
    d.fov = 60.0;
    d.nearClipping = 1.0;
    d.farClipping = 2000000.0;
    d.fit = 1.1;
    d.distance = 1.0;
    d.inverseUp = GfMatrix4d(1.0);
    d.boundingBox = GfBBox3d();
    d.center = GfVec3d(0.0);
    d.focusPoint = d.center;
    d.range = GfRange3d();
    d.cameraUp = ViewCamera::Y;
    d.cameraMode = ViewCamera::None;
    d.direction = ViewCamera::Vertical;
    d.axisyaw = 0.0;
    d.axispitch = 0.0;
    d.axisroll = 0.0;
    d.inverseUp = mapToCameraUp();
    d.valid = false;
}

void
ViewCameraPrivate::frameAll()
{
    GfVec3d size = d.range.GetSize();
    double maxsize = std::max(size[0], std::max(size[1], size[2]));

    double fovangle = d.fov * 0.5;
    if (fovangle == 0.0)
        fovangle = 0.5;

    double length = maxsize * d.fit * 0.5;
    d.distance = length / std::atan(fovangle * M_PI / 180.0);

    if (d.distance < d.nearClipping + maxsize * 0.5)
        d.distance = d.nearClipping + length;

    d.center = d.range.GetMidpoint();
    d.focusPoint = d.center;
    d.valid = false;
}

void
ViewCameraPrivate::tumble(double x, double y)
{
    d.axisroll += x;
    d.axispitch += y;
    d.valid = false;
}

void
ViewCameraPrivate::truck(double right, double up)
{
    const GfFrustum frustum = camera().GetFrustum();
    GfVec3d cameraUp = frustum.ComputeUpVector();
    GfVec3d cameraRight = GfCross(frustum.ComputeViewDirection(), cameraUp);
    GfVec3d delta = right * cameraRight + up * cameraUp;

    d.center += delta;
    d.focusPoint += delta;
    d.valid = false;
}

void
ViewCameraPrivate::distance(double factor)
{
    if (factor > 1.0 && d.distance < 2.0) {
        GfVec3d size = d.range.GetSize();
        double maxsize = std::max(size[0], std::max(size[1], size[2]));
        d.distance += std::min(maxsize / 25.0, factor);
    }
    else {
        d.distance *= factor;
    }

    d.valid = false;
}

double
ViewCameraPrivate::mapToFrustumHeight(int height)
{
    const GfFrustum frustum = camera().GetFrustum();
    return frustum.GetWindow().GetSize()[1] * d.distance / height;
}

GfMatrix4d
ViewCameraPrivate::mapToCameraUp()
{
    GfMatrix4d matrix;

    if (d.cameraUp == ViewCamera::Z) {
        matrix.SetRotate(GfRotation(GfVec3d::XAxis(), -90.0));
    }
    else if (d.cameraUp == ViewCamera::X) {
        matrix.SetRotate(GfRotation(GfVec3d::YAxis(), -90.0));
    }
    else {
        matrix = GfMatrix4d(1.0);
    }

    return matrix.GetInverse();
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
        d.camera.SetPerspectiveFromAspectRatioAndFieldOfView(d.aspectRatio, d.fov, GfCamera::FOVVertical);
        d.camera.SetClippingRange(GfRange1f(d.nearClipping, d.farClipping));

        CameraUtilConformWindowPolicy policy = CameraUtilConformWindowPolicy::CameraUtilFit;
        CameraUtilConformWindow(&d.camera, policy, d.aspectRatio);
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
    const GfBBox3d boundingBox = d.boundingBox;
    const GfRange3d range = d.range;
    const ViewCamera::CameraUp cameraUp = d.cameraUp;
    init();
    d.boundingBox = boundingBox;
    d.range = range;
    d.cameraUp = cameraUp;
    d.inverseUp = mapToCameraUp();
    d.center = d.range.GetMidpoint();
    d.focusPoint = d.center;
    d.valid = false;
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
}

ViewCamera::~ViewCamera() = default;

void
ViewCamera::frameAll()
{
    p->frameAll();
    Q_EMIT cameraChanged(camera());
}

void
ViewCamera::resetView()
{
    p->init();
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
    if (p->d.aspectRatio != aspectRatio) {
        p->d.aspectRatio = aspectRatio;
        p->d.valid = false;
        Q_EMIT cameraChanged(camera());
    }
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

    GfCamera cam = p->camera();
    GfVec3d camPos = cam.GetTransform().ExtractTranslation();

    p->d.focusPoint = point;
    p->d.center = point;
    p->d.distance = (camPos - point).GetLength();
    p->d.valid = false;

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
    if (p->d.boundingBox != boundingBox) {
        p->d.boundingBox = boundingBox;
        p->d.center = boundingBox.ComputeCentroid();
        p->d.focusPoint = p->d.center;
        p->d.range = boundingBox.ComputeAlignedRange();
        p->d.valid = false;
        Q_EMIT cameraChanged(camera());
    }
}

ViewCamera::CameraMode
ViewCamera::cameraMode() const
{
    return p->d.cameraMode;
}

void
ViewCamera::setCameraMode(ViewCamera::CameraMode cameraMode)
{
    if (p->d.cameraMode != cameraMode) {
        p->d.cameraMode = cameraMode;
        p->d.valid = false;
        Q_EMIT cameraChanged(camera());
    }
}

ViewCamera::CameraUp
ViewCamera::cameraUp() const
{
    return p->d.cameraUp;
}

void
ViewCamera::setCameraUp(ViewCamera::CameraUp cameraUp)
{
    if (p->d.cameraUp != cameraUp) {
        p->d.cameraUp = cameraUp;
        p->d.inverseUp = p->mapToCameraUp();
        p->d.valid = false;
        Q_EMIT cameraChanged(camera());
    }
}

double
ViewCamera::fov() const
{
    return p->d.fov;
}

void
ViewCamera::setFov(double fov)
{
    if (p->d.fov != fov) {
        p->d.fov = fov;
        p->d.valid = false;
        Q_EMIT cameraChanged(camera());
    }
}

ViewCamera::FovDirection
ViewCamera::fovDirection() const
{
    return p->d.direction;
}

void
ViewCamera::setFovDirection(ViewCamera::FovDirection direction)
{
    if (p->d.direction != direction) {
        p->d.direction = direction;
        p->d.valid = false;
        Q_EMIT cameraChanged(camera());
    }
}

double
ViewCamera::nearClipping() const
{
    return p->d.nearClipping;
}

void
ViewCamera::setNearClipping(double nearClipping)
{
    if (p->d.nearClipping != nearClipping) {
        p->d.nearClipping = nearClipping;
        p->d.valid = false;
        Q_EMIT cameraChanged(camera());
    }
}

double
ViewCamera::farClipping() const
{
    return p->d.farClipping;
}

void
ViewCamera::setFarClipping(double farClipping)
{
    if (p->d.farClipping != farClipping) {
        p->d.farClipping = farClipping;
        p->d.valid = false;
        Q_EMIT cameraChanged(camera());
    }
}

void
ViewCamera::reset()
{
    p->reset();
    Q_EMIT cameraChanged(camera());
}

}  // namespace stageviz
