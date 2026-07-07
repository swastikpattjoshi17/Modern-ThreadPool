// ============================================================================
// mtp::ThreadPool — a modern C++17 work-stealing thread pool
//
// Features:
//   - Work-stealing per-thread deques (reduces contention vs single queue)
//   - submit() returns std::future<R> for any callable
//   - Priority tasks via a separate high-priority global queue
//   - Graceful shutdown (drain) and immediate shutdown (cancel pending)
//   - Dynamic worker count queries, pause/resume
//   - Exception-safe: exceptions from tasks propagate through the future
//
// Author: Swastik Pattjoshi
// License: MIT
// ============================================================================
#pragma once

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <random>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <vector>

namespace mtp {

// ----------------------------------------------------------------------------
// Task: a type-erased, move-only unit of work.
// ----------------------------------------------------------------------------
class Task {
public:
    Task() = default;

    template <typename F, typename = std::enable_if_t<!std::is_same_v<std::decay_t<F>, Task>>>
    explicit Task(F&& f) : callable_(std::make_unique<Model<std::decay_t<F>>>(std::forward<F>(f))) {}

    Task(Task&&) noexcept = default;
    Task& operator=(Task&&) noexcept = default;
    Task(const Task&) = delete;
    Task& operator=(const Task&) = delete;

    explicit operator bool() const noexcept { return static_cast<bool>(callable_); }

    void operator()() {
        if (callable_) callable_->invoke();
    }

private:
    struct Concept {
        virtual ~Concept() = default;
        virtual void invoke() = 0;
    };

    template <typename F>
    struct Model final : Concept {
        F f;
        explicit Model(F&& fn) : f(std::move(fn)) {}
        void invoke() override { f(); }
    };

    std::unique_ptr<Concept> callable_;
};

// ----------------------------------------------------------------------------
// WorkStealingQueue: a Chase-Lev-style double-ended queue.
// Owner pushes/pops from the back (LIFO, cache-friendly).
// Thieves steal from the front (FIFO), reducing contention with the owner.
// This implementation uses a mutex for clarity/safety rather than a fully
// lock-free CAS ring buffer — a deliberate simplicity/perf tradeoff that is
// still dramatically better than one global mutex-guarded queue at scale.
// ----------------------------------------------------------------------------
class WorkStealingQueue {
public:
    void push(Task&& task) {
        std::lock_guard<std::mutex> lock(mutex_);
        deque_.push_back(std::move(task));
    }

    // Owner-side pop: takes from the back (most recently pushed).
    std::optional<Task> pop() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (deque_.empty()) return std::nullopt;
        Task t = std::move(deque_.back());
        deque_.pop_back();
        return t;
    }

    // Thief-side steal: takes from the front (oldest task).
    std::optional<Task> steal() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (deque_.empty()) return std::nullopt;
        Task t = std::move(deque_.front());
        deque_.pop_front();
        return t;
    }

    bool empty() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return deque_.empty();
    }

    size_t size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return deque_.size();
    }

private:
    mutable std::mutex mutex_;
    std::deque<Task> deque_;
};

// ----------------------------------------------------------------------------
// ThreadPool
// ----------------------------------------------------------------------------
class ThreadPool {
public:
    // Construct with `num_threads` workers (defaults to hardware concurrency).
    explicit ThreadPool(size_t num_threads = std::max(1u, std::thread::hardware_concurrency()))
        : queues_(num_threads), rng_(std::random_device{}()) {
        if (num_threads == 0) {
            throw std::invalid_argument("ThreadPool requires at least one thread");
        }
        workers_.reserve(num_threads);
        for (size_t i = 0; i < num_threads; ++i) {
            workers_.emplace_back([this, i] { workerLoop(i); });
        }
    }

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    ~ThreadPool() {
        shutdown(/*wait_for_tasks=*/true);
    }

