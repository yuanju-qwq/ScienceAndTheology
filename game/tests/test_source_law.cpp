// Regression coverage for the game-owned source-law V0.1 data base.

#include <gtest/gtest.h>

#include "game/source_law/builtin_source_law_content.h"
#include "game/source_law/source_law_evaluator.h"
#include "game/source_law/source_law_persistence_codec.h"
#include "game/source_law/source_law_spell_compiler.h"
#include "game/source_law/source_law_transaction_service.h"

#include <algorithm>
#include <optional>
#include <string>
#include <utility>

namespace {

using snt::game::source_law::CreatureSourceLawState;
using snt::game::source_law::ElementalReactionReport;
using snt::game::source_law::OrganInstance;
using snt::game::source_law::PlayerSourceLawState;
using snt::game::source_law::SourceBodyStage;
using snt::game::source_law::SourceLawBodyEvaluator;
using snt::game::source_law::SourceLawBodyState;
using snt::game::source_law::SourceLawContentSnapshot;
using snt::game::source_law::SourceLawEvaluation;
using snt::game::source_law::SourceLawImplantRequest;
using snt::game::source_law::SourceLawPersistenceCodec;
using snt::game::source_law::SourceLawSpellCompiler;
using snt::game::source_law::SourceLawSpellGraphKind;
using snt::game::source_law::SourceLawSpellNodeKind;
using snt::game::source_law::SourceLawSpellProgramId;
using snt::game::source_law::SourceLawSpellProgramService;
using snt::game::source_law::SourceLawRemoveOrganRequest;
using snt::game::source_law::SourceLawSystemReport;
using snt::game::source_law::SourceLawSystemState;
using snt::game::source_law::SourceLawTransactionEventKind;
using snt::game::source_law::SourceLawTransactionService;
using snt::game::source_law::SourceOrganSlot;
using snt::game::source_law::kSourceLawPlayerOrganSchemaVersion;
using snt::game::source_law::make_builtin_source_law_content_v0_1;

OrganInstance organ(std::string definition_id) {
    return {
        .definition_id = std::move(definition_id),
        .quality_id = "snt:quality.common",
    };
}

SourceLawBodyState make_sand_armor_body() {
    SourceLawBodyState body;
    body.source_reserve_current = 100;
    body.source_reserve_max = 100;
    body.stability = 90.0F;
    body.mutation = 5.0F;
    body.organs[static_cast<size_t>(SourceOrganSlot::kHeart)] = organ("snt:rock_core_heart");
    body.organs[static_cast<size_t>(SourceOrganSlot::kBone)] =
        organ("snt:purified_crystal_bone");
    body.organs[static_cast<size_t>(SourceOrganSlot::kBlood)] = organ("snt:mineral_blood");
    body.organs[static_cast<size_t>(SourceOrganSlot::kSkin)] = organ("snt:geomantic_skin");
    return body;
}

const SourceLawSystemReport* find_system(const SourceLawEvaluation& evaluation,
                                         const std::string& id) {
    const auto found = std::find_if(evaluation.systems.begin(), evaluation.systems.end(),
                                    [&id](const auto& report) {
        return report.system_id == id;
    });
    return found == evaluation.systems.end() ? nullptr : &*found;
}

bool has_id(const std::vector<std::string>& values, const std::string& id) {
    return std::find(values.begin(), values.end(), id) != values.end();
}

SourceLawContentSnapshot builtin_content() {
    auto content = make_builtin_source_law_content_v0_1();
    if (!content) {
        ADD_FAILURE() << content.error().format();
        return {};
    }
    return std::move(*content);
}

}  // namespace

