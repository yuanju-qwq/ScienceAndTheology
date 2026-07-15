// M6: Client prediction + rollback tests.
// Legacy prediction test; client prediction has no current protocol contract.
//
// Tests:
//   1. predict/confirm basic flow — single tick confirmed
//   2. confirm batch — multiple pending ticks confirmed at once
//   3. reject_and_rollback — returns ticks to re-apply
//   4. clear — resets last_confirmed_ to 0
//   5. clear_pending — keeps last_confirmed_, drops pending
//   6. Edge cases: duplicate ticks, out-of-order ticks, already-confirmed
//   7. Monotonic last_confirmed_ — confirm with older tick doesn't regress
//   8. reject_and_rollback does not modify pending list

#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "core/simulation/prediction_buffer.hpp"

using namespace science_and_theology;

namespace {

int g_failures = 0;

void check(bool cond, const std::string& msg) {
    if (cond) return;
    std::cerr << "  FAIL: " << msg << '\n';
    ++g_failures;
}

template <typename T>
bool contains(const std::vector<T>& vec, const T& val) {
    for (const auto& v : vec) {
        if (v == val) return true;
    }
    return false;
}

// --- Test 1: predict/confirm basic flow ---
bool test_predict_confirm_basic() {
    std::cerr << "[M6] test_predict_confirm_basic" << '\n';

    PredictionBuffer buf;
    check(buf.pending_count() == 0, "initial pending_count should be 0");
    check(buf.last_confirmed_tick() == 0, "initial last_confirmed should be 0");

    buf.predict(1);
    check(buf.pending_count() == 1, "after predict(1), pending_count should be 1");
    check(buf.pending_ticks().size() == 1, "pending_ticks size should be 1");
    check(buf.pending_ticks()[0] == 1, "pending_ticks[0] should be 1");

    auto confirmed = buf.confirm(1);
    check(confirmed.size() == 1, "confirm(1) should return 1 tick");
    check(confirmed[0] == 1, "confirmed[0] should be 1");
    check(buf.pending_count() == 0, "after confirm, pending_count should be 0");
    check(buf.last_confirmed_tick() == 1, "last_confirmed should be 1");

    return g_failures == 0;
}

// --- Test 2: confirm batch (multiple pending ticks confirmed at once) ---
bool test_confirm_batch() {
    std::cerr << "[M6] test_confirm_batch" << '\n';

    PredictionBuffer buf;

    // Predict ticks 1, 2, 3
    buf.predict(1);
    buf.predict(2);
    buf.predict(3);
    check(buf.pending_count() == 3, "after 3 predicts, pending_count should be 3");

    // Confirm up to tick 2 — should confirm 1 and 2, leave 3 pending
    auto confirmed = buf.confirm(2);
    check(confirmed.size() == 2, "confirm(2) should return 2 ticks");
    check(contains(confirmed, uint64_t(1)), "confirmed should contain 1");
    check(contains(confirmed, uint64_t(2)), "confirmed should contain 2");
    check(!contains(confirmed, uint64_t(3)), "confirmed should NOT contain 3");
    check(buf.pending_count() == 1, "after confirm(2), pending should be 1");
    check(buf.pending_ticks()[0] == 3, "remaining pending should be 3");
    check(buf.last_confirmed_tick() == 2, "last_confirmed should be 2");

    // Now confirm tick 3
    confirmed = buf.confirm(3);
    check(confirmed.size() == 1, "confirm(3) should return 1 tick");
    check(confirmed[0] == 3, "confirmed[0] should be 3");
    check(buf.pending_count() == 0, "after confirm(3), pending should be 0");
    check(buf.last_confirmed_tick() == 3, "last_confirmed should be 3");

    return g_failures == 0;
}

// --- Test 3: reject_and_rollback ---
bool test_reject_and_rollback() {
    std::cerr << "[M6] test_reject_and_rollback" << '\n';

    PredictionBuffer buf;

    // Confirm tick 1 first (establishes a baseline)
    buf.predict(1);
    buf.confirm(1);
    check(buf.last_confirmed_tick() == 1, "last_confirmed should be 1");

    // Predict 2, 3, 4
    buf.predict(2);
    buf.predict(3);
    buf.predict(4);
    check(buf.pending_count() == 3, "pending should be 3");

    // Server rejects tick 3 — host must roll back to last_confirmed (1)
    // and re-apply predictions with tick >= 3 (i.e., 3 and 4).
    // Tick 2 is implicitly dropped (it's before the rejected tick and
    // was not confirmed, so it's lost — the host should also discard
    // its snapshot when rolling back).
    auto to_reapply = buf.reject_and_rollback(3);
    check(to_reapply.size() == 2, "reject_and_rollback(3) should return 2 ticks");
    check(contains(to_reapply, uint64_t(3)), "should contain 3");
    check(contains(to_reapply, uint64_t(4)), "should contain 4");
    check(!contains(to_reapply, uint64_t(2)), "should NOT contain 2 (before rejected)");

    // Pending list is NOT modified by reject_and_rollback
    check(buf.pending_count() == 3, "pending should still be 3 after reject");
    check(buf.last_confirmed_tick() == 1, "last_confirmed should still be 1");

    return g_failures == 0;
}

// --- Test 4: clear ---
bool test_clear() {
    std::cerr << "[M6] test_clear" << '\n';

    PredictionBuffer buf;
    buf.predict(1);
    buf.predict(2);
    buf.confirm(1);
    check(buf.last_confirmed_tick() == 1, "last_confirmed should be 1");
    check(buf.pending_count() == 1, "pending should be 1");

    buf.clear();
    check(buf.pending_count() == 0, "after clear, pending should be 0");
    check(buf.last_confirmed_tick() == 0, "after clear, last_confirmed should be 0");

    return g_failures == 0;
}

// --- Test 5: clear_pending ---
bool test_clear_pending() {
    std::cerr << "[M6] test_clear_pending" << '\n';

    PredictionBuffer buf;
    buf.predict(1);
    buf.predict(2);
    buf.confirm(1);
    check(buf.last_confirmed_tick() == 1, "last_confirmed should be 1");
    check(buf.pending_count() == 1, "pending should be 1");

    buf.clear_pending();
    check(buf.pending_count() == 0, "after clear_pending, pending should be 0");
    check(buf.last_confirmed_tick() == 1, "after clear_pending, last_confirmed should still be 1");

    return g_failures == 0;
}

// --- Test 6: edge cases ---
bool test_edge_cases() {
    std::cerr << "[M6] test_edge_cases" << '\n';

    PredictionBuffer buf;

    // Predicting a tick <= 0 (when last_confirmed_ is 0) should be ignored
    buf.predict(0);
    check(buf.pending_count() == 0, "predict(0) should be ignored (<= last_confirmed)");

    // Predict tick 1, confirm it
    buf.predict(1);
    buf.confirm(1);

    // Predicting a tick <= last_confirmed should be ignored
    buf.predict(1);
    check(buf.pending_count() == 0, "predict(1) after confirm(1) should be ignored");

    // Duplicate predict (same tick already pending)
    buf.predict(5);
    buf.predict(5);
    check(buf.pending_count() == 1, "duplicate predict(5) should not add twice");

    // Out-of-order predict (tick less than the last pending)
    buf.predict(3);
    check(buf.pending_count() == 2, "out-of-order predict(3) should be added");
    // Pending list should remain sorted
    check(buf.pending_ticks()[0] == 3, "pending should be sorted: [0]=3");
    check(buf.pending_ticks()[1] == 5, "pending should be sorted: [1]=5");

    // Confirm up to 4 — should confirm tick 3, leave 5 pending
    auto confirmed = buf.confirm(4);
    check(confirmed.size() == 1, "confirm(4) should confirm 1 tick (3)");
    check(confirmed[0] == 3, "confirmed[0] should be 3");
    check(buf.pending_count() == 1, "pending should be 1 (tick 5)");
    check(buf.last_confirmed_tick() == 4, "last_confirmed should be 4");

    return g_failures == 0;
}

// --- Test 7: monotonic last_confirmed_ ---
bool test_monotonic_last_confirmed() {
    std::cerr << "[M6] test_monotonic_last_confirmed" << '\n';

    PredictionBuffer buf;

    buf.predict(10);
    auto confirmed = buf.confirm(10);
    check(confirmed.size() == 1, "confirm(10) should return 1 tick");
    check(buf.last_confirmed_tick() == 10, "last_confirmed should be 10");

    // Confirm with an older tick — should NOT regress last_confirmed_
    confirmed = buf.confirm(5);
    check(confirmed.empty(), "confirm(5) should return 0 ticks (none pending <= 5)");
    check(buf.last_confirmed_tick() == 10, "last_confirmed should still be 10 (not regressed)");

    return g_failures == 0;
}

// --- Test 8: reject_and_rollback does not modify pending list ---
bool test_rollback_no_modify() {
    std::cerr << "[M6] test_rollback_no_modify" << '\n';

    PredictionBuffer buf;
    buf.predict(1);
    buf.predict(2);
    buf.predict(3);
    buf.predict(4);

    auto pending_before = buf.pending_ticks();
    auto pending_count_before = buf.pending_count();

    (void)buf.reject_and_rollback(2);

    auto pending_after = buf.pending_ticks();
    check(buf.pending_count() == pending_count_before,
          "pending_count should be unchanged after reject_and_rollback");
    check(pending_before == pending_after,
          "pending_ticks vector should be unchanged after reject_and_rollback");

    return g_failures == 0;
}

// --- Test 9: realistic prediction flow ---
// Simulates a typical client prediction scenario:
//   - Client predicts movement at ticks 1..5
//   - Server confirms up to tick 3
//   - Server rejects tick 4 (e.g., collision)
//   - Client rolls back and re-applies tick 5
bool test_realistic_flow() {
    std::cerr << "[M6] test_realistic_flow" << '\n';

    PredictionBuffer buf;

    // Predict ticks 1..5
    for (uint64_t t = 1; t <= 5; ++t) {
        buf.predict(t);
    }
    check(buf.pending_count() == 5, "should have 5 pending predictions");

    // Server confirms up to tick 3
    auto confirmed = buf.confirm(3);
    check(confirmed.size() == 3, "confirm(3) should return 3 ticks");
    check(buf.last_confirmed_tick() == 3, "last_confirmed should be 3");
    check(buf.pending_count() == 2, "should have 2 pending (4, 5)");

    // Server rejects tick 4 — client rolls back to state at tick 3
    // and re-applies tick 5 (and tick 4 itself, which the host may
    // choose to re-predict or discard).
    auto to_reapply = buf.reject_and_rollback(4);
    check(to_reapply.size() == 2, "should return 2 ticks to re-apply (4, 5)");
    check(to_reapply[0] == 4, "to_reapply[0] should be 4");
    check(to_reapply[1] == 5, "to_reapply[1] should be 5");

    // After rollback, pending list is unchanged (host re-applies and
    // predictions remain pending until confirmed).
    check(buf.pending_count() == 2, "pending should still be 2 after rollback");

    // Host re-applies predictions and server eventually confirms tick 5
    confirmed = buf.confirm(5);
    check(confirmed.size() == 2, "confirm(5) should return 2 ticks (4, 5)");
    check(buf.pending_count() == 0, "pending should be 0");
    check(buf.last_confirmed_tick() == 5, "last_confirmed should be 5");

    return g_failures == 0;
}

} // namespace

int main() {
    int failures_before = g_failures;

    test_predict_confirm_basic();
    test_confirm_batch();
    test_reject_and_rollback();
    test_clear();
    test_clear_pending();
    test_edge_cases();
    test_monotonic_last_confirmed();
    test_rollback_no_modify();
    test_realistic_flow();

    if (g_failures != failures_before) {
        std::cerr << "[M6] FAILED: " << (g_failures - failures_before)
                  << " checks failed" << '\n';
        return 1;
    }

    std::cerr << "[M6] All tests passed" << '\n';
    return 0;
}
