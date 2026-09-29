// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#pragma once

#include "stageviz.h"
#include <pxr/usd/sdf/path.h>

namespace stageviz::paths::auxiliary {

/**
 * @brief Root for Stageviz viewport-only display geometry.
 *
 * Content below this path is presentation geometry such as the grid,
 * guides, ground helpers, and other viewport-only drawables.
 */
inline const pxr::SdfPath display { "/Display" };

/**
 * @brief Root for Stageviz-owned auxiliary materials.
 *
 * Content below this path provides built-in materials used by viewport
 * presentation, inspection modes, selection, and material overrides.
 */
inline const pxr::SdfPath materials { "/Materials" };

}  // namespace stageviz::paths::auxiliary
