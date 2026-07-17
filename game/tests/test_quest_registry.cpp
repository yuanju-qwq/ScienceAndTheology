// Game-owned quest progress, hot-reload, and save-boundary tests.

#include "game/client/game_content_registry.h"
#include "game/quest/quest_registry.h"
#include "game/world/save/quest_progress_persistence.h"

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace {

using snt::game::GameContentRegistry;
using snt::game::IQuestRewardSink;
using snt::game::QuestDefinition;
using snt::game::QuestObjectiveDefinition;
using snt::game::QuestObjectiveKind;
using snt::game::QuestProgressEvent;
using snt::game::QuestItemReward;
using snt::game::QuestRegistry;
using snt::game::QuestRewardDefinition;
using snt::game::QuestRewardKind;
using snt::game::QuestState;
using snt::game::GameSaveQuestProgressPersistence;

class RecordingQuestRewardSink final : public IQuestRewardSink {
public:
    snt::core::Expected<void> grant_item_rewards(
        std::string_view player_id, std::string_view quest_id,
        std::span<const QuestItemReward> rewards) override {
        player_ids.emplace_back(player_id);
        quest_ids.emplace_back(quest_id);
        grants.emplace_back(rewards.begin(), rewards.end());
        return {};
    }

    std::vector<std::string> player_ids;
    std::vector<std::string> quest_ids;
    std::vector<std::vector<QuestItemReward>> grants;
};

QuestDefinition make_craft_quest(std::string id, int32_t required_count) {
    QuestDefinition definition;
    definition.id = std::move(id);
    definition.title = "Craft iron";
    definition.description = "Craft enough iron ingots";
    definition.objectives = {{
        .id = "craft.iron",
        .kind = QuestObjectiveKind::kCraftItem,
        .target_id = "iron_ingot",
        .required_count = required_count,
    }};
    return definition;
}

std::filesystem::path make_quest_progress_save_dir() {
    const auto directory = std::filesystem::temp_directory_path() /
        ("snt_quest_progress_" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::remove_all(directory);
    return directory;
}

std::filesystem::path find_quest_progress_file(const std::filesystem::path& save_dir) {
    const auto players_dir = save_dir / "players";
    for (const auto& entry : std::filesystem::directory_iterator(players_dir)) {
        if (entry.is_regular_file()) return entry.path();
    }
    return {};
}

}  // namespace

TEST(QuestRegistryTest, AutomaticallyActivatesProgressesCompletesAndUnlocksPrerequisites) {
    GameContentRegistry content;
    QuestDefinition smelt = make_craft_quest("stone_age.smelt_iron", 2);
    QuestDefinition forge = make_craft_quest("stone_age.forge_tool", 1);
    forge.prerequisites = {smelt.id};
    ASSERT_TRUE(content.register_builtin_quest(std::move(smelt)));
    ASSERT_TRUE(content.register_builtin_quest(std::move(forge)));

    QuestRegistry quests(content);
    ASSERT_TRUE(quests.tick(1));
    const auto* smelt_progress = quests.find_progress("local", "stone_age.smelt_iron");
    ASSERT_EQ(smelt_progress, nullptr);

    ASSERT_TRUE(quests.record_progress(
        "local", {.kind = QuestObjectiveKind::kCraftItem, .target_id = "iron_ingot", .amount = 1}, 3));
    smelt_progress = quests.find_progress("local", "stone_age.smelt_iron");
    ASSERT_NE(smelt_progress, nullptr);
    EXPECT_EQ(smelt_progress->objective_counts.at("craft.iron"), 1);
    EXPECT_EQ(smelt_progress->state, QuestState::kInProgress);

    ASSERT_TRUE(quests.record_progress(
        "local", {.kind = QuestObjectiveKind::kCraftItem, .target_id = "iron_ingot", .amount = 1}, 4));
    smelt_progress = quests.find_progress("local", "stone_age.smelt_iron");
    ASSERT_NE(smelt_progress, nullptr);
    EXPECT_EQ(smelt_progress->state, QuestState::kCompleted);
    EXPECT_EQ(smelt_progress->completed_tick, 4u);
    EXPECT_EQ(smelt_progress->completion_count, 1u);

    const auto* forge_progress = quests.find_progress("local", "stone_age.forge_tool");
    ASSERT_NE(forge_progress, nullptr);
    EXPECT_EQ(forge_progress->state, QuestState::kInProgress);
}

TEST(QuestRegistryTest, KeepsProgressAcrossDefinitionReload) {
    GameContentRegistry content;
    constexpr snt::game::ScriptId kScript = 71;
    ASSERT_TRUE(content.register_script_quest(kScript, make_craft_quest("p7.reload.quest", 4)));

    QuestRegistry quests(content);
    ASSERT_TRUE(quests.record_progress(
        "local", {.kind = QuestObjectiveKind::kCraftItem, .target_id = "iron_ingot", .amount = 2}, 2));

    ASSERT_TRUE(content.begin_reload(kScript));
    QuestDefinition reloaded = make_craft_quest("p7.reload.quest", 4);
    reloaded.title = "Reloaded quest title";
    ASSERT_TRUE(content.register_script_quest(kScript, std::move(reloaded)));
    ASSERT_TRUE(content.commit_reload(kScript));

    ASSERT_TRUE(quests.tick(3));
    const auto* progress = quests.find_progress("local", "p7.reload.quest");
    ASSERT_NE(progress, nullptr);
    EXPECT_EQ(progress->state, QuestState::kInProgress);
    EXPECT_EQ(progress->objective_counts.at("craft.iron"), 2);

    ASSERT_TRUE(quests.record_progress(
        "local", {.kind = QuestObjectiveKind::kCraftItem, .target_id = "iron_ingot", .amount = 2}, 4));
    progress = quests.find_progress("local", "p7.reload.quest");
    ASSERT_NE(progress, nullptr);
    EXPECT_EQ(progress->state, QuestState::kCompleted);
}

TEST(QuestRegistryTest, AutomaticallyActivatesInventoryAndReachTickObjectives) {
    GameContentRegistry content;
    QuestDefinition definition;
    definition.id = "p7.auto.inventory";
    definition.title = "Prepare supplies";
    definition.description = "Carry wood until the first day passes";
    definition.objectives = {
        {
            .id = "inventory.wood",
            .kind = QuestObjectiveKind::kAcquireItem,
            .target_id = "oak_log",
            .required_count = 3,
        },
        {
            .id = "time.first_day",
            .kind = QuestObjectiveKind::kReachTick,
            .target_id = "",
            .required_count = 5,
        },
    };
    ASSERT_TRUE(content.register_builtin_quest(std::move(definition)));

    QuestRegistry quests(content);
    ASSERT_TRUE(quests.update_inventory("local", [](std::string_view item_id) {
        return item_id == "oak_log" ? 3 : 0;
    }, 4));
    const auto* progress = quests.find_progress("local", "p7.auto.inventory");
    ASSERT_NE(progress, nullptr);
    EXPECT_EQ(progress->state, QuestState::kInProgress);
    EXPECT_EQ(progress->objective_counts.at("inventory.wood"), 3);
    EXPECT_EQ(progress->objective_counts.at("time.first_day"), 4);

    ASSERT_TRUE(quests.tick(5));
    progress = quests.find_progress("local", "p7.auto.inventory");
    ASSERT_NE(progress, nullptr);
    EXPECT_EQ(progress->state, QuestState::kCompleted);
    EXPECT_EQ(progress->completed_tick, 5u);
}

TEST(QuestRegistryTest, RejectsInvalidStructuredQuestDefinitions) {
    GameContentRegistry content;
    QuestDefinition duplicate_prerequisite = make_craft_quest("p7.invalid.prereq", 1);
    duplicate_prerequisite.prerequisites = {"p7.other", "p7.other"};
    EXPECT_FALSE(content.register_builtin_quest(std::move(duplicate_prerequisite)));

    QuestDefinition invalid_kind = make_craft_quest("p7.invalid.kind", 1);
    invalid_kind.objectives.front().kind = static_cast<QuestObjectiveKind>(255);
    EXPECT_FALSE(content.register_builtin_quest(std::move(invalid_kind)));
}

TEST(QuestRegistryTest, ResetsRepeatableQuestWithoutLosingCompletionHistory) {
    GameContentRegistry content;
    QuestDefinition definition = make_craft_quest("p7.repeatable.quest", 1);
    definition.repeatable = true;
    ASSERT_TRUE(content.register_builtin_quest(std::move(definition)));

    QuestRegistry quests(content);
    ASSERT_TRUE(quests.record_progress(
        "local", {.kind = QuestObjectiveKind::kCraftItem, .target_id = "iron_ingot", .amount = 1}, 2));
    auto* progress = quests.find_progress("local", "p7.repeatable.quest");
    ASSERT_NE(progress, nullptr);
    ASSERT_EQ(progress->state, QuestState::kCompleted);
    EXPECT_EQ(progress->completion_count, 1u);

    ASSERT_TRUE(quests.reset_repeatable("local", "p7.repeatable.quest", 3));
    progress = quests.find_progress("local", "p7.repeatable.quest");
    ASSERT_NE(progress, nullptr);
    EXPECT_EQ(progress->state, QuestState::kInProgress);
    EXPECT_EQ(progress->completion_count, 1u);
    EXPECT_EQ(progress->objective_counts.at("craft.iron"), 0);

    ASSERT_TRUE(quests.record_progress(
        "local", {.kind = QuestObjectiveKind::kCraftItem, .target_id = "iron_ingot", .amount = 1}, 5));
    progress = quests.find_progress("local", "p7.repeatable.quest");
    ASSERT_NE(progress, nullptr);
    EXPECT_EQ(progress->state, QuestState::kCompleted);
    EXPECT_EQ(progress->completion_count, 2u);
}

TEST(QuestRegistryTest, RequiresRewardSinkBeforeClaimingItemRewardsAndUnlocksQuest) {
    GameContentRegistry content;
    QuestDefinition gate = make_craft_quest("p7.reward.gate", 100);
    QuestDefinition unlocked = make_craft_quest("p7.reward.unlocked", 1);
    unlocked.prerequisites = {gate.id};
    QuestDefinition rewarded = make_craft_quest("p7.reward.source", 1);
    rewarded.rewards = {
        {.kind = QuestRewardKind::kItem, .target_id = "wrought_iron", .count = 2},
        {.kind = QuestRewardKind::kUnlockQuest, .target_id = unlocked.id, .count = 1},
    };
    ASSERT_TRUE(content.register_builtin_quest(std::move(gate)));
    ASSERT_TRUE(content.register_builtin_quest(std::move(unlocked)));
    ASSERT_TRUE(content.register_builtin_quest(std::move(rewarded)));

    QuestRegistry quests(content);
    ASSERT_TRUE(quests.record_progress(
        "player:reward",
        {.kind = QuestObjectiveKind::kCraftItem, .target_id = "iron_ingot", .amount = 1}, 2));
    const auto* source = quests.find_progress("player:reward", "p7.reward.source");
    ASSERT_NE(source, nullptr);
    ASSERT_EQ(source->state, QuestState::kCompleted);
    EXPECT_FALSE(source->reward_claimed);
    const auto* initially_locked = quests.find_progress("player:reward", "p7.reward.unlocked");
    ASSERT_NE(initially_locked, nullptr);
    EXPECT_EQ(initially_locked->state, QuestState::kLocked);

    EXPECT_FALSE(quests.claim_reward("player:reward", "p7.reward.source", 3));
    source = quests.find_progress("player:reward", "p7.reward.source");
    ASSERT_NE(source, nullptr);
    EXPECT_FALSE(source->reward_claimed);
    const auto* still_locked = quests.find_progress("player:reward", "p7.reward.unlocked");
    ASSERT_NE(still_locked, nullptr);
    EXPECT_EQ(still_locked->state, QuestState::kLocked);

    RecordingQuestRewardSink reward_sink;
    quests.set_reward_sink(&reward_sink);
    ASSERT_TRUE(quests.claim_reward("player:reward", "p7.reward.source", 4));
    source = quests.find_progress("player:reward", "p7.reward.source");
    ASSERT_NE(source, nullptr);
    EXPECT_TRUE(source->reward_claimed);
    const auto* unlocked_progress = quests.find_progress("player:reward", "p7.reward.unlocked");
    ASSERT_NE(unlocked_progress, nullptr);
    EXPECT_EQ(unlocked_progress->state, QuestState::kInProgress);
    ASSERT_EQ(reward_sink.player_ids.size(), 1u);
    EXPECT_EQ(reward_sink.player_ids.front(), "player:reward");
    ASSERT_EQ(reward_sink.quest_ids.size(), 1u);
    EXPECT_EQ(reward_sink.quest_ids.front(), "p7.reward.source");
    ASSERT_EQ(reward_sink.grants.size(), 1u);
    ASSERT_EQ(reward_sink.grants.front().size(), 1u);
    EXPECT_EQ(reward_sink.grants.front().front().item_id, "wrought_iron");
    EXPECT_EQ(reward_sink.grants.front().front().count, 2);
    EXPECT_FALSE(quests.claim_reward("player:reward", "p7.reward.source", 5));
}

TEST(QuestRegistryTest, RestoresStableProgressSnapshotWithoutDefinitionsOwningState) {
    GameContentRegistry content;
    ASSERT_TRUE(content.register_builtin_quest(make_craft_quest("p7.snapshot.quest", 3)));

    QuestRegistry source(content);
    ASSERT_TRUE(source.record_progress(
        "player:42", {.kind = QuestObjectiveKind::kCraftItem, .target_id = "iron_ingot", .amount = 2}, 2));
    const auto snapshot = source.snapshot_progress("player:42");
    ASSERT_EQ(snapshot.size(), 1u);

    QuestRegistry restored(content);
    ASSERT_TRUE(restored.restore_progress("player:42", snapshot));
    const auto* progress = restored.find_progress("player:42", "p7.snapshot.quest");
    ASSERT_NE(progress, nullptr);
    EXPECT_EQ(progress->state, QuestState::kInProgress);
    EXPECT_EQ(progress->objective_counts.at("craft.iron"), 2);

    ASSERT_TRUE(restored.record_progress(
        "player:42", {.kind = QuestObjectiveKind::kCraftItem, .target_id = "iron_ingot", .amount = 1}, 3));
    progress = restored.find_progress("player:42", "p7.snapshot.quest");
    ASSERT_NE(progress, nullptr);
    EXPECT_EQ(progress->state, QuestState::kCompleted);
}

TEST(QuestRegistryTest, PersistsPlayerProgressAcrossFileBackedRestart) {
    const auto save_dir = make_quest_progress_save_dir();

    GameContentRegistry content;
    ASSERT_TRUE(content.register_builtin_quest(make_craft_quest("p7.file.quest", 3)));
    GameSaveQuestProgressPersistence persistence(save_dir.string());

    QuestRegistry source(content);
    ASSERT_TRUE(source.record_progress(
        "player:42", {.kind = QuestObjectiveKind::kCraftItem, .target_id = "iron_ingot", .amount = 2}, 2));
    ASSERT_TRUE(source.save_player_progress("player:42", persistence));

    QuestRegistry restarted(content);
    ASSERT_TRUE(restarted.load_player_progress("player:42", persistence));
    const auto* progress = restarted.find_progress("player:42", "p7.file.quest");
    ASSERT_NE(progress, nullptr);
    EXPECT_EQ(progress->state, QuestState::kInProgress);
    EXPECT_EQ(progress->objective_counts.at("craft.iron"), 2);

    ASSERT_TRUE(restarted.record_progress(
        "player:42", {.kind = QuestObjectiveKind::kCraftItem, .target_id = "iron_ingot", .amount = 1}, 3));
    progress = restarted.find_progress("player:42", "p7.file.quest");
    ASSERT_NE(progress, nullptr);
    EXPECT_EQ(progress->state, QuestState::kCompleted);

    std::error_code error;
    std::filesystem::remove_all(save_dir, error);
    EXPECT_FALSE(error) << error.message();
}

TEST(QuestRegistryTest, RejectsQuestProgressWithTrailingBytes) {
    const auto save_dir = make_quest_progress_save_dir();
    GameSaveQuestProgressPersistence persistence(save_dir.string());
    const std::vector<snt::game::QuestProgressRecord> records{{
        .quest_id = "p7.file.strict",
        .state = QuestState::kInProgress,
        .objective_counts = {{"craft.iron", 1}},
    }};
    ASSERT_TRUE(persistence.save_player_progress("player:strict", records));

    const auto progress_file = find_quest_progress_file(save_dir);
    ASSERT_FALSE(progress_file.empty());
    {
        std::ofstream output(progress_file, std::ios::binary | std::ios::app);
        ASSERT_TRUE(output.is_open());
        output.put(static_cast<char>(0xFF));
        ASSERT_TRUE(output.good());
    }

    const auto loaded = persistence.load_player_progress("player:strict");
    EXPECT_FALSE(loaded);

    std::error_code error;
    std::filesystem::remove_all(save_dir, error);
    EXPECT_FALSE(error) << error.message();
}

TEST(QuestRegistryTest, RecoversQuestProgressFromBackupWhenPrimaryIsMissing) {
    const auto save_dir = make_quest_progress_save_dir();
    GameSaveQuestProgressPersistence persistence(save_dir.string());
    const std::vector<snt::game::QuestProgressRecord> records{{
        .quest_id = "p7.file.recovery",
        .state = QuestState::kCompleted,
        .completion_count = 1,
    }};
    ASSERT_TRUE(persistence.save_player_progress("player:recovery", records));

    const auto primary_file = find_quest_progress_file(save_dir);
    ASSERT_FALSE(primary_file.empty());
    auto backup_file = primary_file;
    backup_file += ".bak";
    std::error_code rename_error;
    std::filesystem::rename(primary_file, backup_file, rename_error);
    ASSERT_FALSE(rename_error) << rename_error.message();

    const auto loaded = persistence.load_player_progress("player:recovery");
    ASSERT_TRUE(loaded) << loaded.error().format();
    ASSERT_EQ(loaded->size(), 1u);
    EXPECT_EQ(loaded->front().quest_id, "p7.file.recovery");
    EXPECT_EQ(loaded->front().state, QuestState::kCompleted);

    std::error_code error;
    std::filesystem::remove_all(save_dir, error);
    EXPECT_FALSE(error) << error.message();
}
