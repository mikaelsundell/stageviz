
// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#include "bboxutils.h"
#include <pxr/usd/usdGeom/imageable.h>

namespace stageviz::bbox {
namespace {
    std::mutex cacheMutex;
    UsdGeomBBoxCache& sharedCache()
    {
        static UsdGeomBBoxCache cache(UsdTimeCode::Default(), UsdGeomImageable::GetOrderedPurposeTokens(), true);
        return cache;
    }
}  // namespace

CacheGuard::CacheGuard()
    : lock_(cacheMutex)
{}

CacheGuard::~CacheGuard() = default;

GfBBox3d
CacheGuard::ComputeWorldBound(const UsdPrim& prim)
{
    return prim ? sharedCache().ComputeWorldBound(prim) : GfBBox3d();
}

void
clear()
{
    std::lock_guard<std::mutex> lock(cacheMutex);
    sharedCache().Clear();
}
}  // namespace stageviz::bbox
