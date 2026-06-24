#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "../common/resource_types.hpp"

namespace science_and_theology::sfm {

// ============================================================
// Steve's Factory Manager (SFM) — Flow Programming System
// ============================================================
//
// This module implements a visual flow-programming logistics
// manager inspired by Minecraft's "Steve's Factory Manager" mod.
//
// Players connect a Manager block to nearby containers via cables,
// then write a "flow program" in a node-based editor. The program
// controls how items / fluids / energy / redstone move between
// containers on each trigger (timer, redstone, item presence).
//
// Architecture:
//   - flow_types.hpp     : core value types, port/node type enums
//   - flow_node.hpp      : node + port definitions
//   - flow_program.hpp   : directed graph of nodes + connections
//   - flow_variable.hpp  : program-scoped variables
//   - i_container_access.hpp : unified container abstraction
//   - flow_executor.hpp  : per-tick program execution engine
//   - sfm_manager.hpp    : per-Manager-block runtime (program + cables)
//   - cable_graph.hpp    : cable topology + container discovery

// ============================================================
// Identifier types
// ============================================================

using FlowNodeId = uint32_t;
using FlowPortId = uint8_t;
using FlowConnectionId = uint32_t;
using FlowVariableId = uint32_t;
using ContainerId = uint32_t;

inline constexpr FlowNodeId kInvalidFlowNodeId = 0;
inline constexpr FlowConnectionId kInvalidFlowConnectionId = 0;
inline constexpr FlowVariableId kInvalidFlowVariableId = 0;
inline constexpr ContainerId kInvalidContainerId = 0;

// ============================================================
// FlowPortType — what kind of value a port carries
// ============================================================
//
// FLOW ports carry the execution trigger signal (green links in the
// original mod). Data ports carry typed values. A connection is only
// valid when source and destination port types match.

enum class FlowPortType : uint8_t {
    NONE          = 0,
    FLOW          = 1,  // Execution flow (trigger signal)
    ITEM_STREAM   = 2,  // List of (item_id, count)
    FLUID_STREAM  = 3,  // List of (fluid_id, amount_mB)
    ENERGY        = 4,  // Energy amount (EU)
    REDSTONE      = 5,  // Redstone signal (0-15)
    NUMBER        = 6,  // Integer
    STRING        = 7,  // Text
    BOOLEAN       = 8,  // Bool
};

// ============================================================
// FlowNodeType — all node kinds
// ============================================================

enum class FlowNodeType : uint16_t {
    // --- Triggers (entry points; produce FLOW output) ---
    TRIGGER_TIMER     = 0,
    TRIGGER_REDSTONE  = 1,
    TRIGGER_ITEM      = 2,

    // --- Item I/O ---
    ITEM_INPUT        = 10,
    ITEM_OUTPUT       = 11,

    // --- Fluid I/O ---
    FLUID_INPUT       = 20,
    FLUID_OUTPUT      = 21,

    // --- Energy I/O ---
    ENERGY_INPUT      = 30,
    ENERGY_OUTPUT     = 31,

    // --- Redstone I/O ---
    REDSTONE_INPUT    = 40,
    REDSTONE_OUTPUT   = 41,

    // --- Filters (pass-through, modify a stream) ---
    ITEM_FILTER       = 50,
    FLUID_FILTER      = 51,

    // --- Control flow ---
    CONDITION         = 60,
    LOOP              = 61,
    GROUP_INPUT       = 62,
    GROUP_OUTPUT      = 63,

    // --- Data ---
    VARIABLE_GET      = 70,
    VARIABLE_SET      = 71,
    MATH              = 72,
    TEXT_LABEL        = 73,

