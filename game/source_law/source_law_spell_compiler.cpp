// Source-law personal spell graph validation and compilation implementation.

#include "game/source_law/source_law_spell_compiler.h"

#include "core/error.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace snt::game::source_law {
namespace {

constexpr size_t kMaxSourceLawIdBytes = 192;
constexpr size_t kMaxSpellGraphNodes = 128;
constexpr size_t kMaxSpellGraphLinks = 512;
constexpr size_t kMaxSpellNodeParameters = 32;
constexpr size_t kMaxPlayerSpellPrograms = 64;
constexpr size_t kMaxSpellDisplayNameBytes = 96;
constexpr uint16_t kMaxSpellControlSteps = 256;

[[nodiscard]] snt::core::Error invalid_argument(std::string message) {
    return {snt::core::ErrorCode::kInvalidArgument, std::move(message)};
}

[[nodiscard]] bool is_valid_id(const SourceLawId& id) noexcept {
    return !id.empty() && id.size() <= kMaxSourceLawIdBytes &&
           id.find('\0') == SourceLawId::npos;
}

template <typename T>
[[nodiscard]] bool contains(const std::vector<T>& values, const T& value) {
    return std::find(values.begin(), values.end(), value) != values.end();
}

template <typename T, typename U>
void append_unique(std::vector<T>& values, U&& value) {
    T normalized{std::forward<U>(value)};
    if (!contains(values, normalized)) values.push_back(std::move(normalized));
}

[[nodiscard]] bool has_unique_ids(const std::vector<SourceLawId>& values) {
    std::set<SourceLawId> seen;
    for (const SourceLawId& value : values) {
        if (!is_valid_id(value) || !seen.insert(value).second) return false;
    }
    return true;
}

struct SpellPortView {
    SourceLawSpellPortType type = SourceLawSpellPortType::kCount;
    bool allows_multiple_links = false;
};

[[nodiscard]] std::optional<SpellPortView> find_intrinsic_port(
    const SourceLawIntrinsicDefinition& definition,
    bool output,
    const SourceLawId& port_id) {
    const std::vector<SourceLawSpellPortType>& types = output
        ? definition.output_port_types
        : definition.input_port_types;
    const std::string prefix = output ? "output." : "input.";
    for (size_t index = 0; index < types.size(); ++index) {
        if (port_id == prefix + std::to_string(index)) {
            return SpellPortView{.type = types[index], .allows_multiple_links = output};
        }
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<SpellPortView> find_operation_port(
    const SourceLawSpellNodeDefinition& definition,
    bool output,
    const SourceLawId& port_id) {
    const std::vector<SourceLawSpellPortDefinition>& ports = output
        ? definition.output_ports
        : definition.input_ports;
    const auto found = std::find_if(ports.begin(), ports.end(), [&port_id](const auto& port) {
        return port.id == port_id;
    });
    if (found == ports.end()) return std::nullopt;
    return SpellPortView{
        .type = found->type,
        .allows_multiple_links = output || found->allows_multiple_links,
    };
}

[[nodiscard]] std::optional<SpellPortView> find_node_port(
    const SourceLawContentSnapshot& content,
    const SourceLawSpellNode& node,
    bool output,
    const SourceLawId& port_id) {
    if (node.kind == SourceLawSpellNodeKind::kBodyIntrinsic) {
        const SourceLawIntrinsicDefinition* intrinsic = content.find_intrinsic(node.definition_id);
        return intrinsic == nullptr ? std::nullopt : find_intrinsic_port(*intrinsic, output, port_id);
    }
    const SourceLawSpellNodeDefinition* definition = content.find_spell_node_definition(
        node.definition_id);
    if (definition == nullptr || definition->kind != node.kind) return std::nullopt;
    return find_operation_port(*definition, output, port_id);
}

[[nodiscard]] bool graph_has_bounded_control_flow(
    const SourceLawContentSnapshot& content,
    const SourceLawSpellGraph& graph,
    const std::map<uint32_t, const SourceLawSpellNode*>& nodes) {
    std::map<uint32_t, std::vector<uint32_t>> adjacency;
    for (const SourceLawSpellLink& link : graph.links) {
        adjacency[link.from_node_id].push_back(link.to_node_id);
    }
    for (const SourceLawSpellNode& node : graph.nodes) {
        if (node.kind != SourceLawSpellNodeKind::kControlFlow) continue;
        const SourceLawSpellNodeDefinition* definition = content.find_spell_node_definition(
            node.definition_id);
        if (definition == nullptr || graph.declared_max_control_steps == 0 ||
            graph.declared_max_control_steps > definition->maximum_control_steps) {
            return false;
        }
    }

    std::map<uint32_t, uint8_t> colors;
    std::vector<uint32_t> stack;
    std::function<bool(uint32_t)> visit = [&](const uint32_t node_id) {
        colors[node_id] = 1;
        stack.push_back(node_id);
        for (const uint32_t next : adjacency[node_id]) {
            if (colors[next] == 0) {
                if (!visit(next)) return false;
                continue;
            }
            if (colors[next] != 1) continue;
            if (graph.declared_max_control_steps == 0) return false;
            bool has_bounded_controller = false;
            for (auto current = stack.rbegin(); current != stack.rend(); ++current) {
                const SourceLawSpellNode* cycle_node = nodes.at(*current);
                if (cycle_node->kind == SourceLawSpellNodeKind::kControlFlow) {
                    const SourceLawSpellNodeDefinition* definition =
                        content.find_spell_node_definition(cycle_node->definition_id);
                    has_bounded_controller = definition != nullptr &&
                        graph.declared_max_control_steps <= definition->maximum_control_steps;
                }
                if (*current == next) break;
            }
            if (!has_bounded_controller) return false;
        }
        stack.pop_back();
        colors[node_id] = 2;
        return true;
    };
    for (const auto& [node_id, node] : nodes) {
        static_cast<void>(node);
        if (colors[node_id] == 0 && !visit(node_id)) return false;
    }
    return true;
}

[[nodiscard]] bool graph_connects_hybrid_systems(
    const SourceLawSpellGraph& graph,
    const std::map<uint32_t, const SourceLawSpellNode*>& nodes,
    const SourceLawContentSnapshot& content,
    const SourceLawHybridLinkDefinition& hybrid) {
    std::map<uint32_t, std::vector<uint32_t>> adjacency;
    for (const SourceLawSpellLink& link : graph.links) {
        adjacency[link.from_node_id].push_back(link.to_node_id);
        adjacency[link.to_node_id].push_back(link.from_node_id);
    }
    std::set<uint32_t> visited;
    for (const auto& [start_id, ignored] : nodes) {
        static_cast<void>(ignored);
        if (!visited.insert(start_id).second) continue;
        std::vector<uint32_t> pending{start_id};
        std::set<SourceBodySystem> systems;
        while (!pending.empty()) {
            const uint32_t node_id = pending.back();
            pending.pop_back();
            const SourceLawSpellNode* node = nodes.at(node_id);
            if (node->kind == SourceLawSpellNodeKind::kBodyIntrinsic &&
                contains(hybrid.required_intrinsic_ids, node->definition_id)) {
                const SourceLawIntrinsicDefinition* intrinsic = content.find_intrinsic(
                    node->definition_id);
                if (intrinsic != nullptr) {
                    for (const SourceBodySystem system : intrinsic->required_closed_systems) {
                        if (contains(hybrid.required_distinct_systems, system)) {
                            systems.insert(system);
                        }
                    }
                }
            }
            for (const uint32_t next : adjacency[node_id]) {
                if (visited.insert(next).second) pending.push_back(next);
            }
        }
        if (systems.size() >= hybrid.minimum_distinct_systems) return true;
    }
    return false;
}

[[nodiscard]] float calculate_effect_budget(const SourceLawBodyCapabilitySnapshot& capabilities) {
    if (!std::isfinite(capabilities.source_throughput) || capabilities.source_throughput <= 0.0F ||
        !std::isfinite(capabilities.stability)) {
        return 0.0F;
    }
    const float stability_factor = 0.35F + 0.65F * std::clamp(capabilities.stability, 0.0F, 100.0F) /
        100.0F;
    const float system_completeness = std::clamp(
        static_cast<float>(capabilities.active_system_ids.size()) /
            static_cast<float>(kSourceBodySystemCount),
        0.0F, 1.0F);
    const float product_completeness = std::clamp(
        static_cast<float>(capabilities.available_product_ids.size()) /
            static_cast<float>(kSourceBodySystemCount),
        0.0F, 1.0F);
    const float reaction_factor = 0.2F + 0.4F * system_completeness +
        0.4F * product_completeness;
    return capabilities.source_throughput * stability_factor * reaction_factor;
}

void append_blocking(SourceLawSpellCompileReport& report, SourceLawId reason) {
    append_unique(report.blocking_reason_ids, std::move(reason));
}

[[nodiscard]] CompiledSourceLawSpell compile_impl(
    const SourceLawContentSnapshot& content,
    const SourceLawBodyCapabilitySnapshot& capabilities,
    const SourceLawSpellGraph& graph,
    const bool graph_requires_unification,
    SourceLawSpellProgramId program_id,
    const uint32_t source_revision) {
    CompiledSourceLawSpell compiled{
        .program_id = program_id,
        .source_revision = source_revision,
        .body_revision = capabilities.body_revision,
    };
    SourceLawSpellCompileReport& report = compiled.report;
    report.available_effect_budget = calculate_effect_budget(capabilities);
    report.primary_circuit_system_id = capabilities.circuit_schedule.current_primary_circuit_system_id;
    report.coordinating_circuit_system_ids =
        capabilities.circuit_schedule.coordinating_circuit_system_ids;

    if (auto result = SourceLawSpellCompiler::validate_graph(content, graph); !result) {
        append_blocking(report, "source_law.reason.spell.graph_invalid");
        return compiled;
    }
    if (graph_requires_unification && !capabilities.integration.unification_circuit_online) {
        append_blocking(report, "source_law.reason.spell.unification_required");
    }
    if (graph.declared_primary_system_ids.size() > 0 &&
        (!report.primary_circuit_system_id ||
         !contains(graph.declared_primary_system_ids, *report.primary_circuit_system_id))) {
        append_blocking(report, "source_law.reason.spell.primary_circuit_not_scheduled");
    }
    for (const SourceLawId& system_id : graph.declared_coordinating_system_ids) {
        if (!contains(report.coordinating_circuit_system_ids, system_id)) {
            append_blocking(report, "source_law.reason.spell.coordinating_circuit_not_scheduled." + system_id);
        }
    }

    std::map<uint32_t, const SourceLawSpellNode*> nodes;
    std::set<SourceLawId> graph_intrinsic_ids;
    std::vector<SourceLawId> emitted_byproducts;
    std::vector<SourceLawId> handled_byproducts;
    const SourcePathDefinition* active_path = capabilities.active_path_id.empty()
        ? nullptr
        : content.find_path(capabilities.active_path_id);
    const auto add_mana_cost = [&report](const int32_t mana_cost) {
        if (mana_cost > std::numeric_limits<int32_t>::max() - report.mana_cost) {
            append_blocking(report, "source_law.reason.spell.mana_cost_overflow");
            return;
        }
        report.mana_cost += mana_cost;
    };

    for (const SourceLawSpellNode& node : graph.nodes) {
        nodes.emplace(node.stable_node_id, &node);
        if (node.kind == SourceLawSpellNodeKind::kBodyIntrinsic) {
            const SourceLawIntrinsicDefinition* intrinsic = content.find_intrinsic(node.definition_id);
            if (intrinsic == nullptr ||
                !contains(capabilities.available_intrinsic_ids, node.definition_id)) {
                append_blocking(report, "source_law.reason.spell.intrinsic_unavailable." +
                                            node.definition_id);
                continue;
            }
            graph_intrinsic_ids.insert(node.definition_id);
            append_unique(compiled.executable_operation_ids, node.definition_id);
            report.required_throughput += intrinsic->required_throughput;
            add_mana_cost(intrinsic->mana_cost);
            for (const SourceLawId& tag : intrinsic->risk_tags) append_unique(report.risk_tags, tag);
            for (const SourceLawId& tag : intrinsic->emitted_byproduct_tags) {
                append_unique(emitted_byproducts, tag);
            }
            if (intrinsic->requires_primary_circuit && !report.primary_circuit_system_id) {
                append_blocking(report, "source_law.reason.spell.primary_circuit_required");
            }
            if (intrinsic->requires_unification_circuit &&
                !capabilities.integration.unification_circuit_online) {
                append_blocking(report, "source_law.reason.spell.unification_required");
            }
            continue;
        }

        const SourceLawSpellNodeDefinition* definition = content.find_spell_node_definition(
            node.definition_id);
        if (definition == nullptr) continue;
        bool usable = true;
        if (definition->requires_primary_circuit && !report.primary_circuit_system_id) {
            append_blocking(report, "source_law.reason.spell.primary_circuit_required");
            usable = false;
        }
        if (definition->requires_unification_circuit &&
            !capabilities.integration.unification_circuit_online) {
            append_blocking(report, "source_law.reason.spell.unification_required");
            usable = false;
        }
        if (node.kind == SourceLawSpellNodeKind::kPathCore) {
            if (active_path == nullptr || !capabilities.active_path_is_resonant ||
                !contains(active_path->path_core_operation_ids, node.definition_id)) {
                append_blocking(report, "source_law.reason.spell.path_core_unavailable." +
                                            node.definition_id);
                usable = false;
            } else {
                append_unique(report.applied_path_core_ids, node.definition_id);
            }
        }
        if (!usable) continue;
        append_unique(compiled.executable_operation_ids, node.definition_id);
        report.required_throughput += definition->required_throughput;
        add_mana_cost(definition->mana_cost);
        for (const SourceLawId& tag : definition->risk_tags) append_unique(report.risk_tags, tag);
        for (const SourceLawId& tag : definition->emitted_byproduct_tags) {
            append_unique(emitted_byproducts, tag);
        }
        for (const SourceLawId& tag : definition->handled_byproduct_tags) {
            append_unique(handled_byproducts, tag);
        }
    }

    std::set<SourceBodySystem> active_body_systems;
    for (const SourceLawId& system_id : capabilities.active_system_ids) {
        const OrganSystemDefinition* system = content.find_system(system_id);
        if (system != nullptr) active_body_systems.insert(system->body_system);
    }
    for (const SourceLawId& hybrid_id : graph.requested_hybrid_link_ids) {
        const SourceLawHybridLinkDefinition* hybrid = content.find_hybrid_link(hybrid_id);
        if (hybrid == nullptr) {
            append_blocking(report, "source_law.reason.spell.hybrid_missing." + hybrid_id);
            continue;
        }
        const bool systems_available = std::all_of(
            hybrid->required_distinct_systems.begin(), hybrid->required_distinct_systems.end(),
            [&active_body_systems](const SourceBodySystem system) {
                return active_body_systems.contains(system);
            });
        const bool intrinsics_available = std::all_of(
            hybrid->required_intrinsic_ids.begin(), hybrid->required_intrinsic_ids.end(),
            [&graph_intrinsic_ids, &capabilities](const SourceLawId& intrinsic_id) {
                return graph_intrinsic_ids.contains(intrinsic_id) &&
                    contains(capabilities.available_intrinsic_ids, intrinsic_id);
            });
        const bool products_available = std::all_of(
            hybrid->required_product_ids.begin(), hybrid->required_product_ids.end(),
            [&capabilities](const SourceLawId& product_id) {
                return contains(capabilities.available_product_ids, product_id);
            });
        if (!systems_available || !intrinsics_available || !products_available ||
            !graph_connects_hybrid_systems(graph, nodes, content, *hybrid)) {
            append_blocking(report, "source_law.reason.spell.hybrid_unsatisfied." + hybrid_id);
            continue;
        }
        append_unique(report.satisfied_hybrid_link_ids, hybrid_id);
        for (const SourceLawId& tag : hybrid->emitted_byproduct_tags) {
            append_unique(emitted_byproducts, tag);
        }
    }

    for (const SourceLawId& byproduct : emitted_byproducts) {
        if (!contains(handled_byproducts, byproduct)) {
            append_blocking(report, "source_law.reason.spell.byproduct_unhandled." + byproduct);
        }
    }
    if (!std::isfinite(report.required_throughput) || report.required_throughput < 0.0F ||
        !std::isfinite(report.available_effect_budget) || report.available_effect_budget <= 0.0F) {
        append_blocking(report, "source_law.reason.spell.invalid_effect_budget");
    } else if (capabilities.source_throughput < report.required_throughput) {
        append_blocking(report, "source_law.reason.spell.insufficient_throughput");
    }
    report.is_compilable = report.blocking_reason_ids.empty();
    return compiled;
}

}  // namespace

snt::core::Expected<void> SourceLawSpellCompiler::validate_graph(
    const SourceLawContentSnapshot& content,
    const SourceLawSpellGraph& graph) {
    if (!is_valid_source_law_spell_graph_kind(graph.kind) || graph.nodes.empty() ||
        graph.nodes.size() > kMaxSpellGraphNodes || graph.links.size() > kMaxSpellGraphLinks ||
        graph.declared_max_control_steps > kMaxSpellControlSteps ||
        !has_unique_ids(graph.required_path_core_ids) ||
        !has_unique_ids(graph.requested_hybrid_link_ids) ||
        !has_unique_ids(graph.declared_primary_system_ids) ||
        !has_unique_ids(graph.declared_coordinating_system_ids)) {
        return invalid_argument("Source-law spell graph has an invalid shape");
    }
    std::map<uint32_t, const SourceLawSpellNode*> nodes;
    std::set<SourceLawId> path_core_nodes;
    bool has_intrinsic = false;
    bool has_output = false;
    for (const SourceLawSpellNode& node : graph.nodes) {
        if (node.stable_node_id == 0 || !nodes.emplace(node.stable_node_id, &node).second ||
            !is_valid_source_law_spell_node_kind(node.kind) || !is_valid_id(node.definition_id) ||
            node.parameter_ids.size() > kMaxSpellNodeParameters ||
            !has_unique_ids(node.parameter_ids)) {
            return invalid_argument("Source-law spell graph contains an invalid node");
        }
        if (node.kind == SourceLawSpellNodeKind::kBodyIntrinsic) {
            if (content.find_intrinsic(node.definition_id) == nullptr) {
                return invalid_argument("Source-law spell graph references an unknown body intrinsic");
            }
            has_intrinsic = true;
        } else {
            const SourceLawSpellNodeDefinition* definition = content.find_spell_node_definition(
                node.definition_id);
            if (definition == nullptr || definition->kind != node.kind) {
                return invalid_argument("Source-law spell graph references an incompatible node definition");
            }
            if (node.kind == SourceLawSpellNodeKind::kPathCore) {
                path_core_nodes.insert(node.definition_id);
            }
            has_output = has_output || node.kind == SourceLawSpellNodeKind::kOutput;
        }
    }
    if (!has_intrinsic || !has_output) {
        return invalid_argument("Source-law spell graph must contain a body intrinsic and an output");
    }
    for (const SourceLawId& path_core_id : graph.required_path_core_ids) {
        if (!path_core_nodes.contains(path_core_id)) {
            return invalid_argument("Source-law spell graph requires a path core it does not contain");
        }
    }
    for (const SourceLawId& hybrid_id : graph.requested_hybrid_link_ids) {
        if (content.find_hybrid_link(hybrid_id) == nullptr) {
            return invalid_argument("Source-law spell graph references an unknown hybrid link");
        }
    }
    for (const SourceLawId& system_id : graph.declared_primary_system_ids) {
        if (content.find_system(system_id) == nullptr) {
            return invalid_argument("Source-law spell graph declares an unknown primary system");
        }
    }
    for (const SourceLawId& system_id : graph.declared_coordinating_system_ids) {
        if (content.find_system(system_id) == nullptr) {
            return invalid_argument("Source-law spell graph declares an unknown coordinating system");
        }
    }

    std::set<std::pair<uint32_t, SourceLawId>> occupied_inputs;
    std::set<std::tuple<uint32_t, SourceLawId, uint32_t, SourceLawId>> unique_links;
    for (const SourceLawSpellLink& link : graph.links) {
        const auto from = nodes.find(link.from_node_id);
        const auto to = nodes.find(link.to_node_id);
        if (from == nodes.end() || to == nodes.end() || !is_valid_id(link.from_port_id) ||
            !is_valid_id(link.to_port_id)) {
            return invalid_argument("Source-law spell graph contains an invalid link");
        }
        const std::optional<SpellPortView> output = find_node_port(
            content, *from->second, true, link.from_port_id);
        const std::optional<SpellPortView> input = find_node_port(
            content, *to->second, false, link.to_port_id);
        if (!output || !input || output->type != input->type ||
            !unique_links.emplace(link.from_node_id, link.from_port_id, link.to_node_id,
                                  link.to_port_id).second ||
            (!input->allows_multiple_links &&
             !occupied_inputs.emplace(link.to_node_id, link.to_port_id).second)) {
            return invalid_argument("Source-law spell graph has an invalid port connection");
        }
    }
    if (!graph_has_bounded_control_flow(content, graph, nodes)) {
        return invalid_argument("Source-law spell graph has unbounded control flow");
    }
    return {};
}

CompiledSourceLawSpell SourceLawSpellCompiler::compile_program(
    const SourceLawContentSnapshot& content,
    const SourceLawBodyCapabilitySnapshot& capabilities,
    const PlayerSourceLawSpellProgram& program) {
    CompiledSourceLawSpell compiled = compile_impl(content, capabilities, program.graph, false,
                                                   program.program_id, program.source_revision);
    if (program.graph.kind != SourceLawSpellGraphKind::kPlayerAuthored) {
        append_blocking(compiled.report, "source_law.reason.spell.program_not_player_authored");
        compiled.report.is_compilable = false;
    }
    return compiled;
}

CompiledSourceLawSpell SourceLawSpellCompiler::compile_graph(
    const SourceLawContentSnapshot& content,
    const SourceLawBodyCapabilitySnapshot& capabilities,
    const SourceLawSpellGraphDefinition& definition,
    SourceLawSpellProgramId program_id,
    uint32_t source_revision) {
    return compile_impl(content, capabilities, definition.graph,
                        definition.requires_unification_circuit, program_id, source_revision);
}

bool SourceLawSpellCompiler::is_current(const CompiledSourceLawSpell& compiled,
                                        const PlayerSourceLawSpellProgram& program,
                                        uint64_t body_revision) noexcept {
    return compiled.program_id == program.program_id &&
           compiled.source_revision == program.source_revision &&
           compiled.body_revision == body_revision;
}

snt::core::Expected<SourceLawSpellProgramEditResult> SourceLawSpellProgramService::edit(
    const SourceLawContentSnapshot& content,
    const PlayerSourceLawState& state,
    SourceLawSpellProgramEditRequest request) {
    if (request.program_id.value == 0 || request.display_name.empty() ||
        request.display_name.size() > kMaxSpellDisplayNameBytes ||
        request.display_name.find('\0') != std::string::npos ||
        request.graph.kind != SourceLawSpellGraphKind::kPlayerAuthored) {
        return invalid_argument("Source-law player spell program edit request is invalid");
    }
    if (request.copied_from_graph_id) {
        const SourceLawSpellGraphDefinition* source = content.find_spell_graph(
            *request.copied_from_graph_id);
        if (source == nullptr || !source->is_copyable_to_player_library) {
            return invalid_argument("Source-law player spell program references a non-copyable graph");
        }
    }
    if (auto result = SourceLawSpellCompiler::validate_graph(content, request.graph); !result) {
        return result.error();
    }

    SourceLawSpellProgramEditResult result{.state = state};
    const auto existing = std::find_if(result.state.personal_spell_programs.begin(),
                                       result.state.personal_spell_programs.end(),
                                       [&request](const auto& program) {
                                           return program.program_id == request.program_id;
                                       });
    uint32_t next_revision = 1;
    if (existing != result.state.personal_spell_programs.end()) {
        if (existing->source_revision == std::numeric_limits<uint32_t>::max()) {
            return invalid_argument("Source-law player spell program revision is exhausted");
        }
        next_revision = existing->source_revision + 1;
    } else if (result.state.personal_spell_programs.size() >= kMaxPlayerSpellPrograms) {
        return invalid_argument("Source-law player spell program capacity is full");
    }
    result.program = {
        .program_id = request.program_id,
        .copied_from_graph_id = std::move(request.copied_from_graph_id),
        .display_name = std::move(request.display_name),
        .graph = std::move(request.graph),
        .source_revision = next_revision,
    };
    if (existing != result.state.personal_spell_programs.end()) {
        *existing = result.program;
    } else {
        result.state.personal_spell_programs.push_back(result.program);
    }
    return result;
}

snt::core::Expected<SourceLawSpellProgramEditResult> SourceLawSpellProgramService::copy_preset(
    const SourceLawContentSnapshot& content,
    const PlayerSourceLawState& state,
    SourceLawSpellProgramId program_id,
    const SourceLawId& preset_graph_id,
    std::string display_name) {
    const SourceLawSpellGraphDefinition* preset = content.find_spell_graph(preset_graph_id);
    if (preset == nullptr || !preset->is_copyable_to_player_library) {
        return invalid_argument("Source-law preset graph is missing or cannot be copied");
    }
    SourceLawSpellGraph graph = preset->graph;
    graph.kind = SourceLawSpellGraphKind::kPlayerAuthored;
    return edit(content, state, {
        .program_id = program_id,
        .copied_from_graph_id = preset_graph_id,
        .display_name = std::move(display_name),
        .graph = std::move(graph),
    });
}

}  // namespace snt::game::source_law
