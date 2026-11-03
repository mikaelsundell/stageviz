// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#pragma once

#include "commandprim.h"
#include <functional>
#include <optional>
#include <pxr/base/gf/matrix4d.h>
#include <pxr/usd/usd/prim.h>

namespace stageviz {
namespace command {

    /**
     * @brief Shares prim creation, unique naming, selection and undo with schema presets.
     *
     * The initializer runs under the stage write lock after definition.
     */
    Command createPrimCommand(const SdfPath& parentPath, const QString& nameInput, const TfToken& typeName,
                              bool selectCreated, std::function<bool(const UsdPrim&, QString&)> initialize = {});


    /**
     * @brief Copies resolved prim targets with an optional world delta shared by transform commands.
     */
    Command copyPathsCommand(const QList<SdfPath>& paths, bool selectCopies, std::optional<GfMatrix4d> worldDelta);

}  // namespace command
}  // namespace stageviz