    // Submit a callable + args; returns a future for the result.
    // Exceptions thrown inside `f` are captured and rethrown on future.get().
    template <typename F, typename... Args>
    auto submit(F&& f, Args&&... args) -> std::future<std::invoke_result_t<F, Args...>> {
        using R = std::invoke_result_t<F, Args...>;

        if (stopping_.load(std::memory_order_acquire)) {
            throw std::runtime_error("submit() called on a stopped ThreadPool");
        }

        auto bound = std::bind(std::forward<F>(f), std::forward<Args>(args)...);
        auto promise = std::make_shared<std::promise<R>>();
        std::future<R> future = promise->get_future();

        Task task([bound = std::move(bound), promise]() mutable {
            try {
                if constexpr (std::is_void_v<R>) {
                    bound();
                    promise->set_value();
                } else {
                    promise->set_value(bound());
                }
            } catch (...) {
                promise->set_exception(std::current_exception());
            }
        });

        pushTask(std::move(task));
        return future;
    }

    // Submit a high-priority task: served before normal tasks whenever a
    // worker looks for work, regardless of which queue it came from.
    template <typename F, typename... Args>
    auto submitPriority(F&& f, Args&&... args) -> std::future<std::invoke_result_t<F, Args...>> {
        using R = std::invoke_result_t<F, Args...>;

        if (stopping_.load(std::memory_order_acquire)) {
            throw std::runtime_error("submitPriority() called on a stopped ThreadPool");
        }

        auto bound = std::bind(std::forward<F>(f), std::forward<Args>(args)...);
        auto promise = std::make_shared<std::promise<R>>();
        std::future<R> future = promise->get_future();

        Task task([bound = std::move(bound), promise]() mutable {
            try {
                if constexpr (std::is_void_v<R>) {
                    bound();
                    promise->set_value();
                } else {
                    promise->set_value(bound());
                }
            } catch (...) {
                promise->set_exception(std::current_exception());
            }
        });

        {
            std::lock_guard<std::mutex> lock(priority_mutex_);
            priority_queue_.push_back(std::move(task));
        }
        pending_.fetch_add(1, std::memory_order_relaxed);
        cv_.notify_one();
        return future;
    }

    // Pause: workers finish their current task, then block until resume().
    void pause() {
        paused_.store(true, std::memory_order_release);
    }

    void resume() {
        paused_.store(false, std::memory_order_release);
        cv_.notify_all();
    }

    bool isPaused() const { return paused_.load(std::memory_order_acquire); }

    size_t threadCount() const { return workers_.size(); }

    // Number of tasks currently queued (approximate under concurrency).
    size_t pendingTasks() const { return pending_.load(std::memory_order_relaxed); }

    // Block until all currently queued/in-flight tasks complete, without
    // stopping the pool (it can accept more work afterward).
    void waitIdle() {
        std::unique_lock<std::mutex> lock(idle_mutex_);
        idle_cv_.wait(lock, [this] {
            return pending_.load(std::memory_order_acquire) == 0 &&
                   active_workers_.load(std::memory_order_acquire) == 0;
        });
    }

    // Stop the pool. If wait_for_tasks is true (default), drains all queued
    // work first (graceful shutdown). If false, discards unstarted tasks.
    void shutdown(bool wait_for_tasks = true) {
        bool expected = false;
        if (!stopping_.compare_exchange_strong(expected, true)) {
            return;  // already shut down
        }
        drain_on_stop_.store(wait_for_tasks, std::memory_order_release);
        cv_.notify_all();
        for (auto& t : workers_) {
            if (t.joinable()) t.join();
        }
    }

private:
    void pushTask(Task&& task) {
        // Round-robin assignment across worker queues balances initial load;
        // work-stealing then rebalances at runtime if some workers finish early.
        size_t idx = next_queue_.fetch_add(1, std::memory_order_relaxed) % queues_.size();
        queues_[idx].push(std::move(task));
        pending_.fetch_add(1, std::memory_order_relaxed);
        cv_.notify_one();
    }

