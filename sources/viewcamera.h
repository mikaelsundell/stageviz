// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#pragma once

#include "stageviz.h"

#include <QObject>
#include <pxr/base/gf/bbox3d.h>
#include <pxr/base/gf/camera.h>
#include <pxr/base/gf/vec3d.h>

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
 * Internally the camera state can be converted to a USD-compatible
 * @c GfCamera for rendering.
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
     * @brief Returns whether the camera is in its default view state.
     *
     * A camera is considered to be in its identity state after construction
     * or resetView()/reset(), until it is otherwise manipulated.
     *
     * @return True if the camera is in its default view state.
     */
    bool isIdentity() const;

    /** @name Framing and Navigation */
    ///@{

    /**
     * @brief Frames the specified bounding box.
     *
     * Frames arbitrary bounds while preserving the current camera
     * orientation. The stored scene bounding box is not changed, so
     * frameAll() and resetView() continue to operate on the complete
     * scene bounds.
     *
     * @param bbox Bounding box to frame.
     */
    void frame(const GfBBox3d& bbox);

    /**
     * @brief Frames the entire stored scene bounding box.
     *
     * Preserves the current camera orientation and fits the stored
     * scene bounds in view.
     */
    void frameAll();

    /**
     * @brief Resets the camera to the default quarter view.
     *
     * Restores the default orbit orientation and frames the stored
     * scene bounds. Projection, clipping, fit, scene bounds, and
     * up-axis are preserved.
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
     * @brief Returns the stored scene bounding box.
     */
    GfBBox3d boundingBox() const;

    /**
     * @brief Sets the stored scene bounding box used by frameAll() and resetView().
     */
    void setBoundingBox(const GfBBox3d& boundingBox);

    /**
     * @brief Returns the framing fit multiplier.
     *
     * The fit value is used by frame(), frameAll(), and resetView()
     * to leave additional space around framed bounds.
     */
    double fit() const;

    /**
     * @brief Sets the framing fit multiplier.
     *
     * @param fit Fit multiplier used for framing.
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
     */
    void setAxisYaw(double yaw);

    /**
     * @brief Returns the camera pitch angle in degrees.
     */
    double axisPitch() const;

    /**
     * @brief Sets the camera pitch angle in degrees.
     */
    void setAxisPitch(double pitch);

    /**
     * @brief Returns the camera roll/orbit angle in degrees.
     */
    double axisRoll() const;

    /**
     * @brief Sets the camera roll/orbit angle in degrees.
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
     * @brief Compatibility alias for resetView().
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