TEST(SourceLawContentTest, BuiltinV01DeclaresTheEightSlotSharedRockLizardTemplate) {
    const SourceLawContentSnapshot content = builtin_content();

    ASSERT_NE(content.find_organ("snt:rock_core_heart"), nullptr);
    ASSERT_NE(content.find_organ("snt:mineral_viscera"), nullptr);
    ASSERT_NE(content.find_system("snt:system.sand_armor.circulatory"), nullptr);
    ASSERT_NE(content.find_system("snt:system.sand_armor.musculoskeletal"), nullptr);
    ASSERT_NE(content.find_intrinsic("snt:intrinsic.sand_armor.pressure_shell"), nullptr);
    ASSERT_NE(content.find_hybrid_link("snt:hybrid.sand_armor.mantle_charge"), nullptr);
    ASSERT_NE(content.find_spell_graph("snt:spell_graph.sand_armor.signature_mantle_charge"),
              nullptr);
    ASSERT_NE(content.find_tool_spell_assembly("snt:tool.rock_lizard_excavator"), nullptr);
    ASSERT_NE(content.find_path("snt:path.sand_armor"), nullptr);
    ASSERT_NE(content.find_body_template("snt:template.rock_lizard"), nullptr);
    ASSERT_NE(content.find_creature_body("snt:creature.rock_lizard"), nullptr);
    ASSERT_NE(content.integration_definition(), nullptr);
}

TEST(SourceLawEvaluatorTest, SandArmorLoopsCloseAndPrimaryCircuitRemainsValid) {
    const SourceLawContentSnapshot content = builtin_content();
    SourceLawBodyState body = make_sand_armor_body();
    body.active_path_id = "snt:path.sand_armor";
    body.circuit_schedule.current_primary_circuit_system_id =
        "snt:system.sand_armor.circulatory";

    const SourceLawEvaluation evaluation = SourceLawBodyEvaluator::evaluate(content, body);
    const SourceLawSystemReport* circulatory = find_system(
        evaluation, "snt:system.sand_armor.circulatory");
    const SourceLawSystemReport* musculoskeletal = find_system(
        evaluation, "snt:system.sand_armor.musculoskeletal");
    ASSERT_NE(circulatory, nullptr);
    ASSERT_NE(musculoskeletal, nullptr);
    EXPECT_EQ(circulatory->state, SourceLawSystemState::kClosed);
    EXPECT_EQ(musculoskeletal->state, SourceLawSystemState::kGrowing);
    EXPECT_TRUE(circulatory->reaction.is_continuous);
    EXPECT_TRUE(circulatory->reaction.all_byproducts_resolved);
    EXPECT_TRUE(evaluation.circuit_schedule.primary_circuit_is_valid);
    EXPECT_EQ(evaluation.circuit_schedule.effective_schedule.current_primary_circuit_system_id,
              body.circuit_schedule.current_primary_circuit_system_id);
    EXPECT_GT(evaluation.derived_source_throughput, 0.0F);
    EXPECT_GT(evaluation.derived_mana_max, 0);
    EXPECT_EQ(evaluation.integration.stage, SourceBodyStage::kGrowing);
    EXPECT_TRUE(evaluation.path.definition_exists);
    EXPECT_TRUE(evaluation.path.is_resonant);
    EXPECT_EQ(evaluation.path.applied_reaction_preferences.size(), 2U);
    EXPECT_TRUE(has_id(evaluation.capability_snapshot.available_intrinsic_ids,
                       "snt:intrinsic.sand_armor.pressure_shell"));
    EXPECT_TRUE(has_id(evaluation.capability_snapshot.available_intrinsic_ids,
                       "snt:intrinsic.sand_armor.structural_charge"));
    EXPECT_TRUE(has_id(evaluation.capability_snapshot.available_product_ids,
                       "snt:product.sand_armor.pressure"));
}

TEST(SourceLawEvaluatorTest, MissingBloodDowngradesLoopWithReadableReactionReason) {
    const SourceLawContentSnapshot content = builtin_content();
    SourceLawBodyState body = make_sand_armor_body();
    body.organs[static_cast<size_t>(SourceOrganSlot::kBlood)].reset();
    body.circuit_schedule.current_primary_circuit_system_id =
        "snt:system.sand_armor.circulatory";

    const SourceLawEvaluation evaluation = SourceLawBodyEvaluator::evaluate(content, body);
    const SourceLawSystemReport* circulatory = find_system(
        evaluation, "snt:system.sand_armor.circulatory");
    ASSERT_NE(circulatory, nullptr);
    EXPECT_EQ(circulatory->state, SourceLawSystemState::kUnavailable);
    EXPECT_FALSE(circulatory->reaction.is_continuous);
    EXPECT_TRUE(has_id(circulatory->reaction.missing_step_ids,
                       "snt:reaction.sand_armor.circulatory.transport"));
    EXPECT_FALSE(evaluation.circuit_schedule.primary_circuit_is_valid);
    EXPECT_FALSE(evaluation.circuit_schedule.effective_schedule.current_primary_circuit_system_id);
}

