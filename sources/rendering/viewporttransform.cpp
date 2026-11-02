// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#include "viewporttransform.h"
#include "application.h"
#include "command.h"
#include "commandtransform.h"
#include "commandstack.h"
#include "primutils.h"
#include "qtutils.h"
#include "session.h"
#include "tracelocks.h"
#include "ui.h"
#include "viewcamera.h"
#include "viewcontext.h"
#include "viewporthost.h"
#include "viewportwidget.h"
#include <QStringList>
#include <algorithm>
#include <cmath>
#include <pxr/base/gf/math.h>
#include <pxr/base/gf/rotation.h>
#include <pxr/usd/usdGeom/bboxCache.h>
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
    QList<SdfPath> selectedVisiblePaths() const;
    QList<SdfPath> visibleTargetPaths() const;
    bool selectionPivot(GfVec3d& pivot);
    void setEnabled(bool enabled);
    void setMode(Mode mode, bool enabled);
    bool beginDrag(const QPointF& pos);
    void updateDrag(const QPointF& pos);
    void endDrag();
    void updateHover(const QPointF& pos);
    bool prepareFrame();
    int hitTest(const QPointF& pos) { return prepareFrame() ? d.gizmo->hitTest(d.frame, pos) : 0; }
    double worldPerPixel(const GfVec3d& pivot) { return d.gizmo->worldPerPixel(pivot); }
    GfVec3d axisVector(int axis) { return ViewportGizmo::axisVector(axis); }
    QPointF axisDirection(int axis) { return d.gizmo->axisDirection(d.frame, axis); }
    bool rotationAngle(int axis, const QPointF& pos, double& angle)
    {
        return d.gizmo->rotationAngle(d.frame, axis, pos, angle);
    }

    struct Data {
        ViewportHost* host = nullptr;
        QScopedPointer<ViewportGizmo> gizmo;
        ViewportGizmo::Frame frame;
        bool dragging = false;
        bool snap = false;
        QPointF startPosition;
        double rotationStartAngle = 0.0;
        GfVec3d startPivot = GfVec3d(0.0);
        double metersPerUnit = 1.0;
        TransformOperation operation;
        QList<PreparedTransform> prepared;
        bool defersPrimsUpdate = false;
        Session::PrimsUpdate previousPrimsUpdate = Session::PrimsUpdate::Immediate;
    };
    Data d;
};

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
                localFrame *= op.GetOpTransform(UsdTimeCode::Default());
            }
            else if (type == UsdGeomXformOp::TypeTransform) {
                localFrame *= frameWithoutScale(op.GetOpTransform(UsdTimeCode::Default()));
            }
        }

        frame = localFrame * frame;
        if (resetsXformStack)
            break;
    }

    frame.SetTranslateOnly(GfVec3d(0.0));
    return frame;
}

GfVec3d
ViewportTransformPrivate::eulerWithoutScale(const GfMatrix4d& matrix)
{
    return rotationWithoutScale(matrix).Decompose(GfVec3d::XAxis(), GfVec3d::YAxis(), GfVec3d::ZAxis());
}

QList<SdfPath>
ViewportTransformPrivate::selectedVisiblePaths() const
{
    QList<SdfPath> paths;
    if (!d.host->stage())
        return paths;

    paths.reserve(d.host->selectedPaths().size());
    for (const SdfPath& selectedPath : d.host->selectedPaths()) {
        const SdfPath path = selectedPath.IsPropertyPath() ? selectedPath.GetPrimPath() : selectedPath;
        if (!d.host->pathMaskedIn(path))
            continue;

        const UsdPrim prim = d.host->stage()->GetPrimAtPath(path);
        if (!prim)
            continue;

        const UsdGeomImageable imageable(prim);
        if (imageable && imageable.ComputeVisibility() == UsdGeomTokens->invisible)
            continue;

        if (!paths.contains(path))
            paths.append(path);
    }
    return paths;
}

