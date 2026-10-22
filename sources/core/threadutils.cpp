// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#include "threadutils.h"

namespace stageviz::threadutils {

unsigned
availableThreads()
{
    return static_cast<unsigned>(std::max(1, QThreadPool::globalInstance()->maxThreadCount()));
}

unsigned
workerCount(std::size_t items, std::size_t minimumItemsPerWorker)
{
    if (items == 0)
        return 1;
    minimumItemsPerWorker = std::max<std::size_t>(1, minimumItemsPerWorker);
    const std::size_t useful = (items + minimumItemsPerWorker - 1) / minimumItemsPerWorker;
    return static_cast<unsigned>(std::min<std::size_t>(availableThreads(), std::max<std::size_t>(1, useful)));
}

}  // namespace stageviz::threadutils
