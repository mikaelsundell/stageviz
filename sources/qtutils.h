// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#pragma once

#include "stageviz.h"

#include <QOpenGLFunctions>
#include <pxr/base/gf/frustum.h>
#include <pxr/base/gf/matrix4d.h>
#include <pxr/base/gf/quaternion.h>
#include <pxr/base/gf/range1f.h>
#include <pxr/base/gf/range2d.h>
#include <pxr/base/gf/vec2i.h>
#include <pxr/imaging/cameraUtil/framing.h>
#include <pxr/usd/sdf/path.h>
#include <pxr/usd/usd/stage.h>
#include <pxr/usd/usd/timeCode.h>
#include <pxr/usd/usdGeom/camera.h>
#include <pxr/usdImaging/usdImagingGL/renderParams.h>

PXR_NAMESPACE_USING_DIRECTIVE

namespace stageviz {
namespace qt {

    /** @name Qt / Image Conversions */
    ///@{
    /**
     * @brief Returns the current application device pixel ratio.
     *
     * This helper returns the device pixel ratio from the active QApplication
     * when available, and falls back to 1.0 when no application instance exists.
     *
     * @return The current device pixel ratio, or 1.0 if unavailable.
     */
    qreal devicePixelRatio();

    /**
     * @brief Converts a logical UI size into a physical pixel size.
     *
     * Multiplies the supplied logical size by the given device pixel ratio and
     * rounds the result to an integer pixel size suitable for raster scaling.
     * If @p dpr is 0.0 or less, the current application device pixel ratio is used.
     *
     * @param logicalSize The logical size in device-independent pixels.
     * @param dpr Optional device pixel ratio override.
     * @return The corresponding physical pixel size, clamped to at least 1.
     */
    int physicalPixelSize(int logicalSize, qreal dpr = 0.0);

    /**
     * @brief Scales an image to a logical UI size with device-pixel-ratio support.
     *
     * The image is scaled to a square raster size derived from @p logicalSize and
     * the current application device pixel ratio, then tagged with that device
     * pixel ratio so it renders at the intended logical size in Qt views.
     *
     * @param image Source image to scale.
     * @param logicalSize Target logical size in device-independent pixels.
     * @param aspectMode Aspect ratio policy used during scaling.
     * @param transformMode Transformation quality used during scaling.
     * @return A scaled image with device pixel ratio set, or a null image on failure.
     */
    QImage scaledImage(const QImage& image, int logicalSize, Qt::AspectRatioMode aspectMode,
                       Qt::TransformationMode transformMode = Qt::SmoothTransformation);

    /**
     * @brief Scales a pixmap to a logical UI size with device-pixel-ratio support.
     *
     * The pixmap is scaled to a square raster size derived from @p logicalSize and
     * the current application device pixel ratio, then tagged with that device
     * pixel ratio so it renders at the intended logical size in Qt widgets.
     *
     * @param pixmap Source pixmap to scale.
     * @param logicalSize Target logical size in device-independent pixels.
     * @param aspectMode Aspect ratio policy used during scaling.
     * @param transformMode Transformation quality used during scaling.
     * @return A scaled pixmap with device pixel ratio set, or a null pixmap on failure.
     */
    QPixmap scaledPixmap(const QPixmap& pixmap, int logicalSize, Qt::AspectRatioMode aspectMode,
                         Qt::TransformationMode transformMode = Qt::SmoothTransformation);

    /**
     * @brief Encodes an image as PNG bytes.
     *
     * Returns an empty byte array if the input image is null or if the image
     * could not be written as PNG data.
     *
     * @param image The image to encode.
     * @return PNG-encoded image bytes.
     */
    QByteArray imageToPngBytes(const QImage& image);

    /**
     * @brief Decodes an image from PNG bytes.
     *
     * Returns a null image if the input byte array is empty or if the PNG data
     * could not be decoded.
     *
     * @param bytes The PNG-encoded image bytes.
     * @return The decoded image.
     */
    QImage pngBytesToImage(const QByteArray& bytes);

    /**
     * @brief Creates an icon from PNG-encoded image bytes.
     *
     * Returns a null icon if the input byte array is empty or if the PNG data
     * could not be decoded into an image.
     *
     * @param bytes The PNG-encoded image bytes.
     * @return An icon created from the decoded image.
     */
    QIcon pngBytesToIcon(const QByteArray& bytes);

