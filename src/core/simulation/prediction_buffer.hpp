#pragma once

#include <cstdint>
#include <vector>

namespace science_and_theology {

// PredictionBuffer — client-side prediction tracking for rollback
// reconciliation.
//
// Design: docs/多人游戏系统设计.md §6.2 M6, §3.4 命令协议升级
//
// High-frequency actions (movement, mining) are predicted locally by
// the client and tagged with a monotonically increasing client_tick.
// The server echoes client_tick in its response. The client uses this
// to confirm or roll back predictions:
//
//   1. predict(tick): record that a prediction was made at client_tick.
//      The host should snapshot any state needed for rollback BEFORE
//      calling this.
//   2. confirm(tick): the server has processed up to client_tick.
//      All pending predictions with tick <= client_tick are confirmed
//      and discarded. Returns the confirmed ticks so the host can
//      discard their snapshots.
//   3. reject_and_rollback(tick): the server disagreed with the
//      prediction at client_tick. The host should roll back to the
//      last confirmed state and re-apply all pending predictions
//      with tick > client_tick. Returns the ticks to re-apply.
//
// This class only tracks tick metadata; the actual state snapshots
// and re-application are handled by the host (GDScript).
//
// Thread safety: not thread-safe. The host must call all methods from
// the same thread (typically the main thread / _process thread).
class PredictionBuffer {
public:
    PredictionBuffer() = default;
    ~PredictionBuffer() = default;

    PredictionBuffer(const PredictionBuffer&) = delete;
    PredictionBuffer& operator=(const PredictionBuffer&) = delete;
    PredictionBuffer(PredictionBuffer&&) = default;
    PredictionBuffer& operator=(PredictionBuffer&&) = default;

    // Record a predicted command at the given client_tick.
    // The tick must be strictly greater than the last confirmed tick
    // and not already pending. Ticks should be monotonically increasing.
    void predict(uint64_t client_tick);

    // Confirm that the server has processed up to client_tick.
    // Removes all pending predictions with tick <= client_tick.
    // Returns the list of confirmed ticks (ascending) so the host can
    // discard their state snapshots.
    std::vector<uint64_t> confirm(uint64_t client_tick);

    // Reject the prediction at client_tick (server disagreed).
    // The host should:
    //   1. Roll back to the state at last_confirmed_tick.
    //   2. Re-apply all predictions with tick > client_tick.
    // Returns the list of ticks to re-apply (ascending), including
    // client_tick itself if it is still pending.
    // The pending list is NOT modified — the host re-applies the
    // predictions and they remain pending until confirmed.
    std::vector<uint64_t> reject_and_rollback(uint64_t client_tick) const;

    // The last confirmed client_tick (0 if none confirmed yet).
    uint64_t last_confirmed_tick() const { return last_confirmed_; }

    // Number of pending (unconfirmed) predictions.
    size_t pending_count() const { return pending_ticks_.size(); }

    // Returns all pending ticks (ascending).
    const std::vector<uint64_t>& pending_ticks() const { return pending_ticks_; }

    // Clear all predictions and reset last_confirmed_ to 0.
    // Use this on teleport, dimension switch, or full resync.
    void clear();

    // Clear all predictions but keep last_confirmed_.
    // Use this when the server sends a full snapshot that supersedes
    // all pending predictions.
    void clear_pending();

private:
    uint64_t last_confirmed_ = 0;
    std::vector<uint64_t> pending_ticks_;  // sorted ascending
};

} // namespace science_and_theology
