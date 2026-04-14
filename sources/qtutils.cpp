// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#include "qtutils.h"
#include <QApplication>
#include <QBuffer>
#include <QColor>
#include <QIcon>
#include <QImage>
#include <QPixmap>
#include <pxr/usd/usd/prim.h>
#include <pxr/usd/usd/primRange.h>
#include <pxr/usd/usdGeom/bboxCache.h>
#include <pxr/usd/usdGeom/imageable.h>

namespace stageviz {
namespace qt {

    qreal devicePixelRatio() { return qApp ? qApp->devicePixelRatio() : 1.0; }

    int physicalPixelSize(int logicalSize, qreal dpr)
    {
        const qreal resolvedDpr = (dpr > 0.0) ? dpr : devicePixelRatio();
        return qMax(1, qRound(logicalSize * resolvedDpr));
    }

    QImage scaledImage(const QImage& image, int logicalSize, Qt::AspectRatioMode aspectMode,
                       Qt::TransformationMode transformMode)
    {
        if (image.isNull() || logicalSize <= 0)
            return QImage();

        const qreal dpr = devicePixelRatio();
        const int physicalSize = physicalPixelSize(logicalSize, dpr);

        QImage scaled = image.scaled(physicalSize, physicalSize, aspectMode, transformMode);
        scaled.setDevicePixelRatio(dpr);
        return scaled;
    }

    QPixmap scaledPixmap(const QPixmap& pixmap, int logicalSize, Qt::AspectRatioMode aspectMode,
                         Qt::TransformationMode transformMode)
    {
        if (pixmap.isNull() || logicalSize <= 0)
            return QPixmap();

        const qreal dpr = devicePixelRatio();
        const int physicalSize = physicalPixelSize(logicalSize, dpr);

        QPixmap scaled = pixmap.scaled(physicalSize, physicalSize, aspectMode, transformMode);
        scaled.setDevicePixelRatio(dpr);
        return scaled;
    }

    QByteArray imageToPngBytes(const QImage& image)
    {
        if (image.isNull())
            return QByteArray();

        QByteArray bytes;
        QBuffer buffer(&bytes);
        if (!buffer.open(QIODevice::WriteOnly))
            return QByteArray();

        image.save(&buffer, "PNG");
        return bytes;
    }

    QImage pngBytesToImage(const QByteArray& bytes)
    {
        if (bytes.isEmpty())
            return QImage();

        QImage image;
        image.loadFromData(bytes, "PNG");
        return image;
    }

    QString normalizeNewlines(const QString& text)
    {
        QString out = text;
        out.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
        out.replace(QChar('\r'), QChar('\n'));
        out.replace(QChar(0x2028), QChar('\n'));
        out.replace(QChar(0x2029), QChar('\n'));
        return out;
    }

    QString firstNonEmptyLine(const QString& text)
    {
        const QStringList lines = text.split('\n');
        for (QString line : lines) {
            line = line.trimmed();
            if (!line.isEmpty())
                return line;
        }
        return QString();
    }

    QString stripPrefix(const QString& text, const QString& prefix)
    {
        QString out = text.trimmed();
        if (out.startsWith(prefix))
            out = out.mid(prefix.size()).trimmed();
        return out;
    }

    QString elideText(const QString& text, int maxLength)
    {
        if (maxLength <= 0)
            return QString();
        QString out = text.trimmed();
        if (out.length() > maxLength)
            out = out.left(maxLength).trimmed() + "...";
        return out;
    }

    QIcon pngBytesToIcon(const QByteArray& bytes) { return QIcon(QPixmap::fromImage(pngBytesToImage(bytes))); }

    std::string QStringToString(const QString& str) { return str.toUtf8().constData(); }

    QString StringToQString(const std::string& str) { return QString::fromUtf8(str.c_str()); }

    TfToken QStringToTfToken(const QString& str) { return TfToken(QStringToString(str)); }

    QString TfTokenToQString(const TfToken& token) { return StringToQString(token.GetString()); }

    QString SdfPathToQString(const SdfPath& path) { return StringToQString(path.GetString()); }

