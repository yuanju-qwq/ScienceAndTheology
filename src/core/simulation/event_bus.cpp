#include "event_bus.hpp"

namespace science_and_theology {

EventBus::HandlerId EventBus::subscribe(GameEventType type, Handler handler) {
    std::lock_guard<std::mutex> lock(mutex_);
    HandlerId id = next_handler_id_++;
    subscribers_[type].push_back({id, std::move(handler)});
    return id;
}

void EventBus::unsubscribe(HandlerId id) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& pair : subscribers_) {
        auto& vec = pair.second;
        vec.erase(
            std::remove_if(vec.begin(), vec.end(),
                [id](const Subscription& s) { return s.id == id; }),
            vec.end());
    }
}

void EventBus::unsubscribe(GameEventType type, HandlerId id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = subscribers_.find(type);
    if (it != subscribers_.end()) {
        auto& vec = it->second;
        vec.erase(
            std::remove_if(vec.begin(), vec.end(),
                [id](const Subscription& s) { return s.id == id; }),
            vec.end());
    }
}

void EventBus::emit(const GameEvent& event) {
    std::vector<Handler> handlers;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = subscribers_.find(event.type);
        if (it != subscribers_.end()) {
            for (auto& sub : it->second) {
                handlers.push_back(sub.handler);
            }
        }
    }
    for (auto& h : handlers) {
        h(event);
    }
}

void EventBus::enqueue(const GameEvent& event) {
    std::lock_guard<std::mutex> lock(mutex_);
    deferred_queue_.push_back(event);
}

void EventBus::process_queue() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        processing_queue_.swap(deferred_queue_);
    }

    for (const auto& event : processing_queue_) {
        emit(event);
    }
    processing_queue_.clear();
}

size_t EventBus::queue_size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return deferred_queue_.size();
}

void EventBus::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    subscribers_.clear();
    deferred_queue_.clear();
    processing_queue_.clear();
}

} // namespace science_and_theology
