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
#include <string>
#include <string_view>
#include <vector>

#include "script/content_host.h"

namespace snt::game {

using ScriptId = snt::script::ScriptId;
inline constexpr ScriptId kBuiltinScriptId = snt::script::kBuiltinScriptId;

struct RecipeOutputDefinition {
    std::string item_id;
    int32_t count = 1;
};

struct RecipeDefinition {
    std::string id;
    std::string machine_id;
    std::string input_item_id;
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

struct QuestDefinition {
    std::string id;
    std::string title;
    std::string description;
    std::vector<std::string> prerequisites;
    std::vector<QuestObjectiveDefinition> objectives;
    std::vector<std::string> rewards;
    bool hidden = false;
    bool repeatable = false;
    bool auto_start = false;
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
    snt::core::Expected<void> unload_script(ScriptId script_id) override;
    std::vector<std::string> callback_ids_for_script(ScriptId script_id) const override;
    void reset() override;

    // Built-in registrations are fallback values restored when a script
    // override unloads. Game bootstrapping code may use them before scripts.
    snt::core::Expected<void> register_builtin_recipe(RecipeDefinition definition);
    snt::core::Expected<void> register_builtin_machine(MachineDefinition definition);
    snt::core::Expected<void> register_builtin_quest(QuestDefinition definition);

    // Script definitions may shadow built-ins but cannot race another script
    // for the same id. Values are copied so no AngelScript memory is retained.
    snt::core::Expected<void> register_script_recipe(ScriptId script_id,
                                                     RecipeDefinition definition);
    snt::core::Expected<void> register_script_machine(ScriptId script_id,
                                                      MachineDefinition definition);
    snt::core::Expected<void> register_script_quest(ScriptId script_id,
                                                    QuestDefinition definition);
    snt::core::Expected<void> add_script_quest_prerequisite(
        ScriptId script_id, std::string quest_id, std::string prerequisite_id);
    snt::core::Expected<void> add_script_quest_objective(
        ScriptId script_id, std::string quest_id, QuestObjectiveDefinition objective);

    const RecipeDefinition* find_recipe(std::string_view id) const;
    const MachineDefinition* find_machine(std::string_view id) const;
    const QuestDefinition* find_quest(std::string_view id) const;
    std::vector<RecipeDefinition> recipes_for_machine(std::string_view machine_id) const;
    std::vector<QuestDefinition> quest_definitions() const;
    [[nodiscard]] uint64_t quest_content_revision() const noexcept {
        return quest_content_revision_;
    }

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

    using RecipeMap = std::map<std::string, OwnedDefinition<RecipeDefinition>, std::less<>>;
    using MachineMap = std::map<std::string, OwnedDefinition<MachineDefinition>, std::less<>>;
    using QuestMap = std::map<std::string, OwnedDefinition<QuestDefinition>, std::less<>>;

    struct ReloadSnapshot {
        RecipeMap recipes;
        MachineMap machines;
        QuestMap quests;
        std::vector<EventListener> event_listeners;
    };

    snt::core::Expected<void> register_recipe(ScriptId owner,
                                              RecipeDefinition definition,
                                              bool builtin);
    snt::core::Expected<void> register_machine(ScriptId owner,
                                               MachineDefinition definition,
                                               bool builtin);
    snt::core::Expected<void> register_quest(ScriptId owner,
                                             QuestDefinition definition,
                                             bool builtin);

    static snt::core::Expected<void> validate(const RecipeDefinition& definition);
    static snt::core::Expected<void> validate(const MachineDefinition& definition);
    static snt::core::Expected<void> validate(const QuestDefinition& definition);
    static snt::core::Expected<void> validate(const QuestObjectiveDefinition& objective);
    static snt::core::Expected<void> validate(const EventListener& listener);

    void erase_script_content(ScriptId script_id);
    ReloadSnapshot snapshot_script_content(ScriptId script_id) const;
    void restore_script_content(const ReloadSnapshot& snapshot);
    void sort_event_listeners(std::string_view event_name);

    // `backup_*` stores built-ins. `live_*` is the effective game definition
    // after an optional script override. Ordered maps make iteration stable.
    RecipeMap backup_recipes_;
    MachineMap backup_machines_;
    QuestMap backup_quests_;
    RecipeMap live_recipes_;
    MachineMap live_machines_;
    QuestMap live_quests_;
    std::map<std::string, std::vector<EventListener>, std::less<>> event_listeners_;
    std::map<ScriptId, std::map<std::string, std::string, std::less<>>> state_store_;
    std::map<ScriptId, ReloadSnapshot> reloads_;
    uint64_t quest_content_revision_ = 1;
};

}  // namespace snt::game