    QString SdfPathListToQString(const QList<SdfPath>& paths)
    {
        QStringList values;
        values.reserve(paths.size());
        for (const SdfPath& path : paths)
            values.append(SdfPathToQString(path));
        return QString("[%1]").arg(values.join(", "));
    }

    QList<QString> TfTokenVectorToQList(const TfTokenVector& tokens)
    {
        QList<QString> list;
        list.reserve(tokens.size());
        for (const auto& token : tokens) {
            list.append(StringToQString(token.GetString()));
        }
        return list;
    }

    SdfPathVector QListToSdfPathVector(const QList<SdfPath>& paths)
    {
        SdfPathVector vec;
        vec.reserve(paths.size());
        for (const SdfPath& p : paths) {
            vec.push_back(p);
        }
        return vec;
    }

    GfVec4f QColorToGfVec4f(const QColor& color)
    {
        return GfVec4f(color.redF(), color.greenF(), color.blueF(), color.alphaF());
    }
}  // namespace qt
}  // namespace stageviz

void
CheckOpenGLError(const char* function, const char* file, int line)
{
    GLenum err;
    while ((err = glGetError()) != GL_NO_ERROR) {
        qWarning() << "OpenGL Error:" << err << " in " << function << " at " << file << ":" << line;
    }
}

void
DebugStagePrims(const UsdStageRefPtr& stage)
{
    if (!stage) {
        qDebug() << "no stage loaded";
        return;
    }
    qDebug() << "scene objects (traversed prims):";
    for (const auto& prim : UsdPrimRange(stage->GetPseudoRoot())) {
        qDebug() << "- " << prim.GetPath() << " (" << prim.GetTypeName() << ")";
    }
}

void
DebugBoundingBoxes(const UsdStageRefPtr& stage)
{
    if (!stage) {
        qDebug() << "no stage loaded";
        return;
    }
    UsdGeomBBoxCache bboxCache(UsdTimeCode::Default(), UsdGeomImageable::GetOrderedPurposeTokens());
    qDebug() << "bounding boxes:";
    for (const auto& prim : stage->Traverse()) {
        if (UsdGeomImageable(prim)) {
            GfBBox3d bbox = bboxCache.ComputeWorldBound(prim);
            GfRange3d range = bbox.ComputeAlignedBox();
            if (!range.IsEmpty()) {
                qDebug() << prim.GetPath() << " bounds: Min(" << range.GetMin() << ") Max(" << range.GetMax() << ")";
            }
            else {
                qDebug() << prim.GetPath() << " has no valid bounding box!";
            }
        }
    }
}

QDebug
operator<<(QDebug debug, const GfBBox3d& bbox)
{
    QDebugStateSaver saver(debug);
    const GfRange3d& range = bbox.GetRange();
    const GfVec3d& min = range.GetMin();
    const GfVec3d& max = range.GetMax();
    debug.nospace() << "GfBBox3d("
                    << "min: (" << min[0] << ", " << min[1] << ", " << min[2] << "), "
                    << "max: (" << max[0] << ", " << max[1] << ", " << max[2] << ")"
                    << ")";
    return debug;
}

QDebug
operator<<(QDebug debug, const GfRange1d& range)
{
    QDebugStateSaver saver(debug);
    debug.nospace() << "GfRange1d(Min: " << range.GetMin() << ", Max: " << range.GetMax() << ")";
    return debug;
}

QDebug
operator<<(QDebug debug, const GfRange1f& range)
{
    QDebugStateSaver saver(debug);
    debug.nospace() << "GfRange1f(Min: " << range.GetMin() << ", Max: " << range.GetMax() << ")";
    return debug;
}

QDebug
operator<<(QDebug debug, const GfRange2d& range)
{
    QDebugStateSaver saver(debug);
    debug.nospace() << "GfRange2d(Min: [" << range.GetMin()[0] << ", " << range.GetMin()[1] << "] Max: ["
                    << range.GetMax()[0] << ", " << range.GetMax()[1] << "])";
    return debug;
}