QList<SdfPath>
ViewportTransformPrivate::visibleTargetPaths() const
{
    const QList<SdfPath> selectedPaths = selectedVisiblePaths();
    return stage::transformEditTargetPaths(d.host->stage(), selectedPaths);
}

bool
ViewportTransformPrivate::selectionPivot(GfVec3d& pivot)
{
    const QList<SdfPath> selectedPaths = selectedVisiblePaths();
    const QList<SdfPath> targetPaths = visibleTargetPaths();
    d.frame.orientationValid = false;

    if (selectedPaths.isEmpty() || targetPaths.isEmpty())
        return false;

    if (selectedPaths.size() == 1 && selectedPaths.first() == targetPaths.first()) {
        const SdfPath& path = targetPaths.first();

        GfMatrix4d matrix(1.0);
        QString error;
        if (!stage::worldTransform(d.host->stage(), path, matrix, error))
            return false;

        pivot = matrix.ExtractTranslation();
        d.frame.orientation = frameWithoutScale(d.host->stage()->GetPrimAtPath(path));
        d.frame.orientationValid = true;

        QString pivotError;
        stage::worldPivot(d.host->stage(), path, pivot, pivotError);
        return true;
    }

    // The session's shared bounds cache is cleared lazily when the complete
    // stage bound is requested. A transform-only notice can invalidate this
    // pivot before that happens, so compute the selection frame from fresh
    // composed bounds rather than reusing a previous gesture's cached values.
    UsdGeomBBoxCache bboxCache(UsdTimeCode::Default(), UsdGeomImageable::GetOrderedPurposeTokens(), true);

    GfRange3d range;
    bool hasBounds = false;

    for (const SdfPath& path : stage::objectPaths(d.host->stage(), selectedPaths)) {
        const UsdPrim prim = d.host->stage()->GetPrimAtPath(path);
        if (!prim)
            continue;

        const GfBBox3d bbox = bboxCache.ComputeWorldBound(prim);
        const GfRange3d aligned = bbox.ComputeAlignedRange();
        if (aligned.IsEmpty())
            continue;

        if (!hasBounds) {
            range = aligned;
            hasBounds = true;
        }
        else {
            range.UnionWith(aligned);
        }
    }

    if (!hasBounds)
        return false;

    pivot = range.GetMidpoint();
    // A proxy child can be manipulated through its owning instance. Keep the
    // gizmo aligned to that instance when it is the sole transform target;
    // otherwise a multi-selection uses the world-space group frame.
    if (targetPaths.size() == 1) {
        GfMatrix4d matrix(1.0);
        QString error;
        if (stage::worldTransform(d.host->stage(), targetPaths.first(), matrix, error)) {
            d.frame.orientation = frameWithoutScale(d.host->stage()->GetPrimAtPath(targetPaths.first()));
            d.frame.orientationValid = true;
        }
    }
    if (!d.frame.orientationValid) {
        d.frame.orientation = GfMatrix4d(1.0);
        d.frame.orientationValid = true;
    }
    return true;
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

    // The gizmo axes are Hydra geometry, not part of the QPainter overlay.
    // Repaint the GL viewport so paintGL() rebuilds them immediately when
    // entering, leaving, or switching transform modes.
    d.host->updateOverlay();
    d.host->widget()->update();
}

bool
ViewportTransformPrivate::beginDrag(const QPointF& pos)
{
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
        if (!beginTransformOperation(d.host->stage(), selectedVisiblePaths(), operation, error)) {
            Q_EMIT d.host->widget()->statusMessage(error);
            return false;
        }
    }
    d.frame.pivot = pivot;
    d.frame.pivotValid = true;
    d.startPivot = d.frame.pivot;
    d.operation = std::move(operation);
    d.startPosition = pos;
    d.frame.activeAxis = handle;
    d.frame.hoverAxis = d.frame.activeAxis;
    d.rotationStartAngle = 0.0;
    if (handle >= 4 && handle <= 6) {
        double angle = 0.0;
        if (!rotationAngle(handle - 3, pos, angle))
            return false;
        d.rotationStartAngle = angle;
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

    d.dragging = true;
    d.gizmo->invalidate();
    d.host->widget()->update();
    return true;
}

