// ScienceAndTheology script-content registry.
//
// Ownership: ScienceAndTheologySimulationSession owns this registry and
// injects it into ScriptManager before script initialization. It owns all
// game definitions, script-persistent transient state, and native `snt_*`
// registrations.
//
// Lifecycle: ScriptManager calls the IScriptContentHost transaction methods
// during module load, reload, unload, and shutdown. Thread affinity: every
// method is main-thread-only; worker systems receive only copied definitions.
//
// The engine sees only IScriptContentHost. Recipes, machines, quests, event
// names, item identifiers, and callback conventions are game-owned data.

#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "game/chemistry/element_catalog.h"
#include "game/resources/resource_runtime_index.h"
#include "game/simulation/machine_placement_registry.h"
#include "script/content_host.h"

namespace snt::game {

using ScriptId = snt::script::ScriptId;
inline constexpr ScriptId kBuiltinScriptId = snt::script::kBuiltinScriptId;
// Material, item-presentation, and tool definitions are game content rather
// than engine state. The registry owns their script-reload transaction so a
// worker snapshot never observes generated material forms from a partial
// material catalog.
enum class GameMaterialState : uint8_t {
    kSolid = 0,
    kLiquid = 1,
    kGas = 2,
    kPlasma = 3,
};

enum class GameMaterialGenerationFlag : uint16_t {
    kDust = 1u << 0,
    kMetal = 1u << 1,
    kGem = 1u << 2,
    kOre = 1u << 3,
    kCell = 1u << 4,
    kPlasma = 1u << 5,
    kWire = 1u << 6,
    kBlock = 1u << 7,
    kPlate = 1u << 8,
    kRod = 1u << 9,
};

inline constexpr bool has_material_generation_flag(
    uint16_t flags, GameMaterialGenerationFlag flag) noexcept {
    return (flags & static_cast<uint16_t>(flag)) != 0;
}

// The numeric values intentionally match the legacy material-form catalog.
// They are content data, never persistent item IDs.
enum class GameMaterialForm : uint8_t {
    kDust = 0,
    kTinyDust,
    kSmallDust,
    kImpureDust,
    kPurifiedDust,
    kCrushed,
    kCrushedPurified,
    kCrushedCentrifuged,
    kGem,
    kFlawedGem,
    kFlawlessGem,
    kExquisiteGem,
    kIngot,
    kHotIngot,
    kNugget,
    kBlock,
    kPlate,
    kDoublePlate,
    kDensePlate,
    kRod,
    kLongRod,
    kBolt,
    kScrew,
    kRing,
    kRotor,
    kGear,
    kSmallGear,
    kFineWire,
    kWire,
    kCell,
    kPlasmaCell,
    kCount,
};

enum class GameItemCategory : uint8_t {
    kMaterials = 0,
    kTools,
    kComponents,
    kPlaceables,
    kResources,
    kFood,
    kMisc,
};

enum class GameToolType : uint8_t {
    kNone = 0,
    kPickaxe,
    kAxe,
    kShovel,
    kSword,
    kHoe,
    kKnife,
    kSpear,
};

struct GameMaterialElement {
    // The script boundary accepts a symbol, then resolves it once to this
    // canonical atomic-number ID. Runtime content never stores that string.
    chemistry::ElementId element;
    uint8_t count = 0;
};

// `icon_path` and `icon_overlay_path` are paths relative to
// `game/resource/items`. The retained-MUI client maps them to its image
// registry; the simulation and server only carry these values as content.
struct GameItemPresentation {
    GameItemCategory category = GameItemCategory::kMisc;
    std::string icon_path;
    std::string icon_overlay_path;
    uint32_t tint_rgb = 0xffffffu;
    bool uses_tint = false;
};

struct GameMaterialFormPresentation {
    std::string title_key;
    int32_t max_stack = 64;
    GameItemPresentation presentation{
        .category = GameItemCategory::kMaterials,
    };
};

struct GameMaterialDefinition {
    std::string id;
    std::string title_key;
    uint16_t generation_flags = 0;
    GameMaterialState state = GameMaterialState::kSolid;
    uint32_t color_rgb = 0xffffffu;
    int64_t melting_point_kelvin = 0;
    int64_t boiling_point_kelvin = 0;
    float mass = 0.0f;
    std::string chemical_formula;
    std::vector<GameMaterialElement> composition;
    std::map<GameMaterialForm, GameMaterialFormPresentation> form_presentations;
};

struct GameMaterialFormReference {
    std::string material_id;
    GameMaterialForm form = GameMaterialForm::kCount;
    int64_t material_units = 0;
};

struct GameToolDefinition {
    GameToolType type = GameToolType::kNone;
    int32_t mining_level = 0;
    std::string material_key;
    float speed = 1.0f;
    int32_t durability = 0;
    float attack_damage = 1.0f;
};

// Game item definitions own stable semantic keys, presentation metadata, and
// optional material/tool behavior. Durable game state refers to `id`; worker
// systems capture the immutable ResourceRuntimeIndex snapshot and use
// RuntimeResourceKey for their hot paths.
struct GameItemDefinition {
    std::string id;
    std::string title_key;
    int32_t max_stack = 64;
    GameItemPresentation presentation;
    std::optional<GameToolDefinition> tool;
    std::vector<std::string> tool_tags;
    std::optional<GameMaterialFormReference> material_form;
};

struct RecipeOutputDefinition {
    std::string item_id;
    int32_t count = 1;
};

struct RecipeInputDefinition {
    std::string item_id;
    int32_t count = 1;
};

// Content-owned prerequisites for a manual machine start. The authoritative
// player-command layer resolves these against world, inventory, and tool
// state, then passes a value-only activation context to game simulation.
struct MachineActivationRequirements {
    bool requires_cover = false;
    bool requires_ignition = false;
    bool requires_valid_structure = false;
    std::string required_tool_tag;

