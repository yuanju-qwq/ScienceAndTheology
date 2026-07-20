// Canonical periodic-table data. The table is constexpr so lookup has no
// registration work, allocation, or script dependency at runtime.

#include "game/chemistry/element_catalog.h"

#include <array>

namespace snt::game::chemistry {
namespace {

constexpr std::array<ElementDefinition, static_cast<size_t>(kElementCount) + 1>
    kElements = {{
        {ElementId{}, "", "", 0, 0, ElementCategory::kUnknown, 0.0},
        {ElementId{1}, "H", "hydrogen", 1, 1, ElementCategory::kNonmetal, 1.008},
        {ElementId{2}, "He", "helium", 1, 18, ElementCategory::kNobleGas, 4.002602},
        {ElementId{3}, "Li", "lithium", 2, 1, ElementCategory::kAlkaliMetal, 6.94},
        {ElementId{4}, "Be", "beryllium", 2, 2, ElementCategory::kAlkalineEarthMetal, 9.0121831},
        {ElementId{5}, "B", "boron", 2, 13, ElementCategory::kMetalloid, 10.81},
        {ElementId{6}, "C", "carbon", 2, 14, ElementCategory::kNonmetal, 12.011},
        {ElementId{7}, "N", "nitrogen", 2, 15, ElementCategory::kNonmetal, 14.007},
        {ElementId{8}, "O", "oxygen", 2, 16, ElementCategory::kNonmetal, 15.999},
        {ElementId{9}, "F", "fluorine", 2, 17, ElementCategory::kHalogen, 18.998403163},
        {ElementId{10}, "Ne", "neon", 2, 18, ElementCategory::kNobleGas, 20.1797},
        {ElementId{11}, "Na", "sodium", 3, 1, ElementCategory::kAlkaliMetal, 22.98976928},
        {ElementId{12}, "Mg", "magnesium", 3, 2, ElementCategory::kAlkalineEarthMetal, 24.305},
        {ElementId{13}, "Al", "aluminium", 3, 13, ElementCategory::kPostTransitionMetal, 26.9815385},
        {ElementId{14}, "Si", "silicon", 3, 14, ElementCategory::kMetalloid, 28.085},
        {ElementId{15}, "P", "phosphorus", 3, 15, ElementCategory::kNonmetal, 30.973761998},
        {ElementId{16}, "S", "sulfur", 3, 16, ElementCategory::kNonmetal, 32.06},
        {ElementId{17}, "Cl", "chlorine", 3, 17, ElementCategory::kHalogen, 35.45},
        {ElementId{18}, "Ar", "argon", 3, 18, ElementCategory::kNobleGas, 39.948},
        {ElementId{19}, "K", "potassium", 4, 1, ElementCategory::kAlkaliMetal, 39.0983},
        {ElementId{20}, "Ca", "calcium", 4, 2, ElementCategory::kAlkalineEarthMetal, 40.078},
        {ElementId{21}, "Sc", "scandium", 4, 3, ElementCategory::kTransitionMetal, 44.955908},
        {ElementId{22}, "Ti", "titanium", 4, 4, ElementCategory::kTransitionMetal, 47.867},
        {ElementId{23}, "V", "vanadium", 4, 5, ElementCategory::kTransitionMetal, 50.9415},
        {ElementId{24}, "Cr", "chromium", 4, 6, ElementCategory::kTransitionMetal, 51.9961},
        {ElementId{25}, "Mn", "manganese", 4, 7, ElementCategory::kTransitionMetal, 54.938044},
        {ElementId{26}, "Fe", "iron", 4, 8, ElementCategory::kTransitionMetal, 55.845},
        {ElementId{27}, "Co", "cobalt", 4, 9, ElementCategory::kTransitionMetal, 58.933194},
        {ElementId{28}, "Ni", "nickel", 4, 10, ElementCategory::kTransitionMetal, 58.6934},
        {ElementId{29}, "Cu", "copper", 4, 11, ElementCategory::kTransitionMetal, 63.546},
        {ElementId{30}, "Zn", "zinc", 4, 12, ElementCategory::kTransitionMetal, 65.38},
        {ElementId{31}, "Ga", "gallium", 4, 13, ElementCategory::kPostTransitionMetal, 69.723},
        {ElementId{32}, "Ge", "germanium", 4, 14, ElementCategory::kMetalloid, 72.630},
        {ElementId{33}, "As", "arsenic", 4, 15, ElementCategory::kMetalloid, 74.921595},
        {ElementId{34}, "Se", "selenium", 4, 16, ElementCategory::kNonmetal, 78.971},
        {ElementId{35}, "Br", "bromine", 4, 17, ElementCategory::kHalogen, 79.904},
        {ElementId{36}, "Kr", "krypton", 4, 18, ElementCategory::kNobleGas, 83.798},
        {ElementId{37}, "Rb", "rubidium", 5, 1, ElementCategory::kAlkaliMetal, 85.4678},
        {ElementId{38}, "Sr", "strontium", 5, 2, ElementCategory::kAlkalineEarthMetal, 87.62},
        {ElementId{39}, "Y", "yttrium", 5, 3, ElementCategory::kTransitionMetal, 88.90584},
        {ElementId{40}, "Zr", "zirconium", 5, 4, ElementCategory::kTransitionMetal, 91.224},
        {ElementId{41}, "Nb", "niobium", 5, 5, ElementCategory::kTransitionMetal, 92.90637},
        {ElementId{42}, "Mo", "molybdenum", 5, 6, ElementCategory::kTransitionMetal, 95.95},
        {ElementId{43}, "Tc", "technetium", 5, 7, ElementCategory::kTransitionMetal, 98.0},
        {ElementId{44}, "Ru", "ruthenium", 5, 8, ElementCategory::kTransitionMetal, 101.07},
        {ElementId{45}, "Rh", "rhodium", 5, 9, ElementCategory::kTransitionMetal, 102.9055},
        {ElementId{46}, "Pd", "palladium", 5, 10, ElementCategory::kTransitionMetal, 106.42},
        {ElementId{47}, "Ag", "silver", 5, 11, ElementCategory::kTransitionMetal, 107.8682},
        {ElementId{48}, "Cd", "cadmium", 5, 12, ElementCategory::kTransitionMetal, 112.414},
        {ElementId{49}, "In", "indium", 5, 13, ElementCategory::kPostTransitionMetal, 114.818},
        {ElementId{50}, "Sn", "tin", 5, 14, ElementCategory::kPostTransitionMetal, 118.710},
        {ElementId{51}, "Sb", "antimony", 5, 15, ElementCategory::kMetalloid, 121.760},
        {ElementId{52}, "Te", "tellurium", 5, 16, ElementCategory::kMetalloid, 127.60},
        {ElementId{53}, "I", "iodine", 5, 17, ElementCategory::kHalogen, 126.90447},
        {ElementId{54}, "Xe", "xenon", 5, 18, ElementCategory::kNobleGas, 131.293},
        {ElementId{55}, "Cs", "caesium", 6, 1, ElementCategory::kAlkaliMetal, 132.90545196},
        {ElementId{56}, "Ba", "barium", 6, 2, ElementCategory::kAlkalineEarthMetal, 137.327},
        {ElementId{57}, "La", "lanthanum", 6, 3, ElementCategory::kLanthanide, 138.90547},
        {ElementId{58}, "Ce", "cerium", 6, 0, ElementCategory::kLanthanide, 140.116},
        {ElementId{59}, "Pr", "praseodymium", 6, 0, ElementCategory::kLanthanide, 140.90766},
        {ElementId{60}, "Nd", "neodymium", 6, 0, ElementCategory::kLanthanide, 144.242},
        {ElementId{61}, "Pm", "promethium", 6, 0, ElementCategory::kLanthanide, 145.0},
        {ElementId{62}, "Sm", "samarium", 6, 0, ElementCategory::kLanthanide, 150.36},
        {ElementId{63}, "Eu", "europium", 6, 0, ElementCategory::kLanthanide, 151.964},
        {ElementId{64}, "Gd", "gadolinium", 6, 0, ElementCategory::kLanthanide, 157.25},
        {ElementId{65}, "Tb", "terbium", 6, 0, ElementCategory::kLanthanide, 158.92535},
        {ElementId{66}, "Dy", "dysprosium", 6, 0, ElementCategory::kLanthanide, 162.500},
        {ElementId{67}, "Ho", "holmium", 6, 0, ElementCategory::kLanthanide, 164.93033},
        {ElementId{68}, "Er", "erbium", 6, 0, ElementCategory::kLanthanide, 167.259},
        {ElementId{69}, "Tm", "thulium", 6, 0, ElementCategory::kLanthanide, 168.93422},
        {ElementId{70}, "Yb", "ytterbium", 6, 0, ElementCategory::kLanthanide, 173.045},
        {ElementId{71}, "Lu", "lutetium", 6, 0, ElementCategory::kLanthanide, 174.9668},
        {ElementId{72}, "Hf", "hafnium", 6, 4, ElementCategory::kTransitionMetal, 178.49},
        {ElementId{73}, "Ta", "tantalum", 6, 5, ElementCategory::kTransitionMetal, 180.94788},
        {ElementId{74}, "W", "tungsten", 6, 6, ElementCategory::kTransitionMetal, 183.84},
        {ElementId{75}, "Re", "rhenium", 6, 7, ElementCategory::kTransitionMetal, 186.207},
        {ElementId{76}, "Os", "osmium", 6, 8, ElementCategory::kTransitionMetal, 190.23},
        {ElementId{77}, "Ir", "iridium", 6, 9, ElementCategory::kTransitionMetal, 192.217},
        {ElementId{78}, "Pt", "platinum", 6, 10, ElementCategory::kTransitionMetal, 195.084},
        {ElementId{79}, "Au", "gold", 6, 11, ElementCategory::kTransitionMetal, 196.966569},
        {ElementId{80}, "Hg", "mercury", 6, 12, ElementCategory::kTransitionMetal, 200.592},
        {ElementId{81}, "Tl", "thallium", 6, 13, ElementCategory::kPostTransitionMetal, 204.38},
        {ElementId{82}, "Pb", "lead", 6, 14, ElementCategory::kPostTransitionMetal, 207.2},
        {ElementId{83}, "Bi", "bismuth", 6, 15, ElementCategory::kPostTransitionMetal, 208.98040},
        {ElementId{84}, "Po", "polonium", 6, 16, ElementCategory::kPostTransitionMetal, 209.0},
        {ElementId{85}, "At", "astatine", 6, 17, ElementCategory::kHalogen, 210.0},
        {ElementId{86}, "Rn", "radon", 6, 18, ElementCategory::kNobleGas, 222.0},
        {ElementId{87}, "Fr", "francium", 7, 1, ElementCategory::kAlkaliMetal, 223.0},
        {ElementId{88}, "Ra", "radium", 7, 2, ElementCategory::kAlkalineEarthMetal, 226.0},
        {ElementId{89}, "Ac", "actinium", 7, 3, ElementCategory::kActinide, 227.0},
        {ElementId{90}, "Th", "thorium", 7, 0, ElementCategory::kActinide, 232.0377},
        {ElementId{91}, "Pa", "protactinium", 7, 0, ElementCategory::kActinide, 231.03588},
        {ElementId{92}, "U", "uranium", 7, 0, ElementCategory::kActinide, 238.02891},
        {ElementId{93}, "Np", "neptunium", 7, 0, ElementCategory::kActinide, 237.0},
        {ElementId{94}, "Pu", "plutonium", 7, 0, ElementCategory::kActinide, 244.0},
        {ElementId{95}, "Am", "americium", 7, 0, ElementCategory::kActinide, 243.0},
        {ElementId{96}, "Cm", "curium", 7, 0, ElementCategory::kActinide, 247.0},
        {ElementId{97}, "Bk", "berkelium", 7, 0, ElementCategory::kActinide, 247.0},
        {ElementId{98}, "Cf", "californium", 7, 0, ElementCategory::kActinide, 251.0},
        {ElementId{99}, "Es", "einsteinium", 7, 0, ElementCategory::kActinide, 252.0},
        {ElementId{100}, "Fm", "fermium", 7, 0, ElementCategory::kActinide, 257.0},
        {ElementId{101}, "Md", "mendelevium", 7, 0, ElementCategory::kActinide, 258.0},
        {ElementId{102}, "No", "nobelium", 7, 0, ElementCategory::kActinide, 259.0},
        {ElementId{103}, "Lr", "lawrencium", 7, 0, ElementCategory::kActinide, 266.0},
        {ElementId{104}, "Rf", "rutherfordium", 7, 4, ElementCategory::kTransitionMetal, 267.0},
        {ElementId{105}, "Db", "dubnium", 7, 5, ElementCategory::kTransitionMetal, 268.0},
        {ElementId{106}, "Sg", "seaborgium", 7, 6, ElementCategory::kTransitionMetal, 269.0},
        {ElementId{107}, "Bh", "bohrium", 7, 7, ElementCategory::kTransitionMetal, 270.0},
        {ElementId{108}, "Hs", "hassium", 7, 8, ElementCategory::kTransitionMetal, 269.0},
        {ElementId{109}, "Mt", "meitnerium", 7, 9, ElementCategory::kTransitionMetal, 278.0},
        {ElementId{110}, "Ds", "darmstadtium", 7, 10, ElementCategory::kTransitionMetal, 281.0},
        {ElementId{111}, "Rg", "roentgenium", 7, 11, ElementCategory::kTransitionMetal, 282.0},
        {ElementId{112}, "Cn", "copernicium", 7, 12, ElementCategory::kTransitionMetal, 285.0},
        {ElementId{113}, "Nh", "nihonium", 7, 13, ElementCategory::kPostTransitionMetal, 286.0},
        {ElementId{114}, "Fl", "flerovium", 7, 14, ElementCategory::kPostTransitionMetal, 289.0},
        {ElementId{115}, "Mc", "moscovium", 7, 15, ElementCategory::kPostTransitionMetal, 290.0},
        {ElementId{116}, "Lv", "livermorium", 7, 16, ElementCategory::kPostTransitionMetal, 293.0},
        {ElementId{117}, "Ts", "tennessine", 7, 17, ElementCategory::kHalogen, 294.0},
        {ElementId{118}, "Og", "oganesson", 7, 18, ElementCategory::kNobleGas, 294.0},
    }};

constexpr bool has_valid_element_layout() {
    if (kElements.front().id.is_valid()) return false;
    for (size_t index = 1; index < kElements.size(); ++index) {
        const ElementDefinition& element = kElements[index];
        if (element.id.atomic_number != index || element.symbol.empty() ||
            element.canonical_name.empty() || element.period == 0 ||
            element.relative_atomic_mass <= 0.0) {
            return false;
        }
    }
    return true;
}

static_assert(has_valid_element_layout(),
              "ElementCatalog must contain every atomic number exactly once");

}  // namespace

const ElementDefinition* ElementCatalog::find(ElementId id) noexcept {
    if (!id.is_valid()) return nullptr;
    return &kElements[id.atomic_number];
}

const ElementDefinition* ElementCatalog::find_by_symbol(std::string_view symbol) noexcept {
    if (symbol.empty() || symbol.size() > 2) return nullptr;
    for (const ElementDefinition& element : all()) {
        if (element.symbol == symbol) return &element;
    }
    return nullptr;
}

std::span<const ElementDefinition> ElementCatalog::all() noexcept {
    return {kElements.data() + 1, static_cast<size_t>(kElementCount)};
}

}  // namespace snt::game::chemistry
