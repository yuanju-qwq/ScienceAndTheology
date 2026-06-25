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

    // Lock the registry against further modifications.
    // After this, register_material() must not be called.
    static void lock();

    // 重置整个注册表到初始状态：清空所有已注册材料、复位 ID 分配器、
    // 复位 finalized 标志。用于测试或热重载场景。
    static void reset();

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
