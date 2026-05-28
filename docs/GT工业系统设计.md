# GT 工业系统设计

## 1. 概述

GregTech（GT）风格的工业系统，涵盖从原始材料到超高电压自动化的完整科技树。设计是**纯 C++ 核心层**（位于 `src/core/` 命名空间 `science_and_theology::gt`），不依赖 Godot 引擎。

## 2. 电压等级系统

### 2.1 16 级电压（GT 标准）

| 等级 | 缩写 | 电压 (EU/t) | 累计 |
|------|------|-------------|------|
| Ultra Low Voltage | ULV | 8 | 8 |
| Low Voltage | LV | 32 | 40 |
| Medium Voltage | MV | 128 | 168 |
| High Voltage | HV | 512 | 680 |
| Extreme Voltage | EV | 2,048 | 2,728 |
| Insane Voltage | IV | 8,192 | 10,920 |
| Ludicrous Voltage | LuV | 32,768 | 43,688 |
| ZPM Voltage | ZPM | 131,072 | 174,760 |
| Ultimate Voltage | UV | 524,288 | 699,048 |
| Ultimate High Voltage | UHV | 2,097,152 | — |
| Ultimate Extreme Voltage | UEV | 8,388,608 | — |
| Ultimate Insane Voltage | UIV | 33,554,432 | — |
| Ultimate Mega Voltage | UMV | 134,217,728 | — |
| Ultimate X Voltage | UXV | 536,870,912 | — |
| MAX Voltage | MAX | 2,147,483,648 | — |

每级电压 = 前一级 × 4。

### 2.2 线缆属性

| 线缆材质 | 电压等级 | 最大安培 | 每格损耗 |
|----------|---------|---------|---------|
| Tin | ULV | 1 | 1 |
| Copper | LV | 1 | 1 |
| Gold | MV | 2 | 2 |
| Aluminium | HV | 2 | 2 |
| Tungsten | EV | 3 | 2 |
| Vanadium-Gallium | IV | 3 | 3 |
| Yttrium Barium Cuprate | LuV | 4 | 3 |
| Osmium | ZPM | 4 | 3 |
| Superconductor | UV | 5 | 4 |

> **未实现**：UHV+ 线缆材质表（代码中 `kCableMaterials` 表止于 UV）。代码注释 `"// UHV+ tier cable materials (TBD — add as progression demands)."`

## 3. 材料与物品系统

### 3.1 Material（112 种预定义材料）

`Material` 结构体包含：化学式、熔点、沸点、颜色、状态（SOLID/LIQUID/GAS/PLASMA）、生成标志（DUST/METAL/GEM/ORE/CELL/PLASMA/WIRE/BLOCK）。

材料分类：
- **基础金属**：铁、铜、锡、铅、金、银、铂、钨等
- **合金**：青铜、钢、不锈钢、钛合金等
- **宝石**：钻石、红宝石、蓝宝石、绿宝石等
- **稀土**：铱、锇、钌、铑等
- **等离子级**：中子素、反物质等

### 3.2 MaterialForm（32 种物品形态）

`enum MaterialForm`：DUST → INGOT → NUGGET → PLATE → ROD → SCREW → RING → GEAR → WIRE → CABLE → ... 共 32 种。

每形态对应特定数量：1 Ingot = 144mb（GT 标准）。

### 3.3 物品 ID 编码

`ItemId = kMaterialItemBase + material_id × 32 + form_id`。实现 O(1) 查询，112 × 32 = 3,584 个物品槽。

### 3.4 工具物品（非材料物品 ID 常量）

约 30 种 GT 工具和机器组件：
- **工具**：Hammer、Wrench、File、Screwdriver、Saw、Wire Cutter、Crowbar、Mallet、Hard Hammer
- **组件**：Motor、Piston、Robot Arm、Conveyor Module、Pump、Fluid Cell（按电压等级分）
- **电路**：Vacuum Tube → Primitive → Basic → Good → Advanced
- **编码 Pattern 范围**：`kEncodedPatternItemBase` 起

## 4. 机器系统

### 4.1 Machine 类

```cpp
struct MachineConfig {
    std::string name;         // "basic_steam_furnace"
    std::string type;         // "furnace"
    std::string recipe_map;   // 配方注册表键
    VoltageTier tier;
    int64_t voltage;          // 当前工作电压 (EU/t)
    int input_slots, output_slots;  // 物品格
    int64_t power_buffer;     // 内部能量缓冲
    int footprint_w, footprint_h;   // 占地图格
    std::vector<MachinePort> ports;       // 接口（I/O 方向）
    std::vector<ModuleSlot> module_slots; // 模块插槽
};
```

### 4.2 机器状态机

```
IDLE → [找配方] → NO_RECIPE（无匹配配方）/ NO_POWER（电力不足）
     → [try_start] → PROCESSING
                    → OUTPUT_FULL（输出空间不足）
     → [try_complete] → 输出物品、扣减输入
                      → 检查下一配方 → IDLE / NO_RECIPE
     → ERROR（模块错误等）
```

