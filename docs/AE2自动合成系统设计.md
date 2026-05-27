# AE2 自动合成系统设计

## 1. 概述

参照 Applied Energistics 2 的自动合成架构，在 C++ 核心层实现了完整的四层体系：Pattern 定义 → 计划计算 → 请求解析 → CPU 调度执行。

命名空间 `science_and_theology::gt`，位于 `src/core/ae2/`（20 个文件）。

## 2. Pattern 系统

### 2.1 Pattern 类型层次

```
AEPattern（抽象基类）
├── AECraftingPattern   # 包装 CraftingRecipe（手动合成台配方）
├── AEProcessingPattern  # 包装 Recipe（机器处理配方）
└── SimplePattern        # 内联数据（外部 Provider 动态提供）
```

每种 Pattern 提供：`get_inputs()`、`get_outputs()`、`get_primary_output()`。

### 2.2 AECraftingPattern

将 `CraftingRecipe` 的 3×3 字符图案去重压缩为输入列表。排除纯工具格（如 Hammer 不消耗）。

### 2.3 AEProcessingPattern

包装机器 `Recipe`，保留 `eu_per_tick` 和 `duration` 用于 CPU 计算处理时间。

### 2.4 PatternRegistry（全局注册表）

维护三个索引：
- **owning_patterns**：系统内置 Pattern（从 CraftingRecipe / Recipe 注册的）
- **provider_patterns**：外部 Provider 动态添加的 Pattern（如 ME Interface）
- **emitable_items**：标记为"可直接产出"（无需合成）的物品

接口：
```cpp
static bool is_craftable(ItemId id);
static const std::vector<const AEPattern*>& get_patterns_for(ItemId id);
static void add_provider_pattern(const AEPattern* pattern, uint64_t provider_id);
static void remove_provider_patterns(uint64_t provider_id);
```

### 2.5 PatternProvider 接口 [部分实现]

```cpp
class PatternProvider {
    virtual std::vector<const AEPattern*> get_provided_patterns() = 0;
    virtual bool is_valid() = 0;
};

class PatternProviderHost {
    void add_provider(PatternProvider* provider);
    void remove_provider(PatternProvider* provider);
    void sync_to_registry();   // 将 Provider 的 Pattern 同步到 PatternRegistry
};
```

`PatternProviderHost::sync_to_registry()` 调用 `PatternRegistry::add_provider_pattern()`。

## 3. 合成计算 (Crafting Calculation)

### 3.1 CraftingPlan

```cpp
struct CraftingPlan {
    ResourceStack final_output;   // 最终产物
    int64_t bytes;                // 所需存储字节数
    bool simulation;              // 是否为模拟模式
    std::unordered_map<ItemId, int64_t> used_items;     // 消耗的物品
    std::unordered_map<ItemId, int64_t> emitted_items;  // 直接产出的物品
    std::unordered_map<ItemId, double> pattern_times;   // 每个 Pattern 的处理时间
};
```

### 3.2 CraftingCalculation（核心算法）

**流程**：
1. 尝试请求 `requested_amount`
2. 若失败 → **二分搜索**最大可行量（CRAFT_LESS 模式）
3. 若二分搜索也失败 → 使用**模拟模式**（假想物品，用于预览）

```
request(target_amount)
 ├── 成功 → 返回 CraftingPlan
 ├── 失败 → binary_search(0, target_amount)
 │   └── 找到最大可行量 → 返回
 └── 全部失败 → simulated_fallback() → 返回模拟 Plan
```

### 3.3 CraftingTreeNode（递归合成树节点）

```cpp
struct CraftingTreeNode {
    ItemId item_id;
    int64_t requested_amount;
    // 解析顺序：Extract → Emit → Craft
    bool try_extract(CraftingSimulationState& state);
    bool try_emit(CraftingSimulationState& state);
    bool try_craft(CraftingContext& ctx);

    // 递归检测
    bool is_ancestor(ItemId id);  // 防循环合成
};
```

### 3.4 CraftingSimulationState

Copy-on-Write 库存快照，支持：
- `extract(ItemId, amount)` — 从模拟库存取物品
- `insert(ItemId, amount)` — 放入模拟结果
- `diff(src)` / `merge(src)` — 计算/合并差异

## 4. 请求解析链 (Crafting Resolver)

### 4.1 优先级体系（4 级）

```
CraftingRequest → 优先级队列
  ├── 1. ExtractItemTask      # 从 ME 网络提取（最高优先）
  ├── 2. EmitItemTask         # 标记为"可直接产出"
  ├── 3. CraftFromPatternTask # 通过 Pattern 合成
  └── 4. ConjureItemTask      # 模拟创建（回退模式）
```

