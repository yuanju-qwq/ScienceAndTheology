#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "../world/chunk_data.hpp"

namespace science_and_theology {

// Binary serializer / deserializer for a single ChunkData.
// Format is forward-compatible via a version byte.
// All multi-byte integers are stored in host byte order (little-endian).
//
// Thread-safe: all methods are stateless and reentrant.
class ChunkSerializer {
public:
    // Current binary format version.
    static constexpr uint8_t kCurrentVersion = 1;

    // Serializes a chunk to raw bytes. Returns empty vector on failure.
    // The chunk's layer_id is embedded as part of the serialized data.
    static std::vector<uint8_t> serialize(
        const std::string& layer_id, const ChunkData& chunk);

    // Deserializes raw bytes back into a ChunkData and its layer_id.
    // Returns true on success. The caller must validate the version.
    static bool deserialize(
        const std::vector<uint8_t>& data,
        std::string& layer_id, ChunkData& chunk);

    // Returns the version byte from serialized data without full parsing.
    // Returns 0 if the data is too short or corrupted.
    static uint8_t peek_version(const std::vector<uint8_t>& data);

private:
    // --- Write helpers ---

    static void write_uint8(std::vector<uint8_t>& buf, uint8_t value);
    static void write_int32(std::vector<uint8_t>& buf, int32_t value);
    static void write_uint32(std::vector<uint8_t>& buf, uint32_t value);
    static void write_uint64(std::vector<uint8_t>& buf, uint64_t value);
    static void write_string(std::vector<uint8_t>& buf,
                             const std::string& str);
    static void write_bytes(std::vector<uint8_t>& buf,
                            const uint8_t* data, size_t len);

    // --- Read helpers ---

    static bool read_uint8(const std::vector<uint8_t>& data,
                           size_t& offset, uint8_t& out);
    static bool read_int32(const std::vector<uint8_t>& data,
                           size_t& offset, int32_t& out);
    static bool read_uint32(const std::vector<uint8_t>& data,
                            size_t& offset, uint32_t& out);
    static bool read_uint64(const std::vector<uint8_t>& data,
                            size_t& offset, uint64_t& out);
    static bool read_string(const std::vector<uint8_t>& data,
                            size_t& offset, std::string& out);
    static bool read_bytes(const std::vector<uint8_t>& data,
                           size_t& offset, uint8_t* out, size_t len);

    // --- Connector serialization ---

    static void write_connector(std::vector<uint8_t>& buf,
                                const ConnectorPlacement& conn);
    static bool read_connector(const std::vector<uint8_t>& data,
                               size_t& offset, ConnectorPlacement& conn);
};

} // namespace science_and_theology