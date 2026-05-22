#pragma once

#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

namespace science_and_theology {

// Simple fixed-size thread pool for CPU-bound work.
// Tasks are enqueued and executed by a pool of worker threads.
// All methods are thread-safe.
//
// Usage:
//   WorkerPool pool(4);
//   pool.enqueue([]() { do_work(); });
//   pool.enqueue([]() { do_more_work(); });
//   pool.wait_all();  // optional barrier
class WorkerPool {
public:
    // Creates a pool with the given number of worker threads.
    // num_threads = 0 means use hardware concurrency.
    explicit WorkerPool(int num_threads);

    // Destroys the pool, waiting for all workers to finish their current task.
    ~WorkerPool();

    // Enqueues a task for execution by a worker thread.
    // Returns immediately; the task runs on a background thread.
    void enqueue(std::function<void()> task);

    // Blocks until all currently enqueued tasks have completed.
    void wait_all();

    // Returns the approximate number of tasks that are pending or in-progress.
    size_t pending_count() const;

    // Returns the number of worker threads.
    int thread_count() const { return thread_count_; }

private:
    // Main loop run by each worker thread.
    void worker_loop();

    std::vector<std::thread> threads_;
    std::queue<std::function<void()>> tasks_;
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::condition_variable done_cv_;
    std::atomic<size_t> active_count_{0};
    int thread_count_ = 0;
    bool stop_ = false;
};

} // namespace science_and_theology