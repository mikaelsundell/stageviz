// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#pragma once

#include "stageviz.h"
#include <Qt>

namespace stageviz::roles::shelf {

/**
 * @brief Item data role storing the user-visible script name.
 */
inline constexpr int scriptName = Qt::UserRole + 1;

/**
 * @brief Item data role storing a PNG-encoded custom script icon.
 */
inline constexpr int scriptIcon = Qt::UserRole + 2;

}  // namespace stageviz::roles::shelf
