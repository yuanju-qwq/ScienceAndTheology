#pragma once

// ============================================================
// chunk_streaming_system.hpp — Sector 感知的 chunk 流式加载
// ============================================================
//
// 设计依据：docs/unified_universe_world_design.md 第 10 节。
//
// ChunkStreamingSystem 负责根据玩家位置、速度和所在 Sector 计算兴趣区域
// （AOI），并产出需要加载 / 卸载的 SectorChunkKey 集合。
//
// U2 阶段：
//   - 不再依赖唯一 active_dimension，而是消费玩家附近的 Sector/Chunk 集合。
//   - 支持球形 AOI（低速移动）和前向锥形 AOI（高速飞船）。
//   - 支持逐帧 chunk 请求预算，避免一帧塞爆生成队列。
//   - 高速预测：根据速度计算前向预加载区域（见设计文档 10.3）。
//
// 本系统只计算"应该加载哪些 chunk"，不直接执行生成或 IO。
// 上层（GDWorldData / 渲染桥）根据返回的集合驱动实际加载。

#include <cstdint>
#include <vector>
#include <unordered_set>
#include <unordered_map>
#include <optional>
#include <mutex>

#include "universe_types.hpp"
#include "sector_manager.hpp"

namespace science_and_theology {

// 玩家会话的流式加载状态。
// 每个玩家维护一份，记录当前位置、速度和已加载 chunk 集合。
struct PlayerStreamingState {
    // 玩家全局位置（连续坐标）。
    GlobalPos pos;

    // 玩家速度（格/秒），用于高速预测。
    GlobalPos velocity;

    // 玩家当前所在 Sector。
    SectorId current_sector;

    // 已加载的 chunk 集合（用于计算卸载差集）。
    std::unordered_set<SectorChunkKey> loaded_chunks;

    // 是否启用高速预测（飞船巡航模式）。
    bool high_speed_prediction = false;

    // 高速预测的前向时长（秒）。
    double preload_seconds = 2.0;
};

// AOI 形状（见设计文档 10.2）。
enum class AoiShape : uint8_t {
    Sphere      = 0,  // 球形 AOI（低速移动、地表、空间站附近）
    ConeForward = 1,  // 前向锥形 AOI（高速飞船）
    Cylinder    = 2,  // 沿桥方向预加载（太空桥移动）
};

// 流式加载配置。
struct StreamingConfig {
    // 球形 AOI 半径（chunk 数）。
    int sphere_radius_chunks = 4;

    // 锥形 AOI 参数。
    int cone_forward_chunks = 12;   // 前向距离
    int cone_backward_chunks = 2;   // 后向距离
    double cone_half_angle = 0.6;   // 半锥角（弧度）

    // 圆柱形 AOI 参数（太空桥）。
    int cylinder_length_chunks = 16;
    int cylinder_radius_chunks = 2;

    // 每帧最大 chunk 请求数（预算，见设计文档 20.2）。
    int max_chunk_requests_per_frame = 4;

    // 卸载距离倍数：chunk 距离玩家超过 sphere_radius * unload_multiplier 时卸载。
    double unload_distance_multiplier = 1.5;

    // 默认 AOI 形状。
    AoiShape default_shape = AoiShape::Sphere;
};

// 流式加载更新结果。
// 包含本次更新计算出的需要加载和卸载的 chunk 集合。
struct StreamingUpdateResult {
    // 需要新加载的 chunk（已按预算截断）。
    std::vector<SectorChunkKey> chunks_to_load;

    // 需要卸载的 chunk（已超出卸载距离）。
    std::vector<SectorChunkKey> chunks_to_unload;

    // 因预算限制被推迟的 chunk 数量（诊断用）。
    int deferred_count = 0;

