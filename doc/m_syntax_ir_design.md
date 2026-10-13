# M 语法的统一 IR 设计

本文按 M 语法项统一记录 IR 设计。每项分别说明简述、non-SSA IR、SSA IR 和 runtime 结构。此处记录目标设计；当前实现可能尚未采用相同的字段或对象表示。

## 基本表达式

### 常量（数值与字符串字面量）

#### 简述

常量是运行之前已经确定值的量，不限于源码中的字面量。数值字面量、字符向量字面量（如 `'aaa'`）和字符串标量字面量（如 `"aaa"`）属于第一批；后续还可以包括已确定值的矩阵、cell、struct 等。

常量的数据类型和维度是其语义的一部分。这些信息隐式包含在 `value` 指向的 `ba_obj` 中，无需在 `ConstInst` 上重复保存。

#### non-SSA IR

使用 `ConstInst` 表示常量，主要字段为：

```cpp
ValueId result;
std::shared_ptr<const ba_obj> value;
```

`result` 是该指令产生的值；`value` 指向运行前已确定的常量对象。字面量先经 `ConstInst` 产生 `ValueId`，再作为其他指令的操作数使用。

#### SSA IR

继续使用同一个 `ConstInst` 表示常量。常量在 non-SSA 和 SSA 阶段的结构与语义基本一致，不为 SSA 另设常量指令。

#### runtime 结构

`value` 持有不可修改的 `ba_obj`，其中包含常量的值、数据类型和维度。多个 `ConstInst` 或多次执行可以共享常量对象；具体共享粒度及复制时机由 runtime 对象模型确定。

#### 注意事项

赋值或修改共享常量的运行时值时必须写时复制，不能改变 IR 持有的常量或其他共享者的值。

### 水平并置与竖直并置

#### 简述

`[a, b]` 表示水平并置，`[a; b]` 表示竖直并置；`[a b; c d]` 可以先逐行水平并置，再对各行进行竖直并置。运行时可能存在用户自定义的 `horzcat` 或 `vertcat`，因此即使输入在生成 IR 时都是常量，也不能随意把整个并置表达式折叠为常量。非空并置保留为 `ConcatInst`；空 `[]` 由 `ConstInst` 表示。

#### non-SSA IR

`ConcatInst` 的主要字段为：

```cpp
ValueId result;
Axis axis;                 // 类内枚举：Horizontal、Vertical
std::vector<ValueId> items;
bool is_dynamic;
```

`items` 按源码顺序保存输入值；`axis` 区分水平与竖直并置。`is_dynamic` 记录 `items` 中是否存在非常量值：只要有一个输入不是常量，就为 `true`。它用于快速判断是否具备常量折叠的输入条件，不表示运行时函数分派已经确定。

#### SSA IR

与 non-SSA IR 使用相同的 `ConcatInst` 结构和语义。

#### runtime 结构

根据 `axis` 执行水平或竖直并置，并遵守运行时对 `horzcat`、`vertcat` 的函数解析与分派规则。结果是一次并置运算产生的运行时值；具体对象类型及维度由实际输入和被调用的实现决定。

#### 注意事项

只有在 `items` 全部为常量（`is_dynamic == false`），且已确认没有会影响此次分派的用户自定义 `horzcat` 或 `vertcat` 时，才可以进行常量折叠。`is_dynamic == false` 单独不足以保证折叠安全。

### 元胞数组构造

#### 简述

`{1 2}` 构造 1×2 元胞数组，`{a, b; c, d}` 构造 2×2 元胞数组。每个输入值分别放入对应元胞，不进行方括号的水平或竖直并置。空 `{}` 可直接表示为持有空元胞数组的 `ConstInst`。

#### non-SSA IR

非空元胞构造使用 `CellConstructInst`，保存结果、元素及行列形状：

```cpp
ValueId result;
std::vector<ValueId> items; // 按行列位置排列
std::size_t rows;
std::size_t cols;
```

`rows`、`cols` 确定构造结果的二维形状，`items` 保存各位置的输入值。该节点没有 `is_dynamic` 字段；是否全部为常量可直接检查 `items` 的定义。

#### SSA IR

与 non-SSA IR 使用相同的 `CellConstructInst` 结构和语义。

#### runtime 结构

分配指定形状的元胞数组，将 `items` 对应的值放入各元胞。元胞构造不需要通过 `horzcat` 或 `vertcat` 做用户函数分派。

#### 注意事项

如果 `items` 全部是常量，可以直接把整个元胞构造折叠为持有 cell 对象的 `ConstInst`。折叠后的对象仍遵守常量共享与写时赋值规则。

### 冒号表达式

