# IR 持久化

本文说明模块、指令及元数据的持久化格式。持久化应从模块入口调用，保存当前阶段的 IR，并在读取时恢复对象所有权、值引用、控制流和函数绑定关系。

模块打印器 `IRPrinter` 和读写接口统一位于 `src/ir/ir_io.h`，实现位于 `src/ir/ir_io.cpp`；常量编解码接口位于 `src/ir/ir_constant.h`，实现位于 `src/ir/ir_constant.cpp`。IR 数据结构不提供文本输出成员函数，格式化统一由 ir_io 实现。

实现状态：模块和常量读写已接入；调用节点按参考项目仅保留同模块静态目标，使用行内 `[target=@...]` 属性；不再持久化节点分派类别或调用属性表。

Non-SSA 返回指令使用无操作数的 `ret`，结果由声明的返回槽位提供；SSA 使用 `ret %N, ...` 直接返回寄存器，数量与签名一致。

## 模块读写

槽位引用统一使用 `slot_变量名`，例如 `load slot_x`、`store slot_y %0`，函数签名为 `define @f(slot_x) -> (slot_y)`。不再输出 slot 注释或独立槽位表。同一代码单元中的同名变量只有一个槽位，内存仍保存 SlotId。读取时先登记参数和返回槽位，再按指令首次出现的名称分配其余槽位；恢复后的数字编号无需与原内存一致。已提升且不再被引用的局部槽位自然不出现在文本中。

内部槽位引用使用 `internal_名称`，例如 `load internal_tmp`，读取时显式恢复 `SlotInfo::is_internal`。用户槽位和内部槽位分别登记，即使名称相同也使用不同的 SlotId；不根据 `for.index.*` 或 `short.*` 名称猜测内部属性。内部槽位不属于用户工作区，不受 workspace_barrier 影响，提升后不生成 record。 SlotInfo 可保存 type 和 shape；显式内部槽位的 load 和提升后的 PHI 继承该信息。循环计数器由 Builder 直接生成 PHI，不分配内部 slot。读取器从内部 load 的结果标注恢复槽位约束，不按调试名称猜测类型。

函数体和脚本主体中的注释及基本块标签顶格输出，基本块内指令缩进 4 个空格。函数的 `; stage:`、`; type:` 和可选嵌套关系注释连续输出，注释组与首个基本块之间保留一个空行。代码单元与末尾元数据块之间、不同元数据块之间使用一个空行分隔；元数据条目缩进 4 个空格。

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

模块格式保留脚本、函数、嵌套函数、槽位表、当前阶段的基本块和指令；多结果调用、分派类型与同模块静态目标会一并恢复。其他非当前阶段的版本不在本次快照中。不再保存 `module_info`：存在 `ModulePath` 时来源设为 File，否则设为 Memory；有脚本主体时类型设为 Script，仅有函数定义时设为 FunctionFile。IR 文件读取用于观察，不注册到 M 源文件缓存，也不保留原始 Command 来源等当前文本结构不能区分的信息。路径头和 stage 通过结构性注释记录，槽位名称直接写在签名与指令中；普通注释被忽略。嵌套关系在函数内 `; type: nested` 下一行使用注释保存完整的词法父函数名路径，例如 `; nested in @outer.inner`，避免不同作用域中同名函数的歧义。读取器兼容旧的函数外索引路径。

`baltam_ir` 将 M 文件转换为 IR，`opt` 读取 IR 并输出转换后的 IR：

```bash
./build/baltam_ir input.m -o example.ir
./build/opt -S -passes='' example.ir -o copied.ir
```

读取器支持当前已实现文本打印的指令；特殊索引冒号使用 `const ":"`，不进入普通常量表；MagicEnd 的文本读写仍待接入。旧的无常量表调试输出不能作为新格式直接读取。

## 寄存器类型与形状

结果定义统一采用 `%N: type shape`，类型与形状各自可省略；两者都未知时不打印冒号。只在定义处标注，引用仍为 `%N`。该规则同时用于 Non-SSA 和 SSA。

```text
%0 = load slot_x
%1: double 1x1 = const !c0 (0)
%2: double = call f(%0)
%3: 2x3 = call g(%0)
%4: double 1x? = call h(%0)
[%5, %6: int64 1x1] = call pair(%0)
```

