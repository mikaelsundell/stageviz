// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#pragma once

#include "stageviz.h"
#include "viewportgizmo.h"
#include <QScopedPointer>
#include <QString>

namespace stageviz {

class ViewportHost;
class ViewportTransformPrivate;

/**
 * @brief Owns viewport transform gestures, snapping, and command integration.
 */
class ViewportTransform {
public:
    /**
     * @brief Uses the same manipulation modes as the handle presentation.
     */
    using Mode = ViewportGizmo::Mode;

    /**
     * @brief Uses the same reference-frame choices as the transform handle.
     */
    using ReferenceFrame = ViewportGizmo::ReferenceFrame;

    /**
     * @brief Constructs the transform tool for the host.
     */
    explicit ViewportTransform(ViewportHost& host);

    /**
     * @brief Destroys the transform tool.
     */
    ~ViewportTransform();

    /**
     * @brief Returns whether manipulation is enabled.
     */
    bool enabled() const;

    /**
     * @brief Returns whether a transform handle can be presented for the current selection.
     */
    bool hasActiveHandle();

    /**
     * @brief Returns the active manipulation mode.
     */
    Mode mode() const;

    /**
     * @brief Enables or disables manipulation.
     */
    void setEnabled(bool enabled);

    /**
     * @brief Enables or disables a manipulation mode.
     */
    void setMode(Mode mode, bool enabled);

    /**
     * @brief Returns the active coordinate frame.
     */
    ReferenceFrame referenceFrame() const;

    /**
     * @brief Sets the coordinate frame used by the transform handle.
     */
    void setReferenceFrame(ReferenceFrame frame);

    /**
     * @brief Applies a relative transform in the handle reference frame through an undoable command.
     *
     * Translation values are stage units, rotation values are degrees, and scale values are factors.
     */
    bool applyNumeric(const GfVec3d& values, QString& error);

    /**
     * @brief Starts a live numeric transform edit.
     */
    bool beginNumericEdit(QString& error);

    /**
     * @brief Updates a live numeric transform preview using absolute local values.
     *
     * Rotation angles use the USD rotateXYZ convention.
     *
     * @param values Absolute local translation in stage units, rotation in degrees, or scale.
     * @param error Receives a failure reason.
     */
    bool updateNumericEdit(const GfVec3d& values, QString& error);

    /**
     * @brief Commits a live numeric transform edit as one undoable command.
     */
    void finishNumericEdit();


    /**
     * @brief Returns whether the gizmo edits pivot frames instead of object transforms.
     */
    bool pivotEnabled() const;

    /**
     * @brief Switches the existing manipulation tools to pivot-frame editing.
     */
    void setPivotEnabled(bool enabled);

    /**
     * @brief Returns whether a drag is active.
     */
    bool dragging() const;

    /**
     * @brief Returns whether a drag is deferring session updates.
     */
    bool deferringUpdates() const;

    /**
     * @brief Enables or disables snapping for the active gesture.
     */
    void setSnap(bool enabled);

    /**
     * @brief Starts a drag when a handle is hit, optionally copying moved objects on release.
     */
    bool beginDrag(const QPointF& pos, bool copy = false);

    /**
     * @brief Updates the active transform gesture.
     */
    void updateDrag(const QPointF& pos);

    /**
     * @brief Finishes the active gesture as an undoable command.
     */
    void endDrag();

    /**
     * @brief Updates the highlighted handle.
     */
    void updateHover(const QPointF& pos);

    /**
     * @brief Invalidates the manipulation frame and handle geometry.
     */
    void invalidate();

    /**
     * @brief Clears hover state.
     */
    void clearHover();

    /**
     * @brief Updates the tool after selection changes.
     */
    void selectionChanged(bool empty);

    /**
     * @brief Updates handle geometry.
     */
    void updateGeometry();

    /**
     * @brief Draws the handle overlay.
     */
    void draw(QPainter& painter);

    /**
     * @brief Resets stage-dependent interaction state and restores deferred updates.
     */
    void close();

    /**
     * @brief Resets handle resources after an auxiliary stage replacement.
     */
    void auxiliaryChanged();

private:
    QScopedPointer<ViewportTransformPrivate> p;
};

}  // namespace stageviz
