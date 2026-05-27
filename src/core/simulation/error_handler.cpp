#include "error_handler.hpp"
#include "event_bus.hpp"

namespace science_and_theology {

void ErrorHandler::report_error(const MachineError& error, EventBus* bus) {
    active_errors_[error.machine_id] = error;
    if (bus) {
        bus->emit(GameEvent::machine_error(
            error.machine_id.id,
            error.error_code.c_str(),
            error.message.c_str()));
    }
}

void ErrorHandler::clear_error(MachineId machine_id) {
    active_errors_.erase(machine_id);
}

bool ErrorHandler::has_error(MachineId machine_id) const {
    return active_errors_.find(machine_id) != active_errors_.end();
}

const MachineError* ErrorHandler::get_error(MachineId machine_id) const {
    auto it = active_errors_.find(machine_id);
    if (it != active_errors_.end()) {
        return &it->second;
    }
    return nullptr;
}

std::vector<MachineError> ErrorHandler::get_all_errors() const {
    std::vector<MachineError> result;
    result.reserve(active_errors_.size());
    for (const auto& pair : active_errors_) {
        result.push_back(pair.second);
    }
    return result;
}

std::vector<MachineError> ErrorHandler::get_errors_by_severity(
    ErrorSeverity min) const {
    std::vector<MachineError> result;
    for (const auto& pair : active_errors_) {
        if (static_cast<uint8_t>(pair.second.severity) >=
            static_cast<uint8_t>(min)) {
            result.push_back(pair.second);
        }
    }
    return result;
}

void ErrorHandler::clear_all() {
    active_errors_.clear();
}

} // namespace science_and_theology