#### 简述

`start:stop` 和 `start:step:stop` 生成序列。两参数形式与显式写出 `step` 的三参数形式应保留区别。冒号语法可能受用户自定义 `colon` 的分派影响，因此生成 IR 时保留 `ColonInst`，不直接改写为普通函数调用或常量。

本节只讨论生成序列的冒号表达式；索引中单独的 `:` 由下文的 `MagicColonInst` 表示。

#### non-SSA IR

`ColonInst` 有五个主要字段：

```cpp
ValueId result;
ValueId start;
std::optional<ValueId> step; // 未提供时为空；与显式写出 1 区分
ValueId stop;
bool needs_runtime_dispatch; // 初始为 true；分派目标确定后可设为 false
```

#### SSA IR

与 non-SSA IR 使用相同的 `ColonInst` 结构和语义。

#### runtime 结构

执行时先按冒号语法的规则解析分派目标，再由相应实现根据 `start`、可选的 `step` 和 `stop` 产生结果。若使用内置数值 `colon`，结果通常是行向量；用户自定义实现的结果由其函数决定。

#### 注意事项

1. **常量折叠：**只有在操作数全部是常量，且已确认没有会影响此次分派的用户自定义 `colon` 时，才能按内置语义进行常量折叠。
2. **操作数形状：**内置数值 `colon` 的标量要求与 MATLAB 版本有关。本机 MATLAB R2022b CLI 实测：`[1 2]:2:5` 得到 `[1 3 5]`，`1:[1 2]:5` 得到 `[1 2 3 4 5]`，`1:2:[5 6]` 得到 `[1 3 5]`；三个位置都接受非标量并取其第一个元素。[MathWorks 的版本记录](https://www.mathworks.com/help/matlab/ref/double.colon.html)说明 R2024a 对非标量操作数发出警告，R2025a 起生成向量时会报错。标量约束不应在 IR 字段类型上强制，以免排除用户自定义 `colon` 的语义或预先固定兼容版本。

### 索引中的单独冒号

#### 简述

`A(:, 2)`、`A(:)` 中单独的 `:` 是索引占位，表示该索引位置的全部元素；它不同于生成序列的 `start:stop`，不调用 `colon`。

#### non-SSA IR

使用 `MagicColonInst`，仅有一个主要字段：

```cpp
ValueId result;
```

`result` 表示供索引指令使用的冒号占位值。例如 `A(:, 2)` 的第一个索引参数引用该 `ValueId`。

#### SSA IR

与 non-SSA IR 使用相同的 `MagicColonInst` 结构和语义。

#### runtime 结构

执行索引操作时，runtime 根据被索引对象和 `MagicColonInst` 结果所在的索引位置，解释“取该位置的全部元素”。占位值本身不独立生成序列。

#### 注意事项

`MagicColonInst` 的结果只能在允许单独冒号的索引上下文使用；它不参与普通 `colon` 函数分派，也不作为普通常量折叠。

### 索引中的 `end`

#### 简述

`A(end)`、`A(1, end)`、`C{end}` 中的 `end` 由所在索引操作解释，不能当作普通名字、常量或普通函数调用。其结果取决于被索引对象、当前索引维度，以及该次索引的参数总数。

#### non-SSA IR

使用 `MagicEndInst`：

```cpp
struct MagicEndContext {
    Operand callee_or_base;
    std::uint32_t dim;
    std::uint32_t nindices;
};

ValueId result;
std::vector<MagicEndContext> candidate_contexts;
```

`candidate_contexts` 按内层到外层排列。单层 `A(end)` 只有一个候选；`A(fun(end))` 在 `fun(...)` 尚未确定是索引还是函数调用时，保留 `fun`、`A` 两层候选。基础 IR 不预先假定 `end` 的结果类型。

`callee_or_base` 的 `Operand` 种类决定候选是否已经绑定，不能把三种情况都当作普通名字重新查找：

| `Operand` 种类 | 含义 |
| --- | --- |
| `InternedString` | 尚未解析的源码名字。若解析为变量，就以该变量为索引对象；若解析为普通函数调用，则该候选不是索引上下文，可继续尝试外层候选。 |
| `Slot` | 已确定是当前函数或 workspace 中的变量。从 slot 取得索引对象并检查绑定状态；已被 `clear` 或未绑定时直接报变量引用错误，不查找同名函数，也不回退到外层候选。 |
| `ValueId` | 已加载或计算出的索引对象值。它已绑定到具体数据流，只需判断该值是否支持索引 `end` 语义；不支持时按该候选报错。 |

#### SSA IR

与 non-SSA IR 使用相同的 `MagicEndInst` 结构和语义，继续保留候选上下文链。

#### runtime 结构

按内层到外层检查候选，选择实际的索引上下文。若所有可跳过的未解析名字候选都不是索引，且没有合法的外层候选，则报缺少索引上下文的错误。普通数组按索引规则计算 `end`；类对象由其索引协议处理 `end(obj, dim, nindices)`。后续可直接解释 `MagicEndInst`，也可在选定合法索引对象后降为类似 `internal.end_index(base, dim, nindices)` 的内部操作；基础 lowering 不提前执行这一步。

#### 注意事项

1. 候选回退仅适用于解析为普通函数调用的未绑定名字；用户路径上的普通 `end.m` 不参与此语法的名字解析。
2. 类对象若重载了 `end` 方法，应按索引协议调用 `end(obj, dim, nindices)`；不能直接用数组维度计算结果，也不能预设结果类型。

## 基本运算

### 一元运算

#### 简述

一元正号 `+x`、一元负号 `-x`、逻辑非 `~x`、非共轭转置 `A.'` 和共轭转置 `A'` 共用 `UnaryInst`，由运算种类保留各自的源码语义。基础 lowering 不预先将动态运算固定为内置函数，因为对应操作可能分派到用户定义的实现。

#### non-SSA IR

目标设计中，`UnaryInst` 有四个主要字段：

```cpp
ValueId result;
UnaryOp op;                 // Uplus、Uminus、LogicalNot、Transpose、Ctranspose
bool needs_runtime_dispatch; // 初始为 true
ValueId operand;
```

`Transpose` 对应 `.'`，`Ctranspose` 对应 `'`。操作数表达式先产生 `ValueId`，一元指令再引用它。

#### SSA IR

与 non-SSA IR 使用相同的 `UnaryInst` 结构和语义。

#### runtime 结构

根据 `op` 和分派状态解析对应的一元运算实现，传入一个操作数并取得一个结果。

#### 注意事项

1. 操作数是常量仍不足以保证可以折叠；还需确认实际分派目标及其可安全求值。
2. 共轭转置 `'` 与非共轭转置 `.'` 语义不同，IR 和后续优化均须保留两者的区别。

### 二元运算

#### 简述

算术运算 `+`、`-`、`*`、`/`、`\`、`^`，逐元素运算 `.*`、`./`、`.\`、`.^`，比较运算 `<`、`<=`、`>`、`>=`、`==`、`~=`，以及两侧都求值的逻辑运算 `&`、`|`，均使用 `BinaryInst`。不同运算种类保留矩阵运算、逐元素运算和比较运算的源码语义。基础 lowering 不预先把动态分派固定为内置实现。短路运算 `&&`、`||` 另由控制流表示。

#### non-SSA IR

目标设计中，`BinaryInst` 有五个主要字段：

```cpp
ValueId result;
BinaryOp op;                 // Add、Sub、Mul、Rdiv、Ldiv、Pow 等
bool needs_runtime_dispatch; // 初始为 true
ValueId lhs;
ValueId rhs;
```

左右操作数表达式先分别产生 `ValueId`，再供二元指令引用。

#### SSA IR

与 non-SSA IR 使用相同的 `BinaryInst` 结构和语义。

#### runtime 结构

先取得左右操作数，再根据 `op` 和分派状态执行对应的二元运算实现，取得一个结果。

#### 注意事项

1. 操作数全部为常量仍不足以保证可以折叠；还需确认实际分派目标及其可安全求值。
2. 比较运算在动态分派尚未确定时，不预设结果一定是逻辑值。
3. 解析到用户定义函数后仍应保留 `BinaryInst` 的源码运算符信息；解析目标的具体存放方式尚未确定。

## 控制流

### `if / elseif / else / end`

#### 简述

条件按源码顺序求值；命中一个分支后执行其语句，跳过后续 `elseif` 条件。所有条件均不成立时，执行 `else`（若存在），否则直接继续执行 `end` 后的语句。

#### non-SSA IR

不设置专用的 `IfInst`，而是使用 CFG。每个条件表达式产生一个 `ValueId`，由 `BranchInst` 选择对应语句块或下一个条件块。没有后续条件时，假边进入 `else` 块或出口块；未以 `return` 等指令终止的分支通过 `GotoInst` 到达出口。分支中的变量读写使用 load/store。

`BranchInst` 是条件块末尾的终结指令，主要字段为：

```cpp
ValueId condition;
BasicBlock* true_target;
BasicBlock* false_target;
```

`condition` 引用已求值的条件；条件成立时进入 `true_target`，否则进入 `false_target`。对 `elseif`，前一条件的 `false_target` 指向下一条件的求值块。

`GotoInst` 是基本块末尾的无条件跳转指令，主要字段为：

```cpp
BasicBlock* target;
```

一个分支的语句执行完毕且没有被 `return` 等指令终止时，用 `GotoInst` 跳到 `if` 的出口块，避免继续执行后续 `elseif` 或 `else` 分支。没有 `else` 且条件为假的路径可由 `BranchInst.false_target` 直接指向出口块。

例如 `if a; x = 1; elseif b; x = 2; else; x = 3; end` 可表示为以下 CFG：

```text
if.cond.0:
  %a = load @a
  branch %a, if.then.0, if.cond.1

if.then.0:
  %one = const 1
  store @x, %one
  goto if.end

if.cond.1:
  %b = load @b
  branch %b, if.then.1, if.else

if.then.1:
  %two = const 2
  store @x, %two
  goto if.end

if.else:
  %three = const 3
  store @x, %three
  goto if.end

if.end:
  // 后续语句
```

`BranchInst` 对条件的真假判断须遵守下文所述的 MATLAB `if` 条件规则。

#### SSA IR

保留相同的条件分支 CFG。若多个分支为同一变量产生不同值，后续 SSA 转换在相应汇合点使用 `phi` 节点；具体转换规则待 SSA 设计确定。

#### runtime 结构

运行时只求值实际到达的条件与语句，并根据条件结果执行相应的 `BranchInst` / `GotoInst` 路径。

#### 注意事项

1. `if` 条件的转换规则不同于短路逻辑运算的标量检查。本机 MATLAB R2022b CLI 实测：`if [1 2]` 为真，`if [1 0]` 与 `if []` 为假，所有元素非零的矩阵为真，`if "x"` 报转换错误。因此不能直接使用短路运算中要求逻辑标量的 `internal.cast(value, Bool)` 规则处理 `if` 条件。
2. 后续可能采用线性 IR。届时 `BranchInst` 的真、假目标和 `GotoInst` 的目标都不应继续保存 `BasicBlock*`，而应改为线性 IR 中可定位的标签或指令位置；上述字段形式仅对应当前基于基本块的 CFG。

### `switch / case / otherwise / end`

#### 简述

`switch` 表达式只求值一次，随后按顺序检查各个 `case`。命中第一个 case 后只执行它的语句；`otherwise` 可选。case 之间没有贯穿执行。

#### non-SSA IR

`switch` 表达式产生 `switch_value : ValueId`，供所有 case 判断块复用，不引入中间 slot。

每个 `case` 生成一个判断块和一个执行块。判断块调用 `internal.switch_match` 后，真边进入当前执行块，假边进入下一个判断块、`otherwise` 或统一出口。执行块正常结束后跳到统一出口。

例如 `switch x; case 1; body1; case 2; body2; otherwise; body3; end` 转为：

```text
current -> switch.case.0

switch.case.0:
  %x = evaluate x
  %case0 = const 1
  %m0 = call internal.switch_match(%x, %case0)
  branch %m0, switch.body.0, switch.case.1

switch.body.0:
  body1
  goto switch.end

switch.case.1:
  %case1 = const 2
  %m1 = call internal.switch_match(%x, %case1)
  branch %m1, switch.body.1, switch.otherwise

switch.body.1:
  body2
  goto switch.end

switch.otherwise:
  body3
  goto switch.end

switch.end:
  // 后续语句
```

#### SSA IR

保留相同的 case 判断链和执行块。`switch_value` 只定义一次，供后续各判断块使用；若多个分支给同一变量赋值，SSA 转换在相应汇合点使用 `phi` 节点。

#### runtime 结构

运行时只求值一次 `switch` 表达式，随后按源码顺序求值所需的 case 值并调用 `internal.switch_match`。命中后不再检查后续 case；执行的分支正常结束后进入统一出口。

#### 注意事项

1. `case {a, b}` 的目标语义是其中任一元素匹配即可进入该 case；该形式尚需补充 lowering。
2. 上述 `BasicBlock*` 跳转目标对应当前 CFG 形式；若以后采用线性 IR，需要改为可定位的标签或指令位置。

### 短路逻辑运算

#### 简述

`a && b` 与 `a || b` 按左操作数决定是否求值右操作数，不能使用两侧都求值的 `BinaryInst`。两个操作数在参与短路判断时都要转换为逻辑标量；整个表达式的结果类型固定为 bool（MATLAB logical scalar）。

#### non-SSA IR

先通过内部函数 `cast(lhs, bool)` 得到左操作数的逻辑标量值，再用 `BranchInst` 选择右侧路径或短路路径。右侧路径仅在被选中时求值右操作数，并通过 `cast(rhs, bool)` 得到逻辑标量；短路路径产生 `ConstInst(false)`（`&&`）或 `ConstInst(true)`（`||`）。内部函数 `cast` 的统一定义见下文“内部函数”。

两条路径将各自的 bool 结果写入同一个临时虚拟寄存器；汇合后从该寄存器读取短路表达式的 bool 类型 `ValueId`。因此 `z = a && b` 可以继续赋值给用户变量 `z`。

以 `z = a && b` 为例，CFG 包含左侧求值块 `entry`、右侧求值块 `sc.rhs`、短路块 `sc.false` 和汇合块 `sc.end`，共四个基本块。`entry` 可以复用当前块，因此只需额外创建三个块。下面的 `%sc` 是临时虚拟寄存器，不是 frame slot：

```text
entry:
  %a = load @a
  %cond = call internal.cast(%a, Bool)
  branch %cond, sc.rhs, sc.false

sc.rhs:
  %b = load @b
  %rhs = call internal.cast(%b, Bool)
  store %sc, %rhs
  goto sc.end

sc.false:
  %false = const false
  store %sc, %false
  goto sc.end

sc.end:
  %result = load %sc                 // bool 类型的 ValueId
  store @z, %result
```

#### SSA IR

保留同样的按需求值 CFG 和内部函数 `cast`。两条路径在汇合块通过 `phi` 节点产生短路表达式的 bool 类型 `ValueId` 结果。

以 `z = a || b` 为例，CFG 同样包含四个基本块；`a` 为真时直接进入短路块：

```text
entry:
  %a = load @a
  %cond = call internal.cast(%a, Bool)
  branch %cond, sc.true, sc.rhs

sc.rhs:
  %b = load @b
  %rhs = call internal.cast(%b, Bool)
  goto sc.end

sc.true:
  %true = const true
  goto sc.end

sc.end:
  %result = phi [sc.rhs: %rhs], [sc.true: %true]  // bool
  store @z, %result
```

#### runtime 结构

内部函数 `cast` 将实际求值的操作数转换为逻辑标量；不能转换为逻辑标量时抛出错误。`BranchInst` 仅进入一条路径，跳过的右操作数不求值。汇合后的结果是 logical scalar。

#### 注意事项

1. 本机 MATLAB R2022b CLI 实测：`[true false] && true`、`true && [true false]`、`[true false] || false` 和 `false || [true false]` 均报 `MATLAB:nonLogicalConditional`；`false && [true false]` 返回 logical scalar `false`，`true || [true false]` 返回 logical scalar `true`。因此必须在各操作数实际求值时检查标量性，不能提前检查被跳过的右侧。
2. 临时虚拟寄存器及其 load/store 需要 IR schema、verifier 和 runtime 支持；汇合后的表达式 `ValueId` 由一次 load 定义，类型固定为 bool。

## 内部函数

内部函数由 IR 或 runtime 直接调用，不参与 M 语言的普通名字查找。此处集中记录它们的参数和语义。

### `cast`

#### 简述

将一个运行时值转换为指定目标类型。目前只定义转换为 bool，用于短路逻辑操作数的标量检查和布尔值生成。

#### non-SSA IR

调用 `internal.cast(value, target_type)`；第一个参数是待转换的 `ValueId`，第二个参数是目标类型标记，目前仅支持 `Bool`。调用产生一个 bool 类型的 `ValueId result`。`cast` 是内部函数，不另设 `CastInst`。

#### SSA IR

沿用内部函数 `cast` 的调用语义；后续优化可以在证明安全时降低为更具体的转换操作。

#### runtime 结构

读取输入值，检查它能否转换为逻辑标量；成功时返回 logical scalar，失败时报告相应错误。

#### 注意事项

对短路表达式的右操作数，仅在控制流真正进入右侧路径后调用 `cast`，不能提前检查被跳过的值。

### `switch_match`

#### 简述

为 `switch` 的一个 `case` 判断选择器与 case 值是否匹配。它是内部函数，不通过普通 M 函数名 `eq` 分派；其匹配规则需要覆盖 MATLAB `switch` 语义。

#### non-SSA IR

调用 `internal.switch_match(switch_value, case_value)`，两个参数均为已经求值的 `ValueId`，结果为 logical scalar `ValueId`，供 `BranchInst.condition` 使用。

#### SSA IR

沿用相同的内部函数调用语义；`switch_value` 的定义支配后续各 case 判断块。

#### runtime 结构

按 MATLAB `switch` 的匹配规则比较选择器和当前 case 值，返回 logical scalar。
