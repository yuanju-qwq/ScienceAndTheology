#include "gd_multiblock_builder.h"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include "core/multiblock/multiblock_controller.hpp"

namespace science_and_theology {

GDMultiblockBuilder::GDMultiblockBuilder() = default;
GDMultiblockBuilder::~GDMultiblockBuilder() = default;

char GDMultiblockBuilder::to_char(const godot::String& s) {
    if (s.is_empty()) return ' ';
    return static_cast<char>(s[0]);
}

void GDMultiblockBuilder::aisle(const godot::PackedStringArray& rows) {
    std::vector<std::string> cpp_rows;
    cpp_rows.reserve(rows.size());
    for (int i = 0; i < rows.size(); ++i) {
        cpp_rows.emplace_back(rows[i].utf8().get_data());
    }
    builder_.aisle(cpp_rows);
}

void GDMultiblockBuilder::where_material(
    const godot::String& symbol, int material_id) {
    builder_.material(to_char(symbol),
                      static_cast<TerrainMaterialId>(material_id));
}

void GDMultiblockBuilder::where_air(const godot::String& symbol) {
    builder_.air(to_char(symbol));
}

void GDMultiblockBuilder::where_any(const godot::String& symbol) {
    builder_.any(to_char(symbol));
}

void GDMultiblockBuilder::where_self(const godot::String& symbol) {
    builder_.self(to_char(symbol));
}

void GDMultiblockBuilder::where_hatch(
    const godot::String& symbol, int type_mask) {
    builder_.hatch(to_char(symbol), static_cast<uint16_t>(type_mask));
}

bool GDMultiblockBuilder::build(const godot::String& machine_type) {
    std::string key = machine_type.utf8().get_data();
    auto def = builder_.build_structure_definition(key);
    if (!def) return false;

    // 注册到 MultiblockControllerBase，之后 check_formation 可用。
    multiblock::MultiblockControllerBase::register_definition(
        key, [def]() { return def; });
    return true;
}

// --- 便捷常量 ---

int GDMultiblockBuilder::HATCH_ITEM_INPUT() {
    return multiblock::HATCH_ITEM_INPUT;
}
int GDMultiblockBuilder::HATCH_ITEM_OUTPUT() {
    return multiblock::HATCH_ITEM_OUTPUT;
}
int GDMultiblockBuilder::HATCH_FLUID_INPUT() {
    return multiblock::HATCH_FLUID_INPUT;
}
int GDMultiblockBuilder::HATCH_FLUID_OUTPUT() {
    return multiblock::HATCH_FLUID_OUTPUT;
}
int GDMultiblockBuilder::HATCH_ENERGY_INPUT() {
    return multiblock::HATCH_ENERGY_INPUT;
}
int GDMultiblockBuilder::HATCH_ENERGY_OUTPUT() {
    return multiblock::HATCH_ENERGY_OUTPUT;
}
int GDMultiblockBuilder::HATCH_ANY() {
    return multiblock::HATCH_ANY;
}

void GDMultiblockBuilder::_bind_methods() {
    using namespace godot;

    ClassDB::bind_method(D_METHOD("aisle", "rows"),
                         &GDMultiblockBuilder::aisle);
    ClassDB::bind_method(D_METHOD("where_material", "symbol", "material_id"),
                         &GDMultiblockBuilder::where_material);
    ClassDB::bind_method(D_METHOD("where_air", "symbol"),
                         &GDMultiblockBuilder::where_air);
    ClassDB::bind_method(D_METHOD("where_any", "symbol"),
                         &GDMultiblockBuilder::where_any);
    ClassDB::bind_method(D_METHOD("where_self", "symbol"),
                         &GDMultiblockBuilder::where_self);
    ClassDB::bind_method(D_METHOD("where_hatch", "symbol", "type_mask"),
                         &GDMultiblockBuilder::where_hatch, DEFVAL(0xFFFF));
    ClassDB::bind_method(D_METHOD("build", "machine_type"),
                         &GDMultiblockBuilder::build);

    // 便捷常量
    ClassDB::bind_static_method("GDMultiblockBuilder",
        D_METHOD("HATCH_ITEM_INPUT"), &GDMultiblockBuilder::HATCH_ITEM_INPUT);
    ClassDB::bind_static_method("GDMultiblockBuilder",
        D_METHOD("HATCH_ITEM_OUTPUT"), &GDMultiblockBuilder::HATCH_ITEM_OUTPUT);
    ClassDB::bind_static_method("GDMultiblockBuilder",
        D_METHOD("HATCH_FLUID_INPUT"), &GDMultiblockBuilder::HATCH_FLUID_INPUT);
    ClassDB::bind_static_method("GDMultiblockBuilder",
        D_METHOD("HATCH_FLUID_OUTPUT"), &GDMultiblockBuilder::HATCH_FLUID_OUTPUT);
    ClassDB::bind_static_method("GDMultiblockBuilder",
        D_METHOD("HATCH_ENERGY_INPUT"), &GDMultiblockBuilder::HATCH_ENERGY_INPUT);
    ClassDB::bind_static_method("GDMultiblockBuilder",
        D_METHOD("HATCH_ENERGY_OUTPUT"), &GDMultiblockBuilder::HATCH_ENERGY_OUTPUT);
    ClassDB::bind_static_method("GDMultiblockBuilder",
        D_METHOD("HATCH_ANY"), &GDMultiblockBuilder::HATCH_ANY);
}

} // namespace science_and_theology