QDebug
operator<<(QDebug debug, const GfRotation& rotation)
{
    QDebugStateSaver saver(debug);
    debug.nospace() << "GfRotation("
                    << "axis = " << rotation.GetAxis() << ", "
                    << "angle = " << rotation.GetAngle() << ")";
    return debug;
}

QDebug
operator<<(QDebug debug, const GfVec2d& vec)
{
    QDebugStateSaver saver(debug);
    debug.nospace() << "GfVec2d(" << vec[0] << ", " << vec[1] << ")";
    return debug;
}

QDebug
operator<<(QDebug debug, const GfVec2i& vec)
{
    QDebugStateSaver saver(debug);
    debug.nospace() << "GfVec2i(" << vec[0] << ", " << vec[1] << ")";
    return debug;
}

QDebug
operator<<(QDebug debug, const GfVec3d& vec)
{
    QDebugStateSaver saver(debug);
    debug.nospace() << "GfVec3d(" << vec[0] << ", " << vec[1] << ", " << vec[2] << ")";
    return debug;
}

QDebug
operator<<(QDebug debug, const GfVec4d& vec)
{
    QDebugStateSaver saver(debug);
    debug.nospace() << "GfVec4d(" << vec[0] << ", " << vec[1] << ", " << vec[2] << ", " << vec[3] << ")";
    return debug;
}

QDebug
operator<<(QDebug debug, const GfVec4f& vec)
{
    QDebugStateSaver saver(debug);
    debug.nospace() << "GfVec4f(" << vec[0] << ", " << vec[1] << ", " << vec[2] << ", " << vec[3] << ")";
    return debug;
}

QDebug
operator<<(QDebug debug, const GfMatrix4d& matrix)
{
    QDebugStateSaver saver(debug);
    debug.nospace() << "GfMatrix4d(\n";
    for (int row = 0; row < 4; ++row) {
        debug.nospace() << "  [ ";
        for (int col = 0; col < 4; ++col) {
            debug.nospace() << matrix[row][col] << " ";
        }
        debug.nospace() << "]\n";
    }
    debug.nospace() << ")";
    return debug;
}

QDebug
operator<<(QDebug debug, const GfQuaternion& quat)
{
    QDebugStateSaver saver(debug);
    debug.nospace() << "GfQuaternion(" << quat.GetReal() << ", " << quat.GetImaginary()[0] << ", "
                    << quat.GetImaginary()[1] << ", " << quat.GetImaginary()[2] << ")";
    return debug;
}

QDebug
operator<<(QDebug debug, const GfRange3d& range)
{
    QDebugStateSaver saver(debug);
    debug.nospace() << "GfRange3d(\n"
                    << "  min=(" << range.GetMin()[0] << ", " << range.GetMin()[1] << ", " << range.GetMin()[2]
                    << "),\n"
                    << "  max=(" << range.GetMax()[0] << ", " << range.GetMax()[1] << ", " << range.GetMax()[2] << "))";
    return debug;
}

QDebug
operator<<(QDebug debug, const GfCamera& camera)
{
    QDebugStateSaver saver(debug);
    debug.nospace() << "GfCamera(\n";
    debug.nospace() << "  transform = " << camera.GetTransform() << ",\n";
    auto projection = camera.GetProjection();
    QString projType = (projection == GfCamera::Projection::Perspective) ? "Perspective" : "Orthographic";
    debug.nospace() << "  projection = " << projType << ",\n";
    debug.nospace() << "  horizontalAperture = " << camera.GetHorizontalAperture() << ",\n";
    debug.nospace() << "  verticalAperture = " << camera.GetVerticalAperture() << ",\n";
    debug.nospace() << "  focalLength = " << camera.GetFocalLength() << ",\n";
    debug.nospace() << "  clippingRange = " << camera.GetClippingRange() << ",\n";
    debug.nospace() << "  focusDistance = " << camera.GetFocusDistance() << "\n";
    debug.nospace() << ")";
    return debug;
}

