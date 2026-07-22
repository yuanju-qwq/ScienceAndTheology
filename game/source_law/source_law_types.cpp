// Game-owned source-law vocabulary implementation.

#include "game/source_law/source_law_types.h"

namespace snt::game::source_law {

std::string_view source_organ_slot_name(SourceOrganSlot value) noexcept {
    switch (value) {
    case SourceOrganSlot::kHeart: return "heart";
    case SourceOrganSlot::kBone: return "bone";
    case SourceOrganSlot::kBlood: return "blood";
    case SourceOrganSlot::kLung: return "lung";
    case SourceOrganSlot::kEye: return "eye";
    case SourceOrganSlot::kNerve: return "nerve";
    case SourceOrganSlot::kSkin: return "skin";
    case SourceOrganSlot::kViscera: return "viscera";
    case SourceOrganSlot::kCount: break;
    }
    return "invalid";
}

std::string_view source_body_system_name(SourceBodySystem value) noexcept {
    switch (value) {
    case SourceBodySystem::kCirculatory: return "circulatory";
    case SourceBodySystem::kMusculoskeletal: return "musculoskeletal";
    case SourceBodySystem::kRespiratory: return "respiratory";
    case SourceBodySystem::kDigestive: return "digestive";
    case SourceBodySystem::kExcretory: return "excretory";
    case SourceBodySystem::kNeurosensory: return "neurosensory";
    case SourceBodySystem::kIntegumentary: return "integumentary";
    case SourceBodySystem::kSecretorySymbiotic: return "secretory_symbiotic";
    case SourceBodySystem::kCount: break;
    }
    return "invalid";
}

std::string_view organ_system_role_name(OrganSystemRole value) noexcept {
    switch (value) {
    case OrganSystemRole::kCore: return "core";
    case OrganSystemRole::kConduit: return "conduit";
    case OrganSystemRole::kEffector: return "effector";
    case OrganSystemRole::kStabilizer: return "stabilizer";
    case OrganSystemRole::kFilter: return "filter";
    case OrganSystemRole::kInterface: return "interface";
    case OrganSystemRole::kSensor: return "sensor";
    case OrganSystemRole::kCount: break;
    }
    return "invalid";
}

std::string_view source_law_element_name(SourceLawElement value) noexcept {
    switch (value) {
    case SourceLawElement::kFire: return "fire";
    case SourceLawElement::kWater: return "water";
    case SourceLawElement::kEarth: return "earth";
    case SourceLawElement::kAir: return "air";
    case SourceLawElement::kLight: return "light";
    case SourceLawElement::kDark: return "dark";
    case SourceLawElement::kOrder: return "order";
    case SourceLawElement::kChaos: return "chaos";
    case SourceLawElement::kCount: break;
    }
    return "invalid";
}

std::string_view elemental_reaction_stage_name(ElementalReactionStage value) noexcept {
    switch (value) {
    case ElementalReactionStage::kGenerationOrIntake: return "generation_or_intake";
    case ElementalReactionStage::kTransport: return "transport";
    case ElementalReactionStage::kTransformation: return "transformation";
    case ElementalReactionStage::kEffectOrRelease: return "effect_or_release";
    case ElementalReactionStage::kRegulationOrRecovery: return "regulation_or_recovery";
    case ElementalReactionStage::kCount: break;
    }
    return "invalid";
}

std::string_view elemental_physiology_action_name(
    ElementalPhysiologyAction value) noexcept {
    switch (value) {
    case ElementalPhysiologyAction::kGenerateHeatPressure:
        return "generate_heat_pressure";
    case ElementalPhysiologyAction::kDissolveCarryRecover:
        return "dissolve_carry_recover";
    case ElementalPhysiologyAction::kAnchorShapeDeposit:
        return "anchor_shape_deposit";
    case ElementalPhysiologyAction::kExchangeSignalVent:
        return "exchange_signal_vent";
    case ElementalPhysiologyAction::kSenseFocusPurify:
        return "sense_focus_purify";
    case ElementalPhysiologyAction::kStoreAbsorbDecompose:
        return "store_absorb_decompose";
    case ElementalPhysiologyAction::kRegulateLimitBoundary:
        return "regulate_limit_boundary";
    case ElementalPhysiologyAction::kMutateAmplifySplit:
        return "mutate_amplify_split";
    case ElementalPhysiologyAction::kCount: break;
    }
    return "invalid";
}

std::string_view source_body_stage_name(SourceBodyStage value) noexcept {
    switch (value) {
    case SourceBodyStage::kDormant: return "dormant";
    case SourceBodyStage::kAwakened: return "awakened";
    case SourceBodyStage::kGrowing: return "growing";
    case SourceBodyStage::kEightOrgansSublimated: return "eight_organs_sublimated";
    case SourceBodyStage::kInitialComplete: return "initial_complete";
    case SourceBodyStage::kMatureComplete: return "mature_complete";
    case SourceBodyStage::kCount: break;
    }
    return "invalid";
}

std::string_view source_body_integration_bridge_name(
    SourceBodyIntegrationBridge value) noexcept {
    switch (value) {
    case SourceBodyIntegrationBridge::kEnergyAndRefinement:
        return "energy_and_refinement";
    case SourceBodyIntegrationBridge::kControlAndFeedback:
        return "control_and_feedback";
    case SourceBodyIntegrationBridge::kEnvironmentAndBarrier:
        return "environment_and_barrier";
    case SourceBodyIntegrationBridge::kLoadAndAction: return "load_and_action";
    case SourceBodyIntegrationBridge::kCount: break;
    }
    return "invalid";
}

std::string_view source_law_system_state_name(SourceLawSystemState value) noexcept {
    switch (value) {
    case SourceLawSystemState::kUnavailable: return "unavailable";
    case SourceLawSystemState::kResonant: return "resonant";
    case SourceLawSystemState::kClosed: return "closed";
    case SourceLawSystemState::kGrowing: return "growing";
    case SourceLawSystemState::kCount: break;
    }
    return "invalid";
}

std::string_view source_law_spell_node_kind_name(SourceLawSpellNodeKind value) noexcept {
    switch (value) {
    case SourceLawSpellNodeKind::kInput: return "input";
    case SourceLawSpellNodeKind::kBodyIntrinsic: return "body_intrinsic";
    case SourceLawSpellNodeKind::kPathCore: return "path_core";
    case SourceLawSpellNodeKind::kControlFlow: return "control_flow";
    case SourceLawSpellNodeKind::kCoordinatingService: return "coordinating_service";
    case SourceLawSpellNodeKind::kOutput: return "output";
    case SourceLawSpellNodeKind::kCount: break;
    }
    return "invalid";
}

std::string_view source_law_spell_port_type_name(SourceLawSpellPortType value) noexcept {
    switch (value) {
    case SourceLawSpellPortType::kMana: return "mana";
    case SourceLawSpellPortType::kReactionProduct: return "reaction_product";
    case SourceLawSpellPortType::kEnvironmentCarrier: return "environment_carrier";
    case SourceLawSpellPortType::kTargetInformation: return "target_information";
    case SourceLawSpellPortType::kControlSignal: return "control_signal";
    case SourceLawSpellPortType::kLoadOrBoundary: return "load_or_boundary";
    case SourceLawSpellPortType::kByproduct: return "byproduct";
    case SourceLawSpellPortType::kEffect: return "effect";
    case SourceLawSpellPortType::kCount: break;
    }
    return "invalid";
}

std::string_view source_law_spell_graph_kind_name(SourceLawSpellGraphKind value) noexcept {
    switch (value) {
    case SourceLawSpellGraphKind::kPathAwakening: return "path_awakening";
    case SourceLawSpellGraphKind::kPathSystem: return "path_system";
    case SourceLawSpellGraphKind::kPathSignature: return "path_signature";
    case SourceLawSpellGraphKind::kPathCompletion: return "path_completion";
    case SourceLawSpellGraphKind::kCreatureInnate: return "creature_innate";
    case SourceLawSpellGraphKind::kPlayerAuthored: return "player_authored";
    case SourceLawSpellGraphKind::kCount: break;
    }
    return "invalid";
}

}  // namespace snt::game::source_law
