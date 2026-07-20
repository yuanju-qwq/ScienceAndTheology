// World-generation script snapshot construction implementation.

#define SNT_LOG_CHANNEL "game.worldgen_script"
#include "game/simulation/worldgen_script_content.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <angelscript.h>

#include "core/error.h"
#include "core/log.h"
#include "game/worldgen/world_gen_config.h"
#include "script/script_manager.h"

namespace snt::game {
namespace {

constexpr std::string_view kWorldgenModuleName = "50_worldgen_catalog";
constexpr uint32_t kKnownTerrainFlags =
    TF_WALKABLE | TF_SOLID | TF_LIQUID | TF_MINEABLE | TF_CLIMBABLE |
    TF_INDESTRUCTIBLE | TF_GRAVITY_FALL | TF_COLLAPSE_RISK | TF_SUPPORT_BEAM;

struct PendingBaseTerrainRule {
    BaseTerrainRule rule;
    std::string default_material_key;
    std::string low_elevation_material_key;
    std::string high_elevation_material_key;
    std::string cave_air_material_key;
};

struct WorldgenScriptDraft {
    WorldGenConfigSnapshot config;
    std::vector<PendingBaseTerrainRule> pending_base_terrain_rules;
};

thread_local WorldgenScriptDraft* g_active_draft = nullptr;

[[nodiscard]] snt::core::Error invalid_argument(std::string message) {
    return {snt::core::ErrorCode::kInvalidArgument, std::move(message)};
}

[[nodiscard]] snt::core::Error invalid_state(std::string message) {
    return {snt::core::ErrorCode::kInvalidState, std::move(message)};
}

void report_binding_error(const snt::core::Error& error) {
    SNT_LOG_ERROR("World-generation script API rejected registration: %s",
                  error.format().c_str());
    if (asIScriptContext* context = asGetActiveContext()) {
        context->SetException(error.format().c_str());
    }
}

[[nodiscard]] WorldgenScriptDraft* active_draft() {
    if (g_active_draft) return g_active_draft;
    report_binding_error(invalid_state(
        "World-generation registration was called outside snt_register_worldgen()"));
    return nullptr;
}

class WorldgenDraftScope final {
public:
    explicit WorldgenDraftScope(WorldgenScriptDraft& draft)
        : previous_(g_active_draft) {
        g_active_draft = &draft;
    }

    ~WorldgenDraftScope() { g_active_draft = previous_; }