    [[nodiscard]] bool empty() const noexcept {
        return !requires_cover && !requires_ignition &&
               !requires_valid_structure && required_tool_tag.empty();
    }
};

// Controls whether a machine can continue advancing after its owning chunk
// leaves the active simulation set. NetworkIsland is declared now so content
// can express the intended policy before the pipe/cable topology producer is
// implemented; the runtime conservatively pauses that mode until then.
enum class MachineOfflineSimulationMode : uint8_t {
    kDisabled = 0,
    kStandalone = 1,
    kNetworkIsland = 2,
};

struct MachineOfflineSimulationProfile {
    MachineOfflineSimulationMode mode = MachineOfflineSimulationMode::kDisabled;
    // The offline scheduler advances a standalone machine in bounded batches
    // instead of retaining a per-machine fixed-tick task while no player is nearby.
    uint32_t max_batch_ticks = 1200;
    // Manual machines still require a player command to begin a new job. The
    // flag applies to automatic machines and is reserved for future networks.
    bool can_start_new_jobs = true;
};

struct RecipeDefinition {
    std::string id;
    std::string machine_id;
    std::vector<RecipeInputDefinition> inputs;
    std::vector<RecipeOutputDefinition> outputs;
    int32_t duration_ticks = 200;
    int32_t energy_per_tick = 0;
    std::string tag;
};

struct MachineDefinition {
    std::string id;
    std::string display_name;
    int32_t tier = 1;
    int32_t power_capacity = 0;
    std::vector<std::string> recipe_types;
    // Manual machines wait for an explicit gameplay command after their
    // input slots match a recipe. The command boundary owns cover, light,
    // tools, and structure validation; worker ticks only consume this flag.
    bool requires_manual_activation = false;
    MachineActivationRequirements activation_requirements;
    MachineOfflineSimulationProfile offline_simulation;
};

// Quest objectives are content data, while their counters and state belong to
// QuestRegistry. The explicit type prevents gameplay systems from parsing
// ad-hoc objective strings and gives P7.4 a stable progress-key boundary.
enum class QuestObjectiveKind : uint8_t {
    kAcquireItem,
    kCraftItem,
    kMineBlock,
    kPlaceMachine,
    kReachTick,
    kCustomEvent,
};

struct QuestObjectiveDefinition {
    std::string id;
    QuestObjectiveKind kind = QuestObjectiveKind::kCraftItem;
    std::string target_id;
    int32_t required_count = 1;
};

// Reward values remain gameplay content, but their effect is committed only
// by the authoritative quest reward service after a player explicitly claims
// a completed quest. `target_id` is an item id for kItem and a quest id for
// kUnlockQuest; the explicit kind keeps scripts from encoding a mini-language
// into an opaque string.
enum class QuestRewardKind : uint8_t {
    kItem,
    kUnlockQuest,
};

struct QuestRewardDefinition {
    QuestRewardKind kind = QuestRewardKind::kItem;
    std::string target_id;
    int32_t count = 1;
};

// Quest-book presentation data is authored alongside gameplay definitions so
// a client can render the same chapter graph without receiving mutable quest
// objects from the server. Coordinates use finite logical world units inside
// the retained PanZoomView, never window pixels.
struct QuestBookChapterDefinition {
    std::string id;
    std::string title;
    std::string description;
    std::string icon_key;
    int32_t sort_order = 0;
};

struct QuestBookNodePosition {
    float x = 0.0f;
    float y = 0.0f;
};

struct QuestDefinition {
    std::string id;
    std::string chapter_id = "main";
    std::string title;
    std::string description;
    std::string icon_key;
    QuestBookNodePosition node_position;
    std::vector<std::string> prerequisites;
    std::vector<QuestObjectiveDefinition> objectives;
    std::vector<QuestRewardDefinition> rewards;
    bool hidden = false;
    bool repeatable = false;
};

struct EventListener {
    ScriptId script_id = kBuiltinScriptId;
    std::string event_name;
    std::string callback_id;

