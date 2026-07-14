# Godot 与 src 迁移及清理评估报告

审计日期：2026-07-14  
审计范围：旧 Godot/GDExtension 工程、`src/`、`scripts/`、相关资源和生成遗留物。

## 1. 结论

当前项目的**构建和运行时依赖已经脱离旧 Godot 树**：顶层 CMake 只构建 `snt_engine/` 与 `game/`，当前游戏包只复制 `game/`、`test_assets/` 和 `resource/terrain/`。旧 `src/`、GDScript、Godot 场景和 GDExtension 不参与当前主构建链。

但“构建脱钩”不等于“功能迁移完成”。如果目标是保留全部旧玩法，按功能域估算仍有约 **70-80%** 的迁移工作；这是功能覆盖估计，不是按代码行数推导的工期。若允许按新版产品范围裁剪，必须先为每个旧功能作出“保留、替换或废弃”的决定，之后才能得到可靠的剩余比例。

## 2. 审计口径

- 行数是物理行数，用于表示遗留规模，不等于有效代码量、复杂度或工期。
- `src/bindings/` 是 Godot GDExtension 包装层。它不应机械迁移；底层功能完成替换后应删除。
- 只有能由当前 CMake 目标、公开接口或迁移注释直接确认的能力才计为“已迁入”。新引擎中可能存在的重新实现不按旧行数重复计入。
- 旧 `tests/core/` 不进入当前 CTest 链。删除旧实现前，应将仍有价值的断言迁入 `snt_engine/tests/` 或 `game/tests/`。

## 3. 当前状态总览

| 范围 | 旧规模 | 当前状态 | 处理原则 |
| --- | ---: | --- | --- |
| `src/core/` | 279 文件，42,413 行 | 数据层的一部分已迁入；大量业务域尚未迁入 | 逐域迁移或明确废弃 |
| `src/bindings/` | 122 文件，19,553 行 | 旧 Godot 包装层 | 不迁移包装代码，替换底层功能后删除 |
| `src/server/` | 15 文件，1,941 行 | 当前只有 replication 契约 | P7.5 实现后删除旧服务端链 |
| GDScript 和示例 mod | 124 个 `.gd`，31,616 行 | 当前仅有脚本框架、炉子内容和基础 UI 原型 | 按玩法/UI 域重建，不保留 Godot 兼容层 |
| Godot 工程入口 | 配置、场景、shader、locale | 仍保留为迁移参考 | 替代方案验收后统一删除 |

旧 `src/` 受 Git 跟踪的文件共有 428 个，约 2.64 MiB；其中 C++ 代码共 420 个文件、63,938 行。`scripts/` 受 Git 跟踪的文件共有 252 个，约 1.30 MiB，其中包含 125 个无需迁移的 `.uid` 文件。

## 4. 已确认迁入的能力

以下三组旧 `src/core` 模块已明确完整迁入自研引擎，合计 5,208 行旧核心代码：

| 旧模块 | 旧物理行数 | 当前归属 |
| --- | ---: | --- |
| `src/core/world_gen/` | 2,574 | `snt_engine/data/world_gen/` |
| `src/core/save/` | 1,943 | `snt_engine/data/save/` |
| `src/core/mobile_structure/` | 691 | `snt_engine/data/mobile_structure/` |

此外，以下能力已处于新路径，但并非简单逐文件复制：

- `WorldData` 的数据职责已拆分为 `ChunkRegistry`、数据定义和 ECS 组件。
- greedy meshing 与碰撞面构造已从 `gd_chunk_helper.cpp` 移入 `snt_engine/voxel/`。
- 玩家体素碰撞、射线检测、运行时调度和渲染已在自研引擎链中实现。
- `Runtime + IGameSession`、游戏内容注册、AngelScript 热重载、基础背包/合成 UI 与 `MachineTickSystem` 已位于 `game/`。
- P7.2.1 已迁入炉子机器及 reload-safe 配方快照；这不代表 bloomery、pit kiln、charcoal pit、anvil 等旧机器已迁完。

## 5. 主要剩余 C++ 功能

下表按旧 `src/core/` 的物理行数列出主要未完成域。行数用于表现审计规模，不代表迁移工时；部分模块应先决定是否保留。