QDebug
operator<<(QDebug debug, const GfFrustum& frustum)
{
    QDebugStateSaver saver(debug);
    debug.nospace() << "GfFrustum(\n";
    debug.nospace() << "  position = " << frustum.GetPosition() << ",\n";
    debug.nospace() << "  rotation = " << frustum.GetRotation() << ",\n";
    debug.nospace() << "  window = " << frustum.GetWindow() << ",\n";
    debug.nospace() << "  nearFar = " << frustum.GetNearFar() << ",\n";
    debug.nospace() << "  viewDistance = " << frustum.GetViewDistance() << ",\n";
    auto projectionType = frustum.GetProjectionType();
    QString projType = (projectionType == GfFrustum::ProjectionType::Perspective) ? "Perspective" : "Orthographic";
    debug.nospace() << "  projection = " << projType << ",\n";
    debug.nospace() << "  viewMatrix = " << frustum.ComputeViewMatrix() << ",\n";
    debug.nospace() << "  projectionMatrix = " << frustum.ComputeProjectionMatrix() << ",\n";
    auto corners = frustum.ComputeCorners();
    debug.nospace() << "  corners = [\n";
    for (const auto& corner : corners) {
        debug.nospace() << "    " << corner << ",\n";
    }
    debug.nospace() << "  ]\n";

    debug.nospace() << ")";
    return debug;
}

QDebug
operator<<(QDebug debug, const CameraUtilFraming& framing)
{
    QDebugStateSaver saver(debug);
    debug.nospace() << "CameraUtilFraming(\n"
                    << "  Display Window: " << framing.displayWindow << "\n"
                    << "  Data Window: [" << framing.dataWindow.GetMinX() << ", " << framing.dataWindow.GetMinY()
                    << ", " << framing.dataWindow.GetMaxX() << ", " << framing.dataWindow.GetMaxY() << "]\n"
                    << "  Pixel Aspect Ratio: " << framing.pixelAspectRatio << "\n)";
    return debug;
}

QDebug
operator<<(QDebug debug, const SdfPath& path)
{
    QDebugStateSaver saver(debug);
    debug.nospace() << "SdfPath(\"" << stageviz::qt::StringToQString(path.GetString()) << "\")";
    return debug;
}

QDebug
operator<<(QDebug debug, const TfToken& token)
{
    QDebugStateSaver saver(debug);
    debug.nospace() << "TfToken(\"" << token.GetString().c_str() << "\")";
    return debug;
}

QDebug
operator<<(QDebug debug, const TfTokenVector& tokens)
{
    QDebugStateSaver saver(debug);
    debug.nospace() << "TfTokenVector [";

    for (size_t i = 0; i < tokens.size(); ++i) {
        if (i > 0)
            debug << ", ";
        debug << "TfToken(\"" << tokens[i].GetString().c_str() << "\")";
    }
    debug << "]";
    return debug;
}

QDebug
operator<<(QDebug debug, const UsdTimeCode& timeCode)
{
    QDebugStateSaver saver(debug);
    debug.nospace() << "UsdTimeCode(" << timeCode.GetValue() << ")";
    return debug;
}

