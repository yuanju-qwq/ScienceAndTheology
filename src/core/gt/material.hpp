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
    Cs, Ba, La, Hf, Ta, W,  Re, Os, Ir, Pt, Au, Hg,
    Tl, Pb, Bi, Po, At, Rn,
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
    "Cs", "Ba", "La", "Hf", "Ta", "W",  "Re", "Os", "Ir", "Pt", "Au", "Hg",
    "Tl", "Pb", "Bi", "Po", "At", "Rn",
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
    const char* display_name;   // localized name, e.g. "Copper"
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

// Pre-defined material IDs for compile-time referencing.
// This mirrors GT5's Materials.* static fields.
namespace materials {
    // IDs are assigned sequentially starting from 0.

    // --- Primitive / stone-age / non-metal solids ---
    constexpr uint16_t STONE    = 0;
    constexpr uint16_t FLINT    = 1;
    constexpr uint16_t COAL     = 2;
    constexpr uint16_t CHARCOAL = 3;
    constexpr uint16_t LIGNITE  = 4;

    // --- Basic metals (solid, DUST | METAL | BLOCK | WIRE) ---
    constexpr uint16_t COPPER     = 5;
    constexpr uint16_t TIN        = 6;
    constexpr uint16_t IRON       = 7;
    constexpr uint16_t LEAD       = 8;
    constexpr uint16_t SILVER     = 9;
    constexpr uint16_t GOLD       = 10;
    constexpr uint16_t ZINC       = 11;
    constexpr uint16_t NICKEL     = 12;
    constexpr uint16_t ALUMINIUM  = 13;
    constexpr uint16_t PLATINUM   = 14;
    constexpr uint16_t TUNGSTEN   = 15;
    constexpr uint16_t TITANIUM   = 16;
    constexpr uint16_t CHROME     = 17;
    constexpr uint16_t MANGANESE  = 18;
    constexpr uint16_t COBALT     = 19;
    constexpr uint16_t BISMUTH    = 20;
    constexpr uint16_t ANTIMONY   = 21;

    // --- Alloys (solid) ---
    constexpr uint16_t BRONZE       = 22;  // Cu + Sn
    constexpr uint16_t BRASS        = 23;  // Cu + Zn
    constexpr uint16_t STEEL        = 24;  // Fe + C
    constexpr uint16_t STAINLESS_STEEL = 25;
    constexpr uint16_t ELECTRUM     = 26;  // Au + Ag
    constexpr uint16_t INVAR        = 27;  // Fe + Ni
    constexpr uint16_t CUPRONICKEL  = 28;  // Cu + Ni
    constexpr uint16_t SOLDER       = 29;  // Sn + Pb
    constexpr uint16_t TIN_ALLOY    = 30;
    constexpr uint16_t RED_ALLOY    = 31;
    constexpr uint16_t ANNEALED_COPPER = 32;
    constexpr uint16_t TUNGSTENSTEEL = 33;
    constexpr uint16_t HSS_G        = 34;
    constexpr uint16_t NAQUADAH     = 35;
    constexpr uint16_t NAQUADAH_ALLOY = 36;

    // --- Gems (solid) ---
    constexpr uint16_t DIAMOND   = 37;
    constexpr uint16_t RUBY      = 38;
    constexpr uint16_t SAPPHIRE  = 39;
    constexpr uint16_t EMERALD   = 40;
    constexpr uint16_t AMETHYST  = 41;
    constexpr uint16_t LAPIS     = 42;
    constexpr uint16_t QUARTZ    = 43;
    constexpr uint16_t NETHER_QUARTZ = 44;

    // --- Rare / exotic metals (solid) ---
    constexpr uint16_t URANIUM    = 45;
    constexpr uint16_t PLUTONIUM  = 46;
    constexpr uint16_t THORIUM    = 47;
    constexpr uint16_t IRIDIUM    = 48;
    constexpr uint16_t OSMIUM     = 49;
    constexpr uint16_t GRAPHENE   = 50;
    constexpr uint16_t SUPERCONDUCTOR = 51;