TEST(SourceLawEvaluatorTest, PlayerAndCreatureBodiesUseTheSameEvaluationRules) {
    const SourceLawContentSnapshot content = builtin_content();
    const SourceLawBodyState body = make_sand_armor_body();
    PlayerSourceLawState player{.body = body};
    CreatureSourceLawState creature{
        .body = body,
        .creature_species_id = "snt:creature.rock_lizard",
        .body_template_id = "snt:template.rock_lizard",
        .current_behavior_id = "snt:behavior.rock_lizard",
        .current_habitat_id = "snt:ecology.arid",
    };

    const SourceLawEvaluation player_evaluation = SourceLawBodyEvaluator::evaluate(content,
                                                                                     player.body);
    const SourceLawEvaluation creature_evaluation = SourceLawBodyEvaluator::evaluate(content,
                                                                                       creature.body);
    ASSERT_EQ(player_evaluation.systems.size(), creature_evaluation.systems.size());
    for (size_t index = 0; index < player_evaluation.systems.size(); ++index) {
        EXPECT_EQ(player_evaluation.systems[index].system_id,
                  creature_evaluation.systems[index].system_id);
        EXPECT_EQ(player_evaluation.systems[index].state,
                  creature_evaluation.systems[index].state);
    }
    EXPECT_EQ(player_evaluation.integration.stage, creature_evaluation.integration.stage);
    EXPECT_FLOAT_EQ(player_evaluation.derived_source_throughput,
                    creature_evaluation.derived_source_throughput);
    EXPECT_EQ(player_evaluation.capability_snapshot.available_intrinsic_ids,
              creature_evaluation.capability_snapshot.available_intrinsic_ids);
}

TEST(SourceLawSpellCompilerTest, PathPresetCanBeCopiedAndCoreChangesSemanticsNotBudget) {
    const SourceLawContentSnapshot content = builtin_content();
    SourceLawBodyState body = make_sand_armor_body();
    body.active_path_id = "snt:path.sand_armor";
    body.circuit_schedule = {
        .current_primary_circuit_system_id = "snt:system.sand_armor.circulatory",
        .coordinating_circuit_system_ids = {"snt:system.sand_armor.musculoskeletal"},
    };
    const SourceLawEvaluation evaluation = SourceLawBodyEvaluator::evaluate(content, body);
    ASSERT_TRUE(evaluation.path.is_resonant);

    const auto copied = SourceLawSpellProgramService::copy_preset(
        content, {.body = body}, {.value = 17},
        "snt:spell_graph.sand_armor.signature_mantle_charge", "Mantle charge");
    ASSERT_TRUE(copied) << (copied ? "" : copied.error().format());
    const auto specialized = SourceLawSpellCompiler::compile_program(
        content, evaluation.capability_snapshot, copied->program);
    EXPECT_TRUE(specialized.report.is_compilable);
    EXPECT_TRUE(has_id(specialized.report.applied_path_core_ids,
                       "snt:path_core.sand_armor.deposit_shape"));
    EXPECT_TRUE(has_id(specialized.report.satisfied_hybrid_link_ids,
                       "snt:hybrid.sand_armor.mantle_charge"));
    EXPECT_TRUE(SourceLawSpellCompiler::is_current(specialized, copied->program,
                                                    body.body_revision));
    EXPECT_FALSE(SourceLawSpellCompiler::is_current(specialized, copied->program,
                                                     body.body_revision + 1));

    const auto* completion_graph = content.find_spell_graph(
        "snt:spell_graph.sand_armor.completion_geode_body");
    ASSERT_NE(completion_graph, nullptr);
    const auto completion = SourceLawSpellCompiler::compile_graph(
        content, evaluation.capability_snapshot, *completion_graph);
    EXPECT_FALSE(completion.report.is_compilable);
    EXPECT_TRUE(has_id(completion.report.blocking_reason_ids,
                       "source_law.reason.spell.unification_required"));

    auto generic_program = copied->program;
    generic_program.copied_from_graph_id.reset();
    generic_program.source_revision = 2;
    generic_program.graph.required_path_core_ids.clear();
    generic_program.graph.nodes.erase(
        std::remove_if(generic_program.graph.nodes.begin(), generic_program.graph.nodes.end(),
                       [](const auto& node) { return node.stable_node_id == 5; }),
        generic_program.graph.nodes.end());
    generic_program.graph.links.erase(
        std::remove_if(generic_program.graph.links.begin(), generic_program.graph.links.end(),
                       [](const auto& link) {
                           return link.from_node_id == 5 || link.to_node_id == 5;
                       }),
        generic_program.graph.links.end());
    generic_program.graph.links.push_back({
        .from_node_id = 4,
        .from_port_id = "effect_out",
        .to_node_id = 6,
        .to_port_id = "effect",
    });
    ASSERT_TRUE(SourceLawSpellCompiler::validate_graph(content, generic_program.graph));
    const auto generic = SourceLawSpellCompiler::compile_program(
        content, evaluation.capability_snapshot, generic_program);
    EXPECT_TRUE(generic.report.is_compilable);
    EXPECT_TRUE(generic.report.applied_path_core_ids.empty());
    EXPECT_FLOAT_EQ(generic.report.available_effect_budget,
                    specialized.report.available_effect_budget);

    auto invalid_graph = generic_program.graph;
    invalid_graph.links.front().from_port_id = "invalid_port";
    const auto rejected = SourceLawSpellProgramService::edit(content, {}, {
        .program_id = {.value = 99},
        .display_name = "Invalid graph",
        .graph = std::move(invalid_graph),
    });
    EXPECT_FALSE(rejected);
}