QDebug
operator<<(QDebug debug, const VtValue& value)
{
    QDebugStateSaver saver(debug);
    if (value.IsHolding<int>()) {
        debug << value.UncheckedGet<int>();
    }
    else if (value.IsHolding<unsigned long>()) {
        debug << value.UncheckedGet<unsigned long>();
    }
    else if (value.IsHolding<float>()) {
        debug << value.UncheckedGet<float>();
    }
    else if (value.IsHolding<double>()) {
        debug << value.UncheckedGet<double>();
    }
    else if (value.IsHolding<std::string>()) {
        debug << "\"" << StringToQString(value.UncheckedGet<std::string>()) << "\"";
    }
    else if (value.IsHolding<VtDictionary>()) {
        debug << value.UncheckedGet<VtDictionary>();
    }
    else if (value.IsArrayValued()) {
        debug << "[";
        VtValue array = value;
        bool first = true;
        if (array.IsHolding<VtArray<int>>()) {
            for (const auto& item : array.UncheckedGet<VtArray<int>>()) {
                if (!first)
                    debug << ", ";
                first = false;
                debug << item;
            }
        }
        if (array.IsHolding<VtArray<unsigned long>>()) {
            for (const auto& item : array.UncheckedGet<VtArray<unsigned long>>()) {
                if (!first)
                    debug << ", ";
                first = false;
                debug << item;
            }
        }
        else if (array.IsHolding<VtArray<float>>()) {
            for (const auto& item : array.UncheckedGet<VtArray<float>>()) {
                if (!first)
                    debug << ", ";
                first = false;
                debug << item;
            }
        }
        else if (array.IsHolding<VtArray<double>>()) {
            for (const auto& item : array.UncheckedGet<VtArray<double>>()) {
                if (!first)
                    debug << ", ";
                first = false;
                debug << item;
            }
        }
        else if (array.IsHolding<VtArray<std::string>>()) {
            for (const auto& item : array.UncheckedGet<VtArray<std::string>>()) {
                if (!first)
                    debug << ", ";
                first = false;
                debug << "\"" << StringToQString(item) << "\"";
            }
        }
        else {
            debug << "<unsupported array type: " << StringToQString(array.GetTypeName()) << ">";
        }
        debug << "]";
    }
    else {
        debug << "<unsupported type: " << StringToQString(value.GetTypeName()) << ">";
    }
    return debug;
}

QDebug
operator<<(QDebug debug, const VtDictionary& dict)
{
    QDebugStateSaver saver(debug);
    debug.nospace() << "VtDictionary {";
    bool first = true;
    for (const auto& pair : dict) {
        if (!first)
            debug << ", ";
        first = false;
        const std::string& key = pair.first;
        const VtValue& value = pair.second;
        debug << "\"" << StringToQString(key) << "\": ";
        debug << value;
    }

    debug << "}";
    return debug;
}

QDebug
operator<<(QDebug debug, UsdImagingGLDrawMode drawMode)
{
    QDebugStateSaver saver(debug);
    debug.nospace() << "UsdImagingGLDrawMode(";
    switch (drawMode) {
    case UsdImagingGLDrawMode::DRAW_POINTS: debug.nospace() << "DRAW_POINTS"; break;
    case UsdImagingGLDrawMode::DRAW_WIREFRAME: debug.nospace() << "DRAW_WIREFRAME"; break;
    case UsdImagingGLDrawMode::DRAW_WIREFRAME_ON_SURFACE: debug.nospace() << "DRAW_WIREFRAME_ON_SURFACE"; break;
    case UsdImagingGLDrawMode::DRAW_SHADED_FLAT: debug.nospace() << "DRAW_SHADED_FLAT"; break;
    case UsdImagingGLDrawMode::DRAW_SHADED_SMOOTH: debug.nospace() << "DRAW_SHADED_SMOOTH"; break;
    case UsdImagingGLDrawMode::DRAW_GEOM_ONLY: debug.nospace() << "DRAW_GEOM_ONLY"; break;
    case UsdImagingGLDrawMode::DRAW_GEOM_FLAT: debug.nospace() << "DRAW_GEOM_FLAT"; break;
    case UsdImagingGLDrawMode::DRAW_GEOM_SMOOTH: debug.nospace() << "DRAW_GEOM_SMOOTH"; break;
    default: debug.nospace() << "UNKNOWN"; break;
    }
    debug.nospace() << ")";
    return debug;
}

QDebug
operator<<(QDebug debug, UsdImagingGLCullStyle cullStyle)
{
    QDebugStateSaver saver(debug);
    debug.nospace() << "UsdImagingGLCullStyle(";
    switch (cullStyle) {
    case UsdImagingGLCullStyle::CULL_STYLE_NO_OPINION: debug.nospace() << "CULL_STYLE_NO_OPINION"; break;
    case UsdImagingGLCullStyle::CULL_STYLE_NOTHING: debug.nospace() << "CULL_STYLE_NOTHING"; break;
    case UsdImagingGLCullStyle::CULL_STYLE_BACK: debug.nospace() << "CULL_STYLE_BACK"; break;
    case UsdImagingGLCullStyle::CULL_STYLE_FRONT: debug.nospace() << "CULL_STYLE_FRONT"; break;
    case UsdImagingGLCullStyle::CULL_STYLE_BACK_UNLESS_DOUBLE_SIDED:
        debug.nospace() << "CULL_STYLE_BACK_UNLESS_DOUBLE_SIDED";
        break;
    case UsdImagingGLCullStyle::CULL_STYLE_COUNT: debug.nospace() << "CULL_STYLE_COUNT"; break;
    default: debug.nospace() << "UNKNOWN"; break;
    }
    debug.nospace() << ")";
    return debug;
}

