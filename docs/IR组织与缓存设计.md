# Baltam IR 组织与缓存设计

本文同时记录当前结构和后续设计。标为“目标设计”“后续”或“规划中”的内容尚未由当前 builder 或运行时实现。

## 一、IR 怎么组织

### 1.1 总体结构

Baltam 使用 `IRModule` 组织单个源码文件或一次源码输入产生的可执行 IR。

```text
IRModule
├── 来源信息
├── CommandUnit（脚本或命令输入，可选）
└── FunctionUnit(s)（模块直接拥有的函数）
    └── Nested FunctionUnit(s)（由词法父函数拥有）
```

- 一个源码输入对应一个 `IRModule`。
- `CommandUnit` 只保存 Non-SSA IR。
- `FunctionUnit` 管理 Non-SSA、SSA 和 Optimized SSA 三个阶段的 IR；具体版本和元数据见 4.1、4.2 节。
- 顶层函数、Local Function 以及 `classdef` 内直接定义的方法由 `IRModule` 拥有。
- Nested Function 由其词法父 `FunctionUnit` 拥有。
- 类方法和普通函数使用相同的 `FunctionUnit` 结构。
- 类文件仍由 `ModuleType::ClassFile` 标识；类声明和属性默认值表达式的 IR 归属待类语法设计时确定。
- IR 系统不持有运行时工作空间或已解析的类元数据。

### 1.2 IRModule

`IRModule` 是**一次源码输入生成的 IR** 的所有者。当前保存来源信息、文件内的代码单元和非拥有函数索引，不保存源码文本或缓存验证记录。缓存命中或失效只决定使用哪个模块，不改变已经发布的模块所有权树。

源码来源和模块类型分开表示：`SourceType` 区分文件、命令行输入和内存源码；`ModuleType` 区分脚本主体、函数文件、类文件和命令输入。内存源码也可能按脚本或函数形式解析，因此不能只用一个枚举同时表达两者。

```cpp
enum class SourceType { File, Command, Memory };
enum class ModuleType { Script, FunctionFile, ClassFile, Command };
```

```cpp
class IRModule {
private:
    SourceType source_type_;
    ModuleType module_type_;
    std::optional<std::filesystem::path> path_;

    std::unique_ptr<CommandUnit> command_unit_;
    std::vector<std::unique_ptr<FunctionUnit>> functions_;
    std::map<std::string, FunctionUnit*> function_map;
};
```

当前 `IRModule` 不持有源码文本或源码版本标识。后续若诊断需要在文件变化后还原原始源码，再引入源码快照及其生命周期规则；文件版本验证由缓存层设计。`SourceType::File` 构造接口把路径转为绝对路径并执行词法归一化；符号链接等路径身份规则留给缓存层。`IRModule` 的所有权、入口约定和索引接口已有实现；`BasicBlock` 已能持有指令并通过 `add_instruction` 创建指令，目前实现了基础表达式、变量声明、索引与成员访问的结构，以及三个终结指令。基本块保存所属 `IRUnit*`；类属性默认值表达式尚未实现。

| 字段 | 作用 |
|---|---|
| `source_type_` | 标识文件、命令行或内存源码 |
| `module_type_` | 标识脚本、函数文件、类文件或命令输入的结构 |
| `path_` | 文件源码的规范化路径；非文件源码为 `nullopt` |
| `command_unit_` | 独占拥有脚本或命令行主体 |
| `functions_` | 独占拥有模块直接包含的函数；嵌套函数由各自的词法父函数拥有 |
| `function_map` | 使用内部限定键索引模块直接拥有的函数；不拥有函数 |

**入口约定：** 调用处构建函数列表时把主函数放在首位，`set_functions` 保留传入顺序，使 `functions_.front()` 固定为入口；`entry_function()` 只按此约定返回首个函数，不另存指针。脚本和命令输入通过 `command_unit_` 进入；类文件本身没有普通函数入口，由类解析结果选择构造函数或方法。脚本的 Local Function 仍在 `functions_` 中，但不是入口。当前没有 `seal()`；模块类型与代码单元的最终组合以及主函数顺序由调用处保证。

| `module_type_` | `command_unit_` | `functions_` |
|---|---|---|
| `Script` | 必须存在 | 可包含脚本 Local Function |
| `FunctionFile` | 空 | 非空，首个为主函数，其后为文件级 Local Function |
| `ClassFile` | 空 | 类内方法及文件级 Local Function，可为空 |
| `Command` | 必须存在 | 空 |

该表约束模块入口，不限制同一个文件内的嵌套函数数量；嵌套函数仍由词法父函数拥有。`SourceType::Command` 与 `ModuleType::Command` 配对；文件或内存源码按实际语法确定 `ModuleType`。类文件中的方法由类解析结果定位，不把 `functions_.front()` 当作构造函数。

**函数索引：** `function_map` 使用 `std::map<std::string, FunctionUnit*>`，键是模块内部的限定标识，不能只用函数原名。可按“语言作用域类别／词法路径／名称”编码，例如：

```text
function/main
local/helper
method/MyClass/calculate
```

这些键不是 M 语言中的可调用名称。调用处把模块级函数及对应限定键一起交给 `IRModule::set_functions`；模块一次接收函数列表并建立索引，`find_function` 只查找模块直接拥有的函数。Nested Function 由词法父 `FunctionUnit` 持有，通过父函数的 `nested_functions()` 访问，不进入模块索引。`std::map` 的排序顺序不表示源码定义顺序；定义顺序由 `functions_` 和各级 `nested_functions_` 保留。索引只负责内部定位，不能绕过 Local Function 的可见性规则或类方法分派规则。方法 IR 仍由 `functions_` 拥有。

