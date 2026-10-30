# IR 持久化

本文说明模块、指令及元数据的持久化格式。持久化应从模块入口调用，保存当前阶段的 IR，并在读取时恢复对象所有权、值引用、控制流和函数绑定关系。

模块打印器 `IRPrinter` 和读写接口统一位于 `src/ir/ir_io.h`，实现位于 `src/ir/ir_io.cpp`；常量编解码接口位于 `src/ir/ir_constant.h`，实现位于 `src/ir/ir_constant.cpp`。单独调用 `ConstInst::to_string()` 仍是调试摘要。

实现状态：模块和常量读写已接入；本文的调用元数据格式已确定，但尚未接入打印器和读取器，当前代码仍使用行内 `[dispatch=...,target=@...]` 属性。调用元数据示例表示目标格式，当前 `opt` 尚不能读取该格式。

## 模块读写

函数体和脚本主体中的注释及基本块标签顶格输出，基本块内指令缩进 4 个空格。代码单元与末尾元数据块之间、不同元数据块之间使用一个空行分隔；元数据条目缩进 4 个空格。

`IRPrinter` 负责遍历模块、函数、基本块和指令，持有本次输出的 `ConstantEncoder`，统一分配常量标签并输出元数据。`IRModule` 保存 IR 数据，不保存编码器或导出目录；函数和基本块的 `to_string()` 无需传递编码器。

```cpp
IRPrinter printer(resource_directory);
auto text = printer.print(module);
```

每次 `print()` 都重置编码状态，可以顺序复用同一个打印器；上一次打印失败不会污染下一次输出。模块的 `to_string(resource_directory)` 是创建打印器并调用 `print()` 的便捷入口，`write_ir_file` 也使用该打印器。单独打印函数或基本块仍用于调试，不生成可独立恢复的完整常量表。

```cpp
#include "ir/ir_io.h"

write_ir_file(module, "example.ir");
auto restored = read_ir_file("example.ir");

// 纯内联常量无需资源目录。涉及 MAT 时显式指定目录。
auto text = module.to_string(resource_directory);
auto from_text = read_ir(text, resource_directory);
```

`write_ir_file` 在 IR 文件末尾输出一个模块级 `constants` 表，并将 MAT 文件放在同目录的 `example.ir.constants/` 内。移动时需将 IR 和该资源目录一起移动。`to_string()` 默认不创建资源文件，遇到需要 MAT 的对象会要求调用方提供资源目录；命令行此时应使用 `-o`。

模块格式保留脚本、函数、嵌套函数、槽位表、当前阶段的基本块和指令；多结果调用、分派类型与同模块静态目标会一并恢复。其他非当前阶段的版本不在本次快照中。不再保存 `module_info`：存在 `ModulePath` 时来源设为 File，否则设为 Memory；有脚本主体时类型设为 Script，仅有函数定义时设为 FunctionFile。IR 文件读取用于观察，不注册到 M 源文件缓存，也不保留原始 Command 来源等当前文本结构不能区分的信息。路径头、stage 和 slot 沿用当前打印格式，读取时识别这些结构性注释；普通注释被忽略。嵌套关系另外使用 `nested in @0.1` 的函数索引路径，避免同名函数歧义。

`baltam_ir` 将 M 文件转换为 IR，`opt` 读取 IR 并输出转换后的 IR：

```bash
./build/baltam_ir input.m -o example.ir
./build/opt -S -passes='' example.ir -o copied.ir
```

读取器支持当前已实现文本打印的指令；原来就未提供文本表示的 MagicColon/MagicEnd 不在支持范围内。旧的无常量表调试输出不能作为新格式直接读取。

## 常量指令与元数据

常量指令格式为 `%x = const !cx (value): type shape`，其中 `(value)` 可省略。所有常量在文件末尾的模块级 `constants` 表中定义。

### 编解码接口

```cpp
ConstantEncoder encoder(ir_path.parent_path());
std::vector<std::string> lines;
for (const ConstInst* instruction : constants)
    lines.push_back(encoder.encode(*instruction));
const auto table = encoder.metadata();
// 将 lines 放回各自基本块位置，table 放在 IR 文件末尾。

ConstantDecoder decoder(table, lines, ir_path.parent_path());
auto restored = decoder.decode(lines.front());
```

每个模块使用一个 encoder。decoder 构造时需要传入模块全部常量指令，先收集类型和形状，再恢复元数据。共享标签只创建一个只读对象。

```text
%0 = const !c0 (1.25): double 1x1
%1 = const !c0 (1.25): double 1x1
%2 = const !c1: double 2x3x4
constants {
    !c0 = inline [0x3ff4000000000000]
    !c1 = mat "constants/c1.mat"
}
```

展示值可省略，不参与恢复或去重。简单数值标量默认输出十进制展示。类型和完整维度由指令声明，同一标签的所有声明必须一致。

数值、逻辑、字符和字符串数组在元素数小于 10 时内联，其他情况使用 MAT。复数属于数值类型。内联列表按列优先线性顺序，元素用逗号分隔；空数组是 `[]`。整数为十进制，逻辑为 true/false，字符使用单引号，字符串使用双引号。转义支持反斜杠、引号、换行、回车、制表符和固定两位 `\xNN` 字节。

single/double 内联浮点分别是 8/16 位十六进制原始位模式，最高有效位在前，机器端序无关。复数为 `(实部位模式, 虚部位模式)`。浮点不通过数值转换恢复，保留负零与 NaN payload。

