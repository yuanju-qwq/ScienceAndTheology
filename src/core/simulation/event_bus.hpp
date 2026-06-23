#pragma once

#include <cstdint>
#include <functional>
#include <mutex>
#include <unordered_map>
#include <vector>

#include "event_types.hpp"

namespace science_and_theology {

// Centralized event bus: subscribe/emit/enqueue model.
// Supports both immediate synchronous dispatch and deferred (queued) dispatch.
// Thread-safe — safe to emit from worker threads.
//
// Usage:
//   EventBus bus;
//   auto id = bus.subscribe(GameEventType::MACHINE_ERROR, [](const GameEvent& e) {
//       // handle error
//   });
//   bus.process_queue();  // drain deferred queue (call once per tick)
class EventBus {
public:
    using Handler = std::function<void(const GameEvent&)>;
    using HandlerId = uint64_t;

    EventBus() = default;
    ~EventBus() = default;

    // Disallow copy, allow move.
    EventBus(const EventBus&) = delete;
    EventBus& operator=(const EventBus&) = delete;
    EventBus(EventBus&&) = delete;
    EventBus& operator=(EventBus&&) = delete;

    // Subscribe to all events of a given type. Returns a handler ID for
    // unsubscription. The handler is called synchronously on emit() or
    // when process_queue() drains deferred events.
    HandlerId subscribe(GameEventType type, Handler handler);

    // Unsubscribe a previously registered handler by ID.
    void unsubscribe(HandlerId id);

    // Unsubscribe all handlers registered by the specified handler ID.
    void unsubscribe(GameEventType type, HandlerId id);

    // Emit an event immediately. All matching subscribers are called
    // synchronously on the calling thread.
    void emit(const GameEvent& event);

    // Queue an event for deferred dispatch. Call process_queue() to
    // drain and dispatch all queued events (typically at end of tick).
    void enqueue(const GameEvent& event);

    // Process all enqueued events, dispatching them to subscribers.
    // Uses double-buffering so new events can be enqueued during dispatch.
    void process_queue();

    // Returns the number of pending deferred events.
    size_t queue_size() const;

    // Clears all subscriptions and queued events.
    void clear();

private:
    struct Subscription {
        HandlerId id = 0;
        Handler handler;
    };

    std::unordered_map<GameEventType, std::vector<Subscription>> subscribers_;
    std::vector<GameEvent> deferred_queue_;
    std::vector<GameEvent> processing_queue_;
    HandlerId next_handler_id_ = 1;
    mutable std::mutex mutex_;
};

} // namespace science_and_theology
