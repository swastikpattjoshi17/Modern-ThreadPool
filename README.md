# ModernThreadPool

A small, header-only, modern C++17 thread pool with **work-stealing queues**,
**futures-based task submission**, **priority tasks**, and **pause/resume/graceful-shutdown**
semantics —  with no external dependencies.

This project focuses on the concurrency primitives you actually need to
reason carefully about in an interview: task queuing, exception-safe futures,
load balancing across worker threads, and correct shutdown ordering.

## Features

- **Work-stealing scheduler** — each worker owns a private deque (LIFO push/pop
  for cache locality); idle workers steal from a random victim's queue (FIFO)
  instead of contending on one global lock.
- **`submit(f, args...)`** — returns a `std::future<R>` for *any* callable
  (function, lambda, functor) with any signature, including `void`.
- **Exception-safe** — an exception thrown inside a task is captured and
  rethrown from `future.get()`, never crashes a worker thread.
- **Priority tasks** — `submitPriority(...)` jumps ahead of normal queued work.
- **Pause / resume** — freeze the pool without discarding queued work.
- **Two shutdown modes** — graceful drain (finish all queued tasks) or
  immediate stop (discard unstarted work), controlled via `shutdown(bool)`.
- **`waitIdle()`** — block until the pool has no pending or in-flight tasks,
  without shutting it down.
- **`parallel_for` / `parallel_reduce`** — small higher-level algorithms built
  on top of the pool (`mtp/parallel_algorithms.hpp`), showing how a raw
  task-submission API composes into useful parallel primitives.

## Why work-stealing?

A single global task queue guarded by one mutex becomes a bottleneck as
thread count grows — every worker fights over the same lock on every pop.
Giving each worker its own queue means, in the common case, a worker never
contends with anyone. Workers only pay a synchronization cost when they run
out of local work and need to steal, which is comparatively rare. This
project's `WorkStealingQueue` uses a mutex per-queue (not a lock-free ring
buffer) — a deliberate choice for correctness and readability, while still
capturing the core performance idea: *N queues beat 1 queue under load.*

## Project layout

```
ModernThreadPool/
├── include/mtp/
│   ├── thread_pool.hpp          # core: Task, WorkStealingQueue, ThreadPool
│   └── parallel_algorithms.hpp  # parallel_for, parallel_reduce
├── examples/
│   ├── basic_usage.cpp
│   └── parallel_algorithms_demo.cpp
├── tests/
│   ├── mini_test.hpp             # ~90-line dependency-free test harness
│   ├── test_thread_pool.cpp
│   └── test_parallel_algorithms.cpp
├── benchmarks/
│   └── bench_thread_pool.cpp
└── CMakeLists.txt
```

## Building

Requires a C++17 compiler and CMake 3.14+. No external dependencies.

```bash
git clone https://github.com/<swastikpattjoshi17>/ModernThreadPool.git
cd ModernThreadPool
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

Run the tests:

```bash
cd build && ctest --output-on-failure
```

Run an example:

```bash
./build/basic_usage
./build/parallel_algorithms_demo
```

Run the benchmark:

```bash
./build/bench_thread_pool
```

## Usage

```cpp
#include <mtp/thread_pool.hpp>

int main() {
    mtp::ThreadPool pool(4);  // 4 worker threads

    // Submit work, get a future back.
    std::future<int> result = pool.submit([](int a, int b) { return a + b; }, 2, 3);
    std::cout << result.get() << '\n';  // 5

    // High-priority task jumps the queue.
    pool.submitPriority([] { std::cout << "urgent!\n"; });

    // Wait for everything queued so far to finish.
    pool.waitIdle();

    // Pool automatically drains and joins all threads on destruction.
}
```

```cpp
#include <mtp/parallel_algorithms.hpp>

mtp::ThreadPool pool;
std::vector<int> data(1'000'000);

mtp::parallel_for(pool, size_t{0}, data.size(), [&](size_t i) {
    data[i] = compute(i);
});

long long total = mtp::parallel_reduce(
    pool, data, 0LL,
    [](int v) { return static_cast<long long>(v); },
    [](long long a, long long b) { return a + b; });
```

## Design notes

- **Type erasure without `std::function` overhead everywhere**: `Task` uses a
  small custom type-erasure wrapper so the pool doesn't force every task
  through `std::function`'s allocation/copy semantics for the outer wrapper —
  tasks are move-only end to end.
- **Round-robin submission, steal-based rebalancing**: new tasks are handed
  out round-robin across per-worker queues so no single queue is favored;
  workers that run dry steal from others rather than sitting idle while
  another worker is backlogged.
- **Shutdown correctness**: `stopping_` is a one-shot atomic flag guarded with
  `compare_exchange_strong`, so calling `shutdown()` twice (or having it fire
  once explicitly and once via the destructor) is safe and idempotent.
- **No busy-waiting**: workers block on a condition variable with a short
  timeout rather than spinning, so an idle pool doesn't burn CPU.

## Testing

The test suite is intentionally dependency-free (no GoogleTest/Catch2) — see
`tests/mini_test.hpp` for a ~90-line `TEST`/`ASSERT_EQ`/`ASSERT_THROWS`
harness. Tests cover: correctness of results, exception propagation, priority
ordering, pause/resume, graceful vs. immediate shutdown, idle-waiting, and a
stress test with thousands of concurrently submitted tasks to exercise the
work-stealing path.

## License

MIT — see [LICENSE](LICENSE).
