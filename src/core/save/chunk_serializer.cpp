#include "chunk_serializer.hpp"

#include <cstring>

namespace science_and_theology {

// --- Serialize ---

std::vector<uint8_t> ChunkSerializer::serialize(
    const std::string& layer_id, const ChunkData& chunk) {
    std::vector<uint8_t> buf;

    // Header.
    write_uint8(buf, kCurrentVersion);
    write_int32(buf, chunk.chunk_x);
    write_int32(buf, chunk.chunk_y);
    write_uint8(buf, static_cast<uint8_t>(chunk.state));
    write_string(buf, layer_id);

    // Terrain.
    int size_x = chunk.terrain.size_x;
    int size_y = chunk.terrain.size_y;
    int cell_count = size_x * size_y;
    write_uint32(buf, static_cast<uint32_t>(size_x));
    write_uint32(buf, static_cast<uint32_t>(size_y));
    write_uint32(buf, static_cast<uint32_t>(cell_count));

    // Materials (uint8 per cell).
    for (int i = 0; i < cell_count; ++i) {
        write_uint8(buf, static_cast<uint8_t>(chunk.terrain.cells[i].material));
    }

    // Flags (uint32 per cell).
    for (int i = 0; i < cell_count; ++i) {
        write_uint32(buf, chunk.terrain.cells[i].flags);
    }

    // Connectors.
    write_uint32(buf, static_cast<uint32_t>(chunk.connectors.size()));
    for (const auto& conn : chunk.connectors) {
        write_connector(buf, conn);
    }

    // Entity IDs.
    write_uint32(buf, static_cast<uint32_t>(chunk.entities.size()));
    for (const auto& eid : chunk.entities) {
        write_uint64(buf, eid.id);
    }

    // Machine IDs.
    write_uint32(buf, static_cast<uint32_t>(chunk.machines.size()));
    for (const auto& mid : chunk.machines) {
        write_uint64(buf, mid.id);
    }

    // Connector IDs.
    write_uint32(buf,
                 static_cast<uint32_t>(chunk.connector_ids.size()));
    for (const auto& cid : chunk.connector_ids) {
        write_uint64(buf, cid.id);
    }

    return buf;
}

// --- Deserialize ---

bool ChunkSerializer::deserialize(
    const std::vector<uint8_t>& data,
    std::string& layer_id, ChunkData& chunk) {
    size_t offset = 0;

    uint8_t version;
    if (!read_uint8(data, offset, version)) return false;
    if (version != kCurrentVersion) return false;

    int32_t cx, cy;
    if (!read_int32(data, offset, cx)) return false;
    if (!read_int32(data, offset, cy)) return false;
    chunk.chunk_x = cx;
    chunk.chunk_y = cy;

    uint8_t state_byte;
    if (!read_uint8(data, offset, state_byte)) return false;
    chunk.state = static_cast<ChunkState>(state_byte);

    if (!read_string(data, offset, layer_id)) return false;

    // Terrain.
    uint32_t sx, sy, cc;
    if (!read_uint32(data, offset, sx)) return false;
    if (!read_uint32(data, offset, sy)) return false;
    if (!read_uint32(data, offset, cc)) return false;

    int size_x = static_cast<int>(sx);
    int size_y = static_cast<int>(sy);
    int cell_count = static_cast<int>(cc);

    if (cell_count != size_x * size_y) return false;
    if (cell_count <= 0 || cell_count > ChunkData::kChunkSize * ChunkData::kChunkSize) {
        return false;
    }

    chunk.terrain.resize(size_x, size_y);

    // Materials.
    for (int i = 0; i < cell_count; ++i) {
        uint8_t mat;
        if (!read_uint8(data, offset, mat)) return false;
        chunk.terrain.cells[i].material = static_cast<TerrainMaterial>(mat);
    }

    // Flags.
    for (int i = 0; i < cell_count; ++i) {
        uint32_t flags;
        if (!read_uint32(data, offset, flags)) return false;
        chunk.terrain.cells[i].flags = flags;
    }

    // Connectors.
    uint32_t conn_count;
    if (!read_uint32(data, offset, conn_count)) return false;
    chunk.connectors.clear();
    chunk.connectors.reserve(conn_count);
    for (uint32_t i = 0; i < conn_count; ++i) {
        ConnectorPlacement conn;
        if (!read_connector(data, offset, conn)) return false;
        chunk.connectors.push_back(std::move(conn));
    }

    // Entity IDs.
    uint32_t entity_count;
    if (!read_uint32(data, offset, entity_count)) return false;
    chunk.entities.clear();
    chunk.entities.reserve(entity_count);
    for (uint32_t i = 0; i < entity_count; ++i) {
        uint64_t id;
        if (!read_uint64(data, offset, id)) return false;
        chunk.entities.push_back(EntityId{id});
    }

    // Machine IDs.
    uint32_t machine_count;
    if (!read_uint32(data, offset, machine_count)) return false;
    chunk.machines.clear();
    chunk.machines.reserve(machine_count);
    for (uint32_t i = 0; i < machine_count; ++i) {
        uint64_t id;
        if (!read_uint64(data, offset, id)) return false;
        chunk.machines.push_back(MachineId{id});
    }

    // Connector IDs.
    uint32_t conn_id_count;
    if (!read_uint32(data, offset, conn_id_count)) return false;
    chunk.connector_ids.clear();
    chunk.connector_ids.reserve(conn_id_count);
    for (uint32_t i = 0; i < conn_id_count; ++i) {
        uint64_t id;
        if (!read_uint64(data, offset, id)) return false;
        chunk.connector_ids.push_back(ConnectorId{id});
    }

    return true;
}

uint8_t ChunkSerializer::peek_version(const std::vector<uint8_t>& data) {
    if (data.empty()) return 0;
    return data[0];
}

// --- Write helpers ---

void ChunkSerializer::write_uint8(std::vector<uint8_t>& buf, uint8_t value) {
    buf.push_back(value);
}

void ChunkSerializer::write_int32(std::vector<uint8_t>& buf, int32_t value) {
    const auto* bytes = reinterpret_cast<const uint8_t*>(&value);
    buf.insert(buf.end(), bytes, bytes + sizeof(value));
}

void ChunkSerializer::write_uint32(std::vector<uint8_t>& buf, uint32_t value) {
    const auto* bytes = reinterpret_cast<const uint8_t*>(&value);
    buf.insert(buf.end(), bytes, bytes + sizeof(value));
}

void ChunkSerializer::write_uint64(std::vector<uint8_t>& buf, uint64_t value) {
    const auto* bytes = reinterpret_cast<const uint8_t*>(&value);
    buf.insert(buf.end(), bytes, bytes + sizeof(value));
}

void ChunkSerializer::write_string(std::vector<uint8_t>& buf,
                                   const std::string& str) {
    uint32_t len = static_cast<uint32_t>(str.size());
    write_uint32(buf, len);
    if (len > 0) {
        const auto* data = reinterpret_cast<const uint8_t*>(str.data());
        buf.insert(buf.end(), data, data + len);
    }
}

void ChunkSerializer::write_bytes(std::vector<uint8_t>& buf,
                                  const uint8_t* data, size_t len) {
    buf.insert(buf.end(), data, data + len);
}

// --- Read helpers ---

bool ChunkSerializer::read_uint8(const std::vector<uint8_t>& data,
                                 size_t& offset, uint8_t& out) {
    if (offset >= data.size()) return false;
    out = data[offset++];
    return true;
}

bool ChunkSerializer::read_int32(const std::vector<uint8_t>& data,
                                 size_t& offset, int32_t& out) {
    if (offset + sizeof(out) > data.size()) return false;
    std::memcpy(&out, &data[offset], sizeof(out));
    offset += sizeof(out);
    return true;
}

bool ChunkSerializer::read_uint32(const std::vector<uint8_t>& data,
                                  size_t& offset, uint32_t& out) {
    if (offset + sizeof(out) > data.size()) return false;
    std::memcpy(&out, &data[offset], sizeof(out));
    offset += sizeof(out);
    return true;
}

bool ChunkSerializer::read_uint64(const std::vector<uint8_t>& data,
                                  size_t& offset, uint64_t& out) {
    if (offset + sizeof(out) > data.size()) return false;
    std::memcpy(&out, &data[offset], sizeof(out));
    offset += sizeof(out);
    return true;
}

bool ChunkSerializer::read_string(const std::vector<uint8_t>& data,
                                  size_t& offset, std::string& out) {
    uint32_t len;
    if (!read_uint32(data, offset, len)) return false;
    if (offset + len > data.size()) return false;
    out.assign(reinterpret_cast<const char*>(&data[offset]), len);
    offset += len;
    return true;
}

bool ChunkSerializer::read_bytes(const std::vector<uint8_t>& data,
                                 size_t& offset, uint8_t* out, size_t len) {
    if (offset + len > data.size()) return false;
    std::memcpy(out, &data[offset], len);
    offset += len;
    return true;
}

// --- Connector helpers ---

void ChunkSerializer::write_connector(std::vector<uint8_t>& buf,
                                      const ConnectorPlacement& conn) {
    write_uint64(buf, static_cast<uint64_t>(conn.connector_id));
    write_string(buf, conn.from_layer);
    write_int32(buf, conn.from_cell_x);
    write_int32(buf, conn.from_cell_y);
    write_string(buf, conn.to_layer);
    write_int32(buf, conn.to_cell_x);
    write_int32(buf, conn.to_cell_y);
    write_uint8(buf, conn.one_way ? 1 : 0);
    write_uint8(buf, conn.locked ? 1 : 0);
    write_string(buf, conn.connector_type);
    write_uint8(buf, static_cast<uint8_t>(conn.activation_mode));
}

bool ChunkSerializer::read_connector(const std::vector<uint8_t>& data,
                                     size_t& offset,
                                     ConnectorPlacement& conn) {
    uint64_t raw_id;
    if (!read_uint64(data, offset, raw_id)) return false;
    conn.connector_id = static_cast<int64_t>(raw_id);
    if (!read_string(data, offset, conn.from_layer)) return false;
    if (!read_int32(data, offset, conn.from_cell_x)) return false;
    if (!read_int32(data, offset, conn.from_cell_y)) return false;
    if (!read_string(data, offset, conn.to_layer)) return false;
    if (!read_int32(data, offset, conn.to_cell_x)) return false;
    if (!read_int32(data, offset, conn.to_cell_y)) return false;

    uint8_t ow, lk, am;
    if (!read_uint8(data, offset, ow)) return false;
    if (!read_uint8(data, offset, lk)) return false;
    conn.one_way = (ow != 0);
    conn.locked = (lk != 0);

    if (!read_string(data, offset, conn.connector_type)) return false;
    if (!read_uint8(data, offset, am)) return false;
    conn.activation_mode = static_cast<int>(am);

    return true;
}

} // namespace science_and_theology