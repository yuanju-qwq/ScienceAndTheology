// Current-format SNTP player-state persistence coverage.

#include "game/world/save/player_state_persistence.h"

#include <gtest/gtest.h>

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <string>

namespace {

std::filesystem::path make_player_state_save_dir() {
    const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto directory = std::filesystem::temp_directory_path() /
                           ("snt_player_state_" + std::to_string(nonce));
    std::filesystem::create_directories(directory);
    return directory;
}

snt::game::GamePlayerPersistentState make_player_state() {
    snt::game::GamePlayerPersistentState state;
    state.position = {
        .dimension_id = "overworld",
        .position = {.x = 34, .y = 72, .z = -19},
    };
    state.respawn_point = snt::game::GamePlayerWorldPosition{
        .dimension_id = "overworld",
        .position = {.x = 8, .y = 65, .z = 4},
    };
    state.inventory = {
        .slots = {
            snt::game::GamePlayerItemStack::item("iron_ingot", 12),
            snt::game::GamePlayerItemStack::item(
                "steel_pickaxe", 1, {}, "durability=238"),
            {},
        },
        .max_slots = 3,
        .max_stack_size = 64,
    };
    state.equipment.slots[static_cast<size_t>(snt::game::GamePlayerEquipmentSlot::kMainHand)] =
        snt::game::GamePlayerItemStack::item("steel_sword", 1, {}, "durability=91");
    state.equipment.slots[static_cast<size_t>(snt::game::GamePlayerEquipmentSlot::kChest)] =
        snt::game::GamePlayerItemStack::item("iron_chestplate", 1);
    state.organs = {
        .schema_id = "source_law_sublimation",
        .schema_version = 1,
        .payload = {std::byte{0x10}, std::byte{0x20}, std::byte{0x30}},
    };
    state.ground_loot_claim_receipts = {11, 29};
    return state;
}

}  // namespace

TEST(GameSavePlayerStatePersistenceTest, RoundTripsFixedSlotsEquipmentRespawnAndOrgans) {
    const auto save_dir = make_player_state_save_dir();
    snt::game::GameSavePlayerStatePersistence persistence(save_dir.string());
    const auto source = make_player_state();

    auto missing = persistence.load_player_state("local-name:Player State");
    ASSERT_TRUE(missing) << missing.error().format();
    EXPECT_FALSE(missing->has_value());

    ASSERT_TRUE(persistence.save_player_state("local-name:Player State", source));
    auto restored = persistence.load_player_state("local-name:Player State");
    ASSERT_TRUE(restored) << restored.error().format();
    ASSERT_TRUE(restored->has_value());
    EXPECT_EQ((**restored).position.dimension_id, source.position.dimension_id);
    EXPECT_EQ((**restored).position.position.x, source.position.position.x);
    EXPECT_EQ((**restored).position.position.y, source.position.position.y);
    EXPECT_EQ((**restored).position.position.z, source.position.position.z);
    ASSERT_TRUE((**restored).respawn_point.has_value());
    EXPECT_EQ((**restored).respawn_point->dimension_id, source.respawn_point->dimension_id);
    EXPECT_EQ((**restored).respawn_point->position.x, source.respawn_point->position.x);
    EXPECT_EQ((**restored).inventory, source.inventory);
    EXPECT_EQ((**restored).equipment, source.equipment);
    EXPECT_EQ((**restored).organs, source.organs);
    EXPECT_EQ((**restored).ground_loot_claim_receipts,
              source.ground_loot_claim_receipts);

    std::error_code error;
    std::filesystem::remove_all(save_dir, error);
    EXPECT_FALSE(error) << error.message();
}

TEST(GameSavePlayerStatePersistenceTest, RejectsCorruptPrimaryInsteadOfReadingBackup) {
    const auto save_dir = make_player_state_save_dir();
    snt::game::GameSavePlayerStatePersistence persistence(save_dir.string());
    const std::string account_id = "local-name:Corrupt Player";
    ASSERT_TRUE(persistence.save_player_state(account_id, make_player_state()));

    const std::filesystem::path players_dir = save_dir / "players";
    const auto primary = *std::filesystem::directory_iterator(players_dir);
    const auto backup = primary.path().string() + ".bak";
    std::filesystem::copy_file(primary.path(), backup,
                               std::filesystem::copy_options::overwrite_existing);
    {
        std::ofstream corrupt(primary.path(), std::ios::binary | std::ios::trunc);
        corrupt.write("broken", 6);
    }

    auto restored = persistence.load_player_state(account_id);
    ASSERT_FALSE(restored);
    EXPECT_EQ(restored.error().code(), snt::core::ErrorCode::kInvalidArgument);

    std::error_code error;
    std::filesystem::remove_all(save_dir, error);
    EXPECT_FALSE(error) << error.message();
}
