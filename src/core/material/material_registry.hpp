#pragma once

#include <cstdint>

namespace science_and_theology::gt {

struct Material;
struct ElementComposition;

// ============================================================
// MaterialRegistry — unified material lookup
// ============================================================
//
// Materials are registered dynamically (from GDScript via GDMaterialRegistry).
// After all registrations, call finalize() to trigger ItemRegistry init.
// All material access goes through this class.

class MaterialRegistry {
public:
    // Register a single material. id must be < kMaxMaterials.
    // The strings are copied internally; the caller can free its copies.
    // Call before finalize().
    static void register_material(
        uint16_t id, const char* name, const char* title_key,
        uint16_t gen_flags, MaterialState state,
        uint32_t color, int64_t melting_point, int64_t boiling_point, float mass,
        const char* chemical_formula,
        uint8_t elem_count, const ElementComposition* composition);

    // Allocate the next available material ID (auto-increment).
    // Use this when registering from GDScript without a hardcoded ID.
    static uint16_t allocate_id();

    // Finalize the registry and trigger ItemRegistry initialization.
    // After this, register_material() must not be called.
    static void finalize();

    // Look up by internal ID (0-based index).
    static const Material* get_by_id(uint16_t id);

    // Look up by name string, e.g. "copper".
    static const Material* get_by_name(const char* name);

    // Number of registered materials.
    static size_t count();

    // Maximum number of material slots (kMaxMaterials).
    static size_t capacity();
};

} // namespace science_and_theology::gt
