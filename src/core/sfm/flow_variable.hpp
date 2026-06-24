#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "flow_types.hpp"

namespace science_and_theology::sfm {

// ============================================================
// FlowVariable — a named, typed value stored per-program
// ============================================================
//
// Variables persist across trigger cycles within the same Manager.
// VARIABLE_GET reads the current value; VARIABLE_SET writes a new
// value (received from its input port) and forwards it on output.

struct FlowVariable {
    FlowVariableId id = kInvalidFlowVariableId;
    std::string name;
    FlowPortType type = FlowPortType::NUMBER;
    FlowValue value;  // Current stored value
};

// ============================================================
// VariableStore — owns all variables for one FlowProgram
// ============================================================

class VariableStore {
public:
    VariableStore() = default;

    FlowVariableId add_variable(const std::string& name, FlowPortType type);
    bool remove_variable(FlowVariableId id);

    FlowVariable* get_variable(FlowVariableId id) {
        auto it = vars_.find(id);
        return it == vars_.end() ? nullptr : &it->second;
    }
    const FlowVariable* get_variable(FlowVariableId id) const {
        auto it = vars_.find(id);
        return it == vars_.end() ? nullptr : &it->second;
    }

    FlowVariable* find_by_name(const std::string& name) {
        for (auto& [id, v] : vars_) {
            if (v.name == name) return &v;
        }
        return nullptr;
    }

    size_t size() const { return vars_.size(); }

    const std::unordered_map<FlowVariableId, FlowVariable>& variables() const {
        return vars_;
    }

    void clear() { vars_.clear(); next_id_ = 1; }

private:
    std::unordered_map<FlowVariableId, FlowVariable> vars_;
    FlowVariableId next_id_ = 1;
};

} // namespace science_and_theology::sfm
