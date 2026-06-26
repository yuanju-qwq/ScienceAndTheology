#include "material.hpp"
#include "material_registry.hpp"
#include "material_item.hpp"
#include "core/common/string_pool.hpp"
#include "core/fuel/fuel_registry.hpp"

#include <cassert>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

namespace science_and_theology::gt {

// --- Form generation logic ---

bool Material::generates_form(MaterialForm form) const {
    auto f = static_cast<uint16_t>(form);

    // Dust variants — only for solids.
    if (f == static_cast<uint16_t>(MaterialForm::DUST) ||
        f == static_cast<uint16_t>(MaterialForm::TINY_DUST) ||
        f == static_cast<uint16_t>(MaterialForm::SMALL_DUST)) {
        return (generation_flags & MaterialGenFlag::DUST) != 0;
    }

    // Gem variants.
    if (f == static_cast<uint16_t>(MaterialForm::GEM) ||
        f == static_cast<uint16_t>(MaterialForm::FLAWED_GEM) ||
        f == static_cast<uint16_t>(MaterialForm::FLAWLESS_GEM) ||
        f == static_cast<uint16_t>(MaterialForm::EXQUISITE_GEM)) {
        return (generation_flags & MaterialGenFlag::GEM) != 0;
    }

    // Crushed / ore variants.
    if (f == static_cast<uint16_t>(MaterialForm::CRUSHED) ||
        f == static_cast<uint16_t>(MaterialForm::CRUSHED_PURIFIED) ||
        f == static_cast<uint16_t>(MaterialForm::CRUSHED_CENTRIFUGED) ||
        f == static_cast<uint16_t>(MaterialForm::IMPURE_DUST) ||
        f == static_cast<uint16_t>(MaterialForm::PURIFIED_DUST)) {
        return (generation_flags & MaterialGenFlag::ORE) != 0;
    }

    // Metal forms: ingot, plate, rod, bolt, screw, ring, rotor, gear.
    if (f == static_cast<uint16_t>(MaterialForm::INGOT) ||
        f == static_cast<uint16_t>(MaterialForm::INGOT_HOT) ||
        f == static_cast<uint16_t>(MaterialForm::NUGGET) ||
        f == static_cast<uint16_t>(MaterialForm::PLATE) ||
        f == static_cast<uint16_t>(MaterialForm::DOUBLE_PLATE) ||
        f == static_cast<uint16_t>(MaterialForm::DENSE_PLATE) ||
        f == static_cast<uint16_t>(MaterialForm::ROD) ||
        f == static_cast<uint16_t>(MaterialForm::LONG_ROD) ||
        f == static_cast<uint16_t>(MaterialForm::BOLT) ||
        f == static_cast<uint16_t>(MaterialForm::SCREW) ||
        f == static_cast<uint16_t>(MaterialForm::RING) ||
        f == static_cast<uint16_t>(MaterialForm::ROTOR) ||
        f == static_cast<uint16_t>(MaterialForm::GEAR) ||
        f == static_cast<uint16_t>(MaterialForm::SMALL_GEAR)) {
        return (generation_flags & MaterialGenFlag::METAL) != 0;
    }

    // Wires.
    if (f == static_cast<uint16_t>(MaterialForm::WIRE_FINE) ||
        f == static_cast<uint16_t>(MaterialForm::WIRE)) {
        return (generation_flags & MaterialGenFlag::WIRE) != 0;
    }

    // Blocks.
    if (f == static_cast<uint16_t>(MaterialForm::BLOCK)) {
        return (generation_flags & MaterialGenFlag::BLOCK) != 0;
    }

    // Cells — fluids (liquids AND gases) can be stored in cells.
    if (f == static_cast<uint16_t>(MaterialForm::CELL)) {
        return (generation_flags & MaterialGenFlag::CELL) != 0;
    }

    // Plasma cells — only plasma-grade materials.
    if (f == static_cast<uint16_t>(MaterialForm::PLASMA_CELL)) {
        return (generation_flags & MaterialGenFlag::PLASMA) != 0;
    }

    return false;
}

// --- Dynamic material registry ---

namespace {

// Per-entry storage for composition data. Strings are interned into the
// core string pool (common/string_pool.hpp) so Material::name etc. point
// to stable pool memory. composition is per-entry (cannot be pooled).
struct MaterialEntry {
    std::vector<ElementComposition> composition;
    Material material = {};
};

// Indexed by material ID. Pre-allocated to kMaxMaterials capacity on first
// use so the vector never reallocates — keeping all c_str()/data() pointers
// stable after registration.
std::vector<MaterialEntry> g_registry;
std::unordered_map<std::string, uint16_t> g_material_name_to_id;
uint16_t g_next_material_id = 0;
bool g_finalized = false;

// Ensures the vector has capacity for kMaxMaterials before any size change.
static void reserve_registry() {
    if (g_registry.capacity() < kMaxMaterials) {
        g_registry.reserve(kMaxMaterials);
    }
}

} // anonymous namespace

void MaterialRegistry::register_material(
    uint16_t id, const char* name, const char* title_key,
    uint16_t gen_flags, MaterialState state,
    uint32_t color, int64_t melting_point, int64_t boiling_point, float mass,
    const char* chemical_formula,
    uint8_t elem_count, const ElementComposition* composition)
{
    assert(!g_finalized && "Cannot register materials after finalize()");
    assert(id < kMaxMaterials && "Material ID exceeds kMaxMaterials");

    // 幂等检查：若 name 已存在，直接返回，保持旧映射不变。
    // - 传入 id 与已有 id 相同：幂等返回，避免覆盖开销。
    // - 传入 id 与已有 id 不同：保持旧映射，避免破坏既有索引。
    if (name != nullptr) {
        auto it = g_material_name_to_id.find(name);
        if (it != g_material_name_to_id.end()) {
            return;
        }
    }

    // Ensure the vector is large enough for this ID.
    // reserve() guarantees no reallocation as we grow within kMaxMaterials.
    reserve_registry();
    if (id >= g_registry.size()) {
        g_registry.resize(id + 1);
    }

    MaterialEntry& entry = g_registry[id];

    // Copy composition array.
    entry.composition.clear();
    if (composition != nullptr && elem_count > 0) {
        entry.composition.assign(composition, composition + elem_count);
    }

    // Fill the Material struct with pre-interned string pointers.
    entry.material.id = id;
    entry.material.name = name;
    entry.material.title_key = title_key;
    entry.material.generation_flags = gen_flags;
    entry.material.state = state;
    entry.material.color = color;
    entry.material.melting_point = melting_point;
    entry.material.boiling_point = boiling_point;
    entry.material.mass = mass;
    entry.material.chemical_formula = chemical_formula;
    entry.material.element_count = static_cast<uint8_t>(entry.composition.size());
    entry.material.composition = entry.composition.empty() ? nullptr : entry.composition.data();

    // Update name→id map (replaces old entry on re-registration).
    g_material_name_to_id[entry.material.name] = id;

    // Advance next ID if this is the highest so far.
    if (id >= g_next_material_id) {
        g_next_material_id = id + 1;
    }
}

void MaterialRegistry::lock() {
    g_finalized = true;
}

void MaterialRegistry::reset() {
    // 清空注册表与名称映射
    g_registry.clear();
    g_material_name_to_id.clear();
    // 复位 ID 分配器与 finalized 标志（同时起到 unlock 作用）
    g_next_material_id = 0;
    g_finalized = false;
    // 重新预留容量，避免后续注册时频繁扩容
    reserve_registry();
}

// --- Lookup functions ---

const Material* get_material(const char* name) {
    if (name == nullptr) return nullptr;
    auto it = g_material_name_to_id.find(name);
    if (it == g_material_name_to_id.end()) return nullptr;
    return get_material_by_id(it->second);
}

const Material* get_material_by_id(uint16_t id) {
    // Direct array access — O(1), no hashing.
    if (id >= g_registry.size()) return nullptr;
    // slot is valid if name is non-null (set during registration).
    return g_registry[id].material.name ? &g_registry[id].material : nullptr;
}

size_t get_material_count() {
    // With sequential IDs g_next_material_id == registered count.
    return g_next_material_id;
}

// --- MaterialRegistry wrapper ---

const Material* MaterialRegistry::get_by_id(uint16_t id) {
    return get_material_by_id(id);
}

const Material* MaterialRegistry::get_by_name(const char* name) {
    return get_material(name);
}

size_t MaterialRegistry::count() {
    return get_material_count();
}

size_t MaterialRegistry::capacity() {
    return kMaxMaterials;
}

uint16_t get_max_material_id() {
    return g_next_material_id;
}

} // namespace science_and_theology::gt