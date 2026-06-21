# 统一宇宙 World 架构设计

## 1. 设计目标

实现一个**连续宇宙体素世界**：

- 玩家可以从一个星球起飞，进入太空，再飞到另一个星球。
- 玩家可以从一个星球一路搭方块，最终搭到另一个星球。
- 星球、太空、空间站、小行星、太空桥都处于同一个连续宇宙坐标体系中。
- 服务器内部通过 Sector、Chunk、Simulation Island 等机制进行性能分区。
- 网络同步只同步玩家兴趣范围内的数据，远处天体使用 LOD / Proxy 表示。
- 工厂系统、电网、物流、AE 类网络不能无限全局连通，需要边界和中继机制。

核心模型：

```text
对玩家：一个连续宇宙。
对引擎：统一坐标 + Sector 分区 + Chunk 流式加载 + 分段模拟。
```

---

## 2. 总体架构

```text
UniverseWorld
  ├── Sector：星球地表区域
  ├── Sector：星球轨道区域
  ├── Sector：深空区域
  ├── Sector：太空桥区域
  ├── Sector：小行星区域
  ├── Sector：空间站区域
  └── Sector：另一个星球区域
```

### 2.1 服务端总结构

```cpp
class UniverseServer {
public:
    UniverseWorld universe;
    PlayerSessionManager players;
    NetworkServer network;
    SaveSystem save_system;
    void tick();
};
```

### 2.2 宇宙世界

```cpp
class UniverseWorld {
public:
    UniverseId id;
    SectorManager sectors;
    ChunkStreamingSystem chunk_streaming;
    EntitySystem entities;
    GravitySystem gravity;
    CelestialSystem celestial;
    BlockSpace block_space;
    void tick(UniverseTickContext& ctx);
};
```

### 2.3 Sector

`Sector` 是统一宇宙中的空间分区，不是独立 World。

```cpp
class Sector {
public:
    SectorId id;
    SectorKind kind;
    AABB64 bounds;
    ChunkManager chunks;
    EntityPartition entities;
    MachineSystem machines;
    PowerNetworkSystem power;
    LogisticsSystem logistics;
    FluidSystem fluids;
    void tick_sector(SectorTickContext& ctx);
};
```

### 2.4 Sector 类型

```cpp
enum class SectorKind {
    PlanetSurface,
    PlanetOrbit,
    DeepSpace,
    SpaceBridge,
    AsteroidField,
    SpaceStation,
};
```

---

## 3. 统一坐标系统

### 3.1 服务端全局坐标

方块使用整数全局坐标：

```cpp
struct GlobalBlockPos {
    int64_t x;
    int64_t y;
    int64_t z;
};
```

实体使用连续坐标：

```cpp
struct GlobalPos {
    double x;
    double y;
    double z;
};
```

坐标规则：

```text
方块：int64 格子坐标。
实体：double 连续坐标。
客户端渲染：相对玩家的 float 局部坐标。
```

### 3.2 客户端 Floating Origin

客户端不要直接用超大全局坐标渲染，否则会出现浮点精度抖动。使用相机相对坐标：

```cpp
RenderPos render_pos = entity.global_pos - client.camera_origin;
```

服务端保存真实全局位置，客户端只使用局部渲染坐标。当玩家渲染坐标超过阈值时，整体平移渲染原点（重基准），同时保持逻辑坐标、速度、相机和射线结果连续。

---

## 4. Sector 分区设计

### 4.1 Sector 的作用

- 避免一个超级 ChunkManager 管理整个宇宙。
- 避免所有机器、电网、物流系统全局 tick。
- 控制每个区域的加载、模拟、存档和网络同步。
- 让连续宇宙仍然具备清晰的工程边界。

### 4.2 Sector 示例

```text
Universe
  Sector 100：planet_alpha_surface
  Sector 101：planet_alpha_orbit
  Sector 200：deep_space_route_alpha_beta
  Sector 300：planet_beta_orbit
  Sector 301：planet_beta_surface
```

玩家从星球 A 飞到星球 B：

```text
planet_alpha_surface → planet_alpha_orbit → deep_space_route_alpha_beta → planet_beta_orbit → planet_beta_surface
```

对玩家来说是连续移动；对引擎来说是实体跨 Sector。

### 4.3 Sector 描述

