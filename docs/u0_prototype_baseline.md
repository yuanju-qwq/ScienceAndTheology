# U0 原型基线

## 1. 基线范围

本基线用于后续统一宇宙架构迁移的回归比较，不代表发布性能目标。

覆盖内容：

- C++ `WorldData`、负坐标 chunk、voxel、block entity 与 3D region 存读档。
- GDScript 内容数据库、物品键和熔炉权威命令链。
- 真实 `WorldMap` 的星球生成、方块写读、存读档、LOD 与迁移期调试传送。
- 默认加载半径下的 FPS、内存、节点、chunk 队列和 mesh 构建耗时。
- Godot headless 项目解析与启动。

统一入口（PowerShell 7）：

```powershell
pwsh -NoLogo -File scripts/tools/Run-U0Checks.ps1 -CaptureBaseline
```

原始采样写入忽略构建目录：

```text
build/u0-prototype-baseline.json
```

## 2. 采集环境

```text
日期：2026-06-19
平台：Windows x86_64
Godot：4.6.3 stable，headless/dummy renderer
GDExtension：Debug
宇宙模式：solar_system
固定种子：20260619
采样时长：15 秒
采样间隔：5 秒
WorldMap 加载参数：项目默认值
```

headless/dummy renderer 的 FPS 与图形客户端不可直接横向比较；后续应使用相同命令、构建类型和机器做趋势比较。

## 3. 首份结果

| 时间 | FPS | 静态内存 MiB | 节点数 | 可见 chunk | 排队 view | tracked chunk | 异步生成 | mesh 数 | mesh 平均 ms | mesh 最大 ms |
|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 5.11s | 20 | 133.50 | 9,579 | 2 | 410 | 477 | 133 | 239 | 16.01 | 36.88 |
| 10.12s | 16 | 144.35 | 11,070 | 2 | 1,157 | 901 | 256 | 451 | 18.06 | 55.26 |
| 15.04s | 17 | 156.76 | 12,792 | 2 | 1,281 | 1,241 | 159 | 621 | 19.60 | 69.90 |

半径 1 场景冒烟的存档结果：

```text
chunk 数：11
save_dimension：28.73 ms
load_dimension：11.81 ms
```

这些数字是首份比较点，不设硬阈值。当前 15 秒内 tracked chunk、排队 view、节点数与内存仍在上升，U1/U2 改造后必须继续观察是否进入稳定平台。

## 4. 已知基线警告

以下内容警告不阻断 U0，但后续内容清理应单独处理：

- `mill_wheat_to_flour`、`spin_cotton_to_fiber`、`weave_fiber_to_cloth` 因物品键缺失被跳过。
- `bake_flour_to_bread` 的 `flour` 输入键未注册。
- 部分矿物、围栏、作物种子与产物尚无可解析 item key。

runner 会把脚本解析失败、shader 编译失败、字符串格式错误和测试断言失败视为阻断；普通内容校验 warning 会保留在日志中。

## 5. U0 修复记录

- 为 CMake 接入 `ctest`，增加含中文路径的 region 存读档回归。
- Windows 存档路径统一按 UTF-8 转换为 `std::filesystem::path`。
- 修正星球 LOD 使用球心距离而非地表高度的问题。
- 修正 billboard shader 的无效 render mode 与 fragment 早退。
- 恢复 `PlayerController` 被压缩破坏的字段/方法命名，使场景可解析。
- `UniverseManager` 接通 `GameSession.save_path`。
- `/travel` 与连接器空间站标记为迁移期调试/兼容入口。
- chunk 与存档指标采用默认关闭的低频采样，不增加每 tick 日志。