    WorldgenDraftScope(const WorldgenDraftScope&) = delete;
    WorldgenDraftScope& operator=(const WorldgenDraftScope&) = delete;

private:
    WorldgenScriptDraft* previous_ = nullptr;
};

[[nodiscard]] snt::core::Expected<std::string> normalize_key(
    std::string_view value,
    std::string_view field_name) {
    auto normalized = normalize_terrain_material_key(value);
    if (!normalized) {
        auto error = normalized.error();
        error.with_context(std::string(field_name));
        return error;
    }
    return normalized;
}

[[nodiscard]] TerrainMaterialDef* find_material(WorldgenScriptDraft& draft,
                                                  std::string_view key) {
    const auto found = std::find_if(
        draft.config.materials.begin(), draft.config.materials.end(),
        [key](const TerrainMaterialDef& material) { return material.key == key; });
    return found == draft.config.materials.end() ? nullptr : &*found;
}

[[nodiscard]] TreeSpeciesDef* find_tree(WorldgenScriptDraft& draft,
                                         std::string_view key) {
    const auto found = std::find_if(
        draft.config.tree_species.begin(), draft.config.tree_species.end(),
        [key](const TreeSpeciesDef& species) { return species.species_key == key; });
    return found == draft.config.tree_species.end() ? nullptr : &*found;
}

[[nodiscard]] CropSpeciesDef* find_crop(WorldgenScriptDraft& draft,
                                         std::string_view key) {
    const auto found = std::find_if(
        draft.config.crop_species.begin(), draft.config.crop_species.end(),
        [key](const CropSpeciesDef& species) { return species.species_key == key; });
    return found == draft.config.crop_species.end() ? nullptr : &*found;
}

[[nodiscard]] bool finite(float value) { return std::isfinite(value); }

[[nodiscard]] bool unit_interval(float value) {
    return finite(value) && value >= 0.0f && value <= 1.0f;
}

[[nodiscard]] std::string* terrain_role_slot(WorldGenConfigSnapshot& config,
                                               std::string_view role) {
    if (role == "air") return &config.role_keys.air;
    if (role == "stone") return &config.role_keys.stone;
    if (role == "dirt") return &config.role_keys.dirt;
    if (role == "sand") return &config.role_keys.sand;
    if (role == "water") return &config.role_keys.water;
    if (role == "lava") return &config.role_keys.lava;
    if (role == "ore_iron") return &config.role_keys.ore_iron;
    if (role == "ore_copper") return &config.role_keys.ore_copper;
    if (role == "ore_coal") return &config.role_keys.ore_coal;
    if (role == "wood") return &config.role_keys.wood;
    if (role == "leaves") return &config.role_keys.leaves;
    if (role == "deepstone") return &config.role_keys.deepstone;
    if (role == "core_barrier") return &config.role_keys.core_barrier;
    if (role == "snow") return &config.role_keys.snow;
    if (role == "ice") return &config.role_keys.ice;
    return nullptr;
}

[[nodiscard]] std::string* runtime_material_role_slot(WorldGenConfigSnapshot& config,
                                                        std::string_view role) {
    if (role == "ladder") return &config.runtime_material_keys.ladder;
    if (role == "workbench") return &config.runtime_material_keys.workbench;
    if (role == "fence") return &config.runtime_material_keys.fence;
    if (role == "farmland") return &config.runtime_material_keys.farmland;
    if (role == "bloomery") return &config.runtime_material_keys.bloomery;
    return nullptr;
}

void api_register_worldgen_material(const std::string& key,
                                    const std::string& title_key,
                                    int flags,
                                    float hardness,
                                    const std::string& required_tool_tag,
                                    int required_mining_level,
                                    float collapse_chance,
                                    int support_radius,
                                    const std::string& rock_layer_key) {
    WorldgenScriptDraft* const draft = active_draft();
    if (!draft) return;
    if (flags < 0 || (static_cast<uint32_t>(flags) & ~kKnownTerrainFlags) != 0 ||
        !finite(hardness) || required_mining_level < 0 || !unit_interval(collapse_chance) ||
        support_radius < 0) {
        report_binding_error(invalid_argument(
            "Terrain material has invalid flags, numeric properties, or mining requirements"));
        return;
    }
    auto normalized_key = normalize_key(key, "worldgen material key");
    if (!normalized_key) {
        report_binding_error(normalized_key.error());
        return;
    }
    auto normalized_rock_layer_key = normalize_key(
        rock_layer_key.empty() ? std::string_view{"snt:unassigned"}
                                : std::string_view{rock_layer_key},
        "worldgen material rock layer key");
    if (!normalized_rock_layer_key) {
        report_binding_error(normalized_rock_layer_key.error());
        return;
    }
    if (find_material(*draft, *normalized_key)) {
        report_binding_error(invalid_argument(
            "World-generation script registered a duplicate terrain material: " + *normalized_key));
        return;
    }

    TerrainMaterialDef material;
    material.key = std::move(*normalized_key);
    material.title_key = title_key;
    material.flags = static_cast<uint32_t>(flags);
    material.hardness = hardness;
    material.required_tool_tag = required_tool_tag;
    material.required_mining_level = required_mining_level;
    material.collapse_chance = collapse_chance;
    material.support_radius = support_radius;
    if (!rock_layer_key.empty()) {
        material.rock_layer_key = std::move(*normalized_rock_layer_key);
    }
    draft->config.materials.push_back(std::move(material));
}

void api_add_worldgen_material_drop(const std::string& material_key,
                                    const std::string& item_key,
                                    int count,
                                    int min_count,
                                    int max_count,
                                    float chance) {
    WorldgenScriptDraft* const draft = active_draft();
    if (!draft) return;
    auto normalized_material_key = normalize_key(material_key, "worldgen drop material key");
    if (!normalized_material_key) {
        report_binding_error(normalized_material_key.error());
        return;
    }
    auto normalized_item_key = normalize_key(item_key, "worldgen drop item key");
    if (!normalized_item_key) {
        report_binding_error(normalized_item_key.error());
        return;
    }
    if (count <= 0 || min_count <= 0 || max_count < min_count || !unit_interval(chance)) {
        report_binding_error(invalid_argument("Terrain material drop has invalid count or chance"));
        return;
    }
    TerrainMaterialDef* const material = find_material(*draft, *normalized_material_key);
    if (!material) {
        report_binding_error(invalid_argument(
            "Terrain drop refers to an unregistered material: " + *normalized_material_key));
        return;
    }
    material->drops.push_back({
        .item_key = std::move(*normalized_item_key),
        .count = count,
        .min_count = min_count,
        .max_count = max_count,
        .chance = chance,
    });
}

void api_bind_worldgen_role(const std::string& role, const std::string& material_key) {
    WorldgenScriptDraft* const draft = active_draft();
    if (!draft) return;
    std::string* const slot = terrain_role_slot(draft->config, role);
    if (!slot) {
        report_binding_error(invalid_argument("Unknown terrain role: " + role));
        return;
    }
    if (!slot->empty()) {
        report_binding_error(invalid_argument("Terrain role is already bound: " + role));
        return;
    }
    auto normalized_key = normalize_key(material_key, "worldgen terrain role material key");
    if (!normalized_key) {
        report_binding_error(normalized_key.error());
        return;
    }
    *slot = std::move(*normalized_key);
}

void api_bind_worldgen_runtime_material(const std::string& role,
                                        const std::string& material_key) {
    WorldgenScriptDraft* const draft = active_draft();
    if (!draft) return;
    std::string* const slot = runtime_material_role_slot(draft->config, role);
    if (!slot) {
        report_binding_error(invalid_argument("Unknown runtime terrain role: " + role));
        return;
    }
    if (!slot->empty()) {
        report_binding_error(invalid_argument("Runtime terrain role is already bound: " + role));
        return;
    }
    auto normalized_key = normalize_key(material_key, "worldgen runtime material key");
    if (!normalized_key) {
        report_binding_error(normalized_key.error());
        return;
    }
    *slot = std::move(*normalized_key);
}

void api_register_worldgen_tree(const std::string& species_key,
                                const std::string& title_key,
                                float temperature_min,
                                float temperature_max,
                                float humidity_min,
                                float humidity_max,
                                float density_weight,
                                int min_trunk_height,
                                int max_trunk_height,
                                int canopy_shape,
                                int canopy_radius,
                                const std::string& wood_material_key,
                                const std::string& leaves_material_key,
                                const std::string& sapling_material_key,
                                bool is_evergreen,
                                int64_t ticks_to_young,
                                int64_t ticks_to_mature,
                                bool has_fruit,
                                const std::string& fruit_item_key,
                                int fruit_season) {
    WorldgenScriptDraft* const draft = active_draft();
    if (!draft) return;
    if (species_key.empty() || find_tree(*draft, species_key) ||
        !finite(temperature_min) || !finite(temperature_max) ||
        !finite(humidity_min) || !finite(humidity_max) || !finite(density_weight) ||
        temperature_min > temperature_max || humidity_min > humidity_max ||
        density_weight < 0.0f || min_trunk_height <= 0 ||
        max_trunk_height < min_trunk_height || canopy_radius <= 0 ||
        canopy_shape < 0 || canopy_shape >= static_cast<int>(CanopyShape::COUNT) ||
        ticks_to_young <= 0 || ticks_to_mature <= 0 || fruit_season < -1 || fruit_season > 3 ||
        (has_fruit && fruit_item_key.empty())) {
        report_binding_error(invalid_argument(
            "World-generation tree has duplicate, invalid, or out-of-range properties"));
        return;
    }
    auto wood_key = normalize_key(wood_material_key, "tree wood material key");
    auto leaves_key = normalize_key(leaves_material_key, "tree leaves material key");
    auto sapling_key = normalize_key(sapling_material_key, "tree sapling material key");
    if (!wood_key || !leaves_key || !sapling_key) {
        report_binding_error(!wood_key ? wood_key.error()
                                       : (!leaves_key ? leaves_key.error() : sapling_key.error()));
        return;
    }
    std::string normalized_fruit_item_key;
    if (has_fruit) {
        auto fruit_key = normalize_key(fruit_item_key, "tree fruit item key");
        if (!fruit_key) {
            report_binding_error(fruit_key.error());
            return;
        }
        normalized_fruit_item_key = std::move(*fruit_key);
    }

    draft->config.tree_species.push_back({
        .species_key = species_key,
        .title_key = title_key,
        .temperature_min = temperature_min,
        .temperature_max = temperature_max,
        .humidity_min = humidity_min,
        .humidity_max = humidity_max,
        .density_weight = density_weight,
        .min_trunk_height = min_trunk_height,
        .max_trunk_height = max_trunk_height,
        .canopy_shape = static_cast<CanopyShape>(canopy_shape),
        .canopy_radius = canopy_radius,
        .wood_material_key = std::move(*wood_key),
        .leaves_material_key = std::move(*leaves_key),
        .sapling_material_key = std::move(*sapling_key),
        .is_evergreen = is_evergreen,
        .ticks_to_young = ticks_to_young,
        .ticks_to_mature = ticks_to_mature,
        .has_fruit = has_fruit,
        .fruit_item_key = std::move(normalized_fruit_item_key),
        .fruit_season = fruit_season,
    });
}

void api_set_worldgen_tree_colors(const std::string& species_key,
                                  float wood_r,
                                  float wood_g,
                                  float wood_b,
                                  float leaves_r,
                                  float leaves_g,
                                  float leaves_b,
                                  float autumn_r,
                                  float autumn_g,
                                  float autumn_b) {
    WorldgenScriptDraft* const draft = active_draft();
    if (!draft) return;
    TreeSpeciesDef* const tree = find_tree(*draft, species_key);
    if (!tree) {
        report_binding_error(invalid_argument("Tree color refers to an unregistered species: " +
                                              species_key));
        return;
    }
    if (!unit_interval(wood_r) || !unit_interval(wood_g) || !unit_interval(wood_b) ||
        !unit_interval(leaves_r) || !unit_interval(leaves_g) || !unit_interval(leaves_b) ||
        !unit_interval(autumn_r) || !unit_interval(autumn_g) || !unit_interval(autumn_b)) {
        report_binding_error(invalid_argument("Tree colors must be finite values in [0, 1]"));
        return;
    }
    tree->wood_color_r = wood_r;
    tree->wood_color_g = wood_g;
    tree->wood_color_b = wood_b;
    tree->leaves_color_r = leaves_r;
    tree->leaves_color_g = leaves_g;
    tree->leaves_color_b = leaves_b;
    tree->autumn_color_r = autumn_r;
    tree->autumn_color_g = autumn_g;
    tree->autumn_color_b = autumn_b;
}

void api_register_worldgen_crop(const std::string& species_key,
                                const std::string& title_key,
                                int category,
                                float temperature_min,
                                float temperature_max,
                                float humidity_min,
                                float humidity_max,
                                int plant_season,
                                int grow_season,
                                int harvest_season,
                                int64_t ticks_seed_to_sprout,
                                int64_t ticks_sprout_to_growing,
                                int64_t ticks_growing_to_mature,
                                const std::string& seed_item_key,
                                const std::string& crop_item_key,
                                const std::string& byproduct_item_key,
                                int crop_min,
                                int crop_max,
                                int byproduct_count,
                                bool repeat_harvest,
                                int64_t regrow_ticks,
                                const std::string& seed_material_key,
                                const std::string& sprout_material_key,
                                const std::string& growing_material_key,
                                const std::string& mature_material_key) {
    WorldgenScriptDraft* const draft = active_draft();
    if (!draft) return;
    const auto valid_season = [](int season) { return season >= -1 && season <= 3; };
    if (species_key.empty() || find_crop(*draft, species_key) || category < 0 ||
        category >= static_cast<int>(CropCategory::COUNT) || !finite(temperature_min) ||
        !finite(temperature_max) || !finite(humidity_min) || !finite(humidity_max) ||
        temperature_min > temperature_max || humidity_min > humidity_max ||
        !valid_season(plant_season) || !valid_season(grow_season) ||
        !valid_season(harvest_season) || ticks_seed_to_sprout <= 0 ||
        ticks_sprout_to_growing <= 0 || ticks_growing_to_mature <= 0 || crop_min <= 0 ||
        crop_max < crop_min || byproduct_count < 0 || regrow_ticks < 0) {
        report_binding_error(invalid_argument(
            "World-generation crop has duplicate, invalid, or out-of-range properties"));
        return;
    }
    auto seed_item = normalize_key(seed_item_key, "crop seed item key");
    auto crop_item = normalize_key(crop_item_key, "crop item key");
    auto seed_material = normalize_key(seed_material_key, "crop seed material key");
    auto sprout_material = normalize_key(sprout_material_key, "crop sprout material key");
    auto growing_material = normalize_key(growing_material_key, "crop growing material key");
    auto mature_material = normalize_key(mature_material_key, "crop mature material key");
    if (!seed_item || !crop_item || !seed_material || !sprout_material || !growing_material ||
        !mature_material) {
        if (!seed_item) report_binding_error(seed_item.error());
        else if (!crop_item) report_binding_error(crop_item.error());
        else if (!seed_material) report_binding_error(seed_material.error());
        else if (!sprout_material) report_binding_error(sprout_material.error());
        else if (!growing_material) report_binding_error(growing_material.error());
        else report_binding_error(mature_material.error());
        return;
    }
    std::string normalized_byproduct_item_key;
    if (!byproduct_item_key.empty()) {
        auto byproduct_item = normalize_key(byproduct_item_key, "crop byproduct item key");
        if (!byproduct_item) {
            report_binding_error(byproduct_item.error());
            return;
        }
        normalized_byproduct_item_key = std::move(*byproduct_item);
    }

    CropSpeciesDef crop;
    crop.species_key = species_key;
    crop.title_key = title_key;
    crop.category = static_cast<CropCategory>(category);
    crop.temperature_min = temperature_min;
    crop.temperature_max = temperature_max;
    crop.humidity_min = humidity_min;
    crop.humidity_max = humidity_max;
    crop.plant_season = plant_season;
    crop.grow_season = grow_season;
    crop.harvest_season = harvest_season;
    crop.ticks_seed_to_sprout = ticks_seed_to_sprout;
    crop.ticks_sprout_to_growing = ticks_sprout_to_growing;
    crop.ticks_growing_to_mature = ticks_growing_to_mature;
    crop.seed_item_key = std::move(*seed_item);
    crop.crop_item_key = std::move(*crop_item);
    crop.byproduct_item_key = std::move(normalized_byproduct_item_key);
    crop.crop_min = crop_min;
    crop.crop_max = crop_max;
    crop.byproduct_count = byproduct_count;
    crop.repeat_harvest = repeat_harvest;
    crop.regrow_ticks = regrow_ticks;
    crop.stage_material_keys[0] = std::move(*seed_material);
    crop.stage_material_keys[1] = std::move(*sprout_material);
    crop.stage_material_keys[2] = std::move(*growing_material);
    crop.stage_material_keys[3] = std::move(*mature_material);
    draft->config.crop_species.push_back(std::move(crop));
}

void api_set_worldgen_crop_environment(const std::string& species_key,
                                       float fertility_sensitivity,
                                       float water_sensitivity,
                                       bool wild_spawn,
                                       float wild_density_weight,
                                       float color_r,
                                       float color_g,
                                       float color_b) {
    WorldgenScriptDraft* const draft = active_draft();
    if (!draft) return;
    CropSpeciesDef* const crop = find_crop(*draft, species_key);
    if (!crop) {
        report_binding_error(invalid_argument("Crop environment refers to an unregistered species: " +
                                              species_key));
        return;
    }
    if (!unit_interval(fertility_sensitivity) || !unit_interval(water_sensitivity) ||
        !finite(wild_density_weight) || wild_density_weight < 0.0f ||
        !unit_interval(color_r) || !unit_interval(color_g) || !unit_interval(color_b)) {
        report_binding_error(invalid_argument(
            "Crop environment values must be finite and within their supported ranges"));
        return;
    }
    crop->fertility_sensitivity = fertility_sensitivity;
    crop->water_sensitivity = water_sensitivity;
    crop->wild_spawn = wild_spawn;
    crop->wild_density_weight = wild_density_weight;
    crop->crop_color_r = color_r;
    crop->crop_color_g = color_g;
    crop->crop_color_b = color_b;
}

void api_register_worldgen_base_terrain_rule(const std::string& dimension_id,
                                             const std::string& mode,
                                             const std::string& default_material_key,
                                             const std::string& low_elevation_material_key,
                                             const std::string& high_elevation_material_key,
                                             const std::string& cave_air_material_key,
                                             float elevation_scale,
                                             int elevation_octaves,
                                             float detail_scale,
                                             int detail_octaves,
                                             float water_elevation_max,
                                             float water_detail_max,
                                             float stone_elevation_abs_min,
                                             float cave_scale,
                                             int cave_octaves,
                                             float cave_threshold,
                                             float cave_edge_threshold_add) {
    WorldgenScriptDraft* const draft = active_draft();
    if (!draft) return;
    if (dimension_id.empty() || mode.empty() || !finite(elevation_scale) ||
        !finite(detail_scale) || !finite(water_elevation_max) || !finite(water_detail_max) ||
        !finite(stone_elevation_abs_min) || !finite(cave_scale) || !finite(cave_threshold) ||
        !finite(cave_edge_threshold_add) || elevation_octaves < 0 || detail_octaves < 0 ||
        cave_octaves < 0) {
        report_binding_error(invalid_argument(
            "Base terrain rule has empty identifiers or invalid numeric properties"));
        return;
    }
    const auto duplicate = std::find_if(
        draft->pending_base_terrain_rules.begin(), draft->pending_base_terrain_rules.end(),
        [&dimension_id](const PendingBaseTerrainRule& rule) {
            return rule.rule.dimension_id == dimension_id;
        });
    if (duplicate != draft->pending_base_terrain_rules.end()) {
        report_binding_error(invalid_argument(
            "World-generation script registered a duplicate base terrain dimension: " +
            dimension_id));
        return;
    }
    auto default_key = normalize_key(default_material_key, "base terrain default material key");
    auto low_key = normalize_key(low_elevation_material_key,
                                 "base terrain low-elevation material key");
    auto high_key = normalize_key(high_elevation_material_key,
                                  "base terrain high-elevation material key");
    auto cave_key = normalize_key(cave_air_material_key, "base terrain cave-air material key");
    if (!default_key || !low_key || !high_key || !cave_key) {
        if (!default_key) report_binding_error(default_key.error());
        else if (!low_key) report_binding_error(low_key.error());
        else if (!high_key) report_binding_error(high_key.error());
        else report_binding_error(cave_key.error());
        return;
    }

    PendingBaseTerrainRule pending;
    pending.rule.dimension_id = dimension_id;
    pending.rule.mode = mode;
    pending.rule.elevation_scale = elevation_scale;
    pending.rule.elevation_octaves = elevation_octaves;
    pending.rule.detail_scale = detail_scale;
    pending.rule.detail_octaves = detail_octaves;
    pending.rule.water_elevation_max = water_elevation_max;
    pending.rule.water_detail_max = water_detail_max;
    pending.rule.stone_elevation_abs_min = stone_elevation_abs_min;
    pending.rule.cave_scale = cave_scale;
    pending.rule.cave_octaves = cave_octaves;
    pending.rule.cave_threshold = cave_threshold;
    pending.rule.cave_edge_threshold_add = cave_edge_threshold_add;
    pending.default_material_key = std::move(*default_key);
    pending.low_elevation_material_key = std::move(*low_key);
    pending.high_elevation_material_key = std::move(*high_key);
    pending.cave_air_material_key = std::move(*cave_key);
    draft->pending_base_terrain_rules.push_back(std::move(pending));
}

[[nodiscard]] snt::core::Expected<TerrainMaterialId> resolve_material_id(
    const WorldGenConfigSnapshot& config,
    std::string_view key,
    std::string_view reference_name) {
    const auto found = config.material_ids_by_key.find(std::string(key));
    if (found == config.material_ids_by_key.end()) {
        return invalid_argument("World-generation " + std::string(reference_name) +
                                " refers to an unregistered material: " + std::string(key));
    }
    return found->second;
}

[[nodiscard]] snt::core::Expected<void> finalize_semantic_references(
    WorldgenScriptDraft& draft) {
    for (const TreeSpeciesDef& tree : draft.config.tree_species) {
        for (const std::string_view key : {std::string_view{tree.wood_material_key},
                                           std::string_view{tree.leaves_material_key},
                                           std::string_view{tree.sapling_material_key}}) {
            if (auto result = resolve_material_id(draft.config, key, "tree material"); !result) {
                return result.error();
            }
        }
    }
    for (const CropSpeciesDef& crop : draft.config.crop_species) {
        for (const std::string& key : crop.stage_material_keys) {
            if (auto result = resolve_material_id(draft.config, key, "crop stage material");
                !result) {
                return result.error();
            }
        }
    }
    for (PendingBaseTerrainRule& pending : draft.pending_base_terrain_rules) {
        auto default_material = resolve_material_id(
            draft.config, pending.default_material_key, "base terrain default material");
        if (!default_material) return default_material.error();
        auto low_elevation_material = resolve_material_id(
            draft.config, pending.low_elevation_material_key, "base terrain low-elevation material");
        if (!low_elevation_material) return low_elevation_material.error();
        auto high_elevation_material = resolve_material_id(
            draft.config, pending.high_elevation_material_key, "base terrain high-elevation material");
        if (!high_elevation_material) return high_elevation_material.error();
        auto cave_air_material = resolve_material_id(
            draft.config, pending.cave_air_material_key, "base terrain cave-air material");
        if (!cave_air_material) return cave_air_material.error();
        pending.rule.default_material = *default_material;
        pending.rule.low_elevation_material = *low_elevation_material;
        pending.rule.high_elevation_material = *high_elevation_material;
        pending.rule.cave_air_material = *cave_air_material;
        draft.config.base_terrain_rules.push_back(std::move(pending.rule));
    }
    return {};
}

[[nodiscard]] snt::core::Expected<void> register_function(
    asIScriptEngine* engine,
    const char* declaration,
    const asSFuncPtr& function) {
    const int result = engine->RegisterGlobalFunction(declaration, function, asCALL_CDECL);
    if (result >= 0) return {};
    return snt::core::Error{
        snt::core::ErrorCode::kScriptEngineInitFailed,
        std::string("RegisterGlobalFunction failed for '") + declaration + "': " +
            std::to_string(result)};
}

}  // namespace

snt::core::Expected<void> register_worldgen_script_api(asIScriptEngine* engine) {
    if (!engine) {
        return invalid_argument("World-generation script API received a null script engine");
    }
    if (auto result = register_function(
            engine,
            "void snt_register_worldgen_material(const string &in, const string &in, int, float, const string &in, int, float, int, const string &in)",
            asFUNCTION(api_register_worldgen_material));
        !result) return result;
    if (auto result = register_function(
            engine,
            "void snt_add_worldgen_material_drop(const string &in, const string &in, int, int, int, float)",
            asFUNCTION(api_add_worldgen_material_drop));
        !result) return result;
    if (auto result = register_function(
            engine, "void snt_bind_worldgen_role(const string &in, const string &in)",
            asFUNCTION(api_bind_worldgen_role));
        !result) return result;
    if (auto result = register_function(
            engine,
            "void snt_bind_worldgen_runtime_material(const string &in, const string &in)",
            asFUNCTION(api_bind_worldgen_runtime_material));
        !result) return result;
    if (auto result = register_function(
            engine,
            "void snt_register_worldgen_tree(const string &in, const string &in, float, float, float, float, float, int, int, int, int, const string &in, const string &in, const string &in, bool, int64, int64, bool, const string &in, int)",
            asFUNCTION(api_register_worldgen_tree));
        !result) return result;
    if (auto result = register_function(
            engine,
            "void snt_set_worldgen_tree_colors(const string &in, float, float, float, float, float, float, float, float, float)",
            asFUNCTION(api_set_worldgen_tree_colors));
        !result) return result;
    if (auto result = register_function(
            engine,
            "void snt_register_worldgen_crop(const string &in, const string &in, int, float, float, float, float, int, int, int, int64, int64, int64, const string &in, const string &in, const string &in, int, int, int, bool, int64, const string &in, const string &in, const string &in, const string &in)",
            asFUNCTION(api_register_worldgen_crop));
        !result) return result;
    if (auto result = register_function(
            engine,
            "void snt_set_worldgen_crop_environment(const string &in, float, float, bool, float, float, float, float)",
            asFUNCTION(api_set_worldgen_crop_environment));
        !result) return result;
    if (auto result = register_function(
            engine,
            "void snt_register_worldgen_base_terrain_rule(const string &in, const string &in, const string &in, const string &in, const string &in, const string &in, float, int, float, int, float, float, float, float, int, float, float)",
            asFUNCTION(api_register_worldgen_base_terrain_rule));
        !result) return result;

    SNT_LOG_INFO("Registered world-generation AngelScript API");
    return {};
}

snt::core::Expected<std::shared_ptr<const WorldGenConfigSnapshot>>
build_worldgen_config_from_script(snt::script::ScriptManager& scripts) {
    snt::script::ScriptModule* const module = scripts.get_module(kWorldgenModuleName);
    if (!module) {
        return snt::core::Error{
            snt::core::ErrorCode::kScriptModuleNotFound,
            "World-generation module is not loaded: " + std::string(kWorldgenModuleName)};
    }

    WorldgenScriptDraft draft;
    {
        WorldgenDraftScope scope(draft);
        if (auto result = module->call_void(scripts.contexts(),
                                            "void snt_register_worldgen()");
            !result) {
            auto error = result.error();
            error.with_context("build_worldgen_config_from_script(snt_register_worldgen)");
            return error;
        }
    }

    if (auto result = finalize_world_gen_config(draft.config); !result) {
        auto error = result.error();
        error.with_context("build_worldgen_config_from_script(finalize materials)");
        return error;
    }
    if (auto result = finalize_semantic_references(draft); !result) {
        auto error = result.error();
        error.with_context("build_worldgen_config_from_script(resolve references)");
        return error;
    }
    if (draft.config.base_terrain_rules.empty()) {
        return invalid_argument(
            "World-generation script must register at least one base terrain rule");
    }

    draft.config.content_hash = hash_world_gen_config(draft.config);
    SNT_LOG_INFO("Published world-generation script snapshot: materials=%zu trees=%zu crops=%zu "
                 "base_rules=%zu hash=%llu",
                 draft.config.materials.size(), draft.config.tree_species.size(),
                 draft.config.crop_species.size(), draft.config.base_terrain_rules.size(),
                 static_cast<unsigned long long>(draft.config.content_hash));
    return std::make_shared<const WorldGenConfigSnapshot>(std::move(draft.config));
}

}  // namespace snt::game