```cpp
struct SectorDesc {
    SectorId id;
    std::string name;
    SectorKind kind;
    AABB64 bounds;
    bool allow_voxel_building;
    bool allow_machines;
    bool allow_power_network;
    bool allow_logistics_network;
    SimulationLevel default_simulation;
};
```

### 4.4 模拟等级

```cpp
enum class SimulationLevel {
    Unloaded,       // 不加载
    Passive,        // 只保存，不 tick
    LowFrequency,   // 低频 tick
    Active,         // 正常 tick
    HighPriority,   // 玩家附近，高优先级
};
```

调度规则：

```text
玩家附近：Active / HighPriority。
远处无人区域：Passive / LowFrequency。
完全无关区域：Unloaded。
```

---

## 5. Chunk 设计

### 5.1 Chunk 大小

```cpp
constexpr int CHUNK_SIZE = 16;
```

### 5.2 ChunkKey

全局坐标连续，但存储带 SectorId：

```cpp
struct ChunkKey {
    SectorId sector;
    ChunkCoord coord;
};
```

优势：

- 避免一个巨大哈希表管理所有 chunk。
- 方便按 Sector 存档、tick 和网络同步。
- 方便控制不同区域的模拟等级。

### 5.3 坐标转换

```cpp
class SectorManager {
public:
    SectorId find_sector(GlobalBlockPos pos);
    ChunkKey make_chunk_key(GlobalBlockPos pos);
};
```

流程：

```text
GlobalBlockPos → 找到所在 Sector → 转换成 Sector 内 ChunkCoord → 访问对应 Sector 的 ChunkManager
```

---

## 6. 星球系统

### 6.1 星球不是 World

星球是 Universe 里的一个天体对象。

```cpp
struct CelestialBody {
    PlanetId id;
    std::string name;
    GlobalPos center;
    double radius;
    double atmosphere_radius;
    SectorId surface_sector;
    SectorId orbit_sector;
    GravityProfile gravity;
    TerrainGeneratorId terrain_generator;
};
```

### 6.2 星球区域

```text
planet_alpha_surface：地表体素区域。
planet_alpha_orbit：轨道区域。
deep_space_alpha_beta：星球之间的深空区域。
```

---

## 7. 重力系统

### 7.1 基本接口

```cpp
class GravitySystem {
public:
    Vec3 sample_gravity(GlobalPos pos);
};
```

### 7.2 重力规则

```text
星球附近：重力方向指向星球中心。
太空中：默认无重力或微重力。
太空桥上：可以由人工重力方块提供方向重力。
空间站内：可以使用人工重力场。
```

### 7.3 重力场

```cpp
struct GravityField {
    GravityFieldId id;
    GravityFieldType type;
    GlobalPos center;
    double radius;
    double strength;
    SectorId sector;
};
```

```cpp
enum class GravityFieldType {
    PlanetRadial,
    ArtificialDirectional,
    NoGravityZone,
};
```

多个重力场接近或重叠时，需要定义选择规则，避免方向瞬跳。

---

## 8. 方块搭桥系统

### 8.1 统一 BlockSpace

统一宇宙坐标下，从一个星球搭到另一个星球不需要 WorldLink 或 Anchor。

```cpp
class BlockSpace {
public:
    BlockId get_block(GlobalBlockPos pos);
    void set_block(GlobalBlockPos pos, BlockId block);
    Optional<GlobalBlockPos> neighbor(GlobalBlockPos pos, Direction dir);
};
```

邻居查询能够跨 Sector 边界。

### 8.2 稀疏太空 Chunk

太空不能生成满世界 chunk。

```text
没有方块、没有实体、没有生成内容的太空 chunk 不创建。
有玩家搭桥、空间站、小行星、飞船停靠点时才创建 chunk。
```

### 8.3 建造限制

如果完全允许无限搭方块，星球之间距离不能采用真实天文尺度。

推荐游戏尺度：

```text
小星球半径：2048 格。
中型星球半径：4096 ~ 8192 格。
星球间距：100,000 ~ 500,000 格。
```

如果星球间距太大，需要增加太空桥投影器、轨道施工无人机、大型结构块、星际道路蓝图、空间锚点/稳定器等辅助建造手段。

---

## 9. LOD 系统

### 9.1 LOD 层级

即使是统一宇宙，也不能在太空中同步整颗星球真实体素。

```text
LOD 0：真实体素 chunk。
LOD 1：低精度地形 mesh。
LOD 2：星球球体模型 + 云层。
LOD 3：远处星点 / 图标。
LOD 4：Billboard。
```

