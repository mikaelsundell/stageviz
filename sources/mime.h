// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#pragma once

namespace stageviz::mime {

/**
 * @brief Mime type for dragged or dropped Python shelf scripts.
 */
inline constexpr char script[] = "application/x-stageviz-python-script";

/**
 * @brief Mime type for dragged or dropped USD prim paths.
 */
inline constexpr char primPath[] = "application/x-stageviz-prim-path";

/**
 * @brief Object property storing the current drag/drop target item pointer.
 */
inline constexpr char dropItemPtrProperty[] = "_stageviz_drop_item_ptr";

/**
 * @brief Object property storing the active drag/drop mode.
 */
inline constexpr char dropModeProperty[] = "_stageviz_drop_mode";

}  // namespace stageviz::mime
