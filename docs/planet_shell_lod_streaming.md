# 大半径星球壳层流式加载与水平 LOD 设计

本文档记录 ScienceAndTheology 的大半径体素星球加载策略。目标是支持 `planet_radius = 131072` 乃至 `524288` 级别的真实球形星球，同时避免完整三维球体加载导致内存、mesh、碰撞和网络同步爆炸。

## 1. 背景

当前项目使用 32 格 chunk：

```text
CHUNK_SIZE = 32
```

当 `loaded_radius = 32` 时，不能按完整三维球体加载：

```text
完整球体 chunk 数 ≈ 4/3 × π × 32^3 ≈ 137,258
```

即使只按玩家水平半径取圆盘，再加载固定 Y 范围，如果垂直层数约为 17：

```text
水平 column ≈ π × 32^2 ≈ 3,217
普通柱体 chunk 数 ≈ 3,217 × 17 ≈ 54,689
```

这个数量级对 Godot 场景节点、ArrayMesh、collision shape、chunk data、tick system 和网络 dirty chunk 都仍然过大。

最终设计必须从“完整球体/普通柱体”转为：

```text
局部切平面盘
+ 径向高度带
+ 球壳裁剪
+ 地表 column 索引
+ 深层地下按需生成
+ 水平 LOD
```

## 2. 坐标语义

大星球不能长期使用全局 `XZ` 表示水平、全局 `Y` 表示高度。玩家绕星球移动后，真正的水平面应是玩家所在地表点的切平面。

核心向量：

```text
up = normalize(player_position - planet_center)
surface_point = planet_center + up × planet_radius
relative = chunk_center - surface_point
radial_height = dot(relative, up)
tangent_offset = relative - up × radial_height
horizontal_distance = length(tangent_offset)
```

主动流式加载的基础条件：

```text
horizontal_distance <= loaded_radius_blocks
-H_below <= radial_height <= H_above
```

因此加载形状不是全局 `XZ 盘 + Y 范围`，而是：

```text
局部切向圆盘 + 径向高度范围
```

## 3. 壳层裁剪

普通柱体仍然会把大量地下层全部加载。大星球应优先加载地表附近的活动壳层。

壳层判断基于球心距离：

```text
r = distance(chunk_center, planet_center)
shell_min = planet_radius - H_below
shell_max = planet_radius + H_above
```

只有与 `[shell_min, shell_max]` 相交的 chunk 才属于主动加载壳层。

推荐初始参数：

```text
H_above = 128 blocks   # 建筑、树、山体和近地表飞行
H_below = 256 blocks   # 浅层挖掘、洞穴入口、浅层矿脉
```

注意：

```text
H_below 是实时主动加载深度，不是星球总可挖掘深度。
```

对于 `planet_radius = 131072`，玩家仍然可以理论上一路挖向核心，但深层地下 chunk 不能随玩家地表移动自动全量加载。

## 4. 深层地下按需生成

地下分三类：

```text
SurfaceStreamingChunk
    地表附近，随玩家移动自动加载。

ShallowUndergroundChunk
    地表以下 H_below 内，低优先级预加载。

DeepUndergroundChunk
    超出 H_below，默认不生成、不加载、不 meshing、不 collision、不 tick。
    只有玩家挖到、矿井结构触达、洞穴连通、扫描器请求、传送/电梯到达时才生成。
```

深层地下的生成触发源：

```text
1. 玩家挖掘到壳层边界以下。
2. 已加载洞穴系统连通到更深 chunk。
3. 地下结构、矿井、电梯、传送门引用该 chunk。
4. 机器网络、管线、物流结构要求该 chunk 保持活动。
5. 存档中已有玩家修改或机器状态。
```

## 5. 地表 Column 索引

流式加载不应该枚举完整三维半径内的所有 chunk 再逐个裁剪。应先维护轻量 column 索引：

```text
SurfaceColumnIndex
```

每个 column 至少记录：

```text
surface_height
surface_chunk_radial_layer
highest_build_layer
lowest_open_layer
has_player_modified
has_cave_entrance
has_active_machine
has_structure
has_deep_access
```

加载流程：

