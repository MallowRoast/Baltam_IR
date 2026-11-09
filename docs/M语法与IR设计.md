# M 语法的统一 IR 设计

本文按 M 语法项统一记录 IR 设计。每项分别说明简述、non-SSA IR、SSA IR 和 runtime 结构。此处记录目标设计；当前实现可能尚未采用相同的字段或对象表示。

下文 C++ 字段示意按对齐要求从高到低分组：通常先放指针、字符串、容器和 `std::size_t`，再放 ID 和可选 ID，最后放单字节枚举、布尔值。同一对齐级别仍按语义顺序排列。具体对齐和大小还受类型定义、基类及目标 ABI 影响，实施时应以 `alignof`、`sizeof` 验证，不依赖字段顺序表达语义。

节点语义和构造接口以 `/home/zj/Desktop/baltam_ir` 为参考；本项目继续使用 `Instruction : User`，所有值输入只保存在 Use 中，不额外复制参考库的公开 ValueId 操作数字段。下文示意字段描述语义内容，实际通过访问器和 setter 维护使用链。

本项目使用签名参数 Value、workspace_barrier、带来源 load、SSA 寄存器参数与返回值，以及跨阶段回退映射。相关约定见 [SSA 化与回退机制](SSA化与回退机制.md)和 [mem2reg 设计](mem2reg设计.md)。

## 基本表达式

### 常量（数值与字符串字面量）

常量是运行之前已经确定值的量，不限于源码中的字面量。数值字面量、字符向量字面量（如 `'aaa'`）和字符串标量字面量（如 `"aaa"`）属于第一批；后续还可以包括已确定值的矩阵、cell、struct 等。

#### non-SSA IR

使用 `ConstInst` 表示常量，主要字段为：

```cpp
const_ba_obj_ptr value; // std::shared_ptr<const ba_obj>
ValueId result;
```

`result` 作为后续指令的输入编号；`value` 共享持有只读 `ba_obj`，对象包含常量的类型、形状及内容，无需在指令上重复保存。

#### 注意事项

赋值或修改共享常量的运行时值时必须写时复制，不能改变 IR 持有的常量或其他共享者的值。

### 水平并置与竖直并置

`[a, b]` 表示水平并置，`[a; b]` 表示竖直并置；二维形式可以按行列组合。执行并置使用 `ConcatInst`；空数组与普通单值形式在 lowering 中单独处理。