shape 用 `x` 分隔维度，已知维数中的未知维度使用 `?`。维数未知时省略整个 shape；不输出 `unknown`、`*` 或 `tensor`。多结果定义使用方括号，每个结果独立标注，不使用未知占位符。

SSA 参数使用 `%N: type shape [name=源码变量名]`，未知信息同样省略。常量具有完整类型和形状；元胞构造也必须保留已知的 `cell 行数x列数`，用于恢复构造参数。ForeachInit 只产生一个结果，固定为 `double 1x1`。

类型和形状保存在 Value 中，克隆与编号压缩保留这些事实，读取器恢复标注。未知静态信息不代表运行时 Undefined。旧尾部常量类型格式及旧 SSA 参数名称格式需重新生成。

## 常量指令与元数据

常量指令格式为 `%x: type shape = const !cx (value)`，其中 `(value)` 可省略。所有常量在文件末尾的模块级 `constants` 表中定义。

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
%0: double 1x1 = const !c0 (1.25)
%1: double 1x1 = const !c0 (1.25)
%2: double 2x3x4 = const !c1
constants {
    !c0 = inline [0x3ff4000000000000]
    !c1 = mat "/absolute/path/constants/c1.mat"
}
```

展示值可省略，不参与恢复或去重。简单数值标量默认输出十进制展示，逻辑标量输出 `(true)` 或 `(false)`。类型和完整维度由指令声明，同一标签的所有声明必须一致。

数值、逻辑、字符和字符串数组在元素数小于 10 时内联，其他情况使用 MAT。复数属于数值类型。内联列表按列优先线性顺序，元素用逗号分隔；空数组是 `[]`。整数为十进制，逻辑为 true/false，字符向量直接写为单引号文本（例如 `inline 'aaa'`），字符矩阵仍使用逐元素列表；读取时兼容旧字符向量列表。字符串使用双引号。转义支持反斜杠、引号、换行、回车、制表符和固定两位 `\xNN` 字节。

single/double 内联浮点分别是 8/16 位十六进制原始位模式，最高有效位在前，机器端序无关。复数为 `(实部位模式, 虚部位模式)`。浮点不通过数值转换恢复，保留负零与 NaN payload。

去重键由稳定类型名称、完整形状和规范化元素内容组成。当前以完整键进行哈希表查找，避免仅凭哈希误合并。元胞及结构体递归比较内容，最大嵌套深度为 64；其他未支持对象明确报错。

## MAT 行为

输出 MAT 路径为绝对路径，文件名直接对应元数据标签，例如 `!c1` 对应 `c1.mat`。读取时仍兼容旧的相对路径。每个文件固定一个名为 `value` 的变量，写入使用压缩模式。先写临时文件并读取校验类型、完整形状和规范化内容，通过后替换目标文件；重复导出时按本次标签更新内容。校验失败时删除临时文件并保留已有目标文件。

当前 MatTool 的非 ASCII 字符串往返有已知问题，因此相关对象可能被校验拒绝；不静默保存错误内容。结构体、元胞可以使用 MAT，但是否能保存取决于后端的往返校验结果。编码整个模块不是事务性的：如果后面的常量失败，前面成功生成的 MAT 文件仍保留。

所有解码输入按可信编译产物处理；MAT 分配由第三方库负责，本接口未提供不可信文件的沙箱或内存配额。错误通过异常报告，调用方应补充模块文件位置。

## 调用指令

```text
call disp(%0)
%1 = call foo(%0)
[%2, %3] = call inner(%1) [target=@0.1]
```

CallInst 保存函数名、已求值实参、请求的结果列表及可选的同模块词法目标。节点不保存 dispatch_type，不按名字预先固定外部函数、内置函数或插件分派。

`target` 使用函数路径而非进程地址：`@0` 表示模块第一个函数，`@0.1` 表示它的第二个嵌套函数。读取后恢复为同模块 FunctionUnit 的非拥有指针。动态调用省略该属性，不再输出 calls 元数据表。

## 循环初始化与特殊索引常量

```text
%0: double 1x1 = intrinsic.foreach_init %iterable
%1 = const ":"
%2 = intrinsic.gt_double %index, %0
%3 = paren_ref %iterable, %1, %index
%4 = intrinsic.add_double %index, %one
```

ForeachInit 接收已经求值的迭代源，只返回 double 标量上限，不再产生 state。其运行时语义概括为双输出 size、对第二输出执行 double 转换，再取得首元素；空结果返回 0，转换失败在取值前抛错。NaN/Inf 等边界须由后续运行时根据 MATLAB 实测处理。

内部下标也为 double，GtDouble 专门比较 double 下标与 double 上限，不触发用户 gt 分派。初始化可能执行重载 size/double，ParenReferenceInst 可能执行重载 subsref。按 SSA 的常规调用假设，它们不修改当前工作区，不自动生成 workspace_barrier；实际修改父工作区时走回退协议。内部下标不受屏障影响。

逐轮取值直接使用原迭代源和两个索引：特殊冒号及当前下标。原表达式只求值一次，循环体重新绑定源变量不会替换该寄存器。foreach_iterate 已移除，旧的双结果 foreach_init 和 int64_gt 文本需重新生成；多结果调用仍使用方括号。

单独索引冒号为特殊 ConstInst，与普通 string 标量 `":"` 不同，不引用 !c 元数据。当前以独立构造接口及空 value 标记这个类别，实际运行时对象留待执行层实现；普通常量构造拒绝空对象。MagicColonInst 已移除。

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

PHI 可用于 Non-SSA 和 SSA，连续位于基本块开头。Non-SSA 中用于汇合短路表达式等寄存器结果，用户变量仍通过 slot 读写；mem2reg 保留已有 PHI，并为提升的 slot 补充 PHI。每个前驱块恰好有一个输入，`Undefined` 对应 `std::nullopt`，不占用值编号。普通输入使用 `ValueId`，允许循环回边前向引用；未定义的值编号仍是错误。读取时恢复前驱块指针、输入和值表结果，并检查重复、遗漏及非前驱输入。

构造期间允许输入列表暂时为空，打印完整 IR 时必须补齐。同一块中的 PHI 按实际进入的前驱并行取值，不允许前一个 PHI 的结果覆盖影响后一个 PHI 的输入。PHI 不改写槽位，也不是终结指令；运行时传递 Undefined 状态及变量读取检查尚待执行器实现。

## 变量绑定

```text
record slot_x, %1
record slot_y, %1
```

多个变量可以直接引用同一个 ValueId，不需要 copy 指令。计算与绑定分开表达，变量是否赋值及赋值发生的位置由 record 保存。

RecordInst 静态保存目标 SlotId 和一个来源 Use，没有结果，文本为 `record slot_x, %N`。它在执行位置提交已提升局部变量的运行时绑定，不复制数据、不执行真实 slot 存储写入；全局、持久、共享等保留存储的变量仍用 store。内部槽位不产生 record。

record 仅允许出现在 SSA 阶段。mem2reg 在原用户 store 位置生成 record，可提升参数在入口生成 record。它是可观察的赋值效果，不能因没有结果删除，也不能任意跨分支或工作区操作移动。读取、克隆、值替换及编号压缩都保留其普通 Use。旧 assigned_slot 属性及 copy 指令格式需要重新生成。

Non-SSA 基线身份、跨阶段寄存器映射及运行时状态不写入文本；读取的单阶段 IR 用于观察和变换，不具备原模块的回退信息。

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

SSA 中的 `load slot_x` 读取当前槽位状态，可取得 Undefined，不立即报错、不执行名字消歧或重载。LoadSlotInst 可以携带一个可空的 SSA 来源 Use，文本为 `load slot_x, %N` 或 `load slot_x, Undefined`。这种形式不重新访问工作区，而转发已有来源。mem2reg 对具体来源 ValueId 直接传播并删除该 load，不要求来源必然已定义。显式 Undefined 来源暂保留原读取位置。没有来源操作数的 `load slot_x` 仍读取实际槽位。SlotId 保留变量身份。这两种读取形式都不更新变量赋值来源，也不生成 record。

普通指令实际取得 SSA 值时检查 Undefined，record 必须先取得有效右值再更新目标绑定；PHI 只选择和传递状态，不提前报错。mem2reg 在前驱出口为 PHI 补输入时只读取状态。指令的构造、克隆、文本读写和编号压缩已接入，运行时执行仍待实现。

## SSA 函数参数与返回值

```text
define @f(%0 [name=x], %1 [name=y]) -> 2 {
; stage: Based-SSA

bb0:
    ret %0, %1
}
```

SSA 参数在签名中定义为寄存器，源码变量名通过 `[name=x]` 属性保存，不添加 slot_ 前缀；读取时建立内部 SlotId 关联；读取时直接登记签名参数 Value，不在入口创建参数指令。参数值参与克隆、Use 链及编号压缩，普通参数的赋值关联用于运行时变量来源记录。

箭头后的数字是返回数量，不是返回 slot。ReturnInst 直接保存各返回寄存器的 Use，多条返回指令可以引用不同结果；零返回值使用 `-> 0` 和 `ret`。读取时检查返回操作数数量是否与签名一致。SSA 文本不再通过函数签名创建返回槽位；源码返回变量若被 record/load 等指令引用，仍按其实际名称登记 SlotInfo。

Non-SSA 继续使用 `define @f(slot_x) -> (slot_y)` 和无操作数 ret。SSA 参数、输出不使用旧槽位签名，已有 SSA 文本需要重新生成。

## 字符向量与字符串标量预览

```text
%0: char 1x6 = const !c0 ('x = 2;')
%1: string 1x1 = const !c1 ("hello")
```

字符行向量和列向量使用单引号预览，字符串标量使用双引号预览。空内容也显示引号；引号、反斜杠和非打印字节沿用常量转义规则。预览可以包含括号，不影响指令解析。字符矩阵和非标量字符串数组不增加该预览。

括号中的内容只用于阅读，恢复仍以常量元数据的类型、形状和数据为准，去重和 MAT 存储规则不变。

## SSA 寄存器编号顺序

压缩编号时依次排列参数、返回结果和函数体中的其他结果。参数按签名顺序排列；返回结果按基本块中 ret 的出现顺序及返回操作数顺序排列；其他结果按基本块、指令及指令结果索引排列。

同一个 Value 只分配一个编号。返回值直接引用参数或多个返回位置共用同一结果时，沿用已有编号，不制造额外复制。该排序统一更新指令结果、Use 和跨阶段恢复映射。

## 计算结果与变量赋值分离

```text
%3: double 1x1 = const !c0 (1)
record slot_x, %3
record slot_y, %3
```

常量和 intrinsic 只负责产生值，record 独立表达赋值时机，不再输出 assigned_slot 尾部属性。相同常量元数据、同一个 SSA 结果可以供多个变量绑定引用；绑定必须在各自原赋值位置提交，不能仅因值相同而合并或提前。

## 点号访问的名字操作数

静态成员名使用 `@名字`，动态成员名使用已求值的 `%值编号`。例如 `a.b` 对应 `dot_ref %0 @b`，`a.(b)` 对应 `dot_ref %0 %1`（`%1` 是变量 b 的读取结果）。`@b` 是全局名字标记，不读取工作区变量 b，也不进入值的 use 链。字段赋值使用相同的名字／值操作数格式，例如 `dot_assign %0 @b, %1`。未消歧访问保留点号写法，例如 `dot_access a.b`、`dot_access a.(%1)`。读取器兼容旧的 `%0.b` 和 `%0.(%1)` 写法。

## 基本块标签注释

标签行显示来源角色和当前 CFG 的前驱列表，例如 `bb3: ; for.body; prev = [bb2]`；入口显示 `prev = []`。角色包括 `entry`、`if.then`、`if.else`、`if.end`、`for.preheader`、`for.header`、`for.body`、`for.latch`、`for.end` 等。角色仅供阅读；块合并时连接各来源角色，阶段克隆和文本往返保留角色。`prev` 每次打印时从真实跳转边计算并去重，不信任输入中的旧注释。没有角色信息的旧 IR 使用 `entry` 或 `block` 作为默认描述，旧的不带注释标签仍可读取。

Builder 完成 lowering 后按控制结构的嵌套层次排列基本块：先排列 body 的全部内层块，再排列外层 latch/end。标签统一顶格显示，指令统一缩进四个空格；块排列不改变跳转或 PHI 语义。

## 函数类别注释

函数内 `; stage` 下一行显示 `; type: main / local / method / nested`。函数文件首个顶层函数默认是 main，其余顶层函数及脚本的文件内函数默认是 local；有词法父函数的函数是 nested，其下一行继续显示 `; nested in @outer.inner`。method 由显式函数类型标记，不因函数出现在类文件中就自动将 local 判为 method。当前尚不支持类源码 lowering，但文本 IR 可保存和恢复 method 类型，不输出 class 信息。旧 IR 缺少 type 注释时按现有归属关系推断。
