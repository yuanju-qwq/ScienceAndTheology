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

    // Built-in glyphs are now registered from GDScript via GDGlyphRegistry
    // (see BuiltinGlyphs.gd).
}

GlyphId GlyphRegistry::register_glyph(const GlyphDef& def) {
    if (g_glyph_registry.size() >= kInvalidGlyphId) return kInvalidGlyphId;

    g_glyph_name_storage.push_back(def.name);
    GlyphDef stored = def;
    stored.name = g_glyph_name_storage.back().c_str();

    GlyphId id = static_cast<GlyphId>(g_glyph_registry.size());
    g_glyph_registry.push_back(stored);
    g_glyph_name_map[stored.name] = id;

    // Update index based on slot_type.
    if (stored.slot_type == GlyphSlotType::FORM) {
        g_form_glyph_index[static_cast<int>(stored.form)] = id;
    } else if (stored.slot_type == GlyphSlotType::EFFECT) {
        g_effect_glyph_index[static_cast<int>(stored.element)][static_cast<int>(stored.tier)] = id;
    } else if (stored.slot_type == GlyphSlotType::AUGMENT) {
        g_augment_glyph_index[static_cast<int>(stored.element)][static_cast<int>(stored.tier)] = id;
    }
    return id;
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

void GlyphRegistry::register_builtin_glyphs() {
    // No-op: built-in glyphs are now registered from GDScript via
    // GDGlyphRegistry (see BuiltinGlyphs.gd).
}

void GlyphRegistry::register_form_glyphs() {
    // No-op: migrated to GDScript (see BuiltinGlyphs.gd).
}

void GlyphRegistry::register_effect_augment_glyphs() {
    // No-op: migrated to GDScript (see BuiltinGlyphs.gd).
}

} // namespace science_and_theology::magic
