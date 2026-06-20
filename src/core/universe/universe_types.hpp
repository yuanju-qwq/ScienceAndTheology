#pragma once

// ============================================================
// universe_types.hpp — U1 统一宇宙核心数据类型
// ============================================================
//
// 本头文件引入统一宇宙架构所需的基础数据类型：
//   - UniverseId / SectorId / StorageShardId
//   - GlobalBlockPos (int64 整数方块坐标)
//   - GlobalPos (double 连续实体坐标)
//   - SectorKind / SimulationLevel
//   - SectorDesc / AABB64
//   - ChunkCoord / SectorChunkKey
//
// 设计依据：docs/unified_universe_world_design.md 第 2~5 节。
//
// U1 阶段只引入数据语义，不改变现有 dimension_id 行为；
// 旧 API 通过 StorageShardId 适配层访问默认 Sector。
//
// 坐标规则（见设计文档 3.1）：
//   方块：int64 格子坐标。
//   实体：double 连续坐标。
//   客户端渲染：相对玩家的 float 局部坐标（由表现层处理）。

#include <cstdint>
#include <string>
#include <functional>
#include <optional>
#include <vector>

namespace science_and_theology {

// --- 标识符 ---

// 宇宙实例标识。0 表示无效。
// 当前单宇宙阶段固定为 1；保留字段以便未来多宇宙支持。
struct UniverseId {
    uint64_t value = 0;

    bool operator==(const UniverseId& other) const { return value == other.value; }
    bool operator!=(const UniverseId& other) const { return value != other.value; }
    bool is_valid() const { return value != 0; }
};

// Sector 标识。0 表示无效。
// Sector 是统一宇宙中的空间分区，不是独立 World（见设计文档 2.3、4.1）。
struct SectorId {
    uint64_t value = 0;

    SectorId() = default;
    explicit SectorId(uint64_t v) : value(v) {}

    bool operator==(const SectorId& other) const { return value == other.value; }
    bool operator!=(const SectorId& other) const { return value != other.value; }
    bool operator<(const SectorId& other) const { return value < other.value; }
    bool is_valid() const { return value != 0; }
    explicit operator bool() const { return is_valid(); }
};

// 迁移期存储分片标识。
// 现有 dimension_id 被包装为 StorageShardId，用于存档兼容（见设计文档 21.1）。
// 新代码不得直接用 dimension_id 表达空间关系。
struct StorageShardId {
    std::string value;  // 原 dimension_id 字符串

    bool operator==(const StorageShardId& other) const { return value == other.value; }
    bool operator!=(const StorageShardId& other) const { return value != other.value; }
    bool is_valid() const { return !value.empty(); }
};

// --- 坐标类型 ---

// 全局方块坐标（整数格子坐标）。
// 使用 int64 以支持星球间距 100,000~500,000 格的统一宇宙（见设计文档 3.1、8.3）。
struct GlobalBlockPos {
    int64_t x = 0;
    int64_t y = 0;
    int64_t z = 0;

    GlobalBlockPos() = default;
    GlobalBlockPos(int64_t x_, int64_t y_, int64_t z_) : x(x_), y(y_), z(z_) {}

    bool operator==(const GlobalBlockPos& other) const {
        return x == other.x && y == other.y && z == other.z;
    }
    bool operator!=(const GlobalBlockPos& other) const { return !(*this == other); }

    GlobalBlockPos operator+(const GlobalBlockPos& other) const {
        return GlobalBlockPos{x + other.x, y + other.y, z + other.z};
    }
    GlobalBlockPos operator-(const GlobalBlockPos& other) const {
        return GlobalBlockPos{x - other.x, y - other.y, z - other.z};
    }
};

// 全局实体坐标（double 连续坐标）。
// 服务端保存真实全局位置；客户端只使用局部渲染坐标（见设计文档 3.1、3.2）。
struct GlobalPos {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;

    GlobalPos() = default;
    GlobalPos(double x_, double y_, double z_) : x(x_), y(y_), z(z_) {}

    bool operator==(const GlobalPos& other) const {
        return x == other.x && y == other.y && z == other.z;
    }

