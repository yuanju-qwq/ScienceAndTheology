# MUI 替代 ImGui 能力补齐清单

## 决策与边界

Dear ImGui 已从本工程的生产源码、CMake 依赖和第三方目录移除。后续游戏运行时 UI、开发期 UI 和 Mod UI 均以 `snt::ui` 的 retained MUI 为唯一 UI 路径；不保留 ImGui 兼容层或回退路径。

本清单不是一次性开发计划。只有某个已确认的玩家流程、开发流程或 Mod 场景需要一项能力时，才实现对应条目；实现时应同时完成这里列出的公共接口、平台边界和测试，避免为单个页面引入临时私有实现。

## 当前基线

已可用或已有明确实现路径的能力：

- Retained View 树、MVVM 绑定、UI 层级和屏幕生命周期。
- `View`、文本、按钮、图像、物品格、网格、帧布局和滚动容器。
- 指针捕获/冒泡、基础键盘激活、焦点状态、模态层输入阻断。
- `TextInput`/`TextEditor` 的 UTF-8 光标与选区编辑、密码显示、剪切/复制/粘贴、撤销/重做、IME 组合/提交，以及宿主候选框定位。
- 值对象化的 `UiDragSession`、`UiDragSource`、`UiDropTarget`；中断拖放会向源和悬停目标发送 `Cancel`。
- Unicode、双向文字、CJK、彩色 Emoji 字形排版，以及图像/字形图集和 Vulkan 绘制。
- JSON PackedScene 与 C++ `UiWidgetTreeBuilder` 共用同一实例化路径。
- `FlexLayout`、`GridLayout`、`ScrollView`、`VirtualListView`、`NineSliceView`、用户 UI 缩放和 DPI 逻辑坐标。
- Button、Checkbox、Slider、Modal、Tooltip、VirtualList、Slot 与 TextEditor 已通过 native、PackedScene v4 和 Mod facade 路径实例化。
- 背包、合成、快捷栏和性能面板已有 MUI 游戏侧入口。
- 任务书已作为 retained MUI 模态层接入：章节标签、可平移/缩放的前置任务图、详情与显式领奖均只消费认证账号的只读 `QuestBookSnapshot`；任务在服务器端按前置自动解锁，UI 不提供接取操作。

对应实现主要位于：

- `snt_engine/ui/retained_mui_runtime.h/.cpp`：每帧编排、布局、绘制与宿主 facade。
- `snt_engine/ui/retained_mui_input_router.h/.cpp`：命中测试、事件传播、焦点、拖放与交互状态。
- `snt_engine/ui/retained_mui_text_input_service.h/.cpp`：剪贴板和原生文本输入平台边界。
- `snt_engine/ui/retained_mui_view.h/.cpp`：retained View 树、通用控件和布局原语。
- `snt_engine/ui/ui_packed_scene.h/.cpp`
- `game/client/gameplay_ui.h/.cpp`
- `snt_engine/tests/test_mui_retained.cpp`

`FlexLayout`、`NineSliceView`、`UiViewport` 和用户 UI 缩放已经贯通 retained 布局、PackedScene、Arc2D/Vulkan draw-data、宿主输入坐标和回归测试。旧 `LinearLayout`/`UiWidgetType::Linear` 没有保留。

`UiLayerStack` 在隐藏、替换或卸载已挂载 root 前同步通知 `UiRuntime`，因此焦点控件先收到 `FocusLost`，活动拖放先收到 `Cancel`，不会依赖下一帧的失效 ID 或悬空 root 指针。

## 当前剩余的硬缺口

### P0：可编辑文本与平台文本输入

触发场景：角色命名、聊天、搜索、服务器地址、Mod 配置值。

`UiInputEvent` 已包含键盘、`TextCommit`、`TextComposition` 和焦点事件；`TextInput` 已提供 UTF-8 单行编辑、密码模式、IME 组合态和提交。客户端宿主会依焦点激活平台文本输入并更新候选框区域。

已落地稳定边界：

