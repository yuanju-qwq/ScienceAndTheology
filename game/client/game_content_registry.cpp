// ScienceAndTheology script-content registry implementation.

#define SNT_LOG_CHANNEL "gameplay"
#include "game_content_registry.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>
#include <set>
#include <string>
#include <string_view>
#include <utility>

#include <angelscript.h>

#include "core/error.h"
#include "core/log.h"
#include "game/chemistry/builtin_element_material_catalog.h"
#include "game/chemistry/element_catalog.h"
#include "game/simulation/worldgen_script_content.h"

namespace snt::game {
namespace {

thread_local GameContentRegistry* g_active_registry = nullptr;
thread_local ScriptId g_active_script_id = kBuiltinScriptId;

snt::core::Expected<void> invalid_argument(std::string message) {
    return snt::core::Error{snt::core::ErrorCode::kInvalidArgument, std::move(message)};
}

snt::core::Expected<void> invalid_state(std::string message) {
    return snt::core::Error{snt::core::ErrorCode::kInvalidState, std::move(message)};
}

void report_binding_error(const snt::core::Error& error) {
    SNT_LOG_ERROR("Game script API rejected registration: %s", error.format().c_str());
    if (asIScriptContext* context = asGetActiveContext()) {
        context->SetException(error.format().c_str());
    }
}

GameContentRegistry* active_registry() {
    if (g_active_registry && g_active_script_id != kBuiltinScriptId) {
        return g_active_registry;
    }
    report_binding_error(snt::core::Error{
        snt::core::ErrorCode::kInvalidState,
        "Gameplay registration was called outside snt_register()"});
    return nullptr;
}

std::optional<QuestObjectiveKind> parse_quest_objective_kind(std::string_view value) {
    if (value == "acquire_item") return QuestObjectiveKind::kAcquireItem;
    if (value == "craft_item") return QuestObjectiveKind::kCraftItem;
    if (value == "mine_block") return QuestObjectiveKind::kMineBlock;
    if (value == "place_machine") return QuestObjectiveKind::kPlaceMachine;
    if (value == "reach_tick") return QuestObjectiveKind::kReachTick;
    if (value == "custom") return QuestObjectiveKind::kCustomEvent;
    return std::nullopt;
}

constexpr uint64_t kQuestBookFingerprintOffset = 1469598103934665603ull;
constexpr uint64_t kQuestBookFingerprintPrime = 1099511628211ull;
constexpr size_t kMaxGameItemKeyBytes = 256;
constexpr int32_t kMaxGameItemStackSize = 1'000'000;
constexpr size_t kMaxMaterialCompositionEntries = 32;
constexpr size_t kMaxItemToolTags = 16;
constexpr uint16_t kKnownMaterialGenerationFlags =
    static_cast<uint16_t>(GameMaterialGenerationFlag::kDust) |
    static_cast<uint16_t>(GameMaterialGenerationFlag::kMetal) |
    static_cast<uint16_t>(GameMaterialGenerationFlag::kGem) |
    static_cast<uint16_t>(GameMaterialGenerationFlag::kOre) |
    static_cast<uint16_t>(GameMaterialGenerationFlag::kCell) |
    static_cast<uint16_t>(GameMaterialGenerationFlag::kPlasma) |
    static_cast<uint16_t>(GameMaterialGenerationFlag::kWire) |
    static_cast<uint16_t>(GameMaterialGenerationFlag::kBlock) |
    static_cast<uint16_t>(GameMaterialGenerationFlag::kPlate) |
    static_cast<uint16_t>(GameMaterialGenerationFlag::kRod);

constexpr std::array<std::string_view,
                     static_cast<size_t>(GameMaterialForm::kCount)>
    kMaterialFormNames = {
        "dust", "tiny_dust", "small_dust", "impure_dust", "purified_dust",
        "crushed", "crushed_purified", "crushed_centrifuged",
        "gem", "flawed_gem", "flawless_gem", "exquisite_gem",
        "ingot", "ingot_hot", "nugget", "block",
        "plate", "double_plate", "dense_plate",
        "rod", "long_rod", "bolt", "screw", "ring", "rotor", "gear",
        "small_gear", "wire_fine", "wire", "cell", "plasma_cell",
    };

constexpr std::array<int64_t, static_cast<size_t>(GameMaterialForm::kCount)>
    kMaterialFormAmounts = {
        144, 16, 36, 144, 144, 144, 144, 144,
        144, 144, 144, 144, 144, 144, 16, 1296,
        144, 288, 1296, 72, 144, 8, 8, 36, 576, 576,
        72, 72, 144, 144, 144,
    };

constexpr size_t material_form_index(GameMaterialForm form) noexcept {
    return static_cast<size_t>(form);
}

constexpr bool valid_material_form(GameMaterialForm form) noexcept {
    return material_form_index(form) < material_form_index(GameMaterialForm::kCount);
}

constexpr std::string_view material_form_name(GameMaterialForm form) noexcept {
    return valid_material_form(form) ? kMaterialFormNames[material_form_index(form)]
                                     : std::string_view{};
}

constexpr int64_t material_form_amount(GameMaterialForm form) noexcept {
    return valid_material_form(form) ? kMaterialFormAmounts[material_form_index(form)] : 0;
}

constexpr bool valid_material_state(GameMaterialState state) noexcept {
    return state == GameMaterialState::kSolid || state == GameMaterialState::kLiquid ||
        state == GameMaterialState::kGas || state == GameMaterialState::kPlasma;
}

constexpr bool valid_item_category(GameItemCategory category) noexcept {
    return static_cast<uint8_t>(category) <= static_cast<uint8_t>(GameItemCategory::kMisc);
}

constexpr bool valid_tool_type(GameToolType type) noexcept {
    return static_cast<uint8_t>(type) <= static_cast<uint8_t>(GameToolType::kSpear);
}

constexpr std::string_view tool_type_tag(GameToolType type) noexcept {
    switch (type) {
    case GameToolType::kPickaxe: return "pickaxe";
    case GameToolType::kAxe: return "axe";
    case GameToolType::kShovel: return "shovel";
    case GameToolType::kSword: return "sword";
    case GameToolType::kHoe: return "hoe";
    case GameToolType::kKnife: return "knife";
    case GameToolType::kSpear: return "spear";
    case GameToolType::kNone: return {};
    }
    return {};
}

constexpr bool material_generates_form(const GameMaterialDefinition& material,
                                       GameMaterialForm form) noexcept {
    switch (form) {
    case GameMaterialForm::kDust:
    case GameMaterialForm::kTinyDust:
    case GameMaterialForm::kSmallDust:
        return has_material_generation_flag(material.generation_flags,
                                            GameMaterialGenerationFlag::kDust);
    case GameMaterialForm::kGem:
    case GameMaterialForm::kFlawedGem:
    case GameMaterialForm::kFlawlessGem:
    case GameMaterialForm::kExquisiteGem:
        return has_material_generation_flag(material.generation_flags,
                                            GameMaterialGenerationFlag::kGem);
    case GameMaterialForm::kImpureDust:
    case GameMaterialForm::kPurifiedDust:
    case GameMaterialForm::kCrushed:
    case GameMaterialForm::kCrushedPurified:
    case GameMaterialForm::kCrushedCentrifuged:
        return has_material_generation_flag(material.generation_flags,
                                            GameMaterialGenerationFlag::kOre);
    case GameMaterialForm::kIngot:
    case GameMaterialForm::kHotIngot:
    case GameMaterialForm::kNugget:
        return has_material_generation_flag(material.generation_flags,
                                            GameMaterialGenerationFlag::kMetal);
    case GameMaterialForm::kPlate:
    case GameMaterialForm::kDoublePlate:
    case GameMaterialForm::kDensePlate:
        return has_material_generation_flag(material.generation_flags,
                                            GameMaterialGenerationFlag::kMetal) ||
            has_material_generation_flag(material.generation_flags,
                                         GameMaterialGenerationFlag::kPlate);
    case GameMaterialForm::kRod:
    case GameMaterialForm::kLongRod:
        return has_material_generation_flag(material.generation_flags,
                                            GameMaterialGenerationFlag::kMetal) ||
            has_material_generation_flag(material.generation_flags,
                                         GameMaterialGenerationFlag::kRod);
    case GameMaterialForm::kBolt:
    case GameMaterialForm::kScrew:
    case GameMaterialForm::kRing:
    case GameMaterialForm::kRotor:
    case GameMaterialForm::kGear:
    case GameMaterialForm::kSmallGear:
        return has_material_generation_flag(material.generation_flags,
                                            GameMaterialGenerationFlag::kMetal);
    case GameMaterialForm::kFineWire:
    case GameMaterialForm::kWire:
        return has_material_generation_flag(material.generation_flags,
                                            GameMaterialGenerationFlag::kWire);
    case GameMaterialForm::kBlock:
        return has_material_generation_flag(material.generation_flags,
                                            GameMaterialGenerationFlag::kBlock);
    case GameMaterialForm::kCell:
        return has_material_generation_flag(material.generation_flags,
                                            GameMaterialGenerationFlag::kCell);
    case GameMaterialForm::kPlasmaCell:
        return has_material_generation_flag(material.generation_flags,
                                            GameMaterialGenerationFlag::kPlasma);
    case GameMaterialForm::kCount:
        return false;
    }
    return false;
}

uint32_t darken_rgb(uint32_t color, uint32_t numerator = 9,
                    uint32_t denominator = 10) noexcept {
    const auto scale = [numerator, denominator](uint32_t channel) {
        return (channel * numerator) / denominator;
    };
    return (scale((color >> 16) & 0xffu) << 16) |
        (scale((color >> 8) & 0xffu) << 8) | scale(color & 0xffu);
}

std::string material_form_icon_path(GameMaterialForm form) {
    switch (form) {
    case GameMaterialForm::kDust:
    case GameMaterialForm::kImpureDust:
    case GameMaterialForm::kPurifiedDust:
        return "material_sets/generic/dust_base_32.png";
    case GameMaterialForm::kTinyDust:
        return "material_sets/generic/dust_tiny_base_32.png";
    case GameMaterialForm::kSmallDust:
        return "material_sets/generic/dust_pile_base_32.png";
    case GameMaterialForm::kCrushed:
    case GameMaterialForm::kCrushedPurified:
    case GameMaterialForm::kCrushedCentrifuged:
        return "material_sets/generic/crushed_base_32.png";
    case GameMaterialForm::kGem:
    case GameMaterialForm::kFlawedGem:
    case GameMaterialForm::kFlawlessGem:
    case GameMaterialForm::kExquisiteGem:
        return "material_sets/generic/gem_base_32.png";
    case GameMaterialForm::kIngot:
    case GameMaterialForm::kHotIngot:
        return "material_sets/generic/ingot_base_32.png";
    case GameMaterialForm::kNugget:
        return "material_sets/generic/nugget_base_32.png";
    case GameMaterialForm::kBlock:
        return "material_sets/generic/block_base_32.png";
    case GameMaterialForm::kPlate:
    case GameMaterialForm::kDoublePlate:
        return "material_sets/generic/plate_base_32.png";
    case GameMaterialForm::kDensePlate:
        return "material_sets/generic/dense_plate_base_32.png";
    case GameMaterialForm::kRod:
    case GameMaterialForm::kLongRod:
        return "material_sets/generic/rod_base_32.png";
    case GameMaterialForm::kBolt:
        return "material_sets/generic/bolt_base_32.png";
    case GameMaterialForm::kScrew:
        return "material_sets/generic/screw_base_32.png";
    case GameMaterialForm::kRing:
        return "material_sets/generic/ring_base_32.png";
    case GameMaterialForm::kRotor:
    case GameMaterialForm::kGear:
    case GameMaterialForm::kSmallGear:
        return "material_sets/generic/gear_base_32.png";
    case GameMaterialForm::kFineWire:
    case GameMaterialForm::kWire:
        return "material_sets/generic/wire_base_32.png";
    case GameMaterialForm::kCell:
    case GameMaterialForm::kPlasmaCell:
        return "material_sets/generic/cell_base_32.png";
    case GameMaterialForm::kCount:
        return {};
    }
    return {};
}

GameItemPresentation make_generated_material_presentation(
    const GameMaterialDefinition& material, GameMaterialForm form) {
    GameItemPresentation presentation;
    presentation.category = GameItemCategory::kMaterials;
    presentation.icon_path = material_form_icon_path(form);
    presentation.icon_overlay_path =
        (form == GameMaterialForm::kIngot || form == GameMaterialForm::kHotIngot)
        ? "material_sets/generic/ingot_overlay_32.png" : "";
    presentation.tint_rgb =
        (form == GameMaterialForm::kDust || form == GameMaterialForm::kTinyDust ||
         form == GameMaterialForm::kSmallDust || form == GameMaterialForm::kImpureDust ||
         form == GameMaterialForm::kPurifiedDust || form == GameMaterialForm::kCrushed ||
         form == GameMaterialForm::kCrushedPurified ||
         form == GameMaterialForm::kCrushedCentrifuged)
        ? darken_rgb(material.color_rgb) : material.color_rgb;
    presentation.uses_tint = true;
    return presentation;
}

bool valid_relative_item_asset_path(std::string_view path) noexcept {
    if (path.empty()) return true;
    return path.size() <= kMaxGameItemKeyBytes && path.find('\0') == std::string_view::npos &&
        !path.starts_with('/') && path.find('\\') == std::string_view::npos &&
        path.find("..") == std::string_view::npos && path.ends_with(".png");
}

void hash_byte(uint64_t& hash, uint8_t value) noexcept {
    hash ^= value;
    hash *= kQuestBookFingerprintPrime;
}

void hash_u32(uint64_t& hash, uint32_t value) noexcept {
    for (uint32_t shift = 0; shift < 32; shift += 8) {
        hash_byte(hash, static_cast<uint8_t>((value >> shift) & 0xffu));
    }
}

void hash_u64(uint64_t& hash, uint64_t value) noexcept {
    for (uint32_t shift = 0; shift < 64; shift += 8) {
        hash_byte(hash, static_cast<uint8_t>((value >> shift) & 0xffu));
    }
}

void hash_string(uint64_t& hash, std::string_view value) noexcept {
    hash_u64(hash, static_cast<uint64_t>(value.size()));
    for (const unsigned char byte : value) hash_byte(hash, byte);
}

void api_register_item(const std::string& id,
                       const std::string& title_key,
                       int max_stack) {
    GameContentRegistry* registry = active_registry();
    if (!registry) return;

    GameItemDefinition definition;
    definition.id = id;
    definition.title_key = title_key;
    definition.max_stack = max_stack;
    if (auto result = registry->register_script_item(
            g_active_script_id, std::move(definition)); !result) {
        report_binding_error(result.error());
    }
}

void api_register_material(const std::string& id,
                           const std::string& title_key,
                           int generation_flags,
                           int state,
                           int color_rgb,
                           int melting_point_kelvin,
                           int boiling_point_kelvin,
                           float mass,
                           const std::string& chemical_formula) {
    GameContentRegistry* registry = active_registry();
    if (!registry) return;
    if (generation_flags < 0 ||
        generation_flags > std::numeric_limits<uint16_t>::max() || state < 0 ||
        state > static_cast<int>(GameMaterialState::kPlasma) || color_rgb < 0 ||
        color_rgb > 0xffffff ||
        melting_point_kelvin < 0 || boiling_point_kelvin < 0) {
        report_binding_error(snt::core::Error{
            snt::core::ErrorCode::kInvalidArgument,
            "Material registration integers must not be negative"});
        return;
    }

    GameMaterialDefinition definition;
    definition.id = id;
    definition.title_key = title_key;
    definition.generation_flags = static_cast<uint16_t>(generation_flags);
    definition.state = static_cast<GameMaterialState>(state);
    definition.color_rgb = static_cast<uint32_t>(color_rgb);
    definition.melting_point_kelvin = melting_point_kelvin;
    definition.boiling_point_kelvin = boiling_point_kelvin;
    definition.mass = mass;
    definition.chemical_formula = chemical_formula;
    if (auto result = registry->register_script_material(
            g_active_script_id, std::move(definition)); !result) {
        report_binding_error(result.error());
    }
}

void api_add_material_element(const std::string& material_id,
                              const std::string& symbol,
                              int count) {
    GameContentRegistry* registry = active_registry();
    if (!registry) return;
    if (count <= 0 || count > std::numeric_limits<uint8_t>::max()) {
        report_binding_error(snt::core::Error{
            snt::core::ErrorCode::kInvalidArgument,
            "Material element count must fit in one positive byte"});
        return;
    }
    const chemistry::ElementDefinition* canonical_element =
        chemistry::ElementCatalog::find_by_symbol(symbol);
    if (!canonical_element) {
        report_binding_error(snt::core::Error{
            snt::core::ErrorCode::kInvalidArgument,
            "Material element symbol '" + symbol +
                "' must be an exact canonical periodic-table symbol"});
        return;
    }
    if (auto result = registry->add_script_material_element(
            g_active_script_id, material_id,
            {.element = canonical_element->id, .count = static_cast<uint8_t>(count)}); !result) {
        report_binding_error(result.error());
    }
}

void api_set_material_form_presentation(const std::string& material_id,
                                        int form,
                                        const std::string& title_key,
                                        int max_stack,
                                        const std::string& icon_path,
                                        const std::string& icon_overlay_path,
                                        int tint_rgb,
                                        bool uses_tint) {
    GameContentRegistry* registry = active_registry();
    if (!registry) return;
    if (form < 0 || form >= static_cast<int>(GameMaterialForm::kCount) ||
        max_stack <= 0 || tint_rgb < 0 || tint_rgb > 0xffffff) {
        report_binding_error(snt::core::Error{
            snt::core::ErrorCode::kInvalidArgument,
            "Material form presentation contains an invalid integer"});
        return;
    }
    GameMaterialFormPresentation presentation;
    presentation.title_key = title_key;
    presentation.max_stack = max_stack;
    presentation.presentation.category = GameItemCategory::kMaterials;
    presentation.presentation.icon_path = icon_path;
    presentation.presentation.icon_overlay_path = icon_overlay_path;
    presentation.presentation.tint_rgb = static_cast<uint32_t>(tint_rgb);
    presentation.presentation.uses_tint = uses_tint;
    if (auto result = registry->set_script_material_form_presentation(
            g_active_script_id, material_id, static_cast<GameMaterialForm>(form),
            std::move(presentation)); !result) {
        report_binding_error(result.error());
    }
}

void api_set_item_presentation(const std::string& item_id,
                               int category,
                               const std::string& icon_path,
                               const std::string& icon_overlay_path,
                               int tint_rgb,
                               bool uses_tint) {
    GameContentRegistry* registry = active_registry();
    if (!registry) return;
    if (category < 0 || category > static_cast<int>(GameItemCategory::kMisc) ||
        tint_rgb < 0 || tint_rgb > 0xffffff) {
        report_binding_error(snt::core::Error{
            snt::core::ErrorCode::kInvalidArgument,
            "Item presentation contains an invalid integer"});
        return;
    }
    GameItemPresentation presentation;
    presentation.category = static_cast<GameItemCategory>(category);
    presentation.icon_path = icon_path;
    presentation.icon_overlay_path = icon_overlay_path;
    presentation.tint_rgb = static_cast<uint32_t>(tint_rgb);
    presentation.uses_tint = uses_tint;
    if (auto result = registry->set_script_item_presentation(
            g_active_script_id, item_id, std::move(presentation)); !result) {
        report_binding_error(result.error());
    }
}

void api_set_item_tool(const std::string& item_id,
                       int type,
                       int mining_level,
                       const std::string& material_key,
                       float speed,
                       int durability,
                       float attack_damage) {
    GameContentRegistry* registry = active_registry();
    if (!registry) return;
    if (type < 0 || type > static_cast<int>(GameToolType::kSpear) ||
        mining_level < 0 || durability < 0) {
        report_binding_error(snt::core::Error{
            snt::core::ErrorCode::kInvalidArgument,
            "Item tool registration contains an invalid integer"});
        return;
    }
    GameToolDefinition definition;
    definition.type = static_cast<GameToolType>(type);
    definition.mining_level = mining_level;
    definition.material_key = material_key;
    definition.speed = speed;
    definition.durability = durability;
    definition.attack_damage = attack_damage;
    if (auto result = registry->set_script_item_tool(
            g_active_script_id, item_id, std::move(definition)); !result) {
        report_binding_error(result.error());
    }
}

void api_add_item_tool_tag(const std::string& item_id, const std::string& tool_tag) {
    GameContentRegistry* registry = active_registry();
    if (!registry) return;
    if (auto result = registry->add_script_item_tool_tag(
            g_active_script_id, item_id, tool_tag); !result) {
        report_binding_error(result.error());
    }
}

void api_register_recipe(const std::string& id,
                         const std::string& machine_id,
                         const std::string& first_input_item_id,
                         int first_input_count,
                         const std::string& output_item_id,
                         int output_count,
                         int duration_ticks,
                         int energy_per_tick,
                         const std::string& tag) {
    GameContentRegistry* registry = active_registry();
    if (!registry) return;

    RecipeDefinition definition;
    definition.id = id;
    definition.machine_id = machine_id;
    definition.inputs = {
        RecipeInputDefinition{first_input_item_id, first_input_count}};
    definition.outputs = {RecipeOutputDefinition{output_item_id, output_count}};
    definition.duration_ticks = duration_ticks;
    definition.energy_per_tick = energy_per_tick;
    definition.tag = tag;
    if (auto result = registry->register_script_recipe(g_active_script_id, std::move(definition)); !result) {
        report_binding_error(result.error());
    }
}

void api_add_recipe_input(const std::string& recipe_id,
                          const std::string& item_id,
                          int count) {
    GameContentRegistry* registry = active_registry();
    if (!registry) return;

    if (auto result = registry->add_script_recipe_input(
            g_active_script_id, recipe_id, RecipeInputDefinition{item_id, count}); !result) {
        report_binding_error(result.error());
    }
}

void api_register_machine(const std::string& id,
                          const std::string& display_name,
                          int tier,
                          int power_capacity,
                          bool requires_manual_activation) {
    GameContentRegistry* registry = active_registry();
    if (!registry) return;

    MachineDefinition definition;
    definition.id = id;
    definition.display_name = display_name;
    definition.tier = tier;
    definition.power_capacity = power_capacity;
    definition.requires_manual_activation = requires_manual_activation;
    if (auto result = registry->register_script_machine(g_active_script_id, std::move(definition)); !result) {
        report_binding_error(result.error());
    }
}

void api_register_machine_placement(const std::string& item_id,
                                    const std::string& machine_id,
                                    const std::string& material_key) {
    GameContentRegistry* registry = active_registry();
    if (!registry) return;
    if (auto result = registry->register_script_machine_placement(
            g_active_script_id,
            {.item_id = item_id,
             .machine_id = machine_id,
             .material_key = material_key});
        !result) {
        report_binding_error(result.error());
    }
}

void api_set_machine_activation_requirements(
    const std::string& machine_id,
    bool requires_cover,
    bool requires_ignition,
    bool requires_valid_structure,
    const std::string& required_tool_tag) {
    GameContentRegistry* registry = active_registry();
    if (!registry) return;

    MachineActivationRequirements requirements;
    requirements.requires_cover = requires_cover;
    requirements.requires_ignition = requires_ignition;
    requirements.requires_valid_structure = requires_valid_structure;
    requirements.required_tool_tag = required_tool_tag;
    if (auto result = registry->set_script_machine_activation_requirements(
            g_active_script_id, machine_id, std::move(requirements)); !result) {
        report_binding_error(result.error());
    }
}

void api_set_machine_offline_simulation(
    const std::string& machine_id,
    int mode,
    int max_batch_ticks,
    bool can_start_new_jobs) {
    GameContentRegistry* registry = active_registry();
    if (!registry) return;

    MachineOfflineSimulationProfile profile;
    profile.mode = static_cast<MachineOfflineSimulationMode>(mode);
    profile.max_batch_ticks = max_batch_ticks > 0
        ? static_cast<uint32_t>(max_batch_ticks)
        : 0;
    profile.can_start_new_jobs = can_start_new_jobs;
    if (auto result = registry->set_script_machine_offline_simulation(
            g_active_script_id, machine_id, std::move(profile)); !result) {
        report_binding_error(result.error());
    }
}

void api_set_machine_offline_power_transfer(
    const std::string& machine_id,
    int max_import_per_tick,
    int max_export_per_tick) {
    GameContentRegistry* registry = active_registry();
    if (!registry) return;
    if (auto result = registry->set_script_machine_offline_power_transfer(
            g_active_script_id, machine_id, max_import_per_tick,
            max_export_per_tick); !result) {
        report_binding_error(result.error());
    }
}

void api_register_quest_chapter(const std::string& id,
                                const std::string& title,
                                const std::string& description,
                                const std::string& icon_key,
                                int sort_order) {
    GameContentRegistry* registry = active_registry();
    if (!registry) return;

    QuestBookChapterDefinition definition;
    definition.id = id;
    definition.title = title;
    definition.description = description;
    definition.icon_key = icon_key;
    definition.sort_order = sort_order;
    if (auto result = registry->register_script_quest_chapter(
            g_active_script_id, std::move(definition)); !result) {
        report_binding_error(result.error());
    }
}

void api_register_quest(const std::string& id,
                        const std::string& chapter_id,
                        const std::string& title,
                        const std::string& description,
                        float node_x,
                        float node_y,
                        const std::string& icon_key,
                        bool hidden,
                        bool repeatable) {
    GameContentRegistry* registry = active_registry();
    if (!registry) return;

    QuestDefinition definition;
    definition.id = id;
    definition.chapter_id = chapter_id;
    definition.title = title;
    definition.description = description;
    definition.icon_key = icon_key;
    definition.node_position = {node_x, node_y};
    definition.hidden = hidden;
    definition.repeatable = repeatable;
    if (auto result = registry->register_script_quest(g_active_script_id, std::move(definition)); !result) {
        report_binding_error(result.error());
    }
}

void api_add_quest_prerequisite(const std::string& quest_id,
                                const std::string& prerequisite_id) {
    GameContentRegistry* registry = active_registry();
    if (!registry) return;

    if (auto result = registry->add_script_quest_prerequisite(
            g_active_script_id, quest_id, prerequisite_id); !result) {
        report_binding_error(result.error());
    }
}

void api_add_quest_objective(const std::string& quest_id,
                             const std::string& objective_id,
                             const std::string& objective_kind,
                             const std::string& target_id,
                             int required_count) {
    GameContentRegistry* registry = active_registry();
    if (!registry) return;

    const auto kind = parse_quest_objective_kind(objective_kind);
    if (!kind) {
        report_binding_error(snt::core::Error{
            snt::core::ErrorCode::kInvalidArgument,
            "Unknown quest objective kind: " + objective_kind});
        return;
    }

    QuestObjectiveDefinition objective;
    objective.id = objective_id;
    objective.kind = *kind;
    objective.target_id = target_id;
    objective.required_count = required_count;
    if (auto result = registry->add_script_quest_objective(
            g_active_script_id, quest_id, std::move(objective)); !result) {
        report_binding_error(result.error());
    }
}

void api_add_quest_item_reward(const std::string& quest_id,
                               const std::string& item_id,
                               int count) {
    GameContentRegistry* registry = active_registry();
    if (!registry) return;

    if (auto result = registry->add_script_quest_reward(
            g_active_script_id, quest_id,
            {.kind = QuestRewardKind::kItem, .target_id = item_id, .count = count});
        !result) {
        report_binding_error(result.error());
    }
}

void api_add_quest_unlock_reward(const std::string& quest_id,
                                 const std::string& unlocked_quest_id) {
    GameContentRegistry* registry = active_registry();
    if (!registry) return;

    if (auto result = registry->add_script_quest_reward(
            g_active_script_id, quest_id,
            {.kind = QuestRewardKind::kUnlockQuest,
             .target_id = unlocked_quest_id,
             .count = 1});
        !result) {
        report_binding_error(result.error());
    }
}

void api_on(const std::string& event_name, const std::string& callback_id) {
    GameContentRegistry* registry = active_registry();
    if (!registry) return;

    if (auto result = registry->add_event_listener(
            EventListener{g_active_script_id, event_name, callback_id}); !result) {
        report_binding_error(result.error());
    }
}

void api_set_state(const std::string& key, const std::string& value) {
    GameContentRegistry* registry = active_registry();
    if (!registry) return;

    if (auto result = registry->set_state(g_active_script_id, key, value); !result) {
        report_binding_error(result.error());
    }
}

std::string api_get_state(const std::string& key) {
    GameContentRegistry* registry = active_registry();
    if (!registry) return {};
    return registry->get_state(g_active_script_id, key).value_or("");
}

void api_log(const std::string& message) {
    SNT_LOG_INFO("[AS] %s", message.c_str());
}

snt::core::Expected<void> register_function(asIScriptEngine* engine,
                                             const char* declaration,
                                             const asSFuncPtr& function) {
    const int result = engine->RegisterGlobalFunction(declaration, function, asCALL_CDECL);
    if (result >= 0) return {};
    return snt::core::Error{
        snt::core::ErrorCode::kScriptEngineInitFailed,
        std::string("RegisterGlobalFunction failed for '") + declaration + "': " +
            std::to_string(result)};
}

class GameContentRegistrationScope final : public snt::script::IScriptRegistrationScope {
public:
    GameContentRegistrationScope(GameContentRegistry& registry, ScriptId script_id)
        : previous_registry_(g_active_registry)
        , previous_script_id_(g_active_script_id) {
        g_active_registry = &registry;
        g_active_script_id = script_id;
    }