### 4.2 ExtractItemTask

- 尝试从 `CraftingSimulationState` 提取物品
- 失败时支持 `partial_refund`（部分退还未消耗的子物品）

### 4.3 EmitItemTask

标记物品为"emitted"——不需要合成，直接从某处获取（如农场、蜂箱等可重生资源）。

### 4.4 CraftFromPatternTask（两阶段）

```
Phase 1: 为每个输入物品创建子 CraftingRequest
Phase 2: 计算最多可完成多少个合成（取所有输入的 min）
         → 推进仿真的 craft_depth
```

### 4.5 ConjureItemTask

回退模式：创建假想物品（用于预览"如果我有这个原料，能合多少"）。

## 5. CraftingCPU（任务调度）

### 5.1 配置

```cpp
struct CraftingCPU {
    int64_t storage_bytes() const;
    int64_t available_storage() const;
    int co_processors() const;
    bool is_busy() const;

    void configure(int64_t storage_bytes, int co_processors);
    void cancel_job();
    int64_t insert_item(ItemId id, int64_t amount);  // 外部机器完成后的产物
};
```

### 5.2 调度回调

两个执行器回调（从 GDScript 注入）：
- `craft_executor(CPU*, Pattern*, count) → bool`：派发合成 Pattern 给 Molecular Assembler
- `process_executor(CPU*, Pattern*, count) → bool`：派发处理 Pattern 给外部机器

### 5.3 协处理器节流

`ExecutingCraftingJob` 使用滚动操作历史限制调度速率：
`max_ops_per_tick = 1 + co_processors`，在 `rolling_ops_history` 中滑动窗口保证不超限。

### 5.4 任务生命周期

```
submit → compute_plan → find_best_cpu → extract_items → submit_job
         → tick: dispatch_waiting → check_readiness → dispatch
         → all_done → deliver_final_output → cpu_done
```

## 6. ME 网络 (MENetwork)

### 6.1 拓扑模型

```cpp
struct MENode {
    MENodeType type;        // CONTROLLER, CABLE, INTERFACE, STORAGE_BUS
    uint32_t component_id;  // 所属连通分量
};

struct MEEdge {
    uint32_t node_a, node_b;
};
```

**连通分量管理**：BFS 重建分量索引。每个分量内的所有 `IStorage` 对象被池化统一查询。

### 6.2 IStorage 接口

```cpp
class IStorage {
    virtual int64_t available(const ResourceId& rid) const = 0;
    virtual int64_t insert(const ResourceId& rid, int64_t amount) = 0;
    virtual int64_t extract(const ResourceId& rid, int64_t amount) = 0;
    virtual std::vector<ResourceId> stored_types() const = 0;
    virtual int64_t total_bytes() const = 0;
    virtual int64_t used_bytes() const = 0;
};
```

### 6.3 StorageCell（数字存储单元）

```cpp
class StorageCell : public IStorage {
    int64_t byte_capacity;     // 总字节容量
    int64_t bytes_per_type;    // 每种物品的固定开销
    int max_types;             // 最多存储种类数
    // 内部：unordered_map<ResourceId, int64_t>
};
```

类型限制插入：新类型需要 `bytes_per_type` 开销，总字节数不能超容量。

### 6.4 ExternalStorage

通过回调桥接外部库存（GDScript 端实体存储）：
```cpp
ExternalStorage::CheckCallback   // 查询库存量
ExternalStorage::ExtractCallback // 提取
ExternalStorage::InsertCallback  // 插入
```

### 6.5 全局与分区查询

- `check(key, context_node)` — 先查 context_node 所在分量，无结果则查全局
- `extract(key, amount, context_node)` — 同上
- `check_global(key)` — 跳过分区，直接全局查询

## 7. Pattern 编码与缓存

### 7.1 PatternDataCache

`PatternDataCache`（`ae2_pattern_cache.hpp`）用于将配方的输入/输出映射为编码 Pattern 物品 ID。

```cpp
static ItemId register_pattern(
    const std::vector<ResourceStack>& inputs,
    const std::vector<ResourceStack>& outputs,
    bool is_crafting,
    const std::string& name);
```

通过内容哈希去重，从 `kEncodedPatternItemBase` 起分配动态 ItemId。

### 7.2 编码 Pattern 物品

- `encode_crafting_pattern(recipe_name)` → 包装 `CraftingRecipe` → 注册到 `PatternDataCache` → 返回 `ItemId`
- `encode_processing_pattern(machine_type, recipe_name)` → 包装机器 `Recipe` → 同上
- `add_encoded_pattern(provider_id, encoded_item_id)` → 从 Cache 解码 → 注册为 Provider Pattern

