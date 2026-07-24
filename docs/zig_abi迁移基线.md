# Zig ABI 迁移基线

> 状态：v1 通用宿主核心已启用，2026-07-23。它定义自研引擎和确定性游戏逻辑向 Zig 的渐进迁移路径：新增自研逻辑默认使用 Zig，既有 C++ 模块在稳定 C ABI 合同准备完成后逐个替换；C++ 保留为旧模块维护和第三方 C++ 生态的薄适配层。

## 当前范围

`snt_engine/abi/` 是唯一允许 Zig、C 或其他原生宿主直接包含的引擎接口根。v1 提供：

- `abi_common.h`：定宽状态码、字节 view 与低频日志 callback 类型；
- `runtime_abi.h`：运行时 ABI 版本/能力查询；
- `runtime_host_abi.h`：由 Zig 通用宿主核心实现的 value-only 宿主创建、确定性 tick、命令、snapshot lease 与关闭合同。
- `runtime_key_index_abi.h`：由 Zig 实现的确定性 StringKey -> runtime ID 索引、不可变 snapshot 与显式 retain/release 合同。
- `uuid_abi.h`：由 Zig 实现的 value-owned UUID seed mixer、generator state 与打包合同；宿主只提供初始化熵。
- `render_snapshot_abi.h`：不含 ECS、Vulkan、资产或玩法类型的不可变 snapshot value contract，以及显式 acquire/release lease。
- `hash_abi.h`：由 `snt_engine/zig/hash.zig` 实现的无分配 FNV-1a 与组合哈希。
- `json_abi.h`：由 `snt_engine/zig/json.zig` 实现的只读 JSON document、不透明 value handle、对象/数组查询与标量读取合同；解析后的字符串和值由 document 持有。

`snt_runtime_abi_query_descriptor()` 只在启动时使用。`snt_abi.lib` 是一个由锁定 Zig
工具链生成的单一静态 archive，内含 C 描述符、哈希、UUID state machine、JSON document adapter 和通用 deterministic host core，避免 Zig host 因 ABI 查询、队列、UUID、JSON 或 snapshot 而链接 C++ runtime；宿主只在创建、关闭或 callback failure 时通过 `SntRuntimeHostCallbacks` 输出低频日志，禁止在 frame/tick 路径记录日志。

## 规则

1. ABI 只使用 C 调用约定、定宽整数、显式 `struct_size`、C 字符串、byte view 和不透明句柄。
2. 不跨边界传递 C++ 类、STL 容器、异常、EnTT entity、Vulkan handle、Zig allocator 或 Zig error union。
3. 每个可扩展的 output struct 由调用方设置 `struct_size`；实现仅写入可容纳的已知字段。新增字段只能追加在尾部。固定大小的 value 输出必须显式声明为不可扩展。
4. snapshot payload 的 schema 与存活期必须由具体 acquire/release API 明确声明；不得把当前 renderer/ECS 内存直接 reinterpret 给客户端。
5. 新 Zig 模块以同一锁定的 Zig 工具链静态编译；与 C++ 的长期契约是 C ABI，不是 Zig ABI。

## 语言归属与迁移策略

语言选择由模块所有权和依赖边界决定，不由“某种语言是否碰巧有库”单独决定。长期
目标是让自研的 engine core 和确定性 game logic 向 Zig 汇聚，而不是把 Zig 限制成少数
工具函数。

| 范围 | 默认实现 | 例外与边界 |
| --- | --- | --- |
| 新增自研引擎核心 | Zig | 平台、GPU、UI 或第三方 C++ SDK 只在 C++ adapter 中存在；adapter 通过 C ABI 与 Zig core 交互。 |
| 新增确定性 game logic | Zig | 包含玩法规则、worldgen、AI、存档、协议编解码和数据变换；内容定义仍可属于脚本或数据。 |
| 既有 C++ engine/game 模块 | C++ 维护，按模块迁移 | 不进行全量重写；只在模块有实质性重构或新能力工作时迁移，并在迁移完成后删除旧实现。 |
| 第三方 C++ 库、模板库或对象型 SDK | C++ 薄 adapter | 不允许第三方 C++ 类型、STL、异常或对象所有权扩散到 Zig 或其他 game 模块。 |
| 仅提供 C API 的库 | 由模块所有权决定 | C API 可直接由 Zig 或 C++ 使用，不能仅因库是 C 就把自研模块默认写成 C++。 |
| 内容、平衡数值和热更规则 | 数据或脚本 | 不把脚本运行时、编辑器对象或内容语义塞进通用引擎 core。 |