| 旧模块 | 行数 | 主要内容 | 当前判断 |
| --- | ---: | --- | --- |
| `simulation` | 9,715 | 生态、季节、作物、区块物理、tick、状态同步 | 需要按新 ECS/scheduler 重新划分所有权 |
| `universe` | 8,271 | Sector、跨区加载、星体 LOD、重力、跨区实体、可靠性 | 尚未形成等价的新产品路径 |
| `ae2` | 3,124 | 自动合成、ME 网络、存储单元 | 先决定是否进入新版范围，再设计数据边界 |
| `sfm` | 2,158 | 物流流图、执行器、容器接入、线缆图 | 旧编辑器/UI 不可直接复用 |
| `source_law` | 2,156 | 源律、器官、升华、药剂、技能 | 未完成玩法迁移 |
| `network` | 1,928 | 电力、流体、物流、信号网络 | 仅少量数据定义迁入，运行逻辑仍缺 |
| `multiblock` | 1,577 | 结构模板、运行态、控制器、机器闭包 | 未完成 |
| `player` | 1,419 | 玩家状态、装备、饥饿、背包 | 新引擎已有基础控制，但旧系统未等价迁入 |
| `magic` | 1,176 | 符文、仪式、法术、魔法书 | 未完成 |
| `quest` | 776 | 任务定义、条件、进度、序列化 | P7.3 待实现 |

`src/bindings/` 的 19,553 行包装代码不是单独的迁移目标。其清理前置条件是上表中的底层行为和新 UI/脚本边界已完成替换，而不是在新工程中重写一套 Godot API。

## 6. 主要剩余 Godot 内容

旧 GDScript 的最大功能域如下：

| 脚本域 | 行数 | 代表内容 |
| --- | ---: | --- |
| `scripts/world/` | 8,229 | 区块、机器管理、渲染桥接、世界交互 |
| `scripts/ui/` | 4,318 | 背包、机器面板、控制台、准星、创造模式 UI |
| `scripts/worldgen/` | 2,623 | 内建地形内容注册 |
| `scripts/nei/` | 1,951 | NEI 索引、搜索、面板与设置 |
| `scripts/player/` | 1,744 | 手部、玩家模型、交互 |
| `scripts/content/` | 1,743 | 内建内容、材料、物品和生物定义 |
| `scripts/mod/` | 1,635 | 模组加载、崩溃保护、入口与资源源 |
| `scripts/network/` | 856 | SteamP2P、LAN 会话与传输 |
| `scripts/source_law/` | 361 | 源律面板与战斗属性 |
| `scripts/quest/` | 329 | 任务数据库 |

`project.godot` 仍注册 9 个 autoload：`GameSession`、`IdentityManager`、`PlayerSaveService`、`KeyBindings`、`ItemDatabase`、`ContentDatabase`、`ModLoader`、`NEIIndex` 和 `NEISettings`。这些服务必须被新的 `game/`、AngelScript 或运行时服务替代，或在范围裁剪时明确废弃。

当前 AngelScript 内容只有一个 `game/scripts/p7_bootstrap.as`，其中仅注册炉子和一条炉子配方。基础背包/合成 UI 已存在，但不等价于旧 UI、NEI、模组和机器面板体系。

## 7. P7 缺口

P7 当前完成脚本框架、热重载、内容注册和炉子机器。以下阶段仍是明确的功能缺口：

| 阶段 | 剩余工作 | 验收结果 |
| --- | --- | --- |
| P7.3 | `QuestRegistry`、任务 API、运行时进度 | 可接取、推进、完成任务；reload 不丢进度 |
| P7.4 | 游戏玩法状态接入 region save | 重启后区块、机器和任务状态一致 |
| P7.5 | SteamP2P / 直连传输与 replication | 服务端权威联机；协议不匹配明确拒绝 |

当前 `snt_engine/network/replication.h` 仅声明 `IReplicationTransport` 与 `ReplicationService`，没有具体传输或 replication 实现。

## 8. 清理清单