    // --- Liquids ---
    constexpr uint16_t WATER             = 52;
    constexpr uint16_t LAVA              = 53;
    constexpr uint16_t STEAM             = 54;  // gaseous water, treated as gas
    constexpr uint16_t CREOSOTE          = 55;
    constexpr uint16_t SULFURIC_ACID     = 56;
    constexpr uint16_t HYDROCHLORIC_ACID = 57;
    constexpr uint16_t NITRIC_ACID       = 58;
    constexpr uint16_t HYDROFLUORIC_ACID = 59;
    constexpr uint16_t AQUA_REGIA        = 60;
    constexpr uint16_t LUBRICANT         = 61;
    constexpr uint16_t BIOMASS           = 62;
    constexpr uint16_t ETHANOL           = 63;
    constexpr uint16_t OIL               = 64;
    constexpr uint16_t OIL_HEAVY         = 65;
    constexpr uint16_t OIL_LIGHT         = 66;
    constexpr uint16_t FUEL_DIESEL       = 67;
    constexpr uint16_t FUEL_ROCKET       = 68;
    constexpr uint16_t GLUE              = 69;
    constexpr uint16_t MERCURY           = 70;
    constexpr uint16_t MOLTEN_IRON       = 71;

    // --- Gases ---
    constexpr uint16_t OXYGEN           = 72;
    constexpr uint16_t HYDROGEN         = 73;
    constexpr uint16_t NITROGEN         = 74;
    constexpr uint16_t CARBON_DIOXIDE   = 75;
    constexpr uint16_t CARBON_MONOXIDE  = 76;
    constexpr uint16_t SULFUR_DIOXIDE   = 77;
    constexpr uint16_t NITROGEN_DIOXIDE = 78;
    constexpr uint16_t NITRIC_OXIDE     = 79;
    constexpr uint16_t AMMONIA          = 80;
    constexpr uint16_t METHANE          = 81;
    constexpr uint16_t NATURAL_GAS      = 82;
    constexpr uint16_t HYDROGEN_SULFIDE = 83;
    constexpr uint16_t OZONE            = 84;
    constexpr uint16_t CHLORINE         = 85;
    constexpr uint16_t FLUORINE         = 86;
    constexpr uint16_t BROMINE          = 87;
    constexpr uint16_t IODINE           = 88;

    // --- Noble gases ---
    constexpr uint16_t HELIUM   = 89;
    constexpr uint16_t NEON     = 90;
    constexpr uint16_t ARGON    = 91;
    constexpr uint16_t KRYPTON  = 92;
    constexpr uint16_t XENON    = 93;
    constexpr uint16_t RADON    = 94;

    // --- Plasma-grade materials ---
    constexpr uint16_t DEUTERIUM   = 95;
    constexpr uint16_t TRITIUM     = 96;
    constexpr uint16_t HELIUM_3    = 97;
    constexpr uint16_t PLASMA_NITROGEN = 98;
    constexpr uint16_t PLASMA_OXYGEN   = 99;
    constexpr uint16_t PLASMA_HELIUM   = 100;

    // --- Hydrocarbons / organics (gases & liquids) ---
    constexpr uint16_t ETHYLENE   = 101;
    constexpr uint16_t PROPYLENE  = 102;
    constexpr uint16_t BENZENE    = 103;
    constexpr uint16_t TOLUENE    = 104;
    constexpr uint16_t PHENOL     = 105;
    constexpr uint16_t FORMALDEHYDE = 106;
    constexpr uint16_t ACETIC_ACID  = 107;
    constexpr uint16_t ACETONE      = 108;
    constexpr uint16_t GLYCEROL     = 109;
    constexpr uint16_t VINYL_CHLORIDE = 110;
    constexpr uint16_t STYRENE      = 111;

    constexpr uint16_t COUNT = 112;
} // namespace materials

// Initialize the material registry (called once at startup).
// Registers all pre-defined materials into a lookup table.
void initialize_materials();

} // namespace science_and_theology::gt
