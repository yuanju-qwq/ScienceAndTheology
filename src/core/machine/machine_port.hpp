#pragma once

#include <cstdint>

namespace science_and_theology::gt {

// ============================================================
// Port types — the physical connection points on a machine
// ============================================================

enum class PortType : uint8_t {
    ENERGY    = 0,  // connects to power poles
    UNIVERSAL = 1,  // connects to item pipes, liquid pipes, or gas pipes;
                    // the connected pipe type determines what flows
};

enum class PortDirection : uint8_t {
    INPUT  = 0,
    OUTPUT = 1,
};

// A physical I/O port on the machine's footprint.
struct MachinePort {
    int rel_x = 0;                   // relative to machine bottom-left
    int rel_y = 0;
    PortType type = PortType::UNIVERSAL;
    PortDirection direction = PortDirection::INPUT;
    bool direction_locked = false;   // true = player cannot flip I/O
};

} // namespace science_and_theology::gt