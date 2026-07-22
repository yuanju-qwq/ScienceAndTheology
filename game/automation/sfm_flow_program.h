// Compiled SFM flow-program contract.
//
// SfmFlowProgramRecord is durable/editor-facing data: it uses stable endpoint
// addresses and ResourceContentStack.  Compilation resolves all of that once
// into dense node indices and SfmBoundResourceTransfer values.  The executor
// then advances only compact node indices, ResourceKey/ResourceStack values,
// and generation-checked endpoint handles on its fixed-tick path.

#pragma once

#include "core/expected.h"
#include "game/automation/sfm_endpoint_registry.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <queue>
#include <unordered_map>
#include <vector>

namespace snt::game {

using SfmFlowNodeId = uint32_t;
inline constexpr SfmFlowNodeId kInvalidSfmFlowNodeId = 0;

enum class SfmFlowNodeType : uint8_t {
    kInterval = 0,
    kTransfer = 1,
};

// Durable node data.  An interval node is a source that fires its outgoing
// edges every `interval_ticks`; a transfer node moves one resource stack and
// then activates its outgoing edges.  More node kinds can be appended without
// changing the fixed-width runtime resource representation.
struct SfmFlowNodeRecord {
    SfmFlowNodeId id = kInvalidSfmFlowNodeId;
    SfmFlowNodeType type = SfmFlowNodeType::kInterval;
    uint32_t interval_ticks = 0;
    SfmResourceTransferRule transfer;
};

struct SfmFlowConnectionRecord {
    SfmFlowNodeId source = kInvalidSfmFlowNodeId;
    SfmFlowNodeId destination = kInvalidSfmFlowNodeId;

    friend bool operator==(const SfmFlowConnectionRecord&,
                           const SfmFlowConnectionRecord&) = default;
};

// The entire persistent graph for one SFM manager/controller.  IDs are
// explicit instead of generated on load so UI edits, save records, and future
// collaboration commands can conditionally target a stable graph element.
struct SfmFlowProgramRecord {
    uint64_t revision = 0;
    std::vector<SfmFlowNodeRecord> nodes;
    std::vector<SfmFlowConnectionRecord> connections;
};

// An unversioned client/editor proposal.  The server combines this durable
// graph body with the caller's expected revision and assigns the next
// authoritative revision atomically.  Keeping the revision out of this type
// prevents a client from choosing a future durable version number.
struct SfmFlowProgramEdit {
    std::vector<SfmFlowNodeRecord> nodes;
    std::vector<SfmFlowConnectionRecord> connections;
};

inline constexpr size_t kMaxSfmFlowProgramNodes = 1024;
inline constexpr size_t kMaxSfmFlowProgramConnections = 4096;

// Shared durable-graph limits used by sidecar persistence, client command
// admission, and runtime compilation. Endpoint availability is intentionally
// outside this validator because it is owned by the active topology.
struct SfmFlowProgramValidationLimits {
    size_t max_nodes = kMaxSfmFlowProgramNodes;
    size_t max_connections = kMaxSfmFlowProgramConnections;
};

[[nodiscard]] snt::core::Expected<void> validate_sfm_flow_program(
    const SfmFlowProgramRecord& program,
    SfmFlowProgramValidationLimits limits = {});

struct SfmFlowCompileLimits {
    size_t max_nodes = 1024;
    size_t max_connections = 4096;
    uint32_t max_node_dispatches_per_tick = 4096;
};

struct SfmFlowExecutionResult {
    uint32_t dispatched_nodes = 0;
    uint32_t executed_transfers = 0;
    int64_t transferred_units = 0;
    bool dispatch_budget_exhausted = false;
};

class SfmCompiledFlowProgram final {
public:
    SfmCompiledFlowProgram() = default;

