#include "material.hpp"
#include "material_registry.hpp"

#include <cassert>
#include <cstring>

namespace science_and_theology::gt {

// --- Form generation logic ---

bool Material::generates_form(MaterialForm form) const {
    auto f = static_cast<uint16_t>(form);

    // Dust variants — only for solids.
    if (f == static_cast<uint16_t>(MaterialForm::DUST) ||
        f == static_cast<uint16_t>(MaterialForm::TINY_DUST) ||
        f == static_cast<uint16_t>(MaterialForm::SMALL_DUST)) {
        return (generation_flags & MaterialGenFlag::DUST) != 0;
    }

    // Gem variants.
    if (f == static_cast<uint16_t>(MaterialForm::GEM) ||
        f == static_cast<uint16_t>(MaterialForm::FLAWED_GEM) ||
        f == static_cast<uint16_t>(MaterialForm::FLAWLESS_GEM) ||
        f == static_cast<uint16_t>(MaterialForm::EXQUISITE_GEM)) {
        return (generation_flags & MaterialGenFlag::GEM) != 0;
    }

    // Crushed / ore variants.
    if (f == static_cast<uint16_t>(MaterialForm::CRUSHED) ||
        f == static_cast<uint16_t>(MaterialForm::CRUSHED_PURIFIED) ||
        f == static_cast<uint16_t>(MaterialForm::CRUSHED_CENTRIFUGED) ||
        f == static_cast<uint16_t>(MaterialForm::IMPURE_DUST) ||
        f == static_cast<uint16_t>(MaterialForm::PURIFIED_DUST)) {
        return (generation_flags & MaterialGenFlag::ORE) != 0;
    }

    // Metal forms: ingot, plate, rod, bolt, screw, ring, rotor, gear.
    if (f == static_cast<uint16_t>(MaterialForm::INGOT) ||
        f == static_cast<uint16_t>(MaterialForm::INGOT_HOT) ||
        f == static_cast<uint16_t>(MaterialForm::NUGGET) ||
        f == static_cast<uint16_t>(MaterialForm::PLATE) ||
        f == static_cast<uint16_t>(MaterialForm::DOUBLE_PLATE) ||
        f == static_cast<uint16_t>(MaterialForm::DENSE_PLATE) ||
        f == static_cast<uint16_t>(MaterialForm::ROD) ||
        f == static_cast<uint16_t>(MaterialForm::LONG_ROD) ||
        f == static_cast<uint16_t>(MaterialForm::BOLT) ||
        f == static_cast<uint16_t>(MaterialForm::SCREW) ||
        f == static_cast<uint16_t>(MaterialForm::RING) ||
        f == static_cast<uint16_t>(MaterialForm::ROTOR) ||
        f == static_cast<uint16_t>(MaterialForm::GEAR) ||
        f == static_cast<uint16_t>(MaterialForm::SMALL_GEAR)) {
        return (generation_flags & MaterialGenFlag::METAL) != 0;
    }

    // Wires.
    if (f == static_cast<uint16_t>(MaterialForm::WIRE_FINE) ||
        f == static_cast<uint16_t>(MaterialForm::WIRE)) {
        return (generation_flags & MaterialGenFlag::WIRE) != 0;
    }

    // Blocks.
    if (f == static_cast<uint16_t>(MaterialForm::BLOCK)) {
        return (generation_flags & MaterialGenFlag::BLOCK) != 0;
    }

    // Cells — fluids (liquids AND gases) can be stored in cells.
    if (f == static_cast<uint16_t>(MaterialForm::CELL)) {
        return (generation_flags & MaterialGenFlag::CELL) != 0;
    }

    // Plasma cells — only plasma-grade materials.
    if (f == static_cast<uint16_t>(MaterialForm::PLASMA_CELL)) {
        return (generation_flags & MaterialGenFlag::PLASMA) != 0;
    }

    return false;
}

// --- Composition helpers ---

namespace {

#define ELEM(e, count) ElementComposition{Element::e, count}

// Pre-defined chemical compositions for display.
constexpr ElementComposition kComp_Fe[]   = { ELEM(Fe, 1) };
constexpr ElementComposition kComp_Cu[]   = { ELEM(Cu, 1) };
constexpr ElementComposition kComp_Sn[]   = { ELEM(Sn, 1) };
constexpr ElementComposition kComp_Pb[]   = { ELEM(Pb, 1) };
constexpr ElementComposition kComp_Ag[]   = { ELEM(Ag, 1) };
constexpr ElementComposition kComp_Au[]   = { ELEM(Au, 1) };
constexpr ElementComposition kComp_Zn[]   = { ELEM(Zn, 1) };
constexpr ElementComposition kComp_Ni[]   = { ELEM(Ni, 1) };
constexpr ElementComposition kComp_Al[]   = { ELEM(Al, 1) };
constexpr ElementComposition kComp_Pt[]   = { ELEM(Pt, 1) };
constexpr ElementComposition kComp_W[]    = { ELEM(W, 1) };
constexpr ElementComposition kComp_Ti[]   = { ELEM(Ti, 1) };
constexpr ElementComposition kComp_Cr[]   = { ELEM(Cr, 1) };
constexpr ElementComposition kComp_Mn[]   = { ELEM(Mn, 1) };
constexpr ElementComposition kComp_Co[]   = { ELEM(Co, 1) };
constexpr ElementComposition kComp_Bi[]   = { ELEM(Bi, 1) };
constexpr ElementComposition kComp_Sb[]   = { ELEM(Sb, 1) };
constexpr ElementComposition kComp_Hg[]   = { ELEM(Hg, 1) };

constexpr ElementComposition kComp_Bronze[]   = { ELEM(Cu, 3), ELEM(Sn, 1) };
constexpr ElementComposition kComp_Brass[]    = { ELEM(Cu, 3), ELEM(Zn, 1) };
constexpr ElementComposition kComp_Steel[]    = { ELEM(Fe, 1), ELEM(C, 1) };
constexpr ElementComposition kComp_Stainless[] = { ELEM(Fe, 9), ELEM(Cr, 1), ELEM(Ni, 1), ELEM(Mn, 1) };
constexpr ElementComposition kComp_Electrum[] = { ELEM(Au, 1), ELEM(Ag, 1) };
constexpr ElementComposition kComp_Invar[]    = { ELEM(Fe, 2), ELEM(Ni, 1) };
constexpr ElementComposition kComp_Cupronickel[] = { ELEM(Cu, 1), ELEM(Ni, 1) };
constexpr ElementComposition kComp_Solder[]   = { ELEM(Sn, 9), ELEM(Pb, 1) };
constexpr ElementComposition kComp_TinAlloy[] = { ELEM(Sn, 1), ELEM(Fe, 1) };
constexpr ElementComposition kComp_Tungstensteel[] = { ELEM(W, 1), ELEM(Fe, 1) };

// Fluid/gas compositions
constexpr ElementComposition kComp_H2O[]   = { ELEM(H, 2), ELEM(O, 1) };
constexpr ElementComposition kComp_O2[]    = { ELEM(O, 2) };
constexpr ElementComposition kComp_O3[]    = { ELEM(O, 3) };
constexpr ElementComposition kComp_H2[]    = { ELEM(H, 2) };
constexpr ElementComposition kComp_N2[]    = { ELEM(N, 2) };
constexpr ElementComposition kComp_CO2[]   = { ELEM(C, 1), ELEM(O, 2) };
constexpr ElementComposition kComp_CO[]    = { ELEM(C, 1), ELEM(O, 1) };
constexpr ElementComposition kComp_H2SO4[] = { ELEM(H, 2), ELEM(S, 1), ELEM(O, 4) };
constexpr ElementComposition kComp_HCl[]   = { ELEM(H, 1), ELEM(Cl, 1) };
constexpr ElementComposition kComp_HNO3[]  = { ELEM(H, 1), ELEM(N, 1), ELEM(O, 3) };
constexpr ElementComposition kComp_HF[]    = { ELEM(H, 1), ELEM(F, 1) };
constexpr ElementComposition kComp_SO2[]   = { ELEM(S, 1), ELEM(O, 2) };
constexpr ElementComposition kComp_NO2[]   = { ELEM(N, 1), ELEM(O, 2) };
constexpr ElementComposition kComp_NO[]    = { ELEM(N, 1), ELEM(O, 1) };
constexpr ElementComposition kComp_NH3[]   = { ELEM(N, 1), ELEM(H, 3) };
constexpr ElementComposition kComp_CH4[]   = { ELEM(C, 1), ELEM(H, 4) };
constexpr ElementComposition kComp_H2S[]   = { ELEM(H, 2), ELEM(S, 1) };
constexpr ElementComposition kComp_Cl2[]   = { ELEM(Cl, 2) };
constexpr ElementComposition kComp_F2[]    = { ELEM(F, 2) };
constexpr ElementComposition kComp_Br2[]   = { ELEM(Br, 2) };
constexpr ElementComposition kComp_I2[]    = { ELEM(I, 2) };
constexpr ElementComposition kComp_C2H4[]  = { ELEM(C, 2), ELEM(H, 4) };
constexpr ElementComposition kComp_C3H6[]  = { ELEM(C, 3), ELEM(H, 6) };
constexpr ElementComposition kComp_C6H6[]  = { ELEM(C, 6), ELEM(H, 6) };
constexpr ElementComposition kComp_C7H8[]  = { ELEM(C, 7), ELEM(H, 8) };
constexpr ElementComposition kComp_CH2O[]  = { ELEM(C, 1), ELEM(H, 2), ELEM(O, 1) };
constexpr ElementComposition kComp_C2H4O[] = { ELEM(C, 2), ELEM(H, 4), ELEM(O, 1) };
constexpr ElementComposition kComp_C3H8O3[] = { ELEM(C, 3), ELEM(H, 8), ELEM(O, 3) };
constexpr ElementComposition kComp_C2H3Cl[] = { ELEM(C, 2), ELEM(H, 3), ELEM(Cl, 1) };
constexpr ElementComposition kComp_C8H8[]  = { ELEM(C, 8), ELEM(H, 8) };

#undef ELEM

} // anonymous namespace

// --- Material registry ---

static Material g_material_registry[materials::COUNT];
static bool g_materials_initialized = false;

// Helper to register one material.
static void register_material(
    uint16_t id, const char* name, const char* display_name,
    uint16_t gen_flags, MaterialState state,
    uint32_t color, int64_t melting_point, int64_t boiling_point, float mass,
    const char* chemical_formula,
    uint8_t elem_count, const ElementComposition* composition)
{
    assert(id < materials::COUNT);
    Material& m = g_material_registry[id];
    m.id = id;
    m.name = name;
    m.display_name = display_name;
    m.generation_flags = gen_flags;
    m.state = state;
    m.color = color;
    m.melting_point = melting_point;
    m.boiling_point = boiling_point;
    m.mass = mass;
    m.chemical_formula = chemical_formula;
    m.element_count = elem_count;
    m.composition = composition;
}

// Shorthands for registration convenience.
using S = MaterialState;
namespace gf = gen_flags;

void initialize_materials() {
    if (g_materials_initialized) return;
    g_materials_initialized = true;

    // ====================
    // Primitive / stone-age (SOLID)
    // ====================
    register_material(materials::STONE,    "stone",    "Stone",    gf::DUST_ONLY, S::SOLID, 0x808080, 0,   0, 1.0f, "?",  0, nullptr);
    register_material(materials::FLINT,    "flint",    "Flint",    gf::DUST_ONLY, S::SOLID, 0x303030, 0,   0, 1.0f, "SiO2", 0, nullptr);
    register_material(materials::COAL,     "coal",     "Coal",     gf::DUST_ONLY | static_cast<uint16_t>(MaterialGenFlag::GEM), S::SOLID, 0x1A1A1A, 0, 0, 1.0f, "C", 0, nullptr);
    register_material(materials::CHARCOAL, "charcoal", "Charcoal", gf::DUST_ONLY, S::SOLID, 0x2A1A0A, 0, 0, 1.0f, "C", 0, nullptr);
    register_material(materials::LIGNITE,  "lignite",  "Lignite",  gf::DUST_ONLY, S::SOLID, 0x3A2A1A, 0, 0, 1.0f, "C", 0, nullptr);

    // ====================
    // Basic metals (SOLID, ORE_FULL)
    // ====================
    register_material(materials::COPPER,    "copper",    "Copper",    gf::ORE_FULL, S::SOLID, 0xFF7F24, 1358, 2835, 63.5f,  "Cu", 1, kComp_Cu);
    register_material(materials::TIN,       "tin",       "Tin",       gf::ORE_FULL, S::SOLID, 0xD3D3D3, 505,  2875, 118.7f, "Sn", 1, kComp_Sn);
    register_material(materials::IRON,      "iron",      "Iron",      gf::ORE_FULL, S::SOLID, 0xBEBEBE, 1811, 3134, 55.8f,  "Fe", 1, kComp_Fe);
    register_material(materials::LEAD,      "lead",      "Lead",      gf::ORE_FULL, S::SOLID, 0x4A3B5C, 601,  2022, 207.2f, "Pb", 1, kComp_Pb);
    register_material(materials::SILVER,    "silver",    "Silver",    gf::ORE_FULL, S::SOLID, 0xE0FFFF, 1235, 2435, 107.9f, "Ag", 1, kComp_Ag);
    register_material(materials::GOLD,      "gold",      "Gold",      gf::ORE_FULL, S::SOLID, 0xFFD700, 1337, 3129, 197.0f, "Au", 1, kComp_Au);
    register_material(materials::ZINC,      "zinc",      "Zinc",      gf::METAL_FULL, S::SOLID, 0xFAFAD2, 693, 1180, 65.4f,  "Zn", 1, kComp_Zn);
    register_material(materials::NICKEL,    "nickel",    "Nickel",    gf::ORE_FULL, S::SOLID, 0xC0C0D0, 1728, 3186, 58.7f,  "Ni", 1, kComp_Ni);
    register_material(materials::ALUMINIUM, "aluminium", "Aluminium", gf::ORE_FULL, S::SOLID, 0xE8C396, 933,  2743, 27.0f,  "Al", 1, kComp_Al);
    register_material(materials::PLATINUM,  "platinum",  "Platinum",  gf::ORE_FULL, S::SOLID, 0xE5E4E2, 2041, 4098, 195.1f, "Pt", 1, kComp_Pt);
    register_material(materials::TUNGSTEN,  "tungsten",  "Tungsten",  gf::ORE_FULL, S::SOLID, 0x78828B, 3695, 5828, 183.8f, "W",  1, kComp_W);
    register_material(materials::TITANIUM,  "titanium",  "Titanium",  gf::ORE_FULL, S::SOLID, 0xC4B0C0, 1941, 3560, 47.9f,  "Ti", 1, kComp_Ti);
    register_material(materials::CHROME,    "chrome",    "Chrome",    gf::ORE_FULL, S::SOLID, 0xE5B2C9, 2180, 2944, 52.0f,  "Cr", 1, kComp_Cr);
    register_material(materials::MANGANESE, "manganese", "Manganese", gf::METAL_FULL, S::SOLID, 0xD0C0C0, 1519, 2334, 54.9f, "Mn", 1, kComp_Mn);
    register_material(materials::COBALT,    "cobalt",    "Cobalt",    gf::ORE_FULL, S::SOLID, 0x0047AB, 1768, 3200, 58.9f,  "Co", 1, kComp_Co);
    register_material(materials::BISMUTH,   "bismuth",   "Bismuth",   gf::METAL_FULL, S::SOLID, 0x7CB7BB, 545, 1837, 209.0f, "Bi", 1, kComp_Bi);
    register_material(materials::ANTIMONY,  "antimony",  "Antimony",  gf::METAL_FULL, S::SOLID, 0xE0E0F0, 904, 1860, 121.8f, "Sb", 1, kComp_Sb);

    // ====================
    // Alloys (SOLID)
    // ====================
    register_material(materials::BRONZE,    "bronze",    "Bronze",    gf::METAL_FULL, S::SOLID, 0xCD7F32, 1183, 0, 80.2f,  "Cu3Sn", 2, kComp_Bronze);
    register_material(materials::BRASS,     "brass",     "Brass",     gf::METAL_FULL, S::SOLID, 0xB5A642, 1193, 0, 69.3f,  "Cu3Zn", 2, kComp_Brass);
    register_material(materials::STEEL,     "steel",     "Steel",     gf::METAL_FULL, S::SOLID, 0x808080, 1811, 0, 51.9f,  "FeC",   2, kComp_Steel);
    register_material(materials::STAINLESS_STEEL, "stainless_steel", "Stainless Steel",
                     gf::METAL_FULL, S::SOLID, 0xC8C8DC, 1700, 0, 55.4f, "Fe9CrNiMn", 4, kComp_Stainless);
    register_material(materials::ELECTRUM,  "electrum",  "Electrum",  gf::METAL_FULL, S::SOLID, 0xFFFF66, 1283, 0, 152.5f, "AuAg",  2, kComp_Electrum);
    register_material(materials::INVAR,     "invar",     "Invar",     gf::METAL_FULL, S::SOLID, 0xC0C096, 1700, 0, 57.3f,  "Fe2Ni", 2, kComp_Invar);
    register_material(materials::CUPRONICKEL, "cupronickel", "Cupronickel",
                     gf::METAL_FULL, S::SOLID, 0xD7B740, 1600, 0, 58.3f, "CuNi", 2, kComp_Cupronickel);
    register_material(materials::SOLDER,    "solder",    "Solder",    gf::METAL_FULL, S::SOLID, 0xA0A0C0, 450,  0, 140.0f, "Sn9Pb", 2, kComp_Solder);
    register_material(materials::TIN_ALLOY, "tin_alloy", "Tin Alloy", gf::METAL_FULL, S::SOLID, 0xD0E0E0, 600, 0, 65.0f, "SnFe", 2, kComp_TinAlloy);
    register_material(materials::RED_ALLOY, "red_alloy", "Red Alloy", gf::METAL_FULL, S::SOLID, 0xCC3300, 600, 0, 50.0f, "Cu?", 0, nullptr);
    register_material(materials::ANNEALED_COPPER, "annealed_copper", "Annealed Copper",
                     gf::METAL_FULL, S::SOLID, 0xFF8C42, 1000, 0, 63.5f, "Cu", 1, kComp_Cu);
    register_material(materials::TUNGSTENSTEEL, "tungstensteel", "Tungstensteel",
                     gf::METAL_FULL, S::SOLID, 0x7070A0, 3200, 0, 119.8f, "WFe", 2, kComp_Tungstensteel);
    register_material(materials::HSS_G,     "hss_g",     "HSS-G",     gf::METAL_FULL, S::SOLID, 0x808090, 2500, 0, 55.0f, "?", 0, nullptr);
    register_material(materials::NAQUADAH,  "naquadah",  "Naquadah",  gf::METAL_FULL, S::SOLID, 0x004400, 5400, 0, 300.0f, "Nq", 0, nullptr);
    register_material(materials::NAQUADAH_ALLOY, "naquadah_alloy", "Naquadah Alloy",
                     gf::METAL_FULL, S::SOLID, 0x006600, 7200, 0, 310.0f, "NqAl", 0, nullptr);

    // ====================
    // Gems (SOLID)
    // ====================
    register_material(materials::DIAMOND,  "diamond",  "Diamond",  gf::GEM_FULL, S::SOLID, 0xCCFFFF, 0,    0, 12.0f,  "C",      0, nullptr);
    register_material(materials::RUBY,     "ruby",     "Ruby",     gf::GEM_FULL, S::SOLID, 0xFF0000, 0,    0, 102.0f, "Al2O3",  0, nullptr);
    register_material(materials::SAPPHIRE, "sapphire", "Sapphire", gf::GEM_FULL, S::SOLID, 0x0000FF, 0,    0, 102.0f, "Al2O3",  0, nullptr);
    register_material(materials::EMERALD,  "emerald",  "Emerald",  gf::GEM_FULL, S::SOLID, 0x00FF00, 0,    0, 175.0f, "Be3Al2Si6O18", 0, nullptr);
    register_material(materials::AMETHYST, "amethyst", "Amethyst", gf::GEM_FULL, S::SOLID, 0xCC66FF, 0,    0, 60.0f,  "SiO2",   0, nullptr);
    register_material(materials::LAPIS,    "lapis",    "Lapis",    gf::GEM_FULL, S::SOLID, 0x0000AA, 0,    0, 200.0f, "(Na,Ca)8Al6Si6O24S4", 0, nullptr);
    register_material(materials::QUARTZ,   "quartz",   "Quartz",   gf::GEM_FULL, S::SOLID, 0xE0E0E0, 0,    0, 60.0f,  "SiO2", 0, nullptr);
    register_material(materials::NETHER_QUARTZ, "nether_quartz", "Nether Quartz",
                     gf::GEM_FULL, S::SOLID, 0xFFAAAA, 0, 0, 60.0f, "SiO2", 0, nullptr);

    // ====================
    // Rare / exotic metals (SOLID)
    // ====================
    register_material(materials::URANIUM,    "uranium",   "Uranium",   gf::ORE_FULL, S::SOLID, 0x3C8C3C, 1405, 4404, 238.0f, "U",  0, nullptr);
    register_material(materials::PLUTONIUM,  "plutonium", "Plutonium", gf::ORE_FULL, S::SOLID, 0x8C003C, 913,  3503, 244.0f, "Pu", 0, nullptr);
    register_material(materials::THORIUM,    "thorium",   "Thorium",   gf::ORE_FULL, S::SOLID, 0x2C2C2C, 2115, 5061, 232.0f, "Th", 0, nullptr);
    register_material(materials::IRIDIUM,    "iridium",   "Iridium",   gf::ORE_FULL, S::SOLID, 0xD0D0FF, 2719, 4701, 192.2f, "Ir", 0, nullptr);
    register_material(materials::OSMIUM,     "osmium",    "Osmium",    gf::ORE_FULL, S::SOLID, 0x5B7D9C, 3306, 5285, 190.2f, "Os", 0, nullptr);
    register_material(materials::GRAPHENE,   "graphene",  "Graphene",  gf::DUST_ONLY, S::SOLID, 0x404040, 0, 0, 12.0f, "C", 0, nullptr);
    register_material(materials::SUPERCONDUCTOR, "superconductor", "Superconductor",
                     gf::DUST_ONLY, S::SOLID, 0xFFFFFF, 92, 0, 100.0f, "?", 0, nullptr);

    // ====================
    // Liquids (LIQUID, CELL generation)
    // ====================
    register_material(materials::WATER,      "water",      "Water",       gf::FLUID, S::LIQUID,
                     0x3355FF, 273, 373, 18.0f, "H2O", 2, kComp_H2O);
    register_material(materials::LAVA,       "lava",       "Lava",        0, S::LIQUID,
                     0xFF4400, 1500, 0, 100.0f, "?", 0, nullptr);
    register_material(materials::CREOSOTE,   "creosote",   "Creosote",    gf::FLUID, S::LIQUID,
                     0x806040, 400, 533, 100.0f, "?", 0, nullptr);
    register_material(materials::SULFURIC_ACID, "sulfuric_acid", "Sulfuric Acid",
                     gf::FLUID, S::LIQUID, 0xCCAA00, 283, 610, 98.0f, "H2SO4", 3, kComp_H2SO4);
    register_material(materials::HYDROCHLORIC_ACID, "hydrochloric_acid", "Hydrochloric Acid",
                     gf::FLUID, S::LIQUID, 0x99CC99, 188, 383, 36.5f, "HCl", 2, kComp_HCl);
    register_material(materials::NITRIC_ACID, "nitric_acid", "Nitric Acid",
                     gf::FLUID, S::LIQUID, 0xCCCC00, 231, 356, 63.0f, "HNO3", 3, kComp_HNO3);
    register_material(materials::HYDROFLUORIC_ACID, "hydrofluoric_acid", "Hydrofluoric Acid",
                     gf::FLUID, S::LIQUID, 0x88CC88, 190, 393, 20.0f, "HF", 2, kComp_HF);
    register_material(materials::AQUA_REGIA, "aqua_regia", "Aqua Regia",
                     gf::FLUID, S::LIQUID, 0xFFAA00, 231, 0, 63.0f, "HNO3+3HCl", 0, nullptr);
    register_material(materials::LUBRICANT,  "lubricant",  "Lubricant",   gf::FLUID, S::LIQUID,
                     0xCCBB55, 300, 600, 400.0f, "?", 0, nullptr);
    register_material(materials::BIOMASS,    "biomass",    "Biomass",     gf::FLUID, S::LIQUID,
                     0x338833, 300, 500, 100.0f, "?", 0, nullptr);
    register_material(materials::ETHANOL,    "ethanol",    "Ethanol",     gf::FLUID, S::LIQUID,
                     0xDDBB88, 159, 351, 46.0f, "C2H5OH", 0, nullptr);
    register_material(materials::OIL,        "oil",        "Oil",         gf::FLUID, S::LIQUID,
                     0x2A2A2A, 300, 600, 200.0f, "?", 0, nullptr);
    register_material(materials::OIL_HEAVY,  "oil_heavy",  "Heavy Oil",   gf::FLUID, S::LIQUID,
                     0x1A1A1A, 350, 700, 300.0f, "?", 0, nullptr);
    register_material(materials::OIL_LIGHT,  "oil_light",  "Light Oil",   gf::FLUID, S::LIQUID,
                     0x3A2A1A, 250, 500, 150.0f, "?", 0, nullptr);
    register_material(materials::FUEL_DIESEL, "fuel_diesel", "Diesel Fuel", gf::FLUID, S::LIQUID,
                     0xCCA000, 250, 450, 180.0f, "?", 0, nullptr);
    register_material(materials::FUEL_ROCKET, "fuel_rocket", "Rocket Fuel", gf::FLUID, S::LIQUID,
                     0xFF6600, 200, 400, 120.0f, "?", 0, nullptr);
    register_material(materials::GLUE,       "glue",       "Glue",        gf::FLUID, S::LIQUID,
                     0xDDDD88, 300, 500, 150.0f, "?", 0, nullptr);
    register_material(materials::MERCURY,    "mercury",    "Mercury",     gf::FLUID, S::LIQUID,
                     0xD0D0D0, 234, 630, 200.6f, "Hg", 1, kComp_Hg);
    register_material(materials::MOLTEN_IRON, "molten_iron", "Molten Iron", 0, S::LIQUID,
                     0xFF6600, 1811, 0, 55.8f, "Fe", 1, kComp_Fe);

    // ====================
    // Gases (GAS, CELL generation, can't form dust/ingots)
    // ====================
    register_material(materials::OXYGEN,     "oxygen",     "Oxygen",      gf::GAS_PLASMA, S::GAS,
                     0x80C0FF, 54,  90,  32.0f,  "O2",   2, kComp_O2);
    register_material(materials::HYDROGEN,   "hydrogen",   "Hydrogen",    gf::GAS_PLASMA, S::GAS,
                     0x80C0FF, 14,  20,  2.0f,   "H2",   2, kComp_H2);
    register_material(materials::NITROGEN,   "nitrogen",   "Nitrogen",    gf::GAS_PLASMA, S::GAS,
                     0x80FFC0, 63,  77,  28.0f,  "N2",   2, kComp_N2);
    register_material(materials::CARBON_DIOXIDE, "carbon_dioxide", "Carbon Dioxide",
                     gf::GAS, S::GAS, 0xA0A0A0, 0, 195, 44.0f, "CO2", 2, kComp_CO2);
    register_material(materials::CARBON_MONOXIDE, "carbon_monoxide", "Carbon Monoxide",
                     gf::GAS, S::GAS, 0xB0B0B0, 68, 82, 28.0f, "CO", 2, kComp_CO);
    register_material(materials::SULFUR_DIOXIDE, "sulfur_dioxide", "Sulfur Dioxide",
                     gf::GAS, S::GAS, 0xCCCC88, 200, 263, 64.0f, "SO2", 2, kComp_SO2);
    register_material(materials::NITROGEN_DIOXIDE, "nitrogen_dioxide", "Nitrogen Dioxide",
                     gf::GAS, S::GAS, 0xCC6600, 262, 294, 46.0f, "NO2", 2, kComp_NO2);
    register_material(materials::NITRIC_OXIDE, "nitric_oxide", "Nitric Oxide",
                     gf::GAS, S::GAS, 0xAADDFF, 109, 121, 30.0f, "NO", 2, kComp_NO);
    register_material(materials::AMMONIA,    "ammonia",    "Ammonia",     gf::GAS, S::GAS,
                     0x88CCFF, 195, 240, 17.0f, "NH3", 2, kComp_NH3);
    register_material(materials::METHANE,    "methane",    "Methane",     gf::GAS, S::GAS,
                     0x8866CC, 91,  112, 16.0f, "CH4", 2, kComp_CH4);
    register_material(materials::NATURAL_GAS, "natural_gas", "Natural Gas",
                     gf::GAS, S::GAS, 0x8877BB, 90, 111, 18.0f, "CH4+", 0, nullptr);
    register_material(materials::HYDROGEN_SULFIDE, "hydrogen_sulfide", "Hydrogen Sulfide",
                     gf::GAS, S::GAS, 0xCCCC00, 187, 213, 34.0f, "H2S", 2, kComp_H2S);
    register_material(materials::OZONE,      "ozone",      "Ozone",       gf::GAS, S::GAS,
                     0x4488FF, 80,  161, 48.0f, "O3", 1, kComp_O3);
    register_material(materials::CHLORINE,   "chlorine",   "Chlorine",    gf::GAS, S::GAS,
                     0x88FF88, 172, 239, 70.9f, "Cl2", 1, kComp_Cl2);
    register_material(materials::FLUORINE,   "fluorine",   "Fluorine",    gf::GAS, S::GAS,
                     0xCCFFCC, 54,  85,  38.0f, "F2", 1, kComp_F2);
    register_material(materials::BROMINE,    "bromine",    "Bromine",     gf::GAS, S::GAS,
                     0x884422, 266, 332, 159.8f, "Br2", 1, kComp_Br2);
    register_material(materials::IODINE,     "iodine",     "Iodine",      gf::GAS, S::GAS,
                     0x6644AA, 387, 458, 253.8f, "I2", 1, kComp_I2);

    // ====================
    // Noble gases (GAS, plasma-capable)
    // ====================
    register_material(materials::HELIUM,  "helium",  "Helium",  gf::GAS_PLASMA, S::GAS,
                     0xFFFFCC, 1,   4,   4.0f,  "He", 0, nullptr);
    register_material(materials::NEON,    "neon",    "Neon",    gf::GAS_PLASMA, S::GAS,
                     0xFF4444, 25,  27,  20.2f, "Ne", 0, nullptr);
    register_material(materials::ARGON,   "argon",   "Argon",   gf::GAS_PLASMA, S::GAS,
                     0x44FF44, 84,  87,  39.9f, "Ar", 0, nullptr);
    register_material(materials::KRYPTON, "krypton", "Krypton", gf::GAS_PLASMA, S::GAS,
                     0x4444FF, 116, 120, 83.8f, "Kr", 0, nullptr);
    register_material(materials::XENON,   "xenon",   "Xenon",   gf::GAS_PLASMA, S::GAS,
                     0x8844FF, 161, 165, 131.3f, "Xe", 0, nullptr);
    register_material(materials::RADON,   "radon",   "Radon",   gf::GAS_PLASMA, S::GAS,
                     0xFF44FF, 202, 211, 222.0f, "Rn", 0, nullptr);

    // ====================
    // Plasma-grade materials (GAS with PLASMA flag)
    // ====================
    register_material(materials::DEUTERIUM, "deuterium", "Deuterium", gf::GAS_PLASMA, S::GAS,
                     0xAAAAFF, 19, 24, 4.0f, "D2", 0, nullptr);
    register_material(materials::TRITIUM,   "tritium",   "Tritium",   gf::GAS_PLASMA, S::GAS,
                     0x8888FF, 20, 25, 6.0f, "T2", 0, nullptr);
    register_material(materials::HELIUM_3,  "helium_3",  "Helium-3",  gf::GAS_PLASMA, S::GAS,
                     0xFFFFDD, 1, 4, 3.0f, "He-3", 0, nullptr);
    register_material(materials::PLASMA_NITROGEN, "plasma_nitrogen", "Plasma N2",
                     gf::FLUID_PLASMA, S::PLASMA, 0xAAFFCC, 0, 0, 28.0f, "N2*", 2, kComp_N2);
    register_material(materials::PLASMA_OXYGEN,   "plasma_oxygen",   "Plasma O2",
                     gf::FLUID_PLASMA, S::PLASMA, 0xAACCFF, 0, 0, 32.0f, "O2*", 2, kComp_O2);
    register_material(materials::PLASMA_HELIUM,   "plasma_helium",   "Plasma He",
                     gf::FLUID_PLASMA, S::PLASMA, 0xFFFFEE, 0, 0, 4.0f, "He*", 0, nullptr);

    // ====================
    // Hydrocarbons / organics (gases & liquids)
    // ====================
    register_material(materials::ETHYLENE,   "ethylene",   "Ethylene",    gf::GAS, S::GAS,
                     0xCCBB88, 104, 169, 28.0f, "C2H4", 2, kComp_C2H4);
    register_material(materials::PROPYLENE,  "propylene",  "Propylene",   gf::GAS, S::GAS,
                     0xDDBB88, 88,  225, 42.0f, "C3H6", 2, kComp_C3H6);
    register_material(materials::BENZENE,    "benzene",    "Benzene",     gf::FLUID, S::LIQUID,
                     0xCCBB66, 279, 353, 78.0f, "C6H6", 2, kComp_C6H6);
    register_material(materials::TOLUENE,    "toluene",    "Toluene",     gf::FLUID, S::LIQUID,
                     0xCCAA55, 178, 384, 92.0f, "C7H8", 2, kComp_C7H8);
    register_material(materials::PHENOL,     "phenol",     "Phenol",      gf::FLUID, S::LIQUID,
                     0xDDBB99, 314, 455, 94.0f, "C6H5OH", 0, nullptr);
    register_material(materials::FORMALDEHYDE, "formaldehyde", "Formaldehyde",
                     gf::GAS, S::GAS, 0xCCBBA0, 181, 254, 30.0f, "CH2O", 3, kComp_CH2O);
    register_material(materials::ACETIC_ACID, "acetic_acid", "Acetic Acid",
                     gf::FLUID, S::LIQUID, 0xCCBB66, 290, 391, 60.0f, "C2H4O2", 0, nullptr);
    register_material(materials::ACETONE,    "acetone",    "Acetone",     gf::FLUID, S::LIQUID,
                     0xAAAACC, 178, 329, 58.0f, "C3H6O", 0, nullptr);
    register_material(materials::GLYCEROL,   "glycerol",   "Glycerol",    gf::FLUID, S::LIQUID,
                     0xCCCC88, 291, 563, 92.0f, "C3H8O3", 3, kComp_C3H8O3);
    register_material(materials::VINYL_CHLORIDE, "vinyl_chloride", "Vinyl Chloride",
                     gf::GAS, S::GAS, 0xCCBBCC, 119, 260, 62.5f, "C2H3Cl", 3, kComp_C2H3Cl);
    register_material(materials::STYRENE,    "styrene",    "Styrene",     gf::FLUID, S::LIQUID,
                     0xDDBB99, 243, 418, 104.0f, "C8H8", 2, kComp_C8H8);

    // Note: STEAM is a special case — gaseous water above 373 K.
    // Register it as GAS since it's water vapor.
    register_material(materials::STEAM,      "steam",      "Steam",       gf::GAS, S::GAS,
                     0xC0C0C0, 373, 373, 18.0f, "H2O", 2, kComp_H2O);

    // ====================
    // Organic solids (DUST + PLATE + ROD + BLOCK)
    // ====================
    register_material(materials::WOOD, "wood", "Wood",
        gf::DUST_ONLY | static_cast<uint16_t>(MaterialGenFlag::METAL)
        | static_cast<uint16_t>(MaterialGenFlag::BLOCK),
        S::SOLID, 0x8B5E3C, 0, 0, 0.5f, "C6H10O5", 0, nullptr);

    // ====================
    // Planetary rock types (DUST_ONLY, each yields unique dust)
    // ====================
    register_material(materials::GRANITE,     "granite",     "Granite",     gf::DUST_ONLY, S::SOLID, 0xA09890, 0, 0, 2.7f, "SiO2+Al2O3", 0, nullptr);
    register_material(materials::BASALT,      "basalt",      "Basalt",      gf::DUST_ONLY, S::SOLID, 0x505050, 0, 0, 3.0f, "SiO2+FeO",   0, nullptr);
    register_material(materials::MARBLE,      "marble",      "Marble",      gf::DUST_ONLY, S::SOLID, 0xE8E0D8, 0, 0, 2.7f, "CaCO3",      0, nullptr);
    register_material(materials::SANDSTONE,   "sandstone",   "Sandstone",   gf::DUST_ONLY, S::SOLID, 0xC8A870, 0, 0, 2.3f, "SiO2",       0, nullptr);
    register_material(materials::SHALE,       "shale",       "Shale",       gf::DUST_ONLY, S::SOLID, 0x5A6050, 0, 0, 2.5f, "SiO2+Al2O3", 0, nullptr);
    register_material(materials::KOMATIITE,   "komatiite",   "Komatiite",   gf::DUST_ONLY, S::SOLID, 0x3A5030, 0, 0, 3.2f, "MgO+SiO2",   0, nullptr);
    register_material(materials::REGOLITH,    "regolith",    "Regolith",    gf::DUST_ONLY, S::SOLID, 0xA06040, 0, 0, 1.8f, "SiO2+Fe2O3", 0, nullptr);
    register_material(materials::ANORTHOSTIE, "anorthosite", "Anorthosite", gf::DUST_ONLY, S::SOLID, 0xC0C0C8, 0, 0, 2.7f, "CaAl2Si2O8", 0, nullptr);
}

// --- Registry lookup ---

const Material* get_material(const char* name) {
    if (!g_materials_initialized) return nullptr;

    for (size_t i = 0; i < materials::COUNT; ++i) {
        if (std::strcmp(g_material_registry[i].name, name) == 0) {
            return &g_material_registry[i];
        }
    }
    return nullptr;
}

const Material* get_material_by_id(uint16_t id) {
    if (!g_materials_initialized || id >= materials::COUNT) return nullptr;
    return &g_material_registry[id];
}

size_t get_material_count() {
    return materials::COUNT;
}

// ============================================================
// MaterialRegistry — wrapper class
// ============================================================

void MaterialRegistry::initialize() {
    initialize_materials();
}

const Material* MaterialRegistry::get_by_id(uint16_t id) {
    return get_material_by_id(id);
}

const Material* MaterialRegistry::get_by_name(const char* name) {
    return get_material(name);
}

size_t MaterialRegistry::count() {
    return get_material_count();
}

} // namespace science_and_theology::gt