`CommandUnit` 和模块级 `FunctionUnit` 均由调用处构造，再以 `std::unique_ptr` 移交给模块。`set_command_unit(std::unique_ptr<CommandUnit>&&)` 设置脚本或命令主体；`set_functions(std::vector<std::pair<std::string, std::unique_ptr<FunctionUnit>>>&&)` 一次设置按源码顺序排列的模块级函数，每项包含限定键与 `std::unique_ptr<FunctionUnit>`。重复键以最后一项为准；结构检查留给独立的检查 pass。

**发布与生命周期：** 模块构建阶段可填充上述字段；发布给缓存或执行器后，构建流程约定不再修改 Unit 所有权树和索引。当前没有封闭状态来强制这一约定。源文件变化时创建新 `IRModule`。执行帧、函数句柄或 `object_meta` 若保存内部 `FunctionUnit*`，必须同时持有该模块的 `shared_ptr`；指针只在模块存活期间有效。按需生成的 SSA／Optimized SSA 不得在执行者仍引用旧版本时原地改写，版本关系见 4.1 节。

类文件的内联方法使用 `FunctionUnit`，由 `functions_` 持有。当前 `IRModule` 不保存类名、属性和方法声明的独立结构，也不保存源码文本。类声明和属性默认值表达式的表示方式留待类语法设计，构建模块时不求值。运行时 `object_meta` 管理类身份、访问控制及继承关系，不反向拥有模块内的 IR。

### 1.3 IRUnit

`IRUnit` 保存指向所属 `IRModule` 的非拥有父指针，是 `CommandUnit` 与 `FunctionUnit` 的公共基类，不直接拥有统一的 IR 包装对象。各派生类通过各自持有的 `IRStageVersion` 管理基本块集合。

```text
IRUnit
├── CommandUnit
├── FunctionUnit
└── AnonymousFunctionUnit（规划中）
```

语法设计文档中的旧称 `CodeUnit` 在当前所有权结构中对应 `IRUnit`。匿名函数体的独立代码单元属于后续设计，当前没有 `AnonymousFunctionUnit`。

### 1.4 CommandUnit

`CommandUnit` 表示命令行输入或脚本主体。**只拥有 Non-SSA 阶段版本，不持有 SSA 或 Optimized SSA。**

```cpp
class CommandUnit : public IRUnit {
private:
    std::shared_ptr<IRStageVersion> version_; // Non-SSA
};
```

`version_` 内的基本块集合独占拥有 Non-SSA 基本块，并与 `ValueTable` 同生存期。首个基本块默认为入口。容器顺序不代表执行顺序，实际执行路径由控制流关系决定。脚本中的 Local Function 由 `IRModule` 管理，不属于 `CommandUnit`。基本块由 `CommandUnit::add_block()` 创建。

### 1.5 FunctionUnit

`FunctionUnit` 统一表示普通函数、Local Function、Nested Function、类成员函数和构造函数。

```cpp
class FunctionUnit : public IRUnit {
private:
    std::string name_;
    std::vector<SlotId> parameter_slots_;
    std::vector<SlotId> return_slots_;

    std::shared_ptr<IRStageVersion> non_ssa_version_;
    std::shared_ptr<IRStageVersion> ssa_version_;
    std::shared_ptr<IRStageVersion> optimized_ssa_version_;

    std::vector<std::unique_ptr<FunctionUnit>> nested_functions_;
};
```

| 字段 | 作用 |
|---|---|
| `name_` | 函数原始名称 |
| `parameter_slots_` | 按声明顺序排列的输入参数槽位 |
| `return_slots_` | 按声明顺序排列的输出参数槽位 |
| `non_ssa_version_` | Non-SSA 基本块和值表 |
| `ssa_version_` | SSA 基本块和值表 |
| `optimized_ssa_version_` | 优化后的 SSA 基本块和值表 |
| `nested_functions_` | 独占拥有直接嵌套的函数 |

构造 `FunctionUnit` 时仍传入源码中的入参和出参名称，但函数内部只保存有序的 `SlotId` 列表；名称与槽位属性由自身的 `SlotTable` 保存。同名入参与出参复用一个 `SlotId`，例如 `function x = f(x)`；输入和输出身份由各自的列表确定，槽位不保存入参或出参类别。`FunctionUnit::current_stage()` 记录当前处理阶段，初始为 Non-SSA，可通过 `set_current_stage()` 更新；三个 IR 阶段各自保存基本块和值表。基本块通过对应的 `stage_version(stage)->add_block(function)` 创建，只保存所属 `IRUnit*`，不保存阶段内序号。嵌套函数通过 `append_nested_function()` 加入；该接口会同步设置新函数及其已有子函数的父模块指针。`current_version()` 按 `current_stage()` 返回当前阶段版本；当前没有阶段版本替换接口。

类方法与 Local Function 不需要通过 `FunctionType`、`is_method_` 或 `is_local_` 区分。类归属、访问权限和继承关系由运行时 `object_meta` 管理。

### 1.5.1 AnonymousFunctionUnit

