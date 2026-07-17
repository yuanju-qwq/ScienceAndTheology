# MUI 替代 ImGui 能力补齐清单

## 决策与边界

Dear ImGui 已从本工程的生产源码、CMake 依赖和第三方目录移除。后续游戏运行时 UI、开发期 UI 和 Mod UI 均以 `snt::ui` 的 retained MUI 为唯一 UI 路径；不保留 ImGui 兼容层或回退路径。

本清单不是一次性开发计划。只有某个已确认的玩家流程、开发流程或 Mod 场景需要一项能力时，才实现对应条目；实现时应同时完成这里列出的公共接口、平台边界和测试，避免为单个页面引入临时私有实现。

## 当前基线

已可用或已有明确实现路径的能力：

- Retained View 树、MVVM 绑定、UI 层级和屏幕生命周期。
- `View`、文本、按钮、图像、物品格、网格、帧布局和滚动容器。
- 指针捕获/冒泡、基础键盘激活、焦点状态、模态层输入阻断。
- Unicode、双向文字、CJK、彩色 Emoji 字形排版，以及图像/字形图集和 Vulkan 绘制。
- JSON PackedScene 与 C++ `UiWidgetTreeBuilder` 共用同一实例化路径。
- 背包、合成、快捷栏和性能面板已有 MUI 游戏侧入口。

对应实现主要位于：

- `snt_engine/ui/retained_mui.h/.cpp`
- `snt_engine/ui/ui_packed_scene.h/.cpp`
- `game/client/gameplay_ui.h/.cpp`
- `snt_engine/tests/test_mui_retained.cpp`

当前工作区中 `FlexLayout`、`NineSliceView`、`UiViewport` 和用户 UI 缩放已出现公共声明，但尚未形成完整的实现、PackedScene 支持和测试闭环。它们在完成前不能视为可交付能力。

本清单编写期间的一次验证中，`cmake --build build_ui_verify --target snt_tests --config Debug` 被这一未完成的接口迁移阻断：当时 `ui_packed_scene.cpp` 仍引用已删除的 `UiWidgetType::Linear` 和 `LinearLayout`。先完成下面的“当前公开控件的实现闭环”，再执行其余 UI 能力的验证。

## 已确认的硬缺口

### P0：可编辑文本与平台文本输入

触发场景：角色命名、聊天、搜索、服务器地址、Mod 配置值。

当前 `UiInputEvent` 只有指针和有限导航键；不存在可编辑文本控件、文本组合事件、剪贴板边界或 IME 生命周期。

实现时先声明并稳定以下边界：

- `TextField`/`TextEditor`：UTF-8 编辑缓冲、光标、选区、撤销/重做、密码模式和提交/取消事件。
- `UiTextInputEvent`：提交文本、组合开始/更新/结束、删除和选择变更；平台 SDL 事件不能直接泄漏到 View API。
- `IUiClipboard`：读取/写入纯文本的可替换平台服务。
- `IUiTextInputPlatform`：激活/停用 IME、设置候选框位置的宿主接口。

验收：中文、日文、阿拉伯文和 Emoji 可编辑；IME 组合文本不重复提交；复制、粘贴、选择、焦点切换和窗口 DPI 变化均有自动化测试。

### P0：库存拖放与物品交互

触发场景：真实库存整理、拆分堆叠、装备栏、容器和快捷栏交换。

当前 `SlotView` 适合展示和激活，但没有通用拖放会话、拖拽预览、放置协商或回滚模型。

实现时先声明：

- `UiDragPayload` 和 `UiDropTarget`，载荷使用稳定物品/槽位 ID，不持有 ECS 指针。
- `UiDragSession`，由 `UiRuntime` 管理捕获、悬停目标、取消和视觉预览。
- 游戏侧 `InventoryTransaction` 命令边界，服务端或模拟层确认后才提交状态。

验收：拖放、交换、合并、拆分、无效放置、窗口外取消和网络拒绝均不会丢失或复制物品。

### P0：窗口缩放、DPI 与布局契约闭环

触发场景：高 DPI 显示器、窗口拖动到不同缩放率屏幕、玩家 UI 缩放设置。

`UiViewport`、`set_viewport()` 和 `set_user_scale()` 已有接口方向，但还需要贯通平台窗口尺寸、逻辑 UI 尺寸、命中测试、渲染坐标和屏幕更新回调。

验收：100%、125%、150%、200% 缩放和窗口 resize 下，布局、裁剪、鼠标命中及字体清晰度一致；所有 MUI 屏幕使用同一视口来源。

### P0：当前公开控件的实现闭环

触发场景：任何页面采用弹性布局、九宫格皮肤或用户缩放。

- 将 `FlexLayout` 的测量、排列、`justify`、`align` 和边距/权重行为实现并测试；旧 `LinearLayout` 名称不再保留。
- 将 `NineSliceView` 从命令缓冲贯通到 `Arc2DRenderer`、`UiDrawData`、Vulkan renderer、图集 UV 和裁剪测试。
- 将 `UiWidgetType`/JSON PackedScene 同步为 `Flex` 和 `NineSlice`，不保留旧序列化类型。
- 让视口接口在 `UiRuntime`、层栈上下文和客户端宿主中全部实际调用。

## 按需求补齐的常用控件

### P1：设置与菜单

触发场景：图形/音频/控制设置、世界创建、Mod 设置页面。

- Toggle、Checkbox、RadioGroup、Slider、Stepper、ProgressBar。
- Select/ComboBox、Tabs、可关闭 Modal、ContextMenu、Tooltip。
- 可验证的表单状态：值、禁用原因、错误消息、提交和恢复默认值。
- 焦点顺序、Tab/方向键导航、Escape 关闭约定。

验收：鼠标和键盘均可完整操作；失焦、禁用、错误、长文本和窄窗口状态均有测试。

### P1：可变数据与性能

触发场景：配方浏览、任务列表、服务器列表、日志查看器、Mod 内容目录。

- 虚拟化列表/网格，具有稳定 item key 和回收策略。
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