    friend bool operator==(const EventListener&, const EventListener&) = default;
};

class GameContentRegistry final : public snt::script::IScriptContentHost {
public:
    GameContentRegistry() = default;
    ~GameContentRegistry() override = default;

    GameContentRegistry(const GameContentRegistry&) = delete;
    GameContentRegistry& operator=(const GameContentRegistry&) = delete;

    // IScriptContentHost: ScriptManager calls these only on the main thread.
    snt::core::Expected<void> register_script_api(asIScriptEngine* engine) override;
    std::unique_ptr<snt::script::IScriptRegistrationScope> begin_registration(
        ScriptId script_id) override;
    snt::core::Expected<void> begin_reload(ScriptId script_id) override;
    snt::core::Expected<void> commit_reload(ScriptId script_id) override;
    snt::core::Expected<void> rollback_reload(ScriptId script_id) override;
    snt::core::Expected<void> begin_reload_batch(
        std::span<const ScriptId> script_ids) override;
    snt::core::Expected<void> commit_reload_batch(
        std::span<const ScriptId> script_ids) override;
    snt::core::Expected<void> rollback_reload_batch(
        std::span<const ScriptId> script_ids) override;
    snt::core::Expected<void> unload_script(ScriptId script_id) override;
    std::vector<std::string> callback_ids_for_script(ScriptId script_id) const override;
    void reset() override;

    // Built-in registrations are fallback values restored when a script
    // override unloads. Materials are batched so one immutable catalog causes
    // one generated-form rebuild and one runtime-index publication.
    snt::core::Expected<void> register_builtin_materials(
        std::span<const GameMaterialDefinition> definitions);
    snt::core::Expected<void> register_builtin_item(GameItemDefinition definition);
    snt::core::Expected<void> register_builtin_recipe(RecipeDefinition definition);
    snt::core::Expected<void> register_builtin_machine(MachineDefinition definition);
    snt::core::Expected<void> register_builtin_machine_placement(
        MachinePlacementDefinition definition);
    snt::core::Expected<void> register_builtin_quest_chapter(
        QuestBookChapterDefinition definition);
    snt::core::Expected<void> register_builtin_quest(QuestDefinition definition);

    // Script definitions may shadow built-ins but cannot race another script
    // for the same id. Values are copied so no AngelScript memory is retained.
    snt::core::Expected<void> register_script_material(
        ScriptId script_id, GameMaterialDefinition definition);
    snt::core::Expected<void> register_script_item(ScriptId script_id,
                                                   GameItemDefinition definition);
    snt::core::Expected<void> add_script_material_element(
        ScriptId script_id, std::string material_id, GameMaterialElement element);
    snt::core::Expected<void> set_script_material_form_presentation(
        ScriptId script_id, std::string material_id, GameMaterialForm form,
        GameMaterialFormPresentation presentation);
    snt::core::Expected<void> set_script_item_presentation(
        ScriptId script_id, std::string item_id, GameItemPresentation presentation);
    snt::core::Expected<void> set_script_item_tool(
        ScriptId script_id, std::string item_id, GameToolDefinition tool);
    snt::core::Expected<void> add_script_item_tool_tag(
        ScriptId script_id, std::string item_id, std::string tool_tag);
    snt::core::Expected<void> register_script_recipe(ScriptId script_id,
                                                     RecipeDefinition definition);
    snt::core::Expected<void> register_script_machine(ScriptId script_id,
                                                      MachineDefinition definition);
    snt::core::Expected<void> register_script_machine_placement(
        ScriptId script_id, MachinePlacementDefinition definition);
    snt::core::Expected<void> register_script_quest_chapter(
        ScriptId script_id, QuestBookChapterDefinition definition);
    snt::core::Expected<void> register_script_quest(ScriptId script_id,
                                                    QuestDefinition definition);
    snt::core::Expected<void> add_script_quest_prerequisite(
        ScriptId script_id, std::string quest_id, std::string prerequisite_id);
    snt::core::Expected<void> add_script_recipe_input(
        ScriptId script_id, std::string recipe_id, RecipeInputDefinition input);
    snt::core::Expected<void> set_script_machine_activation_requirements(
        ScriptId script_id, std::string machine_id,
        MachineActivationRequirements requirements);
    snt::core::Expected<void> set_script_machine_offline_simulation(
        ScriptId script_id, std::string machine_id,
        MachineOfflineSimulationProfile profile);
    snt::core::Expected<void> add_script_quest_objective(
        ScriptId script_id, std::string quest_id, QuestObjectiveDefinition objective);
    snt::core::Expected<void> add_script_quest_reward(
        ScriptId script_id, std::string quest_id, QuestRewardDefinition reward);

