#pragma once

#include <cstdint>
#include <cstring>
#include <vector>
#include <string>
#include <optional>

#include "protocol_types.hpp"

namespace science_and_theology {
namespace server {

// A decoded network frame.
struct Frame {
    uint8_t version = 0;
    PacketType type = PacketType::HEARTBEAT;
    FrameFlags flags = FrameFlags::NONE;
    uint64_t player_handle = 0;
    std::vector<uint8_t> payload;

    bool is_compressed() const {
        return (flags & FrameFlags::COMPRESSED) != FrameFlags::NONE;
    }
};

// Result of an attempt to parse bytes from a stream buffer.
enum class DecodeStatus {
    OK,             // a full frame was decoded
    NEED_MORE_DATA, // not enough bytes yet; wait for more
    INVALID_MAGIC,  // magic bytes don't match — protocol error
    INVALID_VERSION,// unsupported version
    PAYLOAD_TOO_LARGE,
    CRC_MISMATCH,   // CRC32 doesn't match — corrupt frame
    INCOMPLETE,     // have header but not full payload+crc yet
};

// CRC32 (IEEE 802.3 polynomial, same as zlib/PNG).
// Implemented here to avoid a platform dependency.
uint32_t crc32_compute(const uint8_t* data, size_t length);

// Encode a frame into a byte buffer ready for transmission.
// Returns the complete wire bytes (header + payload + crc32).
std::vector<uint8_t> encode_frame(PacketType type,
                                  uint64_t player_handle,
                                  const uint8_t* payload,
                                  size_t payload_len,
                                  FrameFlags flags = FrameFlags::NONE);

// Convenience overload for string payloads.
inline std::vector<uint8_t> encode_frame_string(PacketType type,
                                                uint64_t player_handle,
                                                const std::string& payload,
                                                FrameFlags flags = FrameFlags::NONE) {
    return encode_frame(type, player_handle,
                        reinterpret_cast<const uint8_t*>(payload.data()),
                        payload.size(), flags);
}

// Convenience overload for empty payloads (e.g. heartbeat).
inline std::vector<uint8_t> encode_frame_empty(PacketType type,
                                               uint64_t player_handle,
                                               FrameFlags flags = FrameFlags::NONE) {
    return encode_frame(type, player_handle, nullptr, 0, flags);
}

// Attempt to decode one frame from the front of a receive buffer.
// On success (OK), consumes the frame bytes from `buffer` (erases them)
// and returns the decoded Frame.
// On NEED_MORE_DATA / INCOMPLETE, leaves `buffer` untouched so the caller
// can append more bytes and retry.
// On any other status, the buffer is in an unrecoverable state — the
// caller should drop the connection.
struct DecodeResult {
    DecodeStatus status = DecodeStatus::NEED_MORE_DATA;
    std::optional<Frame> frame;
};

DecodeResult decode_frame(std::vector<uint8_t>& buffer);

} // namespace server
} // namespace science_and_theology
