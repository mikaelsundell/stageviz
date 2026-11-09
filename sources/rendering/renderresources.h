// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 - present Mikael Sundell

#pragma once

#include <QString>

namespace stageviz::renderresources {

/**
 * @brief Finds a bundled rendering resource in the application or test resources directory.
 *
 * @param filename Resource filename.
 * @return Absolute file path, or an empty string if the resource is missing.
 */
QString
path(const QString& filename);

/**
 * @brief Returns the bundled color Scandinavia HDRI for dome lights and viewport lighting.
 *
 * @return Absolute texture path, or an empty string if the resource is missing.
 */
QString
defaultDomeTexture();

/**
 * @brief Returns the bundled monochrome Scandinavia HDRI for neutral material previews.
 *
 * @return Absolute texture path, or an empty string if the resource is missing.
 */
QString
neutralDomeTexture();

}  // namespace stageviz::renderresources