以下是尚未实现的目标设计：`AnonymousFunctionUnit` 保存非拥有的 `IRUnit* lexical_parent_`、按源码顺序排列的 `param_slots_`、按捕获布局排列的 `capture_slots_`，并像 `FunctionUnit` 一样分别拥有三个 IR 阶段版本。匿名函数体没有普通函数名或具名输出变量；表达式结果通过 `ReturnInst.values` 返回。`IRUnit` 已有通用 slot 表，参数和捕获槽位的名称绑定仍由后续构建流程补全。

匿名函数句柄的构造方式和捕获值表示留待后续设计。词法父指针不延长父单元生命周期。

### 1.6 嵌套函数的所有权

嵌套函数由词法父 `FunctionUnit` 独占拥有：

```matlab
function y = outer(x)
    a = 10;
    function z = inner()
        z = x + a;
    end
    y = inner();
end
```

```text
IRModule
└── FunctionUnit (outer)
    ├── Non-SSA BasicBlocks
    ├── SSA BasicBlocks
    ├── Optimized SSA BasicBlocks
    └── nested_functions_
        └── FunctionUnit (inner)
```

所有权树与词法嵌套关系一致，父函数销毁时子函数一同销毁，不再必须保存 `lexical_parent_`。如果分析阶段需要从子函数直接访问父函数，可以另外增加非拥有引用。闭包环境和捕获变量仍由运行时管理。

### 1.7 Local Function 与 Nested Function

```matlab
function y = main(x)
    y = helper(x);
    function z = inner(v)
        z = v + x;
    end
end

function y = helper(x)
    y = x * 2;
end
```

```text
IRModule
├── FunctionUnit (main)
│   └── nested_functions_
│       └── FunctionUnit (inner)
└── FunctionUnit (helper)
```

`main` 和 `helper` 由 `IRModule` 拥有，`inner` 由 `main` 拥有。模块索引只包含 `function/main`、`local/helper` 等模块级函数；`inner` 由 `main` 访问。

### 1.8 BasicBlock 所有权

每个 `IRStageVersion` 独占拥有该阶段的基本块；`CommandUnit` 和 `FunctionUnit` 持有相应版本。`BasicBlock` 保存非拥有的 `IRUnit* parent_` 和 `std::vector<std::unique_ptr<Instruction>>`；每条指令保存指向所属基本块的非拥有父指针，通过 `add_instruction<Inst>(...)` 创建并持有指令。`GotoInst` 与 `BranchInst` 使用非拥有 `BasicBlock*` 保存跳转目标，`ReturnInst` 保存按顺序返回的值；三者都是终结指令。`BasicBlock::terminator()` 返回末尾的终结指令；添加终结指令后不得再向该块追加指令。当前不保存前驱或后继表，也未检查跳转目标是否属于同一代码单元和 IR 阶段。`IRStageVersion::entry_block()` 保存入口基本块指针，创建的第一个基本块默认作为入口。

`ConstInst` 按 [M 语法 IR 设计](M语法与IR设计.md)保存 `const_ba_obj_ptr value` 和 `ValueId result`，在 Non-SSA 与 SSA 阶段使用同一种指令。`const_ba_obj_ptr` 是 `std::shared_ptr<const ba_obj>` 的别名；`value` 持有运行前确定的常量对象。运行时修改该值时仍须遵守写时复制语义。`ba_obj` 在 IR 头文件中仅前置声明，实际对象由运行时创建。`ValueId` 是只保存一个 `std::uint32_t index` 的结构体，不带无效哨兵值；构建调用处通过所属 `IRStageVersion::add_value` 分配编号并登记定义。

`ConcatInst` 保存按源码顺序排列的非空 `items`、`result`、水平或竖直 `axis` 和 `is_dynamic`，Non-SSA 与 SSA 共用此结构。空 `[]` 应使用 `ConstInst`。当前已有值定义表，但尚无类型事实；当前 builder 根据输入的 `ValueInfo.is_constant` 设置 `is_dynamic`，后续优化仍需确定更完整的动态判定规则；仅凭 `is_dynamic == false` 不能跳过运行时 `horzcat`／`vertcat` 的分派检查。

`CellConstructInst` 保存按源码顺序排列的非空 `items`、`rows`、`cols` 和 `result`，要求元素个数恰好等于行数乘列数；这也隐含行列数非零，无须分别检查。它不保存 `is_dynamic`；空 `{}` 使用 `ConstInst`。字段先放 `vector` 和 `size_t`，再放 `ValueId`，避免额外的字段间对齐空隙。

`ColonInst` 保存 `result`、`start`、可选 `step`、`stop` 和初始为 `true` 的 `needs_runtime_dispatch`。`step` 为空表示源码省略步长，与显式写出步长区分；索引中的单独 `:` 不使用该指令。当前没有保存已解析 `colon` 目标的字段，因此将 `needs_runtime_dispatch` 改为 `false` 后如何执行已解析目标，仍须在分派设计中确定。

`MagicColonInst` 只保存 `result`，表示索引中单独 `:` 的占位值，不生成序列，也不参与普通 `colon` 函数分派。索引指令的字段可引用这个结果；当前 builder 尚未生成 `MagicColonInst`。后续构建时可将其 `ValueInfo.is_constant` 标为常量；运行时占位值表示尚未实现，执行时必须按索引位置解释其语义。

