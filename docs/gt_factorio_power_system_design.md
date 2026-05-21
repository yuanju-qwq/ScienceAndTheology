# GT 风格工业游戏 —— 电力系统设计文档，可以参考上级目录GT5项目

## 1. 设计目标

本项目的目标并不是“复刻 GregTech”，而是：

> 在 2D 工业自动化游戏中，保留 GT 工业 progression 与电力系统深度，同时结合 Factorio 风格的大规模电网体验。

核心方向：

- 保留 GT 的工业层级与能源体系
- 保留电压、tier、变压器、过载等机制
- 放弃 Minecraft 式逐格电线连接
- 使用 Factorio 风格电线杆 + 可视化电网
- 使用现代化高性能电网算法
- 支持超大型自动化工厂

---

# 2. 整体架构

```text
Godot 2D
│
├── GDScript
│   ├── UI
│   ├── 动画
│   ├── 电网可视化
│   ├── 玩家交互
│   └── 建筑放置
│
├── C++ GDExtension
│   ├── 电网系统
│   ├── AE2物流系统
│   ├── 工业机器系统
│   ├── 配方系统
│   ├── 多方块系统
│   ├── 流体系统
│   └── 存档系统
│
└── 数据驱动
    ├── JSON机器定义
    ├── 配方
    ├── 电压配置
    ├── 科技树
    └── 平衡参数
```

---

# 3. 电力系统设计

## 3.1 目标

本系统需要实现：

- GT 风格电压等级
- 机器 tier progression
- 变压器
- 电网区域化
- 电力容量限制
- 过载与烧毁
- 大规模工厂性能优化

同时避免：

- Minecraft 式逐格导线更新
- Tick 级复杂电流传播
- 高成本邻居扫描
- 超大规模网络卡顿

---

# 4. 电压等级系统

## 4.1 电压 Tier

参考 GT：

| Tier | 电压 |
|---|---|
| ULV | 8 |
| LV | 32 |
| MV | 128 |
| HV | 512 |
| EV | 2048 |
| IV | 8192 |
| LuV | 32768 |
| ZPM | 131072 |
| UV | 524288 |

可进一步扩展未来科技等级。

---

## 4.2 机器等级

每个机器具有：

```text
machine_tier
max_voltage
power_usage
```

例如：

```json
{
  "name": "Electric Furnace",
  "tier": "LV",
  "max_voltage": 32,
  "power_usage": 24
}
```

---

# 5. 电线杆系统（核心改造）

## 5.1 与 GT 原版区别

### GT 原版

```text
机器 — 电缆 — 电缆 — 电缆 — 发电机
```

问题：

- 每格都是节点
- 网络复杂度高
- Tick 更新昂贵
- 大规模工厂性能差

---

## 5.2 新方案

采用 Factorio 风格：

```text
发电站
  ↓
高压电线杆
  ↓
区域变电站
  ↓
中压工业区
```

电线杆之间：

- 自动连接
- 玩家手动连接
- 支持远距离输电
- 可视化电网

---

# 6. 电网结构

## 6.1 图结构

电网本质：

```text
Graph
```

其中：

- 节点 = 电线杆 / 变压器 / 发电站
- 边 = 输电线路

数据结构：

```cpp
struct PowerNode {
    int id;
    VoltageTier tier;
    float capacity;
    vector<int> connections;
};
```

---

## 6.2 网络更新

避免逐 tick 全图扫描。

仅在以下情况更新：

- 新增节点
- 删除节点
- 电线断开
- 电网合并
- 电网分裂

运行时：

```text
区域供电缓存
```

机器只读取当前区域供电状态。

---

# 7. 安培系统改造

## 7.1 原版 GT 问题

GT 安培：

```text
packet energy system
```

高度 Minecraft 化。

不适合：

- 超大工厂
- 高 FPS 实时模拟
- 海量机器

---

## 7.2 新方案：容量系统

将安培抽象为：

```text
输电容量
```

例如：