    const GameItemDefinition* find_item(std::string_view id) const;
    const GameMaterialDefinition* find_material(std::string_view id) const;
    [[nodiscard]] std::optional<RuntimeResourceKey> find_resource_runtime_key(
        const ResourceKey& key) const;
    [[nodiscard]] std::optional<ResourceKey> find_resource_key(
        const RuntimeResourceKey& key) const;
    [[nodiscard]] ResourceRuntimeIndex::Snapshot resource_runtime_index() const noexcept;
    [[nodiscard]] uint64_t resource_runtime_generation() const noexcept;
    [[nodiscard]] std::vector<GameItemDefinition> item_definitions() const;
    [[nodiscard]] std::vector<GameMaterialDefinition> material_definitions() const;
    // The content revision changes for presentation, material, generated-form,
    // or authored-item changes. Retained MUI uses it to rebuild only when
    // hot-reloaded content can visibly change a displayed slot.
    [[nodiscard]] uint64_t item_content_revision() const noexcept {
        return item_content_revision_;
    }
    [[nodiscard]] std::vector<std::string> tool_tags_for_item(
        std::string_view item_id) const;
    [[nodiscard]] bool item_matches_tool_requirement(
        std::string_view item_id, std::string_view required_tag,
        int32_t required_mining_level) const;

    const RecipeDefinition* find_recipe(std::string_view id) const;
    const MachineDefinition* find_machine(std::string_view id) const;
    const MachinePlacementDefinition* find_machine_placement_by_item(
        std::string_view item_id) const noexcept;
    const MachinePlacementDefinition* find_machine_placement_by_material_key(
        std::string_view material_key) const noexcept;
    [[nodiscard]] std::vector<MachinePlacementDefinition> machine_placement_definitions() const;
    // Machine placement data intentionally lives in its own registry. This
    // validates its references once all current content has been registered.
    [[nodiscard]] snt::core::Expected<void> validate_machine_placement_references() const;
    const QuestBookChapterDefinition* find_quest_chapter(std::string_view id) const;
    const QuestDefinition* find_quest(std::string_view id) const;
    std::vector<RecipeDefinition> recipes_for_machine(std::string_view machine_id) const;
    std::vector<QuestBookChapterDefinition> quest_chapter_definitions() const;
    std::vector<QuestDefinition> quest_definitions() const;
    [[nodiscard]] uint64_t quest_content_revision() const noexcept {
        return quest_content_revision_;
    }
    // Stable semantic fingerprint for the task-book content contract. It is
    // independent of script owner IDs and registration order so a client can
    // reject a server progress snapshot from a different content package.
    [[nodiscard]] uint64_t quest_content_fingerprint() const noexcept;

    snt::core::Expected<void> add_event_listener(EventListener listener);
    std::vector<EventListener> event_listeners(std::string_view event_name) const;

    // State survives a successful or failed script reload, but never survives
    // a game session. It is not a save-game API.
    snt::core::Expected<void> set_state(ScriptId script_id,
                                        std::string key,
                                        std::string value);
    std::optional<std::string> get_state(ScriptId script_id,
                                         std::string_view key) const;

private:
    template <typename Definition>
    struct OwnedDefinition {
        ScriptId owner = kBuiltinScriptId;
        Definition definition;
    };

    using ItemMap = std::map<std::string, OwnedDefinition<GameItemDefinition>, std::less<>>;
    using MaterialMap = std::map<std::string,
                                 OwnedDefinition<GameMaterialDefinition>, std::less<>>;
    using RecipeMap = std::map<std::string, OwnedDefinition<RecipeDefinition>, std::less<>>;
    using MachineMap = std::map<std::string, OwnedDefinition<MachineDefinition>, std::less<>>;
    using QuestChapterMap = std::map<std::string,
                                     OwnedDefinition<QuestBookChapterDefinition>, std::less<>>;
    using QuestMap = std::map<std::string, OwnedDefinition<QuestDefinition>, std::less<>>;