    GlobalPos operator+(const GlobalPos& other) const {
        return GlobalPos{x + other.x, y + other.y, z + other.z};
    }
    GlobalPos operator-(const GlobalPos& other) const {
        return GlobalPos{x - other.x, y - other.y, z - other.z};
    }
};

// 从 GlobalBlockPos 构造 GlobalPos（格子中心）。
inline GlobalPos to_global_pos(const GlobalBlockPos& p) {
    return GlobalPos{
        static_cast<double>(p.x) + 0.5,
        static_cast<double>(p.y) + 0.5,
        static_cast<double>(p.z) + 0.5};
}

// 将 GlobalPos 取整为 GlobalBlockPos（floor 语义）。
inline GlobalBlockPos to_block_pos(const GlobalPos& p) {
    auto floor_i64 = [](double v) -> int64_t {
        int64_t i = static_cast<int64_t>(v);
        // 对负数非整数取整时需要 -1（C++ 截断向零）
        return (v < static_cast<double>(i)) ? i - 1 : i;
    };
    return GlobalBlockPos{floor_i64(p.x), floor_i64(p.y), floor_i64(p.z)};
}

// --- Sector 类型与模拟等级 ---

// Sector 类型（见设计文档 2.4）。
enum class SectorKind : uint8_t {
    PlanetSurface   = 0,
    PlanetOrbit     = 1,
    DeepSpace       = 2,
    SpaceBridge     = 3,
    AsteroidField   = 4,
    SpaceStation    = 5,
    COUNT           = 6,
};

constexpr const char* kSectorKindNames[] = {
    "PlanetSurface", "PlanetOrbit", "DeepSpace",
    "SpaceBridge", "AsteroidField", "SpaceStation",
};

inline const char* sector_kind_name(SectorKind k) {
    uint8_t i = static_cast<uint8_t>(k);
    if (i >= static_cast<uint8_t>(SectorKind::COUNT)) return "Unknown";
    return kSectorKindNames[i];
}

// 模拟等级（见设计文档 4.4）。
// 由 SectorManager 根据玩家位置和兴趣区域动态决定。
enum class SimulationLevel : uint8_t {
    Unloaded      = 0,  // 不加载
    Passive       = 1,  // 只保存，不 tick
    LowFrequency  = 2,  // 低频 tick
    Active        = 3,  // 正常 tick
    HighPriority  = 4,  // 玩家附近，高优先级
    COUNT         = 5,
};

constexpr const char* kSimulationLevelNames[] = {
    "Unloaded", "Passive", "LowFrequency", "Active", "HighPriority",
};

inline const char* simulation_level_name(SimulationLevel l) {
    uint8_t i = static_cast<uint8_t>(l);
    if (i >= static_cast<uint8_t>(SimulationLevel::COUNT)) return "Unknown";
    return kSimulationLevelNames[i];
}

// --- AABB64 ---

// int64 轴对齐包围盒，用于 Sector 边界（见设计文档 4.3）。
// bounds.min 为包含，bounds.max 为包含（闭区间）。
struct AABB64 {
    GlobalBlockPos min;
    GlobalBlockPos max;

    bool contains(const GlobalBlockPos& p) const {
        return p.x >= min.x && p.x <= max.x
            && p.y >= min.y && p.y <= max.y
            && p.z >= min.z && p.z <= max.z;
    }

    bool is_valid() const {
        return min.x <= max.x && min.y <= max.y && min.z <= max.z;
    }

    GlobalBlockPos size() const {
        return GlobalBlockPos{
            max.x - min.x + 1,
            max.y - min.y + 1,
            max.z - min.z + 1};
    }

    bool intersects(const AABB64& other) const {
        return !(max.x < other.min.x || min.x > other.max.x
              || max.y < other.min.y || min.y > other.max.y
              || max.z < other.min.z || min.z > other.max.z);
    }
};

// --- Sector 描述 ---

// Sector 描述（见设计文档 4.3）。
// 描述一个 Sector 的空间范围、能力开关和默认模拟等级。
struct SectorDesc {
    SectorId id;
    std::string name;
    SectorKind kind = SectorKind::DeepSpace;

    AABB64 bounds;

    // 能力开关：决定该 Sector 内允许的玩法。
    bool allow_voxel_building = false;
    bool allow_machines = false;
    bool allow_power_network = false;
    bool allow_logistics_network = false;

    // 默认模拟等级；运行时由 SectorManager 根据玩家位置动态调整。
    SimulationLevel default_simulation = SimulationLevel::Passive;

    // 迁移期：该 Sector 对应的旧 dimension_id（存档兼容用）。
    // 为空表示该 Sector 没有旧存档映射（纯新 Sector）。
    StorageShardId legacy_storage_shard;

    bool is_valid() const {
        return id.is_valid() && bounds.is_valid();
    }
};

// --- Chunk 坐标 ---

// Chunk 大小（见设计文档 5.1）。
// 注意：设计文档示例为 16，但现有 ChunkData::kChunkSize = 32。
// U1 阶段保持与现有实现一致，避免破坏存档。
// 统一宇宙的 SectorManager 通过此常量进行坐标转换。
inline constexpr int64_t kUniverseChunkSize = 32;

// Chunk 坐标（见设计文档 5.2）。
// 使用 int64 以支持大坐标统一宇宙。
struct ChunkCoord {
    int64_t cx = 0;
    int64_t cy = 0;
    int64_t cz = 0;

    ChunkCoord() = default;
    ChunkCoord(int64_t x, int64_t y, int64_t z) : cx(x), cy(y), cz(z) {}

