# Zig ABI 迁移基线

> 状态：v1 基线，2026-07-22。它定义自研引擎向 Zig 的渐进迁移路径：新增的语言无关底层实现默认使用 Zig，既有 C++ 模块在稳定 C ABI 合同准备完成后逐个替换；迁移期间 C++ 继续承担既有宿主与高层集成 API。

## 当前范围

`snt_engine/abi/` 是唯一允许 Zig、C 或其他原生宿主直接包含的引擎接口根。v1 提供：

- `abi_common.h`：定宽状态码、字节 view 与低频日志 callback 类型；
- `runtime_abi.h`：运行时 ABI 版本/能力查询；
- `runtime_host_abi.h`：value-only 的宿主创建、确定性 tick、命令与关闭合同；当前只声明，能力位尚未启用。
- `render_snapshot_abi.h`：不含 ECS、Vulkan、资产或玩法类型的不可变 snapshot value contract，以及显式 acquire/release lease。
- `hash_abi.h`：由 `snt_engine/zig/hash.zig` 实现的无分配 FNV-1a 与组合哈希。

`snt_runtime_abi_query_descriptor()` 只在启动时使用。`snt_abi.lib` 是一个由锁定 Zig
工具链生成的单一静态 archive，内含 C 描述符与 Zig 实现，避免 Zig host 因 ABI 查询或哈希而链接 C++ runtime；实际宿主应在版本协商成功或失败时通过自身 logger / 后续 `SntRuntimeHostCallbacks` 输出一次低频 Info/Warn，禁止在 frame/tick 路径记录日志。

## 规则

1. ABI 只使用 C 调用约定、定宽整数、显式 `struct_size`、C 字符串、byte view 和不透明句柄。
2. 不跨边界传递 C++ 类、STL 容器、异常、EnTT entity、Vulkan handle、Zig allocator 或 Zig error union。
3. 每个 output struct 由调用方设置 `struct_size`；实现仅写入可容纳的已知字段。新增字段只能追加在尾部。
4. snapshot payload 的 schema 与存活期必须由具体 acquire/release API 明确声明；不得把当前 renderer/ECS 内存直接 reinterpret 给客户端。
5. 新 Zig 模块以同一锁定的 Zig 工具链静态编译；与 C++ 的长期契约是 C ABI，不是 Zig ABI。

## 首个 Zig 模块

`snt_engine/zig/hash.zig` 是第一个由 Zig 接管的自研引擎实现。`core/hash.h` 保留已有
C++ 调用面，但不再保留第二份算法实现，而是通过 `hash_abi.h` 委托给 Zig 静态库。C、C++ 和
Zig host 都只链接一个 `snt_abi` archive；`abi_consumer_smoke.zig` 覆盖该单库消费路径。

构建需要精确的 Zig `0.16.0-dev.3142+5ccfeb926` 工具链；配置时通过
`-DSNT_ZIG_EXECUTABLE=/absolute/path/to/zig` 指定。CMake 会拒绝其他版本，并将 Zig
缓存和静态库产物放入构建目录，不把开发机路径或缓存写入源代码树。

面向 C/C++ 链接的 ABI archive 固定使用 Zig `ReleaseFast`，避免把 Zig Debug panic
runtime 带入 MSVC 最终链接；Zig 自身的 `-O Debug` contract test 保留参数校验覆盖。

## 宿主合同

`runtime_host_abi.h` 已冻结未来 `SntRuntimeHost` 的 v1 调用顺序，但当前
`SNT_RUNTIME_ABI_CAPABILITY_HOST_LIFECYCLE`、`...DETERMINISTIC_COMMANDS` 和
`...RENDER_SNAPSHOT_LEASES` 都不会由 descriptor 宣告。没有 capability 的实现必须返回
`SNT_ABI_STATUS_UNSUPPORTED`；当前 stub 在 `create` 时通过 `SntRuntimeHostCallbacks.log`
输出一次低频 Warn，明确说明适配器尚未链接。

1. `create` 接收路径、runtime/session 序列化配置、固定 tick 周期和 C callback table；host 在返回前复制所有 byte view，callback 的 `user_data` 由调用方保持到 `shutdown` 返回。
2. 命令 payload 在 enqueue 时复制。每条命令必须提供非零 `target_tick`、producer 的 128-bit value ID 和单调 sequence；适配器按 `(target_tick, producer.high, producer.low, sequence)` 排序，禁止 socket poll 顺序影响状态。
3. `run_fixed_tick(expected_tick)` 只接受连续的下一 tick，依次调用 sorted `apply_command`、`before_fixed_tick`、引擎 fixed systems、`after_fixed_tick`。v1 所有 host API 都由同一个 control thread 串行调用，不允许 callback 重入 tick/shutdown。
4. `after_fixed_tick` 中只能通过 publish API 发送 render snapshot。host 复制 payload 并自己写入 authoritative tick 和 presentation sequence；acquire 返回的 payload 是 borrowed view，直到使用对应 lease ID release 才失效。

## 后续门槛

在启用 `SntRuntimeHost` capability 并实现 create/tick/shutdown 前，必须完成当前
`ISimulationSession` 的 value-only adapter、command schema registry、snapshot buffer
所有权/并发策略和关闭顺序，并为 C、C++、Zig consumer 添加同一组 golden tests。完成一个模块迁移后删除对应旧实现，不保留长期双实现。
