// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#pragma once

#include "stageviz.h"
#include <QList>
#include <QMetaType>
#include <pxr/base/tf/token.h>
#include <pxr/usd/sdf/path.h>
#include <pxr/usd/usd/notice.h>

PXR_NAMESPACE_USING_DIRECTIVE

namespace stageviz {

/**
 * @struct NoticeEntry
 * @brief One entry describing a changed USD object.
 *
 * This is a lightweight, Qt-friendly representation of information
 * from UsdNotice::ObjectsChanged.
 *
 * It preserves:
 * - path
 * - namespace edit classification (PrimResyncType)
 * - associated path for rename/reparent
 * - info-only vs resync vs asset-path-resync
 * - changed fields (for info-only cases)
 */
struct NoticeEntry {
    /**
     * @brief Path of the affected object (prim or property).
     */
    SdfPath path;

    /**
     * @brief Associated path for namespace edits (rename/reparent pairs).
     */
    SdfPath associatedPath;

    /**
     * @brief Classification for prim resyncs (from USD).
     */
    UsdNotice::ObjectsChanged::PrimResyncType primResyncType = UsdNotice::ObjectsChanged::PrimResyncType::Invalid;

    /**
     * @brief True if this path is in GetChangedInfoOnlyPaths().
     */
    bool changedInfoOnly = false;

    /**
     * @brief True if this path is in GetResolvedAssetPathsResyncedPaths().
     */
    bool resolvedAssetPathsResynced = false;

    /**
     * @brief Changed fields for this object (may be empty).
     */
    TfTokenVector changedFields;
};

/**
 * @struct NoticeBatch
 * @brief Batched USD object changes.
 *
 * This represents a batch of UsdNotice::ObjectsChanged data.
 * Used by Session to deliver updates to widgets in both immediate
 * and deferred modes.
 */
struct NoticeBatch {
    /**
     * @brief Changes accumulated in arrival order; paths may occur more than once.
     */
    QList<NoticeEntry> entries;
};

}  // namespace stageviz

// Register for Qt signal/slot usage
Q_DECLARE_METATYPE(stageviz::NoticeEntry)
Q_DECLARE_METATYPE(stageviz::NoticeBatch)