    COUNT             = 74,
};

inline constexpr const char* kFlowNodeTypeNames[] = {
    "Timer Trigger", "Redstone Trigger", "Item Trigger",        // 0-2
    "", "", "", "", "", "", "",                                  // 3-9
    "Item Input", "Item Output",                                 // 10-11
    "", "", "", "", "", "", "", "",                              // 12-19
    "Fluid Input", "Fluid Output",                               // 20-21
    "", "", "", "", "", "", "", "",                              // 22-29
    "Energy Input", "Energy Output",                             // 30-31
    "", "", "", "", "", "", "", "",                              // 32-39
    "Redstone Input", "Redstone Output",                         // 40-41
    "", "", "", "", "", "", "", "",                              // 42-49
    "Item Filter", "Fluid Filter",                               // 50-51
    "", "", "", "", "", "", "", "",                              // 52-59
    "Condition", "Loop", "Group Input", "Group Output",          // 60-63
    "", "", "", "", "", "",                                      // 64-69
    "Variable Get", "Variable Set", "Math", "Text Label",        // 70-73
};

inline const char* get_node_type_name(FlowNodeType t) {
    auto i = static_cast<size_t>(t);
    if (i < static_cast<size_t>(FlowNodeType::COUNT)) {
        return kFlowNodeTypeNames[i];
    }
    return "Unknown";
}

// ============================================================
// Stream entries
// ============================================================

struct FlowItemEntry {
    gt::ItemId item_id = gt::kInvalidItemId;
    int64_t count = 0;
};

struct FlowFluidEntry {
    gt::FluidId fluid_id = gt::kInvalidFluidId;
    int64_t amount_mb = 0;  // millibuckets
};

// ============================================================
// FlowValue — runtime value carried on a data port
// ============================================================
//
// A single FlowValue holds one active variant determined by `type`.
// Only the field matching `type` is meaningful; others are default.

struct FlowValue {
    FlowPortType type = FlowPortType::NONE;

    // ITEM_STREAM
    std::vector<FlowItemEntry> items;
    // FLUID_STREAM
    std::vector<FlowFluidEntry> fluids;
    // ENERGY
    int64_t energy = 0;
    // REDSTONE
    int32_t redstone = 0;
    // NUMBER
    int64_t number = 0;
    // STRING
    std::string text;
    // BOOLEAN
    bool boolean = false;

    // Helpers to build values.
    static FlowValue make_flow() {
        FlowValue v; v.type = FlowPortType::FLOW; return v;
    }
    static FlowValue make_number(int64_t n) {
        FlowValue v; v.type = FlowPortType::NUMBER; v.number = n; return v;
    }
    static FlowValue make_boolean(bool b) {
        FlowValue v; v.type = FlowPortType::BOOLEAN; v.boolean = b; return v;
    }
    static FlowValue make_string(std::string s) {
        FlowValue v; v.type = FlowPortType::STRING; v.text = std::move(s); return v;
    }
    static FlowValue make_redstone(int32_t r) {
        FlowValue v; v.type = FlowPortType::REDSTONE; v.redstone = r; return v;
    }
    static FlowValue make_energy(int64_t e) {
        FlowValue v; v.type = FlowPortType::ENERGY; v.energy = e; return v;
    }

    bool is_flow() const { return type == FlowPortType::FLOW; }

    void clear() { *this = FlowValue{}; }
};

// ============================================================
// Filters
// ============================================================

enum class FilterMode : uint8_t {
    WHITELIST = 0,  // Only allow listed entries
    BLACKLIST = 1,  // Allow all except listed entries
};

struct ItemFilterDef {
    FilterMode mode = FilterMode::WHITELIST;
    std::vector<gt::ItemId> item_ids;

    bool matches(gt::ItemId id) const {
        bool in_list = false;
        for (auto i : item_ids) {
            if (i == id) { in_list = true; break; }
        }
        return (mode == FilterMode::WHITELIST) ? in_list : !in_list;
    }
};

struct FluidFilterDef {
    FilterMode mode = FilterMode::WHITELIST;
    std::vector<gt::FluidId> fluid_ids;

    bool matches(gt::FluidId id) const {
        bool in_list = false;
        for (auto f : fluid_ids) {
            if (f == id) { in_list = true; break; }
        }
        return (mode == FilterMode::WHITELIST) ? in_list : !in_list;
    }
};

// ============================================================
// Math operation kinds (used by MATH node)
// ============================================================

enum class MathOp : uint8_t {
    ADD = 0,
    SUBTRACT,
    MULTIPLY,
    DIVIDE,
    MODULO,
    MIN,
    MAX,
    COUNT
};

inline constexpr const char* kMathOpNames[] = {
    "Add", "Subtract", "Multiply", "Divide", "Modulo", "Min", "Max",
};

// ============================================================
// Condition comparison kinds (used by CONDITION node)
// ============================================================

enum class CompareOp : uint8_t {
    EQUAL = 0,
    NOT_EQUAL,
    LESS,
    LESS_EQUAL,
    GREATER,
    GREATER_EQUAL,
    COUNT
};

inline constexpr const char* kCompareOpNames[] = {
    "==", "!=", "<", "<=", ">", ">=",
};

} // namespace science_and_theology::sfm
