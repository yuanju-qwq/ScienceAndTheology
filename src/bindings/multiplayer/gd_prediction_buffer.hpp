#pragma once

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/variant/packed_int64_array.hpp>

#include "core/simulation/prediction_buffer.hpp"

#include <memory>

namespace science_and_theology {

// GDPredictionBuffer — Godot wrapper for PredictionBuffer.
//
// Legacy Godot integration; client prediction has no current protocol contract.
//
// Client-side prediction tracking for rollback reconciliation.
// The GDScript client uses this to track predicted commands and
// reconcile them with server confirmations.
//
// Usage in GDScript:
//   var pred_buf = GDPredictionBuffer.new()
//   # Before predicting a command:
//   var snapshot = _take_state_snapshot()
//   pred_buf.predict(client_tick)
//   _apply_prediction_locally(command)
//   net_client.submit_command({"type": "mine", "client_tick": client_tick, ...})
//
//   # When server response arrives (in sync_received signal handler):
//   if data.has("client_tick"):
//       var confirmed = pred_buf.confirm(data["client_tick"])
//       for tick in confirmed:
//           _discard_snapshot(tick)
//
//   # If server rejects (result differs from prediction):
//   var to_reapply = pred_buf.reject_and_rollback(rejected_tick)
//   _rollback_to_last_confirmed()
//   for tick in to_reapply:
//       _reapply_prediction(tick)
class GDPredictionBuffer : public godot::RefCounted {
    GDCLASS(GDPredictionBuffer, godot::RefCounted)

public:
    GDPredictionBuffer();
    ~GDPredictionBuffer() override;

    // Record a predicted command at the given client_tick.
    void predict(int64_t client_tick);

    // Confirm that the server has processed up to client_tick.
    // Returns the list of confirmed ticks (so the host can discard
    // their state snapshots).
    godot::PackedInt64Array confirm(int64_t client_tick);

    // Reject the prediction at client_tick (server disagreed).
    // Returns the list of ticks to re-apply after rollback.
    godot::PackedInt64Array reject_and_rollback(int64_t client_tick) const;

    // The last confirmed client_tick (0 if none confirmed yet).
    int64_t get_last_confirmed_tick() const;

    // Number of pending (unconfirmed) predictions.
    int64_t get_pending_count() const;

    // Returns all pending ticks (ascending).
    godot::PackedInt64Array get_pending_ticks() const;

    // Clear all predictions and reset last_confirmed_ to 0.
    // Use on teleport, dimension switch, or full resync.
    void clear();

    // Clear all pending predictions but keep last_confirmed_.
    // Use when the server sends a full snapshot that supersedes
    // all pending predictions.
    void clear_pending();

protected:
    static void _bind_methods();

private:
    std::unique_ptr<PredictionBuffer> buffer_;
};

} // namespace science_and_theology