`MagicEndInst` 保存按内层到外层排列的候选索引上下文及 `result`；每个上下文保存 `Operand callee_or_base`、从 1 开始的 `dim` 与 `nindices`。`Operand` 用 `std::variant<ValueId, InternedString>` 区分已求出的值与未解析名字。已确定为变量的引用先通过 `LoadSlotInst` 得到 `ValueId`，不得退回名字查找；运行时绑定失效时由槽位读取报告错误。`SlotId` 与 `ValueId` 是各自保存一个 `std::uint32_t index` 的不同结构体，二者不能混用；匿名函数参数与捕获也只保存 `SlotId`。当前 `InternedString` 仍是 `std::string` 别名，并非真正的字符串池句柄；候选上下文的尺寸由该字符串备选类型主导，后续若引入字符串池可重新测量布局。

`LoadSlotInst` 保存 `result: ValueId` 和已绑定的 `slot: SlotId`，从该槽位读取并定义一个值；`StoreSlotInst` 保存 `slot: SlotId` 与已求值的 `value: ValueId`，写入槽位但不产生结果值。两者在 Non-SSA 与 SSA 阶段共用结构。指令构造时不检查编号是否存在；构建调用处通过 `IRStageVersion::add_value` 添加值定义，槽位编号范围由所属代码单元的 `SlotTable` 查询。槽位是否属于当前单元、当前程序点是否仍绑定到全局／持久存储，以及跨调用的可观察副作用，留给后续构建、验证与执行流程处理。

`Instruction::effect()` 仅描述指令是否可能改写已有 slot：`NoSlotWrite` 包括常量、元胞构造、索引冒号占位、load、goto、branch、return 和当前全部 `IntrinsicInst`；其他当前指令保守标为 `MayWriteSlot`。它不表示指令是否会抛错或产生其他副作用。`StoreSlotInst` 的写入目标可从 `slot` 字段读取；优化 pass 可按目标精确失效缓存，其他 `MayWriteSlot` 指令在缺乏进一步信息时需保守处理。增加新的 intrinsic 时须保证其不隐式写入 slot，否则应重新审查 effect。Non-SSA 阶段的 pass 规划见[分析和优化 pass](分析与优化流程.md)。

`GlobalDeclInst` 和 `PersistentDeclInst` 各保存非空的 `std::vector<SlotId>`，都不产生 `ValueId`。声明指令中的槽位按 `SlotId` 引用；其名称由所属代码单元的 `SlotTable` 查询。`BasicBlock` 保存所属 `IRUnit*`，但加入指令时尚不检查 `PersistentDeclInst` 是否位于函数内；重复声明复用同一 slot、静态绑定冲突及运行时环境副作用的表示仍须由后续构建与执行流程确定。

`BranchInst` 按条件值选择真假目标，`GotoInst` 直接指向目标块，`ReturnInst` 结束当前代码单元；脚本或命令输入可返回空 `values`。当前只建立跳转目标与基本块末尾的结构约束，条件真假转换、返回值读取和 CFG 边的验证由后续执行与构建流程负责。

索引与成员访问按 [M 语法 IR 设计](M语法与IR设计.md)实现了 `ApplyInst`、`ValueApplyInst`、`CallInst`、`ParenReferenceInst`、`BraceReferenceInst`、`DotAccessInst`、`DotReferenceInst`、`ParenAssignInst`、`ParenDeleteInst`、`BraceAssignInst` 和 `DotAssignInst`。圆括号和点号表达式的 `Operand` 只保留未消歧名字或已求值结果；已绑定变量先生成 `LoadSlotInst`；`CallInst` 仅保存按名字调用的 `InternedString callee`，函数句柄等按值调用由 `ValueApplyInst` 处理。三种调用或应用指令的 `arguments` 均保存已求值的 `ValueId`。构造函数不再统一检查 `Operand` 中的空名字，名字有效性由语法与构建流程负责；独立的成员名等字符串字段仍各自检查。`CallInst.m_function_target` 仅指向同模块词法绑定的 M 函数，不保存外部 M 函数或插件的运行时缓存；局部函数与嵌套函数的工作区差异由词法关系处理，不增加分派类别。已确定的圆括号和花括号索引节点保存按源码顺序求值后的 `ValueId` 下标；索引与静态成员取值／赋值节点不保存 `DispatchType`，内置行为或对象协议由执行时的 base 决定。赋值与删除都输出 `updated_base`，调用处须为它分配新的值编号并负责将根变量写回。编号归属、值定义顺序和 SSA 唯一性由表及后续构建流程处理。

**待明确的设计问题：** 索引与成员取值／赋值节点已移除 `DispatchType`。若将来静态分派到对象重载方法，需要确定目标及其依赖验证保存在专门的特化表示、旁表，还是统一 lower 为 `CallInst`。复合索引链的 `PendingIndex` 运行时表示、动态成员名 `a.(expr)` 的运行时处理，以及多输出逗号分隔列表的运行时承载方式尚未实现；当前 builder 已能把 `a.(expr)` 降为以 `ValueId` 表示成员名的点号指令。

