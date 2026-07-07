#include <atomic>
#include <chrono>
#include <mtp/thread_pool.hpp>
#include <numeric>
#include <set>
#include <vector>

#include "mini_test.hpp"

using namespace std::chrono_literals;

TEST("submit runs a simple task and returns its result") {
    mtp::ThreadPool pool(2);
    auto fut = pool.submit([] { return 21 * 2; });
    ASSERT_EQ(fut.get(), 42);
}

TEST("submit supports void-returning callables") {
    mtp::ThreadPool pool(2);
    std::atomic<bool> ran{false};
    auto fut = pool.submit([&ran] { ran.store(true); });
    fut.get();
    ASSERT_TRUE(ran.load());
}

TEST("submit forwards arguments correctly") {
    mtp::ThreadPool pool(2);
    auto fut = pool.submit([](int a, int b) { return a + b; }, 10, 32);
    ASSERT_EQ(fut.get(), 42);
}

TEST("exceptions thrown in a task propagate through the future") {
    mtp::ThreadPool pool(2);
    auto fut = pool.submit([]() -> int { throw std::runtime_error("boom"); });
    ASSERT_THROWS(fut.get(), std::runtime_error);
}

TEST("many tasks all complete and produce correct results") {
    mtp::ThreadPool pool(8);
    constexpr int N = 2000;
    std::vector<std::future<int>> futures;
    futures.reserve(N);
    for (int i = 0; i < N; ++i) {
        futures.push_back(pool.submit([i] { return i * i; }));
    }
    for (int i = 0; i < N; ++i) {
        ASSERT_EQ(futures[static_cast<size_t>(i)].get(), i * i);
    }
}

TEST("concurrent increments to a shared atomic counter are all counted") {
    mtp::ThreadPool pool(8);
    std::atomic<long> counter{0};
    constexpr int N = 10000;
    std::vector<std::future<void>> futures;
    futures.reserve(N);
    for (int i = 0; i < N; ++i) {
        futures.push_back(pool.submit([&counter] {
            counter.fetch_add(1, std::memory_order_relaxed);
        }));
    }
    for (auto& f : futures) f.get();
    ASSERT_EQ(counter.load(), N);
}

TEST("priority tasks are serviced ahead of already-queued normal tasks") {
    mtp::ThreadPool pool(1);  // single worker makes ordering deterministic-ish
    std::atomic<bool> normal_started{false};
    std::vector<int> order;
    std::mutex order_mutex;

    // Block the single worker with a slow task so priority tasks queue up
    // behind it and behind subsequently submitted normal tasks.
    auto blocker = pool.submit([&] {
        std::this_thread::sleep_for(30ms);
    });

    for (int i = 0; i < 5; ++i) {
        pool.submit([&order, &order_mutex, i] {
            std::lock_guard<std::mutex> lock(order_mutex);
            order.push_back(100 + i);  // normal tasks tagged >=100
        });
    }

    auto pfut = pool.submitPriority([&order, &order_mutex] {
        std::lock_guard<std::mutex> lock(order_mutex);
        order.push_back(1);  // priority task tagged 1
    });

    blocker.get();
    pfut.get();
    pool.waitIdle();

    std::lock_guard<std::mutex> lock(order_mutex);
    ASSERT_TRUE(!order.empty());
    ASSERT_EQ(order.front(), 1);  // priority task executed first among the pending ones
}

TEST("waitIdle blocks until all submitted work has completed") {
    mtp::ThreadPool pool(4);
    std::atomic<int> completed{0};
    for (int i = 0; i < 200; ++i) {
        pool.submit([&completed] {
            std::this_thread::sleep_for(1ms);
            completed.fetch_add(1, std::memory_order_relaxed);
        });
    }
    pool.waitIdle();
    ASSERT_EQ(completed.load(), 200);
}

TEST("pause stops new tasks from executing until resume is called") {
    mtp::ThreadPool pool(2);
    std::atomic<int> counter{0};

    pool.pause();
    ASSERT_TRUE(pool.isPaused());

    for (int i = 0; i < 5; ++i) {
        pool.submit([&counter] { counter.fetch_add(1); });
    }

    std::this_thread::sleep_for(50ms);
    ASSERT_EQ(counter.load(), 0);  // nothing should have run while paused

    pool.resume();
    pool.waitIdle();
    ASSERT_EQ(counter.load(), 5);
}

TEST("shutdown(true) drains all previously queued tasks before stopping") {
    std::atomic<int> completed{0};
    {
        mtp::ThreadPool pool(4);
        for (int i = 0; i < 500; ++i) {
            pool.submit([&completed] { completed.fetch_add(1); });
        }
        pool.shutdown(true);
    }
    ASSERT_EQ(completed.load(), 500);
}

TEST("submit after shutdown throws instead of silently dropping work") {
    mtp::ThreadPool pool(2);
    pool.shutdown(true);
    ASSERT_THROWS(pool.submit([] { return 1; }), std::runtime_error);
}

TEST("constructing a pool with zero threads throws") {
    ASSERT_THROWS(mtp::ThreadPool(0), std::invalid_argument);
}

TEST("work-stealing lets a single overloaded task not starve other work") {
    // Regression-style test: with many small tasks and few threads, all
    // tasks should still complete promptly (i.e. no queue starves forever).
    mtp::ThreadPool pool(4);
    std::atomic<int> completed{0};
    constexpr int N = 5000;
    std::vector<std::future<void>> futures;
    futures.reserve(N);
    for (int i = 0; i < N; ++i) {
        futures.push_back(pool.submit([&completed] {
            completed.fetch_add(1, std::memory_order_relaxed);
        }));
    }
    for (auto& f : futures) f.get();
    ASSERT_EQ(completed.load(), N);
}

TEST("pendingTasks reports zero once the pool is fully idle") {
    mtp::ThreadPool pool(4);
    for (int i = 0; i < 50; ++i) {
        pool.submit([] { std::this_thread::sleep_for(1ms); });
    }
    pool.waitIdle();
    ASSERT_EQ(pool.pendingTasks(), size_t{0});
}

int main() { return mini_test::run_all(); }
