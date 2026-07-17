// BQ-style player task-book presentation.
//
// Ownership: the graphical client session owns QuestBookViewModel. It reads
// immutable content definitions plus the replicated GameClientQuestBookState;
// it never receives QuestRegistry or mutable server progress. The retained
// screen is game-owned because chapter, node, objective, reward, and command
// semantics are not reusable engine UI concepts.

#pragma once

#include "core/expected.h"
#include "game/client/game_content_registry.h"
#include "game/quest/quest_book.h"
#include "game/quest/quest_progress.h"
#include "ui/retained_mui_screen_stack.h"

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace snt::game::replication {
class GameClientQuestBookState;
}

namespace snt::game {

// The UI can request a reward claim but has no authority to complete it. A
// client-session implementation maps this to the existing typed network
// command; offline or disconnected hosts return an explicit error instead.
class IQuestBookCommandSink {
public:
    virtual ~IQuestBookCommandSink() = default;

    [[nodiscard]] virtual snt::core::Expected<void> submit_quest_reward_claim(
        std::string_view quest_id) = 0;
};

enum class QuestBookPresentationStatus : uint8_t {
    WaitingForSnapshot,
    ContentMismatch,
    Ready,
    Empty,
};

struct QuestBookObjectiveView {
    std::string id;
    std::string target_id;
    int32_t current_count = 0;
    int32_t required_count = 1;
    QuestObjectiveKind kind = QuestObjectiveKind::kCraftItem;
};

struct QuestBookRewardView {
    QuestRewardKind kind = QuestRewardKind::kItem;
    std::string target_id;
    int32_t count = 1;
};

struct QuestBookQuestView {
    std::string id;
    std::string chapter_id;
    std::string title;
    std::string description;
    std::string icon_key;
    snt::ui::Vec2 graph_position{};
    std::vector<std::string> prerequisites;
    std::vector<QuestBookObjectiveView> objectives;
    std::vector<QuestBookRewardView> rewards;
    QuestState state = QuestState::kLocked;
    bool reward_claimed = false;
};

struct QuestBookChapterView {
    std::string id;
    std::string title;
    std::string description;
    std::string icon_key;
    int32_t sort_order = 0;
    std::vector<QuestBookQuestView> quests;
};

// Converts immutable content plus a replicated player snapshot into a stable
// presentation model. `refresh()` is cheap on unchanged revisions and is safe
// to call once from a retained screen updater every rendered frame.
class QuestBookViewModel final {
public:
    explicit QuestBookViewModel(
        const GameContentRegistry& content,
        const replication::GameClientQuestBookState* authoritative_state = nullptr,
        IQuestBookCommandSink* command_sink = nullptr);

    void set_authoritative_state(
        const replication::GameClientQuestBookState* authoritative_state) noexcept;
    void set_command_sink(IQuestBookCommandSink* command_sink) noexcept {
        command_sink_ = command_sink;
    }
    [[nodiscard]] bool refresh();

    [[nodiscard]] QuestBookPresentationStatus status() const noexcept { return status_; }
    [[nodiscard]] uint64_t revision() const noexcept { return revision_; }
    [[nodiscard]] uint64_t local_content_fingerprint() const noexcept;
    [[nodiscard]] uint64_t server_content_fingerprint() const noexcept {
        return server_content_fingerprint_;
    }
    [[nodiscard]] const std::vector<QuestBookChapterView>& chapters() const noexcept {
        return chapters_;
    }
    [[nodiscard]] const std::string& selected_chapter_id() const noexcept {
        return selected_chapter_id_;
    }
    [[nodiscard]] const std::string& selected_quest_id() const noexcept {
        return selected_quest_id_;
    }
    [[nodiscard]] const QuestBookChapterView* selected_chapter() const noexcept;
    [[nodiscard]] const QuestBookQuestView* selected_quest() const noexcept;
    [[nodiscard]] const std::string& action_message() const noexcept { return action_message_; }

    void select_chapter(std::string_view chapter_id);
    void select_quest(std::string_view quest_id);
    [[nodiscard]] snt::core::Expected<void> claim_selected_reward();

private:
    void rebuild(const QuestBookSnapshot* snapshot);
    void choose_valid_selection();
    [[nodiscard]] const QuestBookQuestView* find_quest(std::string_view quest_id) const noexcept;

    const GameContentRegistry* content_ = nullptr;
    const replication::GameClientQuestBookState* authoritative_state_ = nullptr;
    IQuestBookCommandSink* command_sink_ = nullptr;
    std::vector<QuestBookChapterView> chapters_;
    QuestBookPresentationStatus status_ = QuestBookPresentationStatus::WaitingForSnapshot;
    std::string selected_chapter_id_;
    std::string selected_quest_id_;
    std::string action_message_;
    uint64_t observed_content_revision_ = 0;
    uint64_t observed_snapshot_id_ = 0;
    uint64_t observed_progress_revision_ = 0;
    uint64_t server_content_fingerprint_ = 0;
    uint64_t revision_ = 0;
};

// Mounts a modal BQ-style chapter graph and detail panel. Closing is routed
// back to the client session so layer visibility and mouse-lock policy remain
// host-owned rather than being changed by a retained View callback.
[[nodiscard]] snt::ui::UiScreenFactory make_quest_book_ui_factory(
    QuestBookViewModel& model, std::function<void()> on_close);

}  // namespace snt::game