去重键由稳定类型名称、完整形状和规范化元素内容组成。当前以完整键进行哈希表查找，避免仅凭哈希误合并。元胞及结构体递归比较内容，最大嵌套深度为 64；其他未支持对象明确报错。

## MAT 行为

MAT 路径相对于资源目录。每个文件固定一个名为 `value` 的变量。写入使用压缩模式，文件名包含标签和内容键的哈希。同名文件先读取并检查内容，完全一致时复用，否则报错，避免重复导出时覆盖其他常量。哈希不作为相等的唯一依据，最终仍比较完整内容。每次 MAT 写入后立即读取并校验类型、完整形状和规范化内容，检测到改变时拒绝编码并移除该文件。

当前 MatTool 的非 ASCII 字符串往返有已知问题，因此相关对象可能被校验拒绝；不静默保存错误内容。结构体、元胞可以使用 MAT，但是否能保存取决于后端的往返校验结果。编码整个模块不是事务性的：如果后面的常量失败，前面成功生成的 MAT 文件仍保留。

所有解码输入按可信编译产物处理；MAT 分配由第三方库负责，本接口未提供不可信文件的沙箱或内存配额。错误通过异常报告，调用方应补充模块文件位置。

## 调用指令与元数据

本节为已确定、待接入代码的目标格式。调用指令记录结果、函数名、实参和元数据引用；分派方式及同模块静态目标放入文件末尾的模块级 `calls` 表。

### 指令格式

```text
call 函数名(实参列表), !callN
%结果 = call 函数名(实参列表), !callN
[%结果0, %结果1] = call 函数名(实参列表), !callN
```

结果列表按返回顺序排列，实参列表按传入顺序排列，值编号仅在所属代码单元的当前阶段中有效。实参可以为空，结果可以是零个、一个或多个。每条 `CallInst` 都引用一个调用元数据标签，包括默认的动态分派。

```text
call disp(%0), !call0
%1 = call foo(%0), !call1
[%2, %3] = call inner(%1), !call2

calls {
    !call0 = { dispatch=builtin }
    !call1 = { dispatch=dynamic }
    !call2 = { dispatch=mfunction, target=@0.1 }
}
```

### 元数据字段

| 字段 | 是否必需 | 含义 |
| --- | --- | --- |
| `dispatch` | 是 | 调用分派方式 |
| `target` | 否 | 同模块中静态绑定的 M 函数路径 |

`dispatch` 使用以下稳定名称，不直接记录内部枚举编号：

| 名称 | 含义 |
| --- | --- |
| `dynamic` | 由运行时解析调用目标 |
| `mfunction` | M 函数调用 |
| `builtin` | 内置函数调用 |
| `internal` | 内部函数调用 |
| `plugin` | 插件函数调用 |

`target` 仅用于 `mfunction`，不能保存进程地址。`@0` 表示模块中第 0 个顶层函数；`@0.1` 表示该函数的第 1 个嵌套函数；`@0.1.2` 表示继续进入第 2 个嵌套函数。所有索引从零开始，按模块和嵌套函数列表的保存顺序解释。顶层局部函数直接通过 `@N` 定位。

外部 M 函数和可替换的插件函数保留动态解析，不保存跨模块静态指针。函数名仍在指令上提供可读名称；`target` 是静态绑定的恢复依据。

### 标签复用与恢复

`!callN` 在模块内唯一，与常量标签 `!cN` 分属不同类别。按照首次遇到调用属性的顺序分配标签；相同的 `dispatch` 和 `target` 共用标签。函数名、实参、结果不参与调用属性去重。

共用调用元数据只代表分派及绑定信息一致，不表示调用节点或调用结果可以合并。读取后，每条调用仍是独立的 `CallInst`。

恢复流程：

1. 读取调用指令，保存结果、函数名、实参和调用标签。
2. 解析 `calls` 表，恢复各标签的分派信息和可选目标路径。
3. 创建完整的顶层函数及嵌套函数树，再将目标路径解析为 `FunctionUnit*`。
4. 回填调用目标，并为每个结果建立值表条目，记录所属指令和结果序号。

缺失或重复标签、未知字段、重复字段、未知分派名称、非法目标路径和分派类型与目标不匹配均应报错，不能静默退回动态解析。

## 元数据表组织

目标格式将元数据集中放在全部代码单元之后，依次输出 `constants` 和 `calls`。共享标签覆盖整个模块，不按函数重新编号。

```text
define @example() {
bb0:
    %0 = const !c0 (1.25): double 1x1
    %1 = call foo(%0), !call0
    ret %1
}

constants {
    !c0 = inline [0x3ff4000000000000]
}

calls {
    !call0 = { dispatch=dynamic }
}
```

该示例包含尚未接入代码的调用元数据。常量类型与形状保存在引用指令上，调用分派属性保存在 `calls` 表中；两类元数据采用各自的恢复规则。

## 手动验证

在项目根目录构建，并通过命令行执行 M 文件到 IR、IR 到 IR 的转换：

```bash
cmake -S . -B build -DBALTAM_BUILD_UTILS=ON
cmake --build build --target baltam_ir baltam_opt -j2
./build/baltam_ir test/syntax/constant.m -o build/constant.ir
./build/opt -S -passes='' build/constant.ir -o build/constant.restored.ir
diff -u build/constant.ir build/constant.restored.ir
```

运行 M 文件转换需要匹配的 AST 和内核动态库。包含 MAT 常量时，还需校验恢复对象的类型、完整形状和元素位模式；仅比较重新打印的 IR 文本不足以证明数据完全一致。
