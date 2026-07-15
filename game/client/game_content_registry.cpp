// ScienceAndTheology script-content registry implementation.

#define SNT_LOG_CHANNEL "gameplay"
#include "game_content_registry.h"

#include <algorithm>
#include <utility>

#include <angelscript.h>

#include "core/error.h"
#include "core/log.h"

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

void api_register_recipe(const std::string& id,
                         const std::string& machine_id,
                         const std::string& input_item_id,
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
    definition.input_item_id = input_item_id;
    definition.outputs = {RecipeOutputDefinition{output_item_id, output_count}};
    definition.duration_ticks = duration_ticks;
    definition.energy_per_tick = energy_per_tick;
    definition.tag = tag;
    if (auto result = registry->register_script_recipe(g_active_script_id, std::move(definition)); !result) {
        report_binding_error(result.error());
    }
}

void api_register_machine(const std::string& id,
                          const std::string& display_name,
                          int tier,
                          int power_capacity) {
    GameContentRegistry* registry = active_registry();
    if (!registry) return;

    MachineDefinition definition;
    definition.id = id;
    definition.display_name = display_name;
    definition.tier = tier;
    definition.power_capacity = power_capacity;
    if (auto result = registry->register_script_machine(g_active_script_id, std::move(definition)); !result) {
        report_binding_error(result.error());
    }
}

void api_register_quest(const std::string& id,
                        const std::string& title,
                        const std::string& description) {
    GameContentRegistry* registry = active_registry();
    if (!registry) return;

    QuestDefinition definition;
    definition.id = id;
    definition.title = title;
    definition.description = description;
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

    if (auto result = register_function(
            engine,
            "void snt_register_recipe(const string &in, const string &in, const string &in, const string &in, int, int, int, const string &in)",
            asFUNCTION(api_register_recipe)); !result) return result;
    if (auto result = register_function(
            engine,
            "void snt_register_machine(const string &in, const string &in, int, int)",
            asFUNCTION(api_register_machine)); !result) return result;
    if (auto result = register_function(
            engine,
            "void snt_register_quest(const string &in, const string &in, const string &in)",
            asFUNCTION(api_register_quest)); !result) return result;
    if (auto result = register_function(
            engine, "void snt_add_quest_prerequisite(const string &in, const string &in)",
            asFUNCTION(api_add_quest_prerequisite)); !result) return result;
    if (auto result = register_function(
            engine,
            "void snt_add_quest_objective(const string &in, const string &in, const string &in, const string &in, int)",
            asFUNCTION(api_add_quest_objective)); !result) return result;
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

    SNT_LOG_INFO("Registered ScienceAndTheology gameplay Script API");
    return {};
}

std::unique_ptr<snt::script::IScriptRegistrationScope>
GameContentRegistry::begin_registration(ScriptId script_id) {
    return std::make_unique<GameContentRegistrationScope>(*this, script_id);
}

snt::core::Expected<void> GameContentRegistry::validate(const RecipeDefinition& definition) {
    if (definition.id.empty()) return invalid_argument("Recipe id must not be empty");
    if (definition.machine_id.empty()) return invalid_argument("Recipe machine_id must not be empty");
    if (definition.input_item_id.empty()) return invalid_argument("Recipe input_item_id must not be empty");
    if (definition.outputs.empty()) return invalid_argument("Recipe must have at least one output");
    if (definition.duration_ticks <= 0) return invalid_argument("Recipe duration_ticks must be positive");
    if (definition.energy_per_tick < 0) return invalid_argument("Recipe energy_per_tick must not be negative");
    for (const auto& output : definition.outputs) {
        if (output.item_id.empty() || output.count <= 0) {
            return invalid_argument("Recipe output item_id must not be empty and count must be positive");
        }
    }
    return {};
}

snt::core::Expected<void> GameContentRegistry::validate(const MachineDefinition& definition) {
    if (definition.id.empty()) return invalid_argument("Machine id must not be empty");
    if (definition.display_name.empty()) return invalid_argument("Machine display_name must not be empty");
    if (definition.tier <= 0) return invalid_argument("Machine tier must be positive");
    if (definition.power_capacity < 0) return invalid_argument("Machine power_capacity must not be negative");
    return {};
}

