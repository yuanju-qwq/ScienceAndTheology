// Standard-element gameplay materials. The table preserves the former
// game-content presentation and physical values while deriving identity,
// formula, and mass from the immutable ElementCatalog.

#include "game/chemistry/builtin_element_material_catalog.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "core/error.h"
#include "game/chemistry/element_catalog.h"
#include "game/client/game_content_registry.h"

namespace snt::game::chemistry {
namespace {

struct ElementMaterialSpec {
    std::string_view material_id;
    uint16_t generation_flags = 0;
    GameMaterialState state = GameMaterialState::kSolid;
    uint32_t color_rgb = 0xffffffu;
    int64_t melting_point_kelvin = 0;
    int64_t boiling_point_kelvin = 0;
    uint8_t atoms_per_formula_unit = 1;
};

constexpr uint16_t kDustGenerationFlags =
    static_cast<uint16_t>(GameMaterialGenerationFlag::kDust);
constexpr uint16_t kCellGenerationFlags =
    static_cast<uint16_t>(GameMaterialGenerationFlag::kCell);
constexpr uint16_t kDustBlockGenerationFlags =
    static_cast<uint16_t>(GameMaterialGenerationFlag::kDust) |
    static_cast<uint16_t>(GameMaterialGenerationFlag::kBlock);
constexpr uint16_t kDustMetalWireBlockGenerationFlags =
    static_cast<uint16_t>(GameMaterialGenerationFlag::kDust) |
    static_cast<uint16_t>(GameMaterialGenerationFlag::kMetal) |
    static_cast<uint16_t>(GameMaterialGenerationFlag::kWire) |
    static_cast<uint16_t>(GameMaterialGenerationFlag::kBlock);
constexpr uint16_t kDustMetalOreWireBlockGenerationFlags =
    static_cast<uint16_t>(GameMaterialGenerationFlag::kDust) |
    static_cast<uint16_t>(GameMaterialGenerationFlag::kMetal) |
    static_cast<uint16_t>(GameMaterialGenerationFlag::kOre) |
    static_cast<uint16_t>(GameMaterialGenerationFlag::kWire) |
    static_cast<uint16_t>(GameMaterialGenerationFlag::kBlock);

// Entries are ordered by atomic number, matching ElementCatalog::all().
constexpr std::array<ElementMaterialSpec, static_cast<size_t>(kElementCount)>
    kElementMaterialSpecs = {{
    {"hydrogen", kCellGenerationFlags, GameMaterialState::kGas, 0x80e0ffu, 14, 20, 2},
    {"helium", kCellGenerationFlags, GameMaterialState::kGas, 0xc0e0ffu, 1, 4, 1},
    {"lithium", kDustMetalOreWireBlockGenerationFlags, GameMaterialState::kSolid, 0xb0b0b0u, 454, 1615, 1},
    {"beryllium", kDustMetalOreWireBlockGenerationFlags, GameMaterialState::kSolid, 0xa0c0a0u, 1560, 3243, 1},
    {"boron", kDustGenerationFlags, GameMaterialState::kSolid, 0x808080u, 2349, 4200, 1},
    {"carbon", kDustGenerationFlags, GameMaterialState::kSolid, 0x242424u, 3823, 4300, 1},
    {"nitrogen", kCellGenerationFlags, GameMaterialState::kGas, 0x80c0ffu, 63, 77, 2},
    {"oxygen", kCellGenerationFlags, GameMaterialState::kGas, 0x4080ffu, 54, 90, 2},
    {"fluorine", kCellGenerationFlags, GameMaterialState::kGas, 0x80ffa0u, 53, 85, 2},
    {"neon", kCellGenerationFlags, GameMaterialState::kGas, 0xff80a0u, 25, 27, 1},
    {"sodium", kDustMetalOreWireBlockGenerationFlags, GameMaterialState::kSolid, 0xc0c0c0u, 371, 1156, 1},
    {"magnesium", kDustMetalOreWireBlockGenerationFlags, GameMaterialState::kSolid, 0xd0d0d0u, 923, 1363, 1},
    {"aluminium", kDustMetalOreWireBlockGenerationFlags, GameMaterialState::kSolid, 0xd0e0f0u, 933, 2792, 1},
    {"silicon", kDustMetalOreWireBlockGenerationFlags, GameMaterialState::kSolid, 0x808090u, 1687, 3173, 1},
    {"phosphorus", kDustGenerationFlags, GameMaterialState::kSolid, 0xc06040u, 317, 554, 1},
    {"sulfur", kDustBlockGenerationFlags, GameMaterialState::kSolid, 0xc0c040u, 388, 718, 1},
    {"chlorine", kCellGenerationFlags, GameMaterialState::kGas, 0x80ff80u, 172, 239, 2},
    {"argon", kCellGenerationFlags, GameMaterialState::kGas, 0x80e0ffu, 84, 87, 1},
    {"potassium", kDustMetalOreWireBlockGenerationFlags, GameMaterialState::kSolid, 0xb0a080u, 337, 1032, 1},
    {"calcium", kDustMetalOreWireBlockGenerationFlags, GameMaterialState::kSolid, 0xc0c0a0u, 1115, 1757, 1},
    {"scandium", kDustMetalOreWireBlockGenerationFlags, GameMaterialState::kSolid, 0xa0a0b0u, 1814, 3103, 1},
    {"titanium", kDustMetalOreWireBlockGenerationFlags, GameMaterialState::kSolid, 0xc0c0d0u, 1941, 3560, 1},
    {"vanadium", kDustMetalOreWireBlockGenerationFlags, GameMaterialState::kSolid, 0xa0a0c0u, 2183, 3680, 1},
    {"chromium", kDustMetalOreWireBlockGenerationFlags, GameMaterialState::kSolid, 0xd0d0e0u, 2180, 2944, 1},
    {"manganese", kDustMetalOreWireBlockGenerationFlags, GameMaterialState::kSolid, 0xc0b0a0u, 1519, 2334, 1},
    {"iron", kDustMetalOreWireBlockGenerationFlags, GameMaterialState::kSolid, 0xc8b0a0u, 1811, 3134, 1},
    {"cobalt", kDustMetalOreWireBlockGenerationFlags, GameMaterialState::kSolid, 0xb0a0c0u, 1768, 3200, 1},
    {"nickel", kDustMetalOreWireBlockGenerationFlags, GameMaterialState::kSolid, 0xa0b0a0u, 1728, 3186, 1},
    {"copper", kDustMetalOreWireBlockGenerationFlags, GameMaterialState::kSolid, 0xff7f24u, 1358, 2835, 1},
    {"zinc", kDustMetalOreWireBlockGenerationFlags, GameMaterialState::kSolid, 0xc0d0c0u, 693, 1180, 1},
    {"gallium", kDustMetalOreWireBlockGenerationFlags, GameMaterialState::kSolid, 0xc0c0c0u, 303, 2477, 1},
    {"germanium", kDustMetalOreWireBlockGenerationFlags, GameMaterialState::kSolid, 0xa0a0a0u, 1211, 3106, 1},
    {"arsenic", kDustGenerationFlags, GameMaterialState::kSolid, 0x808080u, 1090, 887, 1},
    {"selenium", kDustGenerationFlags, GameMaterialState::kSolid, 0xc0a040u, 494, 958, 1},
    {"bromine", kCellGenerationFlags, GameMaterialState::kGas, 0xa04020u, 266, 332, 2},
    {"krypton", kCellGenerationFlags, GameMaterialState::kGas, 0x80ffe0u, 116, 120, 1},
    {"rubidium", kDustMetalOreWireBlockGenerationFlags, GameMaterialState::kSolid, 0xa09080u, 312, 961, 1},
    {"strontium", kDustMetalOreWireBlockGenerationFlags, GameMaterialState::kSolid, 0xc0b0a0u, 1050, 1655, 1},
    {"yttrium", kDustMetalOreWireBlockGenerationFlags, GameMaterialState::kSolid, 0xa0a0a0u, 1799, 3609, 1},
    {"zirconium", kDustMetalOreWireBlockGenerationFlags, GameMaterialState::kSolid, 0xa0b0b0u, 2128, 4650, 1},
    {"niobium", kDustMetalOreWireBlockGenerationFlags, GameMaterialState::kSolid, 0xa0a0b0u, 2750, 5017, 1},
    {"molybdenum", kDustMetalOreWireBlockGenerationFlags, GameMaterialState::kSolid, 0xa0a0a0u, 2896, 4912, 1},
    {"technetium", kDustMetalWireBlockGenerationFlags, GameMaterialState::kSolid, 0x909090u, 2430, 4538, 1},
    {"ruthenium", kDustMetalOreWireBlockGenerationFlags, GameMaterialState::kSolid, 0xa0a0a0u, 2607, 4423, 1},
    {"rhodium", kDustMetalOreWireBlockGenerationFlags, GameMaterialState::kSolid, 0xc0c0d0u, 2237, 3968, 1},
    {"palladium", kDustMetalOreWireBlockGenerationFlags, GameMaterialState::kSolid, 0xc0c0c0u, 1828, 3236, 1},
    {"silver", kDustMetalOreWireBlockGenerationFlags, GameMaterialState::kSolid, 0xc0c0e0u, 1235, 2435, 1},
    {"cadmium", kDustMetalOreWireBlockGenerationFlags, GameMaterialState::kSolid, 0xa0a0a0u, 594, 1040, 1},
    {"indium", kDustMetalOreWireBlockGenerationFlags, GameMaterialState::kSolid, 0xb0b0c0u, 430, 2345, 1},
    {"tin", kDustMetalOreWireBlockGenerationFlags, GameMaterialState::kSolid, 0xe0e0e0u, 505, 2875, 1},
    {"antimony", kDustMetalWireBlockGenerationFlags, GameMaterialState::kSolid, 0xe0e0f0u, 904, 1860, 1},
    {"tellurium", kDustGenerationFlags, GameMaterialState::kSolid, 0xb0a060u, 723, 1261, 1},
    {"iodine", kCellGenerationFlags, GameMaterialState::kGas, 0x6020a0u, 387, 458, 2},
    {"xenon", kCellGenerationFlags, GameMaterialState::kGas, 0x8080e0u, 161, 165, 1},
    {"caesium", kDustMetalOreWireBlockGenerationFlags, GameMaterialState::kSolid, 0xa09070u, 302, 944, 1},
    {"barium", kDustMetalOreWireBlockGenerationFlags, GameMaterialState::kSolid, 0xb0b0a0u, 1000, 2070, 1},
    {"lanthanum", kDustMetalOreWireBlockGenerationFlags, GameMaterialState::kSolid, 0xa0a0a0u, 1193, 3737, 1},
    {"cerium", kDustMetalOreWireBlockGenerationFlags, GameMaterialState::kSolid, 0xb0b0b0u, 1068, 3716, 1},
    {"praseodymium", kDustMetalOreWireBlockGenerationFlags, GameMaterialState::kSolid, 0xb0c080u, 1208, 3793, 1},
    {"neodymium", kDustMetalOreWireBlockGenerationFlags, GameMaterialState::kSolid, 0xa0b100u, 1297, 3347, 1},
    {"promethium", kDustMetalWireBlockGenerationFlags, GameMaterialState::kSolid, 0xa0c0a0u, 1315, 3273, 1},
    {"samarium", kDustMetalOreWireBlockGenerationFlags, GameMaterialState::kSolid, 0xc0b0e0u, 1345, 2067, 1},
    {"europium", kDustMetalOreWireBlockGenerationFlags, GameMaterialState::kSolid, 0xb0c0c0u, 1099, 1802, 1},
    {"gadolinium", kDustMetalOreWireBlockGenerationFlags, GameMaterialState::kSolid, 0xb0e090u, 1585, 3546, 1},
    {"terbium", kDustMetalOreWireBlockGenerationFlags, GameMaterialState::kSolid, 0xb0d090u, 1629, 3503, 1},
    {"dysprosium", kDustMetalOreWireBlockGenerationFlags, GameMaterialState::kSolid, 0xa0b100u, 1680, 2840, 1},
    {"holmium", kDustMetalOreWireBlockGenerationFlags, GameMaterialState::kSolid, 0xb0a0c0u, 1734, 2993, 1},
    {"erbium", kDustMetalOreWireBlockGenerationFlags, GameMaterialState::kSolid, 0xc0b0a0u, 1802, 3141, 1},
    {"thulium", kDustMetalOreWireBlockGenerationFlags, GameMaterialState::kSolid, 0xb0b080u, 1818, 2223, 1},
    {"ytterbium", kDustMetalOreWireBlockGenerationFlags, GameMaterialState::kSolid, 0xc0c0c0u, 1097, 1469, 1},
    {"lutetium", kDustMetalOreWireBlockGenerationFlags, GameMaterialState::kSolid, 0xb0b0a0u, 1936, 3675, 1},
    {"hafnium", kDustMetalOreWireBlockGenerationFlags, GameMaterialState::kSolid, 0xa0a0a0u, 2506, 4876, 1},
    {"tantalum", kDustMetalOreWireBlockGenerationFlags, GameMaterialState::kSolid, 0xa0a0a0u, 3290, 5731, 1},
    {"tungsten", kDustMetalOreWireBlockGenerationFlags, GameMaterialState::kSolid, 0x909090u, 3695, 6203, 1},
    {"rhenium", kDustMetalOreWireBlockGenerationFlags, GameMaterialState::kSolid, 0xa0a0a0u, 3459, 5869, 1},
    {"osmium", kDustMetalOreWireBlockGenerationFlags, GameMaterialState::kSolid, 0x9090a0u, 3306, 5285, 1},
    {"iridium", kDustMetalOreWireBlockGenerationFlags, GameMaterialState::kSolid, 0xc0c0d0u, 2719, 4701, 1},
    {"platinum", kDustMetalOreWireBlockGenerationFlags, GameMaterialState::kSolid, 0xe0e0e0u, 2041, 4098, 1},
    {"gold", kDustMetalOreWireBlockGenerationFlags, GameMaterialState::kSolid, 0xffd700u, 1337, 3129, 1},
    {"mercury", kCellGenerationFlags, GameMaterialState::kLiquid, 0xe0e0e0u, 234, 630, 1},
    {"thallium", kDustMetalOreWireBlockGenerationFlags, GameMaterialState::kSolid, 0xa0a0a0u, 577, 1746, 1},
    {"lead", kDustMetalOreWireBlockGenerationFlags, GameMaterialState::kSolid, 0x7070a0u, 601, 2013, 1},
    {"bismuth", kDustMetalWireBlockGenerationFlags, GameMaterialState::kSolid, 0xd0a0a0u, 545, 1837, 1},
    {"polonium", kDustGenerationFlags, GameMaterialState::kSolid, 0x808080u, 527, 1235, 1},
    {"astatine", kDustGenerationFlags, GameMaterialState::kSolid, 0xa0a0a0u, 575, 610, 1},
    {"radon", kCellGenerationFlags, GameMaterialState::kGas, 0x2080a0u, 202, 211, 1},
    {"francium", kDustMetalWireBlockGenerationFlags, GameMaterialState::kSolid, 0xa0a0a0u, 300, 950, 1},
    {"radium", kDustMetalWireBlockGenerationFlags, GameMaterialState::kSolid, 0xb0b0c0u, 973, 2010, 1},
    {"actinium", kDustMetalWireBlockGenerationFlags, GameMaterialState::kSolid, 0xb0b0a0u, 1323, 3471, 1},
    {"thorium", kDustMetalOreWireBlockGenerationFlags, GameMaterialState::kSolid, 0x80a080u, 2023, 5061, 1},
    {"protactinium", kDustMetalWireBlockGenerationFlags, GameMaterialState::kSolid, 0xa0a0a0u, 1841, 4300, 1},
    {"uranium", kDustMetalOreWireBlockGenerationFlags, GameMaterialState::kSolid, 0x80b080u, 1408, 4404, 1},
    {"neptunium", kDustMetalWireBlockGenerationFlags, GameMaterialState::kSolid, 0xa0a0a0u, 917, 4273, 1},
    {"plutonium", kDustMetalWireBlockGenerationFlags, GameMaterialState::kSolid, 0xb08080u, 913, 3501, 1},
    {"americium", kDustMetalWireBlockGenerationFlags, GameMaterialState::kSolid, 0xb0d080u, 1449, 2880, 1},
    {"curium", kDustMetalWireBlockGenerationFlags, GameMaterialState::kSolid, 0xb0a0c0u, 1613, 3383, 1},
    {"berkelium", kDustMetalWireBlockGenerationFlags, GameMaterialState::kSolid, 0xa0a0a0u, 1259, 2900, 1},
    {"californium", kDustMetalWireBlockGenerationFlags, GameMaterialState::kSolid, 0xb0b080u, 1173, 1743, 1},
    {"einsteinium", kDustMetalWireBlockGenerationFlags, GameMaterialState::kSolid, 0xa0a0a0u, 1133, 1269, 1},
    {"fermium", kDustMetalWireBlockGenerationFlags, GameMaterialState::kSolid, 0x909090u, 0, 0, 1},
    {"mendelevium", kDustMetalWireBlockGenerationFlags, GameMaterialState::kSolid, 0x909090u, 0, 0, 1},
    {"nobelium", kDustMetalWireBlockGenerationFlags, GameMaterialState::kSolid, 0x909090u, 0, 0, 1},
    {"lawrencium", kDustMetalWireBlockGenerationFlags, GameMaterialState::kSolid, 0x909090u, 0, 0, 1},
    {"rutherfordium", kDustMetalWireBlockGenerationFlags, GameMaterialState::kSolid, 0x808080u, 0, 0, 1},
    {"dubnium", kDustMetalWireBlockGenerationFlags, GameMaterialState::kSolid, 0x808080u, 0, 0, 1},
    {"seaborgium", kDustMetalWireBlockGenerationFlags, GameMaterialState::kSolid, 0x808080u, 0, 0, 1},
    {"bohrium", kDustMetalWireBlockGenerationFlags, GameMaterialState::kSolid, 0x808080u, 0, 0, 1},
    {"hassium", kDustMetalWireBlockGenerationFlags, GameMaterialState::kSolid, 0x808080u, 0, 0, 1},
    {"meitnerium", kDustMetalWireBlockGenerationFlags, GameMaterialState::kSolid, 0x808080u, 0, 0, 1},
    {"darmstadtium", kDustMetalWireBlockGenerationFlags, GameMaterialState::kSolid, 0x808080u, 0, 0, 1},
    {"roentgenium", kDustMetalWireBlockGenerationFlags, GameMaterialState::kSolid, 0x808080u, 0, 0, 1},
    {"copernicium", kDustMetalWireBlockGenerationFlags, GameMaterialState::kSolid, 0xc0c0c0u, 0, 0, 1},
    {"nihonium", kDustMetalWireBlockGenerationFlags, GameMaterialState::kSolid, 0x808080u, 0, 0, 1},
    {"flerovium", kDustMetalWireBlockGenerationFlags, GameMaterialState::kSolid, 0x808080u, 0, 0, 1},
    {"moscovium", kDustMetalWireBlockGenerationFlags, GameMaterialState::kSolid, 0x808080u, 0, 0, 1},
    {"livermorium", kDustMetalWireBlockGenerationFlags, GameMaterialState::kSolid, 0x808080u, 0, 0, 1},
    {"tennessine", kDustGenerationFlags, GameMaterialState::kSolid, 0x808080u, 0, 0, 1},
    {"oganesson", kCellGenerationFlags, GameMaterialState::kGas, 0xc0c0c0u, 0, 0, 1},
}};

std::string elemental_formula(const ElementDefinition& element, uint8_t atom_count) {
    std::string formula(element.symbol);
    if (atom_count > 1) formula += std::to_string(atom_count);
    return formula;
}

}  // namespace

snt::core::Expected<void> register_builtin_element_materials(
    GameContentRegistry& registry) {
    const std::span<const ElementDefinition> elements = ElementCatalog::all();
    if (elements.size() != kElementMaterialSpecs.size()) {
        return snt::core::Error{
            snt::core::ErrorCode::kInvalidState,
            "ElementCatalog and built-in element material catalog have different sizes"};
    }

    std::set<std::string_view, std::less<>> material_ids;
    std::vector<GameMaterialDefinition> definitions;
    definitions.reserve(kElementMaterialSpecs.size());
    for (size_t index = 0; index < kElementMaterialSpecs.size(); ++index) {
        const ElementMaterialSpec& spec = kElementMaterialSpecs[index];
        const ElementDefinition& element = elements[index];
        if (spec.material_id.empty() || spec.atoms_per_formula_unit == 0 ||
            !material_ids.emplace(spec.material_id).second) {
            return snt::core::Error{
                snt::core::ErrorCode::kInvalidState,
                "Built-in element material catalog contains an invalid material specification"};
        }

        GameMaterialDefinition definition{
            .id = std::string(spec.material_id),
            .title_key = "material." + std::string(spec.material_id),
            .generation_flags = spec.generation_flags,
            .state = spec.state,
            .color_rgb = spec.color_rgb,
            .melting_point_kelvin = spec.melting_point_kelvin,
            .boiling_point_kelvin = spec.boiling_point_kelvin,
            .mass = static_cast<float>(element.relative_atomic_mass *
                                       spec.atoms_per_formula_unit),
            .chemical_formula = elemental_formula(element, spec.atoms_per_formula_unit),
        };
        definition.composition.push_back(GameMaterialElement{
            .element = element.id,
            .count = spec.atoms_per_formula_unit,
        });
        definitions.push_back(std::move(definition));
    }

    if (auto result = registry.register_builtin_materials(definitions); !result) {
        auto error = result.error();
        error.with_context("Could not register immutable periodic-table materials");
        return error;
    }
    return {};
}

bool is_builtin_element_material_id(std::string_view id) noexcept {
    for (const ElementMaterialSpec& spec : kElementMaterialSpecs) {
        if (spec.material_id == id) return true;
    }
    return false;
}

}  // namespace snt::game::chemistry
