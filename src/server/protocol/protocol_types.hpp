#pragma once

#include <cstdint>
#include <cstddef>

namespace science_and_theology {
namespace server {

// ============================================================================
// snt_server binary protocol — packet types and frame layout.
//
// Design: docs/多人游戏系统设计.md §4 方案B
//
// Frame layout (little-endian, 20-byte header):
//   ┌──────────┬─────────┬──────┬──────────┬───────────┬─────────────┬─────────┬────────┐
//   │ magic(4) │ ver(1)  │type(1)│ flags(2) │ player_handle(8)│ payload_len(4)│ payload │ crc32(4)│
//   └──────────┴─────────┴──────┴──────────┴───────────┴─────────────┴─────────┴────────┘
//
// magic = "SNT1" (0x53 0x4E 0x54 0x31)
// crc32 covers bytes from magic through end of payload (header + payload),
// excluding the crc32 field itself.
// ============================================================================

// Protocol magic bytes.
inline constexpr uint32_t kProtocolMagic = 0x31544E53u;  // "SNT1" little-endian
inline constexpr char kProtocolMagicBytes[4] = { 'S', 'N', 'T', '1' };
inline constexpr uint8_t kProtocolVersion = 1;

// Frame header size in bytes.
inline constexpr size_t kFrameHeaderSize = 20;
// CRC32 field size.
inline constexpr size_t kFrameCrcSize = 4;
// Maximum payload size (4 MiB — generous for chunk snapshots).
inline constexpr uint32_t kMaxPayloadSize = 4u * 1024u * 1024u;

// Packet type enumeration. Kept as a uint8_t in the wire format so
// unknown types can be skipped gracefully by receivers.
enum class PacketType : uint8_t {
    // --- Command channel (TCP, reliable, ordered) ---
    CMD_LOGIN     = 1,   // client → server: login request (payload = credentials)
    CMD_COMMAND   = 2,   // client → server: gameplay command (payload = serialized cmd)
    CMD_LOGOUT    = 3,   // client → server: graceful logout

    // --- Sync channel (TCP, reliable, ordered) ---
    SYNC_SNAPSHOT      = 10,  // server → client: full chunk snapshot
    SYNC_DELTA         = 11,  // server → client: incremental delta
    SYNC_PLAYER_STATE  = 12,  // server → client: player position/state broadcast

    // --- Position channel (UDP, unreliable, high-frequency 20Hz) ---
    POS_UPDATE    = 20,  // bidirectional: compact player position

    // --- Discovery channel (UDP broadcast) ---
    DISCOVER_REQ    = 30,  // client → broadcast: LAN server discovery request
    DISCOVER_REPLY  = 31,  // server → unicast: discovery reply

    // --- Control ---
    LOGIN_ACCEPT  = 40,  // server → client: login accepted (payload = assigned player_handle)
    LOGIN_REJECT  = 41,  // server → client: login rejected (payload = reason string)
    HEARTBEAT     = 42,  // bidirectional: keepalive
    KICK          = 43,  // server → client: forced disconnect (payload = reason string)
};

// Frame flags (bitfield in the 2-byte flags field).
enum class FrameFlags : uint16_t {
    NONE      = 0,
    COMPRESSED = 1 << 0,  // payload is LZ4-compressed (future)
    ENCRYPTED  = 1 << 1,  // payload is encrypted (future)
};

inline FrameFlags operator|(FrameFlags a, FrameFlags b) {
    return static_cast<FrameFlags>(
        static_cast<uint16_t>(a) | static_cast<uint16_t>(b));
}
inline FrameFlags operator&(FrameFlags a, FrameFlags b) {
    return static_cast<FrameFlags>(
        static_cast<uint16_t>(a) & static_cast<uint16_t>(b));
}

// Default ports.
inline constexpr uint16_t kDefaultTcpPort = 8910;
inline constexpr uint16_t kDefaultUdpPort = 8911;
inline constexpr uint16_t kDefaultDiscoveryPort = 8912;

// Player cap (from design doc §7 Q3: 20 players).
inline constexpr size_t kMaxPlayers = 20;

} // namespace server
} // namespace science_and_theology
