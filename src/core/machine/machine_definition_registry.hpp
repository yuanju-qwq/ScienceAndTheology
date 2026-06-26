#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace science_and_theology::gt {

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
    std::string gui_scene_path;    // path to GUI PackedScene
    std::string model_path;        // path to 3D model
    int32_t tier = 1;              // LV=1, MV=2, HV=3, etc.
    uint16_t hatch_mask = 0;       // bitmask of allowed hatch types
    int32_t input_slots = 1;       // number of input slots
    int32_t output_slots = 1;      // number of output slots
    int32_t power_capacity = 0;    // max energy storage (EU)
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
