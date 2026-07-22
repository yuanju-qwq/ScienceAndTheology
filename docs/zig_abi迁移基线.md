# Zig ABI 迁移基线

> 状态：v1 基线，2026-07-22。它不是将当前引擎全量改写为 Zig 的承诺，而是让后续迁移不再依赖 C++ 内部类型的边界合同。

## 当前范围

`snt_engine/abi/` 是唯一允许 Zig、C 或其他原生宿主直接包含的引擎接口根。v1 提供：

- `abi_common.h`：定宽状态码、字节 view 与低频日志 callback 类型；
- `runtime_abi.h`：运行时 ABI 版本/能力查询；
- `render_snapshot_abi.h`：不含 ECS、Vulkan、资产或玩法类型的不可变 snapshot value contract。

`snt_runtime_abi_query_descriptor()` 只在启动时使用。它是纯 C 叶子库，避免 Zig host 因 ABI 查询而链接 C++ runtime；实际宿主应在版本协商成功或失败时通过自身 logger / 后续 `SntRuntimeHostCallbacks` 输出一次低频 Info/Warn，禁止在 frame/tick 路径记录日志。

## 规则

1. ABI 只使用 C 调用约定、定宽整数、显式 `struct_size`、C 字符串、byte view 和不透明句柄。
2. 不跨边界传递 C++ 类、STL 容器、异常、EnTT entity、Vulkan handle、Zig allocator 或 Zig error union。
3. 每个 output struct 由调用方设置 `struct_size`；实现仅写入可容纳的已知字段。新增字段只能追加在尾部。
4. snapshot payload 的 schema 与存活期必须由具体 acquire/release API 明确声明；不得把当前 renderer/ECS 内存直接 reinterpret 给客户端。
5. 新 Zig 模块以同一锁定的 Zig 工具链静态编译；与 C++ 的长期契约是 C ABI，不是 Zig ABI。

## 后续门槛

在声明 `SntRuntimeHost` 的 create/tick/shutdown C API 前，必须先完成：游戏 session 的 value-only 创建参数、确定性 command 输入、snapshot acquire/release 生命周期和关闭顺序设计，并为 C consumer 与 C++ consumer 添加同一组 golden tests。完成一个模块迁移后删除对应旧实现，不保留长期双实现。
