#include "flow_variable.hpp"

namespace science_and_theology::sfm {

FlowVariableId VariableStore::add_variable(const std::string& name,
                                            FlowPortType type) {
    FlowVariableId id = next_id_++;
    FlowVariable v;
    v.id = id;
    v.name = name;
    v.type = type;
    v.value.type = type;
    vars_[id] = std::move(v);
    return id;
}

bool VariableStore::remove_variable(FlowVariableId id) {
    return vars_.erase(id) > 0;
}

} // namespace science_and_theology::sfm
