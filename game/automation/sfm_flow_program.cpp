// Compiled SFM flow-program implementation.

#define SNT_LOG_CHANNEL "game.automation.sfm"
#include "game/automation/sfm_flow_program.h"

#include "core/error.h"
#include "core/log.h"

#include <algorithm>
#include <limits>
#include <string>
#include <unordered_set>
#include <utility>

namespace snt::game {
namespace {

[[nodiscard]] snt::core::Error invalid_argument(std::string message) {
    return {snt::core::ErrorCode::kInvalidArgument, std::move(message)};
}

[[nodiscard]] snt::core::Error invalid_state(std::string message) {
    return {snt::core::ErrorCode::kInvalidState, std::move(message)};
}

[[nodiscard]] bool is_known_node_type(SfmFlowNodeType type) noexcept {
    return type == SfmFlowNodeType::kInterval || type == SfmFlowNodeType::kTransfer;
}

[[nodiscard]] uint64_t connection_key(uint32_t source, uint32_t destination) noexcept {
    return (static_cast<uint64_t>(source) << 32u) | destination;
}

[[nodiscard]] bool is_absent_transfer(const SfmResourceTransferRule& transfer) noexcept {
    return transfer.source.value.empty() && transfer.destination.value.empty() &&
        transfer.requested.is_absent();
}

void saturating_add(int64_t& target, int64_t amount) noexcept {
    if (amount <= 0) return;
    if (target > std::numeric_limits<int64_t>::max() - amount) {
        target = std::numeric_limits<int64_t>::max();
    } else {
        target += amount;
    }
}

}  // namespace

snt::core::Expected<void> validate_sfm_flow_program(
    const SfmFlowProgramRecord& program,
    SfmFlowProgramValidationLimits limits) {
    if (program.nodes.size() > limits.max_nodes ||
        program.connections.size() > limits.max_connections) {
        return invalid_argument("SFM flow program exceeds durable limits");
    }

    std::unordered_map<SfmFlowNodeId, SfmFlowNodeType> node_types;
    node_types.reserve(program.nodes.size());
    for (const SfmFlowNodeRecord& node : program.nodes) {
        if (node.id == kInvalidSfmFlowNodeId || !is_known_node_type(node.type) ||
            !node_types.emplace(node.id, node.type).second) {
            return invalid_argument("SFM flow program has an invalid node identity");
        }
        if (node.type == SfmFlowNodeType::kInterval) {
            if (node.interval_ticks == 0 || !is_absent_transfer(node.transfer)) {
                return invalid_argument("SFM interval node is malformed");
            }
        } else if (node.interval_ticks != 0 || !node.transfer.is_valid()) {
            return invalid_argument("SFM transfer node is malformed");
        }
    }

    std::unordered_map<SfmFlowNodeId, std::vector<SfmFlowNodeId>> outgoing;
    outgoing.reserve(program.nodes.size());
    std::unordered_set<uint64_t> seen_connections;
    seen_connections.reserve(program.connections.size());
    for (const SfmFlowConnectionRecord& connection : program.connections) {
        const auto source = node_types.find(connection.source);
        const auto destination = node_types.find(connection.destination);
        if (source == node_types.end() || destination == node_types.end() ||
            destination->second == SfmFlowNodeType::kInterval) {
            return invalid_argument("SFM flow connection has an unavailable or invalid endpoint");
        }
        if (!seen_connections.insert(connection_key(connection.source, connection.destination)).second) {
            return invalid_argument("SFM flow program has duplicate connections");
        }
        outgoing[connection.source].push_back(connection.destination);
    }

    // Transfer-only cycles never yield to an interval timer. Reject them at
    // persistence/admission time instead of accepting a graph that can only
    // become offline at its runtime compilation boundary.
    std::unordered_map<SfmFlowNodeId, uint8_t> visit_state;
    visit_state.reserve(program.nodes.size());
    const auto visit = [&](const auto& self, SfmFlowNodeId id) -> bool {
        visit_state[id] = 1;
        const auto edges = outgoing.find(id);
        if (edges != outgoing.end()) {
            for (const SfmFlowNodeId destination : edges->second) {
                if (node_types.at(destination) == SfmFlowNodeType::kInterval) continue;
                const uint8_t state = visit_state[destination];
                if (state == 1) return false;
                if (state == 0 && !self(self, destination)) return false;
            }
        }
        visit_state[id] = 2;
        return true;
    };
    for (const auto& [node_id, type] : node_types) {
        if (type == SfmFlowNodeType::kInterval || visit_state[node_id] != 0) continue;
        if (!visit(visit, node_id)) {
            return invalid_argument("SFM flow program contains an immediate execution cycle");
        }
    }
    return {};
}

snt::core::Expected<SfmCompiledFlowProgram> SfmFlowProgramCompiler::compile(
    const SfmFlowProgramRecord& record,
    const SfmEndpointRegistry& endpoint_registry,
    const IResourceKeyResolver& resource_resolver,
    SfmFlowCompileLimits limits) {
    if (!resource_resolver.key_context().is_valid()) {
        return invalid_argument("SFM flow compilation requires a valid resource snapshot");
    }
    if (record.nodes.empty() || limits.max_node_dispatches_per_tick == 0) {
        return invalid_argument("SFM flow record exceeds its configured compile limits");
    }
    if (auto result = validate_sfm_flow_program(
            record, {.max_nodes = limits.max_nodes, .max_connections = limits.max_connections});
        !result) {
        return result.error();
    }

    SfmCompiledFlowProgram result;
    result.resource_context_ = resource_resolver.key_context();
    result.source_revision_ = record.revision;
    result.max_node_dispatches_per_tick_ = limits.max_node_dispatches_per_tick;
    result.connection_count_ = record.connections.size();
    result.nodes_.reserve(record.nodes.size());
    result.node_indices_.reserve(record.nodes.size());

    for (const SfmFlowNodeRecord& source : record.nodes) {
        SfmCompiledFlowProgram::Node node{
            .id = source.id,
            .type = source.type,
        };
        if (source.type == SfmFlowNodeType::kInterval) {
            if (source.interval_ticks == 0 || source.transfer.is_valid()) {
                return invalid_argument("SFM interval node has invalid durable fields");
            }
            node.interval_ticks = source.interval_ticks;
        } else {
            if (source.interval_ticks != 0 || !source.transfer.is_valid()) {
                return invalid_argument("SFM transfer node has invalid durable fields");
            }
            auto transfer = endpoint_registry.compile_transfer(source.transfer, resource_resolver);
            if (!transfer) return transfer.error();
            node.transfer = std::move(*transfer);
        }
        const uint32_t index = static_cast<uint32_t>(result.nodes_.size());
        result.node_indices_.emplace(source.id, index);
        result.nodes_.push_back(std::move(node));
    }

    for (const SfmFlowConnectionRecord& connection : record.connections) {
        const auto source = result.node_indices_.find(connection.source);
        const auto destination = result.node_indices_.find(connection.destination);
        if (source == result.node_indices_.end() || destination == result.node_indices_.end()) {
            return invalid_argument("SFM flow connection refers to an unavailable node");
        }
        result.nodes_[source->second].outgoing.push_back(destination->second);
    }

    return result;
}

snt::core::Expected<SfmFlowExecutor> SfmFlowExecutor::create(
    SfmCompiledFlowProgram program,
    const SfmEndpointRegistry& endpoint_registry) {
    if (!program.is_valid()) {
        return invalid_argument("SFM flow executor requires a valid compiled program");
    }
    return SfmFlowExecutor{std::move(program), endpoint_registry};
}

SfmFlowExecutor::SfmFlowExecutor(SfmCompiledFlowProgram program,
                                 const SfmEndpointRegistry& endpoint_registry) noexcept
    : program_(std::move(program)), endpoint_registry_(&endpoint_registry) {}

snt::core::Expected<void> SfmFlowExecutor::trigger(SfmFlowNodeId node_id) {
    const auto found = program_.node_indices_.find(node_id);
    if (found == program_.node_indices_.end()) {
        return invalid_argument("SFM flow trigger refers to an unavailable node");
    }
    pending_nodes_.push_back(found->second);
    return {};
}

snt::core::Expected<SfmFlowExecutionResult> SfmFlowExecutor::tick(uint64_t tick_index) {
    if (endpoint_registry_ == nullptr || !program_.is_valid()) {
        return invalid_state("SFM flow executor has no valid compiled program or endpoint registry");
    }
    if (started_ && tick_index < last_tick_index_) {
        return invalid_argument("SFM flow tick index moved backwards");
    }
    if (!started_) {
        schedule_intervals(tick_index);
        started_ = true;
    }
    last_tick_index_ = tick_index;

    while (!due_nodes_.empty() && due_nodes_.top().tick_index <= tick_index) {
        const DueNode due = due_nodes_.top();
        due_nodes_.pop();
        if (due.node_index >= program_.nodes_.size()) {
            return invalid_state("SFM flow executor contains an invalid scheduled node");
        }
        const auto& node = program_.nodes_[due.node_index];
        if (node.type != SfmFlowNodeType::kInterval || node.interval_ticks == 0) {
            return invalid_state("SFM flow executor scheduled a non-interval node");
        }
        pending_nodes_.push_back(due.node_index);
        due_nodes_.push({
            .tick_index = tick_index > std::numeric_limits<uint64_t>::max() - node.interval_ticks
                ? std::numeric_limits<uint64_t>::max()
                : tick_index + node.interval_ticks,
            .node_index = due.node_index,
        });
    }

    SfmFlowExecutionResult result;
    size_t cursor = 0;
    while (cursor < pending_nodes_.size()) {
        if (result.dispatched_nodes >= program_.max_node_dispatches_per_tick_) {
            result.dispatch_budget_exhausted = true;
            SNT_LOG_WARN("SFM flow revision=%llu reached dispatch budget=%u at tick=%llu; "
                         "remaining immediate work was discarded",
                         static_cast<unsigned long long>(program_.source_revision_),
                         static_cast<unsigned int>(program_.max_node_dispatches_per_tick_),
                         static_cast<unsigned long long>(tick_index));
            break;
        }
        const uint32_t node_index = pending_nodes_[cursor++];
        if (node_index >= program_.nodes_.size()) {
            return invalid_state("SFM flow pending queue contains an invalid node");
        }
        const auto& node = program_.nodes_[node_index];
        ++result.dispatched_nodes;
        if (node.type == SfmFlowNodeType::kTransfer) {
            auto transferred = endpoint_registry_->execute_transfer(
                program_.resource_context_, node.transfer, ResourceTransferMode::kExecute);
            if (!transferred) return transferred.error();
            ++result.executed_transfers;
            saturating_add(result.transferred_units, transferred->transferred.amount);
        }
        enqueue_outgoing(node_index);
    }
    pending_nodes_.clear();
    return result;
}

void SfmFlowExecutor::schedule_intervals(uint64_t first_tick) noexcept {
    for (uint32_t index = 0; index < program_.nodes_.size(); ++index) {
        if (program_.nodes_[index].type != SfmFlowNodeType::kInterval) continue;
        due_nodes_.push({.tick_index = first_tick, .node_index = index});
    }
}

void SfmFlowExecutor::enqueue_outgoing(uint32_t node_index) {
    const auto& outgoing = program_.nodes_[node_index].outgoing;
    pending_nodes_.insert(pending_nodes_.end(), outgoing.begin(), outgoing.end());
}

}  // namespace snt::game
