#pragma once

// ============================================================
// save_reliability_manager.hpp — U8 存档可靠性管理
// ============================================================
//
// 设计依据：docs/unified_universe_world_design.md 第 21.2 (U8) 节。
//
// U8 工作项：存档采用临时文件、校验和、原子替换与版本迁移；
// Sector 独立损坏时允许隔离并给出可操作错误。
//
// 验收条件：存档中断可恢复，旧版本处理结果明确且可追踪。
//
// SaveReliabilityManager 的职责：
//   1. 原子写入：先写临时文件，校验通过后原子替换目标文件。
//   2. 校验和：每个存档文件附带 CRC32 校验和，读取时验证。
//   3. 版本迁移：检测旧版本存档，执行迁移或明确拒绝。
//   4. Sector 独立损坏隔离：单个 Sector 存档损坏不影响其他 Sector，
//      返回明确的错误信息（可操作）。
//   5. 中断恢复：写入过程中断时，临时文件不会污染目标文件。
//
// 文件格式（带校验和的封装）：
//   [uint32 magic = 0x534E5452 ("SNTR")]
//   [uint16 format_version]
//   [uint32 payload_size]
//   [uint32 crc32]
//   [uint8 payload[payload_size]]
//
// 原子写入流程：
//   1. 写入 target.tmp
//   2. fsync
//   3. rename(target.tmp, target)
//   4. 失败时删除 target.tmp
//
// 版本迁移策略：
//   - 当前版本：kCurrentSaveVersion = 3
//   - v1 → v3：旧 dimension 存档，标记为 legacy，调用方决定迁移或拒绝
//   - v2 → v3：添加 CRC32 校验和封装
//   - v3：当前版本（带校验和 + 原子写入）

#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <optional>

namespace science_and_theology {

// 存档格式版本。
inline constexpr uint16_t kCurrentSaveVersion = 3;
inline constexpr uint16_t kLegacySaveVersionV1 = 1;
inline constexpr uint16_t kLegacySaveVersionV2 = 2;

// 魔数 "SNTR"（Science And Theology Reliability）。
inline constexpr uint32_t kSaveMagic = 0x534E5452u;

// 存档操作结果。
enum class SaveResult : uint8_t {
    Ok = 0,                // 成功
    WriteFailed = 1,       // 写入失败（IO 错误）
    ChecksumMismatch = 2,  // 校验和不匹配（文件损坏）
    VersionTooOld = 3,     // 版本过旧，需要迁移
    VersionTooNew = 4,     // 版本过新，当前程序不支持
    InvalidFormat = 5,     // 格式无效（魔数不匹配等）
    SectorCorrupted = 6,   // Sector 存档损坏（已隔离）
};

inline const char* save_result_name(SaveResult r) {
    switch (r) {
        case SaveResult::Ok: return "Ok";
        case SaveResult::WriteFailed: return "WriteFailed";
        case SaveResult::ChecksumMismatch: return "ChecksumMismatch";
        case SaveResult::VersionTooOld: return "VersionTooOld";
        case SaveResult::VersionTooNew: return "VersionTooNew";
        case SaveResult::InvalidFormat: return "InvalidFormat";
        case SaveResult::SectorCorrupted: return "SectorCorrupted";
        default: return "Unknown";
    }
}

// Sector 存档状态（用于隔离损坏 Sector）。
enum class SectorSaveStatus : uint8_t {
    Healthy = 0,      // 健康
    Corrupted = 1,    // 损坏（已隔离）
    Missing = 2,      // 缺失（未保存或被删除）
    LegacyVersion = 3,// 旧版本（需要迁移）
};

// 读取结果。
struct SaveReadResult {
    SaveResult result = SaveResult::Ok;
    uint16_t version = 0;           // 文件中的版本
    std::vector<uint8_t> payload;   // 有效载荷（result == Ok 时有效）
    std::string error_detail;       // 错误详情（可操作信息）

    bool ok() const { return result == SaveResult::Ok; }
};

// Sector 健康状态记录。
struct SectorHealthRecord {
    SectorSaveStatus status = SectorSaveStatus::Healthy;
    std::string last_error;         // 最后一次错误信息
    int64_t last_check_tick = 0;    // 最后检查 tick
};

// SaveReliabilityManager — 存档可靠性管理器。
//
// 线程安全：内部加锁。文件操作使用标准 C++ 文件流。
class SaveReliabilityManager {
public:
    SaveReliabilityManager() = default;
    ~SaveReliabilityManager() = default;

    SaveReliabilityManager(const SaveReliabilityManager&) = delete;
    SaveReliabilityManager& operator=(const SaveReliabilityManager&) = delete;

    // --- 原子写入 ---

    // 原子写入文件（临时文件 + 校验和 + 原子替换）。
    // path: 目标文件路径
    // payload: 有效载荷
    // 返回 SaveResult::Ok 表示成功。
    SaveResult save_atomic(const std::string& path,
                            const std::vector<uint8_t>& payload);

    // 读取文件（验证校验和 + 版本检查）。
    SaveReadResult load(const std::string& path) const;

    // --- 版本迁移 ---

    // 检测文件版本（不读取 payload）。
    // 返回文件中的版本号；文件不存在或格式无效返回 0。
    uint16_t detect_version(const std::string& path) const;

    // 迁移旧版本 payload 到当前版本。
    // from_version: 旧版本号
    // old_payload: 旧版本 payload
    // out_new_payload: 输出新版本 payload
    // 返回 true 表示迁移成功；false 表示不支持迁移（需调用方决定拒绝）。
    bool migrate_payload(uint16_t from_version,
                          const std::vector<uint8_t>& old_payload,
                          std::vector<uint8_t>& out_new_payload) const;

    // --- Sector 隔离 ---

    // 记录 Sector 存档状态（用于隔离损坏 Sector）。
    void set_sector_status(uint64_t sector_id,
                            SectorSaveStatus status,
                            const std::string& error_detail);

    // 查询 Sector 存档状态。
    SectorHealthRecord get_sector_status(uint64_t sector_id) const;

    // 查询所有损坏的 Sector（用于诊断和恢复）。
    std::vector<uint64_t> corrupted_sectors() const;

    // 查询所有旧版本的 Sector（需要迁移）。
    std::vector<uint64_t> legacy_sectors() const;

    // 清除 Sector 状态记录（用于恢复后重置）。
    void clear_sector_status(uint64_t sector_id);

    // --- 校验和 ---

    // 计算 payload 的 CRC32 校验和。
    static uint32_t compute_crc32(const std::vector<uint8_t>& data);

    // --- 诊断 ---

    // 返回所有 Sector 健康记录（用于低频日志）。
    std::unordered_map<uint64_t, SectorHealthRecord> all_sector_records() const;

    // 生成低频日志摘要。
    std::string format_summary() const;

    // --- 管理 ---

    void clear();

private:
    mutable std::mutex mutex_;

    // Sector 健康状态表。
    std::unordered_map<uint64_t, SectorHealthRecord> sector_health_;

    // 内部：写入封装格式（magic + version + size + crc32 + payload）。
    static std::vector<uint8_t> encode(uint16_t version,
                                        const std::vector<uint8_t>& payload);

    // 内部：解析封装格式。
    static SaveReadResult decode(const std::vector<uint8_t>& raw);
};

} // namespace science_and_theology
