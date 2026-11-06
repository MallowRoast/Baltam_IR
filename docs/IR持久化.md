# IR 持久化

本文说明模块、指令及元数据的持久化格式。持久化应从模块入口调用，保存当前阶段的 IR，并在读取时恢复对象所有权、值引用、控制流和函数绑定关系。

模块打印器 `IRPrinter` 和读写接口统一位于 `src/ir/ir_io.h`，实现位于 `src/ir/ir_io.cpp`；常量编解码接口位于 `src/ir/ir_constant.h`，实现位于 `src/ir/ir_constant.cpp`。IR 数据结构不提供文本输出成员函数，格式化统一由 ir_io 实现。

实现状态：模块和常量读写已接入；本文的调用元数据格式已确定，但尚未接入打印器和读取器，当前代码仍使用行内 `[dispatch=...,target=@...]` 属性。调用元数据示例表示目标格式，当前 `opt` 尚不能读取该格式。

Non-SSA 返回指令使用无操作数的 `ret`，结果由声明的返回槽位提供；SSA 使用 `ret %N, ...` 直接返回寄存器，数量与签名一致。

## 模块读写

槽位引用统一使用 `slot_变量名`，例如 `load slot_x`、`store slot_y %0`，函数签名为 `define @f(slot_x) -> (slot_y)`。不再输出 slot 注释或独立槽位表。同一代码单元中的同名变量只有一个槽位，内存仍保存 SlotId。读取时先登记参数和返回槽位，再按指令首次出现的名称分配其余槽位；恢复后的数字编号无需与原内存一致。已提升且不再被引用的局部槽位自然不出现在文本中。

函数体和脚本主体中的注释及基本块标签顶格输出，基本块内指令缩进 4 个空格。`; stage:`与首个基本块之间保留一个空行。代码单元与末尾元数据块之间、不同元数据块之间使用一个空行分隔；元数据条目缩进 4 个空格。

`IRPrinter` 负责遍历模块、函数、基本块和指令，持有本次输出的 `ConstantEncoder`，统一分配常量标签并输出元数据。`IRModule`、代码单元、基本块和指令只保存 IR 数据，不提供 `to_string()` 成员函数，也不保存编码器或导出目录。

```cpp
IRPrinter printer(resource_directory);
auto text = printer.print(module);
```

每次 `print()` 都重置编码状态，可以顺序复用同一个打印器；上一次打印失败不会污染下一次输出。非成员接口 `write_ir(module, resource_directory)` 创建打印器并输出完整模块，`write_ir_file` 也使用同一输出路径。不再保留独立的函数、基本块或常量调试摘要格式。

```cpp
#include "ir/ir_io.h"

write_ir_file(module, "example.ir");
auto restored = read_ir_file("example.ir");

// 纯内联常量无需资源目录。涉及 MAT 时显式指定目录。
auto text = write_ir(module, resource_directory);
auto from_text = read_ir(text, resource_directory);
```

`write_ir_file` 在 IR 文件末尾输出一个模块级 `constants` 表，并将 MAT 文件放在同目录的 `example.ir.constants/` 内。移动时需将 IR 和该资源目录一起移动。`write_ir()` 默认不创建资源文件，遇到需要 MAT 的对象会要求调用方提供资源目录；命令行此时应使用 `-o`。

模块格式保留脚本、函数、嵌套函数、槽位表、当前阶段的基本块和指令；多结果调用、分派类型与同模块静态目标会一并恢复。其他非当前阶段的版本不在本次快照中。不再保存 `module_info`：存在 `ModulePath` 时来源设为 File，否则设为 Memory；有脚本主体时类型设为 Script，仅有函数定义时设为 FunctionFile。IR 文件读取用于观察，不注册到 M 源文件缓存，也不保留原始 Command 来源等当前文本结构不能区分的信息。路径头和 stage 通过结构性注释记录，槽位名称直接写在签名与指令中；普通注释被忽略。嵌套关系另外使用 `nested in @0.1` 的函数索引路径，避免同名函数歧义。

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
    ret
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

## PHI 指令

```text
%3 = phi [bb1, %1], [bb2, Undefined]
```

PHI 只用于 SSA 阶段，连续位于基本块开头。每个前驱块恰好有一个输入，`Undefined` 对应 `std::nullopt`，不占用值编号。普通输入使用 `ValueId`，允许循环回边前向引用；未定义的值编号仍是错误。读取时恢复前驱块指针、输入和值表结果，并检查重复、遗漏及非前驱输入。

构造期间允许输入列表暂时为空，打印完整 IR 时必须补齐。PHI 不改写槽位，也不是终结指令；运行时传递 Undefined 状态及变量读取检查尚待执行器实现。

## 独立赋值复制

```text
%4 = copy %1 to slot_x
%5 = copy %1 to slot_y
```

