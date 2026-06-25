#include "gd_material_registry.hpp"

#include "core/material/material.hpp"
#include "core/material/material_registry.hpp"
#include "core/material/material_item.hpp"

#include <deque>
#include <string>

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

using namespace godot;
namespace gt = science_and_theology::gt;

// Persistent string storage for compound item keys registered from GDScript.
// ItemRegistry::register_mod_item() stores raw const char* pointers, so the
// strings must outlive the call. A deque ensures pointers remain valid even
// after push_back (elements are not moved on reallocation).
namespace {
struct CompoundStrings {
    std::string key;
    std::string title;
};
std::deque<CompoundStrings> g_compound_strings;
} // anonymous namespace

static const char* ELEMENT_NAMES[] = {
    "H",  "He",
    "Li", "Be", "B",  "C",  "N",  "O",  "F",  "Ne",
    "Na", "Mg", "Al", "Si", "P",  "S",  "Cl", "Ar",
    "K",  "Ca", "Sc", "Ti", "V",  "Cr", "Mn", "Fe", "Co", "Ni", "Cu", "Zn",
    "Ga", "Ge", "As", "Se", "Br", "Kr",
    "Rb", "Sr", "Y",  "Zr", "Nb", "Mo", "Tc", "Ru", "Rh", "Pd", "Ag", "Cd",
    "In", "Sn", "Sb", "Te", "I",  "Xe",
    "Cs", "Ba", "La", "Ce", "Pr", "Nd", "Pm", "Sm", "Eu", "Gd", "Tb", "Dy", "Ho", "Er", "Tm", "Yb", "Lu",
    "Hf", "Ta", "W",  "Re", "Os", "Ir", "Pt", "Au", "Hg",
    "Tl", "Pb", "Bi", "Po", "At", "Rn",
    "Fr", "Ra", "Ac", "Th", "Pa", "U", "Np", "Pu", "Am", "Cm", "Bk", "Cf", "Es", "Fm", "Md", "No", "Lr",
};

static gt::Element element_from_name(const String& name) {
    for (int i = 0; i < static_cast<int>(gt::Element::COUNT); ++i) {
        if (name == ELEMENT_NAMES[i]) {
            return static_cast<gt::Element>(i);
        }
    }
    return gt::Element::COUNT; // sentinel = invalid
}

void GDMaterialRegistry::_bind_methods() {
    ClassDB::bind_static_method("GDMaterialRegistry",
        D_METHOD("register_material", "def"),
        &GDMaterialRegistry::register_material);
    ClassDB::bind_static_method("GDMaterialRegistry",
        D_METHOD("register_compound", "item_key", "title_key"),
        &GDMaterialRegistry::register_compound);
    ClassDB::bind_static_method("GDMaterialRegistry",
        D_METHOD("finalize"),
        &GDMaterialRegistry::finalize);
}

bool GDMaterialRegistry::register_material(const Dictionary& def) {
    // Auto-assign ID if not provided. Sequential from 0.
    int64_t id = static_cast<int64_t>(def.get("id", -1));
    if (id < 0) {
        id = gt::MaterialRegistry::allocate_id();
    }

    const String name = def.get("name", "");
    const String title_key = def.get("title_key", "");
    if (name.is_empty() || title_key.is_empty()) {
        UtilityFunctions::printerr("GDMaterialRegistry: missing name or title_key for id=", id);
        return false;
    }

    const int64_t gen_flags = static_cast<int64_t>(def.get("gen_flags", 0));
    const int64_t state = static_cast<int64_t>(def.get("state", 0));
    const int64_t color = static_cast<int64_t>(def.get("color", 0x808080));
    const int64_t melting_point = static_cast<int64_t>(def.get("melting_point", 0));
    const int64_t boiling_point = static_cast<int64_t>(def.get("boiling_point", 0));
    const double mass = static_cast<double>(def.get("mass", 1.0));
    const String chemical_formula = def.get("chemical_formula", "");

    // Parse elements array (max 8 per material).
    gt::ElementComposition comp_buf[8];
    uint8_t elem_count = 0;

    const Array elements = def.get("elements", Array());
    for (int i = 0; i < elements.size() && i < 8; ++i) {
        const Dictionary entry = elements[i];
        const String elem_name = entry.get("element", "");
        const int64_t count = static_cast<int64_t>(entry.get("count", 1));

        gt::Element elem = element_from_name(elem_name);
        if (elem == gt::Element::COUNT) {
            UtilityFunctions::printerr("GDMaterialRegistry: unknown element '", elem_name, "' for id=", id);
            return false;
        }
        comp_buf[elem_count].element = elem;
        comp_buf[elem_count].count = static_cast<uint8_t>(count);
        ++elem_count;
    }

    // Register via MaterialRegistry (which copies the strings).
    gt::MaterialRegistry::register_material(
        static_cast<uint16_t>(id),
        name.utf8().get_data(),
        title_key.utf8().get_data(),
        static_cast<uint16_t>(gen_flags),
        static_cast<gt::MaterialState>(state),
        static_cast<uint32_t>(color),
        melting_point,
        boiling_point,
        static_cast<float>(mass),
        chemical_formula.utf8().get_data(),
        elem_count,
        elem_count > 0 ? comp_buf : nullptr);

    return true;
}

bool GDMaterialRegistry::register_compound(const String& p_item_key, const String& p_title_key) {
    if (p_item_key.is_empty()) return false;

    // Copy strings into persistent storage (deque ensures pointer stability).
    g_compound_strings.push_back({
        std::string(p_item_key.utf8().get_data()),
        std::string(p_title_key.utf8().get_data())
    });

    gt::ItemId id = gt::ItemRegistry::register_mod_item(
        g_compound_strings.back().key.c_str(),
        g_compound_strings.back().title.c_str()
    );
    return id != gt::kInvalidItemId;
}

void GDMaterialRegistry::finalize() {
    gt::MaterialRegistry::finalize();
}