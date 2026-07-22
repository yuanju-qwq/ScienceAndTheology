// Source-law graph validation, player program editing, and compilation.
//
// The compiler consumes only immutable content and evaluator value snapshots.
// It produces a disposable runtime cache; player-owned source graphs remain
// the sole persisted authority for personal source-law spells.

#pragma once

#include "core/expected.h"
#include "game/source_law/source_law_evaluator.h"

#include <optional>
#include <string>

namespace snt::game::source_law {

struct SourceLawSpellProgramEditRequest {
    SourceLawSpellProgramId program_id;
    std::optional<SourceLawId> copied_from_graph_id;
    std::string display_name;
    SourceLawSpellGraph graph;
};

struct SourceLawSpellProgramEditResult {
    PlayerSourceLawState state;
    PlayerSourceLawSpellProgram program;
};

class SourceLawSpellCompiler final {
public:
    // Validates content references, ports and bounded control flow without
    // requiring a currently compatible body.
    [[nodiscard]] static snt::core::Expected<void> validate_graph(
        const SourceLawContentSnapshot& content,
        const SourceLawSpellGraph& graph);

    [[nodiscard]] static CompiledSourceLawSpell compile_program(
        const SourceLawContentSnapshot& content,
        const SourceLawBodyCapabilitySnapshot& capabilities,
        const PlayerSourceLawSpellProgram& program);

    [[nodiscard]] static CompiledSourceLawSpell compile_graph(
        const SourceLawContentSnapshot& content,
        const SourceLawBodyCapabilitySnapshot& capabilities,
        const SourceLawSpellGraphDefinition& definition,
        SourceLawSpellProgramId program_id = {},
        uint32_t source_revision = 0);

    [[nodiscard]] static bool is_current(const CompiledSourceLawSpell& compiled,
                                         const PlayerSourceLawSpellProgram& program,
                                         uint64_t body_revision) noexcept;
};

class SourceLawSpellProgramService final {
public:
    // Creates or replaces a player-owned graph. Replacing a program advances
    // source_revision; no compiled cache is persisted or mutated here.
    [[nodiscard]] static snt::core::Expected<SourceLawSpellProgramEditResult> edit(
        const SourceLawContentSnapshot& content,
        const PlayerSourceLawState& state,
        SourceLawSpellProgramEditRequest request);

    // Path and creature graphs remain content-owned. A copy becomes a player
    // graph, preserving only the source graph id for provenance and UI.
    [[nodiscard]] static snt::core::Expected<SourceLawSpellProgramEditResult> copy_preset(
        const SourceLawContentSnapshot& content,
        const PlayerSourceLawState& state,
        SourceLawSpellProgramId program_id,
        const SourceLawId& preset_graph_id,
        std::string display_name);
};

}  // namespace snt::game::source_law
