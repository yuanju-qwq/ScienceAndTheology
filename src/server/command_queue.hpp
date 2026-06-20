#pragma once

#include <cstdint>
#include <deque>
#include <mutex>
#include <vector>
#include <optional>
#include <condition_variable>

#include "protocol/protocol_types.hpp"

namespace science_and_theology {
namespace server {

// A serialized gameplay command queued for server-side execution.
// The payload is an opaque blob — the server core decodes it via the
// host's command executor (GDGameCommandServer in M3, native C++ later).
struct QueuedCommand {
    uint64_t player_id = 0;       // who submitted the command
    uint64_t client_tick = 0;     // optional client prediction tick
    std::vector<uint8_t> payload; // serialized command (format defined by host)
};

// Thread-safe FIFO command queue.
//
// Producer side: network receive thread(s) push commands as they arrive.
// Consumer side: the server tick thread drains all pending commands once
// per tick before advancing the simulation.
//
// This decouples network I/O from simulation timing: a flood of commands
// doesn't interrupt the tick, and a slow tick doesn't block the network.
class CommandQueue {
public:
    CommandQueue() = default;
    ~CommandQueue() = default;

    // Non-copyable, non-movable (shared across threads).
    CommandQueue(const CommandQueue&) = delete;
    CommandQueue& operator=(const CommandQueue&) = delete;

    // Push a command onto the queue (thread-safe, non-blocking).
    void push(QueuedCommand cmd);

    // Push a command by move (convenience).
    void push(uint64_t player_id, std::vector<uint8_t> payload, uint64_t client_tick = 0);

    // Drain all pending commands into `out` (thread-safe, non-blocking).
    // Clears the queue. Returns the number of commands drained.
    size_t drain_all(std::vector<QueuedCommand>& out);

    // Drain up to `max_count` commands (thread-safe, non-blocking).
    size_t drain_some(std::vector<QueuedCommand>& out, size_t max_count);

    // Blocking pop with timeout. Returns nullopt on timeout.
    std::optional<QueuedCommand> pop_blocking(int timeout_ms);

    // Returns the number of pending commands (thread-safe).
    size_t size() const;

    // Returns true if the queue is empty (thread-safe).
    bool empty() const;

    // Clears all pending commands (thread-safe).
    void clear();

private:
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::deque<QueuedCommand> queue_;
};

} // namespace server
} // namespace science_and_theology