状态变更通过 `MachineSystem` 注入的回调函数 emit 到 `EventBus`。

### 4.3 超频计算 (Overclock)

```cpp
struct OverclockResult {
    int64_t eu_per_tick;     // EU *= 4^(tier_diff)
    int64_t duration;        // /= 2^(tier_diff)，下取整 ≥ 1
    int64_t total_energy;    // eu_per_tick × duration
    int tier_diff;           // 机器等级 - 配方等级
    bool viable;
};
```

### 4.4 模块系统（仅限多格机器）

模块只能安装在 **footprint > 1×1** 的多格机器上。单格机器（1×1）调用 `install_module()` 返回 false。

| 模块类型 | 字段 | 说明 |
|----------|------|------|
| ENERGY_INPUT | max_input_voltage | 能量仓（每级电压对应一种） |
| COIL | heat_capacity, efficiency_pct, parallel_bonus | 加热线圈 |
| MUFFLER | pollution_reduction_pct | 消音器（减少污染） |
| TRANSFORMER | max_step | 变压器（升压/降压） |

**互斥规则**：ENERGY_INPUT 和 TRANSFORMER 不能同时安装（都设置 `max_input_voltage`）。

模块效果通过 `recompute_from_modules()` 聚合：
- 线圈：取最高热容量、平均效率、累加并行奖励
- 消音器：累加污染减少百分比

### 4.5 机器端口

`MachinePort`：相对位置 + 类型（ENERGY/UNIVERSAL）+ 方向（INPUT/OUTPUT）+ 可锁定。I/O 方向可在运行时翻转。

## 5. 配方系统

### 5.1 Recipe 结构

```cpp
struct Recipe {
    std::string name, machine_type, category;
    VoltageTier min_tier;
    int64_t eu_per_tick;
    int64_t duration_ticks;
    std::vector<RecipeInput> inputs;      // ResourceStack 列表
    std::vector<RecipeOutput> outputs;    // 含概率
    bool has_chanced_outputs;
};
```

### 5.2 RecipeMap 与 RecipeDatabase

- `RecipeMap`：单个机器类型的配方集合，支持输入匹配
- `RecipeDatabase`：全局注册表，`unordered_map<机器类型名, RecipeMap>`
- 匹配使用 `ResourceKeyList::contains_enough()`（支持同种物品合并查询）

## 6. 手动合成系统

### 6.1 CraftingGrid（3×3 可配置）

`CraftingGrid`：可配置宽高（2×2 或 3×3），每个格子存 `ResourceStack`。

### 6.2 CraftingRecipe 类型

- **Shaped**：用 3×3 字符图案定义位置约束，扫描所有偏移找匹配
- **Shapeless**：只需物品类型和数量匹配，忽略位置
- **工具消耗**：部分配方需要 Hammer / File / Wrench 等工具（消耗耐久度）

### 6.3 已注册配方（50+ 种）

| 类别 | 示例配方 |
|------|----------|
| 材料压缩/解压 | Nugget ↔ Ingot, Ingot ↔ Block, Dust ↔ Block（17+ 种金属） |
| 工具合成 | Hammer, Wrench, Screwdriver, Crowbar, Saw, File, Wire Cutter, Soft/Hard Hammer |
| 零件加工 | Iron Rod, Plate, Screw（需要工具） |
| 线缆 | Wire（锭→2 线，10 种金属），Insulated Cable（线 + 橡胶片） |
| 电路 | Vacuum Tube → Primitive → Basic → Good 电路 |
| 机器组件 | Hull（基础/高级），LV Motor, Piston, Robot Arm, Conveyor, Pump, Fluid Cell |
| 杂项 | Stone Plate, Wood Planks, Sticks, Coal Block, Firebrick |

## 7. 电力网络

### 7.1 图模型（Factorio 电线杆式）

`PowerNetwork`：邻接表图，`list<PowerEdge>` 保证迭代器稳定性。

```cpp
struct PowerNode {
    PowerNodeId id;
    VoltageTier tier;
    MapPosition position;
    int64_t generation_capacity;   // 发电机输出
    int64_t max_input_voltage;     // 机器最大承受电压
    int64_t power_demand;          // 当前电力需求
    // 变压器字段
    bool is_transformer;
    int max_step;                  // 最大升/降压级数
    VoltageTier transformer_output_tier;
    OverloadInfo overload_info;
};

struct PowerEdge {
    PowerNodeId node_a, node_b;
    CableProperties cable;         // 材质/电压/安培/损耗
    int64_t max_capacity;          // 总容量 = 电压 × 安培
    int distance_tiles;
    int64_t power_loss;
    int64_t current_load;          // 当前实际负载
    OverloadInfo overload_info;
};
```

### 7.2 五阶段网络更新

