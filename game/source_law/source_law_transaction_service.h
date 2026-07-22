// Atomic value transactions for source-law body mutations.
//
// Inventory/material ownership remains with the server player service. This
// module prepares a complete body candidate and exposes a narrow reservation
// interface so a later server transaction can commit material and body state
// together without reintroducing Godot-side mutation.

#pragma once

#include "core/expected.h"
#include "game/source_law/source_law_evaluator.h"

#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace snt::game::source_law {

struct SourceLawMaterialCost {
    SourceLawId resource_id;
    int64_t amount = 0;
};

class ISourceLawMaterialReservation {
public:
    virtual ~ISourceLawMaterialReservation() = default;

    // Implementations reserve against an authoritative inventory or altar
    // ledger, then either commit after body assignment or roll back on error.
    [[nodiscard]] virtual snt::core::Expected<void> reserve(
        const std::vector<SourceLawMaterialCost>& costs) = 0;
    [[nodiscard]] virtual snt::core::Expected<void> commit() = 0;
    virtual void rollback() noexcept = 0;
};

enum class SourceLawTransactionEventKind : uint8_t {
    kOrganImplanted = 0,
    kOrganRemoved,
    kPathAnchored,
    kCircuitScheduled,
    kSpellGraphChanged,
    kSpellCompilationChanged,
    kSystemStateChanged,
    kBodyIntegrationStateChanged,
};

struct SourceLawTransactionEvent {
    SourceLawTransactionEventKind kind = SourceLawTransactionEventKind::kOrganImplanted;
    SourceLawId subject_id;
    SourceLawId definition_id;
    SourceOrganSlot slot = SourceOrganSlot::kCount;
    SourceLawSystemState previous_system_state = SourceLawSystemState::kUnavailable;
    SourceLawSystemState current_system_state = SourceLawSystemState::kUnavailable;
    SourceBodyStage previous_body_stage = SourceBodyStage::kDormant;
    SourceBodyStage current_body_stage = SourceBodyStage::kDormant;
    SourceLawSpellProgramId spell_program_id;
    uint32_t source_revision = 0;
    uint64_t body_revision = 0;
    bool is_compilable = false;
    std::vector<SourceLawId> blocking_reason_ids;
};

class ISourceLawEventSink {
public:
    virtual ~ISourceLawEventSink() = default;
    virtual void on_source_law_transaction_event(const SourceLawTransactionEvent& event) = 0;
};

struct SourceLawImplantRequest {
    SourceOrganSlot slot = SourceOrganSlot::kCount;
    SourceLawId organ_definition_id;
    SourceLawId quality_id;
    uint16_t initial_growth_level = 0;
    int32_t source_reserve_cost = 0;
    std::vector<SourceLawId> tuning_tags;
    std::string_view diagnostic_subject_id;
};

struct SourceLawRemoveOrganRequest {
    SourceOrganSlot slot = SourceOrganSlot::kCount;
    std::string_view diagnostic_subject_id;
};

struct SourceLawAnchorPathRequest {
    SourceLawId path_id;
    std::string_view diagnostic_subject_id;
};

struct SourceLawScheduleRequest {
    std::optional<SourceLawId> primary_system_id;
    std::vector<SourceLawId> coordinating_system_ids;
    float reallocation_cooldown_seconds = 0.0F;
    bool respect_existing_cooldown = true;
    std::string_view diagnostic_subject_id;
};

struct SourceLawTransactionResult {
    SourceLawBodyState body;
    SourceLawEvaluation evaluation;
    std::vector<SourceLawTransactionEvent> events;
};

class SourceLawTransactionService final {
public:
    [[nodiscard]] static snt::core::Expected<SourceLawTransactionResult> implant(
        const SourceLawContentSnapshot& content,
        const SourceLawBodyState& body,
        const SourceLawImplantRequest& request,
        const SourceLawEvaluationContext& context = {});

    [[nodiscard]] static snt::core::Expected<SourceLawTransactionResult> remove_organ(
        const SourceLawContentSnapshot& content,
        const SourceLawBodyState& body,
        const SourceLawRemoveOrganRequest& request,
        const SourceLawEvaluationContext& context = {});

    [[nodiscard]] static snt::core::Expected<SourceLawTransactionResult> anchor_path(
        const SourceLawContentSnapshot& content,
        const SourceLawBodyState& body,
        const SourceLawAnchorPathRequest& request,
        const SourceLawEvaluationContext& context = {});

    [[nodiscard]] static snt::core::Expected<SourceLawTransactionResult> schedule_circuits(
        const SourceLawContentSnapshot& content,
        const SourceLawBodyState& body,
        const SourceLawScheduleRequest& request,
        const SourceLawEvaluationContext& context = {});

    static void publish_events(const SourceLawTransactionResult& result,
                               ISourceLawEventSink* sink) noexcept;
};

}  // namespace snt::game::source_law
