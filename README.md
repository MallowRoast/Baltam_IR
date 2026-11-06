# Baltam IR

当前版本：**0.1.3**。更新内容见 [更新日志](release_notes/version.md)。

将 M 源码转换为 Non-SSA IR，为后续 SSA 优化与解释执行提供基础。目前支持的语法范围见 [M 语法与 IR 设计](docs/M语法与IR设计.md)，架构思路见 [新解释器架构](docs/新解释器架构.md)。

## 环境与依赖

- CMake 3.16 或更新版本。
- 支持 C++17 的编译器。
- 同一套兼容版本的 Baltam core、AST、logger SDK，以及 AST 解析器运行时需要的内核和依赖库。
- MatTool 2.0.0 或更新版本，用于 MAT 常量持久化。

默认目录结构：

```text
deps/
├── core/
│   ├── include/
│   └── lib/        # bt_core、bt_ast_base、bt_ast 等
├── logger/
│   └── lib/        # bt_logger
└── mat_tool/
    ├── include/
    └── lib/        # MatTool 及其 CMake 配置
```

CMake 使用 `find_library` 查找库，不硬编码 `.so` 等平台后缀。SDK 本身仍需要提供与目标系统和编译器兼容的库。

## 配置与构建

在项目根目录运行：

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --target baltam_ir baltam_opt
```

项目的主要构建目标：

| 目标 | 作用 |
| --- | --- |
| `baltam_ir_core` | 静态库，包含 IR 数据结构、缓存和 AST → IR 构建 |
| `baltam_ir` | 命令行程序，将 M 文件转换为 IR |
| `baltam_opt` | 生成 `opt`，通过 pass 管线将 IR 转换为另一份 IR |

构建 `baltam_ir` 会自动构建 `baltam_ir_core`。

可选配置：

| 选项 | 默认值 | 说明 |
| --- | --- | --- |
| `BALTAM_AST_ROOT` | `deps/core` | core 与 AST SDK 根目录，包含 include 和 lib |
| `BALTAM_LOGGER_ROOT` | `deps/logger` | logger SDK 根目录 |
| `BALTAM_RUNTIME_LIBRARY_DIRS` | 空 | 额外的构建时运行库搜索目录，用分号分隔 |
| `BALTAM_BUILD_UTILS` | `ON` | 构建命令行程序 |

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

`baltam_ir` 负责 M 文件 → IR；`opt` 负责 IR → IR。

```bash
./build/baltam_ir test/syntax/if_else.m
./build/baltam_ir /absolute/path/to/function.m
./build/baltam_ir test/syntax/if_else.m -o /tmp/if_else.ir
./build/opt -S -passes='' /tmp/if_else.ir -o /tmp/copied.ir
```

输入、输出均支持相对路径，相对路径以当前工作目录为准。不指定 `-o` 时输出到终端；指定时写入文件并覆盖已有内容，输出不能与输入为同一个文件。

输出包括模块路径、函数或脚本、当前 IR 阶段、基本块与指令，槽位直接显示为 `slot_变量名`。例如：

```text
; ModulePath = '/absolute/path/to/if_else.m'