## 8. CraftingService（顶层协调器）

全局唯一的 `CraftingService` 静态实例，统一管理：

1. **Pattern 注册**：内置 Pattern + Provider Pattern + Emitable 标记
2. **CPU 注册**：`add_cpu()` / `remove_cpu()`
3. **任务提交**：`submit_job()` → 计算 Plan → 选最佳 CPU → 提取物品 → 提交任务
4. **网络访问**：通过回调链 `set_network_check/extract/insert_callback()` 连接 ME 网络
5. **每 tick**：`tick()` → 调用所有 CPU 的 `tick()`

### 8.1 CPU 选择算法

```cpp
CraftingCPU* find_best_cpu(const CraftingPlan& plan) {
    // 筛选：available_storage >= plan.bytes
    // 排序：co_processors 降序（优先用协处理器最多的 CPU）
    //       次排序：available_storage 升序（优先用较小的 CPU）
    // 返回最佳匹配
}
```

## 9. 通道系统 [未实现]

> 以下内容来自原设计文档。

AE2 通道系统限制 ME 网络最多 8 个设备/节点（不计算中继器）。功能规划：
- 通道类型（正常/密集）：密集线缆携带 32 通道
- 通道数量管理：每个设备消耗 1 通道
- 优先级：设备级通道优先级（关键设备优先）
- 阻塞检测：无可用通道时的错误处理
- 通道优化：最短路径分配、自动重新分配

## 10. AE2 终端 UI [未实现]

> 以下功能来自原设计文档。

计划中的 Godot UI 界面：

| 终端类型 | 功能 |
|----------|------|
| ME 终端 | 查看全网物品、提取/存入 |
| Pattern 终端 | 编码 Pattern、管理配方 |
| 流体终端 | 查看/管理 ME 流体网络 |
| 合成终端 | 快捷合成（带合成台功能） |
| 接口终端 | 管理 Pattern Provider |

所有终端通过 `GDMENetwork` 的 `check_item`/`extract_item`/`insert_item` 方法操作 ME 网络。

## 11. GDExtension 绑定接口

### 11.1 GDAutocraftingCPU

```gdscript
var cpu = GDAutocraftingCPU.new()
cpu.configure(65536, 4)  # 64K 存储, 4 协处理器
cpu.set_craft_executor(_on_craft_request)
cpu.set_process_executor(_on_process_request)
cpu.insert_item(item_id, amount)  # 外部机器完成产物
```

### 11.2 GDAutocraftingService（静态方法）

```gdscript
GDAutocraftingService.initialize()
GDAutocraftingService.add_cpu(cpu)
GDAutocraftingService.set_network_check_callback(_check_inventory)
GDAutocraftingService.set_network_extract_callback(_extract_from_inventory)
GDAutocraftingService.set_network_insert_callback(_insert_to_inventory)
var result = GDAutocraftingService.submit_job(item_id, 64)
var encoded = GDAutocraftingService.encode_processing_pattern("furnace", "iron_ingot")
GDAutocraftingService.add_encoded_pattern(provider_id, encoded.item_id)
```

### 11.3 GDMENetwork

```gdscript
var node_a = me_net.add_node(0)  # CABLE
var node_b = me_net.add_node(1)  # INTERFACE
me_net.connect(node_a, node_b)
me_net.attach_storage_cell(node_b, {"byte_capacity": 65536, "max_types": 63})
var count = me_net.check_item(item_id, context_node)
var extracted = me_net.extract_item(item_id, 64, context_node)
```

## 12. 开发阶段

| 阶段 | 内容 | 状态 |
|------|------|------|
| 1 | AEPattern 三种类型、PatternRegistry、PatternProvider | ✅ 完成 |
| 2 | CraftingCalculation（二分搜索 + 模拟回退） | ✅ 完成 |
| 3 | CraftingResolver 四级优先级链 | ✅ 完成 |
| 4 | CraftingCPU 调度 + 协处理器节流 | ✅ 完成 |
| 5 | MENetwork（拓扑管理 + IStorage 接口） | ✅ 完成 |
| 6 | StorageCell, ExternalStorage, PatternDataCache | ✅ 完成 |
| 7 | CraftingService 全局协调 + GDExtension 绑定 | ✅ 完成 |
| 8 | 通道系统（正常/密集/优先级/阻塞） | 🔲 待实现 |
| 9 | 终端 UI（ME 终端、Pattern 终端、流体终端） | 🔲 待实现 |
| 10 | 与 GT 电力/物流网络整合（三网分离） | 🔲 待实现 |