| 对象 | 规模 | 清理条件 |
| --- | ---: | --- |
| `src/` | 428 跟踪文件，约 2.64 MiB | 对应功能已迁入或明确废弃；关键行为测试已迁移 |
| `scripts/` | 252 跟踪文件，约 1.30 MiB | 所有保留玩法已进入 `game/`；`.uid` 随脚本删除 |
| Godot 入口 | `project.godot`、`export_presets.cfg`、GDExtension、`icon.svg` | 新启动和打包路径已验证 |
| Godot 场景 | 6 个 `.tscn` | 新 UI/场景流已验收 |
| Godot shader | 8 个 `.gdshader` | 自研 Vulkan shader 或明确废弃方案已完成 |
| locale 与示例 mod | locale 6 文件、mod 3 文件 | 新国际化和模组策略已确定 |
| `tests/core/` | 23 文件，约 0.35 MiB | 仍有价值的断言已迁入当前 CTest 链 |
| `resource/terrain/` | 128 文件，约 0.23 MiB | 不能删除；当前 `game/CMakeLists.txt` 仍复制它 |
| 其他 `resource/` | 435 文件，约 32.5 MiB | 先分类为新包资产、源素材或废弃物，不能自动删除 |
| `bin/` | 约 509 MiB，未跟踪 | 停止运行旧 Godot 工程后可删除；含旧 DLL/PDB |
| `.godot/` | 约 45 MiB，未跟踪 | 停止使用 Godot 编辑器后可删除；仅缓存 |

`resource/` 中当前未进入游戏包的内容包括：`resource/_source/` 约 31.94 MiB、`resource/items/` 约 0.56 MiB、`resource/shaders/` 约 0.03 MiB 和其他少量源素材。它们不一定都是 Godot 遗留物，因此应先确定资产所有权再删除。

## 9. 推荐完成路径

1. 建立功能清单，为每个旧模块标记 `保留`、`替换` 或 `废弃`，并指定唯一的新归属。
2. 优先完成 P7.3、P7.4、P7.5，再按产品优先级迁移工业、源律、魔法、自动化和宇宙系统。
3. 将需要保留的旧测试转为 `snt_engine/tests/` 或 `game/tests/`，为每个替代的 Godot 工作流补充验收。
4. 完成资源分类，特别确认 terrain、items、locale、shader 与模组内容的运行包路径。
5. 在干净构建、CTest 和运行包验证均不再引用旧路径后，一次性删除旧源、Godot 入口、旧测试和生成缓存。

## 10. 方案取舍

| 路线 | 优点 | 缺点 |
| --- | --- | --- |
| 全功能等价迁移 | 最大化保留旧玩法与内容资产 | 工作量大，需要重建大量 Godot UI、GDExtension 边界和旧测试 |
| 按新版范围裁剪 | 能最快删除旧树，符合项目未正式发布且不维护兼容接口的原则 | 需要明确放弃哪些旧玩法，并将决定记录在产品设计中 |

建议采用“按新版范围裁剪”，但先完成逐域的保留、替换、废弃决策。这样可以避免为即将淘汰的 Godot 设计补兼容层，并为删除旧树提供可验证的完成标准。

## 11. 删除完成标准

旧 Godot 与 `src/` 可以删除的最低标准如下：

1. 所有保留功能都有新实现和自动化验收，或者被正式标记为废弃。
2. 顶层构建、运行包、测试和工具中不再引用 `src/`、`scripts/`、`res://`、`godot-cpp`、`.gdextension` 或 Godot 场景路径。
3. `cmake -S . -B <clean-build>`、游戏客户端构建和当前 CTest 在干净目录通过。
4. 新运行包可启动，并覆盖保留的存档、任务、机器、UI、资源和联机验收场景。
5. 删除跟踪文件后再次执行干净构建和测试；最后再清理未跟踪的 `bin/` 与 `.godot/`。

## 12. 依据

- [项目架构总览](项目架构总览.md)：旧 Godot/GDExtension 树仅作为迁移参考，当前主构建只使用 `snt_engine/` 与 `game/`。
- [自研引擎架构设计](自研引擎架构设计.md)：当前运行时边界、已迁入的数据层和未完成玩法状态。
- [P7 玩法迁移设计](p7_玩法迁移设计.md)：P7.3、P7.4、P7.5 的实现和验收目标。
- 顶层 `CMakeLists.txt`、`game/CMakeLists.txt`、旧 `project.godot`、Git 跟踪文件统计和目录物理行数统计。