### 9.2 LOD 接口

```cpp
class CelestialLodSystem {
public:
    LodLevel choose_lod(PlayerId player, PlanetId planet);
    PlanetVisualData build_visual(PlanetId planet, LodLevel lod);
};
```

### 9.3 同步规则

```text
近处：同步真实 chunk。
中距离：同步低精度地形 mesh。
远距离：同步 PlanetProxy / 星球模型。
极远距离：只同步星点或 UI 标记。
```

远处天体不得触发真实体素加载。

---

## 10. Chunk Streaming

### 10.1 基本系统

```cpp
class ChunkStreamingSystem {
public:
    void update_player_interest(PlayerSession& player);
    void request_chunks(PlayerId player, std::span<ChunkKey> chunks);
    void unload_far_chunks(PlayerId player);
};
```

### 10.2 加载区域

不要只使用固定球形加载范围。

```text
低速移动：球形 AOI。
高速飞船：前方锥形 AOI。
降落阶段：目标地表 AOI。
太空桥移动：沿桥方向预加载。
```

### 10.3 高速预测

```cpp
GlobalPos predicted = player.pos + player.velocity * preload_seconds;
```

飞船速度越快，预加载区域越偏向前方。根据速度与航向计算前向预加载区域，并为 chunk 请求、mesh 构建和内存设置逐帧预算。

---

## 11. 网络同步系统

### 11.1 原则

服务端内部可以用全局坐标，但网络包不要频繁发送完整 int64 坐标。网络同步应基于：

```text
SectorId + ChunkCoord + chunk 内 local_index
```

### 11.2 数据包格式

```cpp
struct ChunkDataPacket {
    SectorId sector;
    ChunkCoord coord;
    CompressedChunkData data;
};

struct BlockDeltaPacket {
    SectorId sector;
    ChunkCoord coord;
    std::vector<BlockDelta> deltas;
};

struct BlockDelta {
    uint16_t local_index;
    BlockId block;
};

struct EntitySnapshot {
    EntityId entity;
    SectorId sector;
    Vec3 local_pos;
    Vec3 velocity;
};
```

### 11.3 网络优化原则

- 方块更新按 chunk 批量发送。
- 机器状态只同步给附近玩家或打开 GUI 的玩家。
- 远处星球只同步 LOD 信息。
- 不同步玩家看不到、不能交互的远处真实体素。
- 每个客户端有发送预算，避免一帧塞爆网络。

---

## 12. AOI 兴趣区域系统

### 12.1 AOI 的必要性

玩家不应该收到整个 Universe 的数据。只应该收到：

- 附近 chunk。
- 附近实体。
- 附近机器简略状态。
- 打开 GUI 的机器详细状态。
- 远处星球 LOD。
- 远处大型结构 LOD。

### 12.2 InterestManager

```cpp
class InterestManager {
public:
    InterestSet compute_interest(PlayerSession& player);
};
```

```cpp
struct InterestSet {
    std::vector<ChunkKey> chunks;
    std::vector<EntityId> entities;
    std::vector<MachineId> machines;
    std::vector<CelestialBodyId> celestial_lods;
};
```

### 12.3 分通道发送预算

chunk、实体、block delta、机器详情和天体 LOD 分通道设置发送预算与优先级。

```text
每 tick 最大发送字节数。
每秒最大 chunk 数。
每 tick 最大实体快照数。
每 tick 最大 block delta 数。
每帧最大 mesh 生成数。
```

任一客户端高速移动都不会挤占另一客户端的同步预算。

---

## 13. 机器系统

### 13.1 机器实例使用数据记录

不要把机器设计成继承对象。

```cpp
struct MachineBlockEntityState {
    MachineId id;
    MachineTypeId type;
    GlobalBlockPos controller_pos;
    Direction facing;
    bool formed;
    std::vector<GlobalBlockPos> claimed_cells;
    std::vector<EntityId> hatch_entities;
    uint32_t dirty_flags;
};
```

### 13.2 MachineId

机器属于某个 Sector：

```cpp
struct MachineId {
    SectorId sector;
    uint32_t local_id;
};
```

### 13.3 按 Sector tick

```cpp
void Sector::tick_sector(SectorTickContext& ctx) {
    machines.tick(ctx);
    power.tick(ctx);
    logistics.tick(ctx);
}
```

