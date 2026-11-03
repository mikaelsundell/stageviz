// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#pragma once

#include "command.h"
#include "session.h"
#include <pxr/usd/sdf/path.h>

PXR_NAMESPACE_USING_DIRECTIVE

namespace stageviz {

/**
 * @brief Creates a command that sets the stage up axis.
 *
 * Updates the stage metadata to use the specified up axis.
 *
 * Undo restores the previous up axis.
 *
 * @param stageUp Up axis to apply (Y or Z).
 */
Command
stageUp(Session::StageUp stageUp);

/**
 * @brief Creates a command that sets the stage default prim.
 *
 * The path must refer to a valid root prim, for example `/World`.
 * The pseudo-root `/`, empty paths, child prims such as `/World/Geom`,
 * and invalid prim paths are rejected.
 *
 * The previous default prim is stored so the operation can be undone.
 * If the stage had no default prim before the command, undo clears the
 * default prim metadata.
 *
 * @param path Root prim path to set as the stage default prim.
 * @return Command that applies and undoes the default prim change.
 */
Command
defaultPrimPath(const SdfPath& path);

/**
 * @brief Creates a command that clears the stage default prim.
 *
 * The previous default prim is stored so the operation can be undone.
 *
 * @return Command that clears and restores the default prim metadata.
 */
Command
clearDefaultPrim();

}  // namespace stageviz
