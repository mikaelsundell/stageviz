// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#include "perfscope.h"
#include "viewporttransform.h"
#include "application.h"
#include "command.h"
#include "commandpivot.h"
#include "commandstack.h"
#include "commandtransform.h"
#include "primutils.h"
#include "usdutils.h"
#include "qtutils.h"
#include "session.h"
#include "tracelocks.h"
#include "ui.h"
#include "viewcamera.h"
#include "viewcontext.h"
#include "viewporthost.h"
#include "viewportwidget.h"
#include <QStringList>
#include <QVariant>
#include <algorithm>
#include <cmath>
#include <pxr/base/gf/math.h>
#include <pxr/base/gf/rotation.h>
#include <pxr/usd/usdGeom/imageable.h>
#include <pxr/usd/usdGeom/metrics.h>
#include <pxr/usd/usdGeom/xformable.h>

PXR_NAMESPACE_USING_DIRECTIVE

namespace stageviz {

class ViewportTransformPrivate {
public:
    using Mode = ViewportGizmo::Mode;
    static GfRotation rotationWithoutScale(const GfMatrix4d& matrix);
    static GfMatrix4d frameWithoutScale(const GfMatrix4d& matrix);
    static GfMatrix4d frameWithoutScale(const UsdPrim& prim);
    static GfVec3d eulerWithoutScale(const GfMatrix4d& matrix);
    static GfVec3d eulerNearestTo(const GfMatrix4d& matrix, const GfVec3d& reference);
    static bool applyIndividualRotation(const UsdStageRefPtr& stage, TransformOperation& operation,
                                        const QList<PreparedTransform>& prepared, const GfMatrix4d& rotation,
                                        QStringList* errors);
    bool selectionPivot(GfVec3d& pivot);
    GfMatrix4d absoluteNumericDelta(const GfVec3d& values) const;
    GfMatrix4d numericDelta(const GfVec3d& values, const GfMatrix4d& orientation, const GfVec3d& pivot) const;
    bool prepareFrame();
    int hitTest(const QPointF& pos);
    double worldPerPixel(const GfVec3d& pivot);
    GfVec3d axisVector(int axis);
    QPointF axisDirection(int axis);
    bool rotationAngle(int axis, const QPointF& pos, double& angle);

    void setEnabled(bool enabled);
    void setMode(Mode mode, bool enabled);
    bool beginDrag(const QPointF& pos, bool copy);
    void updateDrag(const QPointF& pos);
    void endDrag();
    void updateHover(const QPointF& pos);
    void updateInputValues();

    GfMatrix4d referenceOrientation(const QList<SdfPath>& targetPaths) const;
    QList<SdfPath> selectedVisiblePaths() const;
    QList<SdfPath> visibleTargetPaths() const;
    ViewportGizmo::Frame dragFrame() const;
    void publishFrameProperties();

