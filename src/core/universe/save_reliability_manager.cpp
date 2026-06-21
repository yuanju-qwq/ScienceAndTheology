#include "save_reliability_manager.hpp"

#include <fstream>
#include <sstream>
#include <iomanip>
#include <cstring>

namespace science_and_theology {

// ============================================================
// CRC32 实现（与协议层一致，避免引入额外依赖）
// ============================================================

namespace {

// CRC32 查找表（多项式 0xEDB88320，标准 zlib CRC32）。
uint32_t crc32_table[256];
bool crc32_table_initialized = false;

void init_crc32_table() {
    if (crc32_table_initialized) return;
    for (uint32_t i = 0; i < 256; ++i) {
        uint32_t c = i;
        for (int k = 0; k < 8; ++k) {
            if (c & 1) {
                c = 0xEDB88320u ^ (c >> 1);
            } else {
                c = c >> 1;
            }
        }
        crc32_table[i] = c;
    }
    crc32_table_initialized = true;
}

} // namespace

uint32_t SaveReliabilityManager::compute_crc32(const std::vector<uint8_t>& data) {
    init_crc32_table();
    uint32_t crc = 0xFFFFFFFFu;
    for (uint8_t byte : data) {
        crc = crc32_table[(crc ^ byte) & 0xFF] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFFu;
}

// ============================================================
// 编码/解码（封装格式）
// ============================================================

std::vector<uint8_t> SaveReliabilityManager::encode(
    uint16_t version,
    const std::vector<uint8_t>& payload) {

    uint32_t crc = compute_crc32(payload);
    uint32_t payload_size = static_cast<uint32_t>(payload.size());

    std::vector<uint8_t> raw;
    raw.reserve(4 + 2 + 4 + 4 + payload.size());

    // magic (4 bytes, little-endian)
    raw.push_back(static_cast<uint8_t>(kSaveMagic & 0xFF));
    raw.push_back(static_cast<uint8_t>((kSaveMagic >> 8) & 0xFF));
    raw.push_back(static_cast<uint8_t>((kSaveMagic >> 16) & 0xFF));
    raw.push_back(static_cast<uint8_t>((kSaveMagic >> 24) & 0xFF));

    // version (2 bytes, little-endian)
    raw.push_back(static_cast<uint8_t>(version & 0xFF));
    raw.push_back(static_cast<uint8_t>((version >> 8) & 0xFF));

    // payload_size (4 bytes, little-endian)
    raw.push_back(static_cast<uint8_t>(payload_size & 0xFF));
    raw.push_back(static_cast<uint8_t>((payload_size >> 8) & 0xFF));
    raw.push_back(static_cast<uint8_t>((payload_size >> 16) & 0xFF));
    raw.push_back(static_cast<uint8_t>((payload_size >> 24) & 0xFF));

    // crc32 (4 bytes, little-endian)
    raw.push_back(static_cast<uint8_t>(crc & 0xFF));
    raw.push_back(static_cast<uint8_t>((crc >> 8) & 0xFF));
    raw.push_back(static_cast<uint8_t>((crc >> 16) & 0xFF));
    raw.push_back(static_cast<uint8_t>((crc >> 24) & 0xFF));

    // payload
    raw.insert(raw.end(), payload.begin(), payload.end());

    return raw;
}

SaveReadResult SaveReliabilityManager::decode(const std::vector<uint8_t>& raw) {
    SaveReadResult result;

    // 最小长度：magic(4) + version(2) + size(4) + crc(4) = 14
    if (raw.size() < 14) {
        result.result = SaveResult::InvalidFormat;
        result.error_detail = "file too small (min 14 bytes, got "
                             + std::to_string(raw.size()) + ")";
        return result;
    }

    size_t offset = 0;

    // magic
    uint32_t magic = static_cast<uint32_t>(raw[offset])
                   | (static_cast<uint32_t>(raw[offset + 1]) << 8)
                   | (static_cast<uint32_t>(raw[offset + 2]) << 16)
                   | (static_cast<uint32_t>(raw[offset + 3]) << 24);
    offset += 4;

    if (magic != kSaveMagic) {
        result.result = SaveResult::InvalidFormat;
        result.error_detail = "magic mismatch (expected 0x534E5452, got 0x"
                            + std::to_string(magic) + ")";
        return result;
    }

    // version
    uint16_t version = static_cast<uint16_t>(raw[offset])
                     | (static_cast<uint16_t>(raw[offset + 1]) << 8);
    offset += 2;
    result.version = version;

    // payload_size
    uint32_t payload_size = static_cast<uint32_t>(raw[offset])
                          | (static_cast<uint32_t>(raw[offset + 1]) << 8)
                          | (static_cast<uint32_t>(raw[offset + 2]) << 16)
                          | (static_cast<uint32_t>(raw[offset + 3]) << 24);
    offset += 4;

    // crc32
    uint32_t expected_crc = static_cast<uint32_t>(raw[offset])
                          | (static_cast<uint32_t>(raw[offset + 1]) << 8)
                          | (static_cast<uint32_t>(raw[offset + 2]) << 16)
                          | (static_cast<uint32_t>(raw[offset + 3]) << 24);
    offset += 4;

    // 检查 payload 长度
    if (raw.size() < offset + payload_size) {
        result.result = SaveResult::InvalidFormat;
        result.error_detail = "payload truncated (expected "
                            + std::to_string(payload_size) + " bytes, got "
                            + std::to_string(raw.size() - offset) + ")";
        return result;
    }

    // 提取 payload
    std::vector<uint8_t> payload(raw.begin() + offset,
                                  raw.begin() + offset + payload_size);

    // 版本检查
    if (version > kCurrentSaveVersion) {
        result.result = SaveResult::VersionTooNew;
        result.error_detail = "version " + std::to_string(version)
                            + " > current " + std::to_string(kCurrentSaveVersion);
        return result;
    }

    if (version < kCurrentSaveVersion) {
        result.result = SaveResult::VersionTooOld;
        result.error_detail = "version " + std::to_string(version)
                            + " < current " + std::to_string(kCurrentSaveVersion)
                            + ", migration required";
        result.payload = std::move(payload);
        return result;
    }

    // 校验和验证（仅当前版本）
    uint32_t actual_crc = compute_crc32(payload);
    if (actual_crc != expected_crc) {
        result.result = SaveResult::ChecksumMismatch;
        result.error_detail = "crc32 mismatch (expected 0x"
                            + std::to_string(expected_crc) + ", got 0x"
                            + std::to_string(actual_crc) + ")";
        return result;
    }

    result.result = SaveResult::Ok;
    result.payload = std::move(payload);
    return result;
}

// ============================================================
// 原子写入
// ============================================================

SaveResult SaveReliabilityManager::save_atomic(
    const std::string& path,
    const std::vector<uint8_t>& payload) {

    // 编码
    std::vector<uint8_t> raw = encode(kCurrentSaveVersion, payload);

    // 写入临时文件
    std::string tmp_path = path + ".tmp";

    {
        std::ofstream ofs(tmp_path, std::ios::binary | std::ios::trunc);
        if (!ofs.is_open()) {
            return SaveResult::WriteFailed;
        }
        ofs.write(reinterpret_cast<const char*>(raw.data()),
                  static_cast<std::streamsize>(raw.size()));
        if (!ofs.good()) {
            // 写入失败，清理临时文件
            ofs.close();
            std::remove(tmp_path.c_str());
            return SaveResult::WriteFailed;
        }
        ofs.flush();
    }

    // 原子替换（rename）
    // Windows 上 rename 会失败如果目标文件存在，需要先删除
    std::remove(path.c_str());
    if (std::rename(tmp_path.c_str(), path.c_str()) != 0) {
        std::remove(tmp_path.c_str());
        return SaveResult::WriteFailed;
    }

    return SaveResult::Ok;
}

SaveReadResult SaveReliabilityManager::load(const std::string& path) const {
    std::ifstream ifs(path, std::ios::binary);
    if (!ifs.is_open()) {
        SaveReadResult result;
        result.result = SaveResult::WriteFailed;
        result.error_detail = "cannot open file: " + path;
        return result;
    }

    std::vector<uint8_t> raw((std::istreambuf_iterator<char>(ifs)),
                              std::istreambuf_iterator<char>());

    return decode(raw);
}

// ============================================================
// 版本迁移
// ============================================================

uint16_t SaveReliabilityManager::detect_version(const std::string& path) const {
    std::ifstream ifs(path, std::ios::binary);
    if (!ifs.is_open()) return 0;

    // 读取前 6 字节（magic + version）
    uint8_t header[6];
    ifs.read(reinterpret_cast<char*>(header), 6);
    if (ifs.gcount() < 6) return 0;

    // 检查 magic
    uint32_t magic = static_cast<uint32_t>(header[0])
                   | (static_cast<uint32_t>(header[1]) << 8)
                   | (static_cast<uint32_t>(header[2]) << 16)
                   | (static_cast<uint32_t>(header[3]) << 24);
    if (magic != kSaveMagic) return 0;

    uint16_t version = static_cast<uint16_t>(header[4])
                     | (static_cast<uint16_t>(header[5]) << 8);
    return version;
}

bool SaveReliabilityManager::migrate_payload(
    uint16_t from_version,
    const std::vector<uint8_t>& old_payload,
    std::vector<uint8_t>& out_new_payload) const {

    // v2 → v3：payload 内容不变，只是添加了校验和封装
    // 旧版本（v2）的 payload 直接作为新版本 payload
    if (from_version == kLegacySaveVersionV2) {
        out_new_payload = old_payload;
        return true;
    }

    // v1 → v3：旧 dimension 存档，需要调用方提供迁移逻辑
    // 这里只标记为需要迁移，实际迁移由上层（SaveManager）处理
    // 返回 false 表示不支持自动迁移
    if (from_version == kLegacySaveVersionV1) {
        return false;
    }

    return false;
}

// ============================================================
// Sector 隔离
// ============================================================

void SaveReliabilityManager::set_sector_status(
    uint64_t sector_id,
    SectorSaveStatus status,
    const std::string& error_detail) {

    std::lock_guard<std::mutex> lock(mutex_);

    SectorHealthRecord& record = sector_health_[sector_id];
    record.status = status;
    record.last_error = error_detail;
}

SectorHealthRecord SaveReliabilityManager::get_sector_status(uint64_t sector_id) const {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = sector_health_.find(sector_id);
    if (it == sector_health_.end()) {
        return SectorHealthRecord{};
    }
    return it->second;
}

std::vector<uint64_t> SaveReliabilityManager::corrupted_sectors() const {
    std::lock_guard<std::mutex> lock(mutex_);

    std::vector<uint64_t> result;
    for (const auto& [id, record] : sector_health_) {
        if (record.status == SectorSaveStatus::Corrupted) {
            result.push_back(id);
        }
    }
    return result;
}

std::vector<uint64_t> SaveReliabilityManager::legacy_sectors() const {
    std::lock_guard<std::mutex> lock(mutex_);

    std::vector<uint64_t> result;
    for (const auto& [id, record] : sector_health_) {
        if (record.status == SectorSaveStatus::LegacyVersion) {
            result.push_back(id);
        }
    }
    return result;
}

void SaveReliabilityManager::clear_sector_status(uint64_t sector_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    sector_health_.erase(sector_id);
}

// ============================================================
// 诊断
// ============================================================

std::unordered_map<uint64_t, SectorHealthRecord>
SaveReliabilityManager::all_sector_records() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return sector_health_;
}

std::string SaveReliabilityManager::format_summary() const {
    std::lock_guard<std::mutex> lock(mutex_);

    std::ostringstream oss;
    oss << "[SaveReliability] sectors=" << sector_health_.size();

    int healthy = 0, corrupted = 0, missing = 0, legacy = 0;
    for (const auto& [_, record] : sector_health_) {
        switch (record.status) {
            case SectorSaveStatus::Healthy: ++healthy; break;
            case SectorSaveStatus::Corrupted: ++corrupted; break;
            case SectorSaveStatus::Missing: ++missing; break;
            case SectorSaveStatus::LegacyVersion: ++legacy; break;
        }
    }

    oss << " healthy=" << healthy
        << " corrupted=" << corrupted
        << " missing=" << missing
        << " legacy=" << legacy;

    return oss.str();
}

// ============================================================
// 管理
// ============================================================

void SaveReliabilityManager::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    sector_health_.clear();
}

} // namespace science_and_theology