`UnaryInst` 和 `BinaryInst` 按 [M 语法 IR 设计](M语法与IR设计.md#基本运算)使用 `ValueId` 引用已求值的操作数，分别保存一元或二元 `op`、`result`，以及初始为 `true` 的 `needs_runtime_dispatch`。一元运算区分两种转置；二元运算区分矩阵、逐元素、比较与双侧求值逻辑运算。`&&` 和 `||` 应由控制流表达，不加入 `BinaryOp`。两个指令在 Non-SSA 与 SSA 阶段共用结构；构造函数检查操作种类，编号是否已定义由 `ValueTable` 和后续构建流程确定。

`IntrinsicInst` 表示已确定的内部语义，不预先指定执行方式。`SwitchMatch` 保存选择值与 case 值两个输入，并产生供 `BranchInst` 使用的单个结果。`ForeachInit`、`ForeachCount`、`ForeachGet`、`IntGt` 和 `IntAdd` 用于 `for` 循环的迭代状态、取值和内部整数运算；`LogicalScalar` 用于短路操作数的标量逻辑转换。它们不得隐式写入已有 slot，故当前全部返回 `NoSlotWrite`；普通 M 函数调用仍使用 `CallInst`。

**待明确的分派设计：** 当前 `needs_runtime_dispatch` 可以改为 `false`，但指令没有保存已解析目标或内置实现标识。设计文档要求二元运算静态解析到用户函数后仍保留源码操作符信息；`dev_zj` 分支的旧说明则建议将这种运算 lower 为 `CallInst`。两者对目标存放位置和运算符信息的保留方式不同，需要在实现静态分派前确定。当前版本仅定义动态分派初始状态，不预设静态解析策略。

函数句柄的构造指令及匿名函数体的代码单元结构均尚未实现。

### 1.9 IR 构建流程

完整的文件级构建流程应负责以下工作；当前 `IRBuilder` 已实现脚本、函数文件及其局部和嵌套函数的 IR 构建：

1. 读取一次源码，确定 `SourceType` 和 `ModuleType`，创建 `IRModule`；源码文本仅供本次解析使用。
2. 根据源码结构在调用处创建 `CommandUnit` 和模块级 `FunctionUnit`；函数文件的函数列表以主函数开头，再设置到 `IRModule`。
3. 遇到 Nested Function 时，由词法父函数创建并加入 `nested_functions_`。
4. 为可执行单元构建 Non-SSA IR；类属性默认值表达式的表示方式另行设计，不在此时求值。
5. 检查模块级函数的限定键唯一、入口与所有权关系，完成后发布模块；这些最终检查尚无统一接口。
6. 后续按需生成 SSA 和 Optimized SSA；发布新版本时遵守 4.1 节的生命周期约束。

当前 `IRBuilder::build_file(path)` 先查询文件缓存，有效时返回已有的 `shared_ptr<IRModule>`；否则解析脚本或函数文件、构建新模块并注册缓存。脚本主体进入 `CommandUnit`，文件级函数进入模块函数列表，嵌套函数由词法父函数拥有。`IRBuilder` 也可接收单个代码单元的 AST，统一完成已支持语法的 Non-SSA lowering：常量、并置、元胞、冒号、一元及二元运算、赋值、圆括号取值及赋值／删除、花括号取值及赋值、点号访问及赋值、短路逻辑赋值与条件、全局及持久声明、`if`、`switch`、`for`、`while`、`break`、`continue` 和 `return`。`IRBuilder` 持有当前插入基本块，构建控制流时切换插入位置；指令创建由其私有方法完成。参数及返回变量已有的 slot 会被复用；每个 `for` 分配独立的内部下标 slot。未支持的 AST 节点直接报错，不由测试用例另行实现 lowering。完整的名字解析及独立 IR 检查仍属于后续构建流程。

不单独设置 `IRManager`。

---

## 二、IR 怎么缓存

### 2.1 缓存范围

**`IRCache` 只缓存 `.m` 源文件生成的 `IRModule`。**

| 源码来源 | 是否缓存 |
|---|---|
| 普通 `.m` 函数文件 | 是 |
| `.m` 脚本文件 | 是 |
| `classdef` 类文件 | 是 |
| 类文件夹中的 `.m` 方法文件 | 是 |
| 命令行输入 | 否 |
| 临时内存代码 | 否 |

命令行和临时内存代码通过上述构建流程得到临时模块，由调用方管理生命周期；若创建的函数句柄仍引用其函数 IR，句柄也必须保持该模块存活。

### 2.2 缓存粒度

以单个 `.m` 文件为单位，一个文件对应一个 `IRModule`，不为 Local Function 或 Nested Function 建立独立文件级缓存。

```text
IRCache
└── /project/calculate.m
    └── CacheEntry
        └── IRModule
            ├── FunctionUnit (calculate)
            └── FunctionUnit (helper)
```

查询条目并取得模块后，通过 `function_map` 定位模块直接拥有的目标函数；嵌套函数由其词法父函数访问。

### 2.3 IRCache 结构

第一阶段缓存按规范化的绝对 `.m` 文件路径建立条目。路径解析由调用方完成；`IRCache` 不负责搜索路径、函数分派或类方法解析。同一物理文件通过不同符号链接到达时可能出现多个条目，是否合并这类路径留待统一的路径身份规则确定。

`WorldId` 定义在 `world_counter.h` 中，缓存复用该类型。

```cpp
struct CacheEntry {
    std::shared_ptr<IRModule> module;
    std::filesystem::file_time_type mtime;
    WorldId world = 0;
    bool valid = true;

    bool is_valid();
};

class IRCache {
public:
    static IRCache& instance() noexcept;
    CacheEntry* find(const std::filesystem::path& path) const;
    CacheEntry* register_module(std::shared_ptr<IRModule> module);
    void remove(const std::filesystem::path& path);

private:
    IRCache() = default;
    std::unordered_map<std::filesystem::path,
                       std::unique_ptr<CacheEntry>> entries_;
};
```

| 字段 | 作用 |
|---|---|
| `module` | 持有已构建的 IRModule |
| `mtime` | 保存注册时的文件修改时间 |
| `world` | 上次完成文件有效性检查时的全局世界计数 |
| `valid` | 条目有效标记，显式失效或文件变化时置为 `false` |
| `entries_` | 文件路径到当前缓存条目的映射 |

`CacheEntry::is_valid()` 先检查 `valid`；若条目的 `world` 与全局 `WorldCounter::current()` 相同，则直接有效。两者不同时才检查文件修改时间；文件未变化则更新 `world` 并复用条目，文件已变化或无法读取则标记失效并返回 `false`。缓存不保存源码字节或外部依赖验证记录，也不负责读取源码。缓存只按路径做点查找，不要求有序遍历，因此使用 `std::unordered_map`。

`IRCache` 通过 `instance()` 提供进程内唯一实例，禁止复制和移动；调用方共享同一份文件缓存。缓存操作仍按单线程调用设计。

### 2.4 缓存获取流程

```text
已解析的绝对 .m 路径
          │
          ▼
调用 IRCache::find(path)，取得缓存条目
          │
          ▼
有条目时调用 CacheEntry::is_valid()
          │
     ┌────┴────┐
     │         │
   有效          未命中或已失效
     │         │
 复用旧条目      调用处读取源码并构建新模块
                    │
                    ▼
              调用 register_module(module)
```

`IRCache` 只提供 `instance()`、`find(path)`、`register_module(module)` 和 `remove(path)`。缓存以 `unique_ptr<CacheEntry>` 独占条目；`find` 与 `register_module` 返回非拥有的 `CacheEntry*`，调用方检查条目的 `is_valid()`。对应路径的条目被替换或移除后，先前取得的指针立即失效；调用方若要在此后继续使用模块，应事先复制条目的 `shared_ptr<IRModule>`。当前 `IRBuilder::build_file(path)` 完成这段查找、构建和注册流程。注册时读取文件修改时间，记录当前全局世界计数。缓存本身不负责构建或读取源码。

文件消失、无法读取或调用处解析失败时，本次加载返回错误，缓存中旧条目不作为新请求的结果；已持有旧模块的执行者仍可继续使用它。构建成功后再注册新条目，避免失败构建破坏旧条目。

### 2.5 世界计数

源文件内容决定本节缓存的基础 `IRModule` 版本；世界计数反映运行时函数解析环境或优化依赖，不能代替文件版本。世界变化本身不要求重新读取并构建相同源码的基础 IR。

全局当前世界由 `WorldCounter` 单例保存，缓存条目的 `world` 使用同一个 `WorldId` 类型：

```cpp
using WorldId = std::uint64_t;

class WorldCounter final {
public:
    static WorldCounter& instance() noexcept;
    WorldId current() const noexcept;
    WorldId advance();

private:
    WorldId current_ = 0;
};
```

初始世界为 0。参考 `object_meta` 的检查方式，调用处在每轮可能观察到文件变化的交互或执行前调用 `advance()`；同一轮内 `CacheEntry::is_valid()` 可通过 `world` 跳过重复的文件时间检查。若没有推进全局世界，同一轮内的文件修改不会被检查。当前仓库尚无交互执行入口，因此只实现计数器及条目检查，推进时机由后续运行时调用处保证。第一阶段按单线程调用设计，计数达到 `WorldId` 上限时报错，不回绕。

世界计数变化不意味着所有 IR 都必须重建。如果调用目标在运行时动态解析，路径变化可能只需重新解析调用目标；如果 Optimized SSA 绑定了特定函数或进行了内联，则可能需要重新验证或重新生成优化结果。

`world` 只记录文件有效性检查的轮次，不表示函数调用或优化结果已完成世界依赖验证。一个模块可能有多个函数、多个优化版本和不同依赖；逐版本依赖验证与所有权仍需按 4.1、4.3 节设计。

### 2.6 所有权与生命周期

```text
IRCache
└── unique_ptr<CacheEntry>
    └── shared_ptr<IRModule>
        ├── unique_ptr<CommandUnit>
        │   └── BasicBlocks
        └── vector<unique_ptr<FunctionUnit>>
            ├── BasicBlocks
            └── nested_functions_
                └── FunctionUnit
                    └── BasicBlocks
```

运行时执行帧或函数句柄可同时保存：

```cpp
std::shared_ptr<IRModule> module;
FunctionUnit* function;
```

`module` 保证整个所有权树存活，`function` 为非拥有指针，可指向任意层级的函数。

### 2.7 缓存失效

检测到源文件修改时间变化时，由调用处重新解析并创建新 `IRModule`，替换同一路径的缓存条目，不原地修改旧模块。当前不比较新旧源码内容；即使内容相同，修改时间变化也会触发重建：

```text
IRCache → unique_ptr<CacheEntry V2> → shared_ptr<IRModule V2>
执行中的函数 → shared_ptr<IRModule V1>
```

缓存映射只保存该路径当前条目。替换或移除时，旧 `CacheEntry` 被销毁，先前取得的 `CacheEntry*` 失效；执行者若持有旧模块的 `shared_ptr`，模块仍可存活，并在最后一个引用释放后销毁。旧 IR 生命周期与旧世界函数定义的可见性属于不同问题。`remove(path)` 移除缓存条目，不得使执行中持有的模块失效。

### 2.8 缓存总结

`IRCache` 负责 `.m` 文件缓存条目的查询、注册和移除；源码读取由构建调用处完成，修改时间检查由 `CacheEntry` 管理。由 `.m` 路径构建的模块即使通过 `baltam_ir` 工具加载，也进入该进程的缓存；直接输入的命令和临时内存 IR 不进入缓存。缓存不负责类元数据、世界依赖验证或优化算法。第一阶段按单线程调用设计，不引入锁或并发构建去重；若运行时允许并发访问缓存，须先定义同步和发布规则。

---

## 三、类文件夹怎么处理

### 3.1 文件夹与模块的关系

`@ClassName` 类文件夹是类方法的组织与解析单位，**不是一个 `IRModule`**。文件夹中的每个 `.m` 文件仍独立生成并缓存一个 `IRModule`，不需要 `ClassModule` 或 `ClassIRCache`。

例如：

```text
project/
└── @MyClass/
    ├── MyClass.m
    ├── calculate.m
    └── display.m
```

对应的 IR 缓存为：

```text
IRCache
├── /project/@MyClass/MyClass.m   → IRModule
├── /project/@MyClass/calculate.m → IRModule
└── /project/@MyClass/display.m   → IRModule
```

### 3.2 方法解析与函数组织

运行时 `object_meta` 负责识别类文件夹中的方法，并保存类归属、访问权限及继承关系。调用方法时，先通过 `object_meta` 定位 `.m` 文件，再从 `IRCache` 获取对应 `IRModule` 和 `FunctionUnit`。

方法文件中的 Local Function 仍由该文件的 `IRModule` 拥有；Nested Function 则由其词法父 `FunctionUnit` 拥有。它们不会因为位于 `@ClassName` 文件夹内就自动成为类方法。

### 3.3 缓存失效

- **方法实现修改**：只使对应 `.m` 文件的 IR 缓存失效。
- **方法增删或类结构变化**：由运行时重新验证 `object_meta`；未修改的 `.m` 文件 IR 可以继续复用。

类元数据缓存和 IR 缓存分别管理，通过方法解析与世界计数协调有效性。

---

## 四、实施前待解决的问题

以下问题不改变“一个源码输入对应一个 `IRModule`”的总体方向，但当前字段示意不足以保证后续 SSA、缓存更新及类定义的正确性。对应结构在实现前需要进一步定案。

### 4.1 优化 IR 的版本与执行中生命周期

世界计数变化后可能重新生成 Optimized SSA。同一模块内更新优化结果时，正在执行旧优化 IR 的 frame、JIT 代码或 deopt 信息可能仍引用旧基本块及指令，因此需要保留旧阶段版本。

`IRStageVersion` 共同拥有基本块集合及值表，阶段由 `FunctionUnit` 的对应版本字段决定，版本对象本身不保存阶段；SSA／Optimized SSA 重新生成时创建新版本，持有旧版本的执行者可继续使用旧基本块和值表。构建调用处应在版本发布后停止修改它，并由执行帧持有正在使用的版本；当前接口尚未强制禁止发布后的修改。

### 4.2 各阶段 IR 的元数据与 deopt 对应关系

三个基本块集合不能单独构成完整的可执行 IR。基础元数据已按以下 `SlotTable`、`ValueTable` 设计并实现。动态绑定状态、源码位置，以及 SSA 指令到 Non-SSA 恢复位置的对应关系仍需后续设计。Optimized SSA 还需要保存优化依赖和相应的回退信息。

#### SlotTable

每个 `IRUnit` 拥有一张 `SlotTable`，供该代码单元的 Non-SSA、SSA 和 Optimized SSA 阶段共用；`CommandUnit` 仅使用 Non-SSA。`SlotId` 只在所属代码单元内有效，不能直接用于另一个函数或匿名函数体。表用 `std::vector<SlotInfo>` 按 `SlotId` 索引，编号从零连续分配，不在表项中重复保存编号。表项保存名称：

```cpp
struct SlotInfo {
    InternedString name;
};
```

构建调用处通过表分配 `SlotId`，`SlotTable::find()` 按 ID 返回表项的常量引用，越界时抛出 `std::out_of_range`；名字解析依赖词法作用域和运行时绑定，不能仅用这张表的名称作通用查找键，也不强制所有表项名称唯一。全局和持久变量当前是否仍绑定由运行时状态决定。表在模块发布后不再原地追加；优化版本若需要新的存储槽位，须另行确定版本私有槽位的归属。

#### ValueTable

每个代码单元的每个 IR 阶段版本各有一张 `ValueTable`。`ValueId` 只在该代码单元的该阶段版本内有效；不同阶段、不同版本或不同代码单元可以出现相同的数字编号。表用 `std::vector<ValueInfo>` 按 `ValueId` 索引，表项不重复保存编号：

```cpp
struct ValueInfo {
    Instruction* def;      // 非拥有，指向同一阶段版本中的指令
    std::uint32_t index;    // 定义指令的第几个结果，从 0 开始
    enum class Form : std::uint8_t { Single, MaybePack } form;
    bool is_constant;
};
```

一个结果编号只绑定一条定义指令；`CallInst` 等多结果指令的不同 `ValueId` 分别保存各自的 `index`。`form` 默认为 `Single`；花括号取值等可能产生逗号分隔列表的结果在绑定时标记为 `MaybePack`。它只表示该结果可能承载多个值，不记录运行时元素数量，也不自动实现函数实参或出参的展开；是否标记由构建调用处根据表达式语境决定。`is_constant` 默认为 `false`，当前 builder 将 `ConstInst` 的结果标为常量，并依据输入标记传播到 `ConcatInst`、`ColonInst`、`UnaryInst` 和 `BinaryInst` 的结果；后续生成 `MagicColonInst` 时也可将其结果标为常量；索引冒号仍须由指令类型区分语义。类型和 shape 事实暂不加入表项，待运行时类型与维度表示确定后再设计。`IRStageVersion::add_value(ValueInfo)` 检查定义指令确实由该版本的基本块持有，再追加信息并返回新 `ValueId`。调用处先根据 `values().size()` 确定指令结果编号，创建指令后再追加信息；越界编号查询抛出 `std::out_of_range`。多结果指令的值表登记方式尚待设计。

`ValueInfo::def` 的生命周期依赖同阶段的基本块。当前基本块集合与 `ValueTable` 已放在同一个 `IRStageVersion` 中，替换阶段版本时一起替换。所有值在创建时即绑定定义；`IRStageVersion::add_value()` 会拒绝空指针和其他版本拥有的指令。`SlotTable` 归代码单元所有，不随单个 SSA 版本重建。

`LoadSlotInst`／`StoreSlotInst`、全局／持久声明以及普通函数的入参／出参列表已只保存 `SlotId`，名称通过所属代码单元的 `SlotTable` 查询。匿名函数参数与捕获也只保存 `SlotId`。

按[新解释器架构](新解释器架构.md)和[Deopt 设计思路](SSA到非SSA的运行时回退设计.md)，SSA 回退需要恢复 slot 的值及绑定状态，并区分调用前、调用后和异常路径的继续位置。后续应为每个 IR 版本固定这些元数据的归属与生命周期，避免基本块已更新而映射仍指向旧版本。

### 4.3 世界验证与源文件版本判定

当前 `CacheEntry` 不保存外部依赖验证结果。优化 pass 需要记录所依赖的函数查找、分派目标或“没有用户自定义实现”等结论；世界变化后逐项检查，才能判断对应优化版本是否仍有效。动态解析的调用是否需要重新验证，仍待确定。

源码修改时间检查由 `CacheEntry` 管理，避免 `IRModule` 承担缓存状态。`world` 仅是文件检查的快速路径，不能用于判断函数及其优化版本的依赖是否有效。确定优化版本的所有权时，应再设计逐版本验证状态。

当前条目先用 `world` 与全局世界计数比较；相同时直接命中，不同时才检查 `mtime`。修改时间变化时调用处重新读取并构建模块。路径规范化与符号链接身份规则尚未最终确定。

### 4.4 `classdef` 文件中的可执行内容

“类文件夹中的每个 `.m` 文件对应一个 `IRModule`”可以保留，但 `classdef` 文件本身不能只按普通函数文件处理。类定义中可能有内联方法、构造函数、属性访问方法以及属性默认值表达式；其中部分方法必须定义在 `classdef` 文件内，默认值表达式在首次需要时求值，而非每次实例化时重算。参见 [MathWorks：Methods in Separate Files](https://www.mathworks.com/help/matlab/matlab_oop/methods-in-separate-files.html) 与 [Initialize Property Values](https://www.mathworks.com/help/matlab/matlab_oop/initialize-property-values.html)。

内联方法由 `functions_` 持有；类声明和属性默认值表达式暂时没有专用 IR 结构。实现类语法前需确定默认值表达式使用的 IR 单元、`object_meta` 的引用接口，以及类声明版本变化时如何同时验证类元数据与相关 IR。`object_meta` 管理类归属和访问规则，并不能代替这些表达式的 IR 表示。

### 4.5 构建接口与当前代码状态

当前仓库已实现 `IRModule`、代码单元、基本块、针对已支持语法的 `IRBuilder`，以及文件来源的 `IRCache`。`IRBuilder` 支持从脚本和函数文件构建模块，并为脚本主体、局部函数及嵌套函数生成 Non-SSA IR；动态名字消歧、独立 IR 检查和未覆盖语法仍待实现。当前 builder 生成的指令已有文本输出；`MagicColonInst` 和 `MagicEndInst` 等尚未由 builder 生成的指令仍需补充 `to_string()`。

---

## 五、结论

Baltam 的 IR 系统采用以 `IRModule` 为核心的所有权模型。

`IRModule` 组织一次源码输入产生的可执行 IR：`CommandUnit` 仅保存 Non-SSA 基本块；`FunctionUnit` 管理 Non-SSA、SSA、Optimized SSA 基本块；类文件由 `ModuleType::ClassFile` 标识，类声明和默认值表达式的表示方式待定。模块直接包含的函数由 `IRModule` 拥有并加入模块索引，Nested Function 由词法父 `FunctionUnit` 拥有且不进入模块索引；函数文件入口按约定取 `functions_.front()`。

`IRCache` **只缓存 `.m` 文件生成的基础 IR**，按规范化文件路径查询、注册和移除条目；`CacheEntry::is_valid()` 通过全局世界计数与文件修改时间判断条目是否有效。全局当前世界由 `WorldCounter` 单例保存；外部依赖验证与优化版本所有权仍待定。缓存独占 `CacheEntry`，执行者通过 `shared_ptr<IRModule>` 保证仍被引用的旧 IR 可以继续存活。直接输入的命令和临时内存 IR 不进入缓存。

对于 `@ClassName` 类文件夹，每个 `.m` 方法文件独立生成和缓存 `IRModule`，文件夹本身不构成 IR 模块。类归属、方法解析和继承关系由运行时 `object_meta` 独立管理。

整体上，**IR 组织、IR 缓存和运行时类元数据各司其职**，避免重复所有权、重复缓存以及职责交叉。第四节列出的版本生命周期、deopt 元数据、依赖验证、`classdef` 表示和构建接口仍须在实现相应功能前确定。
