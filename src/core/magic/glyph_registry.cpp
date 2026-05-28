#include "glyph_registry.hpp"

#include <string>
#include <unordered_map>
#include <vector>

namespace science_and_theology::magic {

static std::vector<GlyphDef> g_glyph_registry;
static std::vector<std::string> g_glyph_name_storage;
static std::unordered_map<std::string, GlyphId> g_glyph_name_map;

// Index: [element][tier] → GlyphId for effect glyphs
static GlyphId g_effect_glyph_index[static_cast<int>(RuneElement::COUNT)][static_cast<int>(RuneTier::COUNT)];
// Index: [element][tier] → GlyphId for augment glyphs
static GlyphId g_augment_glyph_index[static_cast<int>(RuneElement::COUNT)][static_cast<int>(RuneTier::COUNT)];
// Index: [form] → GlyphId for form glyphs
static GlyphId g_form_glyph_index[static_cast<int>(SpellForm::COUNT)];

void GlyphRegistry::initialize() {
    g_glyph_registry.clear();
    g_glyph_name_storage.clear();
    g_glyph_name_map.clear();

    for (int e = 0; e < static_cast<int>(RuneElement::COUNT); ++e) {
        for (int t = 0; t < static_cast<int>(RuneTier::COUNT); ++t) {
            g_effect_glyph_index[e][t] = kInvalidGlyphId;
            g_augment_glyph_index[e][t] = kInvalidGlyphId;
        }
    }
    for (int f = 0; f < static_cast<int>(SpellForm::COUNT); ++f) {
        g_form_glyph_index[f] = kInvalidGlyphId;
    }

    // Reserve ID 0 as invalid.
    g_glyph_registry.push_back({});
    g_glyph_name_storage.push_back("__invalid__");

    register_builtin_glyphs();
}

const GlyphDef* GlyphRegistry::get_by_id(GlyphId id) {
    if (id == kInvalidGlyphId || id >= g_glyph_registry.size()) return nullptr;
    return &g_glyph_registry[id];
}

const GlyphDef* GlyphRegistry::get_by_name(const char* name) {
    auto it = g_glyph_name_map.find(name);
    if (it == g_glyph_name_map.end()) return nullptr;
    return get_by_id(it->second);
}

const GlyphDef* GlyphRegistry::get_effect_glyph(RuneElement element, RuneTier tier) {
    int e = static_cast<int>(element);
    int t = static_cast<int>(tier);
    return get_by_id(g_effect_glyph_index[e][t]);
}

const GlyphDef* GlyphRegistry::get_augment_glyph(RuneElement element, RuneTier tier) {
    int e = static_cast<int>(element);
    int t = static_cast<int>(tier);
    return get_by_id(g_augment_glyph_index[e][t]);
}

const GlyphDef* GlyphRegistry::get_form_glyph(SpellForm form) {
    int f = static_cast<int>(form);
    return get_by_id(g_form_glyph_index[f]);
}

GlyphId GlyphRegistry::get_id(const char* name) {
    auto it = g_glyph_name_map.find(name);
    return (it != g_glyph_name_map.end()) ? it->second : kInvalidGlyphId;
}

size_t GlyphRegistry::count() {
    return g_glyph_registry.size() > 0 ? g_glyph_registry.size() - 1 : 0;
}

static GlyphId register_glyph(const GlyphDef& def, const std::string& name) {
    GlyphId id = static_cast<GlyphId>(g_glyph_registry.size());

    g_glyph_name_storage.push_back(name);
    GlyphDef stored = def;
    stored.name = g_glyph_name_storage.back().c_str();

    g_glyph_registry.push_back(stored);
    g_glyph_name_map[name] = id;
    return id;
}

void GlyphRegistry::register_form_glyphs() {
    const SpellForm forms[] = {
        SpellForm::PROJECTILE,
        SpellForm::SELF,
        SpellForm::AREA,
        SpellForm::BEAM,
        SpellForm::TOUCH
    };

    for (auto form : forms) {
        std::string name = "glyph_form_";
        name += spell_form_name(form);

        GlyphDef def;
        def.slot_type = GlyphSlotType::FORM;
        def.element = RuneElement::FIRE;
        def.tier = RuneTier::COMMON;
        def.potency = 1;
        def.form = form;

        GlyphId id = register_glyph(def, name);
        g_form_glyph_index[static_cast<int>(form)] = id;
    }
}

void GlyphRegistry::register_effect_augment_glyphs() {
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
            GlyphDef effect_def;
            effect_def.slot_type = GlyphSlotType::EFFECT;
            effect_def.element = element;
            effect_def.tier = tier;
            effect_def.potency = rune_tier_potency(tier);
            effect_def.form = SpellForm::PROJECTILE;

            std::string e_name = "glyph_effect_";
            e_name += rune_element_name(element);
            e_name += "_";
            e_name += rune_tier_name(tier);

            GlyphId e_id = register_glyph(effect_def, e_name);
            g_effect_glyph_index[static_cast<int>(element)][static_cast<int>(tier)] = e_id;

            GlyphDef augment_def;
            augment_def.slot_type = GlyphSlotType::AUGMENT;
            augment_def.element = element;
            augment_def.tier = tier;
            augment_def.potency = rune_tier_potency(tier);
            augment_def.form = SpellForm::PROJECTILE;

            std::string a_name = "glyph_augment_";
            a_name += rune_element_name(element);
            a_name += "_";
            a_name += rune_tier_name(tier);

            GlyphId a_id = register_glyph(augment_def, a_name);
            g_augment_glyph_index[static_cast<int>(element)][static_cast<int>(tier)] = a_id;
        }
    }
}

void GlyphRegistry::register_builtin_glyphs() {
    register_form_glyphs();
    register_effect_augment_glyphs();
}

} // namespace science_and_theology::magic
