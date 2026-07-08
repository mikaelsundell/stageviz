// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#pragma once

#include "stageviz.h"
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
class ViewCamera : public QObject {
    Q_OBJECT
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

public:
    /**
     * @brief Constructs a ViewCamera.
     *
     * @param parent Optional parent object.
     */
    ViewCamera(QObject* parent = nullptr);

    /**
     * @brief Constructs a camera with aspect ratio and field of view.
     *
     * @param aspectratio Camera aspect ratio.
     * @param fov Field of view in degrees.
     * @param direction Field-of-view direction.
     */
    ViewCamera(double aspectratio, double fov, ViewCamera::FovDirection direction = Vertical);

    /**
     * @brief Destroys the ViewCamera instance.
     */
    ~ViewCamera();
    
    /**
     * @brief Returns whether the camera is in its default state.
     *
     * A camera is considered to be in its identity state if it has not
     * been manipulated or configured since construction or the last call
     * to reset(). This is primarily used during stage initialization to
     * determine whether the camera should be initialized from the stage
     * bounding box or whether an existing camera state should be preserved.
     *
     * @return True if the camera is in its default state.
     */
    bool isIdentity() const;

    /** @name Framing and Navigation */
    ///@{

    /**
     * @brief Frames the entire scene bounding box.
     */
    void frameAll();

    /**
     * @brief Resets the camera view to default orientation.
     */
    void resetView();

    /**
     * @brief Rotates the camera around the focus point.
     *
     * @param x Horizontal rotation delta.
     * @param y Vertical rotation delta.
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

    /**
     * @brief Returns the framing fit multiplier.
     *
     * The fit value is used by frameAll() to leave additional space
     * around the scene bounding box.
     */
    double fit() const;

    /**
     * @brief Sets the framing fit multiplier.
     *
     * @param fit Fit multiplier used by frameAll().
     */
    void setFit(double fit);

    ///@}

    /** @name Orientation */
    ///@{

    /**
     * @brief Returns the camera up-axis.
     */
    CameraUp cameraUp() const;

    /**
     * @brief Sets the camera up-axis.
     */
    void setCameraUp(ViewCamera::CameraUp cameraUp);

    /**
     * @brief Returns the camera yaw angle in degrees.
     */
    double axisYaw() const;

    /**
     * @brief Sets the camera yaw angle in degrees.
     *
     * This value is part of the orbit transform used to construct
     * the final USD camera transform.
     */
    void setAxisYaw(double yaw);

    /**
     * @brief Returns the camera pitch angle in degrees.
     */
    double axisPitch() const;

    /**
     * @brief Sets the camera pitch angle in degrees.
     *
     * This value is part of the orbit transform used to construct
     * the final USD camera transform.
     */
    void setAxisPitch(double pitch);

    /**
     * @brief Returns the camera roll/orbit angle in degrees.
     */
    double axisRoll() const;

    /**
     * @brief Sets the camera roll/orbit angle in degrees.
     *
     * This value is part of the orbit transform used to construct
     * the final USD camera transform.
     */
    void setAxisRoll(double roll);

    ///@}

    /** @name Interaction */
    ///@{

    /**
     * @brief Returns the current camera interaction mode.
     */
    CameraMode cameraMode() const;

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
    void setFarClipping(double far);

    ///@}

    /** @name Distance */
    ///@{

    /**
     * @brief Returns the camera distance from the focus point.
     */
    double cameraDistance() const;

    /**
     * @brief Sets the camera distance from the focus point.
     *
     * This distance is used when constructing the orbit camera
     * transform and is required to restore the exact view.
     */
    void setCameraDistance(double distance);

    ///@}

    /**
     * @brief Resets the camera to its default state.
     *
     * Restores default projection, clipping, orientation, focus, and
     * navigation parameters while preserving the current bounding box,
     * range, and up-axis.
     */
    void reset();

Q_SIGNALS:
    /**
     * @brief Emitted when the camera changes.
     *
     * @param camera Current USD camera representation.
     */
    void cameraChanged(const GfCamera& camera);

private:
    Q_DISABLE_COPY_MOVE(ViewCamera)
    QScopedPointer<ViewCameraPrivate> p;
};

}  // namespace stageviz
