#include "prediction_buffer.hpp"

#include <algorithm>

namespace science_and_theology {

void PredictionBuffer::predict(uint64_t client_tick) {
    // Ticks should be monotonically increasing. Ignore ticks that are
    // already confirmed or already pending.
    if (client_tick <= last_confirmed_) return;
    if (!pending_ticks_.empty() && client_tick <= pending_ticks_.back()) {
        // Allow re-predicting the same tick (idempotent), but don't
        // add duplicates.
        if (std::find(pending_ticks_.begin(), pending_ticks_.end(),
                      client_tick) != pending_ticks_.end()) {
            return;
        }
        // Out-of-order tick — insert and re-sort.
        pending_ticks_.push_back(client_tick);
        std::sort(pending_ticks_.begin(), pending_ticks_.end());
    } else {
        pending_ticks_.push_back(client_tick);
    }
}

std::vector<uint64_t> PredictionBuffer::confirm(uint64_t client_tick) {
    std::vector<uint64_t> confirmed;

    // Move all pending ticks <= client_tick to the confirmed list.
    auto it = std::lower_bound(pending_ticks_.begin(),
                               pending_ticks_.end(),
                               client_tick + 1);
    confirmed.assign(pending_ticks_.begin(), it);
    pending_ticks_.erase(pending_ticks_.begin(), it);

    // Advance last_confirmed_ to the max of (current, client_tick, last
    // confirmed pending tick).
    if (client_tick > last_confirmed_) {
        last_confirmed_ = client_tick;
    }
    if (!confirmed.empty() && confirmed.back() > last_confirmed_) {
        last_confirmed_ = confirmed.back();
    }

    return confirmed;
}

std::vector<uint64_t> PredictionBuffer::reject_and_rollback(
    uint64_t client_tick) const {
    // Return all pending ticks >= client_tick (the host rolls back to
    // last_confirmed_ and re-applies these).
    std::vector<uint64_t> to_reapply;
    auto it = std::lower_bound(pending_ticks_.begin(),
                               pending_ticks_.end(),
                               client_tick);
    to_reapply.assign(it, pending_ticks_.end());
    return to_reapply;
}

void PredictionBuffer::clear() {
    pending_ticks_.clear();
    last_confirmed_ = 0;
}

void PredictionBuffer::clear_pending() {
    pending_ticks_.clear();
}

} // namespace science_and_theology