QDebug
operator<<(QDebug debug, const UsdImagingGLRenderParams& params)
{
    QDebugStateSaver saver(debug);
    debug.nospace() << "UsdImagingGLRenderParams("
                    << "\n  frame: " << params.frame << "\n  complexity: " << params.complexity
                    << "\n  drawMode: " << params.drawMode << "\n  showGuides: " << params.showGuides
                    << "\n  showProxy: " << params.showProxy << "\n  showRender: " << params.showRender
                    << "\n  forceRefresh: " << params.forceRefresh << "\n  flipFrontFacing: " << params.flipFrontFacing
                    << "\n  cullStyle: " << params.cullStyle << "\n  enableLighting: " << params.enableLighting
                    << "\n  enableSampleAlphaToCoverage: " << params.enableSampleAlphaToCoverage
                    << "\n  applyRenderState: " << params.applyRenderState
                    << "\n  gammaCorrectColors: " << params.gammaCorrectColors << "\n  highlight: " << params.highlight
                    << "\n  overrideColor: " << params.overrideColor << "\n  wireframeColor: " << params.wireframeColor
                    << "\n  alphaThreshold: " << params.alphaThreshold << "\n  clipPlanes: " << params.clipPlanes
                    << "\n  enableSceneMaterials: " << params.enableSceneMaterials
                    << "\n  enableSceneLights: " << params.enableSceneLights
                    << "\n  enableUsdDrawModes: " << params.enableUsdDrawModes
                    << "\n  clearColor: " << params.clearColor
                    << "\n  colorCorrectionMode: " << params.colorCorrectionMode
                    << "\n  lut3dSizeOCIO: " << params.lut3dSizeOCIO << "\n  ocioDisplay: " << params.ocioDisplay
                    << "\n  ocioView: " << params.ocioView << "\n  ocioColorSpace: " << params.ocioColorSpace
                    << "\n  ocioLook: " << params.ocioLook << "\n  bboxes: " << params.bboxes
                    << "\n  bboxLineColor: " << params.bboxLineColor
                    << "\n  bboxLineDashSize: " << params.bboxLineDashSize << "\n)";
    return debug;
}

QDebug
operator<<(QDebug debug, const UsdImagingGLRenderParams::BBoxVector& bboxes)
{
    QDebugStateSaver saver(debug);
    debug.nospace() << "BBoxVector { ";

    for (const auto& bbox : bboxes) {
        const GfRange3d& range = bbox.GetRange();
        debug.nospace() << "[Min: (" << range.GetMin()[0] << ", " << range.GetMin()[1] << ", " << range.GetMin()[2]
                        << ") Max: (" << range.GetMax()[0] << ", " << range.GetMax()[1] << ", " << range.GetMax()[2]
                        << ")], ";
    }
    debug.nospace() << " }";
    return debug;
}

QDebug
operator<<(QDebug debug, const UsdImagingGLRenderParams::ClipPlanesVector& clipPlanes)
{
    QDebugStateSaver saver(debug);
    debug.nospace() << "ClipPlanesVector [";

    for (size_t i = 0; i < clipPlanes.size(); ++i) {
        const GfVec4d& plane = clipPlanes[i];
        debug.nospace() << " Plane" << i << " (" << plane[0] << ", " << plane[1] << ", " << plane[2] << ", " << plane[3]
                        << ")";

        if (i < clipPlanes.size() - 1) {
            debug.nospace() << ", ";
        }
    }

    debug.nospace() << " ]";
    return debug;
}
