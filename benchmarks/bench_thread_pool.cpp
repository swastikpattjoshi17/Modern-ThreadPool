// A small, dependency-free benchmark: compares serial execution against the
// thread pool at increasing worker counts, for a CPU-bound workload.
// Not a substitute for Google Benchmark, but enough to demonstrate scaling
// and gives concrete numbers to talk about in an interview.
#include <chrono>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <mtp/thread_pool.hpp>
#include <vector>

namespace {

// A deliberately CPU-bound "unit of work" so speedups are visible.
double busy_work(int n) {
    double acc = 0.0;
    for (int i = 1; i <= n; ++i) {
        acc += std::sin(static_cast<double>(i)) * std::cos(static_cast<double>(i));
    }
    return acc;
}

template <typename Fn>
long long time_ms(Fn&& fn) {
    auto start = std::chrono::steady_clock::now();
    fn();
    auto end = std::chrono::steady_clock::now();
    return std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
}

}  // namespace

int main() {
    constexpr int kNumTasks = 200;
    constexpr int kWorkPerTask = 200000;

    std::cout << "Benchmark: " << kNumTasks << " CPU-bound tasks, "
              << kWorkPerTask << " iterations each\n\n";

    volatile double sink = 0.0;  // prevent the optimizer from eliding the work

    long long serial_ms = time_ms([&] {
        for (int i = 0; i < kNumTasks; ++i) {
            sink = busy_work(kWorkPerTask);
        }
    });
    std::cout << std::left << std::setw(28) << "Serial (1 thread, no pool)" << serial_ms << " ms\n";

    for (unsigned threads : {1u, 2u, 4u, 8u, std::thread::hardware_concurrency()}) {
        if (threads == 0) continue;
        long long pool_ms = time_ms([&] {
            mtp::ThreadPool pool(threads);
            std::vector<std::future<double>> futures;
            futures.reserve(kNumTasks);
            for (int i = 0; i < kNumTasks; ++i) {
                futures.push_back(pool.submit(busy_work, kWorkPerTask));
            }
            for (auto& f : futures) sink = f.get();
        });

        double speedup = static_cast<double>(serial_ms) / static_cast<double>(pool_ms);
        std::cout << std::left << std::setw(20) << ("Pool with " + std::to_string(threads) + " threads")
                  << std::setw(8) << pool_ms << " ms   (speedup: " << std::fixed
                  << std::setprecision(2) << speedup << "x)\n";
    }

    return 0;
}
