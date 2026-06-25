#include "gd_rune_registry.hpp"

#include <godot_cpp/core/binder_common.hpp>
#include <godot_cpp/core/class_db.hpp>

#include "core/magic/rune_registry.hpp"

using namespace godot;

namespace science_and_theology {

using namespace magic;

void GDRuneRegistry::_bind_methods() {
    ClassDB::bind_static_method("GDRuneRegistry",
        D_METHOD("register_rune", "def"),
        &GDRuneRegistry::register_rune);
    ClassDB::bind_method(D_METHOD("get_rune_by_name", "name"),
                         &GDRuneRegistry::get_rune_by_name);
    ClassDB::bind_method(D_METHOD("get_rune", "element", "tier"),
                         &GDRuneRegistry::get_rune);
    ClassDB::bind_method(D_METHOD("get_rune_count"),
                         &GDRuneRegistry::get_rune_count);
    ClassDB::bind_method(D_METHOD("get_all_rune_names"),
                         &GDRuneRegistry::get_all_rune_names);
}

bool GDRuneRegistry::register_rune(const Dictionary& def) {
    String name = def.get("name", "");
    if (name.is_empty()) return false;

    RuneDef rune;
    rune.name = name.utf8().get_data();
    rune.element = static_cast<RuneElement>(
        static_cast<int>(def.get("element", 0)));
    rune.tier = static_cast<RuneTier>(
        static_cast<int>(def.get("tier", 0)));
    rune.potency = static_cast<int>(def.get("potency", 1));

    return RuneRegistry::register_rune(rune) != kInvalidRuneId;
}

godot::Dictionary GDRuneRegistry::get_rune_by_name(const godot::String& name) const {
    godot::Dictionary dict;
    const RuneDef* rune = RuneRegistry::get_by_name(name.utf8().get_data());
    if (rune == nullptr) return dict;

    dict["name"] = rune->name;
    dict["element"] = static_cast<int>(rune->element);
    dict["tier"] = static_cast<int>(rune->tier);
    dict["potency"] = rune->potency;
    return dict;
}

godot::Dictionary GDRuneRegistry::get_rune(int element, int tier) const {
    godot::Dictionary dict;
    auto elem = static_cast<RuneElement>(element);
    auto t = static_cast<RuneTier>(tier);
    const RuneDef* rune = RuneRegistry::get(elem, t);
    if (rune == nullptr) return dict;

    dict["name"] = rune->name;
    dict["element"] = static_cast<int>(rune->element);
    dict["tier"] = static_cast<int>(rune->tier);
    dict["potency"] = rune->potency;
    return dict;
}

int GDRuneRegistry::get_rune_count() const {
    return static_cast<int>(RuneRegistry::count());
}

godot::PackedStringArray GDRuneRegistry::get_all_rune_names() const {
    godot::PackedStringArray arr;
    const RuneElement elements[] = {
        RuneElement::FIRE, RuneElement::WATER, RuneElement::EARTH,
        RuneElement::AIR, RuneElement::LIGHT, RuneElement::DARK,
        RuneElement::ORDER, RuneElement::CHAOS
    };
    const RuneTier tiers[] = {
        RuneTier::COMMON, RuneTier::REFINED,
        RuneTier::SUPERIOR, RuneTier::LEGENDARY
    };
    for (auto element : elements) {
        for (auto tier : tiers) {
            const RuneDef* rune = RuneRegistry::get(element, tier);
            if (rune) {
                arr.append(rune->name);
            }
        }
    }
    return arr;
}

} // namespace science_and_theology