    struct ReloadSnapshot {
        MaterialMap materials;
        ItemMap items;
        RecipeMap recipes;
        MachineMap machines;
        QuestChapterMap quest_chapters;
        QuestMap quests;
        std::vector<EventListener> event_listeners;
        ResourceRuntimeIndex::Snapshot resource_runtime_index;
        uint64_t item_content_revision = 1;
    };

    struct ReloadBatch {
        std::vector<ScriptId> script_ids;
        uint64_t quest_content_revision = 1;
    };

    snt::core::Expected<void> register_material(ScriptId owner,
                                                GameMaterialDefinition definition,
                                                bool builtin);
    snt::core::Expected<void> register_item(ScriptId owner,
                                            GameItemDefinition definition,
                                            bool builtin);
    snt::core::Expected<void> register_recipe(ScriptId owner,
                                              RecipeDefinition definition,
                                              bool builtin);
    snt::core::Expected<void> register_machine(ScriptId owner,
                                               MachineDefinition definition,
                                               bool builtin);
    snt::core::Expected<void> register_quest_chapter(
        ScriptId owner, QuestBookChapterDefinition definition, bool builtin);
    snt::core::Expected<void> register_quest(ScriptId owner,
                                             QuestDefinition definition,
                                             bool builtin);

    static snt::core::Expected<void> validate(const GameMaterialDefinition& definition);
    static snt::core::Expected<void> validate(const GameMaterialElement& element);
    static snt::core::Expected<void> validate(const GameItemPresentation& presentation);
    static snt::core::Expected<void> validate(const GameMaterialFormPresentation& presentation);
    static snt::core::Expected<void> validate(const GameToolDefinition& definition);
    static snt::core::Expected<void> validate(const GameItemDefinition& definition);
    static snt::core::Expected<void> validate(const RecipeDefinition& definition);
    static snt::core::Expected<void> validate(const RecipeInputDefinition& input);
    static snt::core::Expected<void> validate(
        const MachineActivationRequirements& requirements);
    static snt::core::Expected<void> validate(const MachineDefinition& definition);
    static snt::core::Expected<void> validate(const QuestBookChapterDefinition& definition);
    static snt::core::Expected<void> validate(const QuestDefinition& definition);
    static snt::core::Expected<void> validate(const QuestObjectiveDefinition& objective);
    static snt::core::Expected<void> validate(const QuestRewardDefinition& reward);
    static snt::core::Expected<void> validate(const EventListener& listener);
    static snt::core::Expected<std::string> normalize_item_key(std::string_view key);

    snt::core::Expected<void> rebuild_generated_material_items();
    snt::core::Expected<void> publish_resource_runtime_index();
    snt::core::Expected<void> validate_machine_item_references() const;

    snt::core::Expected<void> erase_script_content(ScriptId script_id);
    ReloadSnapshot snapshot_script_content(ScriptId script_id) const;
    snt::core::Expected<void> restore_script_content(const ReloadSnapshot& snapshot);
    [[nodiscard]] bool matches_active_reload_batch(
        std::span<const ScriptId> script_ids) const noexcept;
    void sort_event_listeners(std::string_view event_name);

    // `backup_*` stores built-ins. `live_*` is the effective game definition
    // after an optional script override. Ordered maps make iteration stable.
    MaterialMap backup_materials_;
    ItemMap backup_items_;
    RecipeMap backup_recipes_;
    MachineMap backup_machines_;
    QuestChapterMap backup_quest_chapters_;
    QuestMap backup_quests_;
    MaterialMap live_materials_;
    ItemMap live_items_;
    // Rebuilt from `live_materials_`; it is deliberately not script-owned
    // state and therefore is reconstructed after every material transition.
    ItemMap live_generated_material_items_;
    RecipeMap live_recipes_;
    MachineMap live_machines_;
    QuestChapterMap live_quest_chapters_;
    QuestMap live_quests_;
    MachinePlacementRegistry machine_placements_;
    std::map<std::string, std::vector<EventListener>, std::less<>> event_listeners_;
    std::map<ScriptId, std::map<std::string, std::string, std::less<>>> state_store_;
    std::map<ScriptId, ReloadSnapshot> reloads_;
    std::optional<ReloadBatch> reload_batch_;
    ResourceRuntimeIndex resource_runtime_index_;
    uint64_t item_content_revision_ = 1;
    uint64_t quest_content_revision_ = 1;
};

}  // namespace snt::game