| 电线类型 | 最大容量 |
|---|---|
| LV Cable | 128 kW |
| MV Cable | 512 kW |
| HV Cable | 2048 kW |

eg:不同的材料做的线缆承载的电压和最大容量不同，不保留gt的多X线缆，只有线缆，但是容量取gt的16x线缆的值
机器耗电：

```text
机器功率消耗 ≤ 电网容量
```

---

# 8. 过载与爆线

## 8.1 机器过压

例如：

```text
LV机器接HV电网
```

结果：

- 机器爆炸
- 火花特效
- 电网跳闸

---

## 8.2 电线过载

例如：

```text
HV线路承载EV负载
```

结果：

- 电线烧毁
- 输电中断
- 火灾
- 连锁停电

---

# 9. 变压器系统

## 9.1 功能

变压器负责：

- 升压
- 降压
- 区域供电
- 电网隔离

例如：

```text
EV 主干线
↓
HV 工业区
↓
MV 加工区
↓
LV 基础设施
```

---

## 9.2 区域化电网

整个工厂被分成：

- 主干高压网
- 工业区中压网
- 民用低压网

这样：

- 更工业化
- 更清晰
- 更适合玩家规划

---

# 10. 多方块系统

## 10.1 2D Blueprint

GT 多方块改为：

```text
#####
#CCC#
#CMC#
#CCC#
#####
```

其中：

- M = 主控制器
- C = 外壳

---

## 10.2 自动结构检测

控制器负责：

- Blueprint 校验
- 结构完整性
- IO 检测
- 功率需求
- 效率计算

---

# 11. AE2 融合设计

## 11.1 三网分离

建议：

| 网络 | 功能 |
|---|---|
| 电力网 | 能量传输 |
| 物流网 | 物品运输 |
| 数据网 | AE2 智能网络 |

---

## 11.2 AE2 网络

AE2 不再使用方块 cable：

改为：

```text
数据节点 + 数据中继塔
```

支持：

- 请求合成
- 数字存储
- 自动物流
- 网络频道
- 区域数据网络

---

# 12. 游戏风格定位

本项目最终方向：

不是：

```text
Minecraft-like
```

而是：

```text
大型工业自动化模拟游戏
```

---

# 13. 核心参考游戏

## 工业物流

- Factorio
- Dyson Sphere Program
- Captain of Industry

## 自动化

- shapez
- Mindustry

## 工业 progression

- GregTech
- GregTech New Horizons

## 智能物流

- Applied Energistics 2

---

# 14. 技术路线建议

## 第一阶段

实现：

- TileMap
- 电线杆
- 基础电网
- 简单机器
- 输电逻辑

---

## 第二阶段

实现：

- tier progression
- 变压器
- 过载
- 工业机器
- 流体系统

---

## 第三阶段

实现：

- AE2 智能物流
- 自动合成
- 多方块系统
- 区域网络
- 大规模优化

---

# 15. 最终目标

最终形成：

```text
GT工业深度
+
AE2智能物流
+
Factorio大规模工厂
+
现代化电网系统
+
2D高可读性
```

的独立工业自动化游戏。

---

# 16. C++ 核心 API（已实现）

## 16.1 PowerNetwork 类

电网系统的核心是 `PowerNetwork` 类（`src/core/gt/power_network.hpp`）：

### 节点生命周期
```cpp
PowerNodeId add_node(VoltageTier tier, MapPosition position);
bool remove_node(PowerNodeId node_id);
PowerNode* get_node(PowerNodeId node_id);
PowerNodeId get_node_at(MapPosition position) const;
```

- 每个地图位置只能有一个节点（`add_node` 在同一位置重复调用返回 `kInvalidNodeId`）
- 删除节点会自动断开所有连接的边
- 节点 ID 从 1 开始递增（0 = `kInvalidNodeId`）