TEST(SourceLawSpellCompilerTest, CreatureInnateAndPlayerGraphsUseTheSameBodySnapshotRules) {
    const SourceLawContentSnapshot content = builtin_content();
    SourceLawBodyState body = make_sand_armor_body();
    body.active_path_id = "snt:path.sand_armor";
    body.circuit_schedule.current_primary_circuit_system_id =
        "snt:system.sand_armor.circulatory";
    const SourceLawEvaluation evaluation = SourceLawBodyEvaluator::evaluate(content, body);
    const auto* innate_graph = content.find_spell_graph(
        "snt:spell_graph.rock_lizard.innate_shell");
    ASSERT_NE(innate_graph, nullptr);
    const auto creature_spell = SourceLawSpellCompiler::compile_graph(
        content, evaluation.capability_snapshot, *innate_graph);
    const auto player = SourceLawSpellProgramService::copy_preset(
        content, {.body = body}, {.value = 23},
        "snt:spell_graph.sand_armor.awakening_shell", "Player shell");
    ASSERT_TRUE(player) << (player ? "" : player.error().format());
    const auto player_spell = SourceLawSpellCompiler::compile_program(
        content, evaluation.capability_snapshot, player->program);
    EXPECT_EQ(creature_spell.report.is_compilable, player_spell.report.is_compilable);
    EXPECT_FLOAT_EQ(creature_spell.report.available_effect_budget,
                    player_spell.report.available_effect_budget);
    EXPECT_EQ(creature_spell.report.required_throughput,
              player_spell.report.required_throughput);
}

TEST(SourceLawTransactionTest, ImplantIsAtomicAndPublishesStateTransitionsOnlyAfterCandidateEvaluation) {
    const SourceLawContentSnapshot content = builtin_content();
    SourceLawBodyState body;
    body.source_reserve_current = 10;
    body.source_reserve_max = 10;

    const SourceLawImplantRequest request{
        .slot = SourceOrganSlot::kHeart,
        .organ_definition_id = "snt:rock_core_heart",
        .quality_id = "snt:quality.common",
        .source_reserve_cost = 4,
        .diagnostic_subject_id = "test-player",
    };
    const auto committed = SourceLawTransactionService::implant(content, body, request);
    ASSERT_TRUE(committed) << (committed ? "" : committed.error().format());
    EXPECT_EQ(body.source_reserve_current, 10);
    EXPECT_EQ(committed->body.source_reserve_current, 6);
    EXPECT_EQ(committed->body.body_revision, 1U);
    ASSERT_TRUE(committed->body.organs[static_cast<size_t>(SourceOrganSlot::kHeart)]);
    ASSERT_FALSE(committed->events.empty());
    EXPECT_EQ(committed->events.front().kind, SourceLawTransactionEventKind::kOrganImplanted);

    SourceLawImplantRequest too_expensive = request;
    too_expensive.slot = SourceOrganSlot::kBone;
    too_expensive.organ_definition_id = "snt:purified_crystal_bone";
    too_expensive.source_reserve_cost = 11;
    const auto rejected = SourceLawTransactionService::implant(content, body, too_expensive);
    EXPECT_FALSE(rejected);
    EXPECT_EQ(body.source_reserve_current, 10);
    EXPECT_FALSE(body.organs[static_cast<size_t>(SourceOrganSlot::kBone)]);
}

