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
    /// Path of the affected object (prim or property)
    SdfPath path;

    /// Associated path for namespace edits (rename/reparent pairs)
    SdfPath associatedPath;

    /// Classification for prim resyncs (from USD)
    UsdNotice::ObjectsChanged::PrimResyncType primResyncType = UsdNotice::ObjectsChanged::PrimResyncType::Invalid;

    /// True if this path is in GetChangedInfoOnlyPaths()
    bool changedInfoOnly = false;

    /// True if this path is in GetResolvedAssetPathsResyncedPaths()
    bool resolvedAssetPathsResynced = false;

    /// Changed fields for this object (may be empty)
    TfTokenVector changedFields;
};

/**
 * @struct NoticeBatch
 * @brief Batched USD object changes.
 *
 * This represents a coalesced set of UsdNotice::ObjectsChanged data.
 * Used by Session to deliver updates to widgets in both immediate
 * and deferred modes.
 */
struct NoticeBatch {
    QList<NoticeEntry> entries;
};

}  // namespace stageviz

// Register for Qt signal/slot usage
Q_DECLARE_METATYPE(stageviz::NoticeEntry)
Q_DECLARE_METATYPE(stageviz::NoticeBatch)