### 边生命周期
```cpp
bool connect(PowerNodeId a, PowerNodeId b, const CableProperties& cable);
bool disconnect(PowerNodeId a, PowerNodeId b);
std::vector<const PowerEdge*> get_edges_for_node(PowerNodeId node_id) const;
```

- `connect()` 自动根据节点坐标计算曼哈顿距离，并缓存损耗
- 重复连接同一对节点会被拒绝
- 自连接（node_a == node_b）会被拒绝

### 网络拓扑
```cpp
void update_network();  // 重新计算整个电网状态
std::vector<PowerNodeId> find_connected_component(PowerNodeId start) const;
std::vector<std::vector<PowerNodeId>> find_all_components() const;
bool are_in_same_network(PowerNodeId a, PowerNodeId b) const;
```

- **非逐帧更新**：仅在拓扑变化后调用 `update_network()`
- 使用 BFS 发现连通分量
- `are_in_same_network()` 基于缓存的 component_index，O(1) 查询

### 电力状态查询
```cpp
void set_power_demand(PowerNodeId node_id, int64_t demand);
void set_generation_capacity(PowerNodeId node_id, int64_t capacity);
bool is_overloaded(PowerNodeId node_id) const;
OverloadInfo get_overload_info(PowerNodeId node_id) const;
int64_t get_total_power_loss() const;
int64_t get_total_generation() const;
int64_t get_total_demand() const;
```

### 过载回调
```cpp
void set_overload_callback(OverloadCallback callback);
// callback: void(PowerNodeId, const OverloadInfo&)
```

每次 `update_network()` 检测到过载时触发回调，GDScript 可以连接此回调来播放特效/处理爆炸。

---

## 16.2 数据结构

### PowerNode（节点）
```cpp
struct PowerNode {
    PowerNodeId id;                    // 唯一 ID
    VoltageTier tier;                  // 电压等级
    MapPosition position;              // 2D 地图坐标
    int64_t generation_capacity;       // 发电能力（0 = 纯消费节点）
    int64_t max_input_voltage;         // 最大承受电压（默认 = tier 额定电压）
    int64_t power_demand;              // 当前电力需求
    bool is_transformer;               // 是否是变压器节点
    VoltageTier transformer_output_tier; // 变压器输出侧电压等级
    OverloadInfo overload_info;        // 过载状态
};
```

### PowerEdge（边 / 电线）
```cpp
struct PowerEdge {
    PowerNodeId node_a, node_b;        // 连接的两个节点
    CableProperties cable;             // 线缆材质属性
    int64_t max_capacity;              // 最大容量 = voltage × amperage
    int64_t distance_tiles;            // 曼哈顿距离
    int64_t power_loss;                // 该线路的固定损耗
    int64_t current_load;              // 当前负载（update_network 时计算）
    OverloadInfo overload_info;        // 过载状态
};
```

### CableProperties（线缆材质）
```cpp
struct CableProperties {
    const char* material_name;         // 材质名（如 "Copper"）
    VoltageTier max_voltage_tier;      // 最大电压等级
    int64_t amperage;                  // 基础安培值（1A, 2A, 4A, 8A, 16A）
    int64_t loss_per_tile;             // 每格传输损耗
};
```

### 电压等级 (VoltageTier)
16 级电压：ULV(8V) → LV(32V) → MV(128V) → HV(512V) → EV(2kV) → IV(8kV) → LuV(33kV) → ZPM(131kV) → UV(524kV) → UHV → UEV → UIV → UMV → UXV → MAX

### 线缆材质表（20 种）
| 材质 | 等级 | 安培 | 损耗/格 |
|------|------|------|---------|
| Tin | ULV | 1A | 1 |
| Red Alloy | ULV | 2A | 2 |
| Lead | LV | 1A | 2 |
| Copper | LV | 1A | 1 |
| Tin Alloy | LV | 2A | 1 |
| Annealed Copper | MV | 1A | 1 |
| Gold | MV | 2A | 2 |
| Silver | HV | 1A | 1 |
| Electrum | HV | 2A | 2 |
| Aluminium | EV | 2A | 2 |
| Platinum | EV | 4A | 1 |
| Tungsten | IV | 2A | 2 |
| Tungstensteel | IV | 4A | 1 |
| Graphene | LuV | 4A | 1 |
| HSS-G | LuV | 8A | 2 |
| Naquadah | ZPM | 4A | 1 |
| Naquadah Alloy | ZPM | 8A | 1 |
| Superconductor | UV | 8A | 0 |
| Superconductor HV | UV | 16A | 0 |

