#pragma once

#include <cstdint>
#include <cstddef>

#include "material_form.hpp"

namespace science_and_theology::gt {

// Physical state of matter. Mirrors GT5's material state system.
// Determines which forms a material can take and how it behaves in recipes.
enum class MaterialState : uint8_t {
    SOLID  = 0,  // metals, gems, stone, dust
    LIQUID = 1,  // water, acids, creosote, molten metals
    GAS    = 2,  // oxygen, hydrogen, steam, natural gas
    PLASMA = 3,  // ionized gases (late-game)
};

// Human-readable state names for UI.
constexpr const char* kMaterialStateNames[] = {
    "Solid", "Liquid", "Gas", "Plasma",
};

// Generation flags mirroring GT5's material category system.
// Each material declares which item forms it should auto-generate.
enum class MaterialGenFlag : uint16_t {
    DUST   = 1 << 0,  // generate dust variants
    METAL  = 1 << 1,  // generate ingot/plate/rod/bolt/screw/gear/ring/rotor
    GEM    = 1 << 2,  // generate gem variants (ruby, diamond, etc.)
    ORE    = 1 << 3,  // generate crushed/purified ore variants
    CELL   = 1 << 4,  // generate fluid cell
    PLASMA = 1 << 5,  // generate plasma cell
    WIRE   = 1 << 6,  // generate fine wire / wire
    BLOCK  = 1 << 7,  // generate storage block
};

inline constexpr uint16_t operator|(MaterialGenFlag a, MaterialGenFlag b) {
    return static_cast<uint16_t>(a) | static_cast<uint16_t>(b);
}
inline constexpr uint16_t operator|(uint16_t a, MaterialGenFlag b) {
    return a | static_cast<uint16_t>(b);
}
inline constexpr bool operator&(uint16_t a, MaterialGenFlag b) {
    return (a & static_cast<uint16_t>(b)) != 0;
}

// Convenience bitmask combinations for registration.
namespace gen_flags {
    constexpr uint16_t DUST_ONLY = static_cast<uint16_t>(MaterialGenFlag::DUST);
    constexpr uint16_t METAL_FULL = DUST_ONLY
        | static_cast<uint16_t>(MaterialGenFlag::METAL)
        | static_cast<uint16_t>(MaterialGenFlag::BLOCK)
        | static_cast<uint16_t>(MaterialGenFlag::WIRE);
    constexpr uint16_t ORE_FULL = METAL_FULL
        | static_cast<uint16_t>(MaterialGenFlag::ORE);
    constexpr uint16_t GEM_FULL = DUST_ONLY
        | static_cast<uint16_t>(MaterialGenFlag::GEM)
        | static_cast<uint16_t>(MaterialGenFlag::BLOCK);
    constexpr uint16_t FLUID = static_cast<uint16_t>(MaterialGenFlag::CELL);
    constexpr uint16_t FLUID_PLASMA = FLUID
        | static_cast<uint16_t>(MaterialGenFlag::PLASMA);
    constexpr uint16_t GAS = FLUID;          // gases stored in cells
    constexpr uint16_t GAS_PLASMA = FLUID_PLASMA;  // plasma-grade gases
} // namespace gen_flags

// Chemical element reference for material composition display.
enum class Element : uint8_t {
    H, He,
    Li, Be, B,  C,  N,  O,  F,  Ne,
    Na, Mg, Al, Si, P,  S,  Cl, Ar,
    K,  Ca, Sc, Ti, V,  Cr, Mn, Fe, Co, Ni, Cu, Zn,
    Ga, Ge, As, Se, Br, Kr,
    Rb, Sr, Y,  Zr, Nb, Mo, Tc, Ru, Rh, Pd, Ag, Cd,
    In, Sn, Sb, Te, I,  Xe,
    Cs, Ba, La, Ce, Pr, Nd, Pm, Sm, Eu, Gd, Tb, Dy, Ho, Er, Tm, Yb, Lu,
    Hf, Ta, W,  Re, Os, Ir, Pt, Au, Hg,
    Tl, Pb, Bi, Po, At, Rn,
    // Actinides (used by nuclear materials)
    Fr, Ra, Ac, Th, Pa, U, Np, Pu, Am, Cm, Bk, Cf, Es, Fm, Md, No, Lr,
    COUNT
};

// Named element symbols for display.
constexpr const char* kElementSymbols[] = {
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
    // Actinides
    "Fr", "Ra", "Ac", "Th", "Pa", "U", "Np", "Pu", "Am", "Cm", "Bk", "Cf", "Es", "Fm", "Md", "No", "Lr",
};

// A chemical composition entry: element + count in formula.
struct ElementComposition {
    Element element;
    uint8_t count; // e.g., Fe2O3: Fe=2, O=3
};

// Core material definition.
// Mirrors GT5's Materials class and IOreMaterial interface.
struct Material {
    const char* name;           // internal name, e.g. "copper"
    const char* title_key;      // translation key, e.g. "material.copper"
    uint16_t id;                // numeric ID (MetaItemSubID in GT5 terms)
    uint16_t generation_flags;  // bitmask of MaterialGenFlag
    MaterialState state;        // solid / liquid / gas / plasma
    uint32_t color;             // RGBA color for UI tinting (0xRRGGBB)
    int64_t melting_point;      // melting point in Kelvin (0 = N/A)
    int64_t boiling_point;      // boiling point in Kelvin (0 = N/A)
    float mass;                 // relative mass (protons/unit, ~1 for light metals)
    uint8_t element_count;      // number of elements in composition
    const ElementComposition* composition; // chemical formula array
    const char* chemical_formula; // display string, e.g. "Fe2O3"

    // Convenience queries.
    bool is_solid()  const { return state == MaterialState::SOLID; }
    bool is_liquid() const { return state == MaterialState::LIQUID; }
    bool is_gas()    const { return state == MaterialState::GAS; }
    bool is_plasma() const { return state == MaterialState::PLASMA; }
    bool is_fluid()  const { return state == MaterialState::LIQUID
                                   || state == MaterialState::GAS; }

    // Returns true if this material should generate items for the given form.
    bool generates_form(MaterialForm form) const;
};

// Forward declare registry functions.
const Material* get_material(const char* name);
const Material* get_material_by_id(uint16_t id);
size_t get_material_count();

// Hard upper bound on materials to keep item ID range predictable.
// 5000 materials × 43 forms = 215K items — fits in uint32_t comfortably.
inline constexpr uint16_t kMaxMaterials = 5000;

// Iteration: returns the highest assigned material ID + 1, or 0 if empty.
// Material IDs are sequential (0, 1, 2, ...), so iterating 0..max_id-1
// and calling get_material_by_id(id) will visit every registered material.
// This only counts IDs issued via allocate_id(); manually registered IDs
// beyond g_next_material_id are NOT included (rare edge case).
uint16_t get_max_material_id();

} // namespace science_and_theology::gt