    // 本次计算涉及的 Sector 集合（用于模拟等级更新）。
    std::vector<SectorId> touched_sectors;
};

// ChunkStreamingSystem — Sector 感知的 chunk 流式加载系统。
//
// 线程安全：内部加锁。可被多个玩家会话并发更新。
// 但单个 PlayerStreamingState 的更新应在外部串行化（同一玩家不会并发更新）。
class ChunkStreamingSystem {
public:
    ChunkStreamingSystem() = default;
    ~ChunkStreamingSystem() = default;

    ChunkStreamingSystem(const ChunkStreamingSystem&) = delete;
    ChunkStreamingSystem& operator=(const ChunkStreamingSystem&) = delete;

    // --- 配置 ---

    void set_config(const StreamingConfig& config);
    const StreamingConfig& config() const;

    // --- 玩家状态 ---

    // 注册一个玩家会话的流式加载状态。
    // 若 PlayerId 已存在则更新。
    void register_player(uint64_t player_id, const PlayerStreamingState& state);

    // 注销玩家会话。
    void unregister_player(uint64_t player_id);

    // 更新玩家位置和速度。
    void update_player_position(uint64_t player_id,
                                const GlobalPos& pos,
                                const GlobalPos& velocity);

    // 设置玩家当前 Sector。
    void set_player_sector(uint64_t player_id, SectorId sector);

    // 设置高速预测模式。
    void set_high_speed_prediction(uint64_t player_id, bool enabled, double preload_seconds);

    // --- 核心更新 ---

    // 计算玩家应该加载/卸载的 chunk 集合。
    // 基于 SectorManager 查询玩家附近的 Sector，并根据 AOI 形状计算 chunk 范围。
    //
    // 流程（见设计文档 10.1、10.2）：
    //   1. 根据玩家位置和速度选择 AOI 形状。
    //   2. 在玩家附近的 Sector 中计算目标 chunk 集合。
    //   3. 与 loaded_chunks 求差集，得到 chunks_to_load。
    //   4. 对超出卸载距离的 loaded_chunks 标记为 chunks_to_unload。
    //   5. 按 max_chunk_requests_per_frame 截断 chunks_to_load。
    //   6. 更新 loaded_chunks。
    StreamingUpdateResult update_player(uint64_t player_id,
                                        const SectorManager& sector_manager);

    // --- 查询 ---

    // 返回玩家当前已加载的 chunk 数量。
    size_t get_loaded_chunk_count(uint64_t player_id) const;

    // 返回玩家当前所在 Sector。
    SectorId get_player_sector(uint64_t player_id) const;

    // --- 管理 ---

    void clear();

private:
    // 计算球形 AOI 内的 chunk 集合。
    // 以玩家所在 chunk 为中心，半径 sphere_radius_chunks 范围内的所有 chunk。
    // 只包含属于已注册 Sector 的 chunk。
    std::vector<SectorChunkKey> compute_sphere_aoi(
        const GlobalPos& pos,
        const SectorManager& sector_manager) const;

    // 计算前向锥形 AOI 内的 chunk 集合。
    // 沿速度方向前向 cone_forward_chunks，后向 cone_backward_chunks。
    // 锥角范围内包含的 chunk。
    std::vector<SectorChunkKey> compute_cone_aoi(
        const GlobalPos& pos,
        const GlobalPos& velocity,
        const SectorManager& sector_manager) const;

    // 计算圆柱形 AOI（太空桥）。
    std::vector<SectorChunkKey> compute_cylinder_aoi(
        const GlobalPos& pos,
        const GlobalPos& velocity,
        const SectorManager& sector_manager) const;

    // 选择 AOI 形状。
    AoiShape choose_aoi_shape(const PlayerStreamingState& state) const;

    // 计算两个 chunk 之间的距离（chunk 单位）。
    static int chunk_distance(const ChunkCoord& a, const ChunkCoord& b);

    mutable std::mutex mutex_;
    StreamingConfig config_;
    std::unordered_map<uint64_t, PlayerStreamingState> players_;
};

} // namespace science_and_theology