    ///@}

    /** @name Qt / String and text Conversions */
    ///@{

    /**
     * @brief Normalizes all line ending variants to LF newlines.
     *
     * Converts CRLF, CR, Unicode line separator (U+2028), and Unicode paragraph
     * separator (U+2029) into the standard line feed character (`'\n'`).
     *
     * @param text The input text to normalize.
     * @return A copy of the text with normalized newlines.
     */
    QString normalizeNewlines(const QString& text);

    /**
     * @brief Returns the first non-empty trimmed line from a block of text.
     *
     * Splits the input text on line feeds, trims each line, and returns the first
     * line that is not empty after trimming. Returns an empty string if no such
     * line exists.
     *
     * @param text The input text to inspect.
     * @return The first non-empty trimmed line, or an empty string if none exists.
     */
    QString firstNonEmptyLine(const QString& text);

    /**
     * @brief Truncates text to a maximum length and appends an ellipsis if needed.
     *
     * The input text is trimmed before processing. If the trimmed text length
     * exceeds @p maxLength, the result is shortened to that length, trimmed again,
     * and suffixed with `...`. Returns an empty string if @p maxLength is zero or
     * negative.
     *
     * @param text The input text to shorten.
     * @param maxLength The maximum number of characters to keep before appending
     * an ellipsis.
     * @return The original trimmed text if it fits, otherwise an elided version.
     */
    QString elideText(const QString& text, int maxLength);

    /**
     * @brief Removes a prefix from text if it is present.
     *
     * Trims the input text, checks whether it starts with @p prefix, and if so
     * removes that prefix and trims the remainder. If the prefix is not present,
     * the trimmed input text is returned unchanged.
     *
     * @param text The input text to process.
     * @param prefix The prefix to remove when present.
     * @return The trimmed text with the prefix removed if matched.
     */
    QString stripPrefix(const QString& text, const QString& prefix);

    ///@}


    /** @name Qt / USD Conversions */
    ///@{

    /**
     * @brief Converts a QString to std::string.
     */
    std::string QStringToString(const QString& str);

    /**
     * @brief Converts a std::string to QString.
     */
    QString StringToQString(const std::string& str);

    /**
     * @brief Converts a QString to a USD TfToken.
     */
    TfToken QStringToTfToken(const QString& str);

    /**
     * @brief Converts a USD TfToken to QString.
     */
    QString TfTokenToQString(const TfToken& token);

    /**
     * @brief Converts a USD SdfPath to QString.
     */
    QString SdfPathToQString(const SdfPath& path);

    /**
     * @brief Converts a QList of USD SdfPath values to a formatted QString list.
     *
     * The result is formatted as:
     * @code
     * [/World, /World/Geom, /World/Geom/Cube]
     * @endcode
     */
    QString SdfPathListToQString(const QList<SdfPath>& paths);

    /**
     * @brief Converts a TfTokenVector to a QList of QString.
     */
    QList<QString> TfTokenVectorToQList(const TfTokenVector& tokens);

    /**
     * @brief Converts a QList of SdfPath to an SdfPathVector.
     */
    SdfPathVector QListToSdfPathVector(const QList<SdfPath>& paths);

    /**
     * @brief Converts a QColor to a USD GfVec4f.
     *
     * The resulting vector stores normalized RGBA components.
     */
    GfVec4f QColorToGfVec4f(const QColor& color);

    ///@}

}  // namespace qt
}  // namespace stageviz

PXR_NAMESPACE_OPEN_SCOPE

using namespace stageviz::qt;

/**
 * @brief Qt hash function for SdfPath.
 *
 * Allows SdfPath to be used as a key in Qt hash containers
 * such as QHash and QSet.
 */
inline size_t
qHash(const SdfPath& path, size_t seed = 0)
{
    return ::qHash(StringToQString(path.GetString()), seed);
}

PXR_NAMESPACE_CLOSE_SCOPE

/** @name Debug Utilities */
///@{

/**
 * @brief Checks for OpenGL errors and reports the source location.
 *
 * Intended to be used through the CHECK_GL_ERROR() macro.
 */
void
CheckOpenGLError(const char* function, const char* file, int line);

/**
 * @brief Convenience macro for OpenGL error checking.
 */
#define CHECK_GL_ERROR() CheckOpenGLError(__FUNCTION__, __FILE__, __LINE__)

