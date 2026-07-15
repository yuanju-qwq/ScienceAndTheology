# Science & Theology

Science & Theology 是一个 C++20 Vulkan 游戏。此仓库负责游戏可执行程序、游戏内容、运行时打包，并通过 Git 子模块固定引擎版本。可复用运行时位于 [ScienceAndTheologyEngine](https://github.com/yuanju-qwq/ScienceAndTheologyEngine)。架构、玩法和迁移文档见 [docs/README.md](docs/README.md)。

## 仓库结构

| 路径 | 职责 |
| --- | --- |
| `snt_engine/` | Git 子模块：渲染、平台、ECS、脚本、数据系统与引擎测试。 |
| `game/` | 游戏宿主可执行程序，以及配置、场景、脚本和内容打包。 |
| `game/client/main.cpp` | 图形宿主，创建 `ClientRuntime + ScienceAndTheologyClientSession`。 |
| `game/server/main.cpp` | 无头宿主，创建 `SimulationRuntime + ScienceAndTheologySimulationSession`。 |

顶层 CMake 只构建 `snt_engine` 与 `game`。旧的 `src/` 布局不属于当前游戏构建链。

## 环境要求

当前支持的开发环境是 Windows 10/11 x64 和 PowerShell 7。

- Git
- CMake 3.24 或更高版本
- Visual Studio 2019 或 2022，并安装“使用 C++ 的桌面开发”工作负载
- Vulkan SDK：`VULKAN_SDK` 必须指向包含 `shaderc_shared` 的 SDK（需要 `Include`、`Lib` 和 `Bin`）

## 获取源码

首次克隆时一并取得引擎子模块：

```powershell
git clone --recurse-submodules https://github.com/yuanju-qwq/ScienceAndTheology.git
Set-Location ScienceAndTheology
```

已有工作区在配置 CMake 前初始化当前固定的引擎版本：

```powershell
git submodule update --init --recursive
```

## 编译游戏

下面的 Debug 构建会跳过引擎测试，生成可直接运行且已打包资源的游戏：

```powershell
cmake -S . -B build -DSNT_BUILD_TESTS=OFF
cmake --build build --target snt_game_client --config Debug
```

配置阶段会自动检查引擎模块的直接 CMake 依赖；出现漏声明的 `#include "module/..."` 时会在生成构建文件前失败。

使用 Visual Studio 生成器时，运行：

```powershell
& .\build\bin\Debug\science_and_theology.exe
```

构建后会在可执行程序旁组装运行时目录：

| 运行时路径 | 内容 |
| --- | --- |
| `engine/` | 编译后的着色器、ICU 数据等引擎资源。 |
| `game/` | 游戏配置、场景、脚本和游戏资产。 |
| `user/` | 程序运行时创建，用于日志、存档和缓存。 |

从同一构建目录编译 Release：

```powershell
cmake --build build --target snt_game_client --config Release
```

Visual Studio 的 Release 可执行程序位于 `build/bin/Release/science_and_theology.exe`。

## 编译无头服务端

服务端与图形客户端复用同一份游戏模拟、脚本、机器和 terrain 初始化，但不链接 SDL、Vulkan、渲染、UI 或玩家输入模块：

```powershell
cmake --build build --target snt_game_server --config Debug
& .\build\bin\Debug\science_and_theology_server.exe --ticks 3
& .\build\bin\Debug\science_and_theology_server.exe --ticks 3 --network --bind 127.0.0.1 --tcp-port 8910 --udp-port 8911
```

不带 `--ticks` 时服务端进入固定 20 TPS 模拟循环。`--ticks` 默认不监听端口；添加 `--network`（或在 `server_network` 配置中启用）后启动 TCP reliable + UDP unreliable transport。服务端已解析版本化 `SNTG` 登录/命令 envelope，但默认安装关闭式认证器，因此不会意外开放匿名玩家准入；具体认证、gameplay command、AOI 和 snapshot/delta 尚未接入。详见 [游戏网络协议设计](docs/游戏网络协议设计.md)。

## 运行引擎测试

需要测试时使用独立构建目录：

```powershell
cmake -S . -B build-tests -DSNT_BUILD_TESTS=ON
cmake --build build-tests --target snt_tests snt_simulation_runtime_tests snt_network_tests --config Debug
ctest --test-dir build-tests -C Debug --output-on-failure
```

`snt_tests` 覆盖通用引擎模块并包含 P6 保留式 UI 测试；`snt_simulation_runtime_tests` 和 `snt_network_tests` 只链接 SDL/Vulkan-free 闭包。`snt_p6_tests` 不再存在。

## 引擎开发

`snt_engine/` 是独立 Git 仓库。引擎修改应先在该目录提交，再在本仓库提交更新后的子模块指针。引擎自身的构建与测试说明见 [`snt_engine/README.md`](snt_engine/README.md)。

子模块推送
git -C snt_engine push origin HEAD:main

## 许可证

代码和资源使用 [PolyForm Noncommercial License 1.0.0](LICENSE)。商业使用需向 yuanju（2358586959@qq.com）单独取得授权。
