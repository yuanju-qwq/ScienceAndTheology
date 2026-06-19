# 统一宇宙 World 架构设计文档

## 1. 设计目标

本设计用于实现一个 **连续宇宙体素世界**：

- 玩家可以从一个星球起飞，进入太空，再飞到另一个星球。
- 玩家可以从一个星球一路搭方块，最终搭到另一个星球。
- 星球、太空、空间站、小行星、太空桥都处于同一个连续宇宙坐标体系中。
- 服务器内部仍然通过 Sector、Chunk、Simulation Island 等机制进行性能分区。
- 网络同步只同步玩家兴趣范围内的数据，远处天体使用 LOD / Proxy 表示。
- 工厂系统、电网、物流、AE 类网络不能无限全局连通，需要边界和中继机制。

核心思想：

```text
对玩家：一个连续宇宙。
对引擎：统一坐标 + Sector 分区 + Chunk 流式加载 + 分段模拟。
```

不要做成：

```text
一个巨大 World + 一个 ChunkManager + 所有机器全局 tick。
```

而应该做成：

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

---

## 2. 总体架构

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

推荐规则：

```text
方块：int64 格子坐标。
实体：double 连续坐标。
客户端渲染：相对玩家的 float 局部坐标。
```

### 3.2 客户端 Floating Origin

客户端不要直接用超大全局坐标渲染，否则会出现浮点精度抖动。

客户端应该使用相机相对坐标：

```cpp
RenderPos render_pos = entity.global_pos - client.camera_origin;
```

服务端保存真实全局位置，客户端只使用局部渲染坐标。

---

## 4. Sector 分区设计

### 4.1 Sector 的作用

`Sector` 用于解决以下问题：

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
planet_alpha_surface
  ↓
planet_alpha_orbit
  ↓
deep_space_route_alpha_beta
  ↓
planet_beta_orbit
  ↓
planet_beta_surface
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

推荐：

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

### 5.2 Chunk 坐标

```cpp
struct ChunkCoord {
    int64_t cx;
    int64_t cy;
    int64_t cz;
};
```

### 5.3 ChunkKey

虽然全局坐标连续，但实际存储最好带 SectorId：

```cpp
struct ChunkKey {
    SectorId sector;
    ChunkCoord coord;
};
```

优势：

- 避免一个巨大哈希表管理所有 chunk。
- 方便按 Sector 存档。
- 方便按 Sector tick 和网络同步。
- 方便控制不同区域的模拟等级。

### 5.4 坐标转换

```cpp
class SectorManager {
public:
    SectorId find_sector(GlobalBlockPos pos);
    ChunkKey make_chunk_key(GlobalBlockPos pos);
};
```

流程：

```text
GlobalBlockPos
  ↓
找到所在 Sector
  ↓
转换成该 Sector 内的 ChunkCoord
  ↓
访问对应 Sector 的 ChunkManager
```

---

## 6. 星球系统

### 6.1 星球不是 World

在方案一中，星球不是独立 World，而是 Universe 里的一个天体对象。

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

一个星球可以拆成多个 Sector：

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

---

## 8. 方块搭桥系统

### 8.1 方案一的优势

统一宇宙坐标下，从一个星球搭到另一个星球不需要 WorldLink，也不需要 Anchor。

放方块就是：

```cpp
class BlockSpace {
public:
    BlockId get_block(GlobalBlockPos pos);
    void set_block(GlobalBlockPos pos, BlockId block);

    Optional<GlobalBlockPos> neighbor(GlobalBlockPos pos, Direction dir);
};
```

普通邻居：

```cpp
GlobalBlockPos neighbor = pos + direction_to_offset(dir);
```

### 8.2 稀疏太空 Chunk

太空不能生成满世界 chunk。

规则：

```text
没有方块、没有实体、没有生成内容的太空 chunk 不创建。
有玩家搭桥、空间站、小行星、飞船停靠点时才创建 chunk。
```

### 8.3 建造限制建议

如果完全允许无限搭方块，星球之间距离不能采用真实天文尺度。

推荐游戏尺度：

```text
小星球半径：2048 格。
中型星球半径：4096 ~ 8192 格。
星球间距：100,000 ~ 500,000 格。
```

如果星球间距太大，需要增加：

- 太空桥投影器。
- 轨道施工无人机。
- 大型结构块。
- 星际道路蓝图。
- 空间锚点 / 稳定器。

---

## 9. LOD 系统

### 9.1 为什么必须有 LOD

即使是统一宇宙，也不能在太空中同步整颗星球真实体素。

玩家在远处看到星球时，应该显示：