CopyInst 的输入通过 Use 引用，结果拥有独立 Value。可选的 `to slot_变量名` 保存结果的 assigned_slot，表示原变量赋值发生的位置；没有 `to` 时是普通值复制。底层数组无需因此深拷贝，但结果不能只是来源可变寄存器位置的别名。

mem2reg 用带槽位的 copy 替换可提升 store，避免多个变量共用一个赋值身份。读取时恢复结果和槽位关联。Non-SSA 基线身份、跨阶段寄存器映射及运行时状态不写入文本；读取的单阶段 IR 用于观察和变换，不具备原模块的回退信息。

## 工作区屏障

```text
workspace_barrier
```

`workspace_barrier` 无输入、无结果、无槽位列表，也不是终结指令。它表示旧变量绑定失效，已有 SSA 值仍保留原值。工作区访问的运行时协议另行设计，不再设置显式同步指令。

当前已支持指令构造、克隆、打印和读取，mem2reg 已将屏障纳入定义传播。可能修改工作区的操作只有在同块紧邻后继屏障时才按显式保护处理；运行时语义和屏障自动插入尚未接入。

## 槽位声明属性

读取 `global` 和 `persistent` 指令时，指令插入流程同步登记 SlotInfo 的声明标记，无需额外文本字段。标记表示存在声明，不表示该声明已经执行。

共享关系的显式登记接口已实现，但共享引用尚未加入持久化格式；当前不能通过文本往返保存手动登记的 shared_target 和 shared_by_nested，读取器也不会按父子函数同名槽位自动推断共享关系。

## 工作区状态读取与定义检查

```text
%0 = load slot_x
%1 = load slot_x, %0
%2 = load slot_x, Undefined
```

SSA 中的 `load slot_x` 读取当前槽位状态，可取得 Undefined，不立即报错、不执行名字消歧或重载。LoadSlotInst 可以携带一个可空的 SSA 来源 Use，文本为 `load slot_x, %N` 或 `load slot_x, Undefined`。这种形式不重新访问工作区，而在原位置读取来源；来源为 Undefined 时报告变量读取错误。没有来源操作数的 `load slot_x` 仍读取实际槽位。SlotId 保留变量身份。这两种读取形式都不更新变量赋值来源，不设置 assigned_slot。

mem2reg 在原变量读取处保留必要检查，在前驱出口为 PHI 补输入时只读取状态，避免提前报错。指令的构造、克隆、文本读写和编号压缩已接入，运行时执行仍待实现。

## SSA 函数参数与返回值

```text
define @f(%0: x, %1: y) -> 2 {
; stage: Based-SSA

bb0:
    ret %0, %1
}
```

SSA 参数在签名中定义为寄存器，冒号后直接显示源码变量名，不添加 slot_ 前缀；读取时建立内部 SlotId 关联；读取时在入口创建 ParameterInst，打印时不在函数体重复定义。参数值参与克隆、Use 链及编号压缩，普通参数的赋值关联用于运行时变量来源记录。

箭头后的数字是返回数量，不是返回 slot。ReturnInst 直接保存各返回寄存器的 Use，多条返回指令可以引用不同结果；零返回值使用 `-> 0` 和 `ret`。读取时检查返回操作数数量是否与签名一致。SSA 文本不再通过函数签名创建返回槽位；源码返回变量若被 copy/load 等指令引用，仍按其实际名称登记 SlotInfo。

Non-SSA 继续使用 `define @f(slot_x) -> (slot_y)` 和无操作数 ret。SSA 参数、输出不使用旧槽位签名，已有 SSA 文本需要重新生成。

## 字符向量与字符串标量预览

```text
%0 = const !c0 ('x = 2;'): char 1x6
%1 = const !c1 ("hello"): string 1x1
```

字符行向量和列向量使用单引号预览，字符串标量使用双引号预览。空内容也显示引号；引号、反斜杠和非打印字节沿用常量转义规则。预览可以包含括号，不影响指令解析。字符矩阵和非标量字符串数组不增加该预览。

括号中的内容只用于阅读，恢复仍以常量元数据的类型、形状和数据为准，去重和 MAT 存储规则不变。

## SSA 寄存器编号顺序

压缩编号时依次排列参数、返回结果和函数体中的其他结果。参数按签名顺序排列；返回结果按基本块中 ret 的出现顺序及返回操作数顺序排列；其他结果按基本块、指令及指令结果索引排列。

同一个 Value 只分配一个编号。返回值直接引用参数或多个返回位置共用同一结果时，沿用已有编号，不制造额外复制。该排序统一更新指令结果、Use 和跨阶段恢复映射。

## 计算结果直接关联变量赋值

```text
%3 = const !c0 (1): double 1x1 [assigned_slot=slot_x]
```

常量和 intrinsic 可以直接承担紧邻的变量赋值，使用尾部注解保留 assigned_slot，省去额外 copy。读取时恢复该字段；常量标签只描述值本身，不包含赋值目标，因此不同变量仍可以引用同一常量元数据。