void
ViewportTransformPrivate::updateDrag(const QPointF& pos)
{
    if (!d.dragging || d.frame.activeAxis == 0 || d.operation.edit.before.isEmpty())
        return;

    d.operation.edit.after = d.operation.edit.before;
    GfMatrix4d dragDelta(1.0);
    QString message;

    auto axisName = [](int axis) -> QString {
        switch (axis) {
        case 1: return QStringLiteral("X");
        case 2: return QStringLiteral("Y");
        case 3: return QStringLiteral("Z");
        default: return QStringLiteral("?");
        }
    };

    auto signedNumber = [](double value, int precision) {
        return QStringLiteral("%1%2").arg(value >= 0.0 ? QStringLiteral("+") : QString()).arg(value, 0, 'f', precision);
    };

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
        const GfRotation candidateRotation = rotationWithoutScale(candidate);
        GfVec3d angles = candidateRotation.Decompose(GfVec3d::XAxis(), GfVec3d::YAxis(), GfVec3d::ZAxis());

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
        // GfRotation::Decompose(X, Y, Z) decomposes the matrix using the
        // opposite matrix-composition order from UsdGeom rotateXYZ.  To rebuild
        // the exact orientation represented by those decomposed angles we must
        // compose Z * Y * X here.  Using X * Y * Z only appears correct when
        // one/two components are near zero; with free rotation or an already
        // compound orientation it produces a completely different rotation.
        const GfMatrix4d snappedRotation = rotateZ * rotateY * rotateX;

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
        const GfVec3d translationMm = translation * (d.metersPerUnit * 1000.0);
        message = QStringLiteral("Translate  X %1 mm  Y %2 mm  Z %3 mm")
                      .arg(signedNumber(translationMm[0], 3), signedNumber(translationMm[1], 3),
                           signedNumber(translationMm[2], 3));

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

        const GfMatrix4d snappedCandidate = d.operation.edit.before.first() * dragDelta;
        const GfVec3d xyz = eulerWithoutScale(snappedCandidate);

        message = QStringLiteral("Rotate  X %1°  Y %2°  Z %3°")
                      .arg(signedNumber(xyz[0], 2), signedNumber(xyz[1], 2), signedNumber(xyz[2], 2));

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
        message = QStringLiteral("Scale XYZ  %1x").arg(factor, 0, 'f', 3);

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
        const double distanceMm = distance * d.metersPerUnit * 1000.0;
        message = QStringLiteral("Translate %1  %2 mm").arg(axisName(d.frame.activeAxis), signedNumber(distanceMm, 3));
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
        rotation.SetRotate(GfRotation(axis, degrees));
        fromOrigin.SetTranslate(d.startPivot);
        const GfMatrix4d delta = toOrigin * rotation * fromOrigin;
        dragDelta = snappedRotationDelta(delta, axisIndex);
        const GfMatrix4d snappedCandidate = d.operation.edit.before.first() * dragDelta;
        const GfVec3d snappedAngles = eulerWithoutScale(snappedCandidate);
        const GfVec3d beforeAngles = eulerWithoutScale(d.operation.edit.before.first());
        double displayedDelta = snappedAngles[axisIndex - 1] - beforeAngles[axisIndex - 1];
        while (displayedDelta > 180.0)
            displayedDelta -= 360.0;
        while (displayedDelta < -180.0)
            displayedDelta += 360.0;
        message = QStringLiteral("Rotate %1  %2°").arg(axisName(axisIndex), signedNumber(displayedDelta, 2));
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
        const GfMatrix4d orientedScale = d.frame.orientation.GetInverse() * scaleMatrix * d.frame.orientation;
        const GfMatrix4d delta = toOrigin * orientedScale * fromOrigin;
        dragDelta = delta;
        message = QStringLiteral("Scale %1  %2x").arg(axisName(axisIndex)).arg(factor, 0, 'f', 3);
        d.frame.pivot = d.startPivot;
    }
    {
        WRITE_LOCKER(locker, d.host->context()->stageLock(), "stageLock");
        if (!d.host->stage())
            return;

        QStringList errors;
        if (!applyTransformOperation(d.host->stage(), d.operation, d.prepared, dragDelta, &errors)) {
            Q_EMIT d.host->widget()->statusMessage(errors.join('\n'));
            return;
        }

        if (d.operation.targetCount == 1) {
            d.frame.orientation = frameWithoutScale(d.host->stage()->GetPrimAtPath(d.operation.paths.first()));
            d.frame.orientationValid = true;
        }
        else {
            d.frame.orientation = GfMatrix4d(1.0);
            d.frame.orientationValid = true;
        }

        // Keep the displayed gizmo center identical to the pivot produced by
        // the authored USD transform. A translated pivot derived only from the
        // mouse delta can differ slightly from stage::worldPivot() for prims
        // with existing pivot/xform-op structure, which otherwise makes the
        // gizmo jump when the drag is released and the pivot is recomputed.
        const bool translating = d.frame.activeAxis == 11 || (d.frame.activeAxis >= 1 && d.frame.activeAxis <= 3);
        if (translating && d.operation.targetCount == 1) {
            GfVec3d authoredPivot(0.0);
            if (selectionPivot(authoredPivot))
                d.frame.pivot = authoredPivot;
        }
    }
    if (!message.isEmpty())
        Q_EMIT d.host->widget()->statusMessage(message);

    d.host->widget()->update();
}

