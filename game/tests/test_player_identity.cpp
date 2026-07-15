// Stable player-identity, local profile, and bootstrap tests.

#include "game/player/local_player_profile_store.h"
#include "game/player/player_identity.h"

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <utility>

namespace {

class RecordingLocalNamePrompt final : public snt::game::ILocalPlayerNamePrompt {
public:
    snt::core::Expected<std::string> request_local_player_name() override {
        ++call_count;
        return next_name;
    }

    std::string next_name = "Local Alex";
    int call_count = 0;
};

class RecordingSteamIdentityProvider final : public snt::game::ISteamPlayerIdentityProvider {
public:
    snt::core::Expected<std::optional<snt::game::SteamIdentityInfo>> current_identity() override {
        ++call_count;
        return identity;
    }

    std::optional<snt::game::SteamIdentityInfo> identity;
    int call_count = 0;
};

std::filesystem::path make_identity_user_root() {
    const auto root = std::filesystem::temp_directory_path() /
        ("snt_player_identity_" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    return root;
}

}  // namespace

TEST(PlayerIdentityTest, SeparatesSteamAndLocalAccountsWithTheSameDisplayName) {
    auto steam = snt::game::make_steam_player_identity(76561198000000001ull, "Alex");
    auto local = snt::game::make_local_name_player_identity("Alex");
    ASSERT_TRUE(steam) << steam.error().format();
    ASSERT_TRUE(local) << local.error().format();

    EXPECT_EQ(steam->display_name, local->display_name);
    EXPECT_EQ(steam->account_id, "steam:76561198000000001");
    EXPECT_EQ(local->account_id, "local-name:Alex");
    EXPECT_NE(steam->account_id, local->account_id);
}

TEST(PlayerIdentityTest, UsesTheSameLocalNameAsTheSameLocalAccount) {
    auto first = snt::game::make_local_name_player_identity("SharedName");
    auto second = snt::game::make_local_name_player_identity("SharedName");
    ASSERT_TRUE(first) << first.error().format();
    ASSERT_TRUE(second) << second.error().format();
    EXPECT_EQ(first->account_id, second->account_id);
}

TEST(LocalPlayerProfileStoreTest, PersistsAndRestoresLocalIdentity) {
    const auto user_root = make_identity_user_root();
    snt::game::LocalPlayerProfileStore store(user_root.string());
    auto identity = snt::game::make_local_name_player_identity("Persisted Player");
    ASSERT_TRUE(identity) << identity.error().format();
    ASSERT_TRUE(store.save(*identity));

    auto loaded = store.load();
    ASSERT_TRUE(loaded) << loaded.error().format();
    ASSERT_TRUE(loaded->has_value());
    EXPECT_EQ((*loaded)->account_id, identity->account_id);
    EXPECT_EQ((*loaded)->display_name, identity->display_name);

    std::error_code error;
    std::filesystem::remove_all(user_root, error);
    EXPECT_FALSE(error) << error.message();
}

TEST(LocalPlayerProfileStoreTest, RejectsCorruptPrimaryAndRecoversBackupOnlyWhenPrimaryIsMissing) {
    const auto user_root = make_identity_user_root();
    snt::game::LocalPlayerProfileStore store(user_root.string());
    auto identity = snt::game::make_local_name_player_identity("Recovered Player");
    ASSERT_TRUE(identity) << identity.error().format();
    ASSERT_TRUE(store.save(*identity));

    const auto primary_path = user_root / "profile" / "local_player_identity.bin";
    auto backup_path = primary_path;
    backup_path += ".bak";
    std::error_code error;
    std::filesystem::copy_file(primary_path, backup_path,
                               std::filesystem::copy_options::overwrite_existing, error);
    ASSERT_FALSE(error) << error.message();
    {
        std::ofstream corrupt_primary(primary_path, std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(corrupt_primary.is_open());
        corrupt_primary.write("bad", 3);
        ASSERT_TRUE(corrupt_primary.good());
    }

    auto corrupt_load = store.load();
    ASSERT_FALSE(corrupt_load);

    RecordingLocalNamePrompt prompt;
    auto corrupt_bootstrap = snt::game::resolve_client_player_identity(user_root.string(), prompt);
    ASSERT_FALSE(corrupt_bootstrap);
    EXPECT_EQ(prompt.call_count, 0);

    std::filesystem::remove(primary_path, error);
    ASSERT_FALSE(error) << error.message();
    auto recovered = store.load();
    ASSERT_TRUE(recovered) << recovered.error().format();
    ASSERT_TRUE(recovered->has_value());
    EXPECT_EQ((*recovered)->account_id, identity->account_id);

    std::filesystem::remove_all(user_root, error);
    EXPECT_FALSE(error) << error.message();
}

TEST(ClientPlayerIdentityBootstrapTest, PrefersSteamAndOtherwisePersistsPromptedLocalName) {
    const auto user_root = make_identity_user_root();
    RecordingLocalNamePrompt prompt;
    RecordingSteamIdentityProvider steam;

    steam.identity = snt::game::SteamIdentityInfo{
        .steam_id = 76561198000000002ull,
        .display_name = "Steam Alex",
    };
    auto steam_identity = snt::game::resolve_client_player_identity(
        user_root.string(), prompt, &steam);
    ASSERT_TRUE(steam_identity) << steam_identity.error().format();
    EXPECT_EQ(steam_identity->account_id, "steam:76561198000000002");
    EXPECT_EQ(prompt.call_count, 0);

    steam.identity.reset();
    prompt.next_name = "Local Alex";
    auto first_local = snt::game::resolve_client_player_identity(
        user_root.string(), prompt, &steam);
    ASSERT_TRUE(first_local) << first_local.error().format();
    EXPECT_EQ(first_local->account_id, "local-name:Local Alex");
    EXPECT_EQ(prompt.call_count, 1);

    prompt.next_name = "Should Not Be Requested";
    auto restored_local = snt::game::resolve_client_player_identity(
        user_root.string(), prompt, &steam);
    ASSERT_TRUE(restored_local) << restored_local.error().format();
    EXPECT_EQ(restored_local->account_id, "local-name:Local Alex");
    EXPECT_EQ(prompt.call_count, 1);

    std::error_code error;
    std::filesystem::remove_all(user_root, error);
    EXPECT_FALSE(error) << error.message();
}
