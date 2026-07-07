#include <mtp/parallel_algorithms.hpp>
#include <mtp/thread_pool.hpp>
#include <numeric>
#include <vector>

#include "mini_test.hpp"

TEST("parallel_for writes the expected value at every index") {
    mtp::ThreadPool pool(4);
    std::vector<int> data(10000, -1);
    mtp::parallel_for(pool, size_t{0}, data.size(), [&data](size_t i) {
        data[i] = static_cast<int>(i);
    });
    for (size_t i = 0; i < data.size(); ++i) {
        ASSERT_EQ(data[i], static_cast<int>(i));
    }
}

TEST("parallel_for handles an empty range without crashing") {
    mtp::ThreadPool pool(4);
    int calls = 0;
    mtp::parallel_for(pool, size_t{0}, size_t{0}, [&calls](size_t) { ++calls; });
    ASSERT_EQ(calls, 0);
}

TEST("parallel_for handles ranges smaller than the thread count") {
    mtp::ThreadPool pool(8);
    std::vector<int> data(3, 0);
    mtp::parallel_for(pool, size_t{0}, data.size(), [&data](size_t i) { data[i] = 7; });
    ASSERT_EQ(data[0], 7);
    ASSERT_EQ(data[1], 7);
    ASSERT_EQ(data[2], 7);
}

TEST("parallel_reduce sums a vector correctly against a serial baseline") {
    mtp::ThreadPool pool(4);
    std::vector<int> data(50000);
    std::iota(data.begin(), data.end(), 1);

    long long expected = std::accumulate(data.begin(), data.end(), 0LL);
    long long actual = mtp::parallel_reduce(
        pool, data, 0LL, [](int v) { return static_cast<long long>(v); },
        [](long long a, long long b) { return a + b; });

    ASSERT_EQ(actual, expected);
}

TEST("parallel_reduce on an empty container returns the initial value") {
    mtp::ThreadPool pool(4);
    std::vector<int> data;
    long long result = mtp::parallel_reduce(
        pool, data, 99LL, [](int v) { return static_cast<long long>(v); },
        [](long long a, long long b) { return a + b; });
    ASSERT_EQ(result, 99LL);
}

TEST("parallel_reduce finds the max via a custom reduce function") {
    mtp::ThreadPool pool(4);
    std::vector<int> data = {3, 1, 4, 1, 5, 9, 2, 6, 5, 3, 5};
    int result = mtp::parallel_reduce(
        pool, data, std::numeric_limits<int>::min(), [](int v) { return v; },
        [](int a, int b) { return std::max(a, b); });
    ASSERT_EQ(result, 9);
}

int main() { return mini_test::run_all(); }
