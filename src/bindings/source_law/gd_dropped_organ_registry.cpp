#include "gd_dropped_organ_registry.hpp"

#include <string>

#include <godot_cpp/core/binder_common.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/array.hpp>

#include "core/source_law/dropped_organ_registry.hpp"

using namespace godot;

namespace science_and_theology {

using namespace source_law;
using namespace magic;

void GDDroppedOrganRegistry::_bind_methods() {
    ClassDB::bind_static_method("GDDroppedOrganRegistry",
        D_METHOD("register_organ", "def"),
        &GDDroppedOrganRegistry::register_organ);
}

bool GDDroppedOrganRegistry::register_organ(const Dictionary& def) {
    String id = def.get("id", "");
    if (id.is_empty()) return false;

    DroppedOrganDef organ;

    // DroppedOrganRegistry::register_organ copies id, title_key and
    // source_creature_id into stable storage, so local strings only need
    // to outlive this call.
    std::string id_str = id.utf8().get_data();
    organ.id = id_str.c_str();

    String title = def.get("title_key", "");
    std::string title_str = title.utf8().get_data();
    organ.title_key = title_str.c_str();

    organ.target_slot = static_cast<OrganSlot>(
        static_cast<int>(def.get("target_slot", 0)));
    organ.source = static_cast<BloodlineSource>(
        static_cast<int>(def.get("source", 0)));

    String creature = def.get("source_creature_id", "");
    std::string creature_str = creature.utf8().get_data();
    organ.source_creature_id = creature_str.c_str();

    organ.primary_element = static_cast<RuneElement>(
        static_cast<int>(def.get("primary_element", 2)));

    organ.secondary_elements.clear();
    Array secondary = def.get("secondary_elements", Array());
    for (int i = 0; i < secondary.size(); ++i) {
        organ.secondary_elements.push_back(
            static_cast<RuneElement>(static_cast<int>(secondary[i])));
    }

    organ.imitated_path = static_cast<SublimationPath>(
        static_cast<int>(def.get("imitated_path", 0)));
    organ.source_cost = static_cast<int>(def.get("source_cost", 0));
    organ.stability_modifier = static_cast<float>(
        def.get("stability_modifier", 0.0));
    organ.mutation_modifier = static_cast<float>(
        def.get("mutation_modifier", 0.0));
    organ.result_quality = static_cast<OrganQuality>(
        static_cast<int>(def.get("result_quality", 1)));
    organ.result_power_multiplier = static_cast<float>(
        def.get("result_power_multiplier", 0.6));

    return DroppedOrganRegistry::register_organ(organ) != kInvalidDroppedOrganId;
}

} // namespace science_and_theology