```text
LOD 0：真实体素 chunk。
LOD 1：低精度地形 mesh。
LOD 2：星球球体模型 + 云层。
LOD 3：远处星点 / 图标。
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

推荐：

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

飞船速度越快，预加载区域越偏向前方。

---

## 11. 网络同步系统

### 11.1 原则

服务端内部可以用全局坐标，但网络包不要频繁发送完整 int64 坐标。

网络同步应基于：

```text
SectorId + ChunkCoord + chunk 内 local_index
```

### 11.2 Chunk 数据包

```cpp
struct ChunkDataPacket {
    SectorId sector;
    ChunkCoord coord;
    CompressedChunkData data;
};
```

### 11.3 方块更新包

```cpp
struct BlockDeltaPacket {
    SectorId sector;
    ChunkCoord coord;
    std::vector<BlockDelta> deltas;
};
```

```cpp
struct BlockDelta {
    uint16_t local_index;
    BlockId block;
};
```

### 11.4 实体同步包

```cpp
struct EntitySnapshot {
    EntityId entity;
    SectorId sector;
    Vec3 local_pos;
    Vec3 velocity;
};
```

### 11.5 网络优化原则

- 方块更新按 chunk 批量发送。
- 机器状态只同步给附近玩家或打开 GUI 的玩家。
- 远处星球只同步 LOD 信息。
- 不同步玩家看不到、不能交互的远处真实体素。
- 每个客户端有发送预算，避免一帧塞爆网络。

---

## 12. AOI 兴趣区域系统

### 12.1 AOI 的必要性

方案一的网络性能依赖 AOI。

玩家不应该收到整个 Universe 的数据。

只应该收到：

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

---

## 13. 机器系统

### 13.1 机器实例仍然使用数据记录

不要因为采用方案一，就把机器设计成继承对象。

推荐：

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

机器应该属于某个 Sector：

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

否则会出现：

```text
跨星球超级电网。
跨星球超级物流网。
跨星球超级 AE 网络。
```

这会导致：

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

物流可以通过包裹系统跨区域：

```cpp
struct LogisticsPackage {
    ItemStack stack;
    GlobalBlockPos current_pos;
    LogisticsRouteId route;
};
```

推荐路径：

```text
本地传送带
  ↓
太空货运节点
  ↓
太空桥物流线
  ↓
目标星球货运节点
  ↓
本地传送带
```

### 14.4 AE 类网络分段

推荐：

```text
本地 AE 网络：Sector 内。
跨 Sector：量子桥。
跨星球：星际量子链路。
```

不要让一个 AE 网络自然跨几十万格无限连通。

---

## 15. 存档结构

虽然逻辑上是一个 UniverseWorld，但存档必须按 Sector 拆分。

推荐结构：

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

方案一没有 World 切换，但仍然有 Sector 迁移。

```cpp
void EntitySystem::update_entity_sector(Entity& e) {
    SectorId new_sector = sector_manager.find_sector(e.global_pos);

    if (new_sector != e.sector) {
        move_entity_to_sector(e, new_sector);
    }
}
```

需要处理：

- 从旧 Sector 的实体分区移除。
- 加入新 Sector。
- 更新网络订阅。
- 更新物理区域。
- 更新重力规则。
- 更新碰撞和 chunk interest。

这是轻量级切换，不应该像切 World 那样重。

---

## 18. 高速飞船设计

### 18.1 不要让高速飞船依赖完整体素加载

高速飞行时，每秒可能跨越大量 chunk。

所以需要飞行模式：

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
LocalVoxelFlight：
真实体素飞行，适合地表、空间站附近、太空桥附近。

Cruise：
宏观巡航，只同步飞船、星球 LOD、航线、大型结构 LOD。

Warp：
更高速的跃迁模式，基本不加载真实体素。

LandingApproach：
接近星球或空间站，开始预加载真实 chunk。
```

### 18.3 接近目标区域