    ~GameContentRegistrationScope() override {
        g_active_registry = previous_registry_;
        g_active_script_id = previous_script_id_;
    }

private:
    GameContentRegistry* previous_registry_ = nullptr;
    ScriptId previous_script_id_ = kBuiltinScriptId;
};

}  // namespace

snt::core::Expected<void> GameContentRegistry::register_script_api(asIScriptEngine* engine) {
    if (!engine) {
        return snt::core::Error{snt::core::ErrorCode::kInvalidArgument,
                                "GameContentRegistry received a null script engine"};
    }
    if (auto result = chemistry::register_builtin_element_materials(*this); !result) {
        return result.error();
    }

    if (auto result = register_function(
            engine, "void snt_register_item(const string &in, const string &in, int)",
            asFUNCTION(api_register_item)); !result) return result;
    if (auto result = register_function(
            engine,
            "void snt_register_material(const string &in, const string &in, int, int, int, int, int, float, const string &in)",
            asFUNCTION(api_register_material)); !result) return result;
    if (auto result = register_function(
            engine, "void snt_add_material_element(const string &in, const string &in, int)",
            asFUNCTION(api_add_material_element)); !result) return result;
    if (auto result = register_function(
            engine,
            "void snt_set_material_form_presentation(const string &in, int, const string &in, int, const string &in, const string &in, int, bool)",
            asFUNCTION(api_set_material_form_presentation)); !result) return result;
    if (auto result = register_function(
            engine,
            "void snt_set_item_presentation(const string &in, int, const string &in, const string &in, int, bool)",
            asFUNCTION(api_set_item_presentation)); !result) return result;
    if (auto result = register_function(
            engine,
            "void snt_set_item_tool(const string &in, int, int, const string &in, float, int, float)",
            asFUNCTION(api_set_item_tool)); !result) return result;
    if (auto result = register_function(
            engine, "void snt_add_item_tool_tag(const string &in, const string &in)",
            asFUNCTION(api_add_item_tool_tag)); !result) return result;
    if (auto result = register_function(
            engine,
            "void snt_register_recipe(const string &in, const string &in, const string &in, int, const string &in, int, int, int, const string &in)",
            asFUNCTION(api_register_recipe)); !result) return result;
    if (auto result = register_function(
            engine,
            "void snt_add_recipe_input(const string &in, const string &in, int)",
            asFUNCTION(api_add_recipe_input)); !result) return result;
    if (auto result = register_function(
            engine,
            "void snt_register_machine(const string &in, const string &in, int, int, bool)",
            asFUNCTION(api_register_machine)); !result) return result;
    if (auto result = register_function(
            engine,
            "void snt_register_machine_placement(const string &in, const string &in, const string &in)",
            asFUNCTION(api_register_machine_placement)); !result) return result;
    if (auto result = register_function(
            engine,
            "void snt_set_machine_activation_requirements(const string &in, bool, bool, bool, const string &in)",
            asFUNCTION(api_set_machine_activation_requirements)); !result) return result;
    if (auto result = register_function(
            engine,
            "void snt_set_machine_offline_simulation(const string &in, int, int, bool)",
            asFUNCTION(api_set_machine_offline_simulation)); !result) return result;
    if (auto result = register_function(
            engine,
            "void snt_set_machine_offline_power_transfer(const string &in, int, int)",
            asFUNCTION(api_set_machine_offline_power_transfer)); !result) return result;
    if (auto result = register_function(
            engine,
            "void snt_register_quest_chapter(const string &in, const string &in, const string &in, const string &in, int)",
            asFUNCTION(api_register_quest_chapter)); !result) return result;
    if (auto result = register_function(
            engine,
            "void snt_register_quest(const string &in, const string &in, const string &in, const string &in, float, float, const string &in, bool, bool)",
            asFUNCTION(api_register_quest)); !result) return result;
    if (auto result = register_function(
            engine, "void snt_add_quest_prerequisite(const string &in, const string &in)",
            asFUNCTION(api_add_quest_prerequisite)); !result) return result;
    if (auto result = register_function(
            engine,
            "void snt_add_quest_objective(const string &in, const string &in, const string &in, const string &in, int)",
            asFUNCTION(api_add_quest_objective)); !result) return result;
    if (auto result = register_function(
            engine,
            "void snt_add_quest_item_reward(const string &in, const string &in, int)",
            asFUNCTION(api_add_quest_item_reward)); !result) return result;
    if (auto result = register_function(
            engine,
            "void snt_add_quest_unlock_reward(const string &in, const string &in)",
            asFUNCTION(api_add_quest_unlock_reward)); !result) return result;
    if (auto result = register_function(
            engine, "void snt_on(const string &in, const string &in)",
            asFUNCTION(api_on)); !result) return result;
    if (auto result = register_function(
            engine, "void snt_set_state(const string &in, const string &in)",
            asFUNCTION(api_set_state)); !result) return result;
    if (auto result = register_function(
            engine, "string snt_get_state(const string &in)",
            asFUNCTION(api_get_state)); !result) return result;
    if (auto result = register_function(
            engine, "void snt_log(const string &in)", asFUNCTION(api_log)); !result) return result;
    if (auto result = register_worldgen_script_api(engine); !result) return result;

    SNT_LOG_INFO("Registered ScienceAndTheology gameplay Script API");
    return {};
}

std::unique_ptr<snt::script::IScriptRegistrationScope>
GameContentRegistry::begin_registration(ScriptId script_id) {
    return std::make_unique<GameContentRegistrationScope>(*this, script_id);
}

snt::core::Expected<std::string> GameContentRegistry::normalize_item_key(
    std::string_view key) {
    if (key.empty() || key.size() > kMaxGameItemKeyBytes) {
        return snt::core::Error{snt::core::ErrorCode::kInvalidArgument,
                                "Game item id must contain 1 to 256 bytes"};
    }

    std::string normalized;
    normalized.reserve(key.size());
    for (const unsigned char byte : key) {
        if (byte >= 'A' && byte <= 'Z') {
            normalized.push_back(static_cast<char>(byte - 'A' + 'a'));
            continue;
        }
        const bool allowed =
            (byte >= 'a' && byte <= 'z') ||
            (byte >= '0' && byte <= '9') ||
            byte == '.' || byte == '_' || byte == ':' || byte == '-';
        if (!allowed) {
            return snt::core::Error{
                snt::core::ErrorCode::kInvalidArgument,
                "Game item id must use ASCII letters, digits, '.', '_', ':', or '-'"};
        }
        normalized.push_back(static_cast<char>(byte));
    }
    return normalized;
}

snt::core::Expected<void> GameContentRegistry::validate(
    const GameMaterialElement& element) {
    if (!chemistry::ElementCatalog::find(element.element) || element.count == 0) {
        return invalid_argument(
            "Material element must use a canonical atomic number and positive count");
    }
    return {};
}

snt::core::Expected<void> GameContentRegistry::validate(
    const GameItemPresentation& presentation) {
    if (!valid_item_category(presentation.category) || presentation.tint_rgb > 0xffffffu ||
        !valid_relative_item_asset_path(presentation.icon_path) ||
        !valid_relative_item_asset_path(presentation.icon_overlay_path)) {
        return invalid_argument("Game item presentation contains an invalid category, tint, or icon path");
    }
    if (presentation.uses_tint && presentation.icon_path.empty()) {
        return invalid_argument("Tinted game item presentation requires an icon path");
    }
    return {};
}

snt::core::Expected<void> GameContentRegistry::validate(
    const GameMaterialFormPresentation& presentation) {
    if (!presentation.title_key.empty() &&
        (presentation.title_key.size() > kMaxGameItemKeyBytes ||
         presentation.title_key.find('\0') != std::string::npos)) {
        return invalid_argument("Material-form title_key must contain at most 256 non-null bytes");
    }
    if (presentation.max_stack <= 0 || presentation.max_stack > kMaxGameItemStackSize ||
        presentation.presentation.category != GameItemCategory::kMaterials) {
        return invalid_argument("Material-form presentation has an invalid stack limit or category");
    }
    return validate(presentation.presentation);
}

snt::core::Expected<void> GameContentRegistry::validate(
    const GameMaterialDefinition& definition) {
    if (definition.id.empty() || definition.id.size() > kMaxGameItemKeyBytes ||
        definition.title_key.empty() || definition.title_key.size() > kMaxGameItemKeyBytes ||
        definition.title_key.find('\0') != std::string::npos ||
        !valid_material_state(definition.state) ||
        (definition.generation_flags & ~kKnownMaterialGenerationFlags) != 0 ||
        definition.color_rgb > 0xffffffu || definition.melting_point_kelvin < 0 ||
        definition.boiling_point_kelvin < 0 || !std::isfinite(definition.mass) ||
        definition.mass < 0.0f || definition.chemical_formula.empty() ||
        definition.chemical_formula.size() > kMaxGameItemKeyBytes ||
        definition.chemical_formula.find('\0') != std::string::npos ||
        definition.composition.size() > kMaxMaterialCompositionEntries) {
        return invalid_argument("Game material definition contains an invalid physical property");
    }
    const auto normalized = normalize_item_key(definition.id);
    if (!normalized) return normalized.error();
    if (*normalized != definition.id) {
        return invalid_argument("Game material id must be normalized before registration");
    }
    std::set<chemistry::ElementId> elements;
    for (const GameMaterialElement& element : definition.composition) {
        if (auto result = validate(element); !result) return result.error();
        if (!elements.insert(element.element).second) {
            return invalid_argument("Game material composition must not repeat an element");
        }
    }
    for (const auto& [form, presentation] : definition.form_presentations) {
        if (!valid_material_form(form) || !material_generates_form(definition, form)) {
            return invalid_argument("Game material form presentation refers to a form it does not generate");
        }
        if (auto result = validate(presentation); !result) return result.error();
    }
    return {};
}

snt::core::Expected<void> GameContentRegistry::validate(
    const GameToolDefinition& definition) {
    if (!valid_tool_type(definition.type) || definition.mining_level < 0 ||
        definition.mining_level > 1'000 || definition.material_key.size() > kMaxGameItemKeyBytes ||
        definition.material_key.find('\0') != std::string::npos ||
        !std::isfinite(definition.speed) || definition.speed < 0.0f ||
        definition.durability < 0 || !std::isfinite(definition.attack_damage) ||
        definition.attack_damage < 0.0f) {
        return invalid_argument("Game tool definition contains an invalid behavior value");
    }
    return {};
}

snt::core::Expected<void> GameContentRegistry::validate(
    const GameItemDefinition& definition) {
    if (definition.id.empty() || definition.id.size() > kMaxGameItemKeyBytes) {
        return invalid_argument("Game item id must contain 1 to 256 bytes");
    }
    if (definition.title_key.empty() || definition.title_key.size() > kMaxGameItemKeyBytes ||
        definition.title_key.find('\0') != std::string::npos) {
        return invalid_argument("Game item title_key must contain 1 to 256 non-null bytes");
    }
    if (definition.max_stack <= 0 || definition.max_stack > kMaxGameItemStackSize) {
        return invalid_argument("Game item max_stack must be within the supported range");
    }
    const auto normalized = normalize_item_key(definition.id);
    if (!normalized) return normalized.error();
    if (*normalized != definition.id) {
        return invalid_argument("Game item id must be normalized before registration");
    }
    if (auto result = validate(definition.presentation); !result) return result.error();
    if (definition.tool.has_value()) {
        if (auto result = validate(*definition.tool); !result) return result.error();
    }
    if (definition.tool_tags.size() > kMaxItemToolTags) {
        return invalid_argument("Game item has too many tool tags");
    }
    std::set<std::string, std::less<>> tags;
    for (const std::string& tag : definition.tool_tags) {
        const auto normalized_tag = normalize_item_key(tag);
        if (!normalized_tag || *normalized_tag != tag || !tags.insert(tag).second) {
            return invalid_argument("Game item tool tags must be unique normalized content keys");
        }
    }
    if (definition.material_form.has_value()) {
        const GameMaterialFormReference& form = *definition.material_form;
        const auto material_id = normalize_item_key(form.material_id);
        if (!material_id || *material_id != form.material_id || !valid_material_form(form.form) ||
            form.material_units <= 0) {
            return invalid_argument("Game item material-form reference is invalid");
        }
    }
    return {};
}

snt::core::Expected<void> GameContentRegistry::validate(const RecipeDefinition& definition) {
    if (definition.id.empty()) return invalid_argument("Recipe id must not be empty");
    if (definition.machine_id.empty()) return invalid_argument("Recipe machine_id must not be empty");
    if (definition.inputs.empty()) return invalid_argument("Recipe must have at least one input");
    if (definition.outputs.empty()) return invalid_argument("Recipe must have at least one output");
    if (definition.duration_ticks <= 0) return invalid_argument("Recipe duration_ticks must be positive");
    if (definition.energy_per_tick < 0) return invalid_argument("Recipe energy_per_tick must not be negative");
    std::set<std::string, std::less<>> input_ids;
    for (const RecipeInputDefinition& input : definition.inputs) {
        if (auto result = validate(input); !result) return result.error();
        if (!input_ids.emplace(input.item_id).second) {
            return invalid_argument("Recipe input item ids must be unique");
        }
    }
    for (const auto& output : definition.outputs) {
        if (output.item_id.empty() || output.count <= 0) {
            return invalid_argument("Recipe output item_id must not be empty and count must be positive");
        }
    }
    return {};
}

snt::core::Expected<void> GameContentRegistry::validate(const RecipeInputDefinition& input) {
    if (input.item_id.empty() || input.count <= 0) {
        return invalid_argument("Recipe input item_id must not be empty and count must be positive");
    }
    return {};
}

snt::core::Expected<void> GameContentRegistry::validate(
    const MachineActivationRequirements& requirements) {
    if (requirements.required_tool_tag.find('\0') != std::string::npos) {
        return invalid_argument("Machine activation tool tag must not contain a null character");
    }
    return {};
}

snt::core::Expected<void> GameContentRegistry::validate(const MachineDefinition& definition) {
    if (definition.id.empty()) return invalid_argument("Machine id must not be empty");
    if (definition.display_name.empty()) return invalid_argument("Machine display_name must not be empty");
    if (definition.tier <= 0) return invalid_argument("Machine tier must be positive");
    if (definition.power_capacity < 0) return invalid_argument("Machine power_capacity must not be negative");
    if (auto result = validate(definition.activation_requirements); !result) return result.error();
    if (!definition.requires_manual_activation && !definition.activation_requirements.empty()) {
        return invalid_argument(
            "Machine activation requirements require manual activation to be enabled");
    }
    const uint8_t offline_mode = static_cast<uint8_t>(definition.offline_simulation.mode);
    if (offline_mode > static_cast<uint8_t>(MachineOfflineSimulationMode::kNetworkIsland)) {
        return invalid_argument("Machine offline simulation mode is invalid");
    }
    if (definition.offline_simulation.mode != MachineOfflineSimulationMode::kDisabled &&
        (definition.offline_simulation.max_batch_ticks == 0 ||
         definition.offline_simulation.max_batch_ticks > 72000)) {
        return invalid_argument(
            "Offline machine simulation batch ticks must be between 1 and 72000");
    }
    if (definition.offline_simulation.max_power_import_per_tick < 0 ||
        definition.offline_simulation.max_power_export_per_tick < 0 ||
        definition.offline_simulation.max_power_import_per_tick > 1'000'000'000 ||
        definition.offline_simulation.max_power_export_per_tick > 1'000'000'000) {
        return invalid_argument("Offline machine power transfer limits are invalid");
    }
    if (definition.offline_simulation.mode != MachineOfflineSimulationMode::kNetworkIsland &&
        (definition.offline_simulation.max_power_import_per_tick != 0 ||
         definition.offline_simulation.max_power_export_per_tick != 0)) {
        return invalid_argument("Offline machine power transfer requires network-island mode");
    }
    return {};
}

snt::core::Expected<void> GameContentRegistry::validate(
    const QuestBookChapterDefinition& definition) {
    if (definition.id.empty()) return invalid_argument("Quest chapter id must not be empty");
    if (definition.title.empty()) return invalid_argument("Quest chapter title must not be empty");
    if (definition.id.find('\0') != std::string::npos ||
        definition.icon_key.find('\0') != std::string::npos) {
        return invalid_argument("Quest chapter identifiers must not contain a null character");
    }
    return {};
}

snt::core::Expected<void> GameContentRegistry::validate(const QuestDefinition& definition) {
    if (definition.id.empty()) return invalid_argument("Quest id must not be empty");
    if (definition.chapter_id.empty()) return invalid_argument("Quest chapter_id must not be empty");
    if (definition.title.empty()) return invalid_argument("Quest title must not be empty");
    if (!std::isfinite(definition.node_position.x) ||
        !std::isfinite(definition.node_position.y)) {
        return invalid_argument("Quest node position must be finite");
    }
    if (definition.id.find('\0') != std::string::npos ||
        definition.chapter_id.find('\0') != std::string::npos ||
        definition.icon_key.find('\0') != std::string::npos) {
        return invalid_argument("Quest identifiers must not contain a null character");
    }
    for (const std::string& prerequisite : definition.prerequisites) {
        if (prerequisite.empty()) return invalid_argument("Quest prerequisite id must not be empty");
        if (prerequisite == definition.id) {
            return invalid_argument("Quest cannot list itself as a prerequisite");
        }
        if (std::count(definition.prerequisites.begin(), definition.prerequisites.end(),
                       prerequisite) != 1) {
            return invalid_argument("Quest prerequisite ids must be unique within a quest");
        }
    }
    for (size_t index = 0; index < definition.objectives.size(); ++index) {
        if (auto result = validate(definition.objectives[index]); !result) return result.error();
        for (size_t prior = 0; prior < index; ++prior) {
            if (definition.objectives[prior].id == definition.objectives[index].id) {
                return invalid_argument("Quest objective ids must be unique within a quest");
            }
        }
    }
    for (const QuestRewardDefinition& reward : definition.rewards) {
        if (auto result = validate(reward); !result) return result.error();
        if (reward.kind == QuestRewardKind::kUnlockQuest && reward.target_id == definition.id) {
            return invalid_argument("Quest cannot unlock itself as a reward");
        }
    }
    return {};
}

snt::core::Expected<void> GameContentRegistry::validate(
    const QuestObjectiveDefinition& objective) {
    if (objective.id.empty()) return invalid_argument("Quest objective id must not be empty");
    if (objective.required_count <= 0) {
        return invalid_argument("Quest objective required_count must be positive");
    }
    switch (objective.kind) {
        case QuestObjectiveKind::kAcquireItem:
        case QuestObjectiveKind::kCraftItem:
        case QuestObjectiveKind::kMineBlock:
        case QuestObjectiveKind::kPlaceMachine:
        case QuestObjectiveKind::kReachTick:
        case QuestObjectiveKind::kCustomEvent:
            break;
        default:
            return invalid_argument("Quest objective kind is invalid");
    }
    if (objective.kind != QuestObjectiveKind::kReachTick && objective.target_id.empty()) {
        return invalid_argument("Quest objective target_id must not be empty");
    }
    return {};
}

snt::core::Expected<void> GameContentRegistry::validate(
    const QuestRewardDefinition& reward) {
    if (reward.target_id.empty()) return invalid_argument("Quest reward target_id must not be empty");
    if (reward.count <= 0) return invalid_argument("Quest reward count must be positive");
    switch (reward.kind) {
        case QuestRewardKind::kItem:
            break;
        case QuestRewardKind::kUnlockQuest:
            if (reward.count != 1) {
                return invalid_argument("Quest unlock reward count must be exactly one");
            }
            break;
        default:
            return invalid_argument("Quest reward kind is invalid");
    }
    return {};
}

snt::core::Expected<void> GameContentRegistry::validate(const EventListener& listener) {
    if (listener.script_id == kBuiltinScriptId) {
        return invalid_argument("Event listener must have a non-builtin ScriptId");
    }
    if (listener.event_name.empty()) return invalid_argument("Event name must not be empty");
    if (listener.callback_id.empty()) return invalid_argument("Event callback_id must not be empty");
    return {};
}

snt::core::Expected<void> GameContentRegistry::register_builtin_materials(
    std::span<const GameMaterialDefinition> definitions) {
    if (!reloads_.empty() || reload_batch_) {
        return invalid_state("Built-in material registration is not allowed during a script reload");
    }
    if (definitions.empty()) return {};

    MaterialMap previous_backup = backup_materials_;
    MaterialMap previous_live = live_materials_;
    ItemMap previous_generated = live_generated_material_items_;
    const auto previous_runtime_index = resource_runtime_index_.snapshot();
    const uint64_t previous_item_content_revision = item_content_revision_;
    const auto rollback = [&]() {
        backup_materials_ = previous_backup;
        live_materials_ = previous_live;
        live_generated_material_items_ = previous_generated;
        resource_runtime_index_.restore(previous_runtime_index);
        item_content_revision_ = previous_item_content_revision;
    };

    for (const GameMaterialDefinition& definition : definitions) {
        if (auto result = register_material(kBuiltinScriptId, definition, true); !result) {
            rollback();
            return result.error();
        }
    }
    if (auto result = rebuild_generated_material_items(); !result) {
        rollback();
        return result.error();
    }
    if (auto result = publish_resource_runtime_index(); !result) {
        rollback();
        return result.error();
    }
    SNT_LOG_INFO("Registered %zu immutable C++ material definition(s) in one batch",
                 definitions.size());
    return {};
}

snt::core::Expected<void> GameContentRegistry::register_builtin_item(
    GameItemDefinition definition) {
    if (!reloads_.empty()) {
        return invalid_state("Built-in item registration is not allowed during a script reload");
    }
    ItemMap previous_backup = backup_items_;
    ItemMap previous_live = live_items_;
    if (auto result = register_item(kBuiltinScriptId, std::move(definition), true); !result) {
        return result.error();
    }
    if (auto result = publish_resource_runtime_index(); !result) {
        backup_items_ = std::move(previous_backup);
        live_items_ = std::move(previous_live);
        return result.error();
    }
    return {};
}

snt::core::Expected<void> GameContentRegistry::register_builtin_recipe(RecipeDefinition definition) {
    return register_recipe(kBuiltinScriptId, std::move(definition), true);
}

snt::core::Expected<void> GameContentRegistry::register_builtin_machine(MachineDefinition definition) {
    return register_machine(kBuiltinScriptId, std::move(definition), true);
}

snt::core::Expected<void> GameContentRegistry::register_builtin_machine_placement(
    MachinePlacementDefinition definition) {
    return machine_placements_.register_builtin(std::move(definition));
}

snt::core::Expected<void> GameContentRegistry::register_builtin_quest_chapter(
    QuestBookChapterDefinition definition) {
    return register_quest_chapter(kBuiltinScriptId, std::move(definition), true);
}

snt::core::Expected<void> GameContentRegistry::register_builtin_quest(QuestDefinition definition) {
    return register_quest(kBuiltinScriptId, std::move(definition), true);
}

snt::core::Expected<void> GameContentRegistry::register_script_item(
    ScriptId script_id, GameItemDefinition definition) {
    const bool staged_reload = reloads_.contains(script_id);
    ItemMap previous_backup;
    ItemMap previous_live;
    if (!staged_reload) {
        previous_backup = backup_items_;
        previous_live = live_items_;
    }
    if (auto result = register_item(script_id, std::move(definition), false); !result) {
        return result.error();
    }
    if (!staged_reload) {
        if (auto result = publish_resource_runtime_index(); !result) {
            backup_items_ = std::move(previous_backup);
            live_items_ = std::move(previous_live);
            return result.error();
        }
    }
    return {};
}

snt::core::Expected<void> GameContentRegistry::register_script_material(
    ScriptId script_id, GameMaterialDefinition definition) {
    if (script_id == kBuiltinScriptId) {
        return invalid_argument("Material script registrations require a non-builtin ScriptId");
    }
    const auto normalized_id = normalize_item_key(definition.id);
    if (!normalized_id) return normalized_id.error();
    if (chemistry::is_builtin_element_material_id(*normalized_id)) {
        return invalid_state("Canonical periodic-table material cannot be overridden by a script: " +
                             *normalized_id);
    }
    definition.id = *normalized_id;
    const bool staged_reload = reloads_.contains(script_id);
    MaterialMap previous_backup;
    MaterialMap previous_live;
    ItemMap previous_generated;
    if (!staged_reload) {
        previous_backup = backup_materials_;
        previous_live = live_materials_;
        previous_generated = live_generated_material_items_;
    }
    if (auto result = register_material(script_id, std::move(definition), false); !result) {
        return result.error();
    }
    // ScriptLoader wraps every module activation in a reload transaction.
    // Rebuilding all generated forms per material turns a large catalog into
    // quadratic work, so the transaction publishes one complete form set in
    // commit_reload instead.
    if (staged_reload) return {};
    if (auto result = rebuild_generated_material_items(); !result) {
        backup_materials_ = std::move(previous_backup);
        live_materials_ = std::move(previous_live);
        live_generated_material_items_ = std::move(previous_generated);
        return result.error();
    }
    if (auto result = publish_resource_runtime_index(); !result) {
        backup_materials_ = std::move(previous_backup);
        live_materials_ = std::move(previous_live);
        live_generated_material_items_ = std::move(previous_generated);
        return result.error();
    }
    return {};
}

snt::core::Expected<void> GameContentRegistry::add_script_material_element(
    ScriptId script_id, std::string material_id, GameMaterialElement element) {
    if (script_id == kBuiltinScriptId) {
        return invalid_argument("Material script mutations require a non-builtin ScriptId");
    }
    if (auto result = validate(element); !result) return result.error();
    const auto normalized_id = normalize_item_key(material_id);
    if (!normalized_id) return normalized_id.error();
    const auto found = live_materials_.find(*normalized_id);
    if (found == live_materials_.end() || found->second.owner != script_id) {
        return invalid_state("Material composition can only modify a material owned by the active script");
    }
    const bool staged_reload = reloads_.contains(script_id);
    MaterialMap previous_live;
    if (!staged_reload) previous_live = live_materials_;

    auto& composition = found->second.definition.composition;
    if (composition.size() >= kMaxMaterialCompositionEntries ||
        std::any_of(composition.begin(), composition.end(), [&element](const auto& current) {
            return current.element == element.element;
        })) {
        return invalid_state("Material composition cannot repeat an element or exceed its entry limit");
    }
    composition.push_back(std::move(element));
    if (auto result = validate(found->second.definition); !result) {
        if (!staged_reload) live_materials_ = std::move(previous_live);
        return result.error();
    }
    if (!staged_reload) {
        if (auto result = publish_resource_runtime_index(); !result) {
            live_materials_ = std::move(previous_live);
            return result.error();
        }
    }
    return {};
}

snt::core::Expected<void> GameContentRegistry::set_script_material_form_presentation(
    ScriptId script_id, std::string material_id, GameMaterialForm form,
    GameMaterialFormPresentation presentation) {
    if (script_id == kBuiltinScriptId) {
        return invalid_argument("Material script mutations require a non-builtin ScriptId");
    }
    if (!valid_material_form(form)) return invalid_argument("Material form is invalid");
    if (auto result = validate(presentation); !result) return result.error();
    const auto normalized_id = normalize_item_key(material_id);
    if (!normalized_id) return normalized_id.error();
    const auto found = live_materials_.find(*normalized_id);
    if (found == live_materials_.end() || found->second.owner != script_id) {
        return invalid_state("Material-form presentation can only modify a material owned by the active script");
    }
    if (!material_generates_form(found->second.definition, form)) {
        return invalid_argument("Material-form presentation refers to a form the material does not generate");
    }
    const bool staged_reload = reloads_.contains(script_id);
    MaterialMap previous_live;
    ItemMap previous_generated;
    if (!staged_reload) {
        previous_live = live_materials_;
        previous_generated = live_generated_material_items_;
    }
    found->second.definition.form_presentations[form] = std::move(presentation);
    if (staged_reload) return {};
    if (auto result = rebuild_generated_material_items(); !result) {
        live_materials_ = std::move(previous_live);
        live_generated_material_items_ = std::move(previous_generated);
        return result.error();
    }
    if (auto result = publish_resource_runtime_index(); !result) {
        live_materials_ = std::move(previous_live);
        live_generated_material_items_ = std::move(previous_generated);
        return result.error();
    }
    return {};
}

snt::core::Expected<void> GameContentRegistry::set_script_item_presentation(
    ScriptId script_id, std::string item_id, GameItemPresentation presentation) {
    if (script_id == kBuiltinScriptId) {
        return invalid_argument("Item script mutations require a non-builtin ScriptId");
    }
    if (auto result = validate(presentation); !result) return result.error();
    const auto normalized_id = normalize_item_key(item_id);
    if (!normalized_id) return normalized_id.error();
    const auto found = live_items_.find(*normalized_id);
    if (found == live_items_.end() || found->second.owner != script_id) {
        return invalid_state("Item presentation can only modify an authored item owned by the active script");
    }
    const bool staged_reload = reloads_.contains(script_id);
    ItemMap previous_live;
    if (!staged_reload) previous_live = live_items_;
    found->second.definition.presentation = std::move(presentation);
    if (auto result = validate(found->second.definition); !result) {
        if (!staged_reload) live_items_ = std::move(previous_live);
        return result.error();
    }
    if (!staged_reload) {
        if (auto result = publish_resource_runtime_index(); !result) {
            live_items_ = std::move(previous_live);
            return result.error();
        }
    }
    return {};
}

snt::core::Expected<void> GameContentRegistry::set_script_item_tool(
    ScriptId script_id, std::string item_id, GameToolDefinition tool) {
    if (script_id == kBuiltinScriptId) {
        return invalid_argument("Item script mutations require a non-builtin ScriptId");
    }
    if (auto result = validate(tool); !result) return result.error();
    const auto normalized_id = normalize_item_key(item_id);
    if (!normalized_id) return normalized_id.error();
    const auto found = live_items_.find(*normalized_id);
    if (found == live_items_.end() || found->second.owner != script_id) {
        return invalid_state("Item tool behavior can only modify an authored item owned by the active script");
    }
    const bool staged_reload = reloads_.contains(script_id);
    ItemMap previous_live;
    if (!staged_reload) previous_live = live_items_;
    found->second.definition.tool = std::move(tool);
    if (auto result = validate(found->second.definition); !result) {
        if (!staged_reload) live_items_ = std::move(previous_live);
        return result.error();
    }
    if (!staged_reload) {
        if (auto result = publish_resource_runtime_index(); !result) {
            live_items_ = std::move(previous_live);
            return result.error();
        }
    }
    return {};
}

snt::core::Expected<void> GameContentRegistry::add_script_item_tool_tag(
    ScriptId script_id, std::string item_id, std::string tool_tag) {
    if (script_id == kBuiltinScriptId) {
        return invalid_argument("Item script mutations require a non-builtin ScriptId");
    }
    const auto normalized_id = normalize_item_key(item_id);
    if (!normalized_id) return normalized_id.error();
    const auto normalized_tag = normalize_item_key(tool_tag);
    if (!normalized_tag) return normalized_tag.error();
    const auto found = live_items_.find(*normalized_id);
    if (found == live_items_.end() || found->second.owner != script_id) {
        return invalid_state("Item tool tags can only modify an authored item owned by the active script");
    }
    const bool staged_reload = reloads_.contains(script_id);
    ItemMap previous_live;
    if (!staged_reload) previous_live = live_items_;
    auto& tags = found->second.definition.tool_tags;
    if (tags.size() >= kMaxItemToolTags ||
        std::find(tags.begin(), tags.end(), *normalized_tag) != tags.end()) {
        return invalid_state("Item tool tag is duplicate or exceeds the supported limit");
    }
    tags.push_back(std::move(*normalized_tag));
    if (!staged_reload) {
        if (auto result = publish_resource_runtime_index(); !result) {
            live_items_ = std::move(previous_live);
            return result.error();
        }
    }
    return {};
}

snt::core::Expected<void> GameContentRegistry::register_script_recipe(
    ScriptId script_id, RecipeDefinition definition) {
    return register_recipe(script_id, std::move(definition), false);
}

snt::core::Expected<void> GameContentRegistry::register_script_machine(
    ScriptId script_id, MachineDefinition definition) {
    return register_machine(script_id, std::move(definition), false);
}

snt::core::Expected<void> GameContentRegistry::register_script_machine_placement(
    ScriptId script_id, MachinePlacementDefinition definition) {
    return machine_placements_.register_script(script_id, std::move(definition));
}

snt::core::Expected<void> GameContentRegistry::register_script_quest_chapter(
    ScriptId script_id, QuestBookChapterDefinition definition) {
    return register_quest_chapter(script_id, std::move(definition), false);
}

snt::core::Expected<void> GameContentRegistry::register_script_quest(
    ScriptId script_id, QuestDefinition definition) {
    return register_quest(script_id, std::move(definition), false);
}

snt::core::Expected<void> GameContentRegistry::add_script_quest_prerequisite(
    ScriptId script_id, std::string quest_id, std::string prerequisite_id) {
    if (script_id == kBuiltinScriptId) {
        return invalid_argument("Quest script mutations require a non-builtin ScriptId");
    }
    if (quest_id.empty() || prerequisite_id.empty()) {
        return invalid_argument("Quest and prerequisite ids must not be empty");
    }
    if (quest_id == prerequisite_id) {
        return invalid_argument("Quest cannot list itself as a prerequisite");
    }

    const auto found = live_quests_.find(quest_id);
    if (found == live_quests_.end() || found->second.owner != script_id) {
        return invalid_state("Quest prerequisite can only modify a quest owned by the active script");
    }
    auto& prerequisites = found->second.definition.prerequisites;
    if (std::find(prerequisites.begin(), prerequisites.end(), prerequisite_id) != prerequisites.end()) {
        return invalid_state("Duplicate quest prerequisite: " + prerequisite_id);
    }
    prerequisites.push_back(std::move(prerequisite_id));
    ++quest_content_revision_;
    return {};
}

snt::core::Expected<void> GameContentRegistry::add_script_recipe_input(
    ScriptId script_id, std::string recipe_id, RecipeInputDefinition input) {
    if (script_id == kBuiltinScriptId) {
        return invalid_argument("Recipe script mutations require a non-builtin ScriptId");
    }
    if (auto result = validate(input); !result) return result.error();

    const auto found = live_recipes_.find(recipe_id);
    if (found == live_recipes_.end() || found->second.owner != script_id) {
        return invalid_state("Recipe input can only modify a recipe owned by the active script");
    }
    auto& inputs = found->second.definition.inputs;
    const auto duplicate = std::find_if(
        inputs.begin(), inputs.end(), [&input](const RecipeInputDefinition& current) {
            return current.item_id == input.item_id;
        });
    if (duplicate != inputs.end()) {
        return invalid_state("Duplicate recipe input item id: " + input.item_id);
    }
    inputs.push_back(std::move(input));
    return {};
}

snt::core::Expected<void> GameContentRegistry::set_script_machine_activation_requirements(
    ScriptId script_id,
    std::string machine_id,
    MachineActivationRequirements requirements) {
    if (script_id == kBuiltinScriptId) {
        return invalid_argument("Machine script mutations require a non-builtin ScriptId");
    }
    if (auto result = validate(requirements); !result) return result.error();

    const auto found = live_machines_.find(machine_id);
    if (found == live_machines_.end() || found->second.owner != script_id) {
        return invalid_state("Machine activation requirements can only modify a machine owned by the active script");
    }
    if (!found->second.definition.requires_manual_activation && !requirements.empty()) {
        return invalid_state(
            "Machine activation requirements require manual activation to be enabled");
    }
    found->second.definition.activation_requirements = std::move(requirements);
    return {};
}

snt::core::Expected<void> GameContentRegistry::set_script_machine_offline_simulation(
    ScriptId script_id,
    std::string machine_id,
    MachineOfflineSimulationProfile profile) {
    if (script_id == kBuiltinScriptId) {
        return invalid_argument("Machine script mutations require a non-builtin ScriptId");
    }

    const auto found = live_machines_.find(machine_id);
    if (found == live_machines_.end() || found->second.owner != script_id) {
        return invalid_state("Machine offline simulation can only modify a machine owned by the active script");
    }
    MachineDefinition candidate = found->second.definition;
    candidate.offline_simulation = std::move(profile);
    if (auto result = validate(candidate); !result) return result.error();
    found->second.definition.offline_simulation = std::move(candidate.offline_simulation);
    return {};
}

snt::core::Expected<void> GameContentRegistry::set_script_machine_offline_power_transfer(
    ScriptId script_id,
    std::string machine_id,
    int32_t max_import_per_tick,
    int32_t max_export_per_tick) {
    if (script_id == kBuiltinScriptId) {
        return invalid_argument("Machine script mutations require a non-builtin ScriptId");
    }
    const auto found = live_machines_.find(machine_id);
    if (found == live_machines_.end() || found->second.owner != script_id) {
        return invalid_state("Machine power transfer can only modify a machine owned by the active script");
    }
    MachineDefinition candidate = found->second.definition;
    candidate.offline_simulation.max_power_import_per_tick = max_import_per_tick;
    candidate.offline_simulation.max_power_export_per_tick = max_export_per_tick;
    if (auto result = validate(candidate); !result) return result.error();
    found->second.definition.offline_simulation = std::move(candidate.offline_simulation);
    return {};
}

snt::core::Expected<void> GameContentRegistry::add_script_quest_objective(
    ScriptId script_id, std::string quest_id, QuestObjectiveDefinition objective) {
    if (script_id == kBuiltinScriptId) {
        return invalid_argument("Quest script mutations require a non-builtin ScriptId");
    }
    if (auto result = validate(objective); !result) return result.error();

    const auto found = live_quests_.find(quest_id);
    if (found == live_quests_.end() || found->second.owner != script_id) {
        return invalid_state("Quest objective can only modify a quest owned by the active script");
    }
    auto& objectives = found->second.definition.objectives;
    const auto duplicate = std::find_if(
        objectives.begin(), objectives.end(), [&objective](const QuestObjectiveDefinition& current) {
            return current.id == objective.id;
        });
    if (duplicate != objectives.end()) {
        return invalid_state("Duplicate quest objective: " + objective.id);
    }
    objectives.push_back(std::move(objective));
    ++quest_content_revision_;
    return {};
}

snt::core::Expected<void> GameContentRegistry::add_script_quest_reward(
    ScriptId script_id, std::string quest_id, QuestRewardDefinition reward) {
    if (script_id == kBuiltinScriptId) {
        return invalid_argument("Quest script mutations require a non-builtin ScriptId");
    }
    if (auto result = validate(reward); !result) return result.error();

    const auto found = live_quests_.find(quest_id);
    if (found == live_quests_.end() || found->second.owner != script_id) {
        return invalid_state("Quest reward can only modify a quest owned by the active script");
    }
    if (reward.kind == QuestRewardKind::kUnlockQuest && reward.target_id == quest_id) {
        return invalid_argument("Quest cannot unlock itself as a reward");
    }
    found->second.definition.rewards.push_back(std::move(reward));
    ++quest_content_revision_;
    return {};
}

snt::core::Expected<void> GameContentRegistry::register_material(
    ScriptId owner, GameMaterialDefinition definition, bool builtin) {
    const auto normalized_id = normalize_item_key(definition.id);
    if (!normalized_id) return normalized_id.error();
    definition.id = *normalized_id;
    if (auto valid = validate(definition); !valid) return valid.error();
    if (builtin != (owner == kBuiltinScriptId)) {
        return invalid_argument("Built-in registrations must use ScriptId 0");
    }

    const std::string id = definition.id;
    const auto existing = live_materials_.find(id);
    if (!builtin && existing != live_materials_.end() &&
        existing->second.owner != kBuiltinScriptId && existing->second.owner != owner) {
        return invalid_state("Game material id is already owned by another script: " + id);
    }
    if (builtin && existing != live_materials_.end() &&
        existing->second.owner != kBuiltinScriptId) {
        return invalid_state("Cannot replace a live script material with a built-in material: " + id);
    }
    for (const auto& [reloading_script_id, snapshot] : reloads_) {
        if (reloading_script_id == owner) continue;
        if (snapshot.materials.contains(id)) {
            return invalid_state(
                "Game material id is reserved by another active script reload: " + id);
        }
    }

    OwnedDefinition<GameMaterialDefinition> entry{owner, std::move(definition)};
    if (builtin) backup_materials_[id] = entry;
    live_materials_[id] = std::move(entry);
    return {};
}

snt::core::Expected<void> GameContentRegistry::register_item(
    ScriptId owner, GameItemDefinition definition, bool builtin) {
    const auto normalized_id = normalize_item_key(definition.id);
    if (!normalized_id) return normalized_id.error();
    definition.id = *normalized_id;

    if (auto valid = validate(definition); !valid) return valid.error();
    if (builtin != (owner == kBuiltinScriptId)) {
        return invalid_argument("Built-in registrations must use ScriptId 0");
    }

    const std::string id = definition.id;
    if (live_generated_material_items_.contains(id)) {
        return invalid_state("Game item id collides with an auto-generated material form: " + id);
    }
    const auto existing = live_items_.find(id);
    if (!builtin && existing != live_items_.end() &&
        existing->second.owner != kBuiltinScriptId && existing->second.owner != owner) {
        return invalid_state("Game item id is already owned by another script: " + id);
    }
    if (builtin && existing != live_items_.end() &&
        existing->second.owner != kBuiltinScriptId) {
        return invalid_state("Cannot replace a live script item with a built-in item: " + id);
    }
    for (const auto& [reloading_script_id, snapshot] : reloads_) {
        if (reloading_script_id == owner) continue;
        if (snapshot.items.contains(id)) {
            return invalid_state(
                "Game item id is reserved by another active script reload: " + id);
        }
    }

    OwnedDefinition<GameItemDefinition> entry{owner, std::move(definition)};
    if (builtin) backup_items_[id] = entry;
    live_items_[id] = std::move(entry);
    return {};
}

snt::core::Expected<void> GameContentRegistry::rebuild_generated_material_items() {
    ItemMap generated;
    for (const auto& [material_id, entry] : live_materials_) {
        const GameMaterialDefinition& material = entry.definition;
        for (size_t index = 0; index < material_form_index(GameMaterialForm::kCount); ++index) {
            const GameMaterialForm form = static_cast<GameMaterialForm>(index);
            if (!material_generates_form(material, form)) continue;

            GameItemDefinition item;
            item.id = std::string(material_form_name(form)) + "." + material_id;
            item.title_key = "item." + material_id + "_" + std::string(material_form_name(form));
            item.max_stack = 64;
            item.presentation = make_generated_material_presentation(material, form);
            item.material_form = GameMaterialFormReference{
                .material_id = material_id,
                .form = form,
                .material_units = material_form_amount(form),
            };

            if (const auto override = material.form_presentations.find(form);
                override != material.form_presentations.end()) {
                if (!override->second.title_key.empty()) item.title_key = override->second.title_key;
                item.max_stack = override->second.max_stack;
                item.presentation = override->second.presentation;
            }
            if (auto result = validate(item); !result) return result.error();
            if (live_items_.contains(item.id)) {
                return invalid_state("Authored game item collides with generated material form: " + item.id);
            }
            // Keep the ordered-map key separate from the moved definition.
            // Function-argument evaluation would otherwise allow `item` to be
            // moved before `item.id` is copied into the key.
            const std::string item_id = item.id;
            if (!generated.emplace(item_id, OwnedDefinition<GameItemDefinition>{
                    .owner = entry.owner,
                    .definition = std::move(item),
                }).second) {
                return invalid_state("Duplicate generated material item id: " + item_id);
            }
        }
    }
    live_generated_material_items_ = std::move(generated);
    return {};
}

snt::core::Expected<void> GameContentRegistry::register_recipe(
    ScriptId owner, RecipeDefinition definition, bool builtin) {
    auto valid = validate(definition);
    if (!valid) return valid.error();
    if (builtin != (owner == kBuiltinScriptId)) {
        return invalid_argument("Built-in registrations must use ScriptId 0");
    }

    const std::string id = definition.id;
    auto existing = live_recipes_.find(id);
    if (!builtin && existing != live_recipes_.end() &&
        existing->second.owner != kBuiltinScriptId && existing->second.owner != owner) {
        return invalid_state("Recipe id is already owned by another script: " + id);
    }
    if (builtin && existing != live_recipes_.end() &&
        existing->second.owner != kBuiltinScriptId) {
        return invalid_state("Cannot replace a live script recipe with a built-in recipe: " + id);
    }

    OwnedDefinition<RecipeDefinition> entry{owner, std::move(definition)};
    if (builtin) backup_recipes_[id] = entry;
    live_recipes_[id] = std::move(entry);
    return {};
}

snt::core::Expected<void> GameContentRegistry::register_machine(
    ScriptId owner, MachineDefinition definition, bool builtin) {
    auto valid = validate(definition);
    if (!valid) return valid.error();
    if (builtin != (owner == kBuiltinScriptId)) {
        return invalid_argument("Built-in registrations must use ScriptId 0");
    }

    const std::string id = definition.id;
    auto existing = live_machines_.find(id);
    if (!builtin && existing != live_machines_.end() &&
        existing->second.owner != kBuiltinScriptId && existing->second.owner != owner) {
        return invalid_state("Machine id is already owned by another script: " + id);
    }
    if (builtin && existing != live_machines_.end() &&
        existing->second.owner != kBuiltinScriptId) {
        return invalid_state("Cannot replace a live script machine with a built-in machine: " + id);
    }

    OwnedDefinition<MachineDefinition> entry{owner, std::move(definition)};
    if (builtin) backup_machines_[id] = entry;
    live_machines_[id] = std::move(entry);
    return {};
}

snt::core::Expected<void> GameContentRegistry::register_quest_chapter(
    ScriptId owner, QuestBookChapterDefinition definition, bool builtin) {
    auto valid = validate(definition);
    if (!valid) return valid.error();
    if (builtin != (owner == kBuiltinScriptId)) {
        return invalid_argument("Built-in registrations must use ScriptId 0");
    }

    const std::string id = definition.id;
    auto existing = live_quest_chapters_.find(id);
    if (!builtin && existing != live_quest_chapters_.end() &&
        existing->second.owner != kBuiltinScriptId && existing->second.owner != owner) {
        return invalid_state("Quest chapter id is already owned by another script: " + id);
    }
    if (builtin && existing != live_quest_chapters_.end() &&
        existing->second.owner != kBuiltinScriptId) {
        return invalid_state("Cannot replace a live script quest chapter with a built-in chapter: " + id);
    }

    OwnedDefinition<QuestBookChapterDefinition> entry{owner, std::move(definition)};
    if (builtin) backup_quest_chapters_[id] = entry;
    live_quest_chapters_[id] = std::move(entry);
    ++quest_content_revision_;
    return {};
}

snt::core::Expected<void> GameContentRegistry::register_quest(
    ScriptId owner, QuestDefinition definition, bool builtin) {
    auto valid = validate(definition);
    if (!valid) return valid.error();
    if (builtin != (owner == kBuiltinScriptId)) {
        return invalid_argument("Built-in registrations must use ScriptId 0");
    }

    const std::string id = definition.id;
    auto existing = live_quests_.find(id);
    if (!builtin && existing != live_quests_.end() &&
        existing->second.owner != kBuiltinScriptId && existing->second.owner != owner) {
        return invalid_state("Quest id is already owned by another script: " + id);
    }
    if (builtin && existing != live_quests_.end() &&
        existing->second.owner != kBuiltinScriptId) {
        return invalid_state("Cannot replace a live script quest with a built-in quest: " + id);
    }

    OwnedDefinition<QuestDefinition> entry{owner, std::move(definition)};
    if (builtin) backup_quests_[id] = entry;
    live_quests_[id] = std::move(entry);
    ++quest_content_revision_;
    return {};
}

const GameItemDefinition* GameContentRegistry::find_item(std::string_view id) const {
    const auto it = live_items_.find(id);
    if (it != live_items_.end()) return &it->second.definition;
    const auto generated = live_generated_material_items_.find(id);
    return generated == live_generated_material_items_.end() ? nullptr :
        &generated->second.definition;
}

const GameMaterialDefinition* GameContentRegistry::find_material(std::string_view id) const {
    const auto it = live_materials_.find(id);
    return it == live_materials_.end() ? nullptr : &it->second.definition;
}

std::optional<RuntimeResourceKey> GameContentRegistry::find_resource_runtime_key(
    const ResourceKey& key) const {
    return resource_runtime_index_.snapshot().resolve_runtime(key);
}

std::optional<ResourceKey> GameContentRegistry::find_resource_key(
    const RuntimeResourceKey& key) const {
    return resource_runtime_index_.snapshot().resolve_semantic(key);
}

ResourceRuntimeIndex::Snapshot GameContentRegistry::resource_runtime_index() const noexcept {
    return resource_runtime_index_.snapshot();
}

uint64_t GameContentRegistry::resource_runtime_generation() const noexcept {
    return resource_runtime_index_.snapshot().generation();
}

std::vector<GameItemDefinition> GameContentRegistry::item_definitions() const {
    std::vector<GameItemDefinition> definitions;
    definitions.reserve(live_items_.size() + live_generated_material_items_.size());
    for (const auto& [id, entry] : live_items_) {
        (void)id;
        definitions.push_back(entry.definition);
    }
    for (const auto& [id, entry] : live_generated_material_items_) {
        (void)id;
        definitions.push_back(entry.definition);
    }
    std::sort(definitions.begin(), definitions.end(), [](const auto& lhs, const auto& rhs) {
        return lhs.id < rhs.id;
    });
    return definitions;
}

std::vector<GameMaterialDefinition> GameContentRegistry::material_definitions() const {
    std::vector<GameMaterialDefinition> definitions;
    definitions.reserve(live_materials_.size());
    for (const auto& [id, entry] : live_materials_) {
        (void)id;
        definitions.push_back(entry.definition);
    }
    return definitions;
}

std::vector<std::string> GameContentRegistry::tool_tags_for_item(
    std::string_view item_id) const {
    const GameItemDefinition* const item = find_item(item_id);
    if (item == nullptr) return {};
    std::vector<std::string> tags = item->tool_tags;
    if (item->tool.has_value()) {
        if (const std::string_view type_tag = tool_type_tag(item->tool->type); !type_tag.empty()) {
            tags.emplace_back(type_tag);
        }
    }
    std::sort(tags.begin(), tags.end());
    tags.erase(std::unique(tags.begin(), tags.end()), tags.end());
    return tags;
}

bool GameContentRegistry::item_matches_tool_requirement(
    std::string_view item_id, std::string_view required_tag,
    int32_t required_mining_level) const {
    if (required_mining_level < 0) return false;
    if (required_tag.empty()) return true;
    const auto normalized_tag = normalize_item_key(required_tag);
    if (!normalized_tag) return false;
    const GameItemDefinition* const item = find_item(item_id);
    if (item == nullptr) return false;
    const std::vector<std::string> tags = tool_tags_for_item(item_id);
    if (std::find(tags.begin(), tags.end(), *normalized_tag) == tags.end()) return false;
    return required_mining_level == 0 ||
        (item->tool.has_value() && item->tool->mining_level >= required_mining_level);
}

snt::core::Expected<void> GameContentRegistry::publish_resource_runtime_index() {
    std::vector<ResourceKey> keys;
    keys.reserve(live_items_.size() + live_generated_material_items_.size());
    for (const auto& [id, entry] : live_items_) {
        (void)entry;
        keys.push_back(ResourceKey::item(id));
    }
    for (const auto& [id, entry] : live_generated_material_items_) {
        (void)entry;
        keys.push_back(ResourceKey::item(id));
    }
    if (auto result = resource_runtime_index_.rebuild(keys); !result) return result.error();
    ++item_content_revision_;
    SNT_LOG_INFO("Published %zu game resource runtime key(s), generation=%llu, content_revision=%llu",
                 keys.size(),
                 static_cast<unsigned long long>(resource_runtime_index_.snapshot().generation()),
                 static_cast<unsigned long long>(item_content_revision_));
    return {};
}

const RecipeDefinition* GameContentRegistry::find_recipe(std::string_view id) const {
    auto it = live_recipes_.find(id);
    return it == live_recipes_.end() ? nullptr : &it->second.definition;
}

const MachineDefinition* GameContentRegistry::find_machine(std::string_view id) const {
    auto it = live_machines_.find(id);
    return it == live_machines_.end() ? nullptr : &it->second.definition;
}

const MachinePlacementDefinition* GameContentRegistry::find_machine_placement_by_item(
    std::string_view item_id) const noexcept {
    return machine_placements_.find_by_item(item_id);
}

const MachinePlacementDefinition* GameContentRegistry::find_machine_placement_by_material_key(
    std::string_view material_key) const noexcept {
    return machine_placements_.find_by_material_key(material_key);
}

std::vector<MachinePlacementDefinition> GameContentRegistry::machine_placement_definitions() const {
    return machine_placements_.definitions();
}

snt::core::Expected<void> GameContentRegistry::validate_machine_placement_references() const {
    for (const MachinePlacementDefinition& placement : machine_placements_.definitions()) {
        if (find_machine(placement.machine_id) != nullptr) continue;
        return invalid_state("Machine placement item '" + placement.item_id +
                             "' refers to a missing machine definition '" +
                             placement.machine_id + "'");
    }
    return {};
}

snt::core::Expected<void> GameContentRegistry::validate_machine_item_references() const {
    for (const auto& [recipe_id, entry] : live_recipes_) {
        const RecipeDefinition& recipe = entry.definition;
        for (const RecipeInputDefinition& input : recipe.inputs) {
            if (find_item(input.item_id) != nullptr) continue;
            return invalid_state("Recipe '" + recipe_id + "' refers to missing input item '" +
                                 input.item_id + "'");
        }
        for (const RecipeOutputDefinition& output : recipe.outputs) {
            if (find_item(output.item_id) != nullptr) continue;
            return invalid_state("Recipe '" + recipe_id + "' refers to missing output item '" +
                                 output.item_id + "'");
        }
    }
    for (const MachinePlacementDefinition& placement : machine_placements_.definitions()) {
        if (find_item(placement.item_id) != nullptr) continue;
        return invalid_state("Machine placement item '" + placement.item_id +
                             "' has no registered game item definition");
    }
    return {};
}

const QuestBookChapterDefinition* GameContentRegistry::find_quest_chapter(
    std::string_view id) const {
    auto it = live_quest_chapters_.find(id);
    return it == live_quest_chapters_.end() ? nullptr : &it->second.definition;
}

const QuestDefinition* GameContentRegistry::find_quest(std::string_view id) const {
    auto it = live_quests_.find(id);
    return it == live_quests_.end() ? nullptr : &it->second.definition;
}

std::vector<QuestBookChapterDefinition> GameContentRegistry::quest_chapter_definitions() const {
    std::vector<QuestBookChapterDefinition> definitions;
    definitions.reserve(live_quest_chapters_.size());
    for (const auto& [id, entry] : live_quest_chapters_) {
        (void)id;
        definitions.push_back(entry.definition);
    }
    return definitions;
}

std::vector<QuestDefinition> GameContentRegistry::quest_definitions() const {
    std::vector<QuestDefinition> definitions;
    definitions.reserve(live_quests_.size());
    for (const auto& [id, entry] : live_quests_) {
        (void)id;
        definitions.push_back(entry.definition);
    }
    return definitions;
}

uint64_t GameContentRegistry::quest_content_fingerprint() const noexcept {
    uint64_t hash = kQuestBookFingerprintOffset;
    hash_string(hash, "snt.quest_book.content.v1");
    hash_u64(hash, static_cast<uint64_t>(live_quest_chapters_.size()));
    for (const auto& [id, entry] : live_quest_chapters_) {
        const QuestBookChapterDefinition& chapter = entry.definition;
        hash_string(hash, id);
        hash_string(hash, chapter.title);
        hash_string(hash, chapter.description);
        hash_string(hash, chapter.icon_key);
        hash_u32(hash, static_cast<uint32_t>(chapter.sort_order));
    }

    hash_u64(hash, static_cast<uint64_t>(live_quests_.size()));
    for (const auto& [id, entry] : live_quests_) {
        const QuestDefinition& quest = entry.definition;
        hash_string(hash, id);
        hash_string(hash, quest.chapter_id);
        hash_string(hash, quest.title);
        hash_string(hash, quest.description);
        hash_string(hash, quest.icon_key);
        hash_u32(hash, std::bit_cast<uint32_t>(quest.node_position.x));
        hash_u32(hash, std::bit_cast<uint32_t>(quest.node_position.y));
        hash_byte(hash, quest.hidden ? 1u : 0u);
        hash_byte(hash, quest.repeatable ? 1u : 0u);

        hash_u64(hash, static_cast<uint64_t>(quest.prerequisites.size()));
        for (const std::string& prerequisite : quest.prerequisites) hash_string(hash, prerequisite);
        hash_u64(hash, static_cast<uint64_t>(quest.objectives.size()));
        for (const QuestObjectiveDefinition& objective : quest.objectives) {
            hash_string(hash, objective.id);
            hash_byte(hash, static_cast<uint8_t>(objective.kind));
            hash_string(hash, objective.target_id);
            hash_u32(hash, static_cast<uint32_t>(objective.required_count));
        }
        hash_u64(hash, static_cast<uint64_t>(quest.rewards.size()));
        for (const QuestRewardDefinition& reward : quest.rewards) {
            hash_byte(hash, static_cast<uint8_t>(reward.kind));
            hash_string(hash, reward.target_id);
            hash_u32(hash, static_cast<uint32_t>(reward.count));
        }
    }
    return hash;
}

std::vector<RecipeDefinition> GameContentRegistry::recipes_for_machine(
    std::string_view machine_id) const {
    std::vector<RecipeDefinition> result;
    for (const auto& [id, entry] : live_recipes_) {
        (void)id;
        if (entry.definition.machine_id == machine_id) result.push_back(entry.definition);
    }
    // live_recipes_ is an unordered map. A machine may intentionally have
    // more than one recipe matching its current inputs, so expose a stable
    // order before its worker captures this value snapshot.
    std::sort(result.begin(), result.end(), [](const RecipeDefinition& lhs,
                                               const RecipeDefinition& rhs) {
        return lhs.id < rhs.id;
    });
    return result;
}

snt::core::Expected<void> GameContentRegistry::add_event_listener(EventListener listener) {
    auto valid = validate(listener);
    if (!valid) return valid.error();

    auto& listeners = event_listeners_[listener.event_name];
    const auto duplicate = std::find(listeners.begin(), listeners.end(), listener);
    if (duplicate != listeners.end()) {
        return invalid_state("Duplicate event listener: " + listener.event_name + "/" + listener.callback_id);
    }
    const std::string event_name = listener.event_name;
    listeners.push_back(std::move(listener));
    sort_event_listeners(event_name);
    return {};
}

std::vector<EventListener> GameContentRegistry::event_listeners(std::string_view event_name) const {
    auto it = event_listeners_.find(event_name);
    return it == event_listeners_.end() ? std::vector<EventListener>{} : it->second;
}

std::vector<std::string> GameContentRegistry::callback_ids_for_script(ScriptId script_id) const {
    std::vector<std::string> callback_ids;
    for (const auto& [event_name, listeners] : event_listeners_) {
        (void)event_name;
        for (const auto& listener : listeners) {
            if (listener.script_id == script_id) callback_ids.push_back(listener.callback_id);
        }
    }
    return callback_ids;
}

snt::core::Expected<void> GameContentRegistry::set_state(ScriptId script_id,
                                                          std::string key,
                                                          std::string value) {
    if (script_id == kBuiltinScriptId) return invalid_argument("StateStore requires a non-builtin ScriptId");
    if (key.empty()) return invalid_argument("StateStore key must not be empty");
    state_store_[script_id][std::move(key)] = std::move(value);
    return {};
}

std::optional<std::string> GameContentRegistry::get_state(ScriptId script_id,
                                                           std::string_view key) const {
    auto script = state_store_.find(script_id);
    if (script == state_store_.end()) return std::nullopt;
    auto value = script->second.find(key);
    if (value == script->second.end()) return std::nullopt;
    return value->second;
}

snt::core::Expected<void> GameContentRegistry::begin_reload(ScriptId script_id) {
    if (reload_batch_) return invalid_state("Cannot start a single-script reload during a reload batch");
    if (script_id == kBuiltinScriptId) return invalid_argument("Built-in content cannot be reloaded as a script");
    if (reloads_.contains(script_id)) return invalid_state("Reload is already active for script");

    if (auto result = machine_placements_.begin_reload(script_id); !result) return result.error();

    auto [reload, inserted] = reloads_.emplace(script_id, snapshot_script_content(script_id));
    (void)inserted;
    if (auto result = erase_script_content(script_id); !result) {
        auto error = result.error();
        static_cast<void>(restore_script_content(reload->second));
        reloads_.erase(reload);
        static_cast<void>(machine_placements_.rollback_reload(script_id));
        error.with_context("Game content reload could not remove prior script content");
        return error;
    }
    SNT_LOG_DEBUG("Game content began transactional reload for script %llu",
                  static_cast<unsigned long long>(script_id));
    return {};
}

snt::core::Expected<void> GameContentRegistry::commit_reload(ScriptId script_id) {
    if (reload_batch_) return invalid_state("Cannot commit a single-script reload during a reload batch");
    auto it = reloads_.find(script_id);
    if (it == reloads_.end()) return invalid_state("No active reload for script");
    // All material changes made during a registration transaction become
    // visible together. This preserves the worker snapshot boundary and
    // avoids regenerating the complete form catalog for every API call.
    if (auto result = rebuild_generated_material_items(); !result) return result.error();
    if (auto result = validate_machine_placement_references(); !result) return result.error();
    if (auto result = validate_machine_item_references(); !result) return result.error();
    if (auto result = publish_resource_runtime_index(); !result) return result.error();
    if (auto result = machine_placements_.commit_reload(script_id); !result) {
        resource_runtime_index_.restore(it->second.resource_runtime_index);
        item_content_revision_ = it->second.item_content_revision;
        return result.error();
    }
    reloads_.erase(it);
    SNT_LOG_INFO("Game content committed reload for script %llu resource_generation=%llu",
                 static_cast<unsigned long long>(script_id),
                 static_cast<unsigned long long>(resource_runtime_index_.snapshot().generation()));
    return {};
}

snt::core::Expected<void> GameContentRegistry::rollback_reload(ScriptId script_id) {
    if (reload_batch_) return invalid_state("Cannot roll back a single-script reload during a reload batch");
    auto it = reloads_.find(script_id);
    if (it == reloads_.end()) return invalid_state("No active reload for script");

    if (auto result = machine_placements_.rollback_reload(script_id); !result) return result.error();

    if (auto result = erase_script_content(script_id); !result) return result.error();
    if (auto result = restore_script_content(it->second); !result) return result.error();
    resource_runtime_index_.restore(it->second.resource_runtime_index);
    item_content_revision_ = it->second.item_content_revision;
    reloads_.erase(it);
    SNT_LOG_WARN("Game content rolled back reload for script %llu",
                 static_cast<unsigned long long>(script_id));
    return {};
}

snt::core::Expected<void> GameContentRegistry::begin_reload_batch(
    std::span<const ScriptId> script_ids) {
    if (reload_batch_) return invalid_state("A game content reload batch is already active");
    if (script_ids.empty()) return invalid_argument("Game content reload batch must not be empty");

    std::set<ScriptId> unique_ids;
    for (const ScriptId script_id : script_ids) {
        if (script_id == kBuiltinScriptId) {
            return invalid_argument("Built-in content cannot be reloaded as a script");
        }
        if (!unique_ids.insert(script_id).second) {
            return invalid_argument("Game content reload batch contains a duplicate ScriptId");
        }
        if (reloads_.contains(script_id)) {
            return invalid_state("A game content script reload is already active");
        }
    }
    if (auto result = machine_placements_.begin_reload_batch(script_ids); !result) {
        return result.error();
    }

    std::vector<ScriptId> started;
    started.reserve(script_ids.size());
    for (const ScriptId script_id : script_ids) {
        auto [reload, inserted] = reloads_.emplace(script_id, snapshot_script_content(script_id));
        (void)reload;
        if (!inserted) {
            static_cast<void>(machine_placements_.rollback_reload_batch(script_ids));
            for (auto it = started.rbegin(); it != started.rend(); ++it) {
                const auto snapshot = reloads_.find(*it);
                static_cast<void>(erase_script_content(*it));
                static_cast<void>(restore_script_content(snapshot->second));
                reloads_.erase(snapshot);
            }
            return invalid_state("Game content reload batch could not create a script snapshot");
        }
        if (auto result = erase_script_content(script_id); !result) {
            auto error = result.error();
            static_cast<void>(machine_placements_.rollback_reload_batch(script_ids));
            for (auto it = started.rbegin(); it != started.rend(); ++it) {
                const auto snapshot = reloads_.find(*it);
                static_cast<void>(erase_script_content(*it));
                static_cast<void>(restore_script_content(snapshot->second));
                reloads_.erase(snapshot);
            }
            const auto failed_snapshot = reloads_.find(script_id);
            if (failed_snapshot != reloads_.end()) {
                static_cast<void>(restore_script_content(failed_snapshot->second));
                reloads_.erase(failed_snapshot);
            }
            error.with_context("Game content reload batch could not remove prior script content");
            return error;
        }
        started.push_back(script_id);
    }

    reload_batch_ = ReloadBatch{
        .script_ids = std::vector<ScriptId>(script_ids.begin(), script_ids.end()),
        .quest_content_revision = quest_content_revision_,
    };
    SNT_LOG_DEBUG("Game content began transactional reload batch with %zu script(s)",
                  script_ids.size());
    return {};
}

snt::core::Expected<void> GameContentRegistry::commit_reload_batch(
    std::span<const ScriptId> script_ids) {
    if (!matches_active_reload_batch(script_ids)) {
        return invalid_state("No matching active game content reload batch");
    }
    if (auto result = rebuild_generated_material_items(); !result) return result.error();
    if (auto result = validate_machine_placement_references(); !result) return result.error();
    if (auto result = validate_machine_item_references(); !result) return result.error();
    if (auto result = publish_resource_runtime_index(); !result) return result.error();
    if (auto result = machine_placements_.commit_reload_batch(script_ids); !result) {
        return result.error();
    }
    for (const ScriptId script_id : script_ids) reloads_.erase(script_id);
    reload_batch_.reset();
    SNT_LOG_INFO("Game content committed reload batch with %zu script(s) resource_generation=%llu",
                 script_ids.size(),
                 static_cast<unsigned long long>(resource_runtime_index_.snapshot().generation()));
    return {};
}

snt::core::Expected<void> GameContentRegistry::rollback_reload_batch(
    std::span<const ScriptId> script_ids) {
    if (!matches_active_reload_batch(script_ids)) {
        return invalid_state("No matching active game content reload batch");
    }
    const auto first_snapshot = reloads_.find(script_ids.front());
    if (first_snapshot == reloads_.end()) {
        return invalid_state("Game content reload batch lost its first script snapshot");
    }
    const auto resource_runtime_index = first_snapshot->second.resource_runtime_index;
    const uint64_t item_content_revision = first_snapshot->second.item_content_revision;
    const uint64_t quest_content_revision = reload_batch_->quest_content_revision;

    if (auto result = machine_placements_.rollback_reload_batch(script_ids); !result) {
        return result.error();
    }
    for (auto it = script_ids.rbegin(); it != script_ids.rend(); ++it) {
        const auto snapshot = reloads_.find(*it);
        if (snapshot == reloads_.end()) {
            return invalid_state("Game content reload batch lost a script snapshot");
        }
        if (auto result = erase_script_content(*it); !result) return result.error();
        if (auto result = restore_script_content(snapshot->second); !result) return result.error();
    }
    resource_runtime_index_.restore(resource_runtime_index);
    item_content_revision_ = item_content_revision;
    quest_content_revision_ = quest_content_revision;
    for (const ScriptId script_id : script_ids) reloads_.erase(script_id);
    reload_batch_.reset();
    SNT_LOG_WARN("Game content rolled back reload batch with %zu script(s)", script_ids.size());
    return {};
}

snt::core::Expected<void> GameContentRegistry::unload_script(ScriptId script_id) {
    if (reload_batch_) return invalid_state("Cannot unload a script during a game content reload batch");
    if (script_id == kBuiltinScriptId) return invalid_argument("Built-in content cannot be unloaded as a script");
    if (reloads_.contains(script_id)) return invalid_state("Cannot unload a script during its active reload");

    // Unload is a content transition too. Reuse the transaction path so an
    // unload cannot strand another script's placement pointing at a machine
    // that only this script supplied.
    if (auto result = begin_reload(script_id); !result) return result.error();
    if (auto result = validate_machine_placement_references(); !result) {
        auto error = result.error();
        if (auto rollback = rollback_reload(script_id); !rollback) {
            error.with_context("Game content unload rollback failed: " +
                               rollback.error().format());
        }
        return error;
    }
    if (auto result = commit_reload(script_id); !result) {
        auto error = result.error();
        if (auto rollback = rollback_reload(script_id); !rollback) {
            error.with_context("Game content unload rollback failed: " +
                               rollback.error().format());
        }
        return error;
    }
    SNT_LOG_INFO("Game content unloaded script %llu",
                 static_cast<unsigned long long>(script_id));
    return {};
}

void GameContentRegistry::reset() {
    const bool had_quest_book_content = !backup_quest_chapters_.empty() ||
        !live_quest_chapters_.empty() || !backup_quests_.empty() || !live_quests_.empty();
    backup_materials_.clear();
    backup_items_.clear();
    backup_recipes_.clear();
    backup_machines_.clear();
    backup_quest_chapters_.clear();
    backup_quests_.clear();
    live_materials_.clear();
    live_items_.clear();
    live_generated_material_items_.clear();
    live_recipes_.clear();
    live_machines_.clear();
    live_quest_chapters_.clear();
    live_quests_.clear();
    machine_placements_.reset();
    event_listeners_.clear();
    state_store_.clear();
    reloads_.clear();
    reload_batch_.reset();
    const std::vector<ResourceKey> no_resource_keys;
    if (auto result = resource_runtime_index_.rebuild(no_resource_keys); !result) {
        SNT_LOG_ERROR("Failed to publish an empty game resource runtime index during reset: %s",
                      result.error().format().c_str());
    }
    ++item_content_revision_;
    if (had_quest_book_content) ++quest_content_revision_;
}

GameContentRegistry::ReloadSnapshot GameContentRegistry::snapshot_script_content(
    ScriptId script_id) const {
    ReloadSnapshot snapshot;
    snapshot.resource_runtime_index = resource_runtime_index_.snapshot();
    snapshot.item_content_revision = item_content_revision_;
    for (const auto& [id, entry] : live_materials_) {
        if (entry.owner == script_id) snapshot.materials.emplace(id, entry);
    }
    for (const auto& [id, entry] : live_items_) {
        if (entry.owner == script_id) snapshot.items.emplace(id, entry);
    }
    for (const auto& [id, entry] : live_recipes_) {
        if (entry.owner == script_id) snapshot.recipes.emplace(id, entry);
    }
    for (const auto& [id, entry] : live_machines_) {
        if (entry.owner == script_id) snapshot.machines.emplace(id, entry);
    }
    for (const auto& [id, entry] : live_quest_chapters_) {
        if (entry.owner == script_id) snapshot.quest_chapters.emplace(id, entry);
    }
    for (const auto& [id, entry] : live_quests_) {
        if (entry.owner == script_id) snapshot.quests.emplace(id, entry);
    }
    for (const auto& [event_name, listeners] : event_listeners_) {
        (void)event_name;
        for (const auto& listener : listeners) {
            if (listener.script_id == script_id) snapshot.event_listeners.push_back(listener);
        }
    }
    return snapshot;
}

snt::core::Expected<void> GameContentRegistry::erase_script_content(ScriptId script_id) {
    for (auto it = live_materials_.begin(); it != live_materials_.end();) {
        if (it->second.owner != script_id) {
            ++it;
            continue;
        }
        auto backup = backup_materials_.find(it->first);
        if (backup == backup_materials_.end()) it = live_materials_.erase(it);
        else {
            it->second = backup->second;
            ++it;
        }
    }
    for (auto it = live_items_.begin(); it != live_items_.end();) {
        if (it->second.owner != script_id) {
            ++it;
            continue;
        }
        auto backup = backup_items_.find(it->first);
        if (backup == backup_items_.end()) it = live_items_.erase(it);
        else {
            it->second = backup->second;
            ++it;
        }
    }
    for (auto it = live_recipes_.begin(); it != live_recipes_.end();) {
        if (it->second.owner != script_id) {
            ++it;
            continue;
        }
        auto backup = backup_recipes_.find(it->first);
        if (backup == backup_recipes_.end()) it = live_recipes_.erase(it);
        else {
            it->second = backup->second;
            ++it;
        }
    }
    for (auto it = live_machines_.begin(); it != live_machines_.end();) {
        if (it->second.owner != script_id) {
            ++it;
            continue;
        }
        auto backup = backup_machines_.find(it->first);
        if (backup == backup_machines_.end()) it = live_machines_.erase(it);
        else {
            it->second = backup->second;
            ++it;
        }
    }
    bool quest_book_changed = false;
    for (auto it = live_quest_chapters_.begin(); it != live_quest_chapters_.end();) {
        if (it->second.owner != script_id) {
            ++it;
            continue;
        }
        quest_book_changed = true;
        auto backup = backup_quest_chapters_.find(it->first);
        if (backup == backup_quest_chapters_.end()) it = live_quest_chapters_.erase(it);
        else {
            it->second = backup->second;
            ++it;
        }
    }
    for (auto it = live_quests_.begin(); it != live_quests_.end();) {
        if (it->second.owner != script_id) {
            ++it;
            continue;
        }
        quest_book_changed = true;
        auto backup = backup_quests_.find(it->first);
        if (backup == backup_quests_.end()) it = live_quests_.erase(it);
        else {
            it->second = backup->second;
            ++it;
        }
    }
    if (quest_book_changed) ++quest_content_revision_;
    for (auto events = event_listeners_.begin(); events != event_listeners_.end();) {
        auto& listeners = events->second;
        std::erase_if(listeners, [script_id](const EventListener& listener) {
            return listener.script_id == script_id;
        });
        if (listeners.empty()) events = event_listeners_.erase(events);
        else ++events;
    }
    return rebuild_generated_material_items();
}

snt::core::Expected<void> GameContentRegistry::restore_script_content(
    const ReloadSnapshot& snapshot) {
    for (const auto& [id, entry] : snapshot.materials) live_materials_[id] = entry;
    for (const auto& [id, entry] : snapshot.items) live_items_[id] = entry;
    for (const auto& [id, entry] : snapshot.recipes) live_recipes_[id] = entry;
    for (const auto& [id, entry] : snapshot.machines) live_machines_[id] = entry;
    for (const auto& [id, entry] : snapshot.quest_chapters) live_quest_chapters_[id] = entry;
    for (const auto& [id, entry] : snapshot.quests) live_quests_[id] = entry;
    if (!snapshot.quest_chapters.empty() || !snapshot.quests.empty()) ++quest_content_revision_;
    for (const auto& listener : snapshot.event_listeners) {
        event_listeners_[listener.event_name].push_back(listener);
        sort_event_listeners(listener.event_name);
    }
    return rebuild_generated_material_items();
}

bool GameContentRegistry::matches_active_reload_batch(
    std::span<const ScriptId> script_ids) const noexcept {
    if (!reload_batch_ || reload_batch_->script_ids.size() != script_ids.size()) return false;
    for (size_t index = 0; index < script_ids.size(); ++index) {
        if (reload_batch_->script_ids[index] != script_ids[index]) return false;
    }
    return true;
}

void GameContentRegistry::sort_event_listeners(std::string_view event_name) {
    auto it = event_listeners_.find(event_name);
    if (it == event_listeners_.end()) return;
    std::sort(it->second.begin(), it->second.end(),
              [](const EventListener& lhs, const EventListener& rhs) {
                  if (lhs.script_id != rhs.script_id) return lhs.script_id < rhs.script_id;
                  return lhs.callback_id < rhs.callback_id;
              });
}

}  // namespace snt::game
