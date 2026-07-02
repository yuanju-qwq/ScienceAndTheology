#include "protocol_codec.hpp"

#include <algorithm>

namespace science_and_theology {
namespace server {

namespace {

// CRC32 lookup table (IEEE 802.3 polynomial 0xEDB88320).
uint32_t crc_table_[256];
bool crc_table_init_ = false;

void init_crc_table() {
    for (uint32_t i = 0; i < 256; ++i) {
        uint32_t c = i;
        for (int k = 0; k < 8; ++k) {
            c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
        }
        crc_table_[i] = c;
    }
    crc_table_init_ = true;
}

// Little-endian read/write helpers.
uint16_t read_u16_le(const uint8_t* p) {
    return static_cast<uint16_t>(p[0]) |
           (static_cast<uint16_t>(p[1]) << 8);
}

uint32_t read_u32_le(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) |
           (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}

uint64_t read_u64_le(const uint8_t* p) {
    return static_cast<uint64_t>(p[0]) |
           (static_cast<uint64_t>(p[1]) << 8) |
           (static_cast<uint64_t>(p[2]) << 16) |
           (static_cast<uint64_t>(p[3]) << 24) |
           (static_cast<uint64_t>(p[4]) << 32) |
           (static_cast<uint64_t>(p[5]) << 40) |
           (static_cast<uint64_t>(p[6]) << 48) |
           (static_cast<uint64_t>(p[7]) << 56);
}

void write_u16_le(uint8_t* p, uint16_t v) {
    p[0] = static_cast<uint8_t>(v & 0xFF);
    p[1] = static_cast<uint8_t>((v >> 8) & 0xFF);
}

void write_u32_le(uint8_t* p, uint32_t v) {
    p[0] = static_cast<uint8_t>(v & 0xFF);
    p[1] = static_cast<uint8_t>((v >> 8) & 0xFF);
    p[2] = static_cast<uint8_t>((v >> 16) & 0xFF);
    p[3] = static_cast<uint8_t>((v >> 24) & 0xFF);
}

void write_u64_le(uint8_t* p, uint64_t v) {
    for (int i = 0; i < 8; ++i) {
        p[i] = static_cast<uint8_t>((v >> (i * 8)) & 0xFF);
    }
}

} // namespace

uint32_t crc32_compute(const uint8_t* data, size_t length) {
    if (!crc_table_init_) init_crc_table();
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < length; ++i) {
        crc = crc_table_[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFFu;
}

std::vector<uint8_t> encode_frame(PacketType type,
                                  uint64_t player_handle,
                                  const uint8_t* payload,
                                  size_t payload_len,
                                  FrameFlags flags) {
    if (payload_len > kMaxPayloadSize) {
        // Caller should guard against this; return empty to signal error.
        return {};
    }

    const size_t total = kFrameHeaderSize + payload_len + kFrameCrcSize;
    std::vector<uint8_t> buf(total);

    // Header.
    uint8_t* p = buf.data();
    // magic "SNT1"
    p[0] = kProtocolMagicBytes[0];
    p[1] = kProtocolMagicBytes[1];
    p[2] = kProtocolMagicBytes[2];
    p[3] = kProtocolMagicBytes[3];
    p += 4;
    // version
    *p++ = kProtocolVersion;
    // type
    *p++ = static_cast<uint8_t>(type);
    // flags
    write_u16_le(p, static_cast<uint16_t>(flags));
    p += 2;
    // player_handle
    write_u64_le(p, player_handle);
    p += 8;
    // payload_len
    write_u32_le(p, static_cast<uint32_t>(payload_len));
    p += 4;

    // payload
    if (payload_len > 0 && payload != nullptr) {
        std::memcpy(p, payload, payload_len);
        p += payload_len;
    }

    // crc32 over header + payload.
    const size_t crc_region_len = kFrameHeaderSize + payload_len;
    const uint32_t crc = crc32_compute(buf.data(), crc_region_len);
    write_u32_le(p, crc);

    return buf;
}

DecodeResult decode_frame(std::vector<uint8_t>& buffer) {
    DecodeResult result;

    // Need at least a full header to inspect.
    if (buffer.size() < kFrameHeaderSize) {
        result.status = DecodeStatus::NEED_MORE_DATA;
        return result;
    }

    const uint8_t* p = buffer.data();

    // Validate magic.
    if (p[0] != kProtocolMagicBytes[0] ||
        p[1] != kProtocolMagicBytes[1] ||
        p[2] != kProtocolMagicBytes[2] ||
        p[3] != kProtocolMagicBytes[3]) {
        result.status = DecodeStatus::INVALID_MAGIC;
        return result;
    }

    const uint8_t version = p[4];
    if (version != kProtocolVersion) {
        result.status = DecodeStatus::INVALID_VERSION;
        return result;
    }

    const PacketType type = static_cast<PacketType>(p[5]);
    const FrameFlags flags = static_cast<FrameFlags>(read_u16_le(p + 6));
    const uint64_t player_handle = read_u64_le(p + 8);
    const uint32_t payload_len = read_u32_le(p + 16);

    if (payload_len > kMaxPayloadSize) {
        result.status = DecodeStatus::PAYLOAD_TOO_LARGE;
        return result;
    }

    const size_t frame_total = kFrameHeaderSize + payload_len + kFrameCrcSize;
    if (buffer.size() < frame_total) {
        result.status = DecodeStatus::INCOMPLETE;
        return result;
    }

    // Verify CRC32 over header + payload.
    const size_t crc_region_len = kFrameHeaderSize + payload_len;
    const uint32_t expected_crc = crc32_compute(buffer.data(), crc_region_len);
    const uint32_t actual_crc = read_u32_le(buffer.data() + kFrameHeaderSize + payload_len);
    if (expected_crc != actual_crc) {
        result.status = DecodeStatus::CRC_MISMATCH;
        return result;
    }

    // Success — build the frame and consume bytes.
    Frame frame;
    frame.version = version;
    frame.type = type;
    frame.flags = flags;
    frame.player_handle = player_handle;
    if (payload_len > 0) {
        frame.payload.assign(buffer.data() + kFrameHeaderSize,
                             buffer.data() + kFrameHeaderSize + payload_len);
    }

    // Erase consumed bytes from the front of the buffer.
    buffer.erase(buffer.begin(),
                 buffer.begin() + static_cast<std::ptrdiff_t>(frame_total));

    result.status = DecodeStatus::OK;
    result.frame = std::move(frame);
    return result;
}

} // namespace server
} // namespace science_and_theology
