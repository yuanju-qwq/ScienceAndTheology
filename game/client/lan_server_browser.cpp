// LAN server browser implementation.

#define SNT_LOG_CHANNEL "game.lan_browser"
#include "game/client/lan_server_browser.h"

#include "core/error.h"
#include "core/log.h"
#include "game/network/game_replication_protocol.h"
#include "ui/mui_gameplay_controls.h"
#include "ui/retained_mui_controls.h"
#include "ui/retained_mui_text_input.h"
#include "ui/retained_mui_view.h"

#include <algorithm>
#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

namespace snt::game {
namespace {

using snt::ui::Button;
using snt::ui::Color;
using snt::ui::FrameLayout;
using snt::ui::LayoutParams;
using snt::ui::ModalView;
using snt::ui::ScrollAxis;
using snt::ui::ScrollView;
using snt::ui::TextInput;
using snt::ui::TextStyle;
using snt::ui::TextView;
using snt::ui::Vec2;
using snt::ui::View;

[[nodiscard]] LayoutParams fixed(float width, float height, float left = 0.0f,
                                 float top = 0.0f) {
    LayoutParams params;
    params.width = std::max(0.0f, width);
    params.height = std::max(0.0f, height);
    params.margin.left = left;
    params.margin.top = top;
    return params;
}

[[nodiscard]] bool same_endpoint(const replication::GameLanDiscoveredServer& left,
                                 const replication::GameLanDiscoveredServer& right) noexcept {
    return left.host == right.host && left.tcp_port == right.tcp_port &&
           left.udp_port == right.udp_port;
}

[[nodiscard]] bool same_server(const replication::GameLanDiscoveredServer& left,
                               const replication::GameLanDiscoveredServer& right) noexcept {
    return same_endpoint(left, right) && left.server_name == right.server_name &&
           left.current_players == right.current_players && left.max_players == right.max_players &&
           left.password_required == right.password_required;
}

[[nodiscard]] bool entry_less(const LanServerBrowserEntry& left,
                              const LanServerBrowserEntry& right) noexcept {
    if (left.server.server_name != right.server.server_name) {
        return left.server.server_name < right.server.server_name;
    }
    if (left.server.host != right.server.host) return left.server.host < right.server.host;
    if (left.server.tcp_port != right.server.tcp_port) {
        return left.server.tcp_port < right.server.tcp_port;
    }
    return left.server.udp_port < right.server.udp_port;
}

[[nodiscard]] std::string truncate_utf8(std::string value, size_t max_bytes) {
    if (value.size() <= max_bytes) return value;
    size_t end = max_bytes > 3 ? max_bytes - 3 : 0;
    while (end > 0 && (static_cast<unsigned char>(value[end]) & 0xC0u) == 0x80u) --end;
    if (end == 0) return "...";
    value.resize(end);
    value += "...";
    return value;
}

[[nodiscard]] std::string server_label(const replication::GameLanDiscoveredServer& server) {
    std::string label = server.server_name.empty() ? "Unnamed server" : server.server_name;
    label += "  ";
    label += server.host;
    label += ":";
    label += std::to_string(server.tcp_port);
    label += "  ";
    label += std::to_string(server.current_players);
    label += "/";
    label += std::to_string(server.max_players);
    label += server.password_required ? "  Password" : "  Open";
    return truncate_utf8(std::move(label), 72);
}

void set_text_style(TextView& view, float size, Color color) {
    TextStyle style = view.text_style();
    style.size_px = size;
    style.color = color;
    view.set_text_style(style);
}

void add_text(FrameLayout& parent, std::string id, std::string text, float size, Color color,
              float width, float height, float left, float top) {
    auto view = std::make_unique<TextView>(std::move(id));
    view->set_text(std::move(text));
    set_text_style(*view, size, color);
    view->set_layout_params(fixed(width, height, left, top));
    parent.add_child(std::move(view));
}

struct BrowserPanelGeometry {
    float width = 640.0f;
    float height = 460.0f;
    float left = 0.0f;
    float top = 0.0f;
};

[[nodiscard]] BrowserPanelGeometry panel_geometry(Vec2 viewport) {
    const float available_width = std::max(300.0f, viewport.x - 32.0f);
    const float available_height = std::max(260.0f, viewport.y - 32.0f);
    BrowserPanelGeometry geometry;
    geometry.width = std::min(680.0f, available_width);
    geometry.height = std::min(500.0f, available_height);
    geometry.left = std::max(16.0f, (viewport.x - geometry.width) * 0.5f);
    geometry.top = std::max(16.0f, (viewport.y - geometry.height) * 0.5f);
    return geometry;
}

[[nodiscard]] std::unique_ptr<ModalView> build_browser_root(
    LanServerBrowserModel& model, std::function<void()> on_close, Vec2 viewport) {
    const BrowserPanelGeometry geometry = panel_geometry(viewport);
    const float root_width = std::max(1.0f, viewport.x);
    const float root_height = std::max(1.0f, viewport.y);
    const float panel_width = geometry.width;
    const float list_top = 154.0f;
    const float list_height = std::max(72.0f, geometry.height - list_top - 14.0f);

    auto root = std::make_unique<ModalView>("lan_server_browser_modal");
    root->set_backdrop({0, 0, 0, 176});
    root->set_layout_params(fixed(root_width, root_height));

    auto panel = std::make_unique<FrameLayout>("lan_server_browser_panel");
    panel->set_background({20, 29, 36, 255}, 6.0f);
    panel->set_layout_params(fixed(panel_width, geometry.height, geometry.left, geometry.top));

    auto header = std::make_unique<FrameLayout>("lan_server_browser_header");
    header->set_background({28, 43, 52, 255}, 6.0f);
    header->set_layout_params(fixed(panel_width, 46.0f));
    add_text(*header, "lan_server_browser_title", "Local Network", 18.0f,
             {239, 245, 249, 255}, std::max(110.0f, panel_width - 202.0f), 28.0f, 14.0f, 9.0f);

    auto refresh = std::make_unique<Button>("lan_server_browser_refresh");
    refresh->set_text("Refresh");
    refresh->set_layout_params(fixed(78.0f, 30.0f, panel_width - 126.0f, 8.0f));
    refresh->set_on_activate([&model] { model.request_refresh(); });
    header->add_child(std::move(refresh));

    auto close = std::make_unique<Button>("lan_server_browser_close");
    close->set_text("X");
    close->set_layout_params(fixed(32.0f, 30.0f, panel_width - 40.0f, 8.0f));
    close->set_on_activate(std::move(on_close));
    header->add_child(std::move(close));
    panel->add_child(std::move(header));

    add_text(*panel, "lan_server_browser_status", model.status_text(), 13.0f,
             {169, 199, 212, 255}, panel_width - 28.0f, 22.0f, 14.0f, 56.0f);
    add_text(*panel, "lan_server_browser_password_label", "Server password", 13.0f,
             {204, 216, 224, 255}, 130.0f, 24.0f, 14.0f, 88.0f);

    auto password = std::make_unique<TextInput>("lan_server_browser_password");
    password->set_text_silently(model.server_password());
    password->set_placeholder("Only needed for protected servers");
    password->set_password(true);
    password->set_max_bytes(replication::kMaxGameServerPasswordBytes);
    password->set_background({14, 20, 26, 255}, 4.0f);
    password->set_layout_params(fixed(panel_width - 158.0f, 34.0f, 144.0f, 82.0f));
    password->set_on_change([&model](std::string_view value) {
        model.set_server_password(std::string(value));
    });
    panel->add_child(std::move(password));

    auto list = std::make_unique<ScrollView>("lan_server_browser_list");
    list->set_scroll_axis(ScrollAxis::Vertical);
    list->set_background({15, 23, 29, 255}, 4.0f);
    list->set_layout_params(fixed(panel_width - 28.0f, list_height, 14.0f, list_top));
    auto rows = std::make_unique<FrameLayout>("lan_server_browser_rows");
    const float row_width = panel_width - 28.0f;
    float row_top = 0.0f;
    for (const LanServerBrowserEntry& entry : model.entries()) {
        auto row = std::make_unique<FrameLayout>("lan_server_browser_server_" + entry.server.host +
                                                 "_" + std::to_string(entry.server.tcp_port));
        row->set_background({28, 40, 49, 255}, 4.0f);
        row->set_layout_params(fixed(row_width, 56.0f, 0.0f, row_top));
        add_text(*row, "lan_server_browser_server_label_" + entry.server.host + "_" +
                           std::to_string(entry.server.tcp_port),
                 server_label(entry.server), 13.0f, {222, 232, 238, 255},
                 std::max(120.0f, row_width - 102.0f), 38.0f, 12.0f, 9.0f);

        auto join = std::make_unique<Button>("lan_server_browser_join_" + entry.server.host +
                                             "_" + std::to_string(entry.server.tcp_port));
        join->set_text("Join");
        join->set_enabled(!model.connection_pending());
        join->set_layout_params(fixed(76.0f, 32.0f, row_width - 88.0f, 12.0f));
        const replication::GameLanDiscoveredServer server = entry.server;
        join->set_on_activate([&model, server] {
            static_cast<void>(model.request_join(server));
        });
        row->add_child(std::move(join));
        rows->add_child(std::move(row));
        row_top += 62.0f;
    }
    if (model.entries().empty()) {
        add_text(*rows, "lan_server_browser_empty", "No compatible LAN servers yet.", 14.0f,
                 {157, 176, 187, 255}, row_width - 24.0f, 30.0f, 12.0f, 14.0f);
        row_top = 52.0f;
    }
    rows->set_layout_params(fixed(row_width, std::max(52.0f, row_top)));
    list->set_content(std::move(rows));
    panel->add_child(std::move(list));

    root->add_child(std::move(panel));
    return root;
}

struct BrowserMountState {
    uint64_t model_revision = 0;
    Vec2 viewport{};
};

[[nodiscard]] bool same_viewport(Vec2 left, Vec2 right) noexcept {
    return left.x == right.x && left.y == right.y;
}

void replace_browser_children(ModalView& mounted_root, LanServerBrowserModel& model,
                              std::function<void()> on_close, Vec2 viewport) {
    auto candidate = build_browser_root(model, std::move(on_close), viewport);
    mounted_root.set_layout_params(candidate->layout_params());
    std::vector<std::unique_ptr<View>> children = std::move(candidate->children());
    mounted_root.clear_children();
    for (std::unique_ptr<View>& child : children) mounted_root.add_child(std::move(child));
}

}  // namespace

LanServerBrowserModel::LanServerBrowserModel(
    LanServerBrowserConfig config, std::unique_ptr<replication::GameLanDiscoveryClient> discovery)
    : config_(std::move(config)), discovery_(std::move(discovery)) {}

snt::core::Expected<std::unique_ptr<LanServerBrowserModel>> LanServerBrowserModel::create(
    LanServerBrowserConfig config) {
    if (config.query_interval_ticks == 0 || config.stale_after_ticks == 0) {
        return snt::core::Error{snt::core::ErrorCode::kInvalidArgument,
                                "LAN browser tick intervals must be positive"};
    }
    auto discovery = replication::GameLanDiscoveryClient::create(config.discovery);
    if (!discovery) {
        auto error = discovery.error();
        error.with_context("LanServerBrowserModel::create");
        return error;
    }
    return std::unique_ptr<LanServerBrowserModel>(
        new LanServerBrowserModel(std::move(config), std::move(*discovery)));
}

void LanServerBrowserModel::fixed_tick(uint64_t tick_index) {
    if (!discovery_) return;
    if (!has_queried_ || tick_index >= next_query_tick_) {
        auto query = discovery_->query();
        has_queried_ = true;
        next_query_tick_ = tick_index + config_.query_interval_ticks;
        if (!query) {
            report_discovery_error("broadcast query", query.error());
        } else {
            if (!last_discovery_error_.empty()) {
                last_discovery_error_.clear();
                refresh_idle_status();
            }
        }
    }

    auto discovered = discovery_->poll();
    if (!discovered) {
        report_discovery_error("reply poll", discovered.error());
        return;
    }
    for (replication::GameLanDiscoveredServer& server : *discovered) {
        merge_discovered_server(std::move(server), tick_index);
    }
    prune_stale_servers(tick_index);
    if (!pending_join_ && last_discovery_error_.empty()) refresh_idle_status();
}

void LanServerBrowserModel::request_refresh() {
    has_queried_ = false;
    next_query_tick_ = 0;
    last_discovery_error_.clear();
    if (!pending_join_) set_status("Searching local network...");
}

void LanServerBrowserModel::set_server_password(std::string password) {
    server_password_ = std::move(password);
}

snt::core::Expected<void> LanServerBrowserModel::request_join(
    replication::GameLanDiscoveredServer server) {
    if (pending_join_) {
        return snt::core::Error{snt::core::ErrorCode::kInvalidState,
                                "A LAN server connection is already pending"};
    }
    if (server.host.empty() || server.tcp_port == 0 || server.udp_port == 0) {
        set_status("Selected server has an invalid endpoint.");
        return snt::core::Error{snt::core::ErrorCode::kInvalidArgument,
                                "Selected LAN server has an invalid endpoint"};
    }
    if (server_password_.size() > replication::kMaxGameServerPasswordBytes ||
        server_password_.find('\0') != std::string::npos) {
        set_status("Server password must contain 1-256 non-NUL bytes.");
        return snt::core::Error{snt::core::ErrorCode::kInvalidArgument,
                                "LAN server password must contain at most 256 non-NUL bytes"};
    }
    if (server.password_required && server_password_.empty()) {
        set_status("The selected server requires a password.");
        return snt::core::Error{snt::core::ErrorCode::kInvalidArgument,
                                "Selected LAN server requires a password"};
    }

    LanServerJoinRequest request;
    request.server = std::move(server);
    if (request.server.password_required) request.server_password = server_password_;
    const std::string name = request.server.server_name.empty()
        ? request.server.host : request.server.server_name;
    pending_join_ = std::move(request);
    set_status("Connecting to " + name + "...");
    return {};
}

std::optional<LanServerJoinRequest> LanServerBrowserModel::take_join_request() {
    std::optional<LanServerJoinRequest> request = std::move(pending_join_);
    pending_join_.reset();
    return request;
}

void LanServerBrowserModel::report_connection_failure(std::string message) {
    if (message.empty()) message = "Connection ended before authentication completed.";
    set_status("Connection failed: " + truncate_utf8(std::move(message), 72));
}

void LanServerBrowserModel::report_connection_established() {
    set_status("Connected.");
}

void LanServerBrowserModel::merge_discovered_server(
    replication::GameLanDiscoveredServer server, uint64_t tick_index) {
    const auto existing = std::find_if(entries_.begin(), entries_.end(), [&server](const auto& entry) {
        return same_endpoint(entry.server, server);
    });
    if (existing == entries_.end()) {
        entries_.push_back({.server = std::move(server), .last_seen_tick = tick_index});
        std::sort(entries_.begin(), entries_.end(), entry_less);
        ++revision_;
        return;
    }
    const bool changed = !same_server(existing->server, server);
    existing->server = std::move(server);
    existing->last_seen_tick = tick_index;
    if (changed) {
        std::sort(entries_.begin(), entries_.end(), entry_less);
        ++revision_;
    }
}

void LanServerBrowserModel::prune_stale_servers(uint64_t tick_index) {
    const size_t before = entries_.size();
    std::erase_if(entries_, [this, tick_index](const LanServerBrowserEntry& entry) {
        return tick_index > entry.last_seen_tick &&
            tick_index - entry.last_seen_tick > config_.stale_after_ticks;
    });
    if (entries_.size() != before) ++revision_;
}

void LanServerBrowserModel::set_status(std::string text) {
    text = truncate_utf8(std::move(text), 72);
    if (status_text_ == text) return;
    status_text_ = std::move(text);
    ++revision_;
}

void LanServerBrowserModel::report_discovery_error(std::string operation,
                                                    const snt::core::Error& error) {
    const std::string formatted = operation + ": " + error.format();
    if (last_discovery_error_ == formatted) return;
    last_discovery_error_ = formatted;
    set_status("LAN discovery unavailable. Check address and port.");
    SNT_LOG_WARN("LAN discovery %s failed: %s", operation.c_str(), error.format().c_str());
}

void LanServerBrowserModel::refresh_idle_status() {
    if (pending_join_ || !last_discovery_error_.empty()) return;
    if (entries_.empty()) {
        set_status("Searching local network...");
        return;
    }
    set_status(std::to_string(entries_.size()) + " compatible LAN server(s) found.");
}

snt::ui::UiScreenFactory make_lan_server_browser_ui_factory(
    LanServerBrowserModel& model, std::function<void()> on_close) {
    return [&model, on_close = std::move(on_close)](const snt::ui::UiScreenMountContext& context)
        -> snt::core::Expected<snt::ui::UiScreenMount> {
        auto root = build_browser_root(model, on_close, context.viewport);
        auto state = std::make_shared<BrowserMountState>();
        state->model_revision = model.revision();
        state->viewport = context.viewport;
        return snt::ui::UiScreenMount{
            .root = std::move(root),
            .update = [&model, on_close, state](View& mounted_root,
                                                  const snt::ui::UiScreenFrameContext& frame_context) {
                if (state->model_revision == model.revision() &&
                    same_viewport(state->viewport, frame_context.viewport)) {
                    return;
                }
                auto* modal = dynamic_cast<ModalView*>(&mounted_root);
                if (!modal) {
                    SNT_LOG_ERROR("LAN browser retained mount root has an invalid type");
                    return;
                }
                replace_browser_children(*modal, model, on_close, frame_context.viewport);
                state->model_revision = model.revision();
                state->viewport = frame_context.viewport;
            },
        };
    };
}

}  // namespace snt::game