```text
高速巡航
  ↓
接近目标星球 / 空间站 / 太空桥
  ↓
退出巡航
  ↓
预加载真实 chunk
  ↓
进入 LocalVoxelFlight
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

### 19.2 多人飞船

多人同乘飞船时，飞船应该作为同步主体：

```cpp
struct VehicleGroup {
    EntityId vehicle;
    std::vector<PlayerId> passengers;
};
```

同步重点：

- 飞船位置。
- 飞船速度。
- 乘客绑定关系。
- 飞船内部状态。
- 所在 Sector。
- 附近 chunk interest。

---

## 20. 性能控制原则

### 20.1 不能全局 tick

禁止：

```text
全宇宙所有机器每 tick 遍历。
全宇宙所有电网每 tick 求解。
全宇宙所有物流每 tick 寻路。
```

应该：

```text
只 tick 活跃 Sector。
只 tick 活跃 chunk。
远处系统低频 tick 或休眠。
```

### 20.2 网络预算

每个客户端设置预算：

```text
每 tick 最大发送字节数。
每秒最大 chunk 数。
每 tick 最大实体快照数。
每 tick 最大 block delta 数。
每帧最大 mesh 生成数。
```

### 20.3 存档预算

存档必须异步：

```text
脏 chunk 异步写入。
机器状态批量写入。
网络图分批保存。
Sector 独立 flush。
```

---

## 21. 开发阶段规划（按当前项目状态重排）

### 21.1 当前基线与关键判断

当前项目已经具备大量可复用基础，不应重新实现旧规划中的“基础方块世界”：

| 能力 | 当前状态 | 统一宇宙仍缺少的部分 |
|---|---|---|
| 3D voxel chunk、异步生成、greedy mesh、chunk 碰撞 | 已具备 | chunk 仍以 `dimension_id + Vector3i` 定位，没有 `SectorId + GlobalBlockPos` |
| 球形星球、径向地层、动态球心重力、大气环境 | 已具备 | 重力与地形仍围绕“当前活跃星球”的局部坐标工作 |
| LOD 0~4、星球代理球、低模与 billboard | 已具备 | LOD 还没有与服务端 AOI、连续航行状态和加载预算统一 |
| 多星球描述、随机星系、远处星系占位 | 已具备原型 | `UniverseManager` 主要位于 GDScript，逻辑权威尚未下沉到 C++ core |
| 多星球旅行 | 已具备传送式原型 | `travel_to_planet()` 会切换活跃 `dimension` 并重定位玩家，不是连续飞行 |
| 浮动原点 | 已具备坐标跟踪原型 | 当前主要服务于星球切换和远景相对摆放，还不是运行中对场景根节点的完整重基准 |
| 分星球存档、3D region 文件、远端星球摘要模拟 | 已具备基础 | 存档键仍是 `dimension_id`；虚拟产物回灌仍有 TODO，缺少 Sector 数据版本迁移 |
| 空间站 | 已具备独立维度原型 | 当前通过 `StationDescriptor + MapConnector` 进入，不是统一空间中的 `SpaceStation` Sector |
| 机器、方块实体、电网、流体、物品管道、AE2 core | 核心已具备 | 尚未定义 Sector 内网络边界和跨 Sector 中继协议 |
| StateSync 与单机权威命令 | 基础已具备 | `PlayerId` 全链路、多观察者 AOI、真实网络层和专用服务器尚未完成 |

因此，后续主线不是“再做一个多星球系统”，而是把现有的多 `dimension` 原型收敛为真正的统一宇宙：

```text
当前：星球/空间站 = 独立 dimension，旅行 = 切换 + 传送。
目标：星球/轨道/深空/空间站 = 同一 UniverseWorld 中的 Sector，旅行 = 连续移动 + Sector 迁移。
```

迁移期间允许保留 `dimension_id` 作为存档兼容键，但不能把它直接改名后当成 `SectorId`；两者的坐标语义和生命周期不同。

### 21.2 阶段总览

| 阶段 | 目标 | 状态/优先级 |
|---|---|---|
| U0 | 锁定当前可运行基线 | 已完成（2026-06-19） |
| U1 | 统一坐标、Sector 数据模型与存档版本 | 下一阶段，最高优先级 |
| U2 | 单星球“地表 + 轨道”连续切换 | 统一宇宙最小闭环 |
| U3 | Deep Space、连续航行与预算化 streaming | 依赖 U2 |
| U4 | 第二颗真实星球与连续着陆 | 依赖 U3 |
| U5 | 跨 Sector 建造与空间站迁移 | 依赖 U2，建议在 U4 后集中完成 |
| U6 | 分段模拟与跨 Sector 工业中继 | 依赖 U5 |
| U7 | 多人 AOI、并发 Sector 与 Dedicated Server | 依赖 U3，并与多人文档 M1~M5 对齐 |
| U8 | 性能压测、故障恢复和发布收口 | 贯穿开发，最终集中验收 |

### U0：锁定当前原型基线

目标是保护已有功能，避免架构迁移时把现有玩法一起推倒。

状态：已完成。测试入口、采集方法与首份数据见《U0 原型基线》。

工作项：

- 固化单星球生成、挖掘/放置、机器、存读档、LOD 切换和传送式多星球旅行的冒烟测试。
- 记录典型场景的 chunk 数、生成队列长度、mesh 构建耗时、存档耗时和内存占用，作为后续比较基线。
- 将 `/travel` 和连接器式空间站明确标记为迁移期调试/兼容入口，不再在其上扩展正式玩法。
- 保留现有低频诊断开关；日志只记录 Sector 切换、加载状态变化、存档结果和预算超限，不做每 tick 输出。

验收条件：当前 C++ 测试、GDScript 测试和 Godot headless 启动检查可重复通过，并形成一份可对比的性能基线。

### U1：统一坐标与 Sector 核心

这是后续所有阶段的前置条件，先解决数据语义，再改表现层。

工作项：

- 在 C++ core 引入 `UniverseId`、`SectorId`、`GlobalBlockPos(int64)`、`GlobalPos(double)`、`SectorDesc`、`SectorKind` 和 `SimulationLevel`。
- 实现 `UniverseWorldCore` / `SectorManager`，负责全局坐标到 Sector、Sector 局部 chunk、方块局部坐标的唯一转换。
- 明确定义负坐标的 floor division、Sector 边界归属、重叠/空洞检测和越界行为。
- 将现有 `dimension_id` 包装为迁移期 `StorageShardId`；旧 API 通过适配层访问默认 Sector，新代码不再直接用 `dimension_id` 表达空间关系。
- 给 `ChunkKey`、block entity 位置、机器位置和存档元数据增加 Sector 语义，先保持旧行为不变。
- 将 Universe 描述、Sector 注册与坐标查询逐步下沉到纯 C++；`UniverseManager.gd` 退为 Godot 场景和 UI 适配器。
- 提升存档 `data_version`，在首次改键前实现旧 `dimension` 存档到 Sector 元数据的可检测迁移或明确拒绝路径。

验收条件：

- 坐标转换往返测试覆盖正负坐标、chunk/Sector 边界和大坐标。
- 同一 `GlobalBlockPos` 最多归属一个可建造 Sector；未归属位置返回明确结果。
- 现有单星球玩法通过兼容层运行，存档重载后方块和 block entity 坐标不变。

### U2：单星球地表与轨道连续化

先只处理一颗星球，将它拆成 `PlanetSurface` 和 `PlanetOrbit` 两个 Sector。

工作项：

- 让渲染与 chunk streaming 消费玩家附近的 Sector/Chunk 集合，不再依赖唯一 `active_dimension`。
- 实现地表与轨道之间的实体 Sector 迁移、重力变化和网络/模拟订阅变化。
- 轨道使用稀疏 chunk：纯空气区域不生成、不存档；只有建造、实体或生成内容时才实例化。
- 将 floating origin 扩展为运行时场景重基准：超过阈值时整体平移渲染根节点，同时保持逻辑坐标、速度、相机和射线结果连续。
- 在过渡期保留 `set_active_dimension()`，但统一宇宙路径不得调用它完成地表到轨道的移动。

验收条件：玩家能从地表连续飞入轨道再返回，全程不传送、不清空全部 chunk 视图；边界两侧方块查询、碰撞、重力和存档结果一致。

### U3：Deep Space 与连续航行

工作项：

- 增加 `DeepSpace` Sector，并建立星球轨道到深空的连续空间关系。
- 引入 `LocalVoxelFlight`、`Cruise`、`LandingApproach`；`Warp` 可后置，但接口在本阶段稳定。
- `LocalVoxelFlight` 加载真实 voxel；`Cruise` 只保留飞船、航线、天体 LOD 和大型结构代理。
- 根据速度与航向计算前向预加载区域，并为 chunk 请求、mesh 构建和内存设置逐帧预算。
- 把现有 LOD 0~4 接入统一 interest 结果，远处天体不得触发真实体素加载。
- 连续保存玩家/飞船的 `GlobalPos`、当前 Sector 和航行模式。

验收条件：玩家能从轨道进入 Deep Space 并持续飞行；巡航时真实 chunk 数保持有界，切回局部飞行时目标区域能在预算内完成预加载。

### U4：第二颗真实星球与连续着陆

工作项：

- 为第二颗星球注册地表和轨道 Sector，复用现有球形地形、资源差异、大气和重力配置。
- 实现 `Billboard/Proxy -> LandingApproach -> SimplifiedMesh -> RealChunks` 的加载链路。
- 定义多个重力场接近或重叠时的选择规则，避免方向瞬跳。
- 到达新星球时只更新玩家的 Sector 与 interest，不切换全局 `active_dimension`。
- `/travel` 保留为开发调试传送，正式旅行和验收不得依赖它。

验收条件：从星球 A 地表起飞，经轨道和 Deep Space 到星球 B 着陆，路径连续；B 的目标 chunk 按需生成，A 的修改在卸载/重载后仍存在。

### U5：跨 Sector 建造与空间站迁移

工作项：

- 实现基于 `GlobalBlockPos` 的 `BlockSpace`，邻居查询能够跨 Sector 边界。
- 方块放置、挖掘、block entity 注册、碰撞刷新和存档 dirty 标记统一走 `BlockSpace`。
- 对太空结构使用稀疏 chunk，并定义空 chunk 回收、结构锚定和最大连续建造预算。
- 将 `StationDescriptor` 的正式实现迁为统一宇宙中的 `SpaceStation` Sector；旧 `MapConnector` 仅用于旧存档兼容。
- 支持短距离跨 Sector 桥接测试；超长星际工程再通过蓝图、投影器或施工无人机扩展，不在底层制造特殊邻接规则。

验收条件：一座跨 Sector 边界的测试桥可连续放置、行走、拆除并正确存读档；空间站可在宇宙坐标中直接接近和离开，不需要切换 dimension。

### U6：分段模拟与工业网络边界

工作项：

- 让 `TickSystem` 按 Sector/Chunk 的 `Unloaded`、`Passive`、`LowFrequency`、`Active`、`HighPriority` 等级调度。
- 现有机器、方块物理、生态、作物、电网、流体、物品管道和 AE2 网络先限制在单 Sector 内。
- 为跨 Sector 传输增加显式 `PowerBridge`、货运节点、流体中继和 AE2 量子链路；中继具备容量、损耗、队列和断线状态。
- 网络图 dirty 与重建不得递归加载另一 Sector 的普通节点。
- 完成 `VirtualPlanetSimulator` 产物回灌，保证重复加载、异常退出和重试不会重复结算。

验收条件：一个 Sector 内的网络变化不会遍历或强制加载另一 Sector；跨 Sector 资源只能经过中继，并受吞吐与损耗约束；休眠 Sector 恢复后状态可确定重放。

### U7：多人 AOI 与并发 Sector

本阶段与《多人游戏系统设计》的 M1~M5 合并执行，不另建一套网络架构。

工作项：

- 完成 `PlayerId`、`PlayerManager`、多玩家活跃集和 per-observer `StateSync`。
- 服务端 `InterestManager` 按玩家位置、速度、Sector、打开的 GUI 和所乘飞船生成 AOI。
- chunk、实体、block delta、机器详情和天体 LOD 分通道设置发送预算与优先级。
- 支持玩家分别位于不同星球/深空/空间站，服务端同时维护多个活跃 Sector。
- 完成 LAN 网络层与 Godot headless Dedicated Server；客户端预测和回滚按多人文档后续阶段实现。

验收条件：两个客户端分处不同 Sector 时互不接收无关真实 chunk；会合后实体与方块状态收敛；任一客户端高速移动都不会挤占另一客户端的同步预算。

### U8：性能、可靠性与发布收口

工作项：

- 建立地表、起飞、巡航、着陆、跨 Sector 工厂和多人分散六类压测场景。
- 对 chunk 生成、mesh、网络、模拟和存档分别建立可观测预算，不用一个总耗时掩盖局部尖峰。
- 存档采用临时文件、校验和、原子替换与版本迁移；Sector 独立损坏时允许隔离并给出可操作错误。
- 补齐跨边界、负坐标、浮动原点重基准、断线重连、卸载中保存和中继断开等故障测试。
- 只在状态变化或预算持续超限时输出低频日志，发布构建默认关闭详细诊断。

验收条件：所有阶段测试通过，长时间航行与多人分散运行不存在无界内存增长；存档中断可恢复，旧版本处理结果明确且可追踪。

### 21.3 里程碑边界

```text
R1（U1~U2）：单星球已是真正的多 Sector 连续世界。
R2（U3~U4）：两颗星球之间可以不经传送完成连续旅行。
R3（U5~U6）：跨 Sector 建造、空间站和工业中继形成可玩闭环。
R4（U7~U8）：多人并发、AOI、专用服务器和可靠性达到可发布标准。
```

在 R2 之前，不继续扩展传送式多星球玩法；在 R3 之前，不允许普通工业网络自然跨 Sector；在 R4 之前，不以单客户端演示结果代替服务端并发验收。

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

## 23. 最终结论

方案一的正确形态不是：

```text
一个超大 World 硬扛所有东西。
```

而是：

```text
一个统一 UniverseWorld + 多 Sector + 分区模拟 + LOD + AOI + 流式加载。
```

核心原则：

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
