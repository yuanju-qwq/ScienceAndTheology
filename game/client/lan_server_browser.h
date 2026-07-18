// LAN server browser state and presentation boundary.
//
// Discovery framing remains in snt_network and game protocol filtering remains
// in GameLanDiscoveryClient. This module owns only client-side polling,
// expiration, optional password input, and the explicit join request consumed
// by the graphical session.

#pragma once

#include "core/error.h"
#include "core/expected.h"
#include "game/network/game_lan_discovery.h"
#include "ui/retained_mui_screen_stack.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace snt::game {

struct LanServerBrowserConfig {
    replication::GameLanDiscoveryClientConfig discovery{};
    uint64_t query_interval_ticks = 20;
    uint64_t stale_after_ticks = 100;
};

struct LanServerBrowserEntry {
    replication::GameLanDiscoveredServer server;
    uint64_t last_seen_tick = 0;
};

// Password material is consumed by the session exactly once when a user joins
// a server. It is intentionally runtime-only and must never enter config,
// persistence, discovery payloads, or logs.
struct LanServerJoinRequest {
    replication::GameLanDiscoveredServer server;
    std::string server_password;
};

class LanServerBrowserModel final {
public:
    [[nodiscard]] static snt::core::Expected<std::unique_ptr<LanServerBrowserModel>> create(
        LanServerBrowserConfig config = {});

    LanServerBrowserModel(const LanServerBrowserModel&) = delete;
    LanServerBrowserModel& operator=(const LanServerBrowserModel&) = delete;

    // Called from the client fixed-tick boundary. Socket errors become a
    // visible browser status so a failed broadcast does not terminate the
    // graphical client. Queries are interval-limited rather than emitted per
    // tick.
    void fixed_tick(uint64_t tick_index);
    void request_refresh();

    void set_server_password(std::string password);
    [[nodiscard]] const std::string& server_password() const noexcept { return server_password_; }

    [[nodiscard]] const std::vector<LanServerBrowserEntry>& entries() const noexcept {
        return entries_;
    }
    [[nodiscard]] const std::string& status_text() const noexcept { return status_text_; }
    [[nodiscard]] bool connection_pending() const noexcept { return pending_join_.has_value(); }
    [[nodiscard]] uint64_t revision() const noexcept { return revision_; }

    // Queues a selected compatible endpoint. Open servers always receive an
    // empty password even if the input field still contains text from a prior
    // protected server.
    [[nodiscard]] snt::core::Expected<void> request_join(
        replication::GameLanDiscoveredServer server);
    [[nodiscard]] std::optional<LanServerJoinRequest> take_join_request();

    void report_connection_failure(std::string message);
    void report_connection_established();

private:
    LanServerBrowserModel(LanServerBrowserConfig config,
                          std::unique_ptr<replication::GameLanDiscoveryClient> discovery);

    void merge_discovered_server(replication::GameLanDiscoveredServer server, uint64_t tick_index);
    void prune_stale_servers(uint64_t tick_index);
    void set_status(std::string text);
    void report_discovery_error(std::string operation, const snt::core::Error& error);
    void refresh_idle_status();

    LanServerBrowserConfig config_;
    std::unique_ptr<replication::GameLanDiscoveryClient> discovery_;
    std::vector<LanServerBrowserEntry> entries_;
    std::optional<LanServerJoinRequest> pending_join_;
    std::string server_password_;
    std::string status_text_ = "Searching local network...";
    std::string last_discovery_error_;
    uint64_t next_query_tick_ = 0;
    uint64_t revision_ = 1;
    bool has_queried_ = false;
};

// Retained-MUI factory for the browser. The UI only mutates the model; the
// client session owns transport creation after it drains a join request.
[[nodiscard]] snt::ui::UiScreenFactory make_lan_server_browser_ui_factory(
    LanServerBrowserModel& model, std::function<void()> on_close);

}  // namespace snt::game
