// BQ-style player task-book presentation implementation.

#define SNT_LOG_CHANNEL "game.quest_book_ui"
#include "game/client/quest_book_ui.h"

#include "core/error.h"
#include "core/log.h"
#include "game/network/game_quest_book_replication.h"
#include "ui/mui_gameplay_controls.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>

namespace snt::game {
namespace {

using snt::ui::Arc2DCommandBuffer;
using snt::ui::Button;
using snt::ui::Color;
using snt::ui::FlexLayout;
using snt::ui::FrameLayout;
using snt::ui::Insets;
using snt::ui::LayoutParams;
using snt::ui::MeasureMode;
using snt::ui::MeasureSpec;
using snt::ui::ModalView;
using snt::ui::PanZoomView;
using snt::ui::ProgressBarView;
using snt::ui::Rect;
using snt::ui::ScrollAxis;
using snt::ui::ScrollView;
using snt::ui::TextEngine;
using snt::ui::TextStyle;
using snt::ui::TextView;
using snt::ui::UiEventPhase;
using snt::ui::UiEventReply;
using snt::ui::UiInputEvent;
using snt::ui::UiInputEventType;
using snt::ui::UiKey;
using snt::ui::UiPointerButton;
using snt::ui::Vec2;
using snt::ui::View;
using snt::ui::ViewGroup;
using snt::ui::Visibility;
using snt::ui::WrappedTextView;

constexpr float kQuestNodeWidth = 168.0f;
constexpr float kQuestNodeHeight = 64.0f;
constexpr float kGraphPadding = 92.0f;

[[nodiscard]] LayoutParams fixed(float width, float height, float left = 0.0f,
                                 float top = 0.0f) {
    LayoutParams params;
    params.width = std::max(0.0f, width);
    params.height = std::max(0.0f, height);
    params.margin.left = left;
    params.margin.top = top;
    return params;
}

[[nodiscard]] Color state_color(QuestState state) noexcept {
    switch (state) {
        case QuestState::kInProgress: return {53, 150, 201, 255};
        case QuestState::kCompleted: return {67, 173, 105, 255};
        case QuestState::kLocked: return {92, 102, 112, 255};
    }
    return {92, 102, 112, 255};
}

[[nodiscard]] const char* state_label(QuestState state) noexcept {
    switch (state) {
        case QuestState::kInProgress: return "In progress";
        case QuestState::kCompleted: return "Completed";
        case QuestState::kLocked: return "Locked";
    }
    return "Unknown";
}

[[nodiscard]] const char* objective_kind_label(QuestObjectiveKind kind) noexcept {
    switch (kind) {
        case QuestObjectiveKind::kAcquireItem: return "Acquire";
        case QuestObjectiveKind::kCraftItem: return "Craft";
        case QuestObjectiveKind::kMineBlock: return "Mine";
        case QuestObjectiveKind::kPlaceMachine: return "Place";
        case QuestObjectiveKind::kReachTick: return "Wait";
        case QuestObjectiveKind::kCustomEvent: return "Complete";
    }
    return "Objective";
}

[[nodiscard]] std::string objective_text(const QuestBookObjectiveView& objective) {
    const std::string target = objective.target_id.empty() ? "game progress" : objective.target_id;
    return std::string(objective_kind_label(objective.kind)) + ": " + target + "  " +
        std::to_string(std::max(0, objective.current_count)) + " / " +
        std::to_string(objective.required_count);
}

[[nodiscard]] std::string reward_text(const QuestBookRewardView& reward) {
    if (reward.kind == QuestRewardKind::kUnlockQuest) {
        return "Unlock: " + reward.target_id;
    }
    return "Item: " + reward.target_id + " x" + std::to_string(reward.count);
}

[[nodiscard]] size_t completed_quest_count(const std::vector<QuestBookChapterView>& chapters) {
    size_t completed = 0;
    for (const QuestBookChapterView& chapter : chapters) {
        completed += static_cast<size_t>(std::count_if(
            chapter.quests.begin(), chapter.quests.end(), [](const QuestBookQuestView& quest) {
                return quest.state == QuestState::kCompleted;
            }));
    }
    return completed;
}

[[nodiscard]] size_t quest_count(const std::vector<QuestBookChapterView>& chapters) {
    size_t count = 0;
    for (const QuestBookChapterView& chapter : chapters) count += chapter.quests.size();
    return count;
}

class QuestChapterTab final : public Button {
public:
    QuestChapterTab(std::string id, bool selected)
        : Button(std::move(id)), selected_(selected) {}

    void paint(Arc2DCommandBuffer& out, TextEngine& text_engine,
               const snt::ui::UiTheme& theme) const override {
        if (visibility() != Visibility::Visible) return;
        const Color fill = selected_ ? Color{55, 111, 142, 255} : Color{35, 48, 59, 255};
        out.rect(bounds_, fill, 4.0f);
        TextView::paint(out, text_engine, theme);
    }

private:
    bool selected_ = false;
};

class QuestNodeView final : public WrappedTextView {
public:
    using ActivateHandler = std::function<void()>;

