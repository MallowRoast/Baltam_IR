# Baltam IR

当前版本：**0.2.0**。更新内容见 [更新日志](release_notes/version.md)。

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

省略输入或输入为 `-` 时从标准输入读取；省略 `-o` 或输出为 `-` 时写到标准输出。`-S` 表示文本输出，目前这也是唯一输出格式。`-passes=` 与 `-passes ''` 均表示空管线。已支持 `mem2reg`、`simplifycfg`、`load-store-elim` 以及 `function(...)` 函数管线，未知 pass 会报错。

```bash
./build/opt -S -passes=mem2reg input.ir -o ssa.ir
./build/opt -S -passes='function(mem2reg)' input.ir -o ssa.ir
```

`simplifycfg` 当前仅处理 Non-SSA：使用 `as_bool()` 折叠可确定的常量条件分支，删除入口不可达区域，修复 PHI 输入并迭代消除平凡 PHI；将以 `goto` 连接、目标唯一前驱为来源块的直线块合并，消除目标块的单前驱 PHI，并更新后继 PHI 的来源块。块合并结束后沿支配树进行常量去重，复用同块较早或支配当前块的、类型／完整形状／精确数据相同的定义；不跨互斥兄弟分支替换，也不移动指令。普通指令顺序和工作区屏障保持不变；Undefined 或无外部来源的 PHI 保守跳过，入口块和可达自循环不被删除。已有 SSA 版本时保留原 Non-SSA 回退基线。同样处理脚本和命令单元，不进行空块绕过；条件转换失败时保留原 branch，不提前抛错。

```bash
./build/opt -S -passes='simplifycfg,mem2reg' input.ir -o ssa.ir
```

`load-store-elim` 是 Non-SSA 专用的槽位读写优化：消除重复 load，将已知 store 的值转发给后续 load，并删除同值重复 store 或 load 后原样写回的 store。转发后若元胞构造的元素均为具体常量，则将其折叠为 ConstInst，并清理无使用的元素常量。保留首次 load；同一局部槽位的具体值写入若在只间隔常量指令的范围内被覆盖，则删除旧 store，并清理无使用的常量。可能改写槽位的指令（包括调用、动态操作、声明及屏障）清空已知值。PHI 的潜在 Undefined、MaybePack 和带 SSA 来源的 load 均保守处理；全局、持久、共享槽位以及嵌套函数暂不优化。跨块可用值在固定 IR 上用工作列表求解：未到达块为格底，已到达的槽位为 Known(ValueId) 或 Unknown；load 固定生成自己的结果。收敛后才进行替换、删除。跨块转发要求所有可达前驱给出相同 ValueId，且定义严格支配目标块；不创建 PHI，循环回边不完整或值不一致时保留读取。跨块覆盖写入消除要求所有后继路径在读取、可能抛错或工作区观察前覆盖旧值；只跨越常量、PHI、goto 和已证明为具体 logical 1x1 的 branch，返回位置保留最终写入。纯循环不能自证最终覆盖。变换迭代到稳定后再发布版本。已有 SSA 版本时不修改回退基线。

```bash
./build/opt -S -passes='simplifycfg,load-store-elim,mem2reg' input.ir -o ssa.ir
```

M 函数在 lowering 前收集当前函数体的变量槽位（不包含嵌套函数的局部变量），因此后面的赋值或声明也会影响前面名字的分类。圆括号表达式 `A(...)` 若 A 没有 slot，则生成 CallInst；有 slot 时按变量值处理。静态变量表仅用于圆括号消歧，不改变 `a.b` 的原有点号访问判定。脚本不使用函数的静态变量收集规则，带结果的未绑定名字仍生成 ApplyInst。

`intrinsic.logical_scalar` 的结果固定标记为 logical 1x1。Builder 以及 Non-SSA 读写优化在输入为具体常量时调用 `ba_obj::as_bool()` 尝试折叠；转换抛出异常时保留原指令，不提前抛错。特殊索引冒号和未知输入不折叠。

for 循环的内部计数器由 Builder 直接生成 double 1x1 PHI，汇合初值与 latch 递增值，不分配临时 slot 或生成计数器 load/store。用户循环变量仍使用 slot；continue 经过 latch，break 直接退出循环。

IRBuilder 在 lowering 过程中直接复用同一基本块内类型、完整形状和精确数据一致的常量（包括特殊索引冒号及折叠后的元胞常量），不生成重复的 ConstInst。lowering 完成后移除没有 use 的常量指令，包括元胞折叠留下的元素常量。浮点比较保留正负零和 NaN 位模式差异，复用容器常量的递归判等规则，不产生 MAT 文件。缓存按基本块隔离，不做跨块复用或指令提升；脚本、函数和嵌套函数均通过同一构建入口处理。没有独立的常量复用 pass，文本 IR 读入不会触发 Builder 的常量复用。

mem2reg 从 load 按需查询块内及前驱定义，使用缓存和占位 PHI 处理分支与循环，替换 load 的使用，并在原用户 store 位置保留 record 绑定记录。可能未定义的变量保留原位置的 `load` 读取语义；显式屏障后的值通过 `load` 按需取得。转换在副本上完成，保留原 Non-SSA 和恢复映射。SSA 参数从签名寄存器进入，ret 直接引用返回寄存器；全局、持久及共享槽位等仍保守保留；普通运算、重载和调用不阻止局部槽位提升；显式工作区变化仍按 workspace_barrier 处理。自动捕获分析尚未接入，嵌套函数及其父函数仍暂不转换。详细流程见 [mem2reg 设计](docs/mem2reg设计.md)。工作区访问、定义检查和回退的运行时执行仍待接入。

Non-SSA 的控制流简化、基于支配关系的跨块常量去重、跨块冗余读写消除和无使用常量清理适用于函数及脚本／命令单元；Builder 常量复用也适用于两者。mem2reg 仍只处理函数。

pass 管理采用函数管线、按需分析缓存和 `PreservedAnalyses` 返回值；版本切换或不保留分析时，清除 CFG 和支配结果。当前实现位于 `src/Transform`，并未链接 LLVM。

`-disable-output` 仍读取并恢复模块，包括 MAT 常量，但不生成任何输出文件。输入或参数错误返回非零退出码，诊断写到标准错误。

## MAT 配套文件

推荐用 `-o` 保存包含 MAT 常量的模块，程序会在输出旁生成 `<输出文件名>.constants/`，文件名按元数据标签命名（例如 `!c1` 对应 `c1.mat`），并在 IR 中记录绝对路径。读取旧 IR 时仍兼容相对于输入 IR 所在目录的 MAT 路径。

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