本机 MATLAB R2022b CLI 实测：在调用函数所在目录的 `private` 文件夹中定义 `horzcat.m`、`vertcat.m` 后，连纯字面量 `[1, 2]`、`[1; 2]` 也分别调用对应的私有函数；私有函数还优先于路径同名函数及输入对象的同名类方法。相同条件下，空 `[]` 不调用这两个函数。仅在普通路径上放同名函数时，纯字面量并置未调用它们，但 `[a, b]`、`[a; b]`（`a`、`b` 为变量）会调用。私有函数相对于对象方法和路径函数的优先级也符合 [MathWorks 的函数优先级说明](https://www.mathworks.com/help/matlab/matlab_prog/function-precedence-order.html)。

#### non-SSA IR

`ConcatInst` 的主要字段为：

```cpp
std::vector<ValueId> items;
ValueId result;
Axis axis;                 // 类内枚举：Horizontal、Vertical
```

`items` 按源码顺序保存输入表达式的值；`axis` 区分水平与竖直并置。lowering 时须先识别两个特殊语法形式：空方括号 `[]`（包括仅含空白的 `[ ]`）直接生成持有空数组的 `ConstInst`；仅含一个普通单值表达式的 `[x]` 先求值 `x`，再沿用其 `ValueId`，例如 `[[]]` 得到空数组常量。两者都不生成 `ConcatInst`。`[x]` 的**处理方式**可静态确定，并不表示 `x` 的值在编译期已知，也不能省略对 `x` 的求值。

其他需要并置的形式才生成 `ConcatInst`，其 `items` 必须非空。输入表达式的值即使是空数组，也仍占一个 `items` 位置：本机实测 `[[], []]` 会调用私有 `horzcat`，不能因为两个输入值都为空就删掉输入项。单个语法表达式还可能展开为多个输入，例如本机实测 `[cells{:}]` 会调用私有 `horzcat`；这类逗号分隔列表不能套用普通单值 `[x]` 的透传规则。是否全部为常量由输入值的定义判断，不另设标记。

#### runtime 结构

根据 `axis` 执行水平或竖直并置，并遵守运行时对 `horzcat`、`vertcat` 的函数解析与分派规则。结果是一次并置运算产生的运行时值；具体对象类型及维度由实际输入和被调用的实现决定。

#### 注意事项

只有在 `items` 全部为常量，且已确认调用处没有会影响此次分派的私有函数、对象方法或其他用户自定义 `horzcat`／`vertcat` 时，才可以进行常量折叠。输入全部为常量单独不足以保证折叠安全；尤其不能因为所有输入都是字面量，就忽略调用文件旁的 `private` 文件夹。

### 元胞数组构造

`{1 2}` 构造 1×2 元胞数组，`{a, b; c, d}` 构造 2×2 元胞数组。每个输入值分别放入对应元胞，不进行方括号的水平或竖直并置。空 `{}` 可直接表示为持有空元胞数组的 `ConstInst`。

#### non-SSA IR

非空元胞构造使用 `CellConstructInst`，保存结果、元素及行列形状：

```cpp
std::vector<ValueId> items; // 按行列位置排列
std::size_t rows;
std::size_t cols;
ValueId result;
```

非空形式的 `rows`、`cols` 由语法结构确定，`items` 保存各位置的输入值；元素数量与 `rows * cols` 一致时，行列数自然都大于零，无须单独处理零行或零列。是否全部为常量可直接检查 `items` 的定义。

#### runtime 结构

分配指定形状的元胞数组，将 `items` 对应的值放入各元胞。元胞构造不需要通过 `horzcat` 或 `vertcat` 做用户函数分派。

#### 注意事项

如果 `items` 全部是常量，可以直接把整个元胞构造折叠为持有 cell 对象的 `ConstInst`。折叠后的对象仍遵守常量共享与写时赋值规则。

### 冒号表达式

`start:stop` 和 `start:step:stop` 生成序列。两参数形式与显式写出 `step` 的三参数形式应保留区别。冒号语法可能受用户自定义 `colon` 的分派影响，因此生成 IR 时保留 `ColonInst`，不直接改写为普通函数调用或常量。

本节只讨论生成序列的冒号表达式；索引中单独的 `:` 由下文的特殊常量 `const ":"` 表示。

#### non-SSA IR

`ColonInst` 保存结果、起点、可选步长和终点：

```cpp
ValueId result;
ValueId start;
std::optional<ValueId> step; // 未提供时为空；与显式写出 1 区分
ValueId stop;
```

#### runtime 结构

执行时先按冒号语法的规则解析分派目标，再由相应实现根据 `start`、可选的 `step` 和 `stop` 产生结果。若使用内置数值 `colon`，结果通常是行向量；用户自定义实现的结果由其函数决定。

#### 注意事项

1. **常量折叠：**只有在操作数全部是常量，且已确认没有会影响此次分派的用户自定义 `colon` 时，才能按内置语义进行常量折叠。
2. **操作数形状：**内置数值 `colon` 的标量要求与 MATLAB 版本有关。本机 MATLAB R2022b CLI 实测：`[1 2]:2:5` 得到 `[1 3 5]`，`1:[1 2]:5` 得到 `[1 2 3 4 5]`，`1:2:[5 6]` 得到 `[1 3 5]`；三个位置都接受非标量并取其第一个元素。[MathWorks 的版本记录](https://www.mathworks.com/help/matlab/ref/double.colon.html)说明 R2024a 对非标量操作数发出警告，R2025a 起生成向量时会报错。标量约束不应在 IR 字段类型上强制，以免排除用户自定义 `colon` 的语义或预先固定兼容版本。

### 索引中的单独冒号

`A(:, 2)`、`A(:)` 中单独的 `:` 是索引占位，表示该索引位置的全部元素；它不同于生成序列的 `start:stop`，不调用 `colon`。

#### non-SSA IR

使用 `ConstInst` 保存特殊冒号标记，文本约定为：

```text
%colon = const ":"
```

索引指令引用该结果编号。不同出现位置可以共享同一个只读标记对象，但不约定固定的特殊 `ValueId`。

#### runtime 结构

冒号标记与普通字符串 `":"` 的类型不同，实际对象由后续运行时实现提供。执行索引时，根据被索引对象和该常量所在的索引位置解释“取该位置的全部元素”；标记本身不生成序列。

#### 注意事项

该标记只用于索引上下文；文本读写须保留它与普通字符串常量的类型区别。

### 索引中的 `end`

`A(end)`、`A(1, end)`、`C{end}` 中的 `end` 由所在索引操作解释，不能当作普通名字、常量或普通函数调用。其结果取决于被索引对象、当前索引维度，以及该次索引的参数总数。

#### non-SSA IR

使用 `MagicEndInst`：

```cpp
struct MagicEndContext {
    Operand callee_or_base;
    std::uint32_t dim;
    std::uint32_t nindices;
};

std::vector<MagicEndContext> candidate_contexts;
ValueId result;
```

`candidate_contexts` 按内层到外层排列。单层 `A(end)` 只有一个候选；`A(fun(end))` 在 `fun(...)` 尚未确定是索引还是函数调用时，保留 `fun`、`A` 两层候选。基础 IR 不预先假定 `end` 的结果类型。

`callee_or_base` 的 `Operand` 种类区分已求值对象与尚未消歧的名字；已确定为变量时须先生成 `LoadSlotInst`，并在索引操作中复用同一 `ValueId`：

| `Operand` 种类 | 含义 |
| --- | --- |
| `InternedString` | 尚未解析的源码名字。若解析为变量，就以该变量为索引对象；若解析为普通函数调用，则该候选不是索引上下文，可继续尝试外层候选。 |

| `ValueId` | 已加载或计算出的索引对象值。它已绑定到具体数据流，只需判断该值是否支持索引 `end` 语义；不支持时按该候选报错。 |

#### runtime 结构

按内层到外层检查候选，选择实际的索引上下文。若所有可跳过的未解析名字候选都不是索引，且没有合法的外层候选，则报缺少索引上下文的错误。普通数组按索引规则计算 `end`；类对象由其索引协议处理 `end(obj, dim, nindices)`。后续可直接解释 `MagicEndInst`，也可在选定合法索引对象后降为类似 `internal.end_index(base, dim, nindices)` 的内部操作；基础 lowering 不提前执行这一步。

#### 注意事项

1. 候选回退仅适用于解析为普通函数调用的未绑定名字；用户路径上的普通 `end.m` 不参与此语法的名字解析。
2. 类对象若重载了 `end` 方法，应按索引协议调用 `end(obj, dim, nindices)`；不能直接用数组维度计算结果，也不能预设结果类型。

## 函数句柄

当前源码尚未实现具名／匿名函数句柄构造指令及匿名函数体；本节保留其目标设计。下文以代码单元描述函数体（旧示意名为 `CodeUnit`，当前基类为 `IRUnit`）；基础代码单元与槽位表已有实现，`AnonymousFunctionUnit` 及捕获槽位类别仍是目标设计。自由变量分析及 closure 运行时尚未接入。

### 具名函数句柄 `@name`

#### 简述

`@sin`、`@myFunction` 构造可调用的具名函数句柄值，不调用函数，也不读取同名变量。基础语法先支持 `@name`；`@pkg.func` 等限定名形式需要另行定义名字解析规则。句柄结果的类型固定为标量 `function_handle`。

#### non-SSA IR

使用 `CreateNamedFunctionHandleInst`，主要字段为：

```cpp
InternedString name;
FunctionUnit* m_function_target;   // 同模块内已确定的 M 函数目标
ValueId result;
```

基础 lowering 只记录源码名字，`m_function_target` 为空；不因名字与内置函数或同文件函数同名就静态绑定。名字解析若能证明同模块 M 函数目标确定，可以填写该目标；其余目标由执行层解析。指令不保存动态分派标记或分派类型字段。

例如 `f = @sin; y = f(x);` 中，构造句柄先产生一个 `ValueId` 并存入 `f` 的 slot；调用时加载 `f`，再经 `ValueApplyInst` 按运行时值分派。构造句柄本身不是 `CallInst`。

#### SSA IR

继续使用同一个 `CreateNamedFunctionHandleInst`；其结果是单次定义的 SSA 值。存入变量 `f` 后，普通变量 slot 可按 SSA 规则提升；若 `phi` 的各输入都是句柄值，汇合结果也仍是函数句柄值，不需要重新执行 `@name` 的解析。

未确定目标的句柄构造会观察函数查找环境，不能仅因 `name` 相同就跨可能改变路径或解析结果的操作合并、提前执行。`@inner` 即使静态确定了函数目标，每次执行构造指令时也必须取得当前这次父函数调用的共享工作区；不能把不同调用创建的句柄合并。父函数和嵌套函数都可能修改共享变量，相关访问不能仅按父函数的普通局部 slot 做 SSA 提升。

#### runtime 结构

执行时，若没有已确定的目标，在构造点按当前函数搜索环境查询一次：找到目标就构造已绑定句柄；未找到则构造保留名字及必要解析上下文的未解析句柄，后续每次调用该句柄时再查询。已确定的同模块目标直接用于构造句柄。句柄调用走值调用分派，并传递调用点的实参与请求输出数。

若 `@inner` 指向嵌套函数，运行时句柄除函数目标外，还要持有**当前这次父函数调用的工作区对象的一份共享引用**。父函数的执行 frame 和由它创建的嵌套函数句柄访问同一个工作区对象及其中的变量绑定；构造多个句柄只复制共享引用，不复制工作区或变量值。父函数返回后，句柄继续持有该工作区，调用 `inner` 仍可读写其中的变量；最后一个持有者释放后，工作区才能销毁。父函数每次调用创建各自的工作区，不跨调用共享。若存在多层嵌套，句柄还须保住所需的外层工作区链。

例如同一次 `outer()` 调用返回两个 `@inner` 句柄时，它们读写同一份父工作区；另一次 `outer()` 返回的句柄则使用另一份父工作区。这与匿名函数在构造点按值捕获不同。

#### 注意事项

1. 静态绑定必须证明局部函数、导入、路径、遮蔽及代码版本等规则下的目标稳定；否则由执行层按名字解析。已绑定句柄持有的代码目标和嵌套环境必须在句柄存活期间有效。
2. `@name` 与 `@(args) expr` 分别使用具名和匿名函数句柄构造指令；后者拥有匿名函数体和按值捕获信息，不能复用此节点。
3. 父工作区必须是独立于执行 frame 的共享对象，不能靠延长整个 frame 的生命周期实现。目标设计中的 `CreateNamedFunctionHandleInst` 使用上述字段；嵌套函数句柄持有父工作区共享引用、共享变量的存储映射，以及未解析句柄的解析上下文仍需要在 runtime 对象模型中落实。

### 匿名函数 `@(args) expr`

#### 简述

`@(x) x + y` 构造一个标量 `function_handle`，包含匿名函数体及构造时捕获的外层变量值。函数体是单个表达式；每次调用按参数求值该表达式并返回结果。匿名函数体没有可供普通名字查找的函数名，也没有具名函数的输出变量列表。

#### non-SSA IR

匿名函数体使用独立的 `AnonymousFunctionUnit`：其 `lexical_parent` 指向定义处的 `IRUnit`，`param_slots` 按源码顺序保存参数 slot，`capture_slots` 保存体内读取捕获值的 `Capture` slot。函数体表达式结果的输出槽位约定仍需在实现匿名函数时确定。

外层代码在构造点使用 `CreateAnonymousFunctionHandleInst`，主要字段为：

```cpp
struct CaptureValue {
    InternedString name;
    Slot source_slot;          // 外层代码单元中的捕获来源
    ValueId captured_value;   // 构造点读取到的值
};

std::shared_ptr<AnonymousFunctionUnit> target;
std::vector<CaptureValue> captures;
ValueId result;
```

lowering 先分析匿名函数体中需要捕获的外层变量，在构造点从各 `source_slot` 生成 `LoadSlotInst`，再把得到的 `captured_value` 交给构造指令。`source_slot` 记录捕获来源，读取时仍需检查该变量是否绑定；实际保存进句柄的是 `captured_value` 对应的运行时值。匿名函数体使用自己的 `Capture` slot 读取这些值，不能直接引用外层的 `ValueId`，因为 `ValueId` 只在所属代码单元内有效。

捕获来源在脚本中通常是 `ScriptVar` slot，在函数中通常是 `Local` slot（包括入参和出参），在匿名函数中也可能是 `Capture` slot；它们都属于构造指令所在的外层代码单元。`source_slot` 仅用于定位和检查来源，closure 保存的是构造点 `LoadSlotInst` 取得的运行时值。

例如：

```matlab
y = 1;
f = @(x) x + y;
y = 10;
z = f(2);  % z 为 3
```

这里的 `f` 保存构造时 `y` 的值。`f(2)` 先读取 `f` 的句柄值，再经 `ValueApplyInst` 分派调用；构造句柄本身不是一次函数调用。

#### SSA IR

`CreateAnonymousFunctionHandleInst` 的 `result` 和各 `captured_value` 都是普通 SSA 值。若外层变量 slot 已提升为 SSA，构造点应引用当时到达的定义；构造点位于不同路径的汇合之后时，先按普通 SSA 规则建立所需的 `phi`，再构造句柄。构造之后对原变量的重新赋值，不会改变句柄中已经捕获的值。

匿名函数体是另一个代码单元，其 `Capture` slot 可在优化时转成函数入口的隐式 SSA 输入，或保留为对 closure 环境的只读加载。不能把外层 `ValueId` 直接跨代码单元复用。若捕获的值本身是句柄对象等引用语义对象，按该值的对象语义保留共享关系；“按值捕获”不要求无条件深拷贝对象。

#### runtime 结构

执行构造指令时，为该次构造创建 closure：它持有匿名函数体的代码对象和本次捕获的值。调用时创建匿名函数的执行 frame，按 `param_slots` 绑定实参，并让 `Capture` slot 读取 closure 中的对应捕获值；执行函数体后返回表达式结果。不同构造执行可以共享函数体代码，但各自保存自己的捕获值。

匿名函数不会为了捕获外层变量而直接持有父函数工作区；若被捕获的值本身是持有工作区引用的嵌套函数句柄，该引用仍随捕获值保留。上一节的嵌套函数 `@inner` 则直接持有父工作区的共享引用，能够读写同一份外层变量绑定。

#### 注意事项

1. 自由变量分析要区分外层变量引用、形参和需要按函数名解析的调用；不能仅凭匿名函数体中出现一个名字，就把它当作捕获变量。
2. 捕获值必须在运行到构造点时取得，且必须保持未绑定错误语义；不能在编译时固定，也不能推迟到句柄调用时才读取外层变量。
3. 目标设计包含 `AnonymousFunctionUnit`、`CreateAnonymousFunctionHandleInst` 和 `Capture` slot；这些结构当前尚未实现。closure 对象、捕获值复制规则及调用期环境映射仍由 runtime 实现。
4. 后续优化可将 `Capture` slot 映射到 closure 环境的固定位置，在入口读取一次或用 SSA 隐式参数传入；证明调用目标和捕获值后，还可特化句柄调用。重复读取合并、store/load 转发等交由通用优化 pass 处理，不塞进匿名函数 lowering。

## 索引与成员访问

本节记录 IR 的语义划分。圆括号和点号表达式可能先经过名字解析，再根据 base 值选择调用或索引；已确定索引类别，也不等于已确定内置实现或对象重载实现。下文的基础指令已定义；多结果扩展、名字消歧、运行时分派和复合索引执行仍待实现。

### 圆括号取值与调用消歧

同一个源码形式 `A(...)` 可以处在以下表示阶段，或解析为不同的最终语义；并非一次执行四条指令。前三种节点已经存在。以下结构只列主要字段，省略继承、构造函数与通用指令属性：

```cpp
// 名字仍未消歧：A(...) 可能是调用，也可能是索引。
struct ApplyInst {
    std::vector<ValueId> results;
    Operand callee_or_base;          // 未解析名字或已求值对象
    std::vector<ValueId> arguments;  // 已求值的实参，可能引用值包
};

// base 已经是值，但可能是函数句柄，也可能是可索引对象。
struct ValueApplyInst {
    std::vector<ValueId> results;
    std::vector<ValueId> arguments;
    ValueId base;
};

// 已确定按函数名调用；函数句柄调用由 ValueApplyInst 按运行时值处理。
struct CallInst {
    std::vector<ValueId> results;
    InternedString callee;
    std::vector<ValueId> arguments;
    FunctionUnit* m_function_target; // 仅同模块、词法绑定的 M 函数目标
};
```

确定是索引后，使用 `ParenReferenceInst`：

```cpp
struct ParenReferenceInst {
    std::vector<ValueId> results;
    std::vector<ValueId> indices;    // 已求值，按源码顺序
    ValueId base;                    // 已求值的索引对象
};
```

`ApplyInst`、`ValueApplyInst` 和 `CallInst` 的 `arguments` 在生成指令前已经分别求值，按源码顺序保存 `ValueId`；可能展开成逗号分隔列表的实参由后续值元数据机制标记。`ApplyInst.callee_or_base` 只保留未消歧名字或已求值的值；已确定为变量时先用 `LoadSlotInst` 得到 `ValueId`；`CallInst.callee` 只保存已确定按名字调用的函数名。`Operand` 中的空名字不在指令构造时统一检查，名称有效性由前面的语法与构建流程负责。`results` 保留请求的输出位次；普通内置圆括号取值通常产生一个值，对象索引协议可能需要多个输出。`ParenReferenceInst` 已确定“圆括号取值”这一类别；内置索引还是对象索引协议由执行时的 base 决定，不在节点中保存分派类别。`ApplyInst` 确定为索引后，沿用已求值的实参作为 `indices`；确定为按名字调用后，沿用实参生成 `CallInst`；若先得到函数句柄等运行时值，则由 `ValueApplyInst` 分派调用或索引。值包的实际展开规则仍需后续执行设计。

目标单结果 lowering 中，`b = A()` 若 `A` 尚无变量 slot，则生成保存名字 `A` 的 `ApplyInst`，保留调用与索引歧义；`b = x(1)` 在 `x` 已绑定时先读取其 slot，再求值下标并生成 `ParenReferenceInst`。当前 `ParenReferenceInst` 只保存一个结果，上方多结果形式是后续扩展草案。

独立语句 `fun(x)` 在 `fun` 没有已绑定变量 slot 时按函数调用生成无结果的 `CallInst`，保留动态名字解析；已绑定变量名的圆括号语句通过 `ValueApplyInst` 保留按值调用或索引的可能。调用实参按源码顺序求值，未使用调用结果不分配 `ValueId`，调用本身仍保留以执行其副作用。

`CallInst` 不保存运行时调用点缓存或分派类别。`m_function_target` 仅引用同模块内词法绑定固定的函数；外部 M 函数、内置函数及插件由执行层解析。嵌套函数调用还需要当前父函数的共享工作区。后续若固定外部目标，须设计版本验证和目标生命周期管理，不能仅靠函数名直接调用。

目标设计让 `A(end)` 等表达式在消歧前使用 `MagicEndInst` 保留候选索引上下文；选择真正的索引层后，才能确定 `end` 的值。单独的 `:` 由特殊常量 `const ":"` 表示，不预先生成序列。两类值的生成留给后续 builder。

### 花括号取值

`C{I}` 使用 `BraceReferenceInst`，表示已经确定的花括号内容访问：

```cpp
struct BraceReferenceInst {
    std::vector<ValueId> results;
    std::vector<ValueId> indices;
    ValueId base;
};
```

取多个元胞内容时可能产生逗号分隔列表或多个输出，不能强制只返回一个普通值。构建调用处为可能承载整组内容的 `ValueId` 在 `Value::form()` 中设置 `MaybePack`；普通单值及已拆分为独立 `ValueId` 的结果保持 `Single`。该标记使函数实参、出参等使用点能识别潜在的值包，但实际元素数量和展开规则仍须由后续运行时设计承载。具体取值仍按 base 的索引协议分派。

当前单层 `C{2}` lowering 先从 `C` 的 slot 读取 base，按源码顺序求值索引，再生成单结果 `BraceReferenceInst`，该结果标记为 `MaybePack`。例如 `b = C{2}` 将结果写入 `b` 的 slot。当前指令结构只保存一个可承载值包的 `result`，上方多结果结构是后续扩展草案。

### 点号访问与取值

`a.b` 保留未消歧的点号访问和已确定的值取成员两种表示：

```cpp
struct DotAccessInst {
    ValueId result;
    Operand base_or_name;            // 允许未解析的左侧名字
    Operand member;                 // 静态名字或动态字段名的值
};

struct DotReferenceInst {
    ValueId result;
    Operand member;
    ValueId base;                    // 已求值的结构或对象
};
```

`DotAccessInst` 不提前把左侧解释为变量值，也不排除后续解析为限定名字等语义。`DotReferenceInst` 已确定字段或属性取值，但具体行为仍可由对象协议决定。静态成员名 `a.b` 使用名字操作数；动态字段名 `a.(expr)` 使用求值后的 `ValueId` 操作数。

当前 lowering 对尚无变量 slot 的 `a.b` 生成保留名字 `a` 的 `DotAccessInst`；`a = struct()` 仍通过名字未消歧的 `ApplyInst` 表示。此后 `a.b` 先读取 `a` 的 slot，再生成 `DotReferenceInst`。`a.b = 2` 生成 `DotAssignInst` 的更新后 base，并写回 `a` 的 slot。`a.('b')` 先生成字符向量常量，将其 `ValueId` 作为动态字段操作数交给 `DotReferenceInst`。当前只支持根变量上的单层点号赋值。

对结构数组等产生多个成员值的情形，`DotReferenceInst` 也须保留逗号分隔列表或多个结果的可能性。

### 赋值与删除

赋值与删除节点以已求值的 `base` 为输入，产生更新后的 base 值，供外层索引更新或根变量写回。主要字段草案为：

```cpp
struct ParenAssignInst {           // A(I) = v
    std::vector<ValueId> indices;
    ValueId updated_base;
    ValueId base;
    ValueId value;
};

struct ParenDeleteInst {           // A(I) = []
    std::vector<ValueId> indices;
    ValueId updated_base;
    ValueId base;
};

struct BraceAssignInst {           // C{I} = v
    std::vector<ValueId> indices;
    std::vector<ValueId> values;   // 可对应多个被赋值的内容
    ValueId updated_base;
    ValueId base;
};

struct DotAssignInst {             // A.b = v
    Operand member;
    ValueId updated_base;
    ValueId base;
    ValueId value;
};
```

`C{I} = []` 仍属于 `BraceAssignInst`，不能只根据右侧是空数组就改成圆括号删除。各写入操作保留对应的对象索引协议及可能的副作用。

`a(i) = 2` 先读取 `a`，再求值索引与右值，生成 `ParenAssignInst` 的更新后 base 并写回 `a` 的 slot；`a(i) = []` 使用 `ParenDeleteInst`，不生成右侧空数组常量，也把更新后 base 写回同一 slot。当前只支持根变量上的单层圆括号赋值或删除。

当前单层 `C{2} = 2` lowering 读取 `C` 的旧值，求值索引和右值，生成 `BraceAssignInst` 的新 base 结果，再将结果写回 `C` 的同一个 slot。当前指令只接收一个右值；复合索引链和多内容赋值仍需扩展 lowering。

已确定的圆括号、花括号索引节点都保存按源码顺序求值后的 `ValueId`；目标设计也允许引用冒号特殊常量和 `MagicEndInst` 的结果，其生成留给后续 builder。未消歧的名字只保留在 `ApplyInst` 等节点中。索引和静态成员取值／赋值节点只确定语法操作，不保存分派类别；执行时仍需按 base 的实际类型选择内置行为或对象协议。若后续优化要固定重载目标，应另设计带依赖验证的特化表示，不能仅增加一个分派类别枚举。

### 注意事项

#### SSA 中的赋值与删除

non-SSA 与 SSA 使用相同的单层索引节点。读取的 `results` 是新定义；`ParenAssignInst`、`BraceAssignInst`、`DotAssignInst` 和 `ParenDeleteInst` 的 `updated_base` 也必须是新的 `ValueId`，不能把原 `base` 的 SSA 定义原地改写。例如 `A(i) = v` 表达为 `%new_A = paren_assign %old_A, %i, %v`，`A(i) = []` 表达为 `%new_A = paren_delete %old_A, %i`。non-SSA 将结果写回 `A` 的 slot 或当前 workspace binding；仅当 `A` 是可安全提升的普通局部时，SSA 才能以 `%new_A` 作为它的新定义。对象索引协议的副作用仍须保留。

#### 复合索引链

`obj.Items{i}.Value` 可以继续 lower 为 `DotReferenceInst`、`BraceReferenceInst`、`DotReferenceInst` 三个单层节点，不需要专用的“复合索引”IR。解释器执行这些节点时，若根对象需要完整索引链才能分派，可让中间 `ValueId` 暂存以下运行时状态，而不立即执行前缀 `subsref`：

```text
PendingIndex {
  root: ba_obj_ptr
  steps: IndexStep[]  // 每步记录 ()、{} 或 .，以及已求值的下标或成员名
}
```

第一层 `obj.Items` 产生 `{root = obj, steps = [.Items]}`；第二层在该状态后追加 `{i}`；第三层追加 `.Value`。真正需要结果的消费者再调用一次 `subsref(root, steps)`。这样无需从当前指令向前或向后查找整条链；链保存在上一个 `ValueId` 的运行时结果中。这里的 `tmp`、`tmp1` 是 IR 临时值，不是赋给用户变量的 M 语句；运行时应使用拥有根对象生命周期的 `ba_obj_ptr`，不直接保存裸指针。目标运行时若由 `RuntimeFrame::temporaries` 保存 `ba_obj_ptr`，实现此方案时需让临时值也能保存 `PendingIndex`，例如改为带标签的运行时值。

`A(i).b{j} = v` 也使用已有的单层索引节点积累 `[(), ., {}]` 路径，最终以根对象、完整路径和右值执行 `subsasgn`，将返回的更新后根对象定义为新的 `ValueId` 并写回 `A`。删除同样产生更新后的根值，而不是原地修改旧 SSA 值。不能把对象的复合赋值强制拆为对每层分别调用一次 `subsasgn`。[MathWorks 的 `subsref` 说明](https://www.mathworks.com/help/matlab/ref/subsref.html)和[`subsasgn` 说明](https://www.mathworks.com/help/matlab/ref/subsasgn.html)均用多项索引结构表示复合索引。

延迟状态只能在同一源码索引表达式内部传递。赋给用户变量、作为普通运算或调用的实参、返回结果等需要真实值的使用点必须先物化；各层索引实参仍按源码顺序求值一次。若后续索引的 `end`、动态分派或中间逗号分隔列表必须观察前缀结果，则要先求值所需前缀，不能无条件将整条链延迟到末端。

## 基本运算

### 一元运算

一元正号 `+x`、一元负号 `-x`、逻辑非 `~x`、非共轭转置 `A.'` 和共轭转置 `A'` 共用 `UnaryInst`，由运算种类保留各自的源码语义。基础 lowering 不预先将动态运算固定为内置函数，因为对应操作可能分派到用户定义的实现。

#### non-SSA IR

`UnaryInst` 的主要字段为：

```cpp
ValueId result;
ValueId operand;
UnaryOp op;                 // Uplus、Uminus、LogicalNot、Transpose、Ctranspose
```

`Transpose` 对应 `.'`，`Ctranspose` 对应 `'`。操作数表达式先产生 `ValueId`，一元指令再引用它。

#### runtime 结构

根据 `op` 与实际输入解析对应的一元运算实现，传入一个操作数并取得一个结果。

#### 注意事项

1. 操作数是常量仍不足以保证可以折叠；还需确认实际分派目标及其可安全求值。
2. 共轭转置 `'` 与非共轭转置 `.'` 语义不同，IR 和后续优化均须保留两者的区别。

### 二元运算

算术运算 `+`、`-`、`*`、`/`、`\`、`^`，逐元素运算 `.*`、`./`、`.\`、`.^`，比较运算 `<`、`<=`、`>`、`>=`、`==`、`~=`，以及两侧都求值的逻辑运算 `&`、`|`，均使用 `BinaryInst`。不同运算种类保留矩阵运算、逐元素运算和比较运算的源码语义。基础 lowering 不预先把动态分派固定为内置实现。短路运算 `&&`、`||` 另由控制流表示。

#### non-SSA IR

`BinaryInst` 的主要字段为：

```cpp
ValueId result;
ValueId lhs;
ValueId rhs;
BinaryOp op;                 // Add、Sub、Mul、Rdiv、Ldiv、Pow 等
```

左右操作数表达式先分别产生 `ValueId`，再供二元指令引用。

#### runtime 结构

先取得左右操作数，再根据 `op` 与实际输入执行对应的二元运算实现，取得一个结果。

#### 注意事项

1. 操作数全部为常量仍不足以保证可以折叠；还需确认实际分派目标及其可安全求值。
2. 比较运算在动态分派尚未确定时，不预设结果一定是逻辑值。
3. 解析到用户定义函数后仍应保留 `BinaryInst` 的源码运算符信息；解析目标的具体存放方式尚未确定。

## 变量声明

`global` 和 `persistent` 的声明确定源码名字的存储类别，但声明过不等于之后每个程序点仍保持该绑定。两者都有静态 `SlotId`，底层值却不属于普通的逐次调用局部 slot。当前 IR 已有声明指令，但 `SlotInfo` 只保存名称，没有 `Global`、`Persistent` 类别；`clear` 后的绑定切换及专用存储访问仍属于目标设计。

基础槽位访问已由 `LoadSlotInst`（`result: ValueId`、`slot: SlotId`）和 `StoreSlotInst`（`slot: SlotId`、`value: ValueId`）表示。`StoreSlotInst` 的右值须先求值并取得编号；它本身不产生结果。槽位元数据拟由所属代码单元的槽位表保存，跨代码单元的槽位归属检查、绑定状态和实际存储读写仍需后续构建与运行时支持。

### `global`

`global x y` 把当前代码单元中的名字接入 session 级全局变量表。声明同名 global 的函数、脚本或命令行上下文应读写同一个值；未声明的同名局部变量不因此变成 global。

#### non-SSA IR

每个声明名在当前 `IRUnit` 的 slot 表中对应一个 slot；当前 `SlotInfo` 不保存类别；原设计中的 `SlotTag::Global` 是后续可选的元数据方案，全局绑定仍需由声明及运行时环境识别。声明位置生成不产生 `ValueId` 的 `GlobalDeclInst`：

```cpp
std::vector<SlotId> slots;
```

目标基础 lowering 对后续访问使用 `LoadSlotInst` / `StoreSlotInst`；全局存储绑定及运行时访问仍需实现。当前 `Effect` 仅区分是否可能写入 slot，没有环境 effect；原设计中的 `Env` 属于后续可扩展的类别。同一代码单元内的重复声明应复用同名 slot；与不同类别的静态绑定冲突时报告诊断属于后续检查设计。脚本中的声明还须把目标 workspace 的名字绑定到全局值。

#### SSA IR

保留声明与对共享存储的 load/store。每次 load 的结果可定义新的 SSA `ValueId`，但不能把 global slot 整体提升为一个由 `phi` 维护的局部变量：其他函数调用可能修改同名 global。仅在证明当前绑定有效且期间没有相关写入或调用 effect 时，才可在局部范围转发读取。以后若特化为 `LoadGlobalInst` / `StoreGlobalInst`，仍须遵守同一约束。

#### runtime 结构

`InterpreterContext::globals.values[name]` 是跨代码单元共享的存储。执行声明时建立当前 workspace 到该值的绑定；读写必须观察全局表中的最新值，而不能依赖逐次调用 frame 中可能过期的值缓存。

#### 注意事项

1. `clear x` 解除当前 workspace 中 `x` 的 global 绑定，不等于清除全局表中的值。其后的 `x` 访问须重新按当前绑定解析；再次执行 `global x` 才重新接入全局值。`clear global x` 则影响全局表中的值。
2. 声明是静态事实，当前程序点的绑定是动态状态。`clear`、可能改变 workspace 绑定的操作及相关函数调用是优化屏障；证明绑定仍有效后才能使用专用 global load/store。当前 IR 尚未完整表示这些 `clear` 后的绑定状态变化。

### `persistent`

`persistent p` 声明当前函数私有、跨调用保留的变量。同一函数的递归或重入调用共享该值；其他函数不能仅凭同名声明访问它。脚本本体不创建 persistent slot。

#### non-SSA IR

每个声明名在当前函数的 slot 表中对应一个 slot；当前 `SlotInfo` 不包含原设计中的 `SlotTag::Persistent`；该类别可作为后续元数据方案。声明位置生成不产生 `ValueId` 的 `PersistentDeclInst`：

```cpp
std::vector<SlotId> slots;
```

目标基础 lowering 对后续访问使用 `LoadSlotInst` / `StoreSlotInst`；声明类别与绑定状态的检查仍需实现。slot 身份在当前函数内稳定；目标运行时让其值存于函数对应的 `CodeObject::persistent.values[SlotId]`，而非每次调用独有的局部值。当前 `Effect` 仅区分是否可能写入 slot；原设计中的 `Env` 环境 effect 属于后续扩展。

#### SSA IR

保留对持久存储的 load/store，每次 load 的结果仍可成为 SSA 值。不能默认将 persistent slot 提升为跨整个函数的 SSA 局部变量。局部转发读取必须分别证明：当前程序点仍绑定到 persistent 存储；期间没有可能修改或清理该值的递归、重入、动态调用及 `clear`。无法证明时，调用与相关 `clear` 应使已有值假设失效。以后特化为 `LoadPersistentInst` / `StorePersistentInst` 时也一样。

#### runtime 结构

函数的 `CodeObject` 持有 persistent 值表，使用该函数的 `SlotId` 定位存储；同一函数的各次调用共享它。执行声明时把当前调用中的名字接入该存储。运行时读写必须观察其他重入调用对该值的更新，不能仅依赖当前 frame 中的旧缓存。

#### 注意事项

1. `clear p` 解除当前调用中 `p` 的 persistent 绑定；同次调用中后续 `p` 的读写不能继续按 persistent slot 处理。下次调用重新执行声明时再建立绑定。清理函数或代码失效还可能重置持久值，相关优化假设必须失效。
2. “没有显式递归调用”不足以证明不会重入：函数句柄、回调或动态调用也可能重新进入同一函数。只有调用目标与 effect、绑定状态和清理行为都可证明时，才能跨调用复用已读取的 persistent 值。

### 后续专用访问

证明当前程序点的绑定仍有效后，可把通用 slot 访问特化为 `LoadGlobalInst` / `StoreGlobalInst` 或 `LoadPersistentInst` / `StorePersistentInst`。load 持有 `result: ValueId`、`slot_id: SlotId`；store 持有 `slot_id: SlotId`、`value: Operand`。这些指令访问的是跨调用可观察的存储，不能按普通 frame slot 优化；原设计建议在扩展 effect 类型后以 `Env` 表示其跨调用可观察的影响。

verifier 应检查结果、操作数及 slot 有效，且按后续确定的声明与绑定元数据检查指令类别；绑定状态分析还须证明访问点没有跨过相关 `clear` 或其他绑定屏障。对无法静态证明的路径，保留可检查当前绑定的基础访问。文本 IR 应区分 `load_global/store_global`、`load_persistent/store_persistent` 与普通 `load_slot/store_slot`。

## 控制流

### `if / elseif / else / end`

条件按源码顺序求值；命中一个分支后执行其语句，跳过后续 `elseif` 条件。所有条件均不成立时，执行 `else`（若存在），否则直接继续执行 `end` 后的语句。

#### non-SSA IR

不设置专用的 `IfInst`，而是使用 CFG。每个条件表达式产生一个 `ValueId`，由 `BranchInst` 选择对应语句块或下一个条件块。没有后续条件时，假边进入 `else` 块或出口块；未以 `return` 等指令终止的分支通过 `GotoInst` 到达出口。分支中的变量读写使用 load/store。

`BranchInst` 是条件块末尾的终结指令，主要字段为：

```cpp
BasicBlock* true_target;
BasicBlock* false_target;
ValueId condition;
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

保留相同的条件分支 CFG。将变量的 load/store 提升为 SSA 值时，若多个前驱为后续使用的同一变量提供不同定义，就在汇合块的普通指令之前放置 `PhiInst`。目标设计中的主要字段为：

```cpp
struct PhiIncoming {
    BasicBlock* predecessor;
    ValueId value;
};

// 直接保存前驱和该路径上的值编号。
std::vector<PhiIncoming> incomings;
ValueId result;
```

`result` 是汇合后唯一的新定义。PHI 的 `incomings` 为汇合块的每个前驱记录一个输入：`predecessor` 指明控制流从哪个块到达，`value` 是沿该路径有效的定义。只有实际进入汇合块的那条路径对应的值成为 `result`；后续指令统一引用 `result`。

例如将上面的 `@x` 提升为 SSA 值后，三个分支分别产生 `%one`、`%two`、`%three`，在出口块汇合：

```text
if.end:
  %x = phi [if.then.0: %one], [if.then.1: %two], [if.else: %three]
  // 后续使用 %x
```

没有 `else` 时，若某条路径未给变量赋新值，该路径的输入应是进入 `if` 前已有的定义；若此前也未定义，不能凭空构造 `ValueId`，仍须保留变量未定义时的错误语义。`phi` 的插入与变量重命名等完整 SSA 转换规则待 SSA 设计确定。

#### 注意事项

1. `if` 条件的转换规则不同于短路逻辑运算的标量检查。本机 MATLAB R2022b CLI 实测：`if [1 2]` 为真，`if [1 0]` 与 `if []` 为假，所有元素非零的矩阵为真，`if "x"` 报转换错误。因此不能直接使用短路运算中要求逻辑标量的 `intrinsic.logical_scalar` 规则处理 `if` 条件。
2. 后续可能采用线性 IR。届时 `BranchInst` 的真、假目标和 `GotoInst` 的目标都不应继续保存 `BasicBlock*`，而应改为线性 IR 中可定位的标签或指令位置；上述字段形式仅对应当前基于基本块的 CFG。

### `while / end`

`while` 在每轮开始时求值条件；条件为真才执行循环体，条件为假则执行 `end` 后的语句。循环体正常执行完毕或遇到 `continue` 时，返回条件处开始下一轮；遇到 `break` 时直接退出最近一层循环。源码中的 `end` 只标记循环体结束，不产生专用指令。

#### non-SSA IR

不设置专用的 `WhileInst`、`BreakInst` 或 `ContinueInst`，而是用 CFG 中的 `BranchInst` 和 `GotoInst` 表示。下文用 `while.header`、`while.body` 和 `while.end` 描述三个块的职责。进入循环前的当前块跳到 `while.header`。

`while.header` 每次进入时重新求值条件，产生 `ValueId`，由 `BranchInst` 在 `while.body` 和 `while.end` 之间选择。`while.body` 中的语句使用普通 IR；若执行到循环体末尾且当前块尚未终止，就用 `GotoInst` 返回 `while.header`。`while.end` 是条件为假或执行 `break` 后的继续位置。

lowering 阶段维护 `LoopControlContext` 栈，每项保存当前循环的 `break_target` 和 `continue_target`。构造循环体时将该循环的上下文压栈，循环体 lowering 结束后弹栈；它只用于选择跳转目标，不是 IR 指令或 runtime 对象。当前基于基本块的上下文结构为：

```cpp
struct LoopControlContext {
    BasicBlock* break_target;
    BasicBlock* continue_target;
};
```

对应 `while` 的目标为：

```text
break_target    = while.end
continue_target = while.header
```

`break` 和 `continue` 从栈顶读取目标，生成相应的 `GotoInst`，并终止当前基本块。嵌套循环进入内层时压入新上下文，退出内层后恢复外层上下文；`if`、`switch` 不改变循环上下文。`while` 无内部迭代下标更新，因此基础 CFG 不需要单独的 `while.latch` 块。

例如 `while cond; body; end` 可表示为：

```text
current:
  goto while.header

while.header:
  %cond = load @cond
  branch %cond, while.body, while.end

while.body:
  // 执行 body
  goto while.header

while.end:
  // end 后的语句
```

#### SSA IR

保留相同的循环 CFG 和 `break / continue` 跳转语义。循环体中更新且在下一轮条件或循环体中使用的变量，需要在 `while.header` 汇合进入循环前的值与回边上的值，通常由 `phi` 节点表示；若 `while.end` 有来自条件假边和 `break` 等多条路径，退出后使用的值也按 SSA 汇合规则处理。具体 SSA 转换规则待 SSA 设计确定。

#### 注意事项

1. `while` 条件应采用与 `if` 相同的 MATLAB 条件真假规则，不能套用短路逻辑运算要求逻辑标量的 `intrinsic.logical_scalar`；当前基础 lowering 直接将条件表达式结果交给 `BranchInst`，条件转换的具体实现需与 `if` 统一。
2. `break`、`continue` 只能出现在循环内；循环上下文栈决定最近一层循环的目标。与 `for` 不同，`while` 的 `continue` 返回条件块，而 `for` 的 `continue` 需要先进入更新迭代下标的 `for.latch`。
3. `while` 嵌套在 `for` 或另一层 `while` 中时，每层都有独立的循环上下文。内层 `break` 跳内层 `while.end`，内层 `continue` 跳内层 `while.header`；从内层循环退出后，继续执行外层循环体的后续语句，由外层循环自己的回边决定下一轮。
4. 若后续采用线性 IR，跳转目标应改为可定位的标签或指令位置，不再保存 `BasicBlock*`。

### `for / end`

`for x = iterable ... end` 在进入循环时求值一次 `iterable`，建立独立的迭代状态。每轮开始时从该状态取得一个迭代值，写给循环变量 `x`，然后执行循环体；迭代结束后执行 `end` 后的语句。矩阵输入按列迭代。循环体对 `x` 或迭代源变量的赋值，不改变已经建立的迭代次数及后续迭代值。

#### non-SSA IR

不设置专用的 `ForInst`、`BreakInst` 或 `ContinueInst`。使用 `for.preheader`、`for.header`、`for.body`、`for.latch`、`for.end` 五个基本块，并用 `BranchInst` / `GotoInst` 连接。

以下 `for.preheader` 等名称表示块的职责。`for.preheader` 只求值一次迭代表达式，通过 `intrinsic.foreach_init(iterable)` 取得 double 标量上限 `max_iter`，迭代源和上限直接通过 `ValueId` 跨块引用，不再产生 state。初始化可能执行重载 size/double，但按不修改当前工作区的 SSA 假设执行，不自动插入屏障。`for.header` 读取内部下标，通过 `intrinsic.gt_double` 判断 `iter_index > max_iter`：为真进入 `for.end`，否则进入 `for.body`。`for.body` 用 `ParenReferenceInst(iterable, {MagicColon, iter_index})` 取得当前值，写入用户循环变量的 slot，再执行循环体。正常执行完毕后进入 `for.latch`；该块用 `intrinsic.add_double` 将下标加一，再回到 `for.header`。内部下标比较与递增不参与用户运算符分派。取值可能执行重载 subsref，按相同的工作区不变假设处理，取值成功后再提交循环变量赋值。

`iter_index` 每轮变化，而 `ValueId` 只能定义一次，因此 non-SSA 阶段为每个 `for` 分配一个独立的内部 slot。lowering 将其 SlotInfo::is_internal 置为 true，并保存 type=ba_double_mat、shape={1,1}，不把它加入用户名字查找表。每次 load 从槽位元数据继承 double 1x1，提升后的 PHI 继承同样约束。每次进入 `for.preheader` 时将它初始化为 1，在 `for.header` 读取，在 `for.latch` 加一后写回。循环体可能修改用户循环变量，但不能据此改变迭代进度。嵌套的 `for` 各有不同的 `SlotId`；打印名仅供调试。

`for` 沿用上述 `LoopControlContext` 栈。lowering 循环体时压入该循环的目标，结束后弹出，恢复外层循环的目标：

```text
break_target    = for.end
continue_target = for.latch
```

`break` 和 `continue` 读取栈顶目标并生成 `GotoInst`。`break` 直接退出最近一层循环；`continue` 先经过 `for.latch` 更新内部下标，再判断下一轮是否存在。嵌套循环各有独立的迭代状态、内部下标和跳转目标。

例如 `for x = A; body; end` 可表示为：

```text
; slot1 是当前循环专用的内部 double 下标，不进入用户名字查找

current:
  goto for.preheader

for.preheader:
  %a = load slot0
  %max_iter: double 1x1 = intrinsic.foreach_init %a
  %one: double 1x1 = const !c_one (1)
  store slot1 %one
  goto for.header

for.header:
  %index = load slot1
  %done = intrinsic.gt_double %index, %max_iter
  branch %done, for.end, for.body

for.body:
  %all = const ":"
  %current = paren_ref %a, %all, %index
  store slot2 %current
  // 执行 body
  goto for.latch

for.latch:
  %latch_index = load slot1
  %next = intrinsic.add_double %latch_index, %one
  store slot1 %next
  goto for.header

for.end:
  // end 后的语句
```

#### SSA IR

保留相同的迭代顺序和 CFG。`iterable` 与 `max_iter` 只在 `for.preheader` 定义一次。若内部下标 slot 只由本循环的 load/store 访问且不逃逸，SSA 转换可提升该 slot：在 `for.header` 用 `PhiInst` 汇合 `for.preheader` 的初值和 `for.latch` 的更新值，后续直接引用 `phi` 结果，不再为下标生成逐轮 load/store。这个 `PhiInst` 的结果及所有输入、`intrinsic.add_double` 的结果都固定为 `double scalar`；通用 `PhiInst` 不因此被限定为整型。

```text
for.preheader:
  %iterable = evaluate A
  %max_iter: double 1x1 = intrinsic.foreach_init %iterable
  %one: double 1x1 = const !c_one (1)
  goto for.header

for.header:
  %index = phi [for.preheader: %one], [for.latch: %next]  // double scalar
  %done = intrinsic.gt_double %index, %max_iter
  branch %done, for.end, for.body

for.body:
  %all = const ":"
  %current = paren_ref %iterable, %all, %index
  record slot_x, %current
  // 执行 body
  goto for.latch

for.latch:
  %next = intrinsic.add_double %index, %one  // double scalar
  goto for.header
```

`%index` 是内部迭代序号，不是用户循环变量 `x`。每次进入 `for.body` 时，仍须用本轮 `%index` 执行圆括号索引取得 `%current`，再写给 `x`；循环体即使修改了 `x`，也不会改变下一轮的 `%index`。后续 JIT 可根据迭代源的类型与形状，在必要的类型守卫下，将 `foreach_init` 和逐轮取值特化为直接的次数计算、按下标计算或取列；固定为 `double` 的 `%index` 类型本身不足以决定取值方式。普通回边与 `continue` 都到达 `for.latch`，只递增一次；`break` 直接到达 `for.end`，不递增。用户变量 `x` 的 slot 是否另行提升为 SSA 值，取决于它是否需要保留 workspace 可见性、捕获或 deopt 状态；若提升，循环出口还需按实际前驱汇合其值。具体 SSA 转换规则待 SSA 设计确定。

#### runtime 结构

迭代源只求值一次并保留在寄存器中。foreach_init 返回 double 标量上限，具体协议见下文。每轮以特殊冒号和内部下标执行 ParenReferenceInst，允许重载 subsref；不再持有不透明 state 或 foreach_iterate 节点。初始化与取值不自动生成屏障，实际修改父工作区时由运行时回退协议处理。当前仅实现 IR 构建，实际执行仍待 runtime 完成。

#### 注意事项

1. 内部 `iter_index` 按协议保存 double 标量循环状态，不是用户循环变量；它需要可跨回边传递的内部存储，不能重复定义同一个 `ValueId`。用户循环变量每轮进入循环体前重新赋值。
2. `for i = 1:10` 中的 `1:10` 仍按上文 `ColonInst` 的目标设计处理，不能把冒号表达式的用户级分派误当成 `for` 协议的内部 helper。
3. `for` 嵌套在 `while` 或另一层 `for` 中时，每层都有独立的迭代状态和循环上下文。内层 `break` 跳内层 `for.end`，内层 `continue` 跳内层 `for.latch`；从内层循环退出后，继续执行外层循环体的后续语句，不会直接退出外层循环。
4. `continue` 的目标是 `for.latch`，与直接回到条件块的 `while` 不同。若后续采用线性 IR，跳转目标需改为可定位的标签或指令位置。

### `switch / case / otherwise / end`

`switch` 表达式只求值一次，随后按顺序检查各个 `case`。命中第一个 case 后只执行它的语句；`otherwise` 可选。case 之间没有贯穿执行。

#### non-SSA IR

`switch` 表达式产生 `switch_value : ValueId`，供所有 case 判断块复用，不引入中间 slot。

每个 `case` 生成一个判断块和一个执行块。判断块执行内部语义操作 `switch_match` 后，真边进入当前执行块，假边进入下一个判断块、`otherwise` 或统一出口。没有 `otherwise` 时，最后一个判断块的假边直接进入统一出口。执行块正常结束后跳到统一出口。

例如 `switch x; case 1; body1; case 2; body2; otherwise; body3; end` 转为：

```text
current -> switch.case.0

switch.case.0:
  %x = evaluate x
  %case0 = const 1
  %m0 = intrinsic.switch_match %x, %case0
  branch %m0, switch.body.0, switch.case.1

switch.body.0:
  body1
  goto switch.end

switch.case.1:
  %case1 = const 2
  %m1 = intrinsic.switch_match %x, %case1
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

#### 注意事项

1. `case {a, b}` 的目标语义是其中任一元素匹配即可进入该 case；该形式尚需补充 lowering。
2. `switch` 本身不是循环，不建立新的循环上下文；`case` 内的 `break / continue` 仍作用于包围它的最近一层循环。
3. 上述 `BasicBlock*` 跳转目标对应当前 CFG 形式；若以后采用线性 IR，需要改为可定位的标签或指令位置。

### `return`

`return` 立即结束当前函数或脚本代码单元，跳过当前分支、循环及其后的语句。M 语法的 `return` 本身不带返回表达式；函数的返回值来自已声明的输出变量。它与 `break`、`continue` 不同，不读取循环上下文，也不跳到循环出口。

#### non-SSA IR

显式 `return` 生成当前基本块末尾的无操作数 `ReturnInst`，文本格式为 `ret`。函数返回值按 `FunctionUnit::return_slots()` 的声明顺序从当前执行路径的输出槽位取得；源码 lowering 不为返回指令额外生成 load 或值列表。

代码单元执行到末尾且当前块尚无终结指令时，lowering 补一条 `ReturnInst`。该指令不产生结果，也没有后继基本块。位于分支或循环内时，它直接结束当前代码单元。

#### SSA IR

保留各条可达返回路径上的 `ReturnInst`。本阶段不实现 mem2reg；目标方案不提升返回槽位，返回前对输出槽位的写入继续保留。未来若要提升输出变量，需要先明确返回值交付约定。输出变量尚未赋值时必须保留原有的未绑定错误语义。

#### runtime 结构

函数执行 `ReturnInst` 时，应按返回槽位的声明顺序收集输出，退出当前执行 frame 并交付调用方；脚本的 `ret` 只结束代码单元。IR 解释执行尚未实现，匿名函数的输出槽位布局也仍待设计。

#### 注意事项

`return` 是终结指令；它后面的同块指令不可执行，也不能再从该块连出 CFG 后继。位于 `try` 或 `catch` 内时，返回前还须退出对应的异常处理上下文。优化 pass 删除不可达块或合并返回路径时，必须保持实际到达路径的返回值、错误及副作用顺序。

### `try / catch / end`

#### 简述

`try` 内任一操作抛出可捕获错误时，立即跳过余下的 `try` 代码并进入唯一的 `catch`；正常执行完 `try` 时跳过 `catch`。`catch ME` 把当前异常对象写入同一工作区的变量 `ME`，单独的 `catch` 不创建该变量。嵌套 `try/catch` 允许各层处理自己的异常；M 语法没有 `finally` 分支。语义以 [MATLAB `try` 文档](https://www.mathworks.com/help/matlab/ref/try.html)为准。

SSA 方案借鉴 [Julia SSA IR 的 `PhiC` / `Upsilon`](https://docs.julialang.org/en/v1/devdocs/ssair/)：异常可以发生在受保护区域的多条指令处，因此不为每条可能抛错的指令拆出显式异常终结边；在变量成功更新后记录 `Upsilon`，进入 `catch` 时由 `PhiC` 取得抛错前最后一次已完成的定义。下面的四种异常处理指令与 Julia 的 [`enter`、`leave`、`the_exception`、`pop_exception`](https://docs.julialang.org/en/v1/devdocs/ast/)对应，但字段按本 IR 的基本块和运行时栈设计。

#### non-SSA IR

`try` 与 `catch` 各有基本块；两条路径都能继续执行时，在 `try.end` 汇合。目标设计增加四种指令；`TryRegionId` 是代码单元内的静态区域编号，每次执行 `enter` 会在当前 frame 的处理器栈中建立该区域的一次动态实例：

```cpp
struct EnterTryInst {
    BasicBlock* catch_target;
    TryRegionId region;
};
struct LeaveTryInst { TryRegionId region; };
struct TheExceptionInst {
    ValueId result;
    TryRegionId region;
};
struct PopExceptionInst { TryRegionId region; };
```

`EnterTryInst` 安装异常处理器；`LeaveTryInst` 在正常离开 `try` 时撤销它。抛错时 runtime 自动撤销当前处理器，保存异常并转到 `catch_target`；`TheExceptionInst` 在 `catch` 入口取得该异常。`catch ME` 随后用普通变量写入指令绑定 `ME`。`PopExceptionInst` 在正常离开 `catch` 时恢复进入本区域前的异常状态。`leave` 与 `pop_exception` 作用于不同路径，不能互相代替。

non-SSA 阶段的用户变量仍使用原有 slot load/store。异常边是由 `EnterTryInst` 所覆盖的指令范围隐式产生的控制流；verifier 和后续 CFG 分析必须能识别该范围，不能只看 `GotoInst` / `BranchInst` 的显式后继。

#### SSA IR

对可提升为 SSA、且在异常路径上仍活跃的局部变量，在 `enter` 后为入区值放置一个 `UpsilonInst`；受保护区域内每次赋值或删除**成功提交后**，再放置该变量的 `UpsilonInst`。这里的“仍活跃”包括在 `catch` 内使用，以及越过 `catch` 后继续使用。`PhiCInst` 位于 `catch` 入口，列出同一变量、同一异常区域的所有可达 `Upsilon`。字段示意为：

```cpp
struct UpsilonInst {
    ValueId result;                 // 供 PhiC 引用的内部定义
    TryRegionId region;
    Slot tracked_slot;
    std::optional<ValueId> value;  // 空值表示当前未绑定
};
struct PhiCInst {
    std::vector<ValueId> upsilons;
    ValueId result;                 // 所选 Upsilon 的值
    ValueId is_bound;               // 固定为 logical scalar
    TryRegionId region;
    Slot tracked_slot;
};
```

`PhiC` 不按普通 `PhiInst` 的显式前驱选值，而是选择本次异常发生之前实际执行过的最后一个对应 `Upsilon`。普通 `PhiInst` 无法直接承担此任务，因为异常可能从区域内多条指令隐式进入同一个 `catch`。若该路径上变量未绑定，`is_bound` 为假；在 `catch` 中读取它仍须报未定义变量错误，不能凭空产生默认值。`PhiC.result` 的类型是各个已绑定输入类型的并集；`is_bound` 的类型固定，但 `result` 的值类型不固定。需要的 `Upsilon` 数量随变量的提交更新次数增长，而不必为区域内每条可能抛错的指令都建立一组普通 `phi` 输入。

例如：

```matlab
x = 0;
try
    x = 1;
    mayFail();
catch ME
    y = x;
end
```

对应的 SSA 结构可概括为：

```text
%x0 = const 0
enter try0 -> try.catch
%u0 = upsilon try0, @x, %x0
%x1 = const 1                 // x = 1 成功后
%u1 = upsilon try0, @x, %x1
call @mayFail()
leave try0
goto try.end

try.catch:
(%xc, %bound) = phic try0, @x [%u0, %u1]
%exc = the_exception try0
store @ME, %exc
%y = checked_read %xc, %bound
pop_exception try0
goto try.end
```

`checked_read` 仅表示使用 `%xc` 前检查 `%bound` 的伪写法，不是新增的 IR 指令。

如果 `mayFail()` 抛错，`catch` 中的 `x` 是 1；若 `x = 1` 的右侧求值或赋值过程尚未成功就抛错，`catch` 必须看到更新前的值。`Upsilon` 因此不能提前到赋值操作之前。`catch` 之后的正常路径与异常路径若对同一变量给出不同定义，再在 `try.end` 使用普通 `PhiInst` 汇合；`PhiC` 只解决异常进入 `catch` 时的取值。

#### runtime 结构

每个 frame 维护按动态嵌套顺序排列的处理器栈和当前异常状态。`enter` 记录处理器、`catch_target` 及进入时的异常状态；受保护操作抛错时，runtime 选择最近的有效处理器，撤销它并跳到对应 `catch`。`the_exception` 取得当前捕获的 `MException` 值；`pop_exception` 在离开 `catch` 时恢复外层异常状态。`catch` 中再次抛出的错误不由同一层 `catch` 捕获，而交给外层处理器或调用者；这条异常退出路径也须清理当前 `catch` 的异常状态。

解释 non-SSA IR 时，`catch` 从实际 slot 读取抛错时已提交的变量状态。解释 SSA IR 时，处理器动态实例为每个需要的 `PhiC` 保存最近执行的 `Upsilon` 值及绑定状态；同一静态区域在循环中反复执行时，每次 `enter` 都要重新初始化，不能沿用上一轮快照。

#### 注意事项

1. `return`、`break`、`continue` 或普通跳转跨出 `try` / `catch` 时，必须按内到外顺序生成相应的 `leave` / `pop_exception`，不允许留下悬挂的处理器或异常状态。跳到 `catch` 的异常路径由 runtime 自动撤销该层处理器。
2. `A(i) = v` 等复合赋值只有在整个更新成功并写回新值后，才能为变量 `A` 记录新 `Upsilon`；中途出错时仍选择旧绑定。`clear` 等解除绑定操作要记录未绑定状态。
3. `global`、`persistent`、父工作区共享变量及可能由 `eval`、脚本或动态绑定改变的变量，仍按可观察存储处理；无法证明其更新点完整时，不强行插入 `PhiC` 取代真实 load/store。`catch ME` 自身是普通变量赋值，其在 `try.end` 的汇合和未绑定检查遵守一般 SSA 规则。
4. `Upsilon` / `PhiC` 的可达性、嵌套区域与异常边必须进入 verifier、活跃性分析和优化 pass。当前源码尚未实现上述六种节点及异常处理器栈，本节是目标设计。
5. 可能抛错的运算不能仅因普通 CFG 上看似可移动，就越过 `enter` / `leave` 或跨过变量的 `Upsilon`；这样的移动会改变由哪一层 `catch` 接收异常，以及 `catch` 观察到的变量版本。

### 短路逻辑运算

`a && b` 和 `a || b` 按左操作数决定是否求值右操作数。实际求值的每个操作数通过 `IntrinsicInst(id = LogicalScalar)` 检查并转换为 logical scalar；被跳过的右操作数不求值，也不执行转换。`BranchInst` 根据转换结果连接控制流，不能用两侧都求值的 `BinaryInst`。

赋值 `z = a && b` 或 `z = a || b` 有现成的目标 slot。lowering 将最终真、假路径分别连接到两个结果块，各块生成一个 logical scalar 常量并直接 `store` 到 `z` 的 slot，然后跳到同一后继块。无需创建临时 slot，也无需在 Non-SSA 阶段引入 Phi。嵌套短路运算递归连接真假目标，只在最外层的结果块写入 `z`，避免中途改写目标变量。

```text
entry:
  %a = load slot0
  %condition = intrinsic.logical_scalar %a
  branch %condition, sc.rhs, sc.false

sc.rhs:
  %b = load slot1
  %rhs_condition = intrinsic.logical_scalar %b
  branch %rhs_condition, sc.true, sc.false

sc.true:
  %true = const true : 1x1 logical
  store slot2 %true
  goto sc.end

sc.false:
  %false = const false : 1x1 logical
  store slot2 %false
  goto sc.end

sc.end:
  // 后续语句
```

`if`、`while` 的短路条件可以直接把最终真假边接到各自的目标块，不产生结果值，也不需要 slot。普通 `if`、`while` 条件仍使用各自的真假规则；短路操作数必须满足逻辑标量规则。进入 SSA 阶段后，如果结果 slot 被提升，才在控制流汇合处为结果引入 Phi。

本机 MATLAB R2022b CLI 实测：`[true false] && true`、`true && [true false]`、`[true false] || false` 和 `false || [true false]` 均报 `MATLAB:nonLogicalConditional`；`false && [true false]` 返回 logical scalar `false`，`true || [true false]` 返回 logical scalar `true`。因此只检查实际到达的操作数。

短路表达式作为数组元素、调用实参或其他表达式的子表达式时，没有可直接写入的源码目标变量。lowering 在真假路径分别生成 logical scalar 常量，并在汇合块直接生成 PHI，得到 logical 1x1 寄存器结果；Non-SSA 也允许这种寄存器汇合，无需临时 slot、load/store 或绑定记录。后续表达式和语句继续在新的尾块生成，不能返回原块提前执行。mem2reg 保留已有 PHI。直接赋值仍复用目标 slot，完整条件仍直接连接控制流。

## 内部语义操作

内部操作不参与普通 M 语言名字查找，由 `IntrinsicInst` 保存语义 ID、输入列表及结果列表。
内部算术不隐式改写槽位。ForeachInit 可能执行用户重载方法，不能视为任意可删除或移动的纯算术，但不因此自动插入工作区屏障；具体实现可以在后续阶段降低为专用指令或运行时调用。

### `add_double`

`IntrinsicId::AddDouble` 对两个内部 `double scalar` 做加法，结果同类型，不调用用户定义的 `plus`。
在 `for.latch` 中用于递增内部下标；SSA 形式下，结果作为循环头下标 Phi 的回边输入。

### `gt_double`

`IntrinsicId::GtDouble` 比较两个内部 `double scalar` 操作数，产生逻辑标量供 `BranchInst` 使用，不进行用户 `gt` 分派。

### `logical_scalar`

`IntrinsicId::LogicalScalar` 将输入检查并转换为逻辑标量，不能转换时报错。
只对实际到达的短路操作数执行，不能提前检查被跳过的右侧。

### `foreach_init`

`IntrinsicId::ForeachInit` 接收已经求值的迭代表达式，唯一结果固定为 double 1x1：

```text
%max_iter: double 1x1 = intrinsic.foreach_init %iterable
```

语义概括为 `[~, col] = size(obj); tmp = double(col); max_iter = tmp(1)`。tmp 为空时返回 0。size 按双输出调用，double 也允许类重载，失败时在第一轮取值前抛错。向量结果取首元素，不强制原 size 输出为整数标量。NaN/Inf 等异常边界的兼容行为留待 runtime 按实测接入，不提前截断上限的小数部分。

ForeachIterate 已移除，逐轮取值统一使用 `ParenReferenceInst` 和特殊冒号常量。该节点表示已经确定的索引，不应将函数句柄误当作普通函数调用；标量函数句柄迭代的兼容行为需由执行层处理。类对象的索引走 subsref，结果类型和 shape 不保证与源对象相同。

JIT 可以依据迭代源类型与形状特化初始化和取值；须保留类型守卫、源表达式只求值一次及逐轮赋值语义。

### `switch_match`

`IntrinsicId::SwitchMatch` 接收选择器与 case 值，按 M 语言 `switch` 的专用匹配规则产生逻辑标量，
不通过普通 `eq` 名字查找。选择器定义一次，并支配后续 case 判断；匹配结果供 `BranchInst` 使用。