    QuestNodeView(std::string id, QuestState state, bool selected)
        : WrappedTextView(std::move(id)), state_(state), selected_(selected) {
        set_hit_test_visible(true);
        set_focusable(true);
        set_max_lines(2);
        set_line_spacing(1.05f);
        set_text_style({.size_px = 13.0f, .color = {237, 243, 247, 255}});
    }

    void set_on_activate(ActivateHandler handler) { activate_handler_ = std::move(handler); }

    void paint(Arc2DCommandBuffer& out, TextEngine& text_engine,
               const snt::ui::UiTheme& theme) const override {
        if (visibility() != Visibility::Visible) return;
        const Color state = state_color(state_);
        const Color fill = selected_ ? Color{39, 66, 80, 255} : Color{30, 39, 48, 255};
        out.rect(bounds_, selected_ ? state : fill, 6.0f);
        const Rect inner{
            .pos = {bounds_.pos.x + (selected_ ? 2.0f : 1.0f),
                    bounds_.pos.y + (selected_ ? 2.0f : 1.0f)},
            .size = {std::max(0.0f, bounds_.size.x - (selected_ ? 4.0f : 2.0f)),
                     std::max(0.0f, bounds_.size.y - (selected_ ? 4.0f : 2.0f))},
        };
        out.rect(inner, fill, 4.0f);
        out.rect({.pos = {inner.pos.x, inner.pos.y}, .size = {5.0f, inner.size.y}}, state, 3.0f);
        WrappedTextView::paint(out, text_engine, theme);
    }

    UiEventReply on_input_event(const UiInputEvent& event) override {
        const UiEventReply base = View::on_input_event(event);
        if (base == UiEventReply::StopPropagation || event.phase != UiEventPhase::Target ||
            !enabled()) {
            return base;
        }
        const bool pointer_activation = event.type == UiInputEventType::PointerUp &&
            event.pointer_button == UiPointerButton::Primary && event.activation;
        const bool key_activation = event.type == UiInputEventType::KeyDown &&
            (event.key == UiKey::Enter || event.key == UiKey::Space);
        if ((pointer_activation || key_activation) && activate_handler_) {
            activate_handler_();
            return UiEventReply::StopPropagation;
        }
        return base;
    }

private:
    QuestState state_ = QuestState::kLocked;
    bool selected_ = false;
    ActivateHandler activate_handler_;
};

struct QuestGraphEdge {
    Vec2 source{};
    Vec2 target{};
    bool source_completed = false;
};

class QuestGraphLinksView final : public View {
public:
    QuestGraphLinksView(std::string id, Vec2 world_size, std::vector<QuestGraphEdge> edges)
        : View(std::move(id)), world_size_(world_size), edges_(std::move(edges)) {
        set_hit_test_visible(false);
    }

    void measure(MeasureSpec width, MeasureSpec height, TextEngine&) override {
        measured_size_.x = resolve_axis(layout_params_.width, width, world_size_.x);
        measured_size_.y = resolve_axis(layout_params_.height, height, world_size_.y);
    }

