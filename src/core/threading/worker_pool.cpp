#include "worker_pool.hpp"

namespace science_and_theology {

WorkerPool::WorkerPool(int num_threads) {
    if (num_threads <= 0) {
        thread_count_ = static_cast<int>(
            std::thread::hardware_concurrency());
        if (thread_count_ <= 0) {
            thread_count_ = 2;
        }
    } else {
        thread_count_ = num_threads;
    }

    threads_.reserve(thread_count_);
    for (int i = 0; i < thread_count_; ++i) {
        threads_.emplace_back(&WorkerPool::worker_loop, this);
    }
}

WorkerPool::~WorkerPool() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stop_ = true;
    }
    cv_.notify_all();

    for (auto& thread : threads_) {
        if (thread.joinable()) {
            thread.join();
        }
    }
}

void WorkerPool::enqueue(std::function<void()> task) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        tasks_.push(std::move(task));
    }
    cv_.notify_one();
}

void WorkerPool::wait_all() {
    std::unique_lock<std::mutex> lock(mutex_);
    done_cv_.wait(lock, [this]() {
        return tasks_.empty() && active_count_.load() == 0;
    });
}

size_t WorkerPool::pending_count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return tasks_.size() + active_count_.load();
}

void WorkerPool::worker_loop() {
    while (true) {
        std::function<void()> task;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait(lock, [this]() {
                return stop_ || !tasks_.empty();
            });

            if (stop_ && tasks_.empty()) {
                return;
            }

            task = std::move(tasks_.front());
            tasks_.pop();
            ++active_count_;
        }

        task();

        {
            std::lock_guard<std::mutex> lock(mutex_);
            --active_count_;
            if (tasks_.empty() && active_count_.load() == 0) {
                done_cv_.notify_all();
            }
        }
    }
}

} // namespace science_and_theology