执行规则：

1. 新的自研逻辑默认先判断能否成为 Zig 模块；只有它本身必须直接持有 C++ 第三方对象、模板类型或 C++ 回调生命周期时，才建立一个隔离的 C++ adapter。
2. “C++ 有可用库”只允许增加 adapter，不能让 adapter 外的整个引擎或玩法模块改为 C++。如果库有稳定 C API，优先让 Zig 模块直接拥有调用。
3. 同一锁定 Zig 工具链下的私有 Zig-to-Zig 源码接口可以使用 Zig 类型；任何 C++/Zig 边界、插件边界、外部宿主边界和可独立链接 archive 都必须使用 C ABI。
4. 既有 C++ 模块的维护、热修和小范围功能扩展可以继续用 C++，但不得以短期开发速度为理由新建长期自研 C++ core。短期交付效率是迁移排序依据，不是长期所有权规则。
5. 每次迁移以可独立测试的纵向切片完成：先定义 C ABI、值所有权、日志和关闭规则，再迁移实现；完成后删除旧实现，不保留长期双写或兼容 wrapper。

当前的优先纵向切片已完成 Zig-owned 的确定性 command queue、snapshot buffer、headless
`ISimulationSession` C++ adapter、`RuntimeKeyIndex` 的 Zig-owned 存储，以及 `UuidGenerator`
的 Zig-owned 状态机：adapter 通过
callback table 驱动既有 session、scheduler 和关闭顺序，同时不让 C++ runtime 对象跨越 ABI；
key index 则由 Zig 复制、校验、排序和发布 immutable snapshot，UUID adapter 只收集宿主熵，
各 C++ facade 都只保留无算法重复的 value facade。下一步才是选定一个真实 game simulation 子系统，接入 command schema、snapshot
编码和外部 tick policy；这些基础设施稳定后再扩大到高频玩法域。

## 已迁移的 Zig 核心模块

`snt_engine/zig/hash.zig` 是第一个由 Zig 接管的自研引擎实现，
`snt_engine/zig/runtime_key_index.zig` 是第一个拥有长期 immutable snapshot 的 Zig engine
core，`snt_engine/zig/uuid.zig` 则拥有 UUID 的 seed mixing、state transition 与 packing，
`snt_engine/zig/json.zig` 则拥有通用 JSON document 的解析与存储。
`core/hash.h`、`core/runtime_key_index.h` 和 `core/uuid.h` 保留 C++ 调用面，但不再保留第二份
算法或容器存储实现：前者通过 `hash_abi.h` 委托给 Zig，key index 通过
`runtime_key_index_abi.h` 持有 opaque snapshot handle，UUID 通过 `uuid_abi.h` 持有 value-owned
state；其 C++ facade 仅采集 `steady_clock` / `random_device` 熵并处理不可能的 ABI failure 日志。
C、C++ 和 Zig host 都只链接一个 `snt_abi` archive；
`core/json.h` 提供不泄漏 `std.json`、nlohmann 或 allocator 的 C++ facade，
`core/runtime_config.cpp` 已通过该 facade 读取 runtime-owned 配置；
`abi_consumer_smoke.zig` 覆盖该单库消费路径。

构建需要精确的 Zig `0.16.0-dev.3142+5ccfeb926` 工具链；配置时通过
`-DSNT_ZIG_EXECUTABLE=/absolute/path/to/zig` 指定。CMake 会拒绝其他版本，并将 Zig
缓存和静态库产物放入构建目录，不把开发机路径或缓存写入源代码树。

面向 C/C++ 链接的 ABI archive 固定使用 Zig `ReleaseFast`，避免把 Zig Debug panic
runtime 带入 MSVC 最终链接；Zig 自身的 `-O Debug` contract test 保留参数校验覆盖。