    [[nodiscard]] bool is_valid() const noexcept {
        return resource_context_.is_valid() && !nodes_.empty() &&
            max_node_dispatches_per_tick_ != 0;
    }
    [[nodiscard]] ResourceKeyContext resource_context() const noexcept {
        return resource_context_;
    }
    [[nodiscard]] uint64_t source_revision() const noexcept { return source_revision_; }
    [[nodiscard]] size_t node_count() const noexcept { return nodes_.size(); }
    [[nodiscard]] size_t connection_count() const noexcept { return connection_count_; }
    [[nodiscard]] uint32_t max_node_dispatches_per_tick() const noexcept {
        return max_node_dispatches_per_tick_;
    }

private:
    friend class SfmFlowProgramCompiler;
    friend class SfmFlowExecutor;

    struct Node {
        SfmFlowNodeId id = kInvalidSfmFlowNodeId;
        SfmFlowNodeType type = SfmFlowNodeType::kInterval;
        uint32_t interval_ticks = 0;
        SfmBoundResourceTransfer transfer;
        std::vector<uint32_t> outgoing;
    };

    ResourceKeyContext resource_context_;
    uint64_t source_revision_ = 0;
    uint32_t max_node_dispatches_per_tick_ = 0;
    size_t connection_count_ = 0;
    std::vector<Node> nodes_;
    std::unordered_map<SfmFlowNodeId, uint32_t> node_indices_;
};

// Content/reload boundary compiler.  This is intentionally the only API that
// turns a durable flow record into executable values.
class SfmFlowProgramCompiler final {
public:
    [[nodiscard]] static snt::core::Expected<SfmCompiledFlowProgram> compile(
        const SfmFlowProgramRecord& record,
        const SfmEndpointRegistry& endpoint_registry,
        const IResourceKeyResolver& resource_resolver,
        SfmFlowCompileLimits limits = {});
};

// Main-thread fixed-tick owner for a compiled program.  Timer scheduling uses
// a due-time heap so the idle path does not scan all interval nodes.  A graph
// dispatch is naturally O(visited nodes + visited edges); individual node and
// endpoint lookups remain expected O(1).  The bounded dispatch limit prevents
// malformed cyclic graphs from creating an unbounded fixed-tick workload.
class SfmFlowExecutor final {
public:
    [[nodiscard]] static snt::core::Expected<SfmFlowExecutor> create(
        SfmCompiledFlowProgram program,
        const SfmEndpointRegistry& endpoint_registry);

    SfmFlowExecutor(const SfmFlowExecutor&) = delete;
    SfmFlowExecutor& operator=(const SfmFlowExecutor&) = delete;
    SfmFlowExecutor(SfmFlowExecutor&&) noexcept = default;
    SfmFlowExecutor& operator=(SfmFlowExecutor&&) noexcept = default;

    [[nodiscard]] const SfmCompiledFlowProgram& program() const noexcept { return program_; }
    [[nodiscard]] bool is_started() const noexcept { return started_; }
    [[nodiscard]] snt::core::Expected<void> trigger(SfmFlowNodeId node_id);
    [[nodiscard]] snt::core::Expected<SfmFlowExecutionResult> tick(uint64_t tick_index);

private:
    struct DueNode {
        uint64_t tick_index = 0;
        uint32_t node_index = 0;

        [[nodiscard]] bool operator>(const DueNode& other) const noexcept {
            if (tick_index != other.tick_index) return tick_index > other.tick_index;
            return node_index > other.node_index;
        }
    };

    SfmFlowExecutor(SfmCompiledFlowProgram program,
                    const SfmEndpointRegistry& endpoint_registry) noexcept;
    void schedule_intervals(uint64_t first_tick) noexcept;
    void enqueue_outgoing(uint32_t node_index);

    SfmCompiledFlowProgram program_;
    const SfmEndpointRegistry* endpoint_registry_ = nullptr;
    std::priority_queue<DueNode, std::vector<DueNode>, std::greater<DueNode>> due_nodes_;
    std::vector<uint32_t> pending_nodes_;
    bool started_ = false;
    uint64_t last_tick_index_ = 0;
};

}  // namespace snt::game
