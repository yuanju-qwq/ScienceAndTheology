// ScienceAndTheology dedicated simulation-server host.
//
// This executable intentionally links only the SDL/Vulkan-free simulation
// closure plus the explicit server replication composition. --ticks remains
// network-disabled unless --network is supplied, so bounded smoke runs never
// contend for developer ports.

#define SNT_LOG_CHANNEL "game.server_host"
#include "game/runtime/runtime_package.h"
#include "game/server/science_and_theology_server_session.h"

#include "core/log.h"
#include "engine/simulation_runtime.h"

#include <charconv>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>

namespace {

struct ServerOptions {
    bool show_help = false;
    bool network_requested = false;
    std::optional<uint64_t> tick_limit;
    std::optional<std::string> bind_address;
    std::optional<uint16_t> tcp_port;
    std::optional<uint16_t> udp_port;
};

snt::core::Expected<uint16_t> parse_port(std::string_view option, std::string_view value) {
    uint32_t port = 0;
    const auto [end, parse_error] = std::from_chars(value.data(), value.data() + value.size(), port);
    if (parse_error != std::errc{} || end != value.data() + value.size() ||
        port > std::numeric_limits<uint16_t>::max()) {
        return snt::core::Error{snt::core::ErrorCode::kInvalidArgument,
                                std::string(option) + " requires a port in [0, 65535]"};
    }
    return static_cast<uint16_t>(port);
}

snt::core::Expected<ServerOptions> parse_server_options(int argc, char* argv[]) {
    ServerOptions options;
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument(argv[index]);
        if (argument == "--help" || argument == "-h") {
            options.show_help = true;
            continue;
        }
        if (argument == "--network") {
            options.network_requested = true;
            continue;
        }
        if (argument == "--bind") {
            if (index + 1 >= argc) {
                return snt::core::Error{snt::core::ErrorCode::kInvalidArgument,
                                        "--bind requires an IPv4 address"};
            }
            options.network_requested = true;
            options.bind_address = argv[++index];
            continue;
        }
        if (argument == "--tcp-port" || argument == "--udp-port") {
            if (index + 1 >= argc) {
                return snt::core::Error{snt::core::ErrorCode::kInvalidArgument,
                                        std::string(argument) + " requires a port"};
            }
            auto port = parse_port(argument, argv[++index]);
            if (!port) return port.error();
            options.network_requested = true;
            if (argument == "--tcp-port") options.tcp_port = *port;
            else options.udp_port = *port;
            continue;
        }
        if (argument != "--ticks") {
            return snt::core::Error{snt::core::ErrorCode::kInvalidArgument,
                                    "Unknown server argument: " + std::string(argument)};
        }
        if (index + 1 >= argc) {
            return snt::core::Error{snt::core::ErrorCode::kInvalidArgument,
                                    "--ticks requires an unsigned integer"};
        }

        const std::string_view value(argv[++index]);
        uint64_t tick_limit = 0;
        const auto [end, parse_error] = std::from_chars(
            value.data(), value.data() + value.size(), tick_limit);
        if (parse_error != std::errc{} || end != value.data() + value.size()) {
            return snt::core::Error{snt::core::ErrorCode::kInvalidArgument,
                                    "--ticks requires an unsigned integer"};
        }
        options.tick_limit = tick_limit;
    }
    return options;
}

void print_usage() {
    std::puts("Usage: science_and_theology_server [--ticks <count>] [--network]");
    std::puts("       [--bind <ipv4>] [--tcp-port <port>] [--udp-port <port>]");
    std::puts("Bounded --ticks runs open no ports unless --network or a network override is supplied.");
}

}  // namespace

int main(int argc, char* argv[]) {
    auto options = parse_server_options(argc, argv);
    if (!options) {
        SNT_LOG_ERROR("Dedicated server arguments are invalid: %s", options.error().format().c_str());
        print_usage();
        return 2;
    }
    if (options->show_help) {
        print_usage();
        return 0;
    }

    auto package = snt::game::load_runtime_package(
        argc > 0 ? argv[0] : "science_and_theology_server");
    if (!package) {
        SNT_LOG_ERROR("Dedicated server package load failed: %s", package.error().format().c_str());
        return 1;
    }

    snt::engine::SimulationRuntime runtime;
    auto session_config = std::move(package->session_config);
    const bool use_network = options->network_requested ||
        (!options->tick_limit && session_config.server_network.enabled);
    session_config.server_network.enabled = use_network;
    if (options->bind_address) session_config.server_network.bind_address = *options->bind_address;
    if (options->tcp_port) session_config.server_network.tcp_port = *options->tcp_port;
    if (options->udp_port) session_config.server_network.udp_port = *options->udp_port;

    auto session = std::make_unique<snt::game::ScienceAndTheologyServerSession>(
        std::move(session_config));
    if (auto result = runtime.init(package->runtime_config, package->paths, std::move(session)); !result) {
        SNT_LOG_ERROR("Dedicated server startup failed: %s", result.error().format().c_str());
        return 1;
    }

    int exit_code = 0;
    if (options->tick_limit) {
        SNT_LOG_INFO("Dedicated server bounded run started: %llu fixed tick(s)",
                     static_cast<unsigned long long>(*options->tick_limit));
        if (auto result = runtime.run_fixed_ticks(*options->tick_limit); !result) {
            SNT_LOG_ERROR("Dedicated server fixed-tick run failed: %s", result.error().format().c_str());
            exit_code = 1;
        }
    } else {
        SNT_LOG_INFO("Dedicated server simulation loop started with %s",
                     use_network ? "TCP+UDP replication" : "network transport disabled");
        runtime.run();
    }
    runtime.shutdown();
    return exit_code;
}