snt::core::Expected<void> GameContentRegistry::validate(const QuestDefinition& definition) {
    if (definition.id.empty()) return invalid_argument("Quest id must not be empty");
    if (definition.title.empty()) return invalid_argument("Quest title must not be empty");
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

snt::core::Expected<void> GameContentRegistry::validate(const EventListener& listener) {
    if (listener.script_id == kBuiltinScriptId) {
        return invalid_argument("Event listener must have a non-builtin ScriptId");
    }
    if (listener.event_name.empty()) return invalid_argument("Event name must not be empty");
    if (listener.callback_id.empty()) return invalid_argument("Event callback_id must not be empty");
    return {};
}

snt::core::Expected<void> GameContentRegistry::register_builtin_recipe(RecipeDefinition definition) {
    return register_recipe(kBuiltinScriptId, std::move(definition), true);
}

snt::core::Expected<void> GameContentRegistry::register_builtin_machine(MachineDefinition definition) {
    return register_machine(kBuiltinScriptId, std::move(definition), true);
}

snt::core::Expected<void> GameContentRegistry::register_builtin_quest(QuestDefinition definition) {
    return register_quest(kBuiltinScriptId, std::move(definition), true);
}

snt::core::Expected<void> GameContentRegistry::register_script_recipe(
    ScriptId script_id, RecipeDefinition definition) {
    return register_recipe(script_id, std::move(definition), false);
}

snt::core::Expected<void> GameContentRegistry::register_script_machine(
    ScriptId script_id, MachineDefinition definition) {
    return register_machine(script_id, std::move(definition), false);
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

const RecipeDefinition* GameContentRegistry::find_recipe(std::string_view id) const {
    auto it = live_recipes_.find(id);
    return it == live_recipes_.end() ? nullptr : &it->second.definition;
}

const MachineDefinition* GameContentRegistry::find_machine(std::string_view id) const {
    auto it = live_machines_.find(id);
    return it == live_machines_.end() ? nullptr : &it->second.definition;
}

const QuestDefinition* GameContentRegistry::find_quest(std::string_view id) const {
    auto it = live_quests_.find(id);
    return it == live_quests_.end() ? nullptr : &it->second.definition;
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

std::vector<RecipeDefinition> GameContentRegistry::recipes_for_machine(
    std::string_view machine_id) const {
    std::vector<RecipeDefinition> result;
    for (const auto& [id, entry] : live_recipes_) {
        (void)id;
        if (entry.definition.machine_id == machine_id) result.push_back(entry.definition);
    }
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
    if (script_id == kBuiltinScriptId) return invalid_argument("Built-in content cannot be reloaded as a script");
    if (reloads_.contains(script_id)) return invalid_state("Reload is already active for script");

    reloads_.emplace(script_id, snapshot_script_content(script_id));
    erase_script_content(script_id);
    SNT_LOG_DEBUG("Game content began transactional reload for script %llu",
                  static_cast<unsigned long long>(script_id));
    return {};
}

snt::core::Expected<void> GameContentRegistry::commit_reload(ScriptId script_id) {
    auto it = reloads_.find(script_id);
    if (it == reloads_.end()) return invalid_state("No active reload for script");
    reloads_.erase(it);
    SNT_LOG_INFO("Game content committed reload for script %llu",
                 static_cast<unsigned long long>(script_id));
    return {};
}

snt::core::Expected<void> GameContentRegistry::rollback_reload(ScriptId script_id) {
    auto it = reloads_.find(script_id);
    if (it == reloads_.end()) return invalid_state("No active reload for script");

    erase_script_content(script_id);
    restore_script_content(it->second);
    reloads_.erase(it);
    SNT_LOG_WARN("Game content rolled back reload for script %llu",
                 static_cast<unsigned long long>(script_id));
    return {};
}

snt::core::Expected<void> GameContentRegistry::unload_script(ScriptId script_id) {
    if (script_id == kBuiltinScriptId) return invalid_argument("Built-in content cannot be unloaded as a script");
    if (reloads_.contains(script_id)) return invalid_state("Cannot unload a script during its active reload");
    erase_script_content(script_id);
    SNT_LOG_INFO("Game content unloaded script %llu",
                 static_cast<unsigned long long>(script_id));
    return {};
}

void GameContentRegistry::reset() {
    const bool had_quests = !backup_quests_.empty() || !live_quests_.empty();
    backup_recipes_.clear();
    backup_machines_.clear();
    backup_quests_.clear();
    live_recipes_.clear();
    live_machines_.clear();
    live_quests_.clear();
    event_listeners_.clear();
    state_store_.clear();
    reloads_.clear();
    if (had_quests) ++quest_content_revision_;
}

GameContentRegistry::ReloadSnapshot GameContentRegistry::snapshot_script_content(
    ScriptId script_id) const {
    ReloadSnapshot snapshot;
    for (const auto& [id, entry] : live_recipes_) {
        if (entry.owner == script_id) snapshot.recipes.emplace(id, entry);
    }
    for (const auto& [id, entry] : live_machines_) {
        if (entry.owner == script_id) snapshot.machines.emplace(id, entry);
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

void GameContentRegistry::erase_script_content(ScriptId script_id) {
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
    bool quests_changed = false;
    for (auto it = live_quests_.begin(); it != live_quests_.end();) {
        if (it->second.owner != script_id) {
            ++it;
            continue;
        }
        quests_changed = true;
        auto backup = backup_quests_.find(it->first);
        if (backup == backup_quests_.end()) it = live_quests_.erase(it);
        else {
            it->second = backup->second;
            ++it;
        }
    }
    if (quests_changed) ++quest_content_revision_;
    for (auto events = event_listeners_.begin(); events != event_listeners_.end();) {
        auto& listeners = events->second;
        std::erase_if(listeners, [script_id](const EventListener& listener) {
            return listener.script_id == script_id;
        });
        if (listeners.empty()) events = event_listeners_.erase(events);
        else ++events;
    }
}

void GameContentRegistry::restore_script_content(const ReloadSnapshot& snapshot) {
    for (const auto& [id, entry] : snapshot.recipes) live_recipes_[id] = entry;
    for (const auto& [id, entry] : snapshot.machines) live_machines_[id] = entry;
    for (const auto& [id, entry] : snapshot.quests) live_quests_[id] = entry;
    if (!snapshot.quests.empty()) ++quest_content_revision_;
    for (const auto& listener : snapshot.event_listeners) {
        event_listeners_[listener.event_name].push_back(listener);
        sort_event_listeners(listener.event_name);
    }
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