---

# 17. 过载机制详解

## 17.1 过压检测 (OVER_VOLTAGE)

**触发条件**：节点的 `max_input_voltage < 电网供电电压`

**检测逻辑**：
```cpp
if (node.power_demand > 0 && supplied_voltage > node.max_input_voltage) {
    node.overload_info.state = OverloadState::OVER_VOLTAGE;
}
```

**关键细节**：
- 只有 **有电力需求的节点**（机器）才会被检测过压。纯发电节点和电线杆不会被检测
- 电网电压取决于连通分量中**最高发电节点的电压等级**
- 变压器是解决过压的主要手段——它将电网隔离成不同电压的区域

**游戏表现**：
- 机器冒烟 → 爆炸 → 消失
- 火花粒子特效
- 爆炸可能波及相邻节点（连锁损坏）

## 17.2 过载检测 (OVER_CAPACITY)

**触发条件**：电线上的 `current_load > max_capacity`

**检测逻辑**（当前为近似算法）：
```cpp
// 当前负载 ≈ 连通分量的有效电力 ÷ 节点的边数
edge.current_load += effective_power / max(adjacency.size(), 1);
if (edge.current_load > edge.max_capacity) {
    edge.overload_info.state = OverloadState::OVER_CAPACITY;
}
```

**关键细节**：
- 每条边被两侧节点各累加一次，形成近似负载分配
- 不追踪每条边的"远端子网络"具体需求（Phase 2 将实现更精确的流分析）
- 损耗会减少有效电力，间接降低过载风险

**游戏表现**：
- 电线冒火花 → 烧毁 → 断开
- 输电中断导致下游区域停电
- 可能引发**连锁过载**——一条线烧断后，其他线路承载更多负载

## 17.3 过载回调

每次 `update_network()` 检测到过载时，通过回调通知 GDScript：
```cpp
overload_callback_(node_id, overload_info);  // 节点过载
overload_callback_(edge.node_a, edge.overload_info); // 边过载（通知两端节点）
```

GDScript 端可以：
- 播放特效动画
- 触发伤害/爆炸逻辑
- 在 UI 上显示警告
- 记录事件日志

---

# 18. 线缆损耗计算公式

## 18.1 基本公式

```
线缆容量 = 额定电压 × 安培值
  例: Copper (LV, 32V, 1A) → 容量 = 32

传输损耗 = 每格损耗 × 曼哈顿距离
  例: Copper 连接距离 5 格 → 损耗 = 1 × 5 = 5

有效供电 = 总发电 - 总损耗
```

## 18.2 距离计算

使用曼哈顿距离（不穿墙直线距离）：
```
dist = |x1 - x2| + |y1 - y2|
```

这和 Factorio 的线杆连接范围检查一致。

## 18.3 损耗影响

总体损耗是**从有效发电中减去**的：
```
effective_power = max(0, component_generation - component_loss)
```

当 `effective_power == 0` 时，电网实质上瘫痪——所有机器无法工作。这在超长距离输电且使用高损耗材质时更容易发生。

## 18.4 零损耗线缆

超导体（Superconductor, Superconductor HV）的 `loss_per_tile = 0`，适合远距离主干输电。代价是昂贵/后期才能获取。

---

# 19. 网络更新算法

## 19.1 更新时机

`update_network()` **不在每帧调用**，仅在以下事件后调用：
- 放置/拆除电线杆
- 连接/断开电线
- 机器功率需求变化（通过 set_power_demand 后显式调用）
- 发电机输出变化

