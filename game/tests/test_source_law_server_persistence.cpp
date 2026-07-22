// Source-law persistence admission coverage at the authoritative server boundary.

#include "game/server/game_server_player_state.h"
#include "game/server/game_server_source_law_service.h"
#include "game/source_law/builtin_source_law_content.h"
#include "game/source_law/source_law_persistence_codec.h"
#include "game/tests/test_player_resource_snapshot.h"

#include "ecs/world.h"
#include "game/player/player_identity.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

namespace {

snt::game::replication::GameAuthenticatedPeer make_peer(
    snt::network::PeerId peer_id, std::string name) {
    auto identity = snt::game::make_local_name_player_identity(std::move(name));
    return {
        .peer = peer_id,
        .identity = identity ? std::move(*identity) : snt::game::PlayerIdentity{},
    };
}

class RecordingSourceLawEventSink final
    : public snt::game::replication::IGameServerSourceLawEventSink {
public:
    void on_source_law_event(
        const snt::game::replication::GameAuthenticatedPeer& peer,
        const snt::game::source_law::SourceLawTransactionEvent& event) noexcept override {
        peers.push_back(peer.peer);
        events.push_back(event);
    }

    std::vector<snt::network::PeerId> peers;
    std::vector<snt::game::source_law::SourceLawTransactionEvent> events;
};

class RecordingCheckpointSink final
    : public snt::game::replication::IGameServerPlayerStateCheckpointSink {
public:
    snt::core::Expected<void> mark_player_state_dirty(
        const snt::game::replication::GameAuthenticatedPeer& peer) override {
        peers.push_back(peer.peer);
        return {};
    }

    std::vector<snt::network::PeerId> peers;
};

bool has_event_kind(
    const std::vector<snt::game::source_law::SourceLawTransactionEvent>& events,
    snt::game::source_law::SourceLawTransactionEventKind kind) {
    return std::any_of(events.begin(), events.end(), [kind](const auto& event) {
        return event.kind == kind;
    });
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

TEST(SourceLawServerRuntimeTest, AdmissionMaterializesCurrentSchemaAndCommitsOnlyAcceptedBodyChanges) {
    using snt::game::source_law::PlayerSourceLawState;
    using snt::game::source_law::SourceLawImplantRequest;
    using snt::game::source_law::SourceLawTransactionEventKind;
    using snt::game::source_law::SourceOrganSlot;

    snt::game::source_law::SourceLawPersistenceCodec codec;
    PlayerSourceLawState persisted_source;
    persisted_source.body.source_reserve_current = 10;
    persisted_source.body.source_reserve_max = 10;
    const auto encoded_initial = codec.encode(persisted_source);
    ASSERT_TRUE(encoded_initial) << encoded_initial.error().format();

    snt::ecs::World world;
    auto player_state = snt::game::replication::GameServerPlayerState::create(
        world,
        {
            .resource_runtime_index = snt::game::test_support::player_resource_snapshot(),
            .organ_state_codec = &codec,
        });
    ASSERT_TRUE(player_state) << player_state.error().format();
    const auto peer = make_peer(109, "Source Law Runtime Player");
    auto persistent = (*player_state)->default_persistent_state();
    persistent.organs = *encoded_initial;
    ASSERT_TRUE((*player_state)->on_peer_authenticated(peer, persistent));

    auto content = snt::game::source_law::make_builtin_source_law_content_v0_1();
    ASSERT_TRUE(content) << content.error().format();
    RecordingSourceLawEventSink event_sink;
    auto service = snt::game::replication::GameServerSourceLawService::create(
        **player_state,
        {
            .content = std::move(*content),
            .event_sink = &event_sink,
        });
    ASSERT_TRUE(service) << service.error().format();
    RecordingCheckpointSink checkpoint_sink;
    (*service)->set_checkpoint_sink(&checkpoint_sink);
    ASSERT_TRUE((*service)->on_player_activated(peer));

    const auto admitted_payload = (*player_state)->organ_state_for_peer(peer);
    ASSERT_TRUE(admitted_payload) << admitted_payload.error().format();
    EXPECT_EQ(admitted_payload->schema_id, snt::game::source_law::kSourceLawPlayerOrganSchemaId);
    const auto admitted = codec.decode(*admitted_payload);
    ASSERT_TRUE(admitted) << admitted.error().format();
    EXPECT_EQ(admitted->body.source_reserve_current, 10);
    EXPECT_EQ(admitted->body.mana_current, 0);

    const auto implanted = (*service)->implant(peer, {
        .slot = SourceOrganSlot::kHeart,
        .organ_definition_id = "snt:rock_core_heart",
        .quality_id = "snt:quality.common",
        .source_reserve_cost = 4,
    });
    ASSERT_TRUE(implanted) << implanted.error().format();
    EXPECT_EQ(implanted->body.body_revision, 1U);
    EXPECT_EQ(checkpoint_sink.peers, std::vector<snt::network::PeerId>{peer.peer});
    EXPECT_TRUE(has_event_kind(event_sink.events, SourceLawTransactionEventKind::kOrganImplanted));
    EXPECT_TRUE(std::all_of(event_sink.peers.begin(), event_sink.peers.end(),
                            [peer](const snt::network::PeerId event_peer) {
                                return event_peer == peer.peer;
                            }));

    const auto committed_payload = (*player_state)->organ_state_for_peer(peer);
    ASSERT_TRUE(committed_payload) << committed_payload.error().format();
    const auto committed = codec.decode(*committed_payload);
    ASSERT_TRUE(committed) << committed.error().format();
    EXPECT_EQ(committed->body.source_reserve_current, 6);
    ASSERT_TRUE(committed->body.organs[static_cast<size_t>(SourceOrganSlot::kHeart)]);
    EXPECT_EQ(committed->body.organs[static_cast<size_t>(SourceOrganSlot::kHeart)]->definition_id,
              "snt:rock_core_heart");

    const auto tuned = (*service)->tune_organ(peer, {
        .slot = SourceOrganSlot::kHeart,
        .tuning_definition_id = "snt:tuning.rock_core_purification",
    });
    ASSERT_TRUE(tuned) << tuned.error().format();
    EXPECT_EQ(tuned->body.body_revision, 2U);
    EXPECT_EQ(checkpoint_sink.peers,
              (std::vector<snt::network::PeerId>{peer.peer, peer.peer}));
    EXPECT_TRUE(has_event_kind(event_sink.events, SourceLawTransactionEventKind::kOrganTuned));

    const auto tuned_payload = (*player_state)->organ_state_for_peer(peer);
    ASSERT_TRUE(tuned_payload) << tuned_payload.error().format();
    const auto tuned_state = codec.decode(*tuned_payload);
    ASSERT_TRUE(tuned_state) << tuned_state.error().format();
    EXPECT_EQ(tuned_state->body.source_reserve_current, 3);
    ASSERT_TRUE(tuned_state->body.organs[static_cast<size_t>(SourceOrganSlot::kHeart)]);
    EXPECT_TRUE(std::find(tuned_state->body.organs[static_cast<size_t>(SourceOrganSlot::kHeart)]
                              ->tuning_tags.begin(),
                          tuned_state->body.organs[static_cast<size_t>(SourceOrganSlot::kHeart)]
                              ->tuning_tags.end(),
                          "snt:tuning.rock_core.purified") !=
                tuned_state->body.organs[static_cast<size_t>(SourceOrganSlot::kHeart)]
                    ->tuning_tags.end());

    const auto payload_before_rejection = *tuned_payload;
    const size_t events_before_rejection = event_sink.events.size();
    const auto rejected = (*service)->implant(peer, {
        .slot = SourceOrganSlot::kBone,
        .organ_definition_id = "snt:rock_core_heart",
        .quality_id = "snt:quality.common",
    });
    EXPECT_FALSE(rejected);
    const auto payload_after_rejection = (*player_state)->organ_state_for_peer(peer);
    ASSERT_TRUE(payload_after_rejection) << payload_after_rejection.error().format();
    EXPECT_EQ(*payload_after_rejection, payload_before_rejection);
    EXPECT_EQ(event_sink.events.size(), events_before_rejection);
    EXPECT_EQ(checkpoint_sink.peers.size(), 2U);

    (*service)->on_player_deactivated(peer, "test complete");
    (*player_state)->on_peer_disconnected(peer, "test complete");
    (*player_state)->shutdown();
}

TEST(SourceLawServerRuntimeTest, BodyTransactionsInvalidateCompiledPersonalSpellCaches) {
    using snt::game::source_law::PlayerSourceLawState;
    using snt::game::source_law::SourceLawAnchorPathRequest;
    using snt::game::source_law::SourceLawImplantRequest;
    using snt::game::source_law::SourceLawRemoveOrganRequest;
    using snt::game::source_law::SourceLawScheduleRequest;
    using snt::game::source_law::SourceLawSpellProgramId;
    using snt::game::source_law::SourceOrganSlot;

    snt::game::source_law::SourceLawPersistenceCodec codec;
    PlayerSourceLawState persisted_source;
    persisted_source.body.source_reserve_current = 100;
    persisted_source.body.source_reserve_max = 100;
    const auto encoded_initial = codec.encode(persisted_source);
    ASSERT_TRUE(encoded_initial) << encoded_initial.error().format();

    snt::ecs::World world;
    auto player_state = snt::game::replication::GameServerPlayerState::create(
        world,
        {
            .resource_runtime_index = snt::game::test_support::player_resource_snapshot(),
            .organ_state_codec = &codec,
        });
    ASSERT_TRUE(player_state) << player_state.error().format();
    const auto peer = make_peer(110, "Source Law Spell Runtime Player");
    auto persistent = (*player_state)->default_persistent_state();
    persistent.organs = *encoded_initial;
    ASSERT_TRUE((*player_state)->on_peer_authenticated(peer, persistent));

    auto content = snt::game::source_law::make_builtin_source_law_content_v0_1();
    ASSERT_TRUE(content) << content.error().format();
    auto service = snt::game::replication::GameServerSourceLawService::create(
        **player_state, {.content = std::move(*content)});
    ASSERT_TRUE(service) << service.error().format();
    ASSERT_TRUE((*service)->on_player_activated(peer));

    const std::vector<std::pair<SourceOrganSlot, std::string>> organs{
        {SourceOrganSlot::kHeart, "snt:rock_core_heart"},
        {SourceOrganSlot::kBone, "snt:purified_crystal_bone"},
        {SourceOrganSlot::kBlood, "snt:mineral_blood"},
        {SourceOrganSlot::kSkin, "snt:geomantic_skin"},
    };
    for (const auto& [slot, definition_id] : organs) {
        const auto implanted = (*service)->implant(peer, {
            .slot = slot,
            .organ_definition_id = definition_id,
            .quality_id = "snt:quality.common",
        });
        ASSERT_TRUE(implanted) << implanted.error().format();
    }
    ASSERT_TRUE((*service)->anchor_path(peer, {
        .path_id = "snt:path.sand_armor",
    }));
    ASSERT_TRUE((*service)->schedule_circuits(peer, {
        .primary_system_id = "snt:system.sand_armor.circulatory",
        .coordinating_system_ids = {"snt:system.sand_armor.musculoskeletal"},
    }));

    const SourceLawSpellProgramId program_id{.value = 801};
    const auto copied = (*service)->copy_spell_preset(
        peer, program_id, "snt:spell_graph.sand_armor.signature_mantle_charge", "Runtime mantle");
    ASSERT_TRUE(copied) << copied.error().format();
    const auto compiled = (*service)->compile_spell_program(peer, program_id);
    ASSERT_TRUE(compiled) << compiled.error().format();
    EXPECT_TRUE(compiled->report.is_compilable);
    const auto cached = (*service)->cached_spell_program(peer, program_id);
    ASSERT_TRUE(cached) << cached.error().format();
    ASSERT_TRUE(cached->has_value());
    EXPECT_EQ(cached->value().body_revision, compiled->body_revision);

    ASSERT_TRUE((*service)->tune_organ(peer, {
        .slot = SourceOrganSlot::kHeart,
        .tuning_definition_id = "snt:tuning.rock_core_purification",
    }));
    const auto invalidated_by_tuning = (*service)->cached_spell_program(peer, program_id);
    ASSERT_TRUE(invalidated_by_tuning) << invalidated_by_tuning.error().format();
    EXPECT_FALSE(invalidated_by_tuning->has_value());

    const auto recompiled = (*service)->compile_spell_program(peer, program_id);
    ASSERT_TRUE(recompiled) << recompiled.error().format();
    EXPECT_TRUE(recompiled->report.is_compilable);

    ASSERT_TRUE((*service)->remove_organ(peer, {
        .slot = SourceOrganSlot::kBlood,
    }));
    const auto invalidated = (*service)->cached_spell_program(peer, program_id);
    ASSERT_TRUE(invalidated) << invalidated.error().format();
    EXPECT_FALSE(invalidated->has_value());

    const auto persisted = (*player_state)->organ_state_for_peer(peer);
    ASSERT_TRUE(persisted) << persisted.error().format();
    const auto decoded = codec.decode(*persisted);
    ASSERT_TRUE(decoded) << decoded.error().format();
    ASSERT_EQ(decoded->personal_spell_programs.size(), 1U);
    EXPECT_EQ(decoded->personal_spell_programs.front().program_id, program_id);

    (*service)->on_player_deactivated(peer, "test complete");
    (*player_state)->on_peer_disconnected(peer, "test complete");
    (*player_state)->shutdown();
}