## 宿主合同

`runtime_host_abi.h` 的 v1 调用顺序现在由 `zig/runtime_host.zig` 的通用宿主核心实现。
descriptor 宣告 `SNT_RUNTIME_ABI_CAPABILITY_HOST_LIFECYCLE`、
`...DETERMINISTIC_COMMANDS` 和 `...RENDER_SNAPSHOT_LEASES`；C、C++ 和 Zig consumer
都通过同一份 `snt_abi` archive 调用。它只持有 C callback table 和 value-owned 数据，
不持有 C++ session、ECS、GPU 或 allocator 对象。

1. `create` 接收路径、runtime/session 序列化配置、固定 tick 周期和 C callback table；host 在返回前复制所有 byte view，callback 的 `user_data` 由调用方保持到 `shutdown` 返回。
2. 命令 payload 在 enqueue 时复制。每条命令必须提供非零 `target_tick`、producer 的 128-bit value ID 和单调 sequence；适配器按 `(target_tick, producer.high, producer.low, sequence)` 排序，禁止 socket poll 顺序影响状态。
3. `run_fixed_tick(expected_tick)` 只接受连续的下一 tick，依次调用 sorted `apply_command`、`before_fixed_tick`、引擎 fixed systems、`after_fixed_tick`。v1 所有 host API 都由同一个 control thread 串行调用，不允许 callback 重入 tick/shutdown。
4. `after_fixed_tick` 中只能通过 publish API 发送 render snapshot。host 复制 payload 并自己写入 authoritative tick 和 presentation sequence；acquire 返回的 payload 是 borrowed view，直到使用对应 lease ID release 才失效。

## 后续门槛

通用 `SntRuntimeHost` 已覆盖 create/tick/shutdown、排序命令和 lease snapshot；
`engine/zig_simulation_runtime_host.h` 现提供 headless C++ `ISimulationSession` adapter，并由
fake session 测试验证 session -> scheduler -> post-tick 顺序、stop、callback failure 和初始化失败
清理。该 adapter
暂时让 `RuntimeConfig` 保留 C++ value model，解析通过 Zig-owned JSON document adapter 完成；ABI
config blob 保持 absent，直到稳定序列化 schema 确定。

`RuntimeKeyIndex` 现在由 `zig/runtime_key_index.zig` 真实拥有 key bytes、字节序排序、generation
和 snapshot 引用计数。`rebuild` 在完成全部复制和校验后才替换发布指针，失败会保持旧 generation；
已 acquire 的 snapshot 即使 index/C++ facade 销毁后仍可查询，直到显式 release。索引 owner
串行化 rebuild/acquire/restore/destroy，snapshot 查询与 retain/release 可以在 worker work 中持有。
这给后续 game content reload、协议表和存档类型表提供了不依赖 C++ STL 的稳定边界。

`UuidGenerator` 现在由 `zig/uuid.zig` 真实拥有种子混合、counter、peek、reset 和 UUID 打包。
`core/uuid.cpp` 只在构造时采集 C++ 平台熵，并在 ABI 调用出现内部不变量失败时记录 fatal；它不在
UUID 高频发放路径记录日志，也不再保留 C++ seed mixer 或 generator 算法。状态使用 versioned
value struct 而非 opaque heap handle，以保留既有 `UuidGenerator` 的值语义，同时不向 ABI 引入
allocator、C++ 对象或 Zig allocator 所有权。

`JsonDocument` 由 `zig/json.zig` 使用私有 `std.json` 解析，并以 opaque document/value handle
发布只读查询。输入字节在 parse 时复制；C++ `string_view` 与 value handle 都是 document 的
借用视图，必须在 document 销毁前使用。重复对象 key 被拒绝，避免 runtime config 出现歧义。
`RuntimeConfig` 本身仍是 C++ value model，负责文件 I/O、字段范围校验和低频错误日志；它不再链接
或暴露 nlohmann JSON。

下一步是选定真实 game session，定义 command schema registry、实际 snapshot 编码和外部 tick
policy；接入时沿用同一组 C、C++、Zig golden tests，完成一个模块迁移后删除对应旧实现。