TEST(SourceLawPersistenceTest, CurrentSchemaRoundTripsAndRejectsRetiredSchemas) {
    PlayerSourceLawState source;
    source.body.active_path_id = "snt:path.sand_armor";
    source.body.stage = SourceBodyStage::kAwakened;
    source.body.source_level = 2;
    source.body.body_revision = 7;
    source.body.source_reserve_current = 15;
    source.body.source_reserve_max = 20;
    source.body.source_throughput = 18.5F;
    source.body.mana_current = 6;
    source.body.mana_max = 18;
    source.body.stability = 88.0F;
    source.body.mutation = 3.0F;
    source.body.mental_load = 4;
    source.body.organs[static_cast<size_t>(SourceOrganSlot::kHeart)] = {
        .definition_id = "snt:rock_core_heart",
        .growth_level = 3,
        .quality_id = "snt:quality.good",
        .contamination = 0.1F,
        .integrity = 0.9F,
        .tuning_tags = {"snt:tuning.anchor"},
    };
    source.body.circuit_schedule = {
        .current_primary_circuit_system_id = "snt:system.sand_armor.circulatory",
        .coordinating_circuit_system_ids = {"snt:system.sand_armor.musculoskeletal"},
        .primary_circuit_reallocation_cooldown_seconds = 2.5F,
    };
    source.discovered_system_ids = {"snt:system.sand_armor.circulatory"};
    source.personal_spell_programs = {{
        .program_id = {.value = 91},
        .copied_from_graph_id = "snt:spell_graph.sand_armor.awakening_shell",
        .display_name = "Personal shell",
        .graph = {
            .kind = SourceLawSpellGraphKind::kPlayerAuthored,
            .nodes = {
                {
                    .stable_node_id = 1,
                    .kind = SourceLawSpellNodeKind::kInput,
                    .definition_id = "snt:spell.input.source_mana",
                },
                {
                    .stable_node_id = 2,
                    .kind = SourceLawSpellNodeKind::kBodyIntrinsic,
                    .definition_id = "snt:intrinsic.sand_armor.pressure_shell",
                },
                {
                    .stable_node_id = 3,
                    .kind = SourceLawSpellNodeKind::kOutput,
                    .definition_id = "snt:spell.output.shield_effect",
                },
            },
            .links = {
                {.from_node_id = 1, .from_port_id = "mana", .to_node_id = 2,
                 .to_port_id = "input.0"},
                {.from_node_id = 2, .from_port_id = "output.0", .to_node_id = 3,
                 .to_port_id = "effect"},
            },
        },
        .source_revision = 1,
    }};

    SourceLawPersistenceCodec codec;
    const auto encoded = codec.encode(source);
    ASSERT_TRUE(encoded) << (encoded ? "" : encoded.error().format());
    EXPECT_EQ(encoded->schema_version, kSourceLawPlayerOrganSchemaVersion);
    const auto decoded = codec.decode(*encoded);
    ASSERT_TRUE(decoded) << (decoded ? "" : decoded.error().format());
    EXPECT_EQ(*decoded, source);

    auto retired = *encoded;
    retired.schema_id = "source_law_sublimation";
    EXPECT_FALSE(codec.decode(retired));

    auto version_one = *encoded;
    version_one.schema_version = 1;
    EXPECT_FALSE(codec.decode(version_one));

    auto malformed = *encoded;
    malformed.payload.pop_back();
    EXPECT_FALSE(codec.validate_organ_state(malformed));
}
