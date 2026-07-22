// Source-law persistence admission coverage at the authoritative server boundary.

#include "game/server/game_server_player_state.h"
#include "game/source_law/source_law_persistence_codec.h"
#include "game/tests/test_player_resource_snapshot.h"

#include "ecs/world.h"
#include "game/player/player_identity.h"

#include <gtest/gtest.h>

#include <string>
#include <utility>

namespace {

snt::game::replication::GameAuthenticatedPeer make_peer(
    snt::network::PeerId peer_id, std::string name) {
    auto identity = snt::game::make_local_name_player_identity(std::move(name));
    return {
        .peer = peer_id,
        .identity = identity ? std::move(*identity) : snt::game::PlayerIdentity{},
    };
}

}  // namespace

TEST(SourceLawServerPersistenceTest, RejectsRetiredSchemaBeforePlayerCreation) {
    snt::game::source_law::SourceLawPersistenceCodec source_law_codec;
    const auto current_organs = source_law_codec.encode({});
    ASSERT_TRUE(current_organs) << current_organs.error().format();
    ASSERT_FALSE(current_organs->payload.empty());

    snt::ecs::World world;
    auto state = snt::game::replication::GameServerPlayerState::create(
        world,
        {
            .resource_runtime_index = snt::game::test_support::player_resource_snapshot(),
            .organ_state_codec = &source_law_codec,
        });
    ASSERT_TRUE(state) << state.error().format();

    const auto peer = make_peer(108, "Source Law Schema Player");
    auto retired = (*state)->default_persistent_state();
    retired.organs = *current_organs;
    retired.organs.schema_id = "source_law_sublimation";
    EXPECT_FALSE((*state)->on_peer_authenticated(peer, retired));
    EXPECT_EQ((*state)->active_player_count(), 0U);

    auto current = (*state)->default_persistent_state();
    current.organs = *current_organs;
    ASSERT_TRUE((*state)->on_peer_authenticated(peer, current));
    const auto captured = (*state)->capture_persistent_state(peer);
    ASSERT_TRUE(captured) << captured.error().format();
    EXPECT_EQ(captured->organs, *current_organs);
    (*state)->shutdown();
}
