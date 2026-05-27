#pragma once

#include <cstdint>

namespace science_and_theology::gt {

struct Material;

// ============================================================
// MaterialRegistry — unified material lookup
// ============================================================
//
// Mirrors FluidRegistry and ItemRegistry in pattern.
// All material access goes through this class.

class MaterialRegistry {
public:
    // Initialize all built-in materials (called once at startup).
    static void initialize();

    // Look up by internal ID (0-based index).
    static const Material* get_by_id(uint16_t id);

    // Look up by name string, e.g. "copper".
    static const Material* get_by_name(const char* name);

    // Total number of registered materials (excluding invalid).
    static size_t count();
};

} // namespace science_and_theology::gt