// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#pragma once

#include "stageviz.h"

#include <QExplicitlySharedDataPointer>
#include <QObject>
#include <pxr/usd/usdGeom/camera.h>

PXR_NAMESPACE_USING_DIRECTIVE

namespace stageviz {

class ViewCameraPrivate;

/**
 * @class ViewCamera
 * @brief Interactive camera controller for the USD viewer.
 *
 * Provides a high-level camera interface used by the viewer to
 * navigate and frame USD scenes. The class manages camera state
 * such as focus point, clipping planes, field of view, and
 * navigation modes like tumble, truck, and zoom.
 *
 * Internally the camera state can be converted to a USD
 * compatible @c GfCamera for rendering.
 */
class ViewCamera {
public:
    /**
     * @brief Camera up-axis orientation.
     */
    enum CameraUp { X, Y, Z };

    /**
     * @brief Interactive camera manipulation modes.
     */
    enum CameraMode { None, Truck, Tumble, Zoom, Pick };

    /**
     * @brief Field-of-view reference direction.
     */
    enum FovDirection { Vertical, Horizontal };

    /**
     * @brief Constructs a default camera.
     */
    ViewCamera();

    /**
     * @brief Constructs a camera with aspect ratio and field of view.
     *
     * @param aspectratio Camera aspect ratio.
     * @param fov Field of view in degrees.
     * @param direction Field-of-view direction.
     */
    ViewCamera(double aspectratio, double fov, ViewCamera::FovDirection direction = Vertical);

    /**
     * @brief Copy constructor.
     */
    ViewCamera(const ViewCamera& other);

    /**
     * @brief Destroys the ViewCamera instance.
     */
    virtual ~ViewCamera();

    /** @name Framing and Navigation */
    ///@{

    /**
     * @brief Frames the entire scene bounding box.
     */
    void frameAll() const;

    /**
     * @brief Resets the camera view to default orientation.
     */
    void resetView();

    /**
     * @brief Rotates the camera around the focus point.
     *
     * @param x Horizontal rotation.
     * @param y Vertical rotation.
     */
    void tumble(double x, double y);

    /**
     * @brief Translates the camera parallel to the view plane.
     *
     * @param right Horizontal translation.
     * @param up Vertical translation.
     */
    void truck(double right, double up);

    /**
     * @brief Adjusts the camera distance to the focus point.
     *
     * @param factor Zoom scaling factor.
     */
    void distance(double factor);

    /**
     * @brief Maps a pixel height to a frustum height value.
     *
     * Used for converting screen-space movement into world-space
     * camera adjustments.
     *
     * @param height Screen-space height.
     *
     * @return Frustum-space height.
     */
    double mapToFrustumHeight(int height);

    ///@}

    /** @name Camera Access */
    ///@{

    /**
     * @brief Returns the USD camera representation.
     */
    GfCamera camera() const;

    ///@}

    /** @name Projection */
    ///@{

    /**
     * @brief Returns the aspect ratio.
     */
    double aspectRatio() const;

    /**
     * @brief Sets the aspect ratio.
     */
    void setAspectRatio(double aspectRatio);

    /**
     * @brief Returns the field of view in degrees.
     */
    double fov() const;

    /**
     * @brief Sets the field of view in degrees.
     */
    void setFov(double fov);

    /**
     * @brief Returns the field-of-view direction.
     */
    FovDirection fovDirection() const;

    /**
     * @brief Sets the field-of-view direction.
     */
    void setFovDirection(ViewCamera::FovDirection direction);

    ///@}

    /** @name Scene Framing */
    ///@{

    /**
     * @brief Returns the current focus point.
     */
    GfVec3d focusPoint() const;

    /**
     * @brief Sets the focus point.
     */
    void setFocusPoint(const GfVec3d& point);

    /**
     * @brief Returns the scene bounding box.
     */
    GfBBox3d boundingBox() const;

    /**
     * @brief Sets the scene bounding box used for framing.
     */
    void setBoundingBox(const GfBBox3d& boundingBox);

    ///@}

    /** @name Orientation */
    ///@{

    /**
     * @brief Returns the camera up-axis.
     */
    CameraUp cameraUp();

    /**
     * @brief Sets the camera up-axis.
     */
    void setCameraUp(ViewCamera::CameraUp cameraUp);

    ///@}

    /** @name Interaction */
    ///@{

    /**
     * @brief Returns the current camera interaction mode.
     */
    CameraMode cameraMode();

    /**
     * @brief Sets the camera interaction mode.
     */
    void setCameraMode(ViewCamera::CameraMode cameraMode);

    ///@}

    /** @name Clipping */
    ///@{

    /**
     * @brief Returns the near clipping plane distance.
     */
    double nearClipping() const;

    /**
     * @brief Sets the near clipping plane distance.
     */
    void setNearClipping(double near);

    /**
     * @brief Returns the far clipping plane distance.
     */
    double farClipping() const;

    /**
     * @brief Sets the far clipping plane distance.
     */
    void setFarClipping(double near);

    ///@}

    /**
     * @brief Assignment operator.
     */
    ViewCamera& operator=(const ViewCamera& other);

private:
    QExplicitlySharedDataPointer<ViewCameraPrivate> p;
};

}  // namespace stageviz