    bool operator==(const ChunkCoord& other) const {
        return cx == other.cx && cy == other.cy && cz == other.cz;
    }
    bool operator!=(const ChunkCoord& other) const { return !(*this == other); }
};

// Sector 内 ChunkKey（见设计文档 5.3）。
// 虽然全局坐标连续，但存储带 SectorId，便于按 Sector 存档、tick 和网络同步。
struct SectorChunkKey {
    SectorId sector;
    ChunkCoord coord;

    bool operator==(const SectorChunkKey& other) const {
        return sector == other.sector && coord == other.coord;
    }
    bool operator!=(const SectorChunkKey& other) const { return !(*this == other); }
};

// --- 方向（用于 BlockSpace 邻居查询，见设计文档 8.1） ---

enum class Direction : uint8_t {
    PosX = 0,
    NegX = 1,
    PosY = 2,
    NegY = 3,
    PosZ = 4,
    NegZ = 5,
    COUNT = 6,
};

inline GlobalBlockPos direction_offset(Direction d) {
    switch (d) {
        case Direction::PosX: return GlobalBlockPos{ 1,  0,  0};
        case Direction::NegX: return GlobalBlockPos{-1,  0,  0};
        case Direction::PosY: return GlobalBlockPos{ 0,  1,  0};
        case Direction::NegY: return GlobalBlockPos{ 0, -1,  0};
        case Direction::PosZ: return GlobalBlockPos{ 0,  0,  1};
        case Direction::NegZ: return GlobalBlockPos{ 0,  0, -1};
        default: return GlobalBlockPos{0, 0, 0};
    }
}

// --- 坐标转换工具 ---

// 将全局方块坐标转换为 chunk 坐标（floor division，正确处理负数）。
// 见设计文档 5.4：GlobalBlockPos → ChunkCoord。
inline ChunkCoord block_pos_to_chunk_coord(const GlobalBlockPos& p) {
    auto floor_div = [](int64_t a, int64_t b) -> int64_t {
        // C++ 整除向零截断；负数需要修正为 floor。
        int64_t q = a / b;
        int64_t r = a % b;
        if (r != 0 && ((r < 0) != (b < 0))) {
            q -= 1;
        }
        return q;
    };
    return ChunkCoord{
        floor_div(p.x, kUniverseChunkSize),
        floor_div(p.y, kUniverseChunkSize),
        floor_div(p.z, kUniverseChunkSize)};
}

// 将 chunk 坐标转换为该 chunk 的最小方块坐标（原点角）。
inline GlobalBlockPos chunk_coord_to_block_pos(const ChunkCoord& c) {
    return GlobalBlockPos{
        c.cx * kUniverseChunkSize,
        c.cy * kUniverseChunkSize,
        c.cz * kUniverseChunkSize};
}

// 返回方块在 chunk 内的局部索引 [0, kUniverseChunkSize^3)。
inline uint32_t block_pos_to_chunk_local_index(const GlobalBlockPos& p) {
    // 对负数取模得到非负余数。
    auto mod_nonneg = [](int64_t a, int64_t b) -> int64_t {
        int64_t r = a % b;
        if (r < 0) r += b;
        return r;
    };
    const int64_t s = kUniverseChunkSize;
    int64_t lx = mod_nonneg(p.x, s);
    int64_t ly = mod_nonneg(p.y, s);
    int64_t lz = mod_nonneg(p.z, s);
    return static_cast<uint32_t>(lx + ly * s + lz * s * s);
}

// 从 chunk 坐标和局部索引还原全局方块坐标。
inline GlobalBlockPos from_chunk_local(const ChunkCoord& c, uint32_t local_index) {
    const int64_t s = kUniverseChunkSize;
    int64_t lz = static_cast<int64_t>(local_index) / (s * s);
    int64_t rem = static_cast<int64_t>(local_index) % (s * s);
    int64_t ly = rem / s;
    int64_t lx = rem % s;
    return GlobalBlockPos{
        c.cx * s + lx,
        c.cy * s + ly,
        c.cz * s + lz};
}

} // namespace science_and_theology

// --- std::hash 特化 ---

template <>
struct std::hash<science_and_theology::SectorId> {
    size_t operator()(const science_and_theology::SectorId& s) const {
        return std::hash<uint64_t>()(s.value);
    }
};

template <>
struct std::hash<science_and_theology::SectorChunkKey> {
    size_t operator()(const science_and_theology::SectorChunkKey& k) const {
        size_t h = std::hash<science_and_theology::SectorId>()(k.sector);
        h ^= std::hash<int64_t>()(k.coord.cx) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<int64_t>()(k.coord.cy) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<int64_t>()(k.coord.cz) + 0x9e3779b9 + (h << 6) + (h >> 2);
        return h;
    }
};

template <>
struct std::hash<science_and_theology::GlobalBlockPos> {
    size_t operator()(const science_and_theology::GlobalBlockPos& p) const {
        size_t h = std::hash<int64_t>()(p.x);
        h ^= std::hash<int64_t>()(p.y) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<int64_t>()(p.z) + 0x9e3779b9 + (h << 6) + (h >> 2);
        return h;
    }
};