```text
1. 计算玩家局部切平面盘内的 column。
2. 对每个 column 查询 surface layer。
3. 按 H_above/H_below 和 column 标记决定加载哪些 chunk。
4. 深层 chunk 只有被引用或触达才进入加载队列。
```

## 6. 水平 LOD 分层

`loaded_radius = 32` 不应表示 32 chunk 内都是真实可交互 chunk。它应表示 32 chunk 内有可见地形，但真实数据、mesh、collision、tick 分层处理。

推荐分层：

```text
LOD0: 0 ~ 4 chunk
    完整 chunk data
    完整 greedy mesh
    collision enabled
    block interaction enabled
    machine/entity tick enabled

LOD1: 4 ~ 8 chunk
    完整或浅层 chunk data
    greedy mesh / simplified mesh
    collision disabled or reduced
    only important tick

LOD2: 8 ~ 16 chunk
    surface shell only
    surface-only mesh / heightfield mesh
    no collision
    no tick

LOD3: 16 ~ 32 chunk
    heightfield / terrain clipmap
    no real chunk data unless already modified
    no collision
    no tick

LOD4: 32 chunk+
    PlanetLodManager proxy sphere / low-poly sphere / billboard
```

这与当前项目中的 PlanetLodManager 思路一致：真实 chunk、简化 mesh、proxy sphere、low-poly sphere、billboard 应形成连续 LOD 链。

## 7. 数据状态拆分

不要把“加载”视为单一状态。chunk 至少应拆成：

```text
Generated
    有 chunk 数据，或可由 seed 确定生成。

Meshed
    有可见 mesh。

Collidable
    有碰撞体。

Ticking
    机器、实体、植物、流体等会更新。
```

不同区域对应不同状态：

| 区域 | Generated | Meshed | Collidable | Ticking |
|---|---:|---:|---:|---:|
| 0~4 chunk | 是 | 完整 | 是 | 是 |
| 4~8 chunk | 是 | 简化 | 少量/否 | 少量 |
| 8~16 chunk | surface only | surface mesh | 否 | 否 |
| 16~32 chunk | heightfield/LOD | terrain LOD | 否 | 否 |
| 深层未触达 | 否 | 否 | 否 | 否 |
| 深层已触达 | 是 | 按需 | 按需 | 按需 |

## 8. 内存目标

以 `loaded_radius = 32` 为例：

```text
完整球体：约 137k chunk，不可行。
普通柱体：约 55k chunk，仍然过大。
壳层 surface column：约 3.2k column，可作为可见范围目标。
```

但即使 3.2k column，也不能全部是完整 chunk + collision。正确目标是：

```text
真实可交互 chunk：几百以内。
可见壳层/LOD chunk：几千以内。
带 collision chunk：只保留玩家附近。
带 tick chunk：只保留机器/实体活动范围。
```

## 9. 与星球半径模型的关系

`planet_radius` 表示中心到平均地表的真实拓扑半径，决定：

```text
地表曲率
绕行周长
中心到地表距离
球形地形裁剪
```

地表到太空不应由 `planet_radius` 推导，而应使用：

```text
space_start_altitude
```

引力范围不应使用 `planet_radius * 4`，而应使用：

```text
gravity_radius = planet_radius + gravity_influence_altitude
```

这使得 `planet_radius = 131072` 的星球仍然可以在地表上方数千到数万格进入太空，而不是必须飞出另一个星球半径。

## 10. 实现优先级

建议后续分阶段实现：

```text
Phase 1:
    PlanetDescriptor 拆出 atmosphere_height、space_start_altitude、gravity_influence_altitude、active_shell_above、active_shell_below。

Phase 2:
    PlanetShellChunkRendererBridge/GDPlanetShellHelper 承担局部切平面盘与壳层候选枚举；
    GDChunkHelper 不再承载 chunk 可见性/加载选择。

Phase 3:
    加入壳层裁剪，主动加载只覆盖地表附近径向高度带。

Phase 4:
    加入 SurfaceColumnIndex，减少三维枚举。

Phase 5:
    加入 horizontal LOD：LOD0 完整交互，LOD1 简化，LOD2 surface shell，LOD3 heightfield/clipmap。

Phase 6:
    DeepUndergroundChunk 按需生成，并把玩家修改、机器、结构引用作为保活条件。
```