不要把全宇宙所有机器放进一个系统里统一 tick。

---

## 14. 电网、物流、AE 网络边界

### 14.1 核心原则

方块可以连续，玩家可以连续走，但复杂网络不能无限全局连通。

否则会出现跨星球超级电网、跨星球超级物流网、跨星球超级 AE 网络，导致：

- 网络图过大。
- 重建成本高。
- 远处 chunk 被迫加载。
- 单个节点变化导致全网 dirty。
- 多人服务器 tick 压力暴涨。

### 14.2 电网分段

普通电网限制在 Sector 内。

```cpp
struct PowerNetwork {
    PowerNetworkId id;
    SectorId sector;
    std::vector<PowerNodeId> nodes;
    PowerTier tier;
};
```

跨 Sector 用 PowerBridge：

```cpp
struct PowerBridge {
    PowerNetworkId a;
    PowerNetworkId b;
    int64_t max_transfer_per_tick;
    double loss_factor;
};
```

### 14.3 物流分段

物流通过包裹系统跨区域：

```cpp
struct LogisticsPackage {
    ItemStack stack;
    GlobalBlockPos current_pos;
    LogisticsRouteId route;
};
```

推荐路径：

```text
本地传送带 → 太空货运节点 → 太空桥物流线 → 目标星球货运节点 → 本地传送带
```

### 14.4 AE 类网络分段

```text
本地 AE 网络：Sector 内。
跨 Sector：量子桥。
跨星球：星际量子链路。
```

不要让一个 AE 网络自然跨几十万格无限连通。

### 14.5 跨 Sector 中继

跨 Sector 传输通过显式中继，中继具备容量、损耗、队列和断线状态。网络图 dirty 与重建不得递归加载另一 Sector 的普通节点。

---

## 15. 存档结构

存档按 Sector 拆分。

```text
saves/
  main_save/
    universe.json
    sectors/
      planet_alpha_surface/
        sector.json
        regions/
        machines/
        networks/
      planet_alpha_orbit/
        sector.json
        regions/
      deep_space_alpha_beta/
        sector.json
        regions/
      planet_beta_surface/
        sector.json
        regions/
        machines/
        networks/
    global/
      celestial_bodies.json
      players/
      teams.json
      tech_tree.json
      blueprints/
```

### 15.1 universe.json

```json
{
  "save_id": "main_save",
  "data_version": 1,
  "seed": 123456789,
  "active_sectors": [
    "planet_alpha_surface",
    "planet_alpha_orbit",
    "deep_space_alpha_beta",
    "planet_beta_surface"
  ]
}
```

### 15.2 celestial_bodies.json

```json
{
  "bodies": [
    {
      "id": "planet_alpha",
      "center": [0, 0, 0],
      "radius": 4096,
      "surface_sector": "planet_alpha_surface",
      "orbit_sector": "planet_alpha_orbit"
    },
    {
      "id": "planet_beta",
      "center": [120000, 0, 0],
      "radius": 4096,
      "surface_sector": "planet_beta_surface",
      "orbit_sector": "planet_beta_orbit"
    }
  ]
}
```

### 15.3 存档可靠性

- 采用临时文件、CRC32 校验和、原子替换写入。
- 支持版本迁移，旧版本处理结果明确且可追踪。
- Sector 独立损坏时允许隔离并给出可操作错误。
- 存档必须异步：脏 chunk 异步写入、机器状态批量写入、网络图分批保存、Sector 独立 flush。

---

## 16. 服务端 Tick 流程

```cpp
void UniverseServer::tick() {
    players.update_inputs();
    universe.chunk_streaming.update(players);
    universe.sectors.update_simulation_levels(players);
    for (Sector& sector : universe.sectors.active_sectors()) {
        sector.tick_sector(ctx);
    }
    universe.entities.process_cross_sector_movement();
    universe.gravity.update_dynamic_fields();
    network.sync(players, universe);
    save_system.flush_dirty_async();
}
```

推荐顺序：

```text
1. 处理玩家输入。
2. 更新 chunk 加载范围。
3. 更新 Sector 模拟等级。
4. tick 活跃 Sector。
5. 处理实体跨 Sector。
6. 网络同步。
7. 异步存档。
```

---

## 17. 实体跨 Sector

统一宇宙没有 World 切换，但仍然有 Sector 迁移。