/**
 * @brief Prints all prims in a USD stage for debugging.
 */
void
DebugStagePrims(const UsdStageRefPtr& stage);

/**
 * @brief Prints bounding boxes for prims in a USD stage.
 */
void
DebugBoundingBoxes(const UsdStageRefPtr& stage);

///@}

/** @name QDebug Stream Operators */
///@{

/**
 * @brief Debug stream operator for GfBBox3d.
 */
QDebug
operator<<(QDebug debug, const GfBBox3d& bbox);

/**
 * @brief Debug stream operator for GfRange1d.
 */
QDebug
operator<<(QDebug debug, const GfRange1d& range);

/**
 * @brief Debug stream operator for GfRange1f.
 */
QDebug
operator<<(QDebug debug, const GfRange1f& range);

/**
 * @brief Debug stream operator for GfRange2d.
 */
QDebug
operator<<(QDebug debug, const GfRange2d& range);

/**
 * @brief Debug stream operator for GfRotation.
 */
QDebug
operator<<(QDebug debug, const GfRotation& rotation);

/**
 * @brief Debug stream operator for GfVec2d.
 */
QDebug
operator<<(QDebug debug, const GfVec2d& vec);

/**
 * @brief Debug stream operator for GfVec2i.
 */
QDebug
operator<<(QDebug debug, const GfVec2i& vec);

/**
 * @brief Debug stream operator for GfVec3d.
 */
QDebug
operator<<(QDebug debug, const GfVec3d& vec);

/**
 * @brief Debug stream operator for GfVec4d.
 */
QDebug
operator<<(QDebug debug, const GfVec4d& vec);

/**
 * @brief Debug stream operator for GfVec4f.
 */
QDebug
operator<<(QDebug debug, const GfVec4f& vec);

/**
 * @brief Debug stream operator for GfMatrix4d.
 */
QDebug
operator<<(QDebug debug, const GfMatrix4d& matrix);

/**
 * @brief Debug stream operator for GfQuaternion.
 */
QDebug
operator<<(QDebug debug, const GfQuaternion& quat);

/**
 * @brief Debug stream operator for GfRange3d.
 */
QDebug
operator<<(QDebug debug, const GfRange3d& range);

/**
 * @brief Debug stream operator for GfCamera.
 */
QDebug
operator<<(QDebug debug, const GfCamera& camera);

/**
 * @brief Debug stream operator for GfFrustum.
 */
QDebug
operator<<(QDebug debug, const GfFrustum& frustum);

/**
 * @brief Debug stream operator for CameraUtilFraming.
 */
QDebug
operator<<(QDebug debug, const CameraUtilFraming& framing);

/**
 * @brief Debug stream operator for SdfPath.
 */
QDebug
operator<<(QDebug debug, const SdfPath& path);

/**
 * @brief Debug stream operator for TfToken.
 */
QDebug
operator<<(QDebug debug, const TfToken& token);

/**
 * @brief Debug stream operator for TfTokenVector.
 */
QDebug
operator<<(QDebug debug, const TfTokenVector& tokens);

/**
 * @brief Debug stream operator for UsdTimeCode.
 */
QDebug
operator<<(QDebug debug, const UsdTimeCode& timeCode);

/**
 * @brief Debug stream operator for VtValue.
 */
QDebug
operator<<(QDebug debug, const VtValue& value);

/**
 * @brief Debug stream operator for VtDictionary.
 */
QDebug
operator<<(QDebug debug, const VtDictionary& dict);

/**
 * @brief Debug stream operator for UsdImagingGLDrawMode.
 */
QDebug
operator<<(QDebug debug, UsdImagingGLDrawMode drawMode);

/**
 * @brief Debug stream operator for UsdImagingGLCullStyle.
 */
QDebug
operator<<(QDebug debug, UsdImagingGLCullStyle cullStyle);

/**
 * @brief Debug stream operator for UsdImagingGLRenderParams.
 */
QDebug
operator<<(QDebug debug, const UsdImagingGLRenderParams& params);

/**
 * @brief Debug stream operator for render bounding boxes.
 */
QDebug
operator<<(QDebug debug, const UsdImagingGLRenderParams::BBoxVector& bboxes);

/**
 * @brief Debug stream operator for clip planes.
 */
QDebug
operator<<(QDebug debug, const UsdImagingGLRenderParams::ClipPlanesVector& clipPlanes);

///@}
