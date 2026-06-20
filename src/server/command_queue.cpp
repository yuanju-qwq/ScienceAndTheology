#include "command_queue.hpp"

namespace science_and_theology {
namespace server {

void CommandQueue::push(QueuedCommand cmd) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        queue_.push_back(std::move(cmd));
    }
    cv_.notify_one();
}

void CommandQueue::push(uint64_t player_id,
                        std::vector<uint8_t> payload,
                        uint64_t client_tick) {
    QueuedCommand cmd;
    cmd.player_id = player_id;
    cmd.client_tick = client_tick;
    cmd.payload = std::move(payload);
    push(std::move(cmd));
}

size_t CommandQueue::drain_all(std::vector<QueuedCommand>& out) {
    std::lock_guard<std::mutex> lock(mutex_);
    const size_t count = queue_.size();
    out.reserve(out.size() + count);
    for (auto& cmd : queue_) {
        out.push_back(std::move(cmd));
    }
    queue_.clear();
    return count;
}

size_t CommandQueue::drain_some(std::vector<QueuedCommand>& out, size_t max_count) {
    std::lock_guard<std::mutex> lock(mutex_);
    size_t taken = 0;
    while (taken < max_count && !queue_.empty()) {
        out.push_back(std::move(queue_.front()));
        queue_.pop_front();
        ++taken;
    }
    return taken;
}

std::optional<QueuedCommand> CommandQueue::pop_blocking(int timeout_ms) {
    std::unique_lock<std::mutex> lock(mutex_);
    if (queue_.empty()) {
        cv_.wait_for(lock, std::chrono::milliseconds(timeout_ms),
                     [this]() { return !queue_.empty(); });
    }
    if (queue_.empty()) return std::nullopt;
    QueuedCommand cmd = std::move(queue_.front());
    queue_.pop_front();
    return cmd;
}

size_t CommandQueue::size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return queue_.size();
}

bool CommandQueue::empty() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return queue_.empty();
}

void CommandQueue::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    queue_.clear();
}

} // namespace server
} // namespace science_and_theology