```cpp
void EntitySystem::update_entity_sector(Entity& e) {
    SectorId new_sector = sector_manager.find_sector(e.global_pos);
    if (new_sector != e.sector) {
        move_entity_to_sector(e, new_sector);
    }
}
```

需要处理：

- 从旧 Sector 的实体分区移除，加入新 Sector。
- 更新网络订阅、物理区域、重力规则、碰撞和 chunk interest。

这是轻量级切换，不应该像切 World 那样重。

---

## 18. 高速飞船设计

### 18.1 飞行模式

高速飞行时每秒可能跨越大量 chunk，需要飞行模式：

```cpp
enum class FlightMode {
    LocalVoxelFlight,
    Cruise,
    Warp,
    LandingApproach
};
```

### 18.2 模式说明

```text
LocalVoxelFlight：真实体素飞行，适合地表、空间站附近、太空桥附近。
Cruise：宏观巡航，只同步飞船、星球 LOD、航线、大型结构代理。
Warp：更高速的跃迁模式，基本不加载真实体素。
LandingApproach：接近星球或空间站，开始预加载真实 chunk。
```

### 18.3 接近目标区域

```text
高速巡航 → 接近目标星球/空间站/太空桥 → 退出巡航 → 预加载真实 chunk → 进入 LocalVoxelFlight
```

这样可以避免高速 chunk streaming 爆炸。

---

## 19. 多人同步设计

### 19.1 玩家订阅

每个玩家根据自己的位置和速度计算 InterestSet。

```text
玩家 A 在星球 A：订阅星球 A 附近 chunk。
玩家 B 在太空桥：订阅太空桥附近 chunk。
玩家 C 在星球 B：订阅星球 B 附近 chunk。
```

两个客户端分处不同 Sector 时互不接收无关真实 chunk；会合后实体与方块状态收敛。

### 19.2 多人飞船

多人同乘飞船时，飞船作为同步主体：

```cpp
struct VehicleGroup {
    EntityId vehicle;
    std::vector<PlayerId> passengers;
};
```

同步重点：飞船位置、速度、乘客绑定关系、飞船内部状态、所在 Sector、附近 chunk interest。

---

## 20. 性能控制原则

### 20.1 分区 tick

禁止全宇宙所有机器、电网、物流每 tick 遍历。

应该只 tick 活跃 Sector、只 tick 活跃 chunk、远处系统低频 tick 或休眠。

### 20.2 分通道性能预算

对 chunk 生成、mesh 构建、网络同步、模拟和存档分别建立可观测预算，不用一个总耗时掩盖局部尖峰。

### 20.3 存档预算

存档必须异步：脏 chunk 异步写入、机器状态批量写入、网络图分批保存、Sector 独立 flush。

---

## 21. 虚拟星球模拟

远处无人星球使用虚拟模拟器产生产物，回灌时保证幂等性：重复加载、异常退出和重试不会重复结算。

---

## 22. 推荐目录结构

```text
src/
  universe/
    universe_world.hpp
    universe_world.cpp
    sector.hpp
    sector.cpp
    sector_manager.hpp
    sector_manager.cpp
    celestial_body.hpp
    celestial_system.hpp
    gravity_system.hpp

  world/
    chunk.hpp
    chunk_manager.hpp
    block_space.hpp
    terrain_generator.hpp

  streaming/
    chunk_streaming_system.hpp
    interest_manager.hpp
    lod_system.hpp

  entity/
    entity.hpp
    entity_system.hpp
    vehicle_system.hpp

  machine/
    machine_state.hpp
    machine_definition.hpp
    machine_system.hpp
    power_network_system.hpp
    logistics_system.hpp

  network/
    packets.hpp
    network_server.hpp
    replication_system.hpp

  save/
    save_system.hpp
    sector_save.hpp
    chunk_region_file.hpp
```

---

## 23. 核心原则总结

```text
1. 方块和玩家移动是连续的。
2. Chunk 和模拟按 Sector 分区。
3. 网络同步按 AOI 裁剪。
4. 远处星球用 LOD。
5. 机器、电网、物流不能无限全局连通。
6. 高速飞船用巡航模式。
7. 客户端使用 floating origin。
8. 存档按 Sector 拆分。
```

一句话总结：

```text
玩家看到一个连续宇宙；
服务端内部是 Sector 化、LOD 化、AOI 化、分段模拟的统一世界。
```
