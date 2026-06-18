#include "quest/gd_quest_system.hpp"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/callable.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <sstream>

using namespace godot;

namespace science_and_theology {

// ============================================================
// Construction / destruction
// ============================================================

GDQuestSystem::GDQuestSystem() = default;

GDQuestSystem::~GDQuestSystem() {
    unsubscribe_from_events();
}

// ============================================================
// Godot lifecycle
// ============================================================

void GDQuestSystem::_ready() {
    // Nothing to do; content registration happens from GDScript.
}

void GDQuestSystem::_process(double delta) {
    quest_system_.tick(static_cast<float>(delta), tick_counter_);
}

// ============================================================
// Content registration
// ============================================================

void GDQuestSystem::register_chapter(const String& chapter_id,
                                     const String& title,
                                     const String& icon_key,
                                     int64_t order_index) {
    ChapterDef def;
    def.chapter_id = chapter_id.utf8().get_data();
    def.title_key = title.utf8().get_data();
    def.icon_key = icon_key.utf8().get_data();
    def.order_index = static_cast<int32_t>(order_index);
    quest_system_.register_chapter(def);
}

void GDQuestSystem::register_quest(const Dictionary& quest_data) {
    QuestDef def;

    // Required fields.
    def.quest_id = String(quest_data.get("quest_id", "")).utf8().get_data();
    def.chapter_id = String(quest_data.get("chapter_id", "")).utf8().get_data();
    def.title_key = String(quest_data.get("title", "")).utf8().get_data();

    // Optional fields.
    def.desc_key = String(quest_data.get("description", "")).utf8().get_data();
    def.icon_key = String(quest_data.get("icon_key", "")).utf8().get_data();
    def.order_index = static_cast<int32_t>(quest_data.get("order_index", 0));

    // Prerequisites.
    if (quest_data.has("prerequisites")) {
        auto arr = quest_data["prerequisites"];
        if (arr.get_type() == Variant::PACKED_STRING_ARRAY) {
            auto psa = (PackedStringArray)arr;
            for (int i = 0; i < psa.size(); ++i) {
                def.prerequisite_ids.push_back(psa[i].utf8().get_data());
            }
        } else if (arr.get_type() == Variant::ARRAY) {
            auto a = (Array)arr;
            for (int i = 0; i < a.size(); ++i) {
                def.prerequisite_ids.push_back(String(a[i]).utf8().get_data());
            }
        }
    }

    // Conditions.
    if (quest_data.has("conditions")) {
        Array conditions = quest_data["conditions"];
        for (int i = 0; i < conditions.size(); ++i) {
            Dictionary cond_dict = conditions[i];
            def.conditions.push_back(parse_condition(cond_dict));
        }
    }

    // Rewards.
    if (quest_data.has("rewards")) {
        Array rewards = quest_data["rewards"];
        for (int i = 0; i < rewards.size(); ++i) {
            Dictionary reward_dict = rewards[i];
            def.rewards.push_back(parse_reward(reward_dict));
        }
    }

    // Flags.
    def.is_hidden = quest_data.get("is_hidden", false);
    def.can_repeat = quest_data.get("can_repeat", false);
    def.auto_start = quest_data.get("auto_start", true);

    quest_system_.register_quest(def);
}

// ============================================================
// Initialization
// ============================================================

void GDQuestSystem::initialize() {
    quest_system_.initialize();
}

// ============================================================
// Event-driven condition updates
// ============================================================

void GDQuestSystem::on_inventory_changed(const Callable& inventory_has_item) {
    inventory_callable_ = inventory_has_item;

    quest_system_.on_inventory_changed(
        [this](const std::string& item_key) -> int32_t {
            if (!inventory_callable_.is_valid()) return 0;
            Variant result = inventory_callable_.call(String(item_key.c_str()));
            if (result.get_type() == Variant::INT) {
                return static_cast<int32_t>((int64_t)result);
            }
            if (result.get_type() == Variant::FLOAT) {
                return static_cast<int32_t>((double)result);
            }
            return 0;
        });
}

void GDQuestSystem::on_item_crafted(const String& item_key, int32_t count) {
    quest_system_.on_item_crafted(item_key.utf8().get_data(), count);
}

void GDQuestSystem::on_block_mined(const String& block_key, int32_t count) {
    quest_system_.on_block_mined(block_key.utf8().get_data(), count);
}

void GDQuestSystem::on_machine_placed(const String& machine_type, int32_t count) {
    quest_system_.on_machine_placed(machine_type.utf8().get_data(), count);
}

// ============================================================
// Query API
// ============================================================

int64_t GDQuestSystem::get_quest_state(const String& quest_id) const {
    return static_cast<int64_t>(quest_system_.get_quest_state(quest_id.utf8().get_data()));
}

Dictionary GDQuestSystem::get_quest_progress(const String& quest_id) const {
    auto* prog = quest_system_.get_progress(quest_id.utf8().get_data());
    if (!prog) return {};
    return quest_progress_to_dict(*prog);
}

Array GDQuestSystem::get_chapters() const {
    Array result;
    for (const auto& id : quest_system_.get_chapters()) {
        result.append(String(id.c_str()));
    }
    return result;
}

Array GDQuestSystem::get_quests_in_chapter(const String& chapter_id) const {
    Array result;
    for (const auto& id : quest_system_.get_quests_in_chapter(chapter_id.utf8().get_data())) {
        result.append(String(id.c_str()));
    }
    return result;
}

Dictionary GDQuestSystem::get_quest_def(const String& quest_id) const {
    auto* def = quest_system_.get_quest_def(quest_id.utf8().get_data());
    if (!def) return {};
    return quest_def_to_dict(*def);
}

Dictionary GDQuestSystem::get_chapter_def(const String& chapter_id) const {
    auto* def = quest_system_.get_chapter_def(chapter_id.utf8().get_data());
    if (!def) return {};
    return chapter_def_to_dict(*def);
}

bool GDQuestSystem::is_quest_visible(const String& quest_id) const {
    return quest_system_.is_quest_visible(quest_id.utf8().get_data());
}

bool GDQuestSystem::is_reward_claimed(const String& quest_id) const {
    return quest_system_.is_reward_claimed(quest_id.utf8().get_data());
}

bool GDQuestSystem::claim_reward(const String& quest_id) {
    return quest_system_.claim_reward(quest_id.utf8().get_data());
}

Array GDQuestSystem::get_quest_rewards(const String& quest_id) const {
    Array result;
    auto* rewards = quest_system_.get_quest_rewards(quest_id.utf8().get_data());
    if (!rewards) return result;
    for (const auto& r : *rewards) {
        result.append(quest_reward_to_dict(r));
    }
    return result;
}

bool GDQuestSystem::start_quest(const String& quest_id) {
    return quest_system_.start_quest(quest_id.utf8().get_data());
}

bool GDQuestSystem::reset_quest(const String& quest_id) {
    return quest_system_.reset_quest(quest_id.utf8().get_data());
}

int64_t GDQuestSystem::quest_count() const {
    return static_cast<int64_t>(quest_system_.quest_count());
}

int64_t GDQuestSystem::completed_count() const {
    return static_cast<int64_t>(quest_system_.completed_count());
}

// ============================================================
// Serialization
// ============================================================

PackedByteArray GDQuestSystem::serialize() const {
    std::stringstream ss(std::ios::out | std::ios::binary);
    quest_system_.serialize(ss);

    std::string data = ss.str();
    PackedByteArray result;
    result.resize(static_cast<int64_t>(data.size()));
    for (size_t i = 0; i < data.size(); ++i) {
        result[static_cast<int64_t>(i)] = static_cast<uint8_t>(data[i]);
    }
    return result;
}

bool GDQuestSystem::deserialize(const PackedByteArray& data) {
    std::string buf;
    buf.resize(data.size());
    for (int64_t i = 0; i < data.size(); ++i) {
        buf[static_cast<size_t>(i)] = static_cast<char>(data[i]);
    }
    std::stringstream ss(buf, std::ios::in | std::ios::binary);
    return quest_system_.deserialize(ss);
}

// ============================================================
// Tick control
// ============================================================

void GDQuestSystem::set_tick_counter(int64_t tick) {
    tick_counter_ = tick;
}

int64_t GDQuestSystem::get_tick_counter() const {
    return tick_counter_;
}

// ============================================================
// Method binding
// ============================================================

void GDQuestSystem::_bind_methods() {
    // Signals
    ADD_SIGNAL(MethodInfo("quest_unlocked", PropertyInfo(Variant::STRING, "quest_id")));
    ADD_SIGNAL(MethodInfo("quest_completed", PropertyInfo(Variant::STRING, "quest_id")));
    ADD_SIGNAL(MethodInfo("quest_progress_changed", PropertyInfo(Variant::STRING, "quest_id")));
    ADD_SIGNAL(MethodInfo("reward_claimed", PropertyInfo(Variant::STRING, "quest_id")));

    // Content registration
    ClassDB::bind_method(D_METHOD("register_chapter", "chapter_id", "title", "icon_key", "order_index"),
                         &GDQuestSystem::register_chapter);
    ClassDB::bind_method(D_METHOD("register_quest", "quest_data"),
                         &GDQuestSystem::register_quest);

    // Initialization
    ClassDB::bind_method(D_METHOD("initialize"),
                         &GDQuestSystem::initialize);

    // Event-driven updates
    ClassDB::bind_method(D_METHOD("on_inventory_changed", "inventory_has_item"),
                         &GDQuestSystem::on_inventory_changed);
    ClassDB::bind_method(D_METHOD("on_item_crafted", "item_key", "count"),
                         &GDQuestSystem::on_item_crafted);
    ClassDB::bind_method(D_METHOD("on_block_mined", "block_key", "count"),
                         &GDQuestSystem::on_block_mined);
    ClassDB::bind_method(D_METHOD("on_machine_placed", "machine_type", "count"),
                         &GDQuestSystem::on_machine_placed);

    // Query API
    ClassDB::bind_method(D_METHOD("get_quest_state", "quest_id"),
                         &GDQuestSystem::get_quest_state);
    ClassDB::bind_method(D_METHOD("get_quest_progress", "quest_id"),
                         &GDQuestSystem::get_quest_progress);
    ClassDB::bind_method(D_METHOD("get_chapters"),
                         &GDQuestSystem::get_chapters);
    ClassDB::bind_method(D_METHOD("get_quests_in_chapter", "chapter_id"),
                         &GDQuestSystem::get_quests_in_chapter);
    ClassDB::bind_method(D_METHOD("get_quest_def", "quest_id"),
                         &GDQuestSystem::get_quest_def);
    ClassDB::bind_method(D_METHOD("get_chapter_def", "chapter_id"),
                         &GDQuestSystem::get_chapter_def);
    ClassDB::bind_method(D_METHOD("is_quest_visible", "quest_id"),
                         &GDQuestSystem::is_quest_visible);
    ClassDB::bind_method(D_METHOD("is_reward_claimed", "quest_id"),
                         &GDQuestSystem::is_reward_claimed);
    ClassDB::bind_method(D_METHOD("claim_reward", "quest_id"),
                         &GDQuestSystem::claim_reward);
    ClassDB::bind_method(D_METHOD("get_quest_rewards", "quest_id"),
                         &GDQuestSystem::get_quest_rewards);
    ClassDB::bind_method(D_METHOD("start_quest", "quest_id"),
                         &GDQuestSystem::start_quest);
    ClassDB::bind_method(D_METHOD("reset_quest", "quest_id"),
                         &GDQuestSystem::reset_quest);
    ClassDB::bind_method(D_METHOD("quest_count"),
                         &GDQuestSystem::quest_count);
    ClassDB::bind_method(D_METHOD("completed_count"),
                         &GDQuestSystem::completed_count);

    // Serialization
    ClassDB::bind_method(D_METHOD("serialize"),
                         &GDQuestSystem::serialize);
    ClassDB::bind_method(D_METHOD("deserialize", "data"),
                         &GDQuestSystem::deserialize);

    // Tick control
    ClassDB::bind_method(D_METHOD("set_tick_counter", "tick"),
                         &GDQuestSystem::set_tick_counter);
    ClassDB::bind_method(D_METHOD("get_tick_counter"),
                         &GDQuestSystem::get_tick_counter);
    ADD_PROPERTY(PropertyInfo(Variant::INT, "tick_counter"),
                 "set_tick_counter", "get_tick_counter");
}

// ============================================================
// Conversion helpers
// ============================================================

Dictionary GDQuestSystem::quest_def_to_dict(const QuestDef& def) const {
    Dictionary d;
    d["quest_id"] = String(def.quest_id.c_str());
    d["chapter_id"] = String(def.chapter_id.c_str());
    d["title"] = String(def.title_key.c_str());
    d["description"] = String(def.desc_key.c_str());
    d["icon_key"] = String(def.icon_key.c_str());
    d["order_index"] = def.order_index;
    d["is_hidden"] = def.is_hidden;
    d["can_repeat"] = def.can_repeat;
    d["auto_start"] = def.auto_start;

    PackedStringArray prereqs;
    for (const auto& p : def.prerequisite_ids) {
        prereqs.append(String(p.c_str()));
    }
    d["prerequisites"] = prereqs;

    Array conditions;
    for (const auto& c : def.conditions) {
        Dictionary cd;
        cd["type"] = static_cast<int64_t>(c.type);
        cd["target_key"] = String(c.target_key.c_str());
        cd["target_count"] = c.target_count;
        cd["condition_key"] = String(c.condition_key.c_str());
        conditions.append(cd);
    }
    d["conditions"] = conditions;

    Array rewards;
    for (const auto& r : def.rewards) {
        rewards.append(quest_reward_to_dict(r));
    }
    d["rewards"] = rewards;

    return d;
}

Dictionary GDQuestSystem::chapter_def_to_dict(const ChapterDef& def) const {
    Dictionary d;
    d["chapter_id"] = String(def.chapter_id.c_str());
    d["title"] = String(def.title_key.c_str());
    d["icon_key"] = String(def.icon_key.c_str());
    d["order_index"] = def.order_index;
    return d;
}

Dictionary GDQuestSystem::quest_progress_to_dict(const QuestProgress& prog) const {
    Dictionary d;
    d["quest_id"] = String(prog.quest_id.c_str());
    d["state"] = static_cast<int64_t>(prog.state);
    d["reward_claimed"] = prog.reward_claimed;
    d["completed_tick"] = prog.completed_tick;
    d["completion_count"] = prog.completion_count;

    Dictionary progress;
    for (const auto& [key, val] : prog.progress_counters) {
        progress[String(key.c_str())] = val;
    }
    d["progress"] = progress;

    return d;
}

Dictionary GDQuestSystem::quest_reward_to_dict(const QuestReward& reward) const {
    Dictionary d;
    d["type"] = static_cast<int64_t>(reward.type);
    d["item_key"] = String(reward.item_key.c_str());
    d["count"] = reward.count;
    d["unlock_quest_id"] = String(reward.unlock_quest_id.c_str());

    PackedStringArray options;
    for (const auto& o : reward.options) {
        options.append(String(o.c_str()));
    }
    d["options"] = options;

    return d;
}

QuestCondition GDQuestSystem::parse_condition(const Dictionary& cond_dict) const {
    QuestCondition cond;
    cond.type = static_cast<QuestConditionType>(
        static_cast<int64_t>(cond_dict.get("type", 0)));
    cond.target_key = String(cond_dict.get("target_key", "")).utf8().get_data();
    cond.target_count = static_cast<int32_t>(cond_dict.get("target_count", 1));
    cond.condition_key = String(cond_dict.get("condition_key", "")).utf8().get_data();

    // Auto-generate condition_key if empty.
    if (cond.condition_key.empty()) {
        // Convention: "{type_short}.{target_key}"
        const char* type_short = "custom";
        switch (cond.type) {
        case QuestConditionType::HAS_ITEM:      type_short = "has"; break;
        case QuestConditionType::CRAFT_ITEM:    type_short = "craft"; break;
        case QuestConditionType::MINE_BLOCK:    type_short = "mine"; break;
        case QuestConditionType::PLACE_MACHINE: type_short = "place"; break;
        case QuestConditionType::REACH_TICK:    type_short = "tick"; break;
        default: break;
        }
        cond.condition_key = std::string(type_short) + "." + cond.target_key;
    }

    return cond;
}

QuestReward GDQuestSystem::parse_reward(const Dictionary& reward_dict) const {
    QuestReward reward;
    reward.type = static_cast<QuestRewardType>(
        static_cast<int64_t>(reward_dict.get("type", 0)));
    reward.item_key = String(reward_dict.get("item_key", "")).utf8().get_data();
    reward.count = static_cast<int32_t>(reward_dict.get("count", 1));
    reward.unlock_quest_id = String(reward_dict.get("unlock_quest_id", "")).utf8().get_data();

    if (reward_dict.has("options")) {
        auto arr = reward_dict["options"];
        if (arr.get_type() == Variant::PACKED_STRING_ARRAY) {
            auto psa = (PackedStringArray)arr;
            for (int i = 0; i < psa.size(); ++i) {
                reward.options.push_back(psa[i].utf8().get_data());
            }
        } else if (arr.get_type() == Variant::ARRAY) {
            auto a = (Array)arr;
            for (int i = 0; i < a.size(); ++i) {
                reward.options.push_back(String(a[i]).utf8().get_data());
            }
        }
    }

    return reward;
}

// ============================================================
// EventBus integration
// ============================================================

void GDQuestSystem::subscribe_to_events() {
    // EventBus subscription will be connected when a GDTickSystem
    // is available. For standalone use, signals are emitted from
    // _process after tick evaluation.
}

void GDQuestSystem::unsubscribe_from_events() {
    event_subscriptions_.clear();
}

} // namespace science_and_theology
