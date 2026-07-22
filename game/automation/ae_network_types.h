// Durable and runtime-neutral AE network node categories.
//
// This small value contract is shared by chunk sidecars and the active AE
// topology owner. It intentionally carries no ResourceKey, storage pointer,
// or graph allocation so saved topology remains independent from one content
// snapshot and hot runtime lookup stays compact.

#pragma once

#include <cstdint>

namespace snt::game {

enum class AeNetworkNodeType : uint8_t {
    kController = 0,
    kChannelProvider = 1,
    kDrive = 2,
    kStorageBus = 3,
    kInterface = 4,
    kTerminal = 5,
    kCable = 6,
};

[[nodiscard]] constexpr bool is_known_ae_network_node_type(
    AeNetworkNodeType type) noexcept {
    switch (type) {
        case AeNetworkNodeType::kController:
        case AeNetworkNodeType::kChannelProvider:
        case AeNetworkNodeType::kDrive:
        case AeNetworkNodeType::kStorageBus:
        case AeNetworkNodeType::kInterface:
        case AeNetworkNodeType::kTerminal:
        case AeNetworkNodeType::kCable:
            return true;
    }
    return false;
}

[[nodiscard]] constexpr bool ae_network_node_is_channel_provider(
    AeNetworkNodeType type) noexcept {
    return type == AeNetworkNodeType::kController ||
        type == AeNetworkNodeType::kChannelProvider;
}

[[nodiscard]] constexpr bool ae_network_node_is_device(
    AeNetworkNodeType type) noexcept {
    switch (type) {
        case AeNetworkNodeType::kDrive:
        case AeNetworkNodeType::kStorageBus:
        case AeNetworkNodeType::kInterface:
        case AeNetworkNodeType::kTerminal:
            return true;
        case AeNetworkNodeType::kController:
        case AeNetworkNodeType::kChannelProvider:
        case AeNetworkNodeType::kCable:
            return false;
    }
    return false;
}

}  // namespace snt::game
