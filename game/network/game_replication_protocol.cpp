// Game-owned replication envelope and first typed message codecs.

#include "game/network/game_replication_protocol.h"

#include "core/error.h"

#include <limits>
#include <string>
#include <utility>

namespace snt::game::replication {
namespace {

[[nodiscard]] snt::core::Error protocol_error(std::string message) {
    return {snt::core::ErrorCode::kProtocolError, std::move(message)};
}

void append_u16(std::vector<std::byte>& bytes, uint16_t value) {
    bytes.push_back(static_cast<std::byte>((value >> 8u) & 0xffu));
    bytes.push_back(static_cast<std::byte>(value & 0xffu));
}

void append_u32(std::vector<std::byte>& bytes, uint32_t value) {
    for (int shift = 24; shift >= 0; shift -= 8) {
        bytes.push_back(static_cast<std::byte>((value >> shift) & 0xffu));
    }
}

void append_u64(std::vector<std::byte>& bytes, uint64_t value) {
    for (int shift = 56; shift >= 0; shift -= 8) {
        bytes.push_back(static_cast<std::byte>((value >> shift) & 0xffu));
    }
}

[[nodiscard]] uint16_t read_u16(std::span<const std::byte> bytes, size_t offset) {
    return static_cast<uint16_t>(std::to_integer<uint8_t>(bytes[offset])) << 8u |
           static_cast<uint16_t>(std::to_integer<uint8_t>(bytes[offset + 1]));
}

[[nodiscard]] uint32_t read_u32(std::span<const std::byte> bytes, size_t offset) {
    uint32_t value = 0;
    for (size_t index = 0; index < 4; ++index) {
        value = (value << 8u) | std::to_integer<uint8_t>(bytes[offset + index]);
    }
    return value;
}

[[nodiscard]] uint64_t read_u64(std::span<const std::byte> bytes, size_t offset) {
    uint64_t value = 0;
    for (size_t index = 0; index < 8; ++index) {
        value = (value << 8u) | std::to_integer<uint8_t>(bytes[offset + index]);
    }
    return value;
}

[[nodiscard]] snt::core::Expected<void> validate_message_shape(
    const GameReplicationMessage& message) {
    if (message.protocol_version != kCurrentGameReplicationProtocolVersion) {
        return protocol_error("Game replication protocol version does not match");
    }
    if (!is_known_game_replication_message_kind(message.kind)) {
        return protocol_error("Unknown game replication message kind");
    }
    if (message.payload.size() > kMaxGameReplicationPayloadBytes) {
        return protocol_error("Game replication payload exceeds the configured limit");
    }
    return {};
}

[[nodiscard]] snt::core::Expected<void> validate_message_kind(
    const GameReplicationMessage& message, GameReplicationMessageKind expected) {
    if (auto result = validate_message_shape(message); !result) return result.error();
    if (message.kind != expected) {
        return protocol_error("Game replication message kind does not match the typed payload");
    }
    return {};
}

[[nodiscard]] snt::core::Expected<void> append_short_string(
    std::vector<std::byte>& bytes, const std::string& value, size_t maximum,
    const char* field_name, bool require_non_empty) {
    if (require_non_empty && value.empty()) {
        return protocol_error(std::string(field_name) + " must not be empty");
    }
    if (value.size() > maximum || value.size() > std::numeric_limits<uint16_t>::max()) {
        return protocol_error(std::string(field_name) + " exceeds the protocol limit");
    }
    append_u16(bytes, static_cast<uint16_t>(value.size()));
    for (const char value_byte : value) {
        bytes.push_back(static_cast<std::byte>(static_cast<unsigned char>(value_byte)));
    }
    return {};
}

[[nodiscard]] snt::core::Expected<std::string> read_short_string(
    std::span<const std::byte> bytes, size_t& offset, size_t maximum,
    const char* field_name, bool require_non_empty) {
    if (bytes.size() - offset < sizeof(uint16_t)) {
        return protocol_error(std::string("Game replication ") + field_name + " is truncated");
    }
    const size_t size = read_u16(bytes, offset);
    offset += sizeof(uint16_t);
    if ((require_non_empty && size == 0) || size > maximum || bytes.size() - offset < size) {
        return protocol_error(std::string("Game replication ") + field_name + " is invalid");
    }

    std::string value;
    value.reserve(size);
    for (size_t index = 0; index < size; ++index) {
        value.push_back(static_cast<char>(std::to_integer<uint8_t>(bytes[offset + index])));
    }
    offset += size;
    return value;
}

[[nodiscard]] snt::core::Expected<std::vector<std::byte>> read_byte_vector(
    std::span<const std::byte> bytes, size_t& offset, size_t maximum,
    const char* field_name) {
    if (bytes.size() - offset < sizeof(uint32_t)) {
        return protocol_error(std::string("Game replication ") + field_name + " is truncated");
    }
    const size_t size = read_u32(bytes, offset);
    offset += sizeof(uint32_t);
    if (size > maximum || bytes.size() - offset < size) {
        return protocol_error(std::string("Game replication ") + field_name + " exceeds the protocol limit");
    }

    std::vector<std::byte> value;
    value.insert(value.end(), bytes.begin() + static_cast<std::ptrdiff_t>(offset),
                 bytes.begin() + static_cast<std::ptrdiff_t>(offset + size));
    offset += size;
    return value;
}

}  // namespace

bool is_known_game_replication_message_kind(GameReplicationMessageKind kind) noexcept {
    switch (kind) {
        case GameReplicationMessageKind::kClientLoginRequest:
        case GameReplicationMessageKind::kClientCommand:
        case GameReplicationMessageKind::kClientInterestUpdate:
        case GameReplicationMessageKind::kClientSnapshotAcknowledgement:
        case GameReplicationMessageKind::kServerLoginAccepted:
        case GameReplicationMessageKind::kServerSnapshot:
        case GameReplicationMessageKind::kServerDelta:
        case GameReplicationMessageKind::kServerNotice:
            return true;
    }
    return false;
}

bool is_client_game_replication_message(GameReplicationMessageKind kind) noexcept {
    switch (kind) {
        case GameReplicationMessageKind::kClientLoginRequest:
        case GameReplicationMessageKind::kClientCommand:
        case GameReplicationMessageKind::kClientInterestUpdate:
        case GameReplicationMessageKind::kClientSnapshotAcknowledgement:
            return true;
        case GameReplicationMessageKind::kServerLoginAccepted:
        case GameReplicationMessageKind::kServerSnapshot:
        case GameReplicationMessageKind::kServerDelta:
        case GameReplicationMessageKind::kServerNotice:
            return false;
    }
    return false;
}

bool is_server_game_replication_message(GameReplicationMessageKind kind) noexcept {
    return is_known_game_replication_message_kind(kind) &&
           !is_client_game_replication_message(kind);
}

snt::core::Expected<void> validate_game_replication_channel(
    GameReplicationMessageKind kind, snt::network::ReplicationChannel channel) {
    if (!is_known_game_replication_message_kind(kind)) {
        return protocol_error("Unknown game replication message kind");
    }
    if (channel != snt::network::ReplicationChannel::Reliable) {
        return protocol_error("Game replication message was sent on the wrong channel");
    }
    return {};
}

snt::core::Expected<std::vector<std::byte>> encode_game_replication_message(
    const GameReplicationMessage& message) {
    if (auto result = validate_message_shape(message); !result) return result.error();
    if (message.payload.size() > std::numeric_limits<uint32_t>::max()) {
        return protocol_error("Game replication payload cannot be represented in 32 bits");
    }

    std::vector<std::byte> bytes;
    bytes.reserve(kGameReplicationHeaderBytes + message.payload.size());
    append_u32(bytes, kGameReplicationMagic);
    append_u16(bytes, message.protocol_version);
    bytes.push_back(static_cast<std::byte>(message.kind));
    bytes.push_back(std::byte{0});
    append_u32(bytes, static_cast<uint32_t>(message.payload.size()));
    bytes.insert(bytes.end(), message.payload.begin(), message.payload.end());
    return bytes;
}

snt::core::Expected<GameReplicationMessage> decode_game_replication_message(
    std::span<const std::byte> bytes) {
    if (bytes.size() < kGameReplicationHeaderBytes) {
        return protocol_error("Game replication message is incomplete");
    }
    if (read_u32(bytes, 0) != kGameReplicationMagic) {
        return protocol_error("Game replication magic does not match");
    }
    if (read_u16(bytes, 4) != kCurrentGameReplicationProtocolVersion) {
        return protocol_error("Game replication protocol version does not match");
    }
    if (std::to_integer<uint8_t>(bytes[7]) != 0) {
        return protocol_error("Game replication reserved header bits are non-zero");
    }

    const size_t payload_size = read_u32(bytes, 8);
    if (payload_size > kMaxGameReplicationPayloadBytes ||
        payload_size > std::numeric_limits<size_t>::max() - kGameReplicationHeaderBytes) {
        return protocol_error("Game replication payload exceeds the protocol limit");
    }
    if (kGameReplicationHeaderBytes + payload_size != bytes.size()) {
        return protocol_error("Game replication message has an invalid payload length");
    }

    GameReplicationMessage message;
    message.protocol_version = read_u16(bytes, 4);
    message.kind = static_cast<GameReplicationMessageKind>(std::to_integer<uint8_t>(bytes[6]));
    message.payload.assign(bytes.begin() + static_cast<std::ptrdiff_t>(kGameReplicationHeaderBytes),
                           bytes.end());
    if (auto result = validate_message_shape(message); !result) return result.error();
    return message;
}

snt::core::Expected<GameReplicationMessage> make_game_login_request(
    const GameLoginRequest& request) {
    if (request.credential.size() > kMaxGameCredentialBytes) {
        return protocol_error("Game login credential exceeds the protocol limit");
    }
    if (!is_valid_player_identity_provider(request.identity_provider)) {
        return protocol_error("Game login identity provider is invalid");
    }
    if (auto result = validate_player_display_name(request.display_name); !result) {
        return protocol_error("Game login display name is invalid");
    }
    if (request.identity_provider == PlayerIdentityProvider::kLocalName &&
        !request.credential.empty()) {
        return protocol_error("Local-name game login must not send credentials");
    }
    if (request.identity_provider == PlayerIdentityProvider::kSteam && request.credential.empty()) {
        return protocol_error("Steam game login requires an authentication credential");
    }

    GameReplicationMessage message;
    message.kind = GameReplicationMessageKind::kClientLoginRequest;
    message.payload.push_back(static_cast<std::byte>(request.identity_provider));
    if (auto result = append_short_string(message.payload, request.display_name,
                                          kMaxGamePlayerNameBytes, "login display name", true);
        !result) {
        return result.error();
    }
    append_u32(message.payload, static_cast<uint32_t>(request.credential.size()));
    message.payload.insert(message.payload.end(), request.credential.begin(), request.credential.end());
    return message;
}

snt::core::Expected<GameLoginRequest> parse_game_login_request(
    const GameReplicationMessage& message) {
    if (auto result = validate_message_kind(message, GameReplicationMessageKind::kClientLoginRequest);
        !result) {
        return result.error();
    }

    const std::span<const std::byte> bytes(message.payload.data(), message.payload.size());
    if (bytes.empty()) return protocol_error("Game login request is incomplete");
    size_t offset = 0;
    const auto identity_provider = static_cast<PlayerIdentityProvider>(
        std::to_integer<uint8_t>(bytes[offset++]));
    if (!is_valid_player_identity_provider(identity_provider)) {
        return protocol_error("Game login identity provider is invalid");
    }
    auto display_name = read_short_string(bytes, offset, kMaxGamePlayerNameBytes,
                                          "login display name", true);
    if (!display_name) return display_name.error();
    if (auto result = validate_player_display_name(*display_name); !result) {
        return protocol_error("Game login display name is invalid");
    }
    auto credential = read_byte_vector(bytes, offset, kMaxGameCredentialBytes, "login credential");
    if (!credential) return credential.error();
    if (identity_provider == PlayerIdentityProvider::kLocalName && !credential->empty()) {
        return protocol_error("Local-name game login must not send credentials");
    }
    if (identity_provider == PlayerIdentityProvider::kSteam && credential->empty()) {
        return protocol_error("Steam game login requires an authentication credential");
    }
    if (offset != bytes.size()) return protocol_error("Game login request has trailing bytes");

    return GameLoginRequest{.identity_provider = identity_provider,
                            .display_name = std::move(*display_name),
                            .credential = std::move(*credential)};
}

snt::core::Expected<GameReplicationMessage> make_game_login_accepted(
    const GameLoginAccepted& accepted) {
    if (auto result = validate_player_identity(accepted.identity); !result) {
        return protocol_error("Accepted game player identity is invalid");
    }
    GameReplicationMessage message;
    message.kind = GameReplicationMessageKind::kServerLoginAccepted;
    message.payload.push_back(static_cast<std::byte>(accepted.identity.provider));
    if (auto result = append_short_string(message.payload, accepted.identity.account_id,
                                          kMaxGamePlayerIdBytes, "accepted player id", true);
        !result) {
        return result.error();
    }
    if (auto result = append_short_string(message.payload, accepted.identity.display_name,
                                          kMaxGamePlayerNameBytes, "accepted player display name", true);
        !result) {
        return result.error();
    }
    return message;
}

snt::core::Expected<GameLoginAccepted> parse_game_login_accepted(
    const GameReplicationMessage& message) {
    if (auto result = validate_message_kind(message, GameReplicationMessageKind::kServerLoginAccepted);
        !result) {
        return result.error();
    }

    const std::span<const std::byte> bytes(message.payload.data(), message.payload.size());
    if (bytes.empty()) return protocol_error("Game login accepted payload is incomplete");
    size_t offset = 0;
    const auto identity_provider = static_cast<PlayerIdentityProvider>(
        std::to_integer<uint8_t>(bytes[offset++]));
    if (!is_valid_player_identity_provider(identity_provider)) {
        return protocol_error("Accepted game player identity provider is invalid");
    }
    auto account_id = read_short_string(bytes, offset, kMaxGamePlayerIdBytes,
                                        "accepted player id", true);
    if (!account_id) return account_id.error();
    auto display_name = read_short_string(bytes, offset, kMaxGamePlayerNameBytes,
                                          "accepted player display name", true);
    if (!display_name) return display_name.error();
    if (offset != bytes.size()) return protocol_error("Game login accepted has trailing bytes");

    PlayerIdentity identity{
        .provider = identity_provider,
        .account_id = std::move(*account_id),
        .display_name = std::move(*display_name),
    };
    if (auto result = validate_player_identity(identity); !result) {
        return protocol_error("Accepted game player identity is invalid");
    }
    return GameLoginAccepted{.identity = std::move(identity)};
}

snt::core::Expected<GameReplicationMessage> make_game_client_command(
    const GameClientCommand& command) {
    if (command.command_type == 0) {
        return protocol_error("Game client command type must be non-zero");
    }
    if (command.payload.size() > kMaxGameCommandPayloadBytes) {
        return protocol_error("Game client command payload exceeds the protocol limit");
    }

    GameReplicationMessage message;
    message.kind = GameReplicationMessageKind::kClientCommand;
    append_u64(message.payload, command.client_sequence);
    append_u16(message.payload, command.command_type);
    append_u32(message.payload, static_cast<uint32_t>(command.payload.size()));
    message.payload.insert(message.payload.end(), command.payload.begin(), command.payload.end());
    return message;
}

snt::core::Expected<GameClientCommand> parse_game_client_command(
    const GameReplicationMessage& message) {
    if (auto result = validate_message_kind(message, GameReplicationMessageKind::kClientCommand);
        !result) {
        return result.error();
    }

    const std::span<const std::byte> bytes(message.payload.data(), message.payload.size());
    constexpr size_t kCommandPrefixBytes = sizeof(uint64_t) + sizeof(uint16_t) + sizeof(uint32_t);
    if (bytes.size() < kCommandPrefixBytes) {
        return protocol_error("Game client command is incomplete");
    }

    size_t offset = 0;
    GameClientCommand command;
    command.client_sequence = read_u64(bytes, offset);
    offset += sizeof(uint64_t);
    command.command_type = read_u16(bytes, offset);
    offset += sizeof(uint16_t);
    if (command.command_type == 0) {
        return protocol_error("Game client command type must be non-zero");
    }
    auto payload = read_byte_vector(bytes, offset, kMaxGameCommandPayloadBytes,
                                    "client command payload");
    if (!payload) return payload.error();
    if (offset != bytes.size()) return protocol_error("Game client command has trailing bytes");
    command.payload = std::move(*payload);
    return command;
}

}  // namespace snt::game::replication
