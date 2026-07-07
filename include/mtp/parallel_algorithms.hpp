// ============================================================================
// mtp::parallel_for / mtp::parallel_reduce
//
// Small convenience algorithms built on top of ThreadPool, demonstrating how
// a raw task-submission API is used to build higher-level concurrency
// primitives (a common pattern in real-world thread pool libraries).
// ============================================================================
#pragma once

#include <future>
#include <numeric>
#include <vector>

#include "mtp/thread_pool.hpp"

namespace mtp {

// Splits [begin, end) into `chunks` contiguous ranges and runs `fn(i)` for
// every index in parallel across the given pool. Blocks until all chunks
// finish. `fn` must be safe to call concurrently for different indices.
template <typename IndexT, typename Fn>
void parallel_for(ThreadPool& pool, IndexT begin, IndexT end, Fn fn,
                   size_t chunks = 0) {
    if (begin >= end) return;

    const size_t total = static_cast<size_t>(end - begin);
    const size_t num_chunks =
        chunks == 0 ? std::min<size_t>(pool.threadCount(), total) : chunks;
    const size_t chunk_size = (total + num_chunks - 1) / num_chunks;

    std::vector<std::future<void>> futures;
    futures.reserve(num_chunks);

    for (size_t c = 0; c < num_chunks; ++c) {
        IndexT chunk_begin = begin + static_cast<IndexT>(c * chunk_size);
        IndexT chunk_end = begin + static_cast<IndexT>(std::min(total, (c + 1) * chunk_size));
        if (chunk_begin >= chunk_end) continue;

        futures.push_back(pool.submit([chunk_begin, chunk_end, &fn] {
            for (IndexT i = chunk_begin; i < chunk_end; ++i) {
                fn(i);
            }
        }));
    }

    for (auto& f : futures) f.get();  // propagates any exception from fn
}

// Parallel map-reduce over a container: applies `map_fn` to each element in
// parallel, then combines partial results with `reduce_fn` (must be
// associative). Returns the final reduced value.
template <typename Container, typename T, typename MapFn, typename ReduceFn>
T parallel_reduce(ThreadPool& pool, const Container& data, T init, MapFn map_fn,
                   ReduceFn reduce_fn) {
    const size_t n = data.size();
    if (n == 0) return init;

    const size_t workers = std::max<size_t>(1, pool.threadCount());
    const size_t chunk_size = (n + workers - 1) / workers;

    std::vector<std::future<T>> futures;
    futures.reserve(workers);

    auto it = data.begin();
    for (size_t c = 0; c < workers; ++c) {
        size_t start = c * chunk_size;
        size_t stop = std::min(n, (c + 1) * chunk_size);
        if (start >= stop) continue;

        auto chunk_begin = it;
        std::advance(chunk_begin, start);
        auto chunk_end = it;
        std::advance(chunk_end, stop);

        futures.push_back(pool.submit([chunk_begin, chunk_end, &map_fn, &reduce_fn]() -> T {
            T local{};
            bool first = true;
            for (auto iter = chunk_begin; iter != chunk_end; ++iter) {
                T mapped = map_fn(*iter);
                local = first ? mapped : reduce_fn(local, mapped);
                first = false;
            }
            return local;
        }));
    }

    T result = init;
    for (auto& f : futures) {
        result = reduce_fn(result, f.get());
    }
    return result;
}

}  // namespace mtp