void
ViewportTransformPrivate::endDrag()
{
    if (!d.dragging)
        return;

    const TransformOperation operation = d.operation;
    const QList<SdfPath>& paths = operation.paths;
    const XformEdit& edit = operation.edit;
    d.dragging = false;
    d.snap = false;
    d.frame.activeAxis = 0;
    d.frame.hoverAxis = 0;
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

    if (changed) {
        if (CommandStack* stack = d.host->context() ? d.host->context()->commandStack() : nullptr)
            stack->execute(new Command(setTransforms(operation)), CommandStack::ExecutionMode::Applied);
    }
    else {
        WRITE_LOCKER(locker, d.host->context()->stageLock(), "stageLock");
        restoreTransformOperation(d.host->stage(), operation);
    }
    if (d.defersPrimsUpdate) {
        session()->setPrimsUpdate(d.previousPrimsUpdate);
        d.defersPrimsUpdate = false;
    }

    Q_EMIT d.host->widget()->statusReady();
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
    return true;
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
ViewportTransform::Mode
ViewportTransform::mode() const
{
    return p->d.frame.mode;
}
void
ViewportTransform::setEnabled(bool enabled)
{
    p->setEnabled(enabled);
}
void
ViewportTransform::setMode(Mode mode, bool enabled)
{
    p->setMode(mode, enabled);
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
    p->d.snap = enabled;
}
bool
ViewportTransform::beginDrag(const QPointF& pos)
{
    return p->beginDrag(pos);
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
    invalidate();
    if (empty) {
        p->d.frame.hoverAxis = 0;
        p->d.frame.activeAxis = 0;
        p->d.gizmo->clear();
    }
}
void
ViewportTransform::updateGeometry()
{
    if (!p->prepareFrame()) {
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
    p->d.frame = ViewportGizmo::Frame();
    p->d.frame.mode = mode;
    p->d.frame.enabled = mode != Mode::None;
    p->d.dragging = false;
    p->d.snap = false;
    p->d.rotationStartAngle = 0.0;
    p->d.startPivot = GfVec3d(0.0);
    p->d.metersPerUnit = 1.0;
    p->d.operation = TransformOperation();
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
