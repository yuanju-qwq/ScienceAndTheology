#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include "quest_def.hpp"
#include "../simulation/simulation_system.hpp"
#include "../simulation/event_bus.hpp"

namespace science_and_theology {

// ============================================================
// QuestSystem — quest progress tracking and condition evaluation
// ============================================================
//
// Manages the full lifecycle of quests:
//   1. Content registration (chapters + quest definitions)
//   2. Dependency resolution (prerequisite DAG)
//   3. Condition evaluation (driven by game events)
//   4. State transitions (LOCKED → AVAILABLE → IN_PROGRESS → COMPLETED)
//   5. Progress serialization / deserialization
//
// QuestSystem is NOT a SimulationSystem subclass because it is
// player-scoped (not chunk-scoped). It ticks once per frame
// driven by GDTickSystem or directly from GDScript.
//
// Thread safety: NOT thread-safe. All calls must be from main thread.
//
// Usage:
//   QuestSystem qs;
//   qs.register_chapter({"stone_age", "Stone Age", "pickaxe.stone", 0});
//   qs.register_quest({...});
//   qs.initialize();  // resolve initial states
//   qs.on_inventory_changed(inventory);
//   qs.tick(delta, current_tick);

class QuestSystem {
public:
    // Callback type for CUSTOM condition evaluation.
    // Returns current progress count for the condition.
    using CustomConditionEvaluator = std::function<int32_t(const std::string& quest_id,
                                                           const QuestCondition& condition)>;

    QuestSystem() = default;
    ~QuestSystem() = default;

    // Disallow copy.
    QuestSystem(const QuestSystem&) = delete;
    QuestSystem& operator=(const QuestSystem&) = delete;

    // ================================================================
    // Content registration (called from GD content layer at startup)
    // ================================================================

    // Register a chapter definition.
    void register_chapter(const ChapterDef& chapter);

    // Register a quest definition.
    // Prerequisite quest_ids are validated; missing prereqs log a warning
    // but do not prevent registration (quest stays LOCKED until prereqs exist).
    void register_quest(const QuestDef& quest);

    // Set the event bus for emitting quest events.
    void set_event_bus(EventBus* bus);

    // Set the custom condition evaluator for CUSTOM type conditions.
    void set_custom_evaluator(CustomConditionEvaluator evaluator);

    // ================================================================
    // Initialization
    // ================================================================

    // Resolve initial quest states based on registered definitions.
    // Must be called after all chapters/quests are registered.
    void initialize();

    // ================================================================
    // Tick — evaluate conditions and update states
    // ================================================================

    // Called once per frame. Evaluates conditions for IN_PROGRESS quests
    // and emits events on state changes.
    // current_tick: the game tick counter from TickSystem.
    void tick(float delta, int64_t current_tick);

    // ================================================================
    // Event-driven condition updates
    // ================================================================

    // Notify that the player inventory changed.
    // Re-evaluates HAS_ITEM conditions for all IN_PROGRESS quests.
    // inventory_has_item: callback that returns count of item_key in inventory.
    using ItemCountQuery = std::function<int32_t(const std::string& item_key)>;
    void on_inventory_changed(ItemCountQuery inventory_has_item);

    // Notify that the player crafted an item.
    // Increments CRAFT_ITEM condition counters.
    void on_item_crafted(const std::string& item_key, int32_t count = 1);

    // Notify that the player mined a block.
    // Increments MINE_BLOCK condition counters.
    void on_block_mined(const std::string& block_key, int32_t count = 1);

    // Notify that the player placed a machine.
    // Increments PLACE_MACHINE condition counters.
    void on_machine_placed(const std::string& machine_type, int32_t count = 1);

    // ================================================================
    // Query API
    // ================================================================

    // Get the state of a quest.
    QuestState get_quest_state(const std::string& quest_id) const;

    // Get the progress data for a quest.
    const QuestProgress* get_progress(const std::string& quest_id) const;

    // Get all registered chapter IDs in display order.
    std::vector<std::string> get_chapters() const;

    // Get all quest IDs in a chapter, in display order.
    std::vector<std::string> get_quests_in_chapter(const std::string& chapter_id) const;

    // Get the quest definition (immutable).
    const QuestDef* get_quest_def(const std::string& quest_id) const;

    // Get the chapter definition (immutable).
    const ChapterDef* get_chapter_def(const std::string& chapter_id) const;

    // Check if a quest is visible (not hidden, or unlocked).
    bool is_quest_visible(const std::string& quest_id) const;

    // Check if a quest's reward has been claimed.
    bool is_reward_claimed(const std::string& quest_id) const;

    // Claim the reward for a completed quest.
    // Returns true if the reward was successfully claimed.
    // Sets reward_claimed = true and emits REWARD_CLAIMED event.
    bool claim_reward(const std::string& quest_id);

    // Get the list of rewards for a quest.
    const std::vector<QuestReward>* get_quest_rewards(const std::string& quest_id) const;

    // Manually start a quest (transition AVAILABLE → IN_PROGRESS).
    // Returns true if the transition succeeded.
    bool start_quest(const std::string& quest_id);

    // Reset a repeatable quest (COMPLETED → AVAILABLE).
    // Returns true if the quest is repeatable and was reset.
    bool reset_quest(const std::string& quest_id);

    // Get total number of quests.
    size_t quest_count() const;

    // Get number of completed quests.
    size_t completed_count() const;

    // ================================================================
    // Serialization
    // ================================================================

    // Serialize all quest progress to a binary stream.
    void serialize(std::ofstream& out) const;

    // Deserialize quest progress from a binary stream.
    // Returns true on success.
    bool deserialize(std::ifstream& in);

private:
    // ================================================================
    // Internal helpers
    // ================================================================

    // Check if all prerequisites for a quest are COMPLETED.
    bool are_prerequisites_met(const std::string& quest_id) const;

    // Evaluate all conditions for a quest. Returns true if all satisfied.
    bool evaluate_conditions(const std::string& quest_id);

    // Evaluate a single condition against current progress.
    bool is_condition_satisfied(const QuestProgress& progress,
                                const QuestCondition& condition) const;

    // Transition a quest to a new state, emitting events as needed.
    void transition_state(const std::string& quest_id, QuestState new_state);

    // Update visibility for hidden quests whose prerequisites are met.
    void update_hidden_visibility();

    // Emit a quest event via the event bus (if set).
    void emit_quest_event(GameEventType type, const std::string& quest_id);

    // ================================================================
    // Data members
    // ================================================================

    // Registered content (immutable after initialize()).
    std::unordered_map<std::string, ChapterDef> chapters_;
    std::vector<std::string> chapter_order_;  // display order

    std::unordered_map<std::string, QuestDef> quests_;
    std::unordered_map<std::string, std::vector<std::string>> chapter_quests_;
    // chapter_id -> quest_ids in display order

    // Runtime progress (mutable).
    std::unordered_map<std::string, QuestProgress> progress_;

    // Event bus for emitting quest events (not owned).
    EventBus* event_bus_ = nullptr;

    // Custom condition evaluator (set from GDScript layer).
    CustomConditionEvaluator custom_evaluator_;

    // Item count query (set per on_inventory_changed call, transient).
    ItemCountQuery inventory_query_;

    // Current tick counter (updated each tick call).
    int64_t current_tick_ = 0;

    // Whether initialize() has been called.
    bool initialized_ = false;

    // Serialization format version.
    static constexpr uint8_t kQuestProgressVersion = 1;
};

} // namespace science_and_theology
