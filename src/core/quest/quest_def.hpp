#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace science_and_theology {

// ============================================================
// Quest condition types
// ============================================================
//
// Each condition type maps to a specific game event or state check.
// CUSTOM conditions are evaluated via GDScript callback.

enum class QuestConditionType : uint8_t {
    HAS_ITEM = 0,       // Player has item in inventory
    CRAFT_ITEM = 1,     // Player has crafted item (tracked)
    MINE_BLOCK = 2,     // Player has mined block type
    PLACE_MACHINE = 3,  // Player has placed machine type
    REACH_TICK = 4,     // Game tick threshold reached
    CUSTOM = 255        // GDScript-evaluated condition
};

// ============================================================
// Quest reward types
// ============================================================

enum class QuestRewardType : uint8_t {
    ITEM = 0,           // Grant item(s)
    SELECT_ONE = 1,     // Choose one from multiple options
    UNLOCK_QUEST = 2    // Unlock a hidden quest
};

// ============================================================
// Quest runtime state
// ============================================================

enum class QuestState : uint8_t {
    LOCKED = 0,         // Prerequisites not met
    AVAILABLE = 1,      // Can be started (prerequisites met)
    IN_PROGRESS = 2,    // Actively tracking progress
    COMPLETED = 3,      // Done
};

// ============================================================
// QuestCondition — a single completion condition for a quest
// ============================================================

struct QuestCondition {
    QuestConditionType type = QuestConditionType::HAS_ITEM;
    std::string target_key;      // item_key / block_key / machine_type
    int32_t target_count = 1;

    // Unique key within a quest for progress tracking.
    // Convention: "{type_short}.{target_key}" e.g. "has.ingot.iron"
    std::string condition_key;
};

// ============================================================
// QuestReward — a single reward entry
// ============================================================

struct QuestReward {
    QuestRewardType type = QuestRewardType::ITEM;
    std::string item_key;        // For ITEM / SELECT_ONE
    int32_t count = 1;
    std::vector<std::string> options;  // For SELECT_ONE: list of item_keys
    std::string unlock_quest_id;       // For UNLOCK_QUEST
};

// ============================================================
// QuestDef — immutable quest definition (content data)
// ============================================================
//
// Registered at startup from GDScript content layer.
// Never modified at runtime.

struct QuestDef {
    std::string quest_id;        // e.g. "stone_age.flint_pickaxe"
    std::string chapter_id;      // e.g. "stone_age"
    std::string title_key;       // Localization key or display text
    std::string desc_key;        // Localization key or description text
    std::string icon_key;        // item_key for icon display
    int32_t order_index = 0;     // Display order within chapter

    // Dependency DAG: quest_ids that must be COMPLETED before this quest
    // becomes AVAILABLE.
    std::vector<std::string> prerequisite_ids;

    // Completion conditions (all must be satisfied).
    std::vector<QuestCondition> conditions;

    // Rewards granted on completion.
    std::vector<QuestReward> rewards;

    // Hidden quests are invisible until unlocked by prerequisite or reward.
    bool is_hidden = false;

    // Repeatable quests can be completed multiple times.
    bool can_repeat = false;

    // Auto-start: quest transitions to IN_PROGRESS as soon as AVAILABLE.
    bool auto_start = true;
};

// ============================================================
// ChapterDef — chapter (tab) definition
// ============================================================

struct ChapterDef {
    std::string chapter_id;      // e.g. "stone_age"
    std::string title_key;       // Display name
    std::string icon_key;        // item_key for tab icon
    int32_t order_index = 0;     // Display order
};

// ============================================================
// QuestProgress — mutable per-quest runtime state
// ============================================================

struct QuestProgress {
    std::string quest_id;
    QuestState state = QuestState::LOCKED;

    // Per-condition progress counters: condition_key -> current count.
    std::unordered_map<std::string, int32_t> progress_counters;

    // Whether the reward has been claimed.
    bool reward_claimed = false;

    // Tick when the quest was completed (0 if not completed).
    int64_t completed_tick = 0;

    // For repeatable quests: how many times completed.
    int32_t completion_count = 0;
};

} // namespace science_and_theology