    void paint(Arc2DCommandBuffer& out, TextEngine& text_engine,
               const snt::ui::UiTheme& theme) const override {
        View::paint(out, text_engine, theme);
        if (visibility() != Visibility::Visible || world_size_.x <= 0.0f || world_size_.y <= 0.0f) {
            return;
        }

        const auto project = [this](Vec2 point) {
            return Vec2{
                bounds_.pos.x + point.x * bounds_.size.x / world_size_.x,
                bounds_.pos.y + point.y * bounds_.size.y / world_size_.y,
            };
        };
        constexpr float kGridStep = 100.0f;
        for (float x = 0.0f; x <= world_size_.x; x += kGridStep) {
            const float sx = project({x, 0.0f}).x;
            out.rect({.pos = {sx, bounds_.pos.y}, .size = {1.0f, bounds_.size.y}},
                     {38, 51, 60, 90});
        }
        for (float y = 0.0f; y <= world_size_.y; y += kGridStep) {
            const float sy = project({0.0f, y}).y;
            out.rect({.pos = {bounds_.pos.x, sy}, .size = {bounds_.size.x, 1.0f}},
                     {38, 51, 60, 90});
        }
        for (const QuestGraphEdge& edge : edges_) {
            const Vec2 source = project(edge.source);
            const Vec2 target = project(edge.target);
            const Color color = edge.source_completed ? Color{72, 170, 106, 255}
                                                       : Color{102, 115, 125, 220};
            const float middle_x = (source.x + target.x) * 0.5f;
            const float horizontal_a = std::min(source.x, middle_x);
            const float horizontal_b = std::max(source.x, middle_x);
            out.rect({.pos = {horizontal_a, source.y - 1.0f},
                      .size = {std::max(1.0f, horizontal_b - horizontal_a), 2.0f}}, color);
            const float vertical_a = std::min(source.y, target.y);
            const float vertical_b = std::max(source.y, target.y);
            out.rect({.pos = {middle_x - 1.0f, vertical_a},
                      .size = {2.0f, std::max(1.0f, vertical_b - vertical_a)}}, color);
            const float target_a = std::min(middle_x, target.x);
            const float target_b = std::max(middle_x, target.x);
            out.rect({.pos = {target_a, target.y - 1.0f},
                      .size = {std::max(1.0f, target_b - target_a), 2.0f}}, color);
        }
    }

private:
    Vec2 world_size_{};
    std::vector<QuestGraphEdge> edges_;
};

struct QuestBookPanelGeometry {
    float panel_width = 0.0f;
    float panel_height = 0.0f;
    float graph_x = 0.0f;
    float graph_y = 0.0f;
    float graph_width = 0.0f;
    float graph_height = 0.0f;
    float detail_x = 0.0f;
    float detail_y = 0.0f;
    float detail_width = 0.0f;
    float detail_height = 0.0f;
};

[[nodiscard]] QuestBookPanelGeometry panel_geometry(Vec2 viewport) {
    const float margin = viewport.x < 700.0f ? 8.0f : 24.0f;
    QuestBookPanelGeometry geometry;
    geometry.panel_width = std::max(1.0f, std::min(1280.0f, viewport.x - margin * 2.0f));
    geometry.panel_height = std::max(1.0f, viewport.y - margin * 2.0f);
    const float inner_width = std::max(1.0f, geometry.panel_width - 24.0f);
    const float body_top = 104.0f;
    const float body_height = std::max(100.0f, geometry.panel_height - body_top - 12.0f);
    const bool stacked = geometry.panel_width < 760.0f;
    if (stacked) {
        geometry.graph_x = 12.0f;
        geometry.graph_y = body_top;
        geometry.graph_width = inner_width;
        geometry.graph_height = std::max(96.0f, body_height * 0.52f);
        geometry.detail_x = 12.0f;
        geometry.detail_y = geometry.graph_y + geometry.graph_height + 8.0f;
        geometry.detail_width = inner_width;
        geometry.detail_height = std::max(64.0f,
                                          geometry.panel_height - geometry.detail_y - 12.0f);
    } else {
        geometry.detail_width = std::clamp(inner_width * 0.34f, 288.0f, 420.0f);
        geometry.graph_width = std::max(120.0f, inner_width - geometry.detail_width - 10.0f);
        geometry.graph_x = 12.0f;
        geometry.graph_y = body_top;
        geometry.graph_height = body_height;
        geometry.detail_x = geometry.graph_x + geometry.graph_width + 10.0f;
        geometry.detail_y = body_top;
        geometry.detail_height = body_height;
    }
    return geometry;
}

[[nodiscard]] float chapter_tab_width(const QuestBookChapterView& chapter) {
    const float title_estimate = static_cast<float>(chapter.title.size()) * 8.5f + 28.0f;
    return std::clamp(title_estimate, 112.0f, 220.0f);
}

[[nodiscard]] Vec2 graph_world_size(const QuestBookChapterView* chapter) {
    float width = 1060.0f;
    float height = 620.0f;
    if (!chapter) return {width, height};
    for (const QuestBookQuestView& quest : chapter->quests) {
        width = std::max(width, quest.graph_position.x + kQuestNodeWidth + kGraphPadding);
        height = std::max(height, quest.graph_position.y + kQuestNodeHeight + kGraphPadding);
    }
    return {width, height};
}

void add_text(ViewGroup& parent, std::string id, std::string text, float size,
              Color color, float width, float height, float x, float y) {
    auto view = std::make_unique<TextView>(std::move(id));
    view->set_text(std::move(text));
    view->set_text_style({.size_px = size, .color = color});
    view->set_layout_params(fixed(width, height, x, y));
    parent.add_child(std::move(view));
}

void add_wrapped_text(ViewGroup& parent, std::string id, std::string text, float size,
                      Color color, size_t max_lines, float width, float height,
                      float x, float y) {
    auto view = std::make_unique<WrappedTextView>(std::move(id));
    view->set_text(std::move(text));
    view->set_text_style({.size_px = size, .color = color});
    view->set_max_lines(max_lines);
    view->set_layout_params(fixed(width, height, x, y));
    parent.add_child(std::move(view));
}

void build_detail_content(FrameLayout& detail, QuestBookViewModel& model,
                          float width, float minimum_height) {
    float y = 12.0f;
    const float text_width = std::max(1.0f, width - 24.0f);
    const QuestBookQuestView* quest = model.selected_quest();
    if (model.status() == QuestBookPresentationStatus::ContentMismatch) {
        add_wrapped_text(detail, "quest_book_content_mismatch",
                         "Task content does not match the connected server.", 16.0f,
                         {240, 177, 91, 255}, 4, text_width, 92.0f, 12.0f, y);
        y += 104.0f;
        add_text(detail, "quest_book_content_hash", "Reconnect with the matching game content.",
                 13.0f, {183, 194, 203, 255}, text_width, 28.0f, 12.0f, y);
    } else if (!quest) {
        const char* message = model.status() == QuestBookPresentationStatus::WaitingForSnapshot
            ? "Waiting for authoritative task progress."
            : "No visible tasks are defined.";
        add_wrapped_text(detail, "quest_book_empty", message, 16.0f,
                         {193, 205, 215, 255}, 4, text_width, 92.0f, 12.0f, y);
    } else {
        add_wrapped_text(detail, "quest_book_detail_title", quest->title, 19.0f,
                         {239, 245, 249, 255}, 2, text_width, 54.0f, 12.0f, y);
        y += 58.0f;
        add_text(detail, "quest_book_detail_state", state_label(quest->state), 13.0f,
                 state_color(quest->state), text_width, 24.0f, 12.0f, y);
        y += 30.0f;
        add_wrapped_text(detail, "quest_book_detail_description", quest->description, 14.0f,
                         {203, 214, 222, 255}, 5, text_width, 106.0f, 12.0f, y);
        y += 112.0f;

        size_t completed_objectives = 0;
        for (const QuestBookObjectiveView& objective : quest->objectives) {
            if (objective.current_count >= objective.required_count) ++completed_objectives;
        }
        add_text(detail, "quest_book_objectives_heading", "Objectives", 14.0f,
                 {153, 199, 222, 255}, text_width, 24.0f, 12.0f, y);
        y += 28.0f;
        auto progress = std::make_unique<ProgressBarView>("quest_book_objective_progress");
        progress->set_range(0.0f, static_cast<float>(std::max<size_t>(1, quest->objectives.size())));
        progress->set_value(static_cast<float>(completed_objectives));
        progress->set_layout_params(fixed(text_width, 16.0f, 12.0f, y));
        detail.add_child(std::move(progress));
        y += 22.0f;
        add_text(detail, "quest_book_objective_count",
                 std::to_string(completed_objectives) + " / " +
                     std::to_string(quest->objectives.size()),
                 12.0f, {184, 199, 207, 255}, text_width, 20.0f, 12.0f, y);
        y += 26.0f;
        for (size_t index = 0; index < quest->objectives.size(); ++index) {
            add_wrapped_text(detail, "quest_book_objective_" + std::to_string(index),
                             objective_text(quest->objectives[index]), 13.0f,
                             {211, 220, 225, 255}, 2, text_width, 42.0f, 12.0f, y);
            y += 46.0f;
        }

        add_text(detail, "quest_book_rewards_heading", "Rewards", 14.0f,
                 {236, 190, 96, 255}, text_width, 24.0f, 12.0f, y);
        y += 28.0f;
        for (size_t index = 0; index < quest->rewards.size(); ++index) {
            add_wrapped_text(detail, "quest_book_reward_" + std::to_string(index),
                             reward_text(quest->rewards[index]), 13.0f,
                             {219, 223, 208, 255}, 2, text_width, 42.0f, 12.0f, y);
            y += 46.0f;
        }
        const bool claimable = model.status() == QuestBookPresentationStatus::Ready &&
            quest->state == QuestState::kCompleted && !quest->reward_claimed;
        if (claimable || quest->reward_claimed) {
            auto claim = std::make_unique<Button>("quest_book_claim_reward");
            claim->set_text(quest->reward_claimed ? "Reward claimed" : "Claim reward");
            claim->set_enabled(claimable);
            claim->set_layout_params(fixed(text_width, 36.0f, 12.0f, y));
            claim->set_on_activate([&model] {
                static_cast<void>(model.claim_selected_reward());
            });
            detail.add_child(std::move(claim));
            y += 44.0f;
        }
    }

    if (!model.action_message().empty()) {
        y += 10.0f;
        add_wrapped_text(detail, "quest_book_action_message", model.action_message(), 12.0f,
                         {230, 184, 102, 255}, 3, text_width, 54.0f, 12.0f, y);
        y += 60.0f;
    }
    detail.set_layout_params(fixed(width, std::max(minimum_height, y + 12.0f)));
}

[[nodiscard]] std::unique_ptr<ModalView> build_quest_book_root(
    QuestBookViewModel& model, std::function<void()> on_close, Vec2 viewport,
    std::optional<std::pair<float, Vec2>> preserved_camera = std::nullopt) {
    const QuestBookPanelGeometry geometry = panel_geometry(viewport);
    const float panel_left = (viewport.x - geometry.panel_width) * 0.5f;
    const float panel_top = (viewport.y - geometry.panel_height) * 0.5f;

    auto root = std::make_unique<ModalView>("quest_book_modal");
    root->set_backdrop({0, 0, 0, 176});
    root->set_layout_params(fixed(std::max(1.0f, viewport.x), std::max(1.0f, viewport.y)));

    auto panel = std::make_unique<FrameLayout>("quest_book_panel");
    panel->set_background({20, 29, 36, 255}, 6.0f);
    panel->set_layout_params(fixed(geometry.panel_width, geometry.panel_height, panel_left, panel_top));

    auto header = std::make_unique<FrameLayout>("quest_book_header");
    header->set_background({28, 43, 52, 255}, 6.0f);
    header->set_layout_params(fixed(geometry.panel_width, 48.0f, 0.0f, 0.0f));
    add_text(*header, "quest_book_title", "Quest Book", 18.0f, {239, 245, 249, 255},
             180.0f, 28.0f, 14.0f, 10.0f);
    const size_t total = quest_count(model.chapters());
    add_text(*header, "quest_book_total_progress",
             std::to_string(completed_quest_count(model.chapters())) + " / " +
                 std::to_string(total),
             13.0f, {169, 199, 212, 255}, 86.0f, 24.0f,
             std::max(204.0f, geometry.panel_width - 154.0f), 12.0f);
    auto close = std::make_unique<Button>("quest_book_close");
    close->set_text("X");
    close->set_layout_params(fixed(32.0f, 30.0f, geometry.panel_width - 42.0f, 9.0f));
    close->set_on_activate(std::move(on_close));
    header->add_child(std::move(close));
    panel->add_child(std::move(header));

    auto tabs = std::make_unique<ScrollView>("quest_book_chapters");
    tabs->set_scroll_axis(ScrollAxis::Horizontal);
    tabs->set_layout_params(fixed(geometry.panel_width - 24.0f, 42.0f, 12.0f, 54.0f));
    auto tab_content = std::make_unique<FlexLayout>("quest_book_chapter_tabs");
    tab_content->set_orientation(snt::ui::Orientation::Horizontal);
    tab_content->set_spacing(6.0f);
    for (const QuestBookChapterView& chapter : model.chapters()) {
        auto tab = std::make_unique<QuestChapterTab>("quest_book_chapter_" + chapter.id,
                                                     chapter.id == model.selected_chapter_id());
        tab->set_text(chapter.title);
        tab->set_layout_params(fixed(chapter_tab_width(chapter), 34.0f));
        const std::string chapter_id = chapter.id;
        tab->set_on_activate([&model, chapter_id] { model.select_chapter(chapter_id); });
        tab_content->add_child(std::move(tab));
    }
    tabs->set_content(std::move(tab_content));
    panel->add_child(std::move(tabs));

    const QuestBookChapterView* chapter = model.selected_chapter();
    const Vec2 world_size = graph_world_size(chapter);
    auto graph = std::make_unique<PanZoomView>("quest_book_graph");
    graph->set_world_size(world_size);
    graph->set_background({17, 26, 33, 255}, 4.0f);
    graph->set_layout_params(fixed(geometry.graph_width, geometry.graph_height,
                                   geometry.graph_x, geometry.graph_y));

    std::map<std::string, const QuestBookQuestView*, std::less<>> graph_quests;
    std::vector<QuestGraphEdge> edges;
    if (chapter) {
        for (const QuestBookQuestView& quest : chapter->quests) {
            graph_quests.emplace(quest.id, &quest);
        }
        for (const QuestBookQuestView& quest : chapter->quests) {
            for (const std::string& prerequisite_id : quest.prerequisites) {
                const auto prerequisite = graph_quests.find(prerequisite_id);
                if (prerequisite == graph_quests.end()) continue;
                edges.push_back({
                    .source = {prerequisite->second->graph_position.x + kQuestNodeWidth,
                               prerequisite->second->graph_position.y + kQuestNodeHeight * 0.5f},
                    .target = {quest.graph_position.x,
                               quest.graph_position.y + kQuestNodeHeight * 0.5f},
                    .source_completed = prerequisite->second->state == QuestState::kCompleted,
                });
            }
        }
    }
    auto links = std::make_unique<QuestGraphLinksView>("quest_book_graph_links", world_size,
                                                        std::move(edges));
    links->set_layout_params(fixed(world_size.x, world_size.y));
    graph->add_child(std::move(links));
    if (chapter) {
        for (const QuestBookQuestView& quest : chapter->quests) {
            auto node = std::make_unique<QuestNodeView>("quest_book_node_" + quest.id,
                                                        quest.state,
                                                        quest.id == model.selected_quest_id());
            node->set_text(quest.title);
            node->set_layout_params(fixed(kQuestNodeWidth, kQuestNodeHeight,
                                          quest.graph_position.x, quest.graph_position.y));
            const std::string quest_id = quest.id;
            node->set_on_activate([&model, quest_id] { model.select_quest(quest_id); });
            graph->add_child(std::move(node));
        }
    }
    if (preserved_camera) {
        graph->set_zoom(preserved_camera->first);
        graph->set_world_origin(preserved_camera->second);
    } else if (const QuestBookQuestView* selected = model.selected_quest()) {
        graph->set_world_origin({std::max(0.0f, selected->graph_position.x - 140.0f),
                                std::max(0.0f, selected->graph_position.y - 120.0f)});
    }
    panel->add_child(std::move(graph));

    auto detail_scroll = std::make_unique<ScrollView>("quest_book_detail_scroll");
    detail_scroll->set_scroll_axis(ScrollAxis::Vertical);
    detail_scroll->set_background({27, 38, 46, 255}, 4.0f);
    detail_scroll->set_layout_params(fixed(geometry.detail_width, geometry.detail_height,
                                           geometry.detail_x, geometry.detail_y));
    auto detail = std::make_unique<FrameLayout>("quest_book_detail");
    build_detail_content(*detail, model, geometry.detail_width, geometry.detail_height);
    detail_scroll->set_content(std::move(detail));
    panel->add_child(std::move(detail_scroll));

    root->add_child(std::move(panel));
    return root;
}

struct QuestBookMountState {
    uint64_t model_revision = 0;
    Vec2 viewport{};
};

[[nodiscard]] bool same_viewport(Vec2 left, Vec2 right) noexcept {
    return left.x == right.x && left.y == right.y;
}

void replace_quest_book_children(ModalView& mounted_root, QuestBookViewModel& model,
                                 std::function<void()> on_close, Vec2 viewport) {
    std::optional<std::pair<float, Vec2>> camera;
    if (auto* existing_graph = dynamic_cast<PanZoomView*>(mounted_root.find("quest_book_graph"))) {
        camera = std::make_pair(existing_graph->zoom(), existing_graph->world_origin());
    }
    auto candidate = build_quest_book_root(model, std::move(on_close), viewport, camera);
    mounted_root.set_layout_params(candidate->layout_params());
    std::vector<std::unique_ptr<View>> children = std::move(candidate->children());
    mounted_root.clear_children();
    for (std::unique_ptr<View>& child : children) mounted_root.add_child(std::move(child));
}

}  // namespace

QuestBookViewModel::QuestBookViewModel(
    const GameContentRegistry& content,
    const replication::GameClientQuestBookState* authoritative_state,
    IQuestBookCommandSink* command_sink)
    : content_(&content), authoritative_state_(authoritative_state), command_sink_(command_sink) {}

void QuestBookViewModel::set_authoritative_state(
    const replication::GameClientQuestBookState* authoritative_state) noexcept {
    if (authoritative_state_ == authoritative_state) return;
    authoritative_state_ = authoritative_state;
    observed_content_revision_ = 0;
    observed_snapshot_id_ = std::numeric_limits<uint64_t>::max();
    observed_progress_revision_ = std::numeric_limits<uint64_t>::max();
    server_content_fingerprint_ = 0;
}

uint64_t QuestBookViewModel::local_content_fingerprint() const noexcept {
    return content_ ? content_->quest_content_fingerprint() : 0;
}

bool QuestBookViewModel::refresh() {
    if (!content_) return false;
    const QuestBookSnapshot* snapshot = authoritative_state_ ? authoritative_state_->snapshot() : nullptr;
    const uint64_t content_revision = content_->quest_content_revision();
    const uint64_t snapshot_id = authoritative_state_
        ? authoritative_state_->active_snapshot_id() : 0;
    const uint64_t progress_revision = snapshot ? snapshot->progress_revision : 0;
    const uint64_t content_fingerprint = snapshot ? snapshot->content_fingerprint : 0;
    if (observed_content_revision_ == content_revision && observed_snapshot_id_ == snapshot_id &&
        observed_progress_revision_ == progress_revision &&
        server_content_fingerprint_ == content_fingerprint) {
        return false;
    }

    rebuild(snapshot);
    observed_content_revision_ = content_revision;
    observed_snapshot_id_ = snapshot_id;
    observed_progress_revision_ = progress_revision;
    server_content_fingerprint_ = content_fingerprint;
    ++revision_;
    return true;
}

const QuestBookChapterView* QuestBookViewModel::selected_chapter() const noexcept {
    const auto found = std::find_if(chapters_.begin(), chapters_.end(), [this](const auto& chapter) {
        return chapter.id == selected_chapter_id_;
    });
    return found == chapters_.end() ? nullptr : &*found;
}

const QuestBookQuestView* QuestBookViewModel::find_quest(std::string_view quest_id) const noexcept {
    for (const QuestBookChapterView& chapter : chapters_) {
        const auto found = std::find_if(chapter.quests.begin(), chapter.quests.end(),
                                        [quest_id](const QuestBookQuestView& quest) {
                                            return quest.id == quest_id;
                                        });
        if (found != chapter.quests.end()) return &*found;
    }
    return nullptr;
}

const QuestBookQuestView* QuestBookViewModel::selected_quest() const noexcept {
    return find_quest(selected_quest_id_);
}

void QuestBookViewModel::select_chapter(std::string_view chapter_id) {
    const auto found = std::find_if(chapters_.begin(), chapters_.end(), [chapter_id](const auto& chapter) {
        return chapter.id == chapter_id;
    });
    if (found == chapters_.end() || selected_chapter_id_ == found->id) return;
    selected_chapter_id_ = found->id;
    selected_quest_id_ = found->quests.empty() ? std::string{} : found->quests.front().id;
    ++revision_;
}

void QuestBookViewModel::select_quest(std::string_view quest_id) {
    const QuestBookQuestView* quest = find_quest(quest_id);
    if (!quest || selected_quest_id_ == quest->id) return;
    selected_quest_id_ = quest->id;
    selected_chapter_id_ = quest->chapter_id;
    ++revision_;
}

snt::core::Expected<void> QuestBookViewModel::claim_selected_reward() {
    const QuestBookQuestView* quest = selected_quest();
    if (status_ != QuestBookPresentationStatus::Ready || !quest) {
        return snt::core::Error{snt::core::ErrorCode::kInvalidState,
                                "Task-book reward claim requires an authoritative task snapshot"};
    }
    if (quest->state != QuestState::kCompleted || quest->reward_claimed) {
        return snt::core::Error{snt::core::ErrorCode::kInvalidState,
                                "Selected task does not have an unclaimed reward"};
    }
    if (!command_sink_) {
        return snt::core::Error{snt::core::ErrorCode::kInvalidState,
                                "Task-book reward command sink is unavailable"};
    }
    auto submitted = command_sink_->submit_quest_reward_claim(quest->id);
    if (!submitted) {
        action_message_ = "Reward claim was not sent: " + submitted.error().format();
        ++revision_;
        SNT_LOG_WARN("Task-book reward claim was rejected before send for '%s': %s",
                     quest->id.c_str(), submitted.error().format().c_str());
        return submitted.error();
    }
    action_message_ = "Reward claim requested. Waiting for server confirmation.";
    ++revision_;
    SNT_LOG_INFO("Task-book reward claim queued for '%s'", quest->id.c_str());
    return {};
}

void QuestBookViewModel::rebuild(const QuestBookSnapshot* snapshot) {
    chapters_.clear();
    action_message_.clear();
    if (snapshot && snapshot->content_fingerprint != local_content_fingerprint()) {
        status_ = QuestBookPresentationStatus::ContentMismatch;
        selected_chapter_id_.clear();
        selected_quest_id_.clear();
        SNT_LOG_WARN("Task-book content fingerprint mismatch: client=%016llx server=%016llx",
                     static_cast<unsigned long long>(local_content_fingerprint()),
                     static_cast<unsigned long long>(snapshot->content_fingerprint));
        return;
    }

    std::map<std::string, const QuestProgressRecord*, std::less<>> progress;
    if (snapshot) {
        for (const QuestProgressRecord& record : snapshot->progress) {
            progress.emplace(record.quest_id, &record);
            if (!content_->find_quest(record.quest_id)) {
                SNT_LOG_WARN("Task-book snapshot contains undefined quest '%s'", record.quest_id.c_str());
            }
        }
    }

    const auto add_chapter = [this](QuestBookChapterDefinition definition) {
        QuestBookChapterView chapter;
        chapter.id = std::move(definition.id);
        chapter.title = std::move(definition.title);
        chapter.description = std::move(definition.description);
        chapter.icon_key = std::move(definition.icon_key);
        chapter.sort_order = definition.sort_order;
        chapters_.push_back(std::move(chapter));
    };
    for (QuestBookChapterDefinition definition : content_->quest_chapter_definitions()) {
        add_chapter(std::move(definition));
    }
    std::sort(chapters_.begin(), chapters_.end(), [](const auto& left, const auto& right) {
        if (left.sort_order != right.sort_order) return left.sort_order < right.sort_order;
        return left.id < right.id;
    });

    const auto chapter_for = [this](std::string_view id) -> QuestBookChapterView* {
        const auto found = std::find_if(chapters_.begin(), chapters_.end(), [id](const auto& chapter) {
            return chapter.id == id;
        });
        return found == chapters_.end() ? nullptr : &*found;
    };

    for (const QuestDefinition& definition : content_->quest_definitions()) {
        const auto progress_it = progress.find(definition.id);
        const QuestProgressRecord* record = progress_it == progress.end() ? nullptr : progress_it->second;
        const QuestState state = record ? record->state : QuestState::kLocked;
        if (definition.hidden && state == QuestState::kLocked) continue;

        QuestBookChapterView* chapter = chapter_for(definition.chapter_id);
        if (!chapter) {
            QuestBookChapterDefinition fallback;
            fallback.id = definition.chapter_id;
            fallback.title = definition.chapter_id;
            fallback.sort_order = std::numeric_limits<int32_t>::max();
            add_chapter(std::move(fallback));
            chapter = &chapters_.back();
            SNT_LOG_WARN("Task-book quest '%s' references undefined chapter '%s'",
                         definition.id.c_str(), definition.chapter_id.c_str());
        }

        QuestBookQuestView quest;
        quest.id = definition.id;
        quest.chapter_id = definition.chapter_id;
        quest.title = definition.title;
        quest.description = definition.description;
        quest.icon_key = definition.icon_key;
        quest.graph_position = {definition.node_position.x, definition.node_position.y};
        quest.prerequisites = definition.prerequisites;
        quest.state = state;
        quest.reward_claimed = record && record->reward_claimed;
        for (const QuestObjectiveDefinition& objective : definition.objectives) {
            int32_t current = 0;
            if (record) {
                const auto count = record->objective_counts.find(objective.id);
                if (count != record->objective_counts.end()) current = count->second;
            }
            quest.objectives.push_back({
                .id = objective.id,
                .target_id = objective.target_id,
                .current_count = current,
                .required_count = objective.required_count,
                .kind = objective.kind,
            });
        }
        for (const QuestRewardDefinition& reward : definition.rewards) {
            quest.rewards.push_back({
                .kind = reward.kind,
                .target_id = reward.target_id,
                .count = reward.count,
            });
        }
        chapter->quests.push_back(std::move(quest));
    }

    std::erase_if(chapters_, [](const QuestBookChapterView& chapter) {
        return chapter.quests.empty();
    });
    for (QuestBookChapterView& chapter : chapters_) {
        std::sort(chapter.quests.begin(), chapter.quests.end(), [](const auto& left, const auto& right) {
            if (left.graph_position.x != right.graph_position.x) {
                return left.graph_position.x < right.graph_position.x;
            }
            if (left.graph_position.y != right.graph_position.y) {
                return left.graph_position.y < right.graph_position.y;
            }
            return left.id < right.id;
        });
    }
    if (chapters_.empty()) {
        status_ = QuestBookPresentationStatus::Empty;
    } else {
        status_ = snapshot ? QuestBookPresentationStatus::Ready
                           : QuestBookPresentationStatus::WaitingForSnapshot;
    }
    choose_valid_selection();
}

void QuestBookViewModel::choose_valid_selection() {
    const QuestBookChapterView* chapter = selected_chapter();
    if (!chapter && !chapters_.empty()) {
        selected_chapter_id_ = chapters_.front().id;
        chapter = &chapters_.front();
    }
    const QuestBookQuestView* quest = selected_quest();
    if ((!quest || !chapter || quest->chapter_id != chapter->id) && chapter) {
        selected_quest_id_ = chapter->quests.empty() ? std::string{} : chapter->quests.front().id;
    }
}

snt::ui::UiScreenFactory make_quest_book_ui_factory(
    QuestBookViewModel& model, std::function<void()> on_close) {
    return [&model, on_close = std::move(on_close)](const snt::ui::UiScreenMountContext& context)
        -> snt::core::Expected<snt::ui::UiScreenMount> {
        (void)model.refresh();
        auto root = build_quest_book_root(model, on_close, context.viewport);
        auto state = std::make_shared<QuestBookMountState>();
        state->model_revision = model.revision();
        state->viewport = context.viewport;
        return snt::ui::UiScreenMount{
            .root = std::move(root),
            .update = [&model, on_close, state](View& mounted_root,
                                                  const snt::ui::UiScreenFrameContext& frame_context) {
                (void)model.refresh();
                if (state->model_revision == model.revision() &&
                    same_viewport(state->viewport, frame_context.viewport)) {
                    return;
                }
                auto* modal = dynamic_cast<ModalView*>(&mounted_root);
                if (!modal) {
                    SNT_LOG_ERROR("Task-book retained mount root has an invalid type");
                    return;
                }
                replace_quest_book_children(*modal, model, on_close, frame_context.viewport);
                state->model_revision = model.revision();
                state->viewport = frame_context.viewport;
            },
        };
    };
}

}  // namespace snt::game