define @if_else() {
; stage: Non-SSA
...
}
```

脚本主体表示为 `script`，局部函数随后输出；嵌套函数以 `nested in @函数路径` 记录词法父函数，并保留便于阅读的注释。文件末尾的 `constants` 表统一记录常量数据。尚未支持的语法会报告错误。

可选：将可执行文件所在目录加入当前终端的 `PATH`，之后直接使用 `baltam_ir`：

```bash
export PATH="$PWD/build:$PATH"
baltam_ir test/syntax/if_else.m
```

## opt

CMake 目标为 `baltam_opt`，可执行文件为 `opt`。命令行参考 LLVM opt 的 `opt [options] [filename]` 形式，处理的是 Baltam IR，不接受 LLVM IR 或 LLVM bitcode。

```bash
./build/opt -S -passes='' constant.ir -o copied.ir
./build/opt constant.ir -o -
./build/opt -S < constant.ir
./build/opt -disable-output constant.ir
./build/opt -help
./build/opt -print-passes
```

省略输入或输入为 `-` 时从标准输入读取；省略 `-o` 或输出为 `-` 时写到标准输出。`-S` 表示文本输出，目前这也是唯一输出格式。`-passes=` 与 `-passes ''` 均表示空管线。已支持 `mem2reg` 和 `function(mem2reg)`，未知 pass（例如尚未实现的 `simplifycfg`）会报错。

```bash
./build/opt -S -passes=mem2reg input.ir -o ssa.ir
./build/opt -S -passes='function(mem2reg)' input.ir -o ssa.ir
```

mem2reg 从 load 按需查询块内及前驱定义，使用缓存和占位 PHI 处理分支与循环，先替换 load 再将 store 转为独立赋值 copy。可能未定义的变量保留原位置的 `load` 读取语义；显式屏障后的值通过 `load` 按需取得。转换在副本上完成，保留原 Non-SSA 和恢复映射。SSA 参数从签名寄存器进入，ret 直接引用返回寄存器；全局、持久及共享槽位等仍保守保留；未受同块紧邻后继屏障保护的动态操作使函数保持 Non-SSA。自动捕获分析尚未接入，嵌套函数及其父函数仍暂不转换。详细流程见 [mem2reg 设计](docs/mem2reg设计.md)。工作区访问、定义检查和回退的运行时执行仍待接入。

pass 管理采用函数管线、按需分析缓存和 `PreservedAnalyses` 返回值；版本切换或不保留分析时，清除 CFG 和支配结果。当前实现位于 `src/Transform`，并未链接 LLVM。

`-disable-output` 仍读取并恢复模块，包括 MAT 常量，但不生成任何输出文件。输入或参数错误返回非零退出码，诊断写到标准错误。

## MAT 配套文件

推荐用 `-o` 保存包含 MAT 常量的模块，程序会在输出旁生成 `<输出文件名>.constants/`，并在 IR 中记录相对路径。文件输入中的 MAT 路径始终相对于输入 IR 所在目录解析，与当前工作目录无关。

从标准输入读取涉及 MAT 的 IR 时，使用 `-resource-dir` 指定输入资源基准目录。写到标准输出且需要 MAT 时，也必须显式提供资源目录，编码器在该目录的 `constants/` 下创建或复用常量文件。

```bash
./build/opt -resource-dir=/path/to/input -disable-output < input.ir
./build/opt input.ir -o output.ir
./build/opt input.ir -resource-dir=/path/to/export -o -
```

文件输入配合 `-resource-dir` 时，该选项只决定标准输出所配套的资源目录，不改变输入 MAT 文件的解析位置。仅含内联常量的标准输入和标准输出无需该选项。

若系统无法找到 SDK 或 MatTool 的间接动态库，需要在运行环境的动态库搜索路径中包含 `deps/core/lib`、`deps/logger/lib` 和 `deps/mat_tool/lib` 及 SDK 所需的其他目录。

参考：[LLVM opt 命令行文档](https://llvm.org/docs/CommandGuide/opt.html)。


## 样例与代码接口

`test/syntax` 保留 M 文件样例。配置好运行时库后，可以手动生成并优化 IR：

```bash
./build/baltam_ir test/syntax/if_else.m -o /tmp/if_else.ir
./build/opt -passes=mem2reg /tmp/if_else.ir -o /tmp/if_else.ssa.ir
```

C++ 调用方链接 `baltam_ir_core`，通过 `IRBuilder::build_file(path)` 获取共享的 `IRModule`。同一进程内会复用有效缓存；跨世界的文件修改检查由缓存处理，运行时需要在合适边界推进世界计数。

模块持久化接口为 `write_ir(module, resource_directory)`、`write_ir_file(module, path)`、`read_ir_file(path)` 和 `read_ir(text, resource_directory)`，与模块打印器 `IRPrinter` 统一定义在 `ir/ir_io.h`。格式和接口说明见 [IR 持久化](docs/IR持久化.md)。

运行时调用目标缓存的设计见 [函数调用点缓存设计](docs/函数调用点缓存设计.md)。

IR 生成与回归验证的分层方案见 [IR 生成正确性测试方案](docs/IR生成正确性测试方案.md)。

通用控制流与支配分析设计见 [支配分析设计](docs/支配分析设计.md)，槽位提升的算法与限制见 [mem2reg 设计](docs/mem2reg设计.md)。

## 代码格式化

根目录的 `format-with-clang.sh` 使用 `.clang-format`，默认调用 `clang-format-11`，覆盖 `src`、`test` 和 `utils`，不处理依赖及构建目录。也可传入指定文件；文件路径相对于项目根目录。

```bash
./format-with-clang.sh
./format-with-clang.sh --check
./format-with-clang.sh src/Transform/mem2reg.cpp
cmake --build build --target format
cmake --build build --target format-check
```

如需在每次构建前自动格式化，显式开启：

```bash
cmake -S . -B build -DBALTAM_FORMAT_BEFORE_BUILD=ON
cmake --build build
```

默认关闭自动修改，避免普通构建改变源码；用 `-DBALTAM_FORMAT_BEFORE_BUILD=OFF` 关闭已启用的开关。CI 可执行 `format-check`，不合规时返回失败，不自动改文件。格式化工具不负责翻译注释或自动补充分隔函数的空行，中文注释和函数间空行仍需人工保持。
