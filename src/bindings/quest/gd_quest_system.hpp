#pragma once

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/callable.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/string.hpp>

#include <vector>

#include "core/quest/quest_system.hpp"

namespace science_and_theology {

// ============================================================
// GDQuestSystem — GDExtension binding for QuestSystem
// ============================================================
//
// Node that owns and drives the C++ QuestSystem.
// Bridges quest events to Godot signals for UI updates.
//
// Usage in GDScript:
//   var quest_sys = GDQuestSystem.new()
//   quest_sys.register_chapter("stone_age", "Stone Age", "pickaxe.stone", 0)
//   quest_sys.register_quest({...})
//   quest_sys.initialize()
//
//   func _process(delta):
//       quest_sys.tick(delta)
//
// Signals:
//   quest_unlocked(quest_id: String)
//   quest_completed(quest_id: String)
//   quest_progress_changed(quest_id: String)
//   reward_claimed(quest_id: String)

class GDQuestSystem : public godot::Node {
    GDCLASS(GDQuestSystem, godot::Node)

public:
    GDQuestSystem();
    ~GDQuestSystem() override;

    void _ready() override;
    void _process(double delta) override;

    // ================================================================
    // Content registration (called from GDScript content layer)
    // ================================================================

    // Register a chapter.
    // chapter_id: unique identifier e.g. "stone_age"
    // title: display name
    // icon_key: item_key for tab icon
    // order_index: display order (lower = first)
    void register_chapter(const godot::String& chapter_id,
                          const godot::String& title,
                          const godot::String& icon_key,
                          int64_t order_index);

    // Register a quest from a Dictionary.
    // Expected keys:
    //   "quest_id": String (required)
    //   "chapter_id": String (required)
    //   "title": String (required)
    //   "description": String (optional, default "")
    //   "icon_key": String (optional, default "")
    //   "order_index": int (optional, default 0)
    //   "prerequisites": PackedStringArray (optional, default [])
    //   "conditions": Array[Dictionary] (optional, default [])
    //     Each condition dict keys:
    //       "type": int (QuestConditionType enum value)
    //       "target_key": String
    //       "target_count": int (optional, default 1)
    //       "condition_key": String (optional, auto-generated if empty)
    //   "rewards": Array[Dictionary] (optional, default [])
    //     Each reward dict keys:
    //       "type": int (QuestRewardType enum value)
    //       "item_key": String (for ITEM/SELECT_ONE)
    //       "count": int (optional, default 1)
    //       "options": PackedStringArray (for SELECT_ONE)
    //       "unlock_quest_id": String (for UNLOCK_QUEST)
    //   "is_hidden": bool (optional, default false)
    //   "can_repeat": bool (optional, default false)
    //   "auto_start": bool (optional, default true)
    void register_quest(const godot::Dictionary& quest_data);

    // ================================================================
    // Initialization
    // ================================================================

    // Resolve initial quest states. Must be called after all
    // chapters and quests are registered.
    void initialize();

    // ================================================================
    // Event-driven condition updates
    // ================================================================

    // Notify that the player inventory changed.
    // inventory_has_item: Callable that takes item_key (String) and
    //   returns count (int). Typically: func(key): return inventory.count_item(key)
    void on_inventory_changed(const godot::Callable& inventory_has_item);

    // Notify that the player crafted an item.
    void on_item_crafted(const godot::String& item_key, int32_t count);

    // Notify that the player mined a block.
    void on_block_mined(const godot::String& block_key, int32_t count);

    // Notify that the player placed a machine.
    void on_machine_placed(const godot::String& machine_type, int32_t count);

    // ================================================================
    // Query API
    // ================================================================

    // Get the state of a quest as int (0=LOCKED, 1=AVAILABLE, 2=IN_PROGRESS, 3=COMPLETED).
    int64_t get_quest_state(const godot::String& quest_id) const;

    // Get progress data for a quest as Dictionary.
    // Keys: "quest_id", "state" (int), "progress" (Dictionary: condition_key -> count),
    //       "reward_claimed" (bool), "completed_tick" (int), "completion_count" (int)
    godot::Dictionary get_quest_progress(const godot::String& quest_id) const;

    // Get all chapter IDs in display order.
    godot::Array get_chapters() const;

    // Get all quest IDs in a chapter, in display order.
    godot::Array get_quests_in_chapter(const godot::String& chapter_id) const;

    // Get quest definition as Dictionary.
    godot::Dictionary get_quest_def(const godot::String& quest_id) const;

    // Get chapter definition as Dictionary.
    godot::Dictionary get_chapter_def(const godot::String& chapter_id) const;

    // Check if a quest is visible (not hidden, or unlocked).
    bool is_quest_visible(const godot::String& quest_id) const;

    // Check if a quest's reward has been claimed.
    bool is_reward_claimed(const godot::String& quest_id) const;

    // Claim the reward for a completed quest. Returns true on success.
    bool claim_reward(const godot::String& quest_id);

    // Get the list of rewards for a quest as Array of Dictionaries.
    godot::Array get_quest_rewards(const godot::String& quest_id) const;

    // Manually start a quest (AVAILABLE -> IN_PROGRESS). Returns true on success.
    bool start_quest(const godot::String& quest_id);

    // Reset a repeatable quest. Returns true on success.
    bool reset_quest(const godot::String& quest_id);

    // Get total number of registered quests.
    int64_t quest_count() const;

    // Get number of completed quests.
    int64_t completed_count() const;

    // ================================================================
    // Serialization
    // ================================================================

    // Serialize all quest progress to a PackedByteArray.
    godot::PackedByteArray serialize() const;

    // Deserialize quest progress from a PackedByteArray. Returns true on success.
    bool deserialize(const godot::PackedByteArray& data);

    // ================================================================
    // Tick control
    // ================================================================

    // Set the tick counter source (typically from GDTickSystem).
    void set_tick_counter(int64_t tick);
    int64_t get_tick_counter() const;

    // Access to underlying C++ QuestSystem (for GDTickSystem integration).
    QuestSystem& quest_system() { return quest_system_; }
    const QuestSystem& quest_system() const { return quest_system_; }

protected:
    static void _bind_methods();

private:
    // Convert a QuestDef to Dictionary.
    godot::Dictionary quest_def_to_dict(const QuestDef& def) const;

    // Convert a ChapterDef to Dictionary.
    godot::Dictionary chapter_def_to_dict(const ChapterDef& def) const;

    // Convert a QuestProgress to Dictionary.
    godot::Dictionary quest_progress_to_dict(const QuestProgress& prog) const;

    // Convert a QuestReward to Dictionary.
    godot::Dictionary quest_reward_to_dict(const QuestReward& reward) const;

    // Parse a condition Dictionary into QuestCondition.
    QuestCondition parse_condition(const godot::Dictionary& cond_dict) const;

    // Parse a reward Dictionary into QuestReward.
    QuestReward parse_reward(const godot::Dictionary& reward_dict) const;

    // Subscribe to EventBus for quest events.
    void subscribe_to_events();
    void unsubscribe_from_events();

    QuestSystem quest_system_;
    int64_t tick_counter_ = 0;

    // EventBus subscription IDs.
    std::vector<EventBus::HandlerId> event_subscriptions_;

    // Cached inventory query callable from GDScript.
    godot::Callable inventory_callable_;
};

} // namespace science_and_theology
