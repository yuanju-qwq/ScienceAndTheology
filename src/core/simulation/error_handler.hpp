#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "../world/entity_data.hpp"
#include "event_types.hpp"

namespace science_and_theology {

class EventBus;

enum class ErrorSeverity : uint8_t {
    WARNING      = 0,
    RECOVERABLE  = 1,
    PLAYER_ACTION = 2,
    FATAL        = 3,
};

struct MachineError {
    ErrorSeverity severity = ErrorSeverity::WARNING;
    MachineId machine_id;
    std::string error_code;
    std::string message;
    int64_t timestamp = 0;
};

// Tracks active errors across all machines.
// Errors are reported to the EventBus for UI/notification.
class ErrorHandler {
public:
    ErrorHandler() = default;

    // Report an error. Emits MACHINE_ERROR event via the bus.
    void report_error(const MachineError& error, EventBus* bus);

    // Clear errors for a specific machine (e.g. after player fix).
    void clear_error(MachineId machine_id);

    // Returns true if the machine has an active error.
    bool has_error(MachineId machine_id) const;

    // Returns the active error for a machine, or nullptr.
    const MachineError* get_error(MachineId machine_id) const;

    // Returns all active errors.
    std::vector<MachineError> get_all_errors() const;

    // Returns errors of at least the specified severity.
    std::vector<MachineError> get_errors_by_severity(ErrorSeverity min) const;

    // Removes all errors.
    void clear_all();

private:
    std::unordered_map<MachineId, MachineError> active_errors_;
};

} // namespace science_and_theology