- `TextInput` 是单行字段；`TextEditor` 是多行 retained 控件。两者共享 UTF-8 选区、密码保护、撤销/重做、IME 组合态、提交回调和销毁安全的绑定生命周期。
- `UiInputEvent` 继续只暴露语义键、修饰键、提交文本与组合文本；SDL 键码、修饰掩码和事件指针不会进入 View 或 Mod API。
- `IUiClipboard` 和 `IUiTextInputPlatform` 由 `UiRuntime` 以共享所有权持有。客户端通过 SDL `Window` 适配器提供真实系统剪贴板、IME 启停与候选框区域；无图形宿主可使用 `UiMemoryClipboard`。
- 剪贴板失败和平台 IME 失败按 root/view 结构化告警并去重，不会在每帧刷日志。

已验收：IME 组合文本不会重复提交，焦点切换会清理组合态，UTF-8 选区/剪贴板/撤销重做、多行 Enter 与 Ctrl/Meta+Enter 提交、以及 DPI 候选框坐标均有 native 回归测试。多行自动换行、历史视图和滚动跟随仍在对应聊天/编辑器实际需求出现时补齐。

已聚焦控件或其任一祖先在运行时变为隐藏、`Gone`、禁用或不可聚焦时，路由器会沿 retained 路径发送一次 `FocusLost`，再清除焦点。原生文本输入同步只承认可交互的整条祖先路径，因此候选框与 IME 会在同一帧停用，不会保留到下一次按键。

### P0：库存拖放与物品交互

触发场景：真实库存整理、拆分堆叠、装备栏、容器和快捷栏交换。

`SlotView` 已通过通用 `UiDragSource`/`UiDropTarget` 接入 `UiRuntime` 管理的值对象 `UiDragSession`。载荷、源/目标 ID 和取消事件不携带 ECS 指针；窗口外、输入禁用、模态层截断和 root 卸载均会终止会话。

游戏侧稳定命令边界已落地：

- `game/client/inventory_slot_transaction.h` 定义值对象 `InventorySlotTransferRequest` / `InventorySlotTransferConfirmation`，以及 `IInventorySlotTransferCommandSink` / `IInventorySlotTransferConfirmationSource`。接口不包含 `View`、ECS、网络或原始指针。
- `GameplayUiController` 在 `Drop` 时只提交稳定槽位索引、观察到的栈快照和修订号；只有匹配确认才替换 `InventoryViewModel`。主键拖放发送整组，次键拖放发送向上取整的一半。
- 离线会话使用 `LocalInventorySlotTransferAuthority`，但仍通过排队确认返回，覆盖交换、合并、拆分、容量不足和陈旧修订拒绝。
- 服务端 `GamePlayerInventorySlotTransfer` / `GameServerPlayerState::apply_inventory_slot_transfer()` 使用同构条件槽位规则，并返回已提交的库存快照，供未来网络确认适配器使用。

已验收：通用源/目标协商、悬停、无效放置和中断取消；离线确认后的交换、合并和次键拆分；服务端条件槽位转移的原子提交和陈旧栈拒绝。

仍待网络流程接入：为 `GamePlayerInventorySlotTransfer` 增加客户端命令编解码、服务端确认/库存快照消息，以及连接模式下的 `IInventorySlotTransferCommandSink` 适配器。在这之前，联网客户端不会回退到本地模拟，而是拒绝该请求并记录一次结构化警告。

### P0：窗口缩放、DPI 与布局契约闭环

触发场景：高 DPI 显示器、窗口拖动到不同缩放率屏幕、玩家 UI 缩放设置。

`UiViewport`、`set_viewport()` 和 `set_user_scale()` 已贯通平台窗口尺寸、逻辑 UI 尺寸、命中测试、draw-data 坐标、裁剪区和屏幕更新上下文。

自动化覆盖高 DPI 与用户缩放下的逻辑输入/绘制一致性；100%、125%、150%、200% 的实机字体清晰度和跨显示器 resize 仍应在发布前截图回归中覆盖。

### P0：当前公开控件的实现闭环（已完成）

触发场景：任何页面采用弹性布局、九宫格皮肤或用户缩放。

- `FlexLayout` 的测量、排列、`justify`、`align`、边距和权重已实现并测试；旧 `LinearLayout` 名称不再保留。
- `NineSliceView` 已从命令缓冲贯通到 `Arc2DRenderer`、`UiDrawData`、Vulkan renderer、图集 UV 和裁剪测试。
- `UiWidgetType`/JSON PackedScene 使用 `Flex`、`NineSlice` 和 `TextEditor`，并已升级到 v4，以承载可变高度虚拟列表与自动 Tooltip 声明。
- 视口接口已在 `UiRuntime`、层栈上下文和客户端宿主实际调用。

