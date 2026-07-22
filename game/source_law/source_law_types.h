// Game-owned source-law vocabulary.
//
// This module defines the current eight-slot body model. It deliberately
// does not import retired src/Godot source-law enums or payload types.

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace snt::game::source_law {

using SourceLawId = std::string;

enum class SourceOrganSlot : uint8_t {
    kHeart = 0,
    kBone,
    kBlood,
    kLung,
    kEye,
    kNerve,
    kSkin,
    kViscera,
    kCount,
};

inline constexpr size_t kSourceOrganSlotCount =
    static_cast<size_t>(SourceOrganSlot::kCount);

enum class SourceBodySystem : uint8_t {
    kCirculatory = 0,
    kMusculoskeletal,
    kRespiratory,
    kDigestive,
    kExcretory,
    kNeurosensory,
    kIntegumentary,
    kSecretorySymbiotic,
    kCount,
};

inline constexpr size_t kSourceBodySystemCount =
    static_cast<size_t>(SourceBodySystem::kCount);

enum class OrganSystemRole : uint8_t {
    kCore = 0,
    kConduit,
    kEffector,
    kStabilizer,
    kFilter,
    kInterface,
    kSensor,
    kCount,
};

enum class SourceLawElement : uint8_t {
    kFire = 0,
    kWater,
    kEarth,
    kAir,
    kLight,
    kDark,
    kOrder,
    kChaos,
    kCount,
};

inline constexpr size_t kSourceLawElementCount =
    static_cast<size_t>(SourceLawElement::kCount);

enum class ElementalReactionStage : uint8_t {
    kGenerationOrIntake = 0,
    kTransport,
    kTransformation,
    kEffectOrRelease,
    kRegulationOrRecovery,
    kCount,
};

enum class ElementalPhysiologyAction : uint8_t {
    kGenerateHeatPressure = 0,
    kDissolveCarryRecover,
    kAnchorShapeDeposit,
    kExchangeSignalVent,
    kSenseFocusPurify,
    kStoreAbsorbDecompose,
    kRegulateLimitBoundary,
    kMutateAmplifySplit,
    kCount,
};

enum class SourceBodyStage : uint8_t {
    kDormant = 0,
    kAwakened,
    kGrowing,
    kEightOrgansSublimated,
    kInitialComplete,
    kMatureComplete,
    kCount,
};

enum class SourceBodyIntegrationBridge : uint8_t {
    kEnergyAndRefinement = 0,
    kControlAndFeedback,
    kEnvironmentAndBarrier,
    kLoadAndAction,
    kCount,
};

inline constexpr size_t kSourceBodyIntegrationBridgeCount =
    static_cast<size_t>(SourceBodyIntegrationBridge::kCount);

enum class SourceLawSystemState : uint8_t {
    kUnavailable = 0,
    kResonant,
    kClosed,
    kGrowing,
    kCount,
};

[[nodiscard]] constexpr bool is_valid_source_organ_slot(SourceOrganSlot value) noexcept {
    return static_cast<uint8_t>(value) < static_cast<uint8_t>(SourceOrganSlot::kCount);
}

[[nodiscard]] constexpr bool is_valid_source_body_system(SourceBodySystem value) noexcept {
    return static_cast<uint8_t>(value) < static_cast<uint8_t>(SourceBodySystem::kCount);
}

[[nodiscard]] constexpr bool is_valid_organ_system_role(OrganSystemRole value) noexcept {
    return static_cast<uint8_t>(value) < static_cast<uint8_t>(OrganSystemRole::kCount);
}

[[nodiscard]] constexpr bool is_valid_source_law_element(SourceLawElement value) noexcept {
    return static_cast<uint8_t>(value) < static_cast<uint8_t>(SourceLawElement::kCount);
}

[[nodiscard]] constexpr bool is_valid_elemental_reaction_stage(
    ElementalReactionStage value) noexcept {
    return static_cast<uint8_t>(value) < static_cast<uint8_t>(ElementalReactionStage::kCount);
}

[[nodiscard]] constexpr bool is_valid_elemental_physiology_action(
    ElementalPhysiologyAction value) noexcept {
    return static_cast<uint8_t>(value) <
           static_cast<uint8_t>(ElementalPhysiologyAction::kCount);
}

[[nodiscard]] constexpr bool is_valid_source_body_stage(SourceBodyStage value) noexcept {
    return static_cast<uint8_t>(value) < static_cast<uint8_t>(SourceBodyStage::kCount);
}

[[nodiscard]] constexpr bool is_valid_source_body_integration_bridge(
    SourceBodyIntegrationBridge value) noexcept {
    return static_cast<uint8_t>(value) <
           static_cast<uint8_t>(SourceBodyIntegrationBridge::kCount);
}

[[nodiscard]] constexpr bool is_valid_source_law_system_state(
    SourceLawSystemState value) noexcept {
    return static_cast<uint8_t>(value) < static_cast<uint8_t>(SourceLawSystemState::kCount);
}

[[nodiscard]] std::string_view source_organ_slot_name(SourceOrganSlot value) noexcept;
[[nodiscard]] std::string_view source_body_system_name(SourceBodySystem value) noexcept;
[[nodiscard]] std::string_view organ_system_role_name(OrganSystemRole value) noexcept;
[[nodiscard]] std::string_view source_law_element_name(SourceLawElement value) noexcept;
[[nodiscard]] std::string_view elemental_reaction_stage_name(
    ElementalReactionStage value) noexcept;
[[nodiscard]] std::string_view elemental_physiology_action_name(
    ElementalPhysiologyAction value) noexcept;
[[nodiscard]] std::string_view source_body_stage_name(SourceBodyStage value) noexcept;
[[nodiscard]] std::string_view source_body_integration_bridge_name(
    SourceBodyIntegrationBridge value) noexcept;
[[nodiscard]] std::string_view source_law_system_state_name(
    SourceLawSystemState value) noexcept;

}  // namespace snt::game::source_law
