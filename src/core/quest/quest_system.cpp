#include "quest/quest_system.hpp"

#include <algorithm>
#include <cstring>
#include <fstream>

namespace science_and_theology {

// ============================================================
// Content registration
// ============================================================

void QuestSystem::register_chapter(const ChapterDef& chapter) {
    chapters_[chapter.chapter_id] = chapter;

    // Maintain display order (insert sorted by order_index).
    auto it = std::find(chapter_order_.begin(), chapter_order_.end(),
                        chapter.chapter_id);
    if (it != chapter_order_.end()) {
        // Already registered; update.
        return;
    }

    auto pos = std::lower_bound(
        chapter_order_.begin(), chapter_order_.end(), chapter.order_index,
        [this](const std::string& id, int32_t order) {
            return chapters_[id].order_index < order;
        });
    chapter_order_.insert(pos, chapter.chapter_id);

    // Ensure chapter_quests_ entry exists.
    if (!chapter_quests_.count(chapter.chapter_id)) {
        chapter_quests_[chapter.chapter_id] = {};
    }
}

void QuestSystem::register_quest(const QuestDef& quest) {
    quests_[quest.quest_id] = quest;

    // Insert into chapter quest list (sorted by order_index).
    auto& quest_list = chapter_quests_[quest.chapter_id];
    auto it = std::find(quest_list.begin(), quest_list.end(), quest.quest_id);
    if (it != quest_list.end()) {
        // Already registered; update in place.
        return;
    }

    auto pos = std::lower_bound(
        quest_list.begin(), quest_list.end(), quest.order_index,
        [this](const std::string& id, int32_t order) {
            return quests_[id].order_index < order;
        });
    quest_list.insert(pos, quest.quest_id);

    // Initialize progress entry if not exists.
    if (!progress_.count(quest.quest_id)) {
        QuestProgress prog;
        prog.quest_id = quest.quest_id;
        prog.state = QuestState::LOCKED;

        // Initialize progress counters for each condition.
        for (const auto& cond : quest.conditions) {
            prog.progress_counters[cond.condition_key] = 0;
        }
        progress_[quest.quest_id] = std::move(prog);
    }
}

void QuestSystem::set_event_bus(EventBus* bus) {
    event_bus_ = bus;
}

void QuestSystem::set_custom_evaluator(CustomConditionEvaluator evaluator) {
    custom_evaluator_ = std::move(evaluator);
}

// ============================================================
// Initialization
// ============================================================

void QuestSystem::initialize() {
    if (initialized_) return;

    // Resolve initial states: quests with no prerequisites start as AVAILABLE.
    for (auto& [qid, prog] : progress_) {
        auto it = quests_.find(qid);
        if (it == quests_.end()) continue;

        const auto& def = it->second;
        if (def.prerequisite_ids.empty()) {
            prog.state = QuestState::AVAILABLE;

            // Auto-start if configured.
            if (def.auto_start) {
                prog.state = QuestState::IN_PROGRESS;
            }
        }
    }

    // Update hidden quest visibility.
    update_hidden_visibility();

    initialized_ = true;
}

// ============================================================
// Tick
// ============================================================

void QuestSystem::tick(float delta, int64_t current_tick) {
    current_tick_ = current_tick;

    // Check REACH_TICK conditions for all IN_PROGRESS quests.
    for (auto& [qid, prog] : progress_) {
        if (prog.state != QuestState::IN_PROGRESS) continue;

        auto it = quests_.find(qid);
        if (it == quests_.end()) continue;

        const auto& def = it->second;

        // Update REACH_TICK conditions.
        for (const auto& cond : def.conditions) {
            if (cond.type == QuestConditionType::REACH_TICK) {
                prog.progress_counters[cond.condition_key] =
                    static_cast<int32_t>(current_tick);
            }

            // Update CUSTOM conditions via evaluator.
            if (cond.type == QuestConditionType::CUSTOM && custom_evaluator_) {
                prog.progress_counters[cond.condition_key] =
                    custom_evaluator_(qid, cond);
            }
        }

        // Check if all conditions are now met.
        if (evaluate_conditions(qid)) {
            transition_state(qid, QuestState::COMPLETED);
        }
    }

    // Check if any LOCKED quests should become AVAILABLE.
    for (auto& [qid, prog] : progress_) {
        if (prog.state != QuestState::LOCKED) continue;

        if (are_prerequisites_met(qid)) {
            auto it = quests_.find(qid);
            bool is_hidden_quest = it != quests_.end() && it->second.is_hidden;
            if (!is_hidden_quest) {
                transition_state(qid, QuestState::AVAILABLE);

                // Auto-start if configured.
                if (it != quests_.end() && it->second.auto_start) {
                    transition_state(qid, QuestState::IN_PROGRESS);
                }
            }
        }
    }

    update_hidden_visibility();
}

// ============================================================
// Event-driven condition updates
// ============================================================

void QuestSystem::on_inventory_changed(ItemCountQuery inventory_has_item) {
    inventory_query_ = std::move(inventory_has_item);

    for (auto& [qid, prog] : progress_) {
        if (prog.state != QuestState::IN_PROGRESS) continue;

        auto it = quests_.find(qid);
        if (it == quests_.end()) continue;

        const auto& def = it->second;
        for (const auto& cond : def.conditions) {
            if (cond.type == QuestConditionType::HAS_ITEM && inventory_query_) {
                prog.progress_counters[cond.condition_key] =
                    inventory_query_(cond.target_key);
            }
        }

        if (evaluate_conditions(qid)) {
            transition_state(qid, QuestState::COMPLETED);
        }
    }
}

void QuestSystem::on_item_crafted(const std::string& item_key, int32_t count) {
    for (auto& [qid, prog] : progress_) {
        if (prog.state != QuestState::IN_PROGRESS) continue;

        auto it = quests_.find(qid);
        if (it == quests_.end()) continue;

        for (const auto& cond : it->second.conditions) {
            if (cond.type == QuestConditionType::CRAFT_ITEM &&
                cond.target_key == item_key) {
                prog.progress_counters[cond.condition_key] += count;
            }
        }

        if (evaluate_conditions(qid)) {
            transition_state(qid, QuestState::COMPLETED);
        }
    }
}

void QuestSystem::on_block_mined(const std::string& block_key, int32_t count) {
    for (auto& [qid, prog] : progress_) {
        if (prog.state != QuestState::IN_PROGRESS) continue;

        auto it = quests_.find(qid);
        if (it == quests_.end()) continue;

        for (const auto& cond : it->second.conditions) {
            if (cond.type == QuestConditionType::MINE_BLOCK &&
                cond.target_key == block_key) {
                prog.progress_counters[cond.condition_key] += count;
            }
        }

        if (evaluate_conditions(qid)) {
            transition_state(qid, QuestState::COMPLETED);
        }
    }
}

void QuestSystem::on_machine_placed(const std::string& machine_type, int32_t count) {
    for (auto& [qid, prog] : progress_) {
        if (prog.state != QuestState::IN_PROGRESS) continue;

        auto it = quests_.find(qid);
        if (it == quests_.end()) continue;

        for (const auto& cond : it->second.conditions) {
            if (cond.type == QuestConditionType::PLACE_MACHINE &&
                cond.target_key == machine_type) {
                prog.progress_counters[cond.condition_key] += count;
            }
        }

        if (evaluate_conditions(qid)) {
            transition_state(qid, QuestState::COMPLETED);
        }
    }
}

// ============================================================
// Query API
// ============================================================

QuestState QuestSystem::get_quest_state(const std::string& quest_id) const {
    auto it = progress_.find(quest_id);
    if (it == progress_.end()) return QuestState::LOCKED;
    return it->second.state;
}

const QuestProgress* QuestSystem::get_progress(const std::string& quest_id) const {
    auto it = progress_.find(quest_id);
    if (it == progress_.end()) return nullptr;
    return &it->second;
}

std::vector<std::string> QuestSystem::get_chapters() const {
    return chapter_order_;
}

std::vector<std::string> QuestSystem::get_quests_in_chapter(
    const std::string& chapter_id) const {
    auto it = chapter_quests_.find(chapter_id);
    if (it == chapter_quests_.end()) return {};
    return it->second;
}

const QuestDef* QuestSystem::get_quest_def(const std::string& quest_id) const {
    auto it = quests_.find(quest_id);
    if (it == quests_.end()) return nullptr;
    return &it->second;
}

const ChapterDef* QuestSystem::get_chapter_def(const std::string& chapter_id) const {
    auto it = chapters_.find(chapter_id);
    if (it == chapters_.end()) return nullptr;
    return &it->second;
}

bool QuestSystem::is_quest_visible(const std::string& quest_id) const {
    auto def_it = quests_.find(quest_id);
    if (def_it == quests_.end()) return false;

    // Non-hidden quests are always visible.
    if (!def_it->second.is_hidden) return true;

    // Hidden quests are visible only if their prerequisites are met
    // (i.e. they have been "unlocked").
    return are_prerequisites_met(quest_id);
}

bool QuestSystem::is_reward_claimed(const std::string& quest_id) const {
    auto it = progress_.find(quest_id);
    if (it == progress_.end()) return false;
    return it->second.reward_claimed;
}

bool QuestSystem::claim_reward(const std::string& quest_id) {
    auto it = progress_.find(quest_id);
    if (it == progress_.end()) return false;

    auto& prog = it->second;
    if (prog.state != QuestState::COMPLETED) return false;
    if (prog.reward_claimed) return false;

    prog.reward_claimed = true;
    emit_quest_event(GameEventType::REWARD_CLAIMED, quest_id);
    return true;
}

const std::vector<QuestReward>* QuestSystem::get_quest_rewards(
    const std::string& quest_id) const {
    auto it = quests_.find(quest_id);
    if (it == quests_.end()) return nullptr;
    return &it->second.rewards;
}

bool QuestSystem::start_quest(const std::string& quest_id) {
    auto it = progress_.find(quest_id);
    if (it == progress_.end()) return false;

    if (it->second.state != QuestState::AVAILABLE) return false;

    transition_state(quest_id, QuestState::IN_PROGRESS);
    return true;
}

bool QuestSystem::reset_quest(const std::string& quest_id) {
    auto prog_it = progress_.find(quest_id);
    if (prog_it == progress_.end()) return false;

    auto def_it = quests_.find(quest_id);
    if (def_it == quests_.end()) return false;

    if (!def_it->second.can_repeat) return false;
    if (prog_it->second.state != QuestState::COMPLETED) return false;

    // Reset progress counters.
    for (auto& [key, val] : prog_it->second.progress_counters) {
        val = 0;
    }
    prog_it->second.reward_claimed = false;
    prog_it->second.completed_tick = 0;
    prog_it->second.completion_count++;

    transition_state(quest_id, QuestState::AVAILABLE);

    // Auto-start if configured.
    if (def_it->second.auto_start) {
        transition_state(quest_id, QuestState::IN_PROGRESS);
    }

    return true;
}

size_t QuestSystem::quest_count() const {
    return quests_.size();
}

size_t QuestSystem::completed_count() const {
    size_t count = 0;
    for (const auto& [qid, prog] : progress_) {
        if (prog.state == QuestState::COMPLETED) ++count;
    }
    return count;
}

// ============================================================
// Serialization
// ============================================================

void QuestSystem::serialize(std::ostream& out) const {
    // Header
    out.write(reinterpret_cast<const char*>(&kQuestProgressVersion), sizeof(uint8_t));

    // Number of quest progress entries.
    uint32_t count = static_cast<uint32_t>(progress_.size());
    out.write(reinterpret_cast<const char*>(&count), sizeof(uint32_t));

    for (const auto& [qid, prog] : progress_) {
        // Quest ID (length-prefixed).
        uint32_t id_len = static_cast<uint32_t>(qid.size());
        out.write(reinterpret_cast<const char*>(&id_len), sizeof(uint32_t));
        out.write(qid.data(), id_len);

        // State.
        uint8_t state = static_cast<uint8_t>(prog.state);
        out.write(reinterpret_cast<const char*>(&state), sizeof(uint8_t));

        // Reward claimed.
        out.write(reinterpret_cast<const char*>(&prog.reward_claimed), sizeof(bool));

        // Completed tick.
        out.write(reinterpret_cast<const char*>(&prog.completed_tick), sizeof(int64_t));

        // Completion count.
        out.write(reinterpret_cast<const char*>(&prog.completion_count), sizeof(int32_t));

        // Progress counters.
        uint32_t counter_count = static_cast<uint32_t>(prog.progress_counters.size());
        out.write(reinterpret_cast<const char*>(&counter_count), sizeof(uint32_t));

        for (const auto& [key, val] : prog.progress_counters) {
            uint32_t key_len = static_cast<uint32_t>(key.size());
            out.write(reinterpret_cast<const char*>(&key_len), sizeof(uint32_t));
            out.write(key.data(), key_len);
            out.write(reinterpret_cast<const char*>(&val), sizeof(int32_t));
        }
    }
}

bool QuestSystem::deserialize(std::istream& in) {
    uint8_t version = 0;
    in.read(reinterpret_cast<char*>(&version), sizeof(uint8_t));
    if (version != kQuestProgressVersion) return false;

    uint32_t count = 0;
    in.read(reinterpret_cast<char*>(&count), sizeof(uint32_t));

    for (uint32_t i = 0; i < count; ++i) {
        // Quest ID.
        uint32_t id_len = 0;
        in.read(reinterpret_cast<char*>(&id_len), sizeof(uint32_t));
        if (id_len > 256) return false;
        std::string qid(id_len, '\0');
        in.read(qid.data(), id_len);

        // Only restore progress for quests that are still registered.
        if (!quests_.count(qid)) {
            // Skip this entry: read remaining fields and discard.
            uint8_t state = 0;
            in.read(reinterpret_cast<char*>(&state), sizeof(uint8_t));
            bool reward_claimed = false;
            in.read(reinterpret_cast<char*>(&reward_claimed), sizeof(bool));
            int64_t completed_tick = 0;
            in.read(reinterpret_cast<char*>(&completed_tick), sizeof(int64_t));
            int32_t completion_count = 0;
            in.read(reinterpret_cast<char*>(&completion_count), sizeof(int32_t));
            uint32_t counter_count = 0;
            in.read(reinterpret_cast<char*>(&counter_count), sizeof(uint32_t));
            for (uint32_t j = 0; j < counter_count; ++j) {
                uint32_t key_len = 0;
                in.read(reinterpret_cast<char*>(&key_len), sizeof(uint32_t));
                std::string key(key_len, '\0');
                in.read(key.data(), key_len);
                int32_t val = 0;
                in.read(reinterpret_cast<char*>(&val), sizeof(int32_t));
            }
            continue;
        }

        auto& prog = progress_[qid];
        prog.quest_id = qid;

        uint8_t state = 0;
        in.read(reinterpret_cast<char*>(&state), sizeof(uint8_t));
        prog.state = static_cast<QuestState>(state);

        in.read(reinterpret_cast<char*>(&prog.reward_claimed), sizeof(bool));
        in.read(reinterpret_cast<char*>(&prog.completed_tick), sizeof(int64_t));
        in.read(reinterpret_cast<char*>(&prog.completion_count), sizeof(int32_t));

        uint32_t counter_count = 0;
        in.read(reinterpret_cast<char*>(&counter_count), sizeof(uint32_t));

        prog.progress_counters.clear();
        for (uint32_t j = 0; j < counter_count; ++j) {
            uint32_t key_len = 0;
            in.read(reinterpret_cast<char*>(&key_len), sizeof(uint32_t));
            if (key_len > 256) return false;
            std::string key(key_len, '\0');
            in.read(key.data(), key_len);
            int32_t val = 0;
            in.read(reinterpret_cast<char*>(&val), sizeof(int32_t));
            prog.progress_counters[key] = val;
        }
    }

    return in.good();
}

// ============================================================
// Internal helpers
// ============================================================

bool QuestSystem::are_prerequisites_met(const std::string& quest_id) const {
    auto it = quests_.find(quest_id);
    if (it == quests_.end()) return false;

    for (const auto& prereq_id : it->second.prerequisite_ids) {
        auto prog_it = progress_.find(prereq_id);
        if (prog_it == progress_.end()) return false;
        if (prog_it->second.state != QuestState::COMPLETED) return false;
    }
    return true;
}

bool QuestSystem::evaluate_conditions(const std::string& quest_id) {
    auto def_it = quests_.find(quest_id);
    if (def_it == quests_.end()) return false;

    auto prog_it = progress_.find(quest_id);
    if (prog_it == progress_.end()) return false;

    const auto& def = def_it->second;
    const auto& prog = prog_it->second;

    for (const auto& cond : def.conditions) {
        if (!is_condition_satisfied(prog, cond)) return false;
    }
    return true;
}

bool QuestSystem::is_condition_satisfied(const QuestProgress& progress,
                                         const QuestCondition& condition) const {
    auto it = progress.progress_counters.find(condition.condition_key);
    if (it == progress.progress_counters.end()) return false;
    return it->second >= condition.target_count;
}

void QuestSystem::transition_state(const std::string& quest_id,
                                   QuestState new_state) {
    auto it = progress_.find(quest_id);
    if (it == progress_.end()) return;

    auto& prog = it->second;
    QuestState old_state = prog.state;

    if (old_state == new_state) return;

    prog.state = new_state;

    // Emit events on meaningful transitions.
    switch (new_state) {
    case QuestState::AVAILABLE:
        emit_quest_event(GameEventType::QUEST_UNLOCKED, quest_id);
        break;
    case QuestState::COMPLETED: {
        prog.completed_tick = current_tick_;
        emit_quest_event(GameEventType::QUEST_COMPLETED, quest_id);

        // Process UNLOCK_QUEST rewards immediately.
        auto def_it = quests_.find(quest_id);
        if (def_it != quests_.end()) {
            for (const auto& reward : def_it->second.rewards) {
                if (reward.type == QuestRewardType::UNLOCK_QUEST) {
                    // Force the target quest to AVAILABLE (override hidden).
                    auto target_it = progress_.find(reward.unlock_quest_id);
                    if (target_it != progress_.end() &&
                        target_it->second.state == QuestState::LOCKED) {
                        target_it->second.state = QuestState::AVAILABLE;
                        emit_quest_event(GameEventType::QUEST_UNLOCKED,
                                         reward.unlock_quest_id);

                        // Auto-start if configured.
                        auto target_def = quests_.find(reward.unlock_quest_id);
                        if (target_def != quests_.end() &&
                            target_def->second.auto_start) {
                            target_it->second.state = QuestState::IN_PROGRESS;
                        }
                    }
                }
            }
        }
        break;
    }
    default:
        break;
    }

    emit_quest_event(GameEventType::QUEST_PROGRESS_CHANGED, quest_id);
}

void QuestSystem::update_hidden_visibility() {
    for (auto& [qid, prog] : progress_) {
        auto def_it = quests_.find(qid);
        if (def_it == quests_.end()) continue;

        if (!def_it->second.is_hidden) continue;
        if (prog.state != QuestState::LOCKED) continue;

        // Hidden quests become AVAILABLE when prerequisites are met
        // (they are "discovered").
        if (are_prerequisites_met(qid)) {
            prog.state = QuestState::AVAILABLE;

            if (def_it->second.auto_start) {
                prog.state = QuestState::IN_PROGRESS;
            }
        }
    }
}

void QuestSystem::emit_quest_event(GameEventType type,
                                   const std::string& quest_id) {
    if (!event_bus_) return;

    GameEvent event;
    event.type = type;
    event.string_data["quest_id"] = quest_id;
    event_bus_->emit(event);
}

} // namespace science_and_theology