```
1. BFS 连通分量发现 → 分量索引缓存
2. 分量统计：总发电、总需求、总损耗、最低电压等级
3. 变压器电力传输：过剩电力按降级传递（max_step 校验）
4. BFS 树边缘流量计算：从发电机 root 出 BFS 森林，需求反向传播
5. 过载检测：
   - 节点 OVER_VOLTAGE：max_input_voltage < 实际电压
   - 边 OVER_CAPACITY：current_load > max_capacity
```

### 7.3 过载检测

`OverloadCallback` 系统在过载发生时触发回调。`GDTickSystem` 将 `power_overload` 事件桥接到 Godot Signal。

### 7.4 已知限制

1. **边负载近似**：当前均分到所有边。网格拓扑在 Phase 2 需要精确子网络需求传播。
2. **变压器未实现网络隔离**：`is_transformer` 字段已定义但 `process_component()` 未使用。变压器节点当前被当作普通节点处理。
3. **单线程**：所有计算在主线程。适用于 <10 万节点。
4. **UHV+ 线缆材质未定义**（见 2.2 节注释）。

## 8. 流体网络

### 8.1 模型

```cpp
struct FluidNode {
    uint32_t id;
    FluidId fluid_type;           // 单流体类型（互斥）
    PipeType pipe_type;           // LIQUID / GAS
    int64_t production, demand;
    int64_t buffer, delivered;
};
```

**规则**：
- 每个连通分量内只允许一种流体类型
- 液体管道拒绝气体，气体管道拒绝液体
- 推式分配：产出按比例分给所有消费者（余数给最后一个消费者）

## 9. 物品管道网络

### 9.1 共享缓冲区模型

每个连通分量维护一个 `unordered_map<ItemId, int64_t>` 作为共享物品缓冲区——所有物品对所有节点立即可用。

```cpp
struct ItemPipeNode {
    uint32_t id;
    bool is_source, is_sink;
};
```

**限制**：每个分量最多 64 种不同物品类型。

**合并/拆分**：`update_network()` 在分量合并或拆分时正确迁移/合并缓冲区。

> **未实现**（代码注释 `"// Phase 2 may add throughput limits, per-tick movement, and routing."`）

### 9.2 管道类型

| 类型 | 默认吞吐量 (items/tick) |
|------|------------------------|
| LIQUID | 1000 |
| GAS | 800 |
| ITEM | 64 |

## 10. 流体注册表（30+ 种）

| 类别 | 流体 |
|------|------|
| 基础 | Water, Lava, Steam |
| 石油加工 | Oil, Heavy Oil, Light Oil |
| 燃料 | Diesel, Rocket Fuel, Ethanol |
| 酸类 | Sulfuric, Hydrochloric, Nitric, Hydrofluoric, Aqua Regia |
| 工业化学品 | Ammonia, Methanol, Acetic Acid, Chlorine |
| 气体 | O2, H2, N2, Natural Gas, Helium, Neon, Argon, Krypton, Xenon |
| 熔融金属 | Molten Iron, Gold, Copper, Tin, Lead |

## 11. 多格单方块 (替代原多方块系统)

> 原设计文档中的多方块系统已被替换。当前方案：单个 Machine 可占据多格（`footprint_width` × `footprint_height`），通过模块系统（能量仓、线圈、消音器）定制其内部功能，而非拼装多个独立方块。

## 12. 三网分离架构 [未实现设计]

> 以下内容来自原设计文档。

三种独立网络并存于同一坐标空间：

| 网络 | 功能 | 传输介质 |
|------|------|----------|
| **电力网络** (Power Grid) | 能量传输 | 线缆连接电线杆 |
| **物流网络** (Logistics Grid) | 物品/流体传输 | 管道 + 物流容器 |
| **数据网络** (Data Grid) | AE2 智能物流 | 数据节点 + 数据中继塔 |

数据网络使用"数据节点 + 数据中继塔"覆盖区域（非逐方块线缆）。

## 13. 开发阶段

| 阶段 | 内容 | 状态 |
|------|------|------|
| 1 | 电压等级、线缆材质、PowerNode/PowerEdge | ✅ 完成 |
| 2 | PowerNetwork 图算法（BFS + 流量 + 过载） | ✅ 完成 |
| 3 | FluidNetwork + ItemPipeNetwork | ✅ 完成 |
| 4 | Machine + Recipe + Module + ProcessingLogic | ✅ 完成 |
| 5 | CraftingGrid + CraftingManager（50+ 配方） | ✅ 完成 |
| 6 | Material × Form + ItemRegistry + MaterialRegistry | ✅ 完成 |
| 7 | GDExtension 绑定（GDPowerNetwork 等） | ✅ 完成 |
| 8 | 变压器网络隔离 | 🔲 待实现 |
| 9 | 精确子网络流量计算 | 🔲 待实现 |
| 10 | 三网分离 + 数据中继塔 | 🔲 待实现 |
| 11 | Godot UI（能源统计面板、线缆铺设工具） | 🔲 待实现 |
