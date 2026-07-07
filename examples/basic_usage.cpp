// Basic usage: submitting tasks, getting futures, handling exceptions.
#include <chrono>
#include <iostream>
#include <mtp/thread_pool.hpp>

int main() {
    mtp::ThreadPool pool(4);
    std::cout << "Pool started with " << pool.threadCount() << " threads\n";

    // Fire-and-collect: submit several tasks, gather futures.
    std::vector<std::future<int>> futures;
    for (int i = 0; i < 8; ++i) {
        futures.push_back(pool.submit([i] {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            return i * i;
        }));
    }

    std::cout << "Squares: ";
    for (auto& f : futures) std::cout << f.get() << ' ';
    std::cout << '\n';

    // Void task.
    auto done = pool.submit([] { std::cout << "Void task ran\n"; });
    done.get();

    // Exception propagation through the future.
    auto risky = pool.submit([]() -> int {
        throw std::runtime_error("something went wrong inside a task");
    });
    try {
        risky.get();
    } catch (const std::exception& e) {
        std::cout << "Caught expected exception: " << e.what() << '\n';
    }

    // Priority task jumps ahead of normal queued work.
    for (int i = 0; i < 20; ++i) {
        pool.submit([i] { std::this_thread::sleep_for(std::chrono::milliseconds(5)); });
    }
    auto urgent = pool.submitPriority([] {
        std::cout << "Priority task executed promptly\n";
        return 42;
    });
    std::cout << "Priority result: " << urgent.get() << '\n';

    pool.waitIdle();
    std::cout << "All tasks drained. Shutting down.\n";
    return 0;
}