    struct Data {
        ViewportHost* host = nullptr;
        QScopedPointer<ViewportGizmo> gizmo;
        ViewportGizmo::Frame frame;
        bool dragging = false;
        bool snap = false;
        bool copyDrag = false;
        QList<SdfPath> copySelection;
        QPointF startPosition;
        double rotationStartAngle = 0.0;
        GfVec3d startPivot = GfVec3d(0.0);
        double metersPerUnit = 1.0;
        TransformOperation operation;
        PivotOperation pivotOperation;
        bool numericEditing = false;
        TransformOperation numericOperation;
        QList<PreparedTransform> numericPrepared;
        GfVec3d numericPivot = GfVec3d(0.0);
        GfVec3d inputValues = GfVec3d(0.0);
        GfVec3d numericStartValues = GfVec3d(0.0);
        GfMatrix4d startOrientation = GfMatrix4d(1.0);
        QList<PreparedTransform> prepared;
        bool defersPrimsUpdate = false;
        Session::PrimsUpdate previousPrimsUpdate = Session::PrimsUpdate::Immediate;
    };
    Data d;
};

bool
ViewportTransformPrivate::applyIndividualRotation(const UsdStageRefPtr& stage, TransformOperation& operation,
                                                  const QList<PreparedTransform>& prepared, const GfMatrix4d& rotation,
                                                  QStringList* errors)
{
    if (!stage || operation.targetCount <= 0 || operation.targetCount > operation.paths.size()
        || operation.edit.before.size() != operation.paths.size() || prepared.size() != operation.paths.size()) {
        if (errors)
            errors->append(QStringLiteral("Invalid individual rotation operation"));
        return false;
    }

    QList<GfMatrix4d> after = operation.edit.before;
    for (qsizetype i = 0; i < operation.targetCount; ++i) {
        // Use the original cached world transform and local pivot so the
        // pivot stays fixed across every live preview update.
        const GfMatrix4d pivotFrame = prepared.at(i).localPivot * operation.edit.before.at(i);
        const GfVec3d pivot = pivotFrame.ExtractTranslation();
        GfMatrix4d toOrigin(1.0);
        GfMatrix4d fromOrigin(1.0);
        toOrigin.SetTranslate(-pivot);
        fromOrigin.SetTranslate(pivot);
        const GfMatrix4d delta = toOrigin * rotation * fromOrigin;
        after[i] = operation.edit.before.at(i) * delta;
    }

    operation.edit.after = after;
    operation.edit.delta = GfMatrix4d(1.0);
    operation.edit.hasDelta = false;
    return applyPreparedTransforms(stage, prepared, after, GfMatrix4d(1.0), false, errors);
}

ViewportGizmo::Frame
ViewportTransformPrivate::dragFrame() const
{
    ViewportGizmo::Frame frame = d.frame;
    if (d.dragging && d.frame.activeAxis >= 4 && d.frame.activeAxis <= 6) {
        // Object mode refreshes the displayed orientation from the
        // rotated prim during a live preview. Keep the interaction basis
        // fixed at mouse-down so that refresh cannot feed back into the
        // angle and axis calculation for the same drag.
        frame.orientation = d.startOrientation;
        frame.orientationValid = true;
    }
    return frame;
}

int
ViewportTransformPrivate::hitTest(const QPointF& pos)
{
    return prepareFrame() ? d.gizmo->hitTest(d.frame, pos) : 0;
}

double
ViewportTransformPrivate::worldPerPixel(const GfVec3d& pivot)
{
    return d.gizmo->worldPerPixel(pivot);
}

GfVec3d
ViewportTransformPrivate::axisVector(int axis)
{
    GfVec3d direction = ViewportGizmo::axisVector(axis);
    const ViewportGizmo::Frame frame = dragFrame();
    if (frame.orientationValid)
        direction = frame.orientation.TransformDir(direction);
    return direction.GetLengthSq() > 1e-12 ? direction.GetNormalized() : GfVec3d(0.0);
}

QPointF
ViewportTransformPrivate::axisDirection(int axis)
{
    return d.gizmo->axisDirection(d.frame, axis);
}

bool
ViewportTransformPrivate::rotationAngle(int axis, const QPointF& pos, double& angle)
{
    return d.gizmo->rotationAngle(dragFrame(), axis, pos, angle);
}

GfRotation
ViewportTransformPrivate::rotationWithoutScale(const GfMatrix4d& matrix)
{
    GfMatrix4d rotationMatrix(matrix);
    for (int row = 0; row < 3; ++row) {
        const double length = std::sqrt(rotationMatrix[row][0] * rotationMatrix[row][0]
                                        + rotationMatrix[row][1] * rotationMatrix[row][1]
                                        + rotationMatrix[row][2] * rotationMatrix[row][2]);
        if (length > 1e-12) {
            rotationMatrix[row][0] /= length;
            rotationMatrix[row][1] /= length;
            rotationMatrix[row][2] /= length;
        }
    }
    return rotationMatrix.ExtractRotation();
}

GfMatrix4d
ViewportTransformPrivate::frameWithoutScale(const GfMatrix4d& matrix)
{
    // Keep the complete orthonormal basis, including reflections. A GfRotation
    // cannot represent a left-handed transform, which is common in imported
    // CAD stages; extracting a quaternion from one can discard the object's
    // actual local axes and leave the scale gizmo world-aligned.
    GfMatrix4d frame = matrix.GetOrthonormalized(false);
    frame.SetTranslateOnly(GfVec3d(0.0));
    return frame;
}

GfMatrix4d
ViewportTransformPrivate::frameWithoutScale(const UsdPrim& prim)
{
    if (!prim)
        return GfMatrix4d(1.0);
    if (prim.GetAttribute(TfToken("xformOp:transform:pivot"))) {
        GfMatrix4d pivotFrame(1.0);
        QString error;
        if (stage::worldPivotFrame(prim.GetStage(), prim.GetPath(), pivotFrame, error))
            return frameWithoutScale(pivotFrame);
    }


    // Build the frame from authored rotation operations instead of
    // orthonormalizing the composed world matrix. Non-uniform scales between
    // rotations can introduce shear in that matrix, making its best-fit frame
    // differ from the prim's actual local axes.
    GfMatrix4d frame(1.0);
    for (UsdPrim current = prim; current && !current.IsPseudoRoot(); current = current.GetParent()) {
        const UsdGeomXformable xformable(current);
        bool resetsXformStack = false;
        const std::vector<UsdGeomXformOp> ops = xformable.GetOrderedXformOps(&resetsXformStack);
        GfMatrix4d localFrame(1.0);
        for (const UsdGeomXformOp& op : ops) {
            const UsdGeomXformOp::Type type = op.GetOpType();
            const bool isRotation = type >= UsdGeomXformOp::TypeRotateX && type <= UsdGeomXformOp::TypeOrient;
            if (isRotation) {
                localFrame = op.GetOpTransform(UsdTimeCode::Default()) * localFrame;
            }
            else if (type == UsdGeomXformOp::TypeTransform) {
                localFrame = frameWithoutScale(op.GetOpTransform(UsdTimeCode::Default())) * localFrame;
            }
        }

        frame *= localFrame;
        if (resetsXformStack)
            break;
    }

    frame.SetTranslateOnly(GfVec3d(0.0));
    return frame;
}

GfVec3d
ViewportTransformPrivate::eulerWithoutScale(const GfMatrix4d& matrix)
{
    // Gf decomposes in the reverse of USD's row-vector composition order.
    // Reverse both the requested axes and returned components for rotateXYZ.
    const GfVec3d zyx = rotationWithoutScale(matrix).Decompose(GfVec3d::ZAxis(), GfVec3d::YAxis(), GfVec3d::XAxis());
    return GfVec3d(zyx[2], zyx[1], zyx[0]);
}

GfVec3d
ViewportTransformPrivate::eulerNearestTo(const GfMatrix4d& matrix, const GfVec3d& reference)
{
    const GfVec3d primary = eulerWithoutScale(matrix);
    GfVec3d alternate(primary[0] + 180.0, 180.0 - primary[1], primary[2] + 180.0);
    GfVec3d nearestPrimary = primary;
    GfVec3d nearestAlternate = alternate;
    double primaryDistance = 0.0;
    double alternateDistance = 0.0;
    for (int axis = 0; axis < 3; ++axis) {
        nearestPrimary[axis] += 360.0 * std::round((reference[axis] - nearestPrimary[axis]) / 360.0);
        nearestAlternate[axis] += 360.0 * std::round((reference[axis] - nearestAlternate[axis]) / 360.0);
        primaryDistance += std::pow(nearestPrimary[axis] - reference[axis], 2.0);
        alternateDistance += std::pow(nearestAlternate[axis] - reference[axis], 2.0);
    }
    return alternateDistance < primaryDistance ? nearestAlternate : nearestPrimary;
}

GfMatrix4d
ViewportTransformPrivate::referenceOrientation(const QList<SdfPath>& targetPaths) const
{
    switch (d.frame.referenceFrame) {
    case ViewportGizmo::ReferenceFrame::World: return GfMatrix4d(1.0);
    case ViewportGizmo::ReferenceFrame::Camera: return frameWithoutScale(d.host->viewCamera()->camera().GetTransform());
    case ViewportGizmo::ReferenceFrame::Parent:
        if (targetPaths.size() == 1 && d.host->stage()) {
            const UsdPrim prim = d.host->stage()->GetPrimAtPath(targetPaths.first());
            const UsdPrim parent = prim ? prim.GetParent() : UsdPrim();
            GfMatrix4d parentWorld(1.0);
            QString error;
            if (parent && stage::worldTransform(d.host->stage(), parent.GetPath(), parentWorld, error))
                return frameWithoutScale(parentWorld);
        }
        return GfMatrix4d(1.0);
    case ViewportGizmo::ReferenceFrame::Object:
        if (targetPaths.size() == 1 && d.host->stage())
            return frameWithoutScale(d.host->stage()->GetPrimAtPath(targetPaths.first()));
        return GfMatrix4d(1.0);
    }
    return GfMatrix4d(1.0);
}

QList<SdfPath>
ViewportTransformPrivate::selectedVisiblePaths() const
{
    PerfScope perfScope("transform.visible_selection");
    QList<SdfPath> paths;
    if (!d.host->stage())
        return paths;

    paths.reserve(d.host->selectedPaths().size());
    path::PathCollector collector(paths);
    path::PathIndex maskRoots;
    for (const auto& mask : d.host->maskPaths())
        maskRoots.insert(mask.IsPropertyPath() ? mask.GetPrimPath() : mask);
    for (const SdfPath& selectedPath : d.host->selectedPaths()) {
        const SdfPath path = selectedPath.IsPropertyPath() ? selectedPath.GetPrimPath() : selectedPath;
        if (!d.host->maskPaths().isEmpty() && !maskRoots.covers(path))
            continue;

        const UsdPrim prim = d.host->stage()->GetPrimAtPath(path);
        if (!prim)
            continue;

        const UsdGeomImageable imageable(prim);
        if (imageable && imageable.ComputeVisibility() == UsdGeomTokens->invisible)
            continue;

        collector.append(path);
    }
    return paths;
}

QList<SdfPath>
ViewportTransformPrivate::visibleTargetPaths() const
{
    return stage::transformEditTargetPaths(d.host->stage(), selectedVisiblePaths());
}

bool
ViewportTransformPrivate::selectionPivot(GfVec3d& pivot)
{
    PerfScope perfScope("transform.selection_pivot");
    const QList<SdfPath> selectedPaths = selectedVisiblePaths();
    const QList<SdfPath> targetPaths = stage::transformEditTargetPaths(d.host->stage(), selectedPaths);
    d.frame.orientationValid = false;

    if (selectedPaths.isEmpty() || targetPaths.isEmpty())
        return false;

    if (d.frame.pivotEditing) {
        pivot = GfVec3d(0.0);
        for (const auto& path : targetPaths) {
            GfMatrix4d frame(1.0);
            QString error;
            if (!stage::worldPivotFrame(d.host->stage(), path, frame, error))
                return false;
            pivot += frame.ExtractTranslation();
            if (targetPaths.size() == 1)
                d.frame.orientation = frameWithoutScale(frame);
        }
        pivot /= static_cast<double>(targetPaths.size());
        d.frame.orientation = referenceOrientation(targetPaths);
        d.frame.orientationValid = true;
        return true;
    }

    // Use the most recently selected transform target as the lead object.
    // This keeps the gizmo anchored to the active object whose TRS is shown
    // in the panel, while transform operations still apply their delta to
    // every target.
    const SdfPath& path = targetPaths.last();
    GfMatrix4d frame(1.0);
    QString error;
    if (!stage::worldPivotFrame(d.host->stage(), path, frame, error))
        return false;
    pivot = frame.ExtractTranslation();
    d.frame.orientation = referenceOrientation({ path });
    d.frame.orientationValid = true;
    return true;
}

void
ViewportTransformPrivate::updateInputValues()
{
    GfVec3d values = d.frame.mode == Mode::Scale ? GfVec3d(1.0) : GfVec3d(0.0);
    if (d.host->stage() && d.host->context() && !d.frame.pivotEditing) {
        READ_LOCKER(locker, d.host->context()->stageLock(), "stageLock");
        const QList<SdfPath> targets = visibleTargetPaths();
        if (!targets.isEmpty() && d.host->stage()) {
            const UsdPrim prim = d.host->stage()->GetPrimAtPath(targets.last());
            const UsdGeomXformable xformable(prim);
            GfMatrix4d localTransform(1.0);
            bool resetsXformStack = false;
            if (xformable
                && xformable.GetLocalTransformation(&localTransform, &resetsXformStack, UsdTimeCode::Default())) {
                if (d.frame.mode == Mode::Move) {
                    values = localTransform.ExtractTranslation();
                }
                else if (d.frame.mode == Mode::Rotate) {
                    values = eulerNearestTo(localTransform, d.inputValues);
                }
                else if (d.frame.mode == Mode::Scale) {
                    GfMatrix4d rotation(1.0), shear(1.0), perspective(1.0);
                    GfVec3d translation(0.0);
                    localTransform.Factor(&rotation, &values, &shear, &translation, &perspective);
                }
            }
        }
    }
    d.inputValues = values;
    Q_EMIT d.host->widget()->transformInputValuesChanged(values);
}

void
ViewportTransformPrivate::setEnabled(bool enabled)
{
    if (!enabled) {
        setMode(Mode::None, true);
        return;
    }

    if (d.frame.mode == Mode::None)
        setMode(Mode::Move, true);
}

void
ViewportTransformPrivate::setMode(Mode mode, bool enabled)
{
    const Mode nextMode = enabled ? mode : (d.frame.mode == mode ? Mode::None : d.frame.mode);

    if (d.frame.mode == nextMode) {
        d.host->updateOverlay();
        d.host->widget()->update();
        return;
    }

    if (d.dragging)
        endDrag();

    d.gizmo->invalidate();
    d.frame.mode = nextMode;
    d.frame.enabled = d.frame.mode != Mode::None;
    d.snap = false;
    d.frame.hoverAxis = 0;
    d.frame.activeAxis = 0;
    updateInputValues();

    // The gizmo axes are Hydra geometry, not part of the QPainter overlay.
    // Repaint the GL viewport so paintGL() rebuilds them immediately when
    // entering, leaving, or switching transform modes.
    d.host->updateOverlay();
    d.host->widget()->update();
}

bool
ViewportTransformPrivate::beginDrag(const QPointF& pos, bool copy)
{
    PerfScope perfScope("transform.begin_drag");
    if (!d.frame.enabled)
        return false;
    const int handle = hitTest(pos);
    if (handle == 0 || !d.host->context() || !d.host->stage())
        return false;
    TransformOperation operation;
    d.operation = TransformOperation();
    GfVec3d pivot = d.frame.pivot;
    {
        READ_LOCKER(locker, d.host->context()->stageLock(), "stageLock");
        if (!d.host->stage())
            return false;

        if (!d.frame.pivotValid && !selectionPivot(pivot))
            return false;

        const SdfLayerHandle editLayer = d.host->stage()->GetEditTarget().GetLayer();
        if (!editLayer)
            return false;

        d.metersPerUnit = UsdGeomGetStageMetersPerUnit(d.host->stage());
        if (!std::isfinite(d.metersPerUnit) || d.metersPerUnit <= 0.0)
            d.metersPerUnit = 1.0;

        QString error;
        const bool captured = d.frame.pivotEditing
                                  ? beginPivotOperation(d.host->stage(), selectedVisiblePaths(), d.pivotOperation,
                                                        error)
                                  : beginTransformOperation(d.host->stage(), selectedVisiblePaths(), operation, error);
        if (!captured) {
            Q_EMIT d.host->widget()->statusMessage(error);
            return false;
        }
    }
    d.frame.pivot = pivot;
    d.frame.pivotValid = true;
    d.startPivot = d.frame.pivot;
    d.operation = d.frame.pivotEditing ? static_cast<TransformOperation>(d.pivotOperation) : std::move(operation);
    if (d.frame.pivotEditing) {
        d.operation.edit.before = d.pivotOperation.beforeFrames;
        d.operation.edit.after = d.operation.edit.before;
    }
    d.startOrientation = d.frame.orientation;
    d.startPosition = pos;
    d.frame.activeAxis = handle;
    d.frame.hoverAxis = d.frame.activeAxis;
    d.rotationStartAngle = 0.0;
    d.frame.rotationStartAngle = 0.0;
    d.frame.rotationDeltaAngle = 0.0;
    d.frame.rotationStartOrientationValid = false;
    if (handle >= 4 && handle <= 6) {
        double angle = 0.0;
        if (!rotationAngle(handle - 3, pos, angle))
            return false;
        d.rotationStartAngle = angle;
        d.frame.rotationStartAngle = angle;
        d.frame.rotationStartOrientation = d.startOrientation;
        d.frame.rotationStartOrientationValid = true;
    }

    d.previousPrimsUpdate = session()->primsUpdate();
    if (d.previousPrimsUpdate != Session::PrimsUpdate::Deferred) {
        session()->setPrimsUpdate(Session::PrimsUpdate::Deferred);
        d.defersPrimsUpdate = true;
    }
    else {
        d.defersPrimsUpdate = false;
    }

    // Normalize transform authoring once at drag start, then cache the matrix
    // ops used by the common case. This avoids re-running the generic
    // stage::setWorldTransform() setup for every selected prim on every
    // mouse-move event.
    d.prepared.clear();
    QStringList errors;
    {
        WRITE_LOCKER(locker, d.host->context()->stageLock(), "stageLock");
        if (!d.host->stage())
            return false;

        if (!d.frame.pivotEditing)
            d.prepared = prepareTransforms(d.host->stage(), d.operation.paths, d.operation.edit.before, &errors);
        if (!errors.isEmpty())
            restoreTransformOperation(d.host->stage(), d.operation, &errors);
    }
    if (!errors.isEmpty()) {
        if (d.defersPrimsUpdate) {
            session()->setPrimsUpdate(d.previousPrimsUpdate);
            d.defersPrimsUpdate = false;
        }
        d.operation = TransformOperation();
        d.prepared.clear();
        Q_EMIT d.host->widget()->statusMessage(errors.join('\n'));
        return false;
    }

    d.copyDrag = copy && !d.frame.pivotEditing && d.frame.mode == Mode::Move;
    d.copySelection = d.copyDrag ? selectedVisiblePaths() : QList<SdfPath>();
    if (d.copyDrag)
        d.snap = false;
    d.dragging = true;
    Q_EMIT d.host->widget()->transformInputValuesChanged(d.inputValues);
    d.gizmo->invalidate();
    d.host->widget()->update();
    return true;
}

void
ViewportTransformPrivate::updateDrag(const QPointF& pos)
{
    PerfScope perfScope("transform.update_drag");
    if (!d.dragging || d.frame.activeAxis == 0 || d.operation.edit.before.isEmpty())
        return;

    d.operation.edit.after = d.operation.edit.before;
    GfMatrix4d dragDelta(1.0);
    GfVec3d inputValues = d.frame.mode == Mode::Scale ? GfVec3d(1.0) : GfVec3d(0.0);

    auto snapValue = [](double value, double unit) { return std::round(value / unit) * unit; };


    auto matrixScale = [](const GfMatrix4d& matrix) {
        return GfVec3d(
            std::sqrt(matrix[0][0] * matrix[0][0] + matrix[0][1] * matrix[0][1] + matrix[0][2] * matrix[0][2]),
            std::sqrt(matrix[1][0] * matrix[1][0] + matrix[1][1] * matrix[1][1] + matrix[1][2] * matrix[1][2]),
            std::sqrt(matrix[2][0] * matrix[2][0] + matrix[2][1] * matrix[2][1] + matrix[2][2] * matrix[2][2]));
    };


    auto snappedRotationDelta = [&](const GfMatrix4d& inputDelta, int snapAxis) {
        if (!d.snap || d.operation.edit.before.isEmpty())
            return inputDelta;

        // First calculate the unsnapped absolute world orientation produced by
        // the drag.  Snapping is defined in that absolute orientation, not in
        // the incremental mouse-delta matrix.
        const GfMatrix4d& before = d.operation.edit.before.first();
        const GfMatrix4d candidate = before * inputDelta;
        GfVec3d angles = eulerWithoutScale(candidate);

        if (snapAxis >= 1 && snapAxis <= 3) {
            angles[snapAxis - 1] = snapValue(angles[snapAxis - 1], ui()->transformRotateSnap());
        }
        else {
            for (int axis = 0; axis < 3; ++axis)
                angles[axis] = snapValue(angles[axis], ui()->transformRotateSnap());
        }

        // Rebuild the exact desired snapped absolute orientation.
        GfMatrix4d rotateX(1.0);
        GfMatrix4d rotateY(1.0);
        GfMatrix4d rotateZ(1.0);
        rotateX.SetRotate(GfRotation(GfVec3d::XAxis(), angles[0]));
        rotateY.SetRotate(GfRotation(GfVec3d::YAxis(), angles[1]));
        rotateZ.SetRotate(GfRotation(GfVec3d::ZAxis(), angles[2]));
        // Use the same USD rotateXYZ convention as the numeric panel.
        const GfMatrix4d snappedRotation = rotateX * rotateY * rotateZ;

        // Build ONE rotation delta from the drag-start orientation directly to
        // the desired snapped orientation.  The previous implementation first
        // applied inputDelta and then appended a candidate-space correction;
        // with an already-rotated prim that correction was composed in the wrong
        // space and changed the other Euler components as well.
        GfMatrix4d beforeRotation(1.0);
        beforeRotation.SetRotate(rotationWithoutScale(before));
        const GfMatrix4d deltaRotation = beforeRotation.GetInverse() * snappedRotation;

        GfMatrix4d toOrigin(1.0);
        GfMatrix4d fromOrigin(1.0);
        toOrigin.SetTranslate(-d.startPivot);
        fromOrigin.SetTranslate(d.startPivot);
        const GfMatrix4d snappedDelta = toOrigin * deltaRotation * fromOrigin;


        return snappedDelta;
    };

    if (d.frame.activeAxis == 11) {
        // Free move is solved directly in screen space on a camera-facing plane
        // through the starting pivot. Using the projected camera right/up basis
        // keeps the gizmo center exactly under the mouse instead of relying on
        // an approximate world-units-per-pixel conversion.
        const GfCamera camera = d.host->viewCamera()->camera();
        const GfMatrix4d cameraTransform = camera.GetTransform();
        const GfVec3d right = cameraTransform.TransformDir(GfVec3d::XAxis()).GetNormalized();
        const GfVec3d up = cameraTransform.TransformDir(GfVec3d::YAxis()).GetNormalized();

        QPointF pivotScreen;
        if (!d.host->projectWorldToScreen(d.startPivot, pivotScreen))
            return;

        const double probeDistance = std::max(1e-6, worldPerPixel(d.startPivot) * 100.0);
        QPointF rightScreen;
        QPointF upScreen;
        if (!d.host->projectWorldToScreen(d.startPivot + right * probeDistance, rightScreen)
            || !d.host->projectWorldToScreen(d.startPivot + up * probeDistance, upScreen))
            return;

        const QPointF screenRight = rightScreen - pivotScreen;
        const QPointF screenUp = upScreen - pivotScreen;
        const double determinant = screenRight.x() * screenUp.y() - screenRight.y() * screenUp.x();
        if (std::abs(determinant) < 1e-8)
            return;

        // The entire center circle is one logical free-move handle.  Do not
        // make the world-space pivot jump to the exact pixel that was clicked
        // inside that circle.  Measure only the mouse movement since drag start
        // so grabbing the left/right/top/bottom side of the center handle gives
        // exactly the same translation behavior and zero delta at mouse-down.
        const QPointF screenDelta = pos - d.startPosition;
        const double rightAmount = (screenDelta.x() * screenUp.y() - screenDelta.y() * screenUp.x()) / determinant;
        const double upAmount = (screenRight.x() * screenDelta.y() - screenRight.y() * screenDelta.x()) / determinant;
        GfVec3d translation = right * (rightAmount * probeDistance) + up * (upAmount * probeDistance);

        if (d.snap) {
            GfVec3d target = d.startPivot + translation;
            for (int axis = 0; axis < 3; ++axis)
                target[axis] = snapValue(target[axis], ui()->transformMoveSnap());
            translation = target - d.startPivot;
        }

        dragDelta.SetTranslate(translation);
        inputValues = d.startOrientation.GetInverse().TransformDir(translation);

        d.frame.pivot = d.startPivot + translation;
    }
    else if (d.frame.activeAxis == 10) {
        const QPointF mouseDelta = pos - d.startPosition;
        const GfCamera camera = d.host->viewCamera()->camera();
        const GfMatrix4d cameraTransform = camera.GetTransform();
        const GfVec3d right = cameraTransform.TransformDir(GfVec3d::XAxis()).GetNormalized();
        const GfVec3d up = cameraTransform.TransformDir(GfVec3d::YAxis()).GetNormalized();
        constexpr double degreesPerPixel = 0.35;
        double horizontalAngle = mouseDelta.x() * degreesPerPixel;
        double verticalAngle = mouseDelta.y() * degreesPerPixel;

        // Keep the free gesture continuous here. Shift snapping is applied to
        // the resulting absolute XYZ orientation below, so an object that starts
        // on fractional angles is pulled onto whole-degree values as well.
        GfMatrix4d horizontalRotation(1.0);
        GfMatrix4d verticalRotation(1.0);
        horizontalRotation.SetRotate(GfRotation(up, horizontalAngle));
        verticalRotation.SetRotate(GfRotation(right, verticalAngle));
        const GfMatrix4d rotationMatrix = verticalRotation * horizontalRotation;

        GfMatrix4d toOrigin(1.0);
        GfMatrix4d fromOrigin(1.0);
        toOrigin.SetTranslate(-d.startPivot);
        fromOrigin.SetTranslate(d.startPivot);
        const GfMatrix4d delta = toOrigin * rotationMatrix * fromOrigin;


        dragDelta = snappedRotationDelta(delta, 0);

        GfMatrix4d dragRotation(1.0);
        dragRotation.SetRotate(rotationWithoutScale(dragDelta));
        const GfMatrix4d referenceRotation = d.startOrientation.GetInverse() * dragRotation * d.startOrientation;
        inputValues = eulerWithoutScale(referenceRotation);

        d.frame.pivot = d.startPivot;
    }
    else if (d.frame.activeAxis == 12) {
        const QPointF mouseDelta = pos - d.startPosition;
        const double rawFactor = std::clamp(std::exp((mouseDelta.x() - mouseDelta.y()) * 0.01), 0.01, 100.0);
        double factor = rawFactor;
        if (d.snap) {
            const GfVec3d startScale = matrixScale(d.operation.edit.before.first());
            const double minScale = std::min({ startScale[0], startScale[1], startScale[2] });
            const double maxScale = std::max({ startScale[0], startScale[1], startScale[2] });

            // If the starting scale is uniform, snap the resulting absolute scale
            // to whole units. For an intentionally non-uniform object there is no
            // single uniform factor that can make all three axes integral, so snap
            // the uniform factor itself to whole units instead.
            if (maxScale > 1e-8 && (maxScale - minScale) <= maxScale * 1e-6) {
                const double baseScale = (startScale[0] + startScale[1] + startScale[2]) / 3.0;
                const double targetScale = std::max(ui()->transformScaleSnap(),
                                                    snapValue(baseScale * factor, ui()->transformScaleSnap()));
                factor = targetScale / baseScale;
            }
            else {
                factor = std::max(ui()->transformScaleSnap(), snapValue(factor, ui()->transformScaleSnap()));
            }
            factor = std::clamp(factor, 0.01, 100.0);
        }

        GfMatrix4d toOrigin(1.0);
        GfMatrix4d scaleMatrix(1.0);
        GfMatrix4d fromOrigin(1.0);
        toOrigin.SetTranslate(-d.startPivot);
        scaleMatrix.SetScale(GfVec3d(factor));
        fromOrigin.SetTranslate(d.startPivot);
        const GfMatrix4d delta = toOrigin * scaleMatrix * fromOrigin;
        dragDelta = delta;
        inputValues = GfVec3d(factor);

        d.frame.pivot = d.startPivot;
    }
    else if (d.frame.activeAxis >= 1 && d.frame.activeAxis <= 3) {
        const GfVec3d axis = axisVector(d.frame.activeAxis);
        if (axis.GetLengthSq() < 1e-12)
            return;

        QPointF pivotScreen;
        QPointF axisScreen;
        if (!d.host->projectWorldToScreen(d.startPivot, pivotScreen))
            return;

        const double probeDistance = std::max(1e-6, worldPerPixel(d.startPivot) * 100.0);
        if (!d.host->projectWorldToScreen(d.startPivot + axis * probeDistance, axisScreen)) {
            return;
        }

        const QPointF projectedAxis = axisScreen - pivotScreen;
        const double projectedLength = std::hypot(projectedAxis.x(), projectedAxis.y());
        if (projectedLength < 1e-6)
            return;

        const QPointF screenAxis = projectedAxis / projectedLength;
        const double pixels = QPointF::dotProduct(pos - d.startPosition, screenAxis);
        const double worldPerScreenPixel = probeDistance / projectedLength;
        double distance = pixels * worldPerScreenPixel;
        if (d.snap) {
            const int axisIndex = d.frame.activeAxis - 1;
            const double target = d.startPivot[axisIndex] + distance;
            distance = snapValue(target, ui()->transformMoveSnap()) - d.startPivot[axisIndex];
        }
        const GfVec3d delta = axis * distance;
        dragDelta.SetTranslate(delta);
        inputValues[d.frame.activeAxis - 1] = distance;
        d.frame.pivot = d.startPivot + delta;
    }
    else if (d.frame.activeAxis >= 4 && d.frame.activeAxis <= 6) {
        const int axisIndex = d.frame.activeAxis - 3;
        double currentAngle = 0.0;
        if (!rotationAngle(axisIndex, pos, currentAngle)) {
            return;
        }
        double deltaAngle = currentAngle - d.rotationStartAngle;
        const double halfTurn = GfDegreesToRadians(180.0);
        const double fullTurn = GfDegreesToRadians(360.0);
        while (deltaAngle > halfTurn)
            deltaAngle -= fullTurn;

        while (deltaAngle < -halfTurn)
            deltaAngle += fullTurn;

        const double degrees = GfRadiansToDegrees(deltaAngle);

        const GfVec3d axis = axisVector(axisIndex);
        GfMatrix4d toOrigin(1.0);
        GfMatrix4d rotation(1.0);
        GfMatrix4d fromOrigin(1.0);
        toOrigin.SetTranslate(-d.startPivot);
        const GfVec3d basisA = axisVector(axisIndex % 3 + 1);
        const GfVec3d basisB = axisVector((axisIndex + 1) % 3 + 1);
        const double frameHandedness = GfDot(GfCross(axis, basisA), basisB) < 0.0 ? -1.0 : 1.0;
        rotation.SetRotate(GfRotation(axis, degrees * frameHandedness));
        fromOrigin.SetTranslate(d.startPivot);
        const GfMatrix4d delta = toOrigin * rotation * fromOrigin;
        dragDelta = snappedRotationDelta(delta, axisIndex);
        // Measure the applied rotation in the selected ring's plane. Use
        // the authored delta so the overlay also follows Shift snapping.
        const GfVec3d startDirection = basisA * std::cos(d.rotationStartAngle)
                                       + basisB * std::sin(d.rotationStartAngle);
        const GfVec3d direction = dragDelta.TransformDir(startDirection);
        double appliedAngle = std::atan2(GfDot(direction, basisB), GfDot(direction, basisA)) - d.rotationStartAngle;
        while (appliedAngle > halfTurn)
            appliedAngle -= fullTurn;
        while (appliedAngle < -halfTurn)
            appliedAngle += fullTurn;
        d.frame.rotationDeltaAngle = appliedAngle;
        const double displayedDelta = GfRadiansToDegrees(appliedAngle);
        inputValues[axisIndex - 1] = displayedDelta;
        d.frame.pivot = d.startPivot;
    }
    else if (d.frame.activeAxis >= 7 && d.frame.activeAxis <= 9) {
        const int axisIndex = d.frame.activeAxis - 6;
        const QPointF screenAxis = axisDirection(axisIndex);
        if (screenAxis.isNull())
            return;

        const double pixels = QPointF::dotProduct(pos - d.startPosition, screenAxis);
        const double rawFactor = std::clamp(std::exp(pixels * 0.01), 0.01, 100.0);
        double factor = rawFactor;
        if (d.snap) {
            const GfVec3d startScale = matrixScale(d.operation.edit.before.first());
            const double baseScale = startScale[axisIndex - 1];
            if (baseScale > 1e-8) {
                const double targetScale = std::max(ui()->transformScaleSnap(),
                                                    snapValue(baseScale * factor, ui()->transformScaleSnap()));
                factor = targetScale / baseScale;
            }
            else {
                factor = std::max(ui()->transformScaleSnap(), snapValue(factor, ui()->transformScaleSnap()));
            }
            factor = std::clamp(factor, 0.01, 100.0);
        }
        GfVec3d scale(1.0);
        scale[axisIndex - 1] = factor;
        GfMatrix4d toOrigin(1.0);
        GfMatrix4d scaleMatrix(1.0);
        GfMatrix4d fromOrigin(1.0);
        toOrigin.SetTranslate(-d.startPivot);
        scaleMatrix.SetScale(scale);
        fromOrigin.SetTranslate(d.startPivot);
        const GfMatrix4d orientedScale = d.startOrientation.GetInverse() * scaleMatrix * d.startOrientation;
        const GfMatrix4d delta = toOrigin * orientedScale * fromOrigin;
        dragDelta = delta;
        inputValues[axisIndex - 1] = factor;
        d.frame.pivot = d.startPivot;
    }
    {
        WRITE_LOCKER(locker, d.host->context()->stageLock(), "stageLock");
        if (!d.host->stage())
            return;

        QStringList errors;
        bool applied = false;
        if (d.frame.pivotEditing) {
            applied = applyPivotOperation(d.host->stage(), d.pivotOperation, dragDelta, &errors);
        }
        else if (d.frame.mode == Mode::Rotate) {
            GfMatrix4d rotation(1.0);
            rotation.SetRotate(rotationWithoutScale(dragDelta));
            applied = applyIndividualRotation(d.host->stage(), d.operation, d.prepared, rotation, &errors);
        }
        else {
            applied = applyTransformOperation(d.host->stage(), d.operation, d.prepared, dragDelta, &errors);
        }
        if (!applied) {
            Q_EMIT d.host->widget()->statusMessage(errors.join('\n'));
            return;
        }

        if (d.frame.pivotEditing) {
            d.operation.edit.after = d.pivotOperation.afterFrames;
            d.frame.pivot = dragDelta.Transform(d.startPivot);
            d.frame.orientation = referenceOrientation(d.operation.paths);
            d.frame.orientationValid = true;
        }
        else if (d.frame.mode == Mode::Rotate && d.frame.activeAxis >= 4 && d.frame.activeAxis <= 6
                 && d.frame.referenceFrame == ViewportGizmo::ReferenceFrame::Object) {
            // Build the live Object frame from the same drag-start orientation
            // and preview delta used to author the prim. This keeps the rings
            // tracking the object even when its composed transform hierarchy
            // cannot be reliably re-read during the live preview.
            GfMatrix4d previewRotation(1.0);
            previewRotation.SetRotate(rotationWithoutScale(dragDelta));
            d.frame.orientation = frameWithoutScale(d.startOrientation * previewRotation);
            d.frame.orientationValid = true;
        }
        else if (d.operation.targetCount == 1) {
            d.frame.orientation = referenceOrientation({ d.operation.paths.first() });
            d.frame.orientationValid = true;
        }
        else {
            d.frame.orientation = d.operation.paths.isEmpty() ? GfMatrix4d(1.0)
                                                              : referenceOrientation({ d.operation.paths.first() });
            d.frame.orientationValid = true;
        }

        // Keep the displayed gizmo center identical to the pivot produced by
        // the authored USD transform. A translated pivot derived only from the
        // mouse delta can differ slightly from stage::worldPivot() for prims
        // with existing pivot/xform-op structure, which otherwise makes the
        // gizmo jump when the drag is released and the pivot is recomputed.
        const bool translating = d.frame.activeAxis == 11 || (d.frame.activeAxis >= 1 && d.frame.activeAxis <= 3);
        if (!d.frame.pivotEditing && translating && d.operation.targetCount == 1) {
            GfVec3d authoredPivot(0.0);
            if (selectionPivot(authoredPivot))
                d.frame.pivot = authoredPivot;
        }
    }
    if (d.frame.pivotEditing)
        Q_EMIT d.host->widget()->transformInputValuesChanged(inputValues);
    else
        updateInputValues();

    d.host->widget()->update();
}

void
ViewportTransformPrivate::endDrag()
{
    PerfScope perfScope("transform.end_drag");
    if (!d.dragging)
        return;

    const TransformOperation operation = d.operation;
    const bool copy = d.copyDrag;
    const QList<SdfPath> copySelection = d.copySelection;
    d.copyDrag = false;
    d.copySelection.clear();
    const QList<SdfPath>& paths = operation.paths;
    const XformEdit& edit = operation.edit;
    d.dragging = false;
    d.snap = false;
    d.frame.activeAxis = 0;
    d.frame.hoverAxis = 0;
    d.frame.rotationStartOrientationValid = false;
    d.gizmo->invalidate();
    d.operation = TransformOperation();
    d.prepared.clear();

    bool changed = !paths.isEmpty() && paths.size() == edit.before.size() && edit.before.size() == edit.after.size();
    if (changed) {
        changed = false;
        for (qsizetype i = 0; i < edit.before.size(); ++i) {
            if (edit.before.at(i) != edit.after.at(i)) {
                changed = true;
                break;
            }
        }
    }

    bool createCopies = false;
    if (changed && copy) {
        QStringList errors;
        {
            WRITE_LOCKER(locker, d.host->context()->stageLock(), "stageLock");
            restoreTransformOperation(d.host->stage(), operation, &errors);
        }
        createCopies = errors.isEmpty();
        if (!errors.isEmpty()) {
            Q_EMIT d.host->widget()->statusMessage(errors.join('\n'));
        }
        d.frame.pivotValid = false;
        d.frame.orientationValid = false;
    }
    else if (changed) {
        if (CommandStack* stack = d.host->context() ? d.host->context()->commandStack() : nullptr)
            stack->execute(new Command(d.frame.pivotEditing ? setPivotTransforms(d.pivotOperation)
                                                            : setTransforms(operation)),
                           CommandStack::ExecutionMode::Applied);
    }
    else {
        WRITE_LOCKER(locker, d.host->context()->stageLock(), "stageLock");
        if (d.frame.pivotEditing)
            restorePivotOperation(d.host->stage(), d.pivotOperation);
        else
            restoreTransformOperation(d.host->stage(), operation);
    }
    d.pivotOperation = PivotOperation();
    if (d.defersPrimsUpdate) {
        session()->setPrimsUpdate(d.previousPrimsUpdate);
        d.defersPrimsUpdate = false;
    }

    Q_EMIT d.host->widget()->statusReady();
    if (createCopies) {
        if (CommandStack* stack = d.host->context()->commandStack())
            stack->execute(new Command(copyTransforms(copySelection, edit.delta)));
    }
    updateInputValues();
    d.host->widget()->update();
}

void
ViewportTransformPrivate::updateHover(const QPointF& pos)
{
    if (!d.frame.enabled) {
        if (d.frame.hoverAxis != 0) {
            d.frame.hoverAxis = 0;
            d.host->updateOverlay();
        }
        return;
    }
    const int handle = hitTest(pos);
    if (handle == d.frame.hoverAxis)
        return;

    d.frame.hoverAxis = handle;
    d.gizmo->invalidate();
    d.host->widget()->update();
    d.host->updateOverlay();
}

bool
ViewportTransformPrivate::prepareFrame()
{
    if (!d.frame.enabled || !d.host->context() || !d.host->stage())
        return false;
    if (!d.frame.pivotValid || !d.frame.orientationValid) {
        GfVec3d pivot(0.0);
        READ_LOCKER(locker, d.host->context()->stageLock(), "stageLock");
        if (!d.host->stage() || !selectionPivot(pivot))
            return false;
        d.frame.pivot = pivot;
        d.frame.pivotValid = true;
    }
    publishFrameProperties();
    return true;
}

void
ViewportTransformPrivate::publishFrameProperties()
{
    if (!qEnvironmentVariableIsSet("STAGEVIZ_PERF_LOG"))
        return;
    QPointF screen;
    const bool projected = d.frame.pivotValid && d.host->projectWorldToScreen(d.frame.pivot, screen);
    d.host->widget()->setProperty("transformPivotValid", projected);
    d.host->widget()->setProperty("transformPivotScreen", screen);
    d.host->widget()->setProperty("transformMode", static_cast<int>(d.frame.mode));
}

ViewportTransform::ViewportTransform(ViewportHost& host)
    : p(new ViewportTransformPrivate())
{
    p->d.host = &host;
    p->d.gizmo.reset(new ViewportGizmo(host));
}
ViewportTransform::~ViewportTransform() = default;
bool
ViewportTransform::enabled() const
{
    return p->d.frame.enabled;
}

bool
ViewportTransform::hasActiveHandle()
{
    if (!p->d.frame.enabled || p->d.frame.pivotEditing)
        return false;
    return p->prepareFrame();
}
ViewportTransform::Mode
ViewportTransform::mode() const
{
    return p->d.frame.mode;
}
void
ViewportTransform::setEnabled(bool enabled)
{
    finishNumericEdit();
    p->setEnabled(enabled);
}
void
ViewportTransform::setMode(Mode mode, bool enabled)
{
    finishNumericEdit();
    p->setMode(mode, enabled);
}

ViewportTransform::ReferenceFrame
ViewportTransform::referenceFrame() const
{
    return p->d.frame.referenceFrame;
}

void
ViewportTransform::setReferenceFrame(ReferenceFrame frame)
{
    if (p->d.frame.referenceFrame == frame)
        return;
    finishNumericEdit();
    if (p->d.dragging)
        p->endDrag();
    p->d.frame.referenceFrame = frame;
    p->d.frame.pivotValid = false;
    p->d.frame.orientationValid = false;
    p->d.gizmo->invalidate();
    p->d.host->widget()->update();
}

bool
ViewportTransform::applyNumeric(const GfVec3d& values, QString& error)
{
    if (!p->d.frame.enabled || p->d.frame.mode == Mode::None || p->d.frame.pivotEditing) {
        error = QStringLiteral("Numeric transforms require an active object transform tool");
        return false;
    }
    if (!p->prepareFrame() || !p->d.host->context() || !p->d.host->stage()) {
        error = QStringLiteral("No transformable selection");
        return false;
    }
    CommandStack* stack = p->d.host->context()->commandStack();
    if (!stack) {
        error = QStringLiteral("No command stack is available for numeric transforms");
        return false;
    }

    TransformOperation operation;
    QList<PreparedTransform> prepared;
    const GfMatrix4d orientation = p->d.frame.orientation;
    const GfVec3d pivot = p->d.frame.pivot;
    const GfMatrix4d delta = p->numericDelta(values, orientation, pivot);

    QStringList errors;
    {
        WRITE_LOCKER(locker, p->d.host->context()->stageLock(), "stageLock");
        if (!p->d.host->stage()
            || !beginTransformOperation(p->d.host->stage(), p->selectedVisiblePaths(), operation, error))
            return false;
        prepared = prepareTransforms(p->d.host->stage(), operation.paths, operation.edit.before, &errors);
        if (errors.isEmpty()) {
            bool applied = false;
            if (p->d.frame.mode == Mode::Rotate) {
                GfMatrix4d rotation(1.0);
                rotation.SetRotate(ViewportTransformPrivate::rotationWithoutScale(delta));
                applied = p->applyIndividualRotation(p->d.host->stage(), operation, prepared, rotation, &errors);
            }
            else {
                applied = applyTransformOperation(p->d.host->stage(), operation, prepared, delta, &errors);
            }
            if (!applied && errors.isEmpty())
                errors.append(QStringLiteral("Could not apply numeric transform"));
        }
        if (!errors.isEmpty())
            restoreTransformOperation(p->d.host->stage(), operation, &errors);
    }
    if (!errors.isEmpty()) {
        error = errors.join('\n');
        return false;
    }
    stack->execute(new Command(setTransforms(operation)), CommandStack::ExecutionMode::Applied);
    p->d.frame.pivotValid = false;
    p->d.frame.orientationValid = false;
    p->d.gizmo->invalidate();
    p->updateInputValues();
    p->d.host->widget()->update();
    return true;
}

GfMatrix4d
ViewportTransformPrivate::numericDelta(const GfVec3d& values, const GfMatrix4d& orientation, const GfVec3d& pivot) const
{
    GfMatrix4d delta(1.0);
    if (d.frame.mode == Mode::Move) {
        delta.SetTranslate(orientation.TransformDir(values));
    }
    else if (d.frame.mode == Mode::Rotate) {
        GfMatrix4d rotationX(1.0), rotationY(1.0), rotationZ(1.0);
        GfVec3d axes[] = { orientation.TransformDir(GfVec3d::XAxis()), orientation.TransformDir(GfVec3d::YAxis()),
                           orientation.TransformDir(GfVec3d::ZAxis()) };
        rotationX.SetRotate(GfRotation(axes[0], values[0]));
        rotationY.SetRotate(GfRotation(axes[1], values[1]));
        rotationZ.SetRotate(GfRotation(axes[2], values[2]));
        const GfMatrix4d rotation = rotationX * rotationY * rotationZ;
        GfMatrix4d toOrigin(1.0), fromOrigin(1.0);
        toOrigin.SetTranslate(-pivot);
        fromOrigin.SetTranslate(pivot);
        delta = toOrigin * rotation * fromOrigin;
    }
    else if (d.frame.mode == Mode::Scale) {
        GfMatrix4d scale(1.0), toOrigin(1.0), fromOrigin(1.0);
        scale.SetScale(values);
        const GfMatrix4d orientedScale = orientation.GetInverse() * scale * orientation;
        toOrigin.SetTranslate(-pivot);
        fromOrigin.SetTranslate(pivot);
        delta = toOrigin * orientedScale * fromOrigin;
    }
    return delta;
}

GfMatrix4d
ViewportTransformPrivate::absoluteNumericDelta(const GfVec3d& values) const
{
    // The panel displays absolute local TRS. Convert its correction through
    // the lead object's parent, independently of the handle reference frame.
    const int lead = d.numericOperation.targetCount - 1;
    GfMatrix4d parent = d.numericPrepared.at(lead).parentWorld;
    const GfMatrix4d local = d.numericOperation.edit.before.at(lead) * parent.GetInverse();
    parent.SetTranslateOnly(GfVec3d(0.0));
    GfMatrix4d correction(1.0);
    if (d.frame.mode == Mode::Move) {
        correction.SetTranslate(values - d.numericStartValues);
        return parent.GetInverse() * correction * parent;
    }
    GfMatrix4d beforeRotation(1.0);
    beforeRotation.SetRotate(rotationWithoutScale(local));
    if (d.frame.mode == Mode::Rotate) {
        GfMatrix4d x(1.0), y(1.0), z(1.0);
        x.SetRotate(GfRotation(GfVec3d::XAxis(), values[0]));
        y.SetRotate(GfRotation(GfVec3d::YAxis(), values[1]));
        z.SetRotate(GfRotation(GfVec3d::ZAxis(), values[2]));
        // Match UsdGeom rotateXYZ and the panel decomposition.
        correction = beforeRotation.GetInverse() * x * y * z;
    }
    else {
        GfVec3d ratios(1.0);
        for (int axis = 0; axis < 3; ++axis) {
            const double start = d.numericStartValues[axis];
            ratios[axis] = std::abs(start) > 1e-12 ? values[axis] / start : values[axis];
        }
        correction.SetScale(ratios);
        correction = beforeRotation.GetInverse() * correction * beforeRotation;
    }
    correction = parent.GetInverse() * correction * parent;
    GfMatrix4d toOrigin(1.0), fromOrigin(1.0);
    toOrigin.SetTranslate(-d.numericPivot);
    fromOrigin.SetTranslate(d.numericPivot);
    return toOrigin * correction * fromOrigin;
}

bool
ViewportTransform::beginNumericEdit(QString& error)
{
    if (p->d.numericEditing)
        return true;
    if (p->d.dragging || !p->d.frame.enabled || p->d.frame.mode == Mode::None || p->d.frame.pivotEditing) {
        error = QStringLiteral("Numeric transforms require an active object transform tool");
        return false;
    }
    if (!p->prepareFrame() || !p->d.host->context() || !p->d.host->stage()) {
        error = QStringLiteral("No transformable selection");
        return false;
    }
    if (!p->d.host->context()->commandStack()) {
        error = QStringLiteral("No command stack is available for numeric transforms");
        return false;
    }

    p->d.previousPrimsUpdate = session()->primsUpdate();
    if (p->d.previousPrimsUpdate != Session::PrimsUpdate::Deferred) {
        session()->setPrimsUpdate(Session::PrimsUpdate::Deferred);
        p->d.defersPrimsUpdate = true;
    }
    p->d.numericPivot = p->d.frame.pivot;
    p->d.numericStartValues = p->d.inputValues;
    QStringList errors;
    bool captured = false;
    {
        WRITE_LOCKER(locker, p->d.host->context()->stageLock(), "stageLock");
        captured = p->d.host->stage()
                   && beginTransformOperation(p->d.host->stage(), p->selectedVisiblePaths(), p->d.numericOperation,
                                              error);
        if (captured)
            p->d.numericPrepared = prepareTransforms(p->d.host->stage(), p->d.numericOperation.paths,
                                                     p->d.numericOperation.edit.before, &errors);
    }
    if (!captured) {
        if (p->d.defersPrimsUpdate) {
            session()->setPrimsUpdate(p->d.previousPrimsUpdate);
            p->d.defersPrimsUpdate = false;
        }
        if (error.isEmpty())
            error = QStringLiteral("No transformable selection");
        return false;
    }
    if (!errors.isEmpty()) {
        p->d.numericOperation = TransformOperation();
        p->d.numericPrepared.clear();
        if (p->d.defersPrimsUpdate) {
            session()->setPrimsUpdate(p->d.previousPrimsUpdate);
            p->d.defersPrimsUpdate = false;
        }
        error = errors.join('\n');
        return false;
    }
    p->d.numericEditing = true;
    return true;
}

bool
ViewportTransform::updateNumericEdit(const GfVec3d& values, QString& error)
{
    if (!p->d.numericEditing && !beginNumericEdit(error))
        return false;
    p->d.numericOperation.edit.after = p->d.numericOperation.edit.before;
    const GfMatrix4d delta = p->absoluteNumericDelta(values);
    QStringList errors;
    {
        WRITE_LOCKER(locker, p->d.host->context()->stageLock(), "stageLock");
        if (!p->d.host->stage()) {
            errors.append(QStringLiteral("The stage closed during a numeric transform"));
        }
        else if (p->d.frame.mode == Mode::Rotate
                     ? !p->applyIndividualRotation(p->d.host->stage(), p->d.numericOperation, p->d.numericPrepared,
                                                   GfMatrix4d(delta).SetTranslateOnly(GfVec3d(0.0)), &errors)
                     : !applyTransformOperation(p->d.host->stage(), p->d.numericOperation, p->d.numericPrepared, delta,
                                                &errors)) {
            restoreTransformOperation(p->d.host->stage(), p->d.numericOperation, &errors);
        }
    }
    if (!errors.isEmpty()) {
        error = errors.join('\n');
        return false;
    }
    // Retain the Euler branch explicitly entered by the user when the
    // committed matrix has more than one equivalent XYZ representation.
    p->d.inputValues = values;
    p->d.frame.pivotValid = false;
    p->d.frame.orientationValid = false;
    p->d.gizmo->invalidate();
    p->d.host->widget()->update();
    p->d.host->updateOverlay();
    return true;
}

void
ViewportTransform::finishNumericEdit()
{
    if (!p->d.numericEditing)
        return;
    const TransformOperation operation = p->d.numericOperation;
    p->d.numericEditing = false;
    p->d.numericOperation = TransformOperation();
    p->d.numericPrepared.clear();
    bool changed = false;
    for (qsizetype i = 0; i < operation.edit.before.size() && i < operation.edit.after.size(); ++i) {
        if (operation.edit.before.at(i) != operation.edit.after.at(i)) {
            changed = true;
            break;
        }
    }
    if (changed) {
        if (CommandStack* stack = p->d.host->context()->commandStack())
            stack->execute(new Command(setTransforms(operation)), CommandStack::ExecutionMode::Applied);
    }
    else {
        WRITE_LOCKER(locker, p->d.host->context()->stageLock(), "stageLock");
        restoreTransformOperation(p->d.host->stage(), operation);
    }
    if (p->d.defersPrimsUpdate) {
        session()->setPrimsUpdate(p->d.previousPrimsUpdate);
        p->d.defersPrimsUpdate = false;
    }
    p->d.frame.pivotValid = false;
    p->d.frame.orientationValid = false;
    p->d.gizmo->invalidate();
    p->updateInputValues();
    p->d.host->widget()->update();
}

bool
ViewportTransform::pivotEnabled() const
{
    return p->d.frame.pivotEditing;
}

void
ViewportTransform::setPivotEnabled(bool enabled)
{
    if (p->d.frame.pivotEditing == enabled)
        return;
    finishNumericEdit();
    if (p->d.dragging)
        p->endDrag();
    p->d.frame.pivotEditing = enabled;
    invalidate();
    p->d.host->widget()->update();
}

bool
ViewportTransform::dragging() const
{
    return p->d.dragging;
}
bool
ViewportTransform::deferringUpdates() const
{
    return p->d.defersPrimsUpdate;
}
void
ViewportTransform::setSnap(bool enabled)
{
    p->d.snap = enabled && !p->d.copyDrag;
}
bool
ViewportTransform::beginDrag(const QPointF& pos, bool copy)
{
    finishNumericEdit();
    const bool started = p->beginDrag(pos, copy);
    if (qEnvironmentVariableIsSet("STAGEVIZ_PERF_LOG")) {
        p->d.host->widget()->setProperty("transformDragging", p->d.dragging);
        p->d.host->widget()->setProperty("transformActiveAxis", p->d.frame.activeAxis);
        p->d.host->widget()->setProperty("transformTargetCount", p->d.operation.targetCount);
    }
    return started;
}
void
ViewportTransform::updateDrag(const QPointF& pos)
{
    p->updateDrag(pos);
}
void
ViewportTransform::endDrag()
{
    p->endDrag();
    if (qEnvironmentVariableIsSet("STAGEVIZ_PERF_LOG"))
        p->d.host->widget()->setProperty("transformDragging", p->d.dragging);
}
void
ViewportTransform::updateHover(const QPointF& pos)
{
    p->updateHover(pos);
}
void
ViewportTransform::invalidate()
{
    p->d.frame.pivotValid = false;
    p->d.frame.orientationValid = false;
    p->d.gizmo->invalidate();
    if (!p->d.dragging && !p->d.numericEditing)
        p->updateInputValues();
}
void
ViewportTransform::clearHover()
{
    p->d.frame.hoverAxis = 0;
    p->d.gizmo->invalidate();
}
void
ViewportTransform::selectionChanged(bool empty)
{
    if (dragging())
        return;
    finishNumericEdit();
    invalidate();
    if (empty) {
        p->d.frame.hoverAxis = 0;
        p->d.frame.activeAxis = 0;
        p->d.gizmo->clear();
    }
    p->updateInputValues();
}
void
ViewportTransform::updateGeometry()
{
    PerfScope perfScope("transform.update_geometry");
    if (!p->prepareFrame()) {
        if (qEnvironmentVariableIsSet("STAGEVIZ_PERF_LOG"))
            p->d.host->widget()->setProperty("transformPivotValid", false);
        p->d.gizmo->clear();
        return;
    }
    p->d.gizmo->update(p->d.frame);
}
void
ViewportTransform::draw(QPainter& painter)
{
    p->d.gizmo->draw(p->d.frame, painter);
}
void
ViewportTransform::close()
{
    if (p->d.defersPrimsUpdate) {
        session()->setPrimsUpdate(p->d.previousPrimsUpdate);
        p->d.defersPrimsUpdate = false;
    }
    const Mode mode = p->d.frame.mode;
    const bool pivotEditing = p->d.frame.pivotEditing;
    p->d.frame = ViewportGizmo::Frame();
    p->d.frame.pivotEditing = pivotEditing;
    p->d.frame.mode = mode;
    p->d.frame.enabled = mode != Mode::None;
    p->d.dragging = false;
    p->d.copyDrag = false;
    p->d.copySelection.clear();
    p->d.snap = false;
    p->d.rotationStartAngle = 0.0;
    p->d.startPivot = GfVec3d(0.0);
    p->d.metersPerUnit = 1.0;
    p->d.operation = TransformOperation();
    p->d.pivotOperation = PivotOperation();
    p->d.prepared.clear();
    p->d.gizmo->clear();
    invalidate();
}
void
ViewportTransform::auxiliaryChanged()
{
    p->d.gizmo->reset();
}

}  // namespace stageviz
