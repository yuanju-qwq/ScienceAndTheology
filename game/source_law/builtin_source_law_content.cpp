// Built-in V0.1 source-law content.

#include "game/source_law/builtin_source_law_content.h"

#include <utility>

namespace snt::game::source_law {
namespace {

[[nodiscard]] ElementalPhysiologyRule make_rule(
    SourceLawElement element,
    ElementalPhysiologyAction action,
    std::vector<ElementalReactionStage> stages,
    std::vector<ElementalPhysiologyAction> next_actions,
    std::vector<ElementalPhysiologyAction> competing_actions = {}) {
    return {
        .element = element,
        .allowed_actions = {action},
        .allowed_stages = std::move(stages),
        .allowed_next_actions = std::move(next_actions),
        .competing_actions = std::move(competing_actions),
    };
}

[[nodiscard]] OrganElementalContribution contribution(
    SourceLawElement element,
    ElementalPhysiologyAction action,
    ElementalReactionStage stage,
    float capacity) {
    return {
        .element = element,
        .action = action,
        .stage = stage,
        .base_capacity = capacity,
    };
}

[[nodiscard]] OrganSystemRequirement requirement(
    std::vector<SourceOrganSlot> slots,
    std::vector<OrganSystemRole> roles,
    std::vector<SourceLawId> tags) {
    return {
        .allowed_slots = std::move(slots),
        .required_roles = std::move(roles),
        .required_tags = std::move(tags),
    };
}

[[nodiscard]] ElementalReactionStepDefinition step(
    SourceLawId id,
    ElementalReactionStage stage_value,
    std::vector<ElementalPhysiologyAction> actions,
    std::vector<SourceLawElement> elements) {
    return {
        .id = std::move(id),
        .stage = stage_value,
        .allowed_actions = std::move(actions),
        .allowed_elements = std::move(elements),
    };
}

}  // namespace

snt::core::Expected<SourceLawContentSnapshot>
make_builtin_source_law_content_v0_1(uint64_t revision) {
    SourceLawContentBuilder builder;
    const std::vector<ElementalPhysiologyAction> broad_next_actions = {
        ElementalPhysiologyAction::kGenerateHeatPressure,
        ElementalPhysiologyAction::kDissolveCarryRecover,
        ElementalPhysiologyAction::kAnchorShapeDeposit,
        ElementalPhysiologyAction::kExchangeSignalVent,
        ElementalPhysiologyAction::kSenseFocusPurify,
        ElementalPhysiologyAction::kStoreAbsorbDecompose,
        ElementalPhysiologyAction::kRegulateLimitBoundary,
        ElementalPhysiologyAction::kMutateAmplifySplit,
    };
    if (auto result = builder.add_element_rule(make_rule(
            SourceLawElement::kFire,
            ElementalPhysiologyAction::kGenerateHeatPressure,
            {ElementalReactionStage::kGenerationOrIntake,
             ElementalReactionStage::kTransformation,
             ElementalReactionStage::kEffectOrRelease},
            broad_next_actions)); !result) {
        return result.error();
    }
    if (auto result = builder.add_element_rule(make_rule(
            SourceLawElement::kWater,
            ElementalPhysiologyAction::kDissolveCarryRecover,
            {ElementalReactionStage::kGenerationOrIntake,
             ElementalReactionStage::kTransport,
             ElementalReactionStage::kRegulationOrRecovery},
            broad_next_actions)); !result) {
        return result.error();
    }
    if (auto result = builder.add_element_rule(make_rule(
            SourceLawElement::kEarth,
            ElementalPhysiologyAction::kAnchorShapeDeposit,
            {ElementalReactionStage::kGenerationOrIntake,
             ElementalReactionStage::kTransformation,
             ElementalReactionStage::kEffectOrRelease,
             ElementalReactionStage::kRegulationOrRecovery},
            broad_next_actions)); !result) {
        return result.error();
    }
    if (auto result = builder.add_element_rule(make_rule(
            SourceLawElement::kAir,
            ElementalPhysiologyAction::kExchangeSignalVent,
            {ElementalReactionStage::kGenerationOrIntake,
             ElementalReactionStage::kTransport,
             ElementalReactionStage::kEffectOrRelease},
            broad_next_actions)); !result) {
        return result.error();
    }
    if (auto result = builder.add_element_rule(make_rule(
            SourceLawElement::kLight,
            ElementalPhysiologyAction::kSenseFocusPurify,
            {ElementalReactionStage::kGenerationOrIntake,
             ElementalReactionStage::kRegulationOrRecovery},
            broad_next_actions)); !result) {
        return result.error();
    }
    if (auto result = builder.add_element_rule(make_rule(
            SourceLawElement::kDark,
            ElementalPhysiologyAction::kStoreAbsorbDecompose,
            {ElementalReactionStage::kGenerationOrIntake,
             ElementalReactionStage::kTransformation,
             ElementalReactionStage::kRegulationOrRecovery},
            broad_next_actions)); !result) {
        return result.error();
    }
    if (auto result = builder.add_element_rule(make_rule(
            SourceLawElement::kOrder,
            ElementalPhysiologyAction::kRegulateLimitBoundary,
            {ElementalReactionStage::kGenerationOrIntake,
             ElementalReactionStage::kTransport,
             ElementalReactionStage::kEffectOrRelease,
             ElementalReactionStage::kRegulationOrRecovery},
            broad_next_actions)); !result) {
        return result.error();
    }
    if (auto result = builder.add_element_rule(make_rule(
            SourceLawElement::kChaos,
            ElementalPhysiologyAction::kMutateAmplifySplit,
            {ElementalReactionStage::kTransformation,
             ElementalReactionStage::kEffectOrRelease},
            broad_next_actions)); !result) {
        return result.error();
    }

    if (auto result = builder.add_organ({
            .id = "snt:rock_core_heart",
            .slot = SourceOrganSlot::kHeart,
            .roles = {OrganSystemRole::kCore, OrganSystemRole::kStabilizer},
            .elemental_contributions = {
                contribution(SourceLawElement::kEarth,
                             ElementalPhysiologyAction::kAnchorShapeDeposit,
                             ElementalReactionStage::kGenerationOrIntake, 12.0F),
                contribution(SourceLawElement::kOrder,
                             ElementalPhysiologyAction::kRegulateLimitBoundary,
                             ElementalReactionStage::kRegulationOrRecovery, 5.0F),
            },
            .purity_profile_id = "snt:purity.geode",
            .bloodline_tags = {"snt:bloodline.rock_lizard"},
            .ecology_tags = {"snt:ecology.arid"},
            .system_tags = {"snt:system.sand_armor"},
            .native_path_tags = {"snt:path.sand_armor"},
            .base_stability_modifier = 8.0F,
            .pressure_tags = {"snt:source_law.relief.stone_deposit"},
        }); !result) {
        return result.error();
    }
    if (auto result = builder.add_organ({
            .id = "snt:purified_crystal_bone",
            .slot = SourceOrganSlot::kBone,
            .roles = {OrganSystemRole::kCore, OrganSystemRole::kEffector,
                      OrganSystemRole::kStabilizer},
            .elemental_contributions = {
                contribution(SourceLawElement::kEarth,
                             ElementalPhysiologyAction::kAnchorShapeDeposit,
                             ElementalReactionStage::kTransformation, 10.0F),
                contribution(SourceLawElement::kEarth,
                             ElementalPhysiologyAction::kAnchorShapeDeposit,
                             ElementalReactionStage::kEffectOrRelease, 14.0F),
                contribution(SourceLawElement::kOrder,
                             ElementalPhysiologyAction::kRegulateLimitBoundary,
                             ElementalReactionStage::kRegulationOrRecovery, 5.0F),
            },
            .purity_profile_id = "snt:purity.geode",
            .bloodline_tags = {"snt:bloodline.rock_lizard"},
            .ecology_tags = {"snt:ecology.arid"},
            .system_tags = {"snt:system.sand_armor"},
            .native_path_tags = {"snt:path.sand_armor"},
            .base_stability_modifier = 10.0F,
            .pressure_tags = {"snt:source_law.relief.stone_deposit",
                              "snt:source_law.relief.structural_release"},
        }); !result) {
        return result.error();
    }
    if (auto result = builder.add_organ({
            .id = "snt:mineral_blood",
            .slot = SourceOrganSlot::kBlood,
            .roles = {OrganSystemRole::kConduit, OrganSystemRole::kStabilizer},
            .elemental_contributions = {
                contribution(SourceLawElement::kWater,
                             ElementalPhysiologyAction::kDissolveCarryRecover,
                             ElementalReactionStage::kTransport, 11.0F),
                contribution(SourceLawElement::kWater,
                             ElementalPhysiologyAction::kDissolveCarryRecover,
                             ElementalReactionStage::kRegulationOrRecovery, 4.0F),
            },
            .purity_profile_id = "snt:purity.mineral",
            .bloodline_tags = {"snt:bloodline.rock_lizard"},
            .ecology_tags = {"snt:ecology.arid"},
            .system_tags = {"snt:system.sand_armor"},
            .native_path_tags = {"snt:path.sand_armor"},
            .base_stability_modifier = 4.0F,
        }); !result) {
        return result.error();
    }
    if (auto result = builder.add_organ({
            .id = "snt:stone_lizard_lung",
            .slot = SourceOrganSlot::kLung,
            .roles = {OrganSystemRole::kInterface, OrganSystemRole::kConduit},
            .elemental_contributions = {
                contribution(SourceLawElement::kAir,
                             ElementalPhysiologyAction::kExchangeSignalVent,
                             ElementalReactionStage::kGenerationOrIntake, 7.0F),
                contribution(SourceLawElement::kAir,
                             ElementalPhysiologyAction::kExchangeSignalVent,
                             ElementalReactionStage::kEffectOrRelease, 6.0F),
            },
            .purity_profile_id = "snt:purity.wind",
            .bloodline_tags = {"snt:bloodline.rock_lizard"},
            .ecology_tags = {"snt:ecology.arid"},
            .system_tags = {"snt:system.rock_lizard"},
        }); !result) {
        return result.error();
    }
    if (auto result = builder.add_organ({
            .id = "snt:geode_eye",
            .slot = SourceOrganSlot::kEye,
            .roles = {OrganSystemRole::kSensor},
            .elemental_contributions = {
                contribution(SourceLawElement::kLight,
                             ElementalPhysiologyAction::kSenseFocusPurify,
                             ElementalReactionStage::kGenerationOrIntake, 6.0F),
                contribution(SourceLawElement::kLight,
                             ElementalPhysiologyAction::kSenseFocusPurify,
                             ElementalReactionStage::kRegulationOrRecovery, 4.0F),
            },
            .purity_profile_id = "snt:purity.geode",
            .bloodline_tags = {"snt:bloodline.rock_lizard"},
            .ecology_tags = {"snt:ecology.arid"},
            .system_tags = {"snt:system.rock_lizard"},
        }); !result) {
        return result.error();
    }
    if (auto result = builder.add_organ({
            .id = "snt:resonant_nerve",
            .slot = SourceOrganSlot::kNerve,
            .roles = {OrganSystemRole::kConduit, OrganSystemRole::kSensor,
                      OrganSystemRole::kStabilizer},
            .elemental_contributions = {
                contribution(SourceLawElement::kAir,
                             ElementalPhysiologyAction::kExchangeSignalVent,
                             ElementalReactionStage::kTransport, 6.0F),
                contribution(SourceLawElement::kOrder,
                             ElementalPhysiologyAction::kRegulateLimitBoundary,
                             ElementalReactionStage::kTransport, 7.0F),
            },
            .purity_profile_id = "snt:purity.resonant",
            .bloodline_tags = {"snt:bloodline.rock_lizard"},
            .ecology_tags = {"snt:ecology.arid"},
            .system_tags = {"snt:system.rock_lizard"},
        }); !result) {
        return result.error();
    }
    if (auto result = builder.add_organ({
            .id = "snt:geomantic_skin",
            .slot = SourceOrganSlot::kSkin,
            .roles = {OrganSystemRole::kEffector, OrganSystemRole::kInterface,
                      OrganSystemRole::kStabilizer},
            .elemental_contributions = {
                contribution(SourceLawElement::kEarth,
                             ElementalPhysiologyAction::kAnchorShapeDeposit,
                             ElementalReactionStage::kEffectOrRelease, 10.0F),
            },
            .purity_profile_id = "snt:purity.geode",
            .bloodline_tags = {"snt:bloodline.rock_lizard"},
            .ecology_tags = {"snt:ecology.arid"},
            .system_tags = {"snt:system.sand_armor"},
            .native_path_tags = {"snt:path.sand_armor"},
            .base_stability_modifier = 6.0F,
            .pressure_tags = {"snt:source_law.relief.structural_release"},
        }); !result) {
        return result.error();
    }
    if (auto result = builder.add_organ({
            .id = "snt:mineral_viscera",
            .slot = SourceOrganSlot::kViscera,
            .roles = {OrganSystemRole::kCore, OrganSystemRole::kFilter},
            .elemental_contributions = {
                contribution(SourceLawElement::kDark,
                             ElementalPhysiologyAction::kStoreAbsorbDecompose,
                             ElementalReactionStage::kTransformation, 8.0F),
                contribution(SourceLawElement::kFire,
                             ElementalPhysiologyAction::kGenerateHeatPressure,
                             ElementalReactionStage::kGenerationOrIntake, 7.0F),
            },
            .purity_profile_id = "snt:purity.mineral",
            .bloodline_tags = {"snt:bloodline.rock_lizard"},
            .ecology_tags = {"snt:ecology.arid"},
            .system_tags = {"snt:system.rock_lizard"},
        }); !result) {
        return result.error();
    }

    if (auto result = builder.add_reaction({
            .id = "snt:reaction.sand_armor.circulatory",
            .closure_steps = {
                step("snt:reaction.sand_armor.circulatory.generation",
                     ElementalReactionStage::kGenerationOrIntake,
                     {ElementalPhysiologyAction::kAnchorShapeDeposit,
                      ElementalPhysiologyAction::kRegulateLimitBoundary},
                     {SourceLawElement::kEarth, SourceLawElement::kOrder}),
                step("snt:reaction.sand_armor.circulatory.transport",
                     ElementalReactionStage::kTransport,
                     {ElementalPhysiologyAction::kDissolveCarryRecover},
                     {SourceLawElement::kWater}),
                step("snt:reaction.sand_armor.circulatory.effect",
                     ElementalReactionStage::kEffectOrRelease,
                     {ElementalPhysiologyAction::kAnchorShapeDeposit},
                     {SourceLawElement::kEarth}),
            },
            .product_definition_id = "snt:product.sand_armor.pressure",
            .byproduct_tags = {"snt:source_law.byproduct.stone_dust"},
            .required_relief_tags = {"snt:source_law.relief.stone_deposit"},
        }); !result) {
        return result.error();
    }
    if (auto result = builder.add_reaction({
            .id = "snt:reaction.sand_armor.musculoskeletal",
            .closure_steps = {
                step("snt:reaction.sand_armor.musculoskeletal.generation",
                     ElementalReactionStage::kGenerationOrIntake,
                     {ElementalPhysiologyAction::kAnchorShapeDeposit},
                     {SourceLawElement::kEarth}),
                step("snt:reaction.sand_armor.musculoskeletal.transformation",
                     ElementalReactionStage::kTransformation,
                     {ElementalPhysiologyAction::kAnchorShapeDeposit},
                     {SourceLawElement::kEarth}),
                step("snt:reaction.sand_armor.musculoskeletal.transport",
                     ElementalReactionStage::kTransport,
                     {ElementalPhysiologyAction::kDissolveCarryRecover},
                     {SourceLawElement::kWater}),
                step("snt:reaction.sand_armor.musculoskeletal.effect",
                     ElementalReactionStage::kEffectOrRelease,
                     {ElementalPhysiologyAction::kAnchorShapeDeposit},
                     {SourceLawElement::kEarth}),
            },
            .product_definition_id = "snt:product.sand_armor.frame",
            .byproduct_tags = {"snt:source_law.byproduct.structural_strain"},
            .required_relief_tags = {"snt:source_law.relief.structural_release"},
        }); !result) {
        return result.error();
    }

    if (auto result = builder.add_system({
            .id = "snt:system.sand_armor.circulatory",
            .body_system = SourceBodySystem::kCirculatory,
            .resonance_requirements = {
                requirement({SourceOrganSlot::kHeart}, {OrganSystemRole::kCore},
                            {"snt:system.sand_armor"}),
                requirement({SourceOrganSlot::kBlood}, {OrganSystemRole::kConduit},
                            {"snt:system.sand_armor"}),
                requirement({SourceOrganSlot::kBone}, {OrganSystemRole::kEffector},
                            {"snt:system.sand_armor"}),
            },
            .closure_requirements = {
                requirement({SourceOrganSlot::kBone}, {OrganSystemRole::kStabilizer},
                            {"snt:source_law.relief.stone_deposit"}),
            },
            .elemental_reaction_id = "snt:reaction.sand_armor.circulatory",
            .base_active_ability_id = "snt:ability.rock_shield",
            .pressure_tags = {"snt:source_law.byproduct.stone_dust"},
        }); !result) {
        return result.error();
    }
    if (auto result = builder.add_system({
            .id = "snt:system.sand_armor.musculoskeletal",
            .body_system = SourceBodySystem::kMusculoskeletal,
            .resonance_requirements = {
                requirement({SourceOrganSlot::kBone}, {OrganSystemRole::kCore},
                            {"snt:system.sand_armor"}),
                requirement({SourceOrganSlot::kBlood}, {OrganSystemRole::kConduit},
                            {"snt:system.sand_armor"}),
                requirement({SourceOrganSlot::kSkin}, {OrganSystemRole::kEffector},
                            {"snt:system.sand_armor"}),
            },
            .closure_requirements = {
                requirement({SourceOrganSlot::kSkin}, {OrganSystemRole::kStabilizer},
                            {"snt:source_law.relief.structural_release"}),
            },
            .growth_link_requirements = {
                requirement({SourceOrganSlot::kHeart}, {OrganSystemRole::kStabilizer},
                            {"snt:system.sand_armor"}),
            },
            .elemental_reaction_id = "snt:reaction.sand_armor.musculoskeletal",
            .base_active_ability_id = "snt:ability.crystal_charge",
            .pressure_tags = {"snt:source_law.byproduct.structural_strain"},
        }); !result) {
        return result.error();
    }

    if (auto result = builder.set_integration_definition({
            .id = "snt:integration.source_body.v0_1",
            .required_bridges = {{
                {
                    .bridge = SourceBodyIntegrationBridge::kEnergyAndRefinement,
                    .id = "snt:bridge.energy_and_refinement",
                    .required_slots = {SourceOrganSlot::kHeart, SourceOrganSlot::kBlood,
                                       SourceOrganSlot::kViscera},
                    .required_roles = {OrganSystemRole::kCore, OrganSystemRole::kConduit,
                                       OrganSystemRole::kFilter},
                    .required_reaction_stages = {
                        ElementalReactionStage::kGenerationOrIntake,
                        ElementalReactionStage::kTransport,
                        ElementalReactionStage::kTransformation},
                    .required_actions = {
                        ElementalPhysiologyAction::kGenerateHeatPressure,
                        ElementalPhysiologyAction::kDissolveCarryRecover,
                        ElementalPhysiologyAction::kStoreAbsorbDecompose},
                },
                {
                    .bridge = SourceBodyIntegrationBridge::kControlAndFeedback,
                    .id = "snt:bridge.control_and_feedback",
                    .required_slots = {SourceOrganSlot::kHeart, SourceOrganSlot::kEye,
                                       SourceOrganSlot::kNerve},
                    .required_roles = {OrganSystemRole::kStabilizer, OrganSystemRole::kSensor,
                                       OrganSystemRole::kConduit},
                    .required_reaction_stages = {
                        ElementalReactionStage::kGenerationOrIntake,
                        ElementalReactionStage::kTransport,
                        ElementalReactionStage::kRegulationOrRecovery},
                    .required_actions = {
                        ElementalPhysiologyAction::kSenseFocusPurify,
                        ElementalPhysiologyAction::kExchangeSignalVent,
                        ElementalPhysiologyAction::kRegulateLimitBoundary},
                },
                {
                    .bridge = SourceBodyIntegrationBridge::kEnvironmentAndBarrier,
                    .id = "snt:bridge.environment_and_barrier",
                    .required_slots = {SourceOrganSlot::kLung, SourceOrganSlot::kEye,
                                       SourceOrganSlot::kSkin},
                    .required_roles = {OrganSystemRole::kInterface, OrganSystemRole::kSensor,
                                       OrganSystemRole::kEffector},
                    .required_reaction_stages = {
                        ElementalReactionStage::kGenerationOrIntake,
                        ElementalReactionStage::kEffectOrRelease,
                        ElementalReactionStage::kRegulationOrRecovery},
                    .required_actions = {
                        ElementalPhysiologyAction::kExchangeSignalVent,
                        ElementalPhysiologyAction::kSenseFocusPurify,
                        ElementalPhysiologyAction::kAnchorShapeDeposit},
                },
                {
                    .bridge = SourceBodyIntegrationBridge::kLoadAndAction,
                    .id = "snt:bridge.load_and_action",
                    .required_slots = {SourceOrganSlot::kBone, SourceOrganSlot::kBlood,
                                       SourceOrganSlot::kSkin},
                    .required_roles = {OrganSystemRole::kCore, OrganSystemRole::kConduit,
                                       OrganSystemRole::kStabilizer},
                    .required_reaction_stages = {
                        ElementalReactionStage::kTransformation,
                        ElementalReactionStage::kTransport,
                        ElementalReactionStage::kEffectOrRelease},
                    .required_actions = {
                        ElementalPhysiologyAction::kAnchorShapeDeposit,
                        ElementalPhysiologyAction::kDissolveCarryRecover,
                        ElementalPhysiologyAction::kRegulateLimitBoundary},
                },
            }},
            .minimum_stability = 70.0F,
            .maximum_mutation = 25.0F,
            .integration_ritual_id = "snt:ritual.full_body_unification",
            .ecology_conditions = {"snt:ecology.arid"},
        }); !result) {
        return result.error();
    }

    if (auto result = builder.add_path({
            .id = "snt:path.sand_armor",
            .preferred_systems = {SourceBodySystem::kCirculatory,
                                  SourceBodySystem::kMusculoskeletal},
            .reaction_preferences = {
                {
                    .body_system = SourceBodySystem::kCirculatory,
                    .stage = ElementalReactionStage::kGenerationOrIntake,
                    .action = ElementalPhysiologyAction::kAnchorShapeDeposit,
                    .product_modifier_id = "snt:modifier.sand_armor.shield_pressure",
                    .byproduct_handling_modifier_id = "snt:modifier.sand_armor.deposit",
                },
                {
                    .body_system = SourceBodySystem::kMusculoskeletal,
                    .stage = ElementalReactionStage::kEffectOrRelease,
                    .action = ElementalPhysiologyAction::kAnchorShapeDeposit,
                    .product_modifier_id = "snt:modifier.sand_armor.mining_force",
                    .byproduct_handling_modifier_id = "snt:modifier.sand_armor.structural_release",
                },
            },
            .core_organ_tags = {"snt:system.sand_armor"},
            .resonance_rules = {"snt:resonance.sand_armor"},
            .signature_ability_id = "snt:ability.rock_shield",
            .completion_body_ability_id = "snt:ability.geode_body",
            .domain_ability_id = "snt:ability.sand_armor_domain",
        }); !result) {
        return result.error();
    }

    if (auto result = builder.add_bloodline({
            .id = "snt:bloodline.rock_lizard",
            .lineage_tags = {"snt:bloodline.rock_lizard"},
            .ecology_tags = {"snt:ecology.arid"},
            .innate_reaction_ids = {"snt:reaction.sand_armor.circulatory",
                                    "snt:reaction.sand_armor.musculoskeletal"},
            .compatible_bloodline_tags = {"snt:bloodline.rock_lizard"},
        }); !result) {
        return result.error();
    }
    SourceBodyTemplate rock_lizard_template{
        .id = "snt:template.rock_lizard",
        .innate_path_id = "snt:path.sand_armor",
        .initial_system_ids = {"snt:system.sand_armor.circulatory",
                               "snt:system.sand_armor.musculoskeletal"},
        .initial_reaction_ids = {"snt:reaction.sand_armor.circulatory",
                                 "snt:reaction.sand_armor.musculoskeletal"},
        .integration_condition_ids = {"snt:ecology.arid"},
    };
    rock_lizard_template.organ_candidates[static_cast<size_t>(SourceOrganSlot::kHeart)] =
        {"snt:rock_core_heart"};
    rock_lizard_template.organ_candidates[static_cast<size_t>(SourceOrganSlot::kBone)] =
        {"snt:purified_crystal_bone"};
    rock_lizard_template.organ_candidates[static_cast<size_t>(SourceOrganSlot::kBlood)] =
        {"snt:mineral_blood"};
    rock_lizard_template.organ_candidates[static_cast<size_t>(SourceOrganSlot::kLung)] =
        {"snt:stone_lizard_lung"};
    rock_lizard_template.organ_candidates[static_cast<size_t>(SourceOrganSlot::kEye)] =
        {"snt:geode_eye"};
    rock_lizard_template.organ_candidates[static_cast<size_t>(SourceOrganSlot::kNerve)] =
        {"snt:resonant_nerve"};
    rock_lizard_template.organ_candidates[static_cast<size_t>(SourceOrganSlot::kSkin)] =
        {"snt:geomantic_skin"};
    rock_lizard_template.organ_candidates[static_cast<size_t>(SourceOrganSlot::kViscera)] =
        {"snt:mineral_viscera"};
    if (auto result = builder.add_body_template(std::move(rock_lizard_template)); !result) {
        return result.error();
    }
    if (auto result = builder.add_creature_body({
            .creature_species_id = "snt:creature.rock_lizard",
            .bloodline_profile_id = "snt:bloodline.rock_lizard",
            .body_template_id = "snt:template.rock_lizard",
            .behavior_profile_id = "snt:behavior.rock_lizard",
            .sample_definition_ids = {"snt:sample.rock_lizard.scale",
                                      "snt:sample.rock_lizard.mineral_blood"},
            .ecology_conditions = {"snt:ecology.arid"},
        }); !result) {
        return result.error();
    }
    return std::move(builder).build(revision);
}

}  // namespace snt::game::source_law
