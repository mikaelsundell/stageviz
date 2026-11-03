// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 - present Mikael Sundell

#pragma once

#include <QJsonObject>
#include <pxr/base/gf/camera.h>
#include <pxr/imaging/hd/rprimCollection.h>
#include <pxr/imaging/hd/sceneIndex.h>
#include <pxr/usd/usd/stage.h>

PXR_NAMESPACE_OPEN_SCOPE
class HdRenderIndex;
PXR_NAMESPACE_CLOSE_SCOPE

namespace stageviz {
/**
 * @brief Reads USD, scene-index, and Storm geometry state without rendering or GPU readback.
 *
 * Reports the last synchronized render items alongside current USD opinions.
 * Buffer handles and sizes describe allocation metadata, not successful rasterization.
 * The caller holds the stage read lock on the viewport GUI thread.
 */
QJsonObject
inspectRenderPrim(const pxr::UsdStageRefPtr& stage, pxr::HdRenderIndex* index,
                  const pxr::HdSceneIndexBaseRefPtr& inputScene, const pxr::SdfPath& usdPath,
                  const pxr::SdfPathVector& hydraPaths, const pxr::GfCamera& camera, double timeCode,
                  const pxr::HdRprimCollection& collection);
}  // namespace stageviz