**设计原理**：电网状态是"准静态"的——大部分时间不变，只在玩家操作时改变。省去逐帧扫描可支持超大规模工厂。

## 19.2 算法流程

```
update_network()
  │
  ├─ find_all_components()      [BFS, O(V+E)]
  │   └─ 每个未访问节点启动 BFS，发现整个连通分量
  │
  ├─ 重建 component_index_      [O(V)]
  │   └─ 节点 → 分量ID 的哈希映射（O(1) 同网查询）
  │
  ├─ reset_overloads()          [O(V+E)]
  │   └─ 清除所有过载标记和边负载
  │
  └─ for each component:
      └─ process_component()    [O(V_c + E_c)]
          ├─ 确定优势电压等级（最高发电节点 tier）
          ├─ 汇总发电和需求
          ├─ compute_component_loss() — 不重复统计边
          ├─ check_node_overload() — 过压检测
          └─ check_edge_overload() — 过载检测
```

**总复杂度**：O(V + E)，对于 10万节点+20万边的规模约 <10ms（单线程）。

## 19.3 已知局限

1. **边负载近似**：当前将电力平均分配到各边，而非沿网络拓扑精确求解。对于树状辐射状电网（最常见布局）误差可接受；对于复杂网状网，Phase 2 将实现基于子网络需求传播的精确算法。

2. **变压器未实现网络隔离**：`PowerNode.is_transformer` 字段已定义，但 `process_component()` 尚未使用变压器来拆分连通分量。变压器节点当前被当作普通节点处理。

3. **单线程**：所有计算在主线程进行。10万节点以下完全够用。

4. **UHV+ 线缆材质未定义**：目前线缆表只到 UV。更高等级的材质需要游戏 progression 走到后期再添加。

---

# 20. GDExtension 绑定计划

## 20.1 需要绑定的类

| C++ 类 | Godot 类名 | 用途 |
|--------|-----------|------|
| `PowerNetwork` | `GDPowerNetwork` | 电网核心，暴露给 GDScript |
| `PowerNode` | (RefCounted) | 节点数据，只读查询 |
| `PowerEdge` | (RefCounted) | 边数据，只读查询 |
| `CableProperties` | `GDCableProperties` | 线缆材质查询 |

## 20.2 绑定示例（计划）

```cpp
// GDPowerNetwork : public Resource
class GDPowerNetwork : public Resource {
    GDCLASS(GDPowerNetwork, Resource);

    PowerNetwork network_;  // 内部 C++ 实例

    // 暴露给 GDScript 的方法：
    int add_node(int tier, Vector2i position);
    void remove_node(int node_id);
    bool connect_nodes(int a, int b, String cable_material);
    void update_network();
    bool is_overloaded(int node_id);
    Dictionary get_overload_info(int node_id);
    // ...
};
```

## 20.3 信号

```
overload_detected(node_id: int, overload_type: int, actual: int, max: int)
```

在 GDScript 中：
```gdscript
power_network.overload_detected.connect(func(id, type, actual, max):
    if type == OVER_VOLTAGE:
        explode_machine(id)
    else:
        burn_cable(id)
)
```

---

# 21. 总结：Phase 1 完成状态

| 模块 | 状态 |
|------|------|
| 电压等级常量表 (16 tier) | 已完成 |
| 线缆材质表 (20种) | 已完成 |
| PowerNode / PowerEdge 数据结构 | 已完成 |
| PowerNetwork 图管理 | 已完成 |
| BFS 连通分量发现 | 已完成 |
| 过压检测 + 回调 | 已完成 |
| 过载检测 + 回调 | 已完成 |
| 容量计算 (V × A) | 已完成 |
| 距离损耗计算 | 已完成 |
| GDExtension 绑定 | 待实现 |
| 变压器网络隔离 | 待实现 |
| 精确子网络流分析 | 待实现 |
| UI 可视化 | 待实现 |

