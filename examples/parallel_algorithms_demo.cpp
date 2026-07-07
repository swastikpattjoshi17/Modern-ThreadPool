// Demonstrates parallel_for and parallel_reduce built on top of ThreadPool.
#include <iostream>
#include <mtp/parallel_algorithms.hpp>
#include <mtp/thread_pool.hpp>
#include <mutex>
#include <numeric>
#include <vector>

int main() {
    mtp::ThreadPool pool;

    // parallel_for: fill a vector concurrently.
    std::vector<int> data(1'000'000);
    mtp::parallel_for(pool, size_t{0}, data.size(), [&data](size_t i) {
        data[i] = static_cast<int>(i % 97);
    });

    long long serial_sum = std::accumulate(data.begin(), data.end(), 0LL);

    // parallel_reduce: sum the same data concurrently and cross-check.
    long long parallel_sum = mtp::parallel_reduce(
        pool, data, 0LL, [](int v) { return static_cast<long long>(v); },
        [](long long a, long long b) { return a + b; });

    std::cout << "Serial sum:   " << serial_sum << '\n';
    std::cout << "Parallel sum: " << parallel_sum << '\n';
    std::cout << (serial_sum == parallel_sum ? "MATCH\n" : "MISMATCH!\n");

    return serial_sum == parallel_sum ? 0 : 1;
}
