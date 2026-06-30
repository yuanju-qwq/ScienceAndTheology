# Science & Theology

一个独立游戏项目。

## 编译 core

core (`snt_core`) 是纯 C++20 静态库，无 Godot 依赖，可在引擎外独立编译和测试。

### 前置要求

- CMake >= 3.20
- 支持 C++20 的编译器（MSVC 2019/2022、Clang、GCC 均可）

### 编译步骤

```bash
# 在项目根目录下
cd src

# 配置（使用 build 目录）
cmake -B build -DCMAKE_BUILD_TYPE=Debug

# 编译 core 库
cmake --build build --target snt_core --config Debug

# 编译全部（含测试）
cmake -B build -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON
cmake --build build --config Debug

# 运行 core 测试
ctest --test-dir build -R "^snt\."
```

> `build/` 和 `build2/` 目录为已有构建产物，若需重新配置可先删除。

### 项目结构

| 路径 | 说明 |
|------|------|
| `src/CMakeLists.txt` | 顶层 CMake 配置 |
| `src/core/` | 核心库源码（`snt_core`） |
| `src/server/` | 独立服务器模块 |
| `src/bindings/` | GDExtension 绑定（生成 .dll） |
| `tests/core/` | core 单元测试 |
| `build/` | 已有构建目录（VS2019 x64 Debug） |
| `build2/` | 另一组已有构建目录 |

## 许可证

本项目的代码和资源采用 **PolyForm Noncommercial License 1.0.0**。

- ✅ 您可以出于非商业目的查看、Fork、修改和分发本项目
- ❌ **未经授权，禁止将本项目或其衍生产品用于任何商业用途**（包括但不限于销售、商业发行、商业托管等）
- 💼 如需商业授权（如上架 Steam），请联系 **yuanju (2358586959@qq.com)**

详见 [LICENSE](./LICENSE) 文件。

本项目使用了 [godot-cpp](https://github.com/godotengine/godot-cpp)（MIT 许可证），相关版权声明见 [NOTICE](./NOTICE) 文件。
