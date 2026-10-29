# Baltam IR

将 M 源码转换为 Non-SSA IR，为后续 SSA 优化与解释执行提供基础。目前支持的语法范围见 [M 语法与 IR 设计](docs/M语法与IR设计.md)，架构思路见 [新解释器架构](docs/新解释器架构.md)。

## 环境与依赖

- CMake 3.16 或更新版本。
- 支持 C++17 的编译器。
- 同一套兼容版本的 Baltam core、AST、logger SDK，以及 AST 解析器运行时需要的内核和依赖库。

默认目录结构：

```text
deps/
├── core/
│   ├── include/
│   └── lib/        # bt_core、bt_ast_base、bt_ast 等
└── logger/
    └── lib/        # bt_logger
```

CMake 使用 `find_library` 查找库，不硬编码 `.so` 等平台后缀。SDK 本身仍需要提供与目标系统和编译器兼容的库。

## 配置与构建

在项目根目录运行：

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --target baltam_ir
```

项目的两个主要构建目标：

| 目标 | 作用 |
| --- | --- |
| `baltam_ir_core` | 静态库，包含 IR 数据结构、缓存和 AST → IR 构建 |
| `baltam_ir` | 命令行程序，读取 M 文件并输出 IR |

构建 `baltam_ir` 会自动构建 `baltam_ir_core`。启用测试后还会产生测试目标；CTest 也会添加辅助目标。

可选配置：

| 选项 | 默认值 | 说明 |
| --- | --- | --- |
| `BALTAM_AST_ROOT` | `deps/core` | core 与 AST SDK 根目录，包含 include 和 lib |
| `BALTAM_LOGGER_ROOT` | `deps/logger` | logger SDK 根目录 |
| `BALTAM_RUNTIME_LIBRARY_DIRS` | 空 | 额外的构建时运行库搜索目录，用分号分隔 |
| `BALTAM_BUILD_UTILS` | `ON` | 构建命令行程序 |
| `BUILD_TESTING` | `ON` | 注册测试 |
| `BALTAM_ENABLE_AST_TEST` | `OFF` | 构建并注册 AST 降低与缓存集成测试 |

例如指定另一套 SDK：

```bash
cmake -S . -B build \
  -DBALTAM_AST_ROOT=/path/to/core \
  -DBALTAM_LOGGER_ROOT=/path/to/logger
```

切换 SDK 时建议使用新的构建目录，以免继续使用 CMake 已缓存的库路径。多配置生成器使用 `cmake --build build --config Debug`，程序通常位于 `build/Debug`；Windows 下文件名为 `baltam_ir.exe`。

## 运行时库配置

构建时链接成功，并不代表 AST 运行时能找到动态加载的内核库。需要让系统动态加载器找到内核及其依赖，并确保它们与 SDK 配套。

Linux 本地开发示例（根据实际安装位置调整）：

```bash
export LD_LIBRARY_PATH=/home/zj/Desktop/baltam_kernel/build/src:/home/zj/Desktop/baltam_kernel/build/src/interface:/home/zj/Desktop/baltam_kernel/deps/core/lib:/home/zj/Desktop/baltam_kernel/deps/logger/lib:/opt/Baltamatica/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}
```

从 CLion 启动时，在 Run/Debug Configuration 中设置相同的环境变量。Windows 使用 `PATH` 配置 DLL 搜索目录；其他系统按其动态加载机制配置。`BALTAM_RUNTIME_LIBRARY_DIRS` 可补充支持 RPATH 的系统上的构建产物搜索路径，但动态加载库的传递依赖仍可能需要运行环境配置。

遇到 `AST 解析器初始化失败: 100` 时，检查内核库是否可被找到、传递依赖是否完整，以及是否混用了不同版本。出现 `undefined symbol` 时，尤其要检查实际加载的 core 与内核是否配套。

## baltam_ir 用法

```bash
./build/baltam_ir test/syntax/if_else.m
./build/baltam_ir /absolute/path/to/function.m
./build/baltam_ir test/syntax/if_else.m -o /tmp/if_else.ir
```

输入、输出均支持相对路径，相对路径以当前工作目录为准。不指定 `-o` 时输出到终端；指定时写入文件并覆盖已有内容，输出不能与输入为同一个文件。

输出包括模块路径、函数或脚本、当前 IR 阶段、slot 表、基本块与指令。例如：

```text
; ModulePath = '/absolute/path/to/if_else.m'

define @if_else() {
; stage: Non-SSA
; slots:
;   slot0 = x
...
}
```

脚本主体表示为 `script`，局部函数随后输出；嵌套函数以 `nested in` 注释标明词法父函数。尚未支持的语法会报告错误。

可选：将可执行文件所在目录加入当前终端的 `PATH`，之后直接使用 `baltam_ir`：

```bash
export PATH="$PWD/build:$PATH"
baltam_ir test/syntax/if_else.m
```

## 测试与代码接口

先按上文配置运行时库，再执行：

```bash
cmake -S . -B build -DBALTAM_ENABLE_AST_TEST=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

C++ 调用方链接 `baltam_ir_core`，通过 `IRBuilder::build_file(path)` 获取共享的 `IRModule`。同一进程内会复用有效缓存；跨世界的文件修改检查由缓存处理，运行时需要在合适边界推进世界计数。
