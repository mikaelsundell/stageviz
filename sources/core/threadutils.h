// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#pragma once

#include <QRunnable>
#include <QThreadPool>
#include <algorithm>
#include <cstddef>
#include <exception>
#include <future>
#include <memory>
#include <vector>

namespace stageviz::threadutils {

/**
 * @brief Returns the number of threads available for parallel work.
 *
 * @return Number of available threads.
 */
unsigned
availableThreads();

/**
 * @brief Determines a suitable worker count for a given workload.
 *
 * @param items Number of items to process.
 * @param minimumItemsPerWorker Minimum workload assigned per worker.
 * @return Recommended number of workers.
 */
unsigned
workerCount(std::size_t items, std::size_t minimumItemsPerWorker = 256);

/**
 * @brief Executes a callback synchronously over disjoint item ranges.
 *
 * The calling thread processes worker zero while remaining workers run on
 * Qt's global thread pool. The function waits for all workers to finish and
 * rethrows any captured exception after joining them.
 *
 * Worker callbacks must not access Qt widgets. Callers working with USD
 * stages must hold any required stage read lock for the entire call.
 *
 * @tparam Callback Callable accepting (workerIndex, begin, end).
 * @param items Total number of items to process.
 * @param workers Requested worker count, clamped to the item count.
 * @param callback Function invoked for each half-open item range.
 */
template<class Callback>
void
parallelForRanges(std::size_t items, unsigned workers, Callback&& callback)
{
    if (items == 0)
        return;
    workers = std::max(1u, std::min(workers, static_cast<unsigned>(items)));
    if (workers == 1) {
        callback(0, 0, items);
        return;
    }

    QThreadPool* pool = QThreadPool::globalInstance();
    std::vector<std::future<void>> futures;
    futures.reserve(workers - 1);
    for (unsigned worker = 1; worker < workers; ++worker) {
        auto task = std::make_shared<std::packaged_task<void()>>(
            [&, worker] { callback(worker, items * worker / workers, items * (worker + 1) / workers); });
        futures.push_back(task->get_future());
        pool->start(QRunnable::create([task] { (*task)(); }));
    }

    std::exception_ptr callerError;
    try {
        callback(0, 0, items / workers);
    } catch (...) {
        callerError = std::current_exception();
    }
    std::exception_ptr workerError;
    for (auto& future : futures) {
        try {
            future.get();
        } catch (...) {
            if (!workerError)
                workerError = std::current_exception();
        }
    }
    if (callerError)
        std::rethrow_exception(callerError);
    if (workerError)
        std::rethrow_exception(workerError);
}

}  // namespace stageviz::threadutils
