#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace science_and_theology::gt {

// ============================================================
// PanelLayout — data-driven machine GUI layout
// ============================================================
//
// A PanelLayout describes which widgets a machine panel should build
// and where to place them. It is retained as legacy source metadata;
// the current native MUI builds its machine panel from replicated state
// rather than a Godot scene.
//
// Coordinates are legacy panel-local pixel offsets retained for source
// comparison during later machine migrations.

// A single widget descriptor inside a machine panel layout.
struct PanelElement {
    std::string element_id;       // unique id within panel, e.g. "input_slot"
    std::string element_type;     // "slot" | "progress_bar" | "fuel_bar" | "energy_bar" | "fluid_tank"
    std::string role;             // slot role: "input" | "fuel" | "output" (empty for bars)
    int32_t slot_index = 0;       // slot index within its role (for multi-slot machines)
    float offset_left = 0.0f;     // rect left offset relative to panel
    float offset_top = 0.0f;      // rect top offset relative to panel
    float offset_right = 0.0f;    // rect right offset relative to panel
    float offset_bottom = 0.0f;   // rect bottom offset relative to panel
};

struct PanelLayout {
    float panel_width = 320.0f;             // total panel width in px
    float panel_height = 220.0f;            // total panel height in px
    std::vector<PanelElement> elements;    // ordered list of widgets to build
};

// ============================================================
// MachineDefinition — metadata for a machine type
// ============================================================
//
// A MachineDefinition describes a machine type's static properties:
// display name, tier, GUI scene path, model path, and hatch mask.
// Mod-registered machines get a MachineDefinition entry here; builtin
// machines (furnace, coke_oven, etc.) may also be registered for
// consistency, but their logic remains in C++.
//
// The process_callback is handled at the GD binding layer (GDCallable)
// since C++ core cannot depend on Godot's Callable type. The C++ side
// stores only data; the GD side stores the callback alongside the
// type_key.

struct MachineDefinition {
    std::string type_key;          // e.g. "my_mod:custom_furnace"
    std::string display_name;      // human-readable name
    std::string gui_scene_path;    // legacy GUI scene source path; unused by current MUI
    std::string model_path;        // legacy 3D model source path
    int32_t tier = 1;              // LV=1, MV=2, HV=3, etc.
    uint16_t hatch_mask = 0;       // bitmask of allowed hatch types
    int32_t input_slots = 1;       // number of input slots
    int32_t output_slots = 1;      // number of output slots
    int32_t power_capacity = 0;    // max energy storage (EU)
    PanelLayout layout;            // legacy layout metadata; unused by current MUI
};

class MachineDefinitionRegistry {
public:
    static void initialize();
    static void reset();

    // Register a machine definition. Replaces existing if type_key matches.
    // Returns true on success.
    static bool register_definition(const MachineDefinition& def);

    // Look up a machine definition by type_key. Returns nullptr if not found.
    static const MachineDefinition* get_definition(const std::string& type_key);

    // Returns true if a machine type is registered.
    static bool has_definition(const std::string& type_key);

    // Returns the total number of registered machine definitions.
    static size_t get_definition_count();

private:
    static std::unordered_map<std::string, MachineDefinition>& registry();
};

} // namespace science_and_theology::gt
