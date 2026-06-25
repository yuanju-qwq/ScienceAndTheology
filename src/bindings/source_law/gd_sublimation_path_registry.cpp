#include "gd_sublimation_path_registry.hpp"

#include <vector>

#include <godot_cpp/core/binder_common.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/char_string.hpp>

#include "core/magic/rune_def.hpp"
#include "core/source_law/sublimation_path_def.hpp"
#include "core/source_law/sublimation_path_registry.hpp"
#include "core/source_law/source_law_types.hpp"

using namespace godot;

namespace science_and_theology {

void GDSublimationPathRegistry::_bind_methods() {
    ClassDB::bind_static_method("GDSublimationPathRegistry",
        D_METHOD("register_path", "def"),
        &GDSublimationPathRegistry::register_path);
    ClassDB::bind_static_method("GDSublimationPathRegistry",
        D_METHOD("register_skill", "def"),
        &GDSublimationPathRegistry::register_skill);
}

bool GDSublimationPathRegistry::register_path(const Dictionary& def) {
    int path_id = static_cast<int>(def.get("path_id", 0));
    String id_str = def.get("id", "");
    if (id_str.is_empty()) return false;

    String title_str = def.get("title_key", "");
    int primary_element = static_cast<int>(def.get("primary_element", 2));

    // Keep UTF-8 buffers alive across the registry call; the registry copies
    // the strings into persistent storage (store_string) synchronously.
    CharString id_cs = id_str.utf8();
    CharString title_cs = title_str.utf8();

    source_law::SublimationPathDef path_def;
    path_def.path_id = static_cast<source_law::SublimationPath>(path_id);
    path_def.id = id_cs.get_data();
    path_def.title_key = title_cs.get_data();
    path_def.primary_element = static_cast<magic::RuneElement>(primary_element);

    Array stages = def.get("organ_stages", Array());
    std::vector<CharString> organ_name_cs;
    organ_name_cs.reserve(stages.size());
    for (int i = 0; i < stages.size(); ++i) {
        Dictionary stage = stages[i];
        source_law::PathOrganStage s;
        String organ_name_str = stage.get("organ_name", "");
        organ_name_cs.push_back(organ_name_str.utf8());
        s.organ_name = organ_name_cs.back().get_data();
        s.slot = static_cast<source_law::OrganSlot>(
            static_cast<int>(stage.get("slot", 1)));
        s.element = static_cast<magic::RuneElement>(
            static_cast<int>(stage.get("element", primary_element)));
        s.min_sublimation_level =
            static_cast<int>(stage.get("min_sublimation_level", 1));
        s.sublimation_degree_granted =
            static_cast<int>(stage.get("sublimation_degree_granted", 1));
        path_def.organ_stages.push_back(s);
    }

    return source_law::SublimationPathRegistry::register_path(path_def);
}

bool GDSublimationPathRegistry::register_skill(const Dictionary& def) {
    String id_str = def.get("id", "");
    if (id_str.is_empty()) return false;

    String title_str = def.get("title_key", "");

    // Keep UTF-8 buffers alive across the registry call.
    CharString id_cs = id_str.utf8();
    CharString title_cs = title_str.utf8();

    source_law::OrganSkillDef skill;
    skill.id = id_cs.get_data();
    skill.title_key = title_cs.get_data();
    skill.required_slot = static_cast<source_law::OrganSlot>(
        static_cast<int>(def.get("required_slot", 1)));
    skill.required_path = static_cast<source_law::SublimationPath>(
        static_cast<int>(def.get("required_path", 1)));
    skill.min_organ_level = static_cast<int>(def.get("min_organ_level", 0));
    skill.mana_cost = static_cast<int>(def.get("mana_cost", 10));
    skill.cooldown_ticks = static_cast<int>(def.get("cooldown_ticks", 60));
    skill.effect_type = static_cast<int>(def.get("effect_type", 0));
    skill.effect_param_1 = static_cast<float>(
        static_cast<double>(def.get("effect_param_1", 0.0)));
    skill.effect_param_2 = static_cast<float>(
        static_cast<double>(def.get("effect_param_2", 0.0)));

    return source_law::SublimationPathRegistry::register_skill(skill);
}

} // namespace science_and_theology
