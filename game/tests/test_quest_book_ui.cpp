// BQ-style task-book presentation tests.
//
// These tests keep the player-facing graph strictly read-only: the server
// remains responsible for automatic prerequisite unlocks and task progress;
// the MUI only reflects snapshots and may request an explicit reward claim.

#include "game/client/quest_book_ui.h"

#include "core/path_utils.h"
#include "game/network/game_quest_book_replication.h"
#include "ui/retained_mui_runtime.h"

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {

using snt::game::GameContentRegistry;
using snt::game::IQuestBookCommandSink;
using snt::game::QuestBookChapterDefinition;
using snt::game::QuestBookPresentationStatus;
using snt::game::QuestBookSnapshot;
using snt::game::QuestBookViewModel;
using snt::game::QuestDefinition;
using snt::game::QuestObjectiveDefinition;
using snt::game::QuestObjectiveKind;
using snt::game::QuestProgressRecord;
using snt::game::QuestRewardDefinition;
using snt::game::QuestRewardKind;
using snt::game::QuestState;
using snt::game::make_quest_book_ui_factory;
using snt::game::replication::GameClientQuestBookState;
using snt::game::replication::GameReplicationValueKind;
using snt::game::replication::GameReplicationValueOperation;
using snt::game::replication::GameSnapshot;
using snt::ui::Button;
using snt::ui::UiFrameResult;
using snt::ui::UiImageRegistry;
using snt::ui::UiLayer;
using snt::ui::UiLayerStack;
using snt::ui::UiRuntime;
using snt::ui::Vec2;
using snt::ui::View;
using snt::ui::ViewGroup;

class RecordingQuestBookCommandSink final : public IQuestBookCommandSink {
public:
    [[nodiscard]] snt::core::Expected<void> submit_quest_reward_claim(
        std::string_view quest_id) override {
        claimed_quest_ids.emplace_back(quest_id);
        return {};
    }

    std::vector<std::string> claimed_quest_ids;
};

snt::core::Expected<snt::core::RuntimePathResolver> make_test_path_resolver() {
    return snt::core::RuntimePathResolver::create({
        .engine_root = SNT_ENGINE_TEST_ROOT,
        .game_root = SNT_GAME_TEST_ROOT,
        .user_root = SNT_ENGINE_TEST_ROOT,
    });
}

void register_task_book_content(GameContentRegistry& content) {
    ASSERT_TRUE(content.register_builtin_quest_chapter({
        .id = "p7.primitive",
        .title = "Primitive Industry",
        .description = "The first heat and metalworking chain.",
        .sort_order = 3,
    }));

    QuestDefinition gather;
    gather.id = "p7.primitive.stone";
    gather.chapter_id = "p7.primitive";
    gather.title = "Gather Stone";
    gather.description = "Mine stone to establish the first material supply.";
    gather.node_position = {.x = 80.0f, .y = 180.0f};
    gather.objectives = {{
        .id = "mine.stone",
        .kind = QuestObjectiveKind::kMineBlock,
        .target_id = "stone",
        .required_count = 8,
    }};
    gather.rewards = {{
        .kind = QuestRewardKind::kItem,
        .target_id = "wood_dust",
        .count = 8,
    }};
    ASSERT_TRUE(content.register_builtin_quest(std::move(gather)));

    QuestDefinition smelt;
    smelt.id = "p7.primitive.iron";
    smelt.chapter_id = "p7.primitive";
    smelt.title = "Smelt Iron";
    smelt.description = "Run the furnace until it produces an iron ingot.";
    smelt.node_position = {.x = 330.0f, .y = 180.0f};
    smelt.prerequisites = {"p7.primitive.stone"};
    smelt.objectives = {{
        .id = "craft.iron_ingot",
        .kind = QuestObjectiveKind::kCraftItem,
        .target_id = "iron_ingot",
        .required_count = 1,
    }};
    smelt.rewards = {{
        .kind = QuestRewardKind::kItem,
        .target_id = "charcoal",
        .count = 4,
    }};
    ASSERT_TRUE(content.register_builtin_quest(std::move(smelt)));
}

QuestBookSnapshot make_task_book_snapshot(const GameContentRegistry& content,
                                          uint64_t revision = 7) {
    QuestProgressRecord gather;
    gather.quest_id = "p7.primitive.stone";
    gather.state = QuestState::kCompleted;
    gather.objective_counts.emplace("mine.stone", 8);
    gather.completed_tick = 42;

    QuestProgressRecord smelt;
    smelt.quest_id = "p7.primitive.iron";
    smelt.state = QuestState::kInProgress;
    smelt.objective_counts.emplace("craft.iron_ingot", 0);

    return {
        .account_id = "local-name:QuestBookUi",
        .content_fingerprint = content.quest_content_fingerprint(),
        .progress_revision = revision,
        .progress = {std::move(gather), std::move(smelt)},
    };
}

void apply_snapshot(GameClientQuestBookState& state, const QuestBookSnapshot& snapshot,
                    uint64_t snapshot_id = 19) {
    auto encoded = snt::game::replication::encode_game_quest_book_snapshot(snapshot);
    ASSERT_TRUE(encoded) << encoded.error().format();
    GameSnapshot replication_snapshot;
    replication_snapshot.snapshot_id = snapshot_id;
    replication_snapshot.values.push_back({
        .kind = GameReplicationValueKind::kQuestBook,
        .operation = GameReplicationValueOperation::kUpsert,
        .payload = std::move(*encoded),
    });
    auto applied = state.apply(replication_snapshot);
    ASSERT_TRUE(applied) << applied.error().format();
}

void click_primary(UiRuntime& runtime, View& root, Vec2 point) {
    runtime.begin_input_frame({
        .pointer_position = point,
        .pointer_held = {true, false, false},
        .pointer_pressed = {true, false, false},
    });
    ASSERT_TRUE(runtime.dispatch_pointer_input(root));
    runtime.synchronize_interaction_state(root);

    runtime.begin_input_frame({
        .pointer_position = point,
        .pointer_released = {true, false, false},
    });
    ASSERT_TRUE(runtime.dispatch_pointer_input(root));
    runtime.synchronize_interaction_state(root);
}

TEST(QuestBookUi, ProjectsAutomaticUnlockChainAndRequestsOnlyRewardClaim) {
    GameContentRegistry content;
    register_task_book_content(content);
    GameClientQuestBookState state{"local-name:QuestBookUi"};
    apply_snapshot(state, make_task_book_snapshot(content));

    RecordingQuestBookCommandSink command_sink;
    QuestBookViewModel model{content, &state, &command_sink};
    ASSERT_TRUE(model.refresh());
    EXPECT_EQ(model.status(), QuestBookPresentationStatus::Ready);
    ASSERT_EQ(model.chapters().size(), 1u);
    const auto& chapter = model.chapters().front();
    EXPECT_EQ(chapter.id, "p7.primitive");
    ASSERT_EQ(chapter.quests.size(), 2u);
    EXPECT_EQ(chapter.quests[0].id, "p7.primitive.stone");
    EXPECT_EQ(chapter.quests[0].state, QuestState::kCompleted);
    EXPECT_EQ(chapter.quests[0].objectives.front().current_count, 8);
    EXPECT_EQ(chapter.quests[1].id, "p7.primitive.iron");
    EXPECT_EQ(chapter.quests[1].state, QuestState::kInProgress);
    EXPECT_EQ(chapter.quests[1].prerequisites,
              (std::vector<std::string>{"p7.primitive.stone"}));

    ASSERT_NE(model.selected_quest(), nullptr);
    EXPECT_EQ(model.selected_quest()->id, "p7.primitive.stone");
    ASSERT_TRUE(model.claim_selected_reward());
    EXPECT_EQ(command_sink.claimed_quest_ids,
              (std::vector<std::string>{"p7.primitive.stone"}));
    EXPECT_NE(model.action_message().find("Waiting for server confirmation"), std::string::npos);

    model.select_quest("p7.primitive.iron");
    EXPECT_FALSE(model.claim_selected_reward());
    EXPECT_EQ(command_sink.claimed_quest_ids.size(), 1u);
}

TEST(QuestBookUi, RejectsMismatchedContentBeforePresentingOrCommanding) {
    GameContentRegistry content;
    register_task_book_content(content);
    GameClientQuestBookState state{"local-name:QuestBookUi"};
    QuestBookSnapshot snapshot = make_task_book_snapshot(content);
    snapshot.content_fingerprint ^= 0x1ull;
    apply_snapshot(state, snapshot);

    RecordingQuestBookCommandSink command_sink;
    QuestBookViewModel model{content, &state, &command_sink};
    ASSERT_TRUE(model.refresh());
    EXPECT_EQ(model.status(), QuestBookPresentationStatus::ContentMismatch);
    EXPECT_TRUE(model.chapters().empty());
    EXPECT_FALSE(model.claim_selected_reward());
    EXPECT_TRUE(command_sink.claimed_quest_ids.empty());
}

TEST(QuestBookUi, MountsRetainedGraphAndNeverShowsTaskAcceptanceControl) {
    GameContentRegistry content;
    register_task_book_content(content);
    GameClientQuestBookState state{"local-name:QuestBookUi"};
    apply_snapshot(state, make_task_book_snapshot(content));

    RecordingQuestBookCommandSink command_sink;
    QuestBookViewModel model{content, &state, &command_sink};
    UiImageRegistry images;
    UiLayerStack layers;
    ASSERT_TRUE(layers.register_screen({
        .owner_id = "science_and_theology",
        .screen_id = "quest_book",
        .layer = UiLayer::Modal,
        .initially_visible = true,
        .factory = make_quest_book_ui_factory(model, [] {}),
    }));

    const snt::ui::UiScreenFrameContext frame_context{
        .viewport = {1280.0f, 720.0f},
        .images = images,
    };
    const auto& first_frame = layers.prepare_frame(frame_context);
    ASSERT_EQ(first_frame.size(), 1u);
    View* const root = first_frame.front().root;
    ASSERT_NE(root, nullptr);
    auto* root_group = dynamic_cast<ViewGroup*>(root);
    ASSERT_NE(root_group, nullptr);
    EXPECT_NE(root_group->find("quest_book_graph_links"), nullptr);
    EXPECT_NE(root_group->find("quest_book_node_p7.primitive.stone"), nullptr);
    EXPECT_NE(root_group->find("quest_book_node_p7.primitive.iron"), nullptr);
    EXPECT_EQ(root_group->find("quest_book_accept_quest"), nullptr);

    auto paths = make_test_path_resolver();
    ASSERT_TRUE(paths) << paths.error().format();
    UiRuntime runtime(*paths);
    runtime.layout(*root, frame_context.viewport);
    UiFrameResult painted = runtime.paint(*root);
    EXPECT_FALSE(painted.draw_data.vertices.empty());

    auto* claim = dynamic_cast<Button*>(root_group->find("quest_book_claim_reward"));
    ASSERT_NE(claim, nullptr);
    const snt::ui::Rect claim_bounds = claim->bounds();
    click_primary(runtime, *root, {
        claim_bounds.pos.x + claim_bounds.size.x * 0.5f,
        claim_bounds.pos.y + claim_bounds.size.y * 0.5f,
    });
    EXPECT_EQ(command_sink.claimed_quest_ids,
              (std::vector<std::string>{"p7.primitive.stone"}));

    const auto& after_claim_frame = layers.prepare_frame(frame_context);
    ASSERT_EQ(after_claim_frame.size(), 1u);
    EXPECT_EQ(after_claim_frame.front().root, root);
    EXPECT_NE(root_group->find("quest_book_action_message"), nullptr);

    model.select_quest("p7.primitive.iron");
    const auto& after_selection_frame = layers.prepare_frame(frame_context);
    ASSERT_EQ(after_selection_frame.size(), 1u);
    EXPECT_EQ(after_selection_frame.front().root, root);
    EXPECT_EQ(root_group->find("quest_book_claim_reward"), nullptr);
    EXPECT_EQ(root_group->find("quest_book_accept_quest"), nullptr);
}

}  // namespace