    std::optional<Task> tryGetPriorityTask() {
        std::lock_guard<std::mutex> lock(priority_mutex_);
        if (priority_queue_.empty()) return std::nullopt;
        Task t = std::move(priority_queue_.front());
        priority_queue_.pop_front();
        return t;
    }

    std::optional<Task> findTask(size_t my_index) {
        // 1) Priority queue always wins.
        if (auto t = tryGetPriorityTask()) return t;

        // 2) Try our own queue first (cache-friendly, LIFO).
        if (auto t = queues_[my_index].pop()) return t;

        // 3) Steal from a random victim to balance load.
        size_t n = queues_.size();
        if (n > 1) {
            std::uniform_int_distribution<size_t> dist(0, n - 1);
            for (size_t attempts = 0; attempts < n * 2; ++attempts) {
                size_t victim = dist(rng_);
                if (victim == my_index) continue;
                if (auto t = queues_[victim].steal()) return t;
            }
        }
        return std::nullopt;
    }

    bool allQueuesEmpty() const {
        {
            std::lock_guard<std::mutex> lock(priority_mutex_);
            if (!priority_queue_.empty()) return false;
        }
        for (auto& q : queues_) {
            if (!q.empty()) return false;
        }
        return true;
    }

    void workerLoop(size_t index) {
        std::mt19937 local_rng(std::random_device{}() + index);
        while (true) {
            // Respect pause: block here until resumed or stopped.
            while (paused_.load(std::memory_order_acquire) &&
                   !stopping_.load(std::memory_order_acquire)) {
                std::unique_lock<std::mutex> lock(cv_mutex_);
                cv_.wait_for(lock, std::chrono::milliseconds(20));
            }

            std::optional<Task> task = findTaskWithRng(index, local_rng);

            if (!task) {
                if (stopping_.load(std::memory_order_acquire)) {
                    if (!drain_on_stop_.load(std::memory_order_acquire) || allQueuesEmpty()) {
                        return;  // nothing left, or immediate shutdown requested
                    }
                }
                std::unique_lock<std::mutex> lock(cv_mutex_);
                cv_.wait_for(lock, std::chrono::milliseconds(20), [this] {
                    return pending_.load(std::memory_order_acquire) > 0 ||
                           stopping_.load(std::memory_order_acquire);
                });
                continue;
            }

            active_workers_.fetch_add(1, std::memory_order_acq_rel);
            (*task)();  // exceptions are already captured inside the Task itself
            pending_.fetch_sub(1, std::memory_order_relaxed);
            active_workers_.fetch_sub(1, std::memory_order_acq_rel);

            if (pending_.load(std::memory_order_acquire) == 0 &&
                active_workers_.load(std::memory_order_acquire) == 0) {
                idle_cv_.notify_all();
            }
        }
    }

    std::optional<Task> findTaskWithRng(size_t my_index, std::mt19937& local_rng) {
        if (auto t = tryGetPriorityTask()) return t;
        if (auto t = queues_[my_index].pop()) return t;

        size_t n = queues_.size();
        if (n > 1) {
            std::uniform_int_distribution<size_t> dist(0, n - 1);
            for (size_t attempts = 0; attempts < n * 2; ++attempts) {
                size_t victim = dist(local_rng);
                if (victim == my_index) continue;
                if (auto t = queues_[victim].steal()) return t;
            }
        }
        return std::nullopt;
    }

    std::vector<std::thread> workers_;
    std::vector<WorkStealingQueue> queues_;
    std::atomic<size_t> next_queue_{0};

    mutable std::mutex priority_mutex_;
    std::deque<Task> priority_queue_;

    std::atomic<size_t> pending_{0};
    std::atomic<size_t> active_workers_{0};

    std::atomic<bool> stopping_{false};
    std::atomic<bool> drain_on_stop_{true};
    std::atomic<bool> paused_{false};

    std::mutex cv_mutex_;
    std::condition_variable cv_;

    std::mutex idle_mutex_;
    std::condition_variable idle_cv_;

    std::mt19937 rng_;
};

}  // namespace mtp