## 按需求补齐的常用控件

### P1：设置与菜单

触发场景：图形/音频/控制设置、世界创建、Mod 设置页面。

- Checkbox、Slider、可关闭 Modal 和 Tooltip 已实现；Toggle、RadioGroup、Stepper、ProgressBar 仍待需求驱动。
- Select/ComboBox、Tabs、ContextMenu 仍待需求驱动。
- 可验证的表单状态：值、禁用原因、错误消息、提交和恢复默认值。
- 焦点顺序与 `Tab`/`Shift+Tab` 循环导航已由 `UiRuntime` 统一实现，跳过隐藏和禁用控件，并沿用 `FocusLost`/`FocusGained` 冒泡生命周期；方向键导航和 Escape 关闭约定仍待需求驱动。

验收：鼠标和键盘均可完整操作；失焦、禁用、错误、长文本和窄窗口状态均有测试。

### P1：可变数据与性能

触发场景：配方浏览、任务列表、服务器列表、日志查看器、Mod 内容目录。

- 可变高度的 `VirtualListView` 已具备稳定 index、可见项回收和每项高度 provider；可变高度网格虚拟化仍待需求驱动。
- 可排序、筛选、搜索、高亮和空状态。
- 可折叠树与懒加载节点，用于层级设置或内容浏览。
- UI 帧统计：布局次数、绘制批数、顶点数、图集压力和慢路径告警。

日志只在阈值超限、图集耗尽、布局异常或数据源错误时输出，不能在每帧产生高频日志。

### P1：文本与本地化展示

触发场景：说明页、任务文本、快捷键提示、富状态信息。

- 多行自动换行、最大行数、省略号和可选择/可复制文本。
- 结构化富文本，禁止把未验证的标记直接传给渲染器。
- 本地化参数、复数规则和语言切换后的增量刷新。
- 图标字体或内联图片与基线对齐。

## 开发与维护能力

### P2：替代 ImGui 调试面板的方式

触发场景：定位布局、输入、图集和渲染问题。

不重新引入 ImGui。按需要提供以下 MUI/日志化工具：

- 受配置开关控制的 MUI Debug Overlay：View 边界、ID、焦点、裁剪区、绘制批次和输入路由。
- 可导出的 UI 帧诊断快照（JSON/日志），用于无图形环境和线上问题分析。
- 固定视口截图回归测试，覆盖中英文、RTL、缩放和窄屏。
- PackedScene 校验器和资源加载错误的源文件/节点 ID 定位。

### P2：无障碍与输入设备

触发场景：正式发布前的键鼠、手柄与辅助功能要求。

- 语义角色、标签、状态和可访问名称的 UI 树导出。
- 完整手柄焦点导航、重复按键和焦点可视化。
- 高对比度主题、字体大小、减少动画和色觉辅助主题。

## 实现约束

- 新能力放入 `snt_engine/ui`，游戏规则、物品语义和网络命令仍放在 `game`/模拟层。
- 公共接口先写在头文件；平台、渲染器和游戏侧通过接口连接，不直接相互包含实现细节。
- 不再新增 ImGui 头文件、CMake target、第三方下载或兼容适配器。
- 每个控件至少覆盖：布局、鼠标、键盘、禁用状态、销毁后回调安全和 draw-data 正确性。
- 所有不确定的运行时问题优先增加低频、可筛选的结构化日志，日志中包含 UI root ID、View ID 和屏幕所有者。

## ImGui 移除验证

每次引入 UI 依赖或升级构建脚本后执行：

```powershell
$matches = rg -n -i -uu --glob '!build/**' --glob '!build_*/*' `
  'dear[ _-]?imgui|\bimgui\b|im_gui|IMGUI' CMakeLists.txt game snt_engine
if ($LASTEXITCODE -eq 0) {
    $matches
    throw 'ImGui reference detected.'
}
if ($LASTEXITCODE -ne 1) {
    throw "ImGui audit failed with rg exit code $LASTEXITCODE."
}
```

截至本清单创建时，该检查在源码树、CMake 和第三方目录中没有匹配项。
