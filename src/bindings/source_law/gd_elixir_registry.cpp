#include "gd_elixir_registry.hpp"

#include "core/common/string_pool.hpp"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/variant.hpp>

#include "core/source_law/elixir_registry.hpp"

using namespace godot;

namespace science_and_theology {

using source_law::ElixirRecipe;
using source_law::ElixirType;
using source_law::SublimationPath;
using source_law::OrganSlot;
using source_law::ElixirRegistry;
using source_law::ElixirId;
using source_law::kInvalidElixirId;
using magic::RuneElement;

void GDElixirRegistry::_bind_methods() {
    ClassDB::bind_static_method("GDElixirRegistry",
        D_METHOD("register_recipe", "def"),
        &GDElixirRegistry::register_recipe);
}

bool GDElixirRegistry::register_recipe(const Dictionary& def) {
    String id = def.get("id", "");
    if (id.is_empty()) return false;

    String title_key_str = def.get("title_key", "");
    ElixirRecipe recipe;
    recipe.id = gt::intern_string(id.utf8().get_data());
    recipe.title_key = gt::intern_string(title_key_str.utf8().get_data());

    recipe.type = static_cast<ElixirType>(
        static_cast<int>(def.get("type", 0)));
    recipe.target_path = static_cast<SublimationPath>(
        static_cast<int>(def.get("target_path", 0)));
    recipe.target_slot = static_cast<OrganSlot>(
        static_cast<int>(def.get("target_slot",
            static_cast<int>(OrganSlot::COUNT))));
    recipe.primary_element = static_cast<RuneElement>(
        static_cast<int>(def.get("primary_element",
            static_cast<int>(RuneElement::EARTH))));
    recipe.source_cost = static_cast<int>(def.get("source_cost", 0));
    recipe.stability_modifier = static_cast<float>(
        static_cast<double>(def.get("stability_modifier", 0.0)));
    recipe.mutation_modifier = static_cast<float>(
        static_cast<double>(def.get("mutation_modifier", 0.0)));
    recipe.tuning_degree = static_cast<int>(def.get("tuning_degree", 0));

    // required_rune_elements: accept PackedByteArray or Array of int.
    Variant req_var = def.get("required_rune_elements", Variant());
    if (req_var.get_type() == Variant::PACKED_BYTE_ARRAY) {
        PackedByteArray bytes = req_var;
        for (int i = 0; i < bytes.size(); ++i) {
            recipe.required_rune_elements.push_back(
                static_cast<RuneElement>(bytes[i]));
        }
    } else if (req_var.get_type() == Variant::ARRAY) {
        Array arr = req_var;
        for (int i = 0; i < arr.size(); ++i) {
            recipe.required_rune_elements.push_back(
                static_cast<RuneElement>(static_cast<int>(arr[i])));
        }
    }

    // 支持显式确定性 ID（P1: 热重载后 ID 不漂移）。
    // 注意：string "id" 字段是 recipe 的字符串主键，与 numeric runtime ID 是
    // 两个不同概念，故使用独立字段 "explicit_id"。
    ElixirId explicit_id = kInvalidElixirId;
    Variant eid_var = def.get("explicit_id", Variant());
    if (eid_var.get_type() == Variant::INT) {
        explicit_id = static_cast<ElixirId>(static_cast<int>(eid_var));
    }

    ElixirId registered = ElixirRegistry::register_recipe(recipe, explicit_id);
    return registered != kInvalidElixirId;
}

} // namespace science_and_theology
