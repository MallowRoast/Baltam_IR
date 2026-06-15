# Matlab IR Schema

本文记录当前 `src/ir` 中实际存在的语义 IR schema。历史草案和已经删除的结构不再在这里展开；
需要看设计背景时可参考 [ir_draft.md](./ir_draft.md)、[ir_lowering_design.md](./ir_lowering_design.md)
和 [anonymous_function_handle_design.md](./anonymous_function_handle_design.md)。

## 当前范围

当前实现覆盖：

- `.m` 文件输入
- `script`、`function`、文件内 `local function`
- module 级匿名函数表和 `AnonymousFunctionUnit`
- 具名函数句柄、匿名函数句柄、`apply`、`value_apply`、`call`
- 多返回值调用和左值列表，`~` 占位输出位用空 `ValueId` 表示
- `if`、`switch`、`for`、`while`、`break`、`continue`、显式 / 隐式 `return`

当前仍未完整覆盖：

- `CommandUnit` / REPL lowering。源码中只保留 `CodeUnit::Command` 和 `CommandUnit` 占位。
- 嵌套函数、`try/catch`、`global`、`persistent`
- 完整索引语义、成员访问、完整 Matlab 函数优先级和路径语义

## 1. 基础 ID

当前使用 `EntityId<Tag>` 定义强类型 ID：

- `SlotId`：当前 `CodeUnit` 内 frame slot 的稳定 layout 句柄。
- `ValueId`：当前 `CodeUnit` 内指令结果值的稳定句柄。
- `AnonymousFunctionId`：当前 `IRModule` 内匿名函数体的稳定句柄。

`SlotId` 和 `ValueId` 按 `CodeUnit` 局部分配；`AnonymousFunctionId` 按 `IRModule` 分配。
无效 ID 用对应的 `Invalid*Id` 常量表示。

`SlotId` 只说明 layout 中存在对应 slot。对用户可见的 `Arg` / `Ret` / `Local` 名字，当前
activation 中该名字是否仍 live 还属于运行时 binding state；`clear a` 或
`eval('clear a')` 可以让 `a -> SlotId` 的 live binding 失效。

## 2. IRModule / MFileUnit

`IRModule` 是当前构建会话的顶层拥有者：

```text
IRModule
  files               : unique_ptr<MFileUnit>[]
  anonymous_functions : AnonymousFunctionTable
```

`MFileUnit` 对应一个 `.m` 文件：

```text
MFileUnit
  module             : IRModule*
  path               : NormalizedPath
  code_units         : unique_ptr<CodeUnit>[]
  entry_unit         : CodeUnit*
  local_function_map : map<InternedString, FunctionUnit*>
```

约束：

- `IRModule::files` 拥有所有文件单元。
- `MFileUnit::module` 指回所属 module。
- `MFileUnit::code_units` 只拥有该文件直接定义的 `ScriptUnit` / `FunctionUnit`。
- 匿名函数体不由 `MFileUnit` 拥有，而由 `IRModule::anonymous_functions` 拥有。
- `entry_unit` 必须指向当前文件拥有的入口代码单元。
- `local_function_map` 只记录当前文件内的 local `FunctionUnit`。

文件类型通过 `entry_unit` 推导：

- `entry_unit->is_script()`：脚本文件。
- `entry_unit->is_function()`：函数文件。

## 3. CodeUnit 类型

`CodeUnit` 是所有可执行代码体的抽象基类：

```text
CodeUnit
  name         : InternedString
  slot_table   : SlotTable
  value_table  : ValueTable
  entry_block  : BasicBlock*
  basic_blocks : unique_ptr<BasicBlock>[]
  source_span  : SourceSpan
```

`CodeUnit` 基类不保存统一的 `MFileUnit* parent` 或 `IRModule* module`。这些关系按实际语义
放在派生结构上：

- `ScriptUnit::file -> MFileUnit`
- `FunctionUnit::file -> MFileUnit`
- `AnonymousFunctionUnit::lexical_parent -> CodeUnit`
- `MFileUnit::module -> IRModule`

当前 `CodeUnit::Type` 包括：

- `Script`
- `Function`
- `AnonymousFunction`
- `Command`

`CommandUnit` 目前只是 REPL / 命令行输入的类型占位，不接入 builder、lowering、printer 或
verifier 的主路径，也不由 `IRModule` 拥有。

### ScriptUnit

```text
ScriptUnit : CodeUnit
  file : MFileUnit*
```

脚本中静态出现的变量会进入当前脚本单元的 slot 表，slot tag 为 `ScriptVar`。基础
lowering 不再生成 workspace 读写指令；运行时可以根据 `ScriptVar` slot 的绑定状态决定
直接访问已绑定存储，或在绑定失效后退回动态 lookup。

### FunctionUnit

```text
FunctionUnit : CodeUnit
  file         : MFileUnit*
  param_slots  : Slot[]
  return_slots : Slot[]
```

`param_slots` 和 `return_slots` 按源码声明顺序保存接口 slot。参数名、返回值名和源码位置由
对应 `Slot` 提供。

### AnonymousFunctionUnit

```text
AnonymousFunctionUnit : CodeUnit
  id             : AnonymousFunctionId
  lexical_parent : CodeUnit*
  param_slots    : Slot[]
  capture_slots  : Slot[]
```

匿名函数体没有 Matlab 名字空间中的函数名，也没有 `return_slots`。其 body 直接通过
`ReturnInst` 返回表达式 lowering 后的 `ValueId`。

`lexical_parent` 记录匿名函数表达式出现的位置，用于诊断、source 归属和 local function lookup。

## 4. Slot

`SlotTable` 是当前 `CodeUnit` 的 slot 定义表：

```text
SlotTable
  slots : SlotInfo[]
```

`Slot` 是变量的静态身份：

```text
Slot
  id  : SlotId
  tag : SlotTag
```

`SlotInfo` 保存 slot 的 side metadata：

```text
SlotInfo
  slot        : Slot
  name        : InternedString
  source_span : SourceSpan
  value_type  : Unknown | Int64Scalar | LogicalScalar
```

`SlotTag` 当前包括：

- `BaseVar`
- `ScriptVar`
- `Local`
- `Arg`
- `Ret`
- `Capture`
- `InternalLocal`
- `Global`
- `Persistent`
- `Nargin`
- `Nargout`
- `Varargin`
- `Varargout`

约束：

- `Slot` 是否有效只由 `SlotId` 决定；`SlotTag` 不包含 `Invalid` 哨兵。
- `ScriptVar` 只能出现在 `ScriptUnit`。
- `Capture` 只能出现在 `AnonymousFunctionUnit`。
- `Nargin / Nargout / Varargin / Varargout` 只能出现在 `FunctionUnit`。
- `Nargin / Nargout / Varargin / Varargout` 在同一个 `CodeUnit` 中同一 tag 至多出现一次。

## 5. ValueTable

`ValueTable` 是 `ValueId` 的 side metadata 表：

```text
ValueTable
  values : ValueInfo[]
```

`ValueInfo`：

```text
ValueInfo
  value_id     : ValueId
  result_index : size_t
  type_fact    : TypeFact
  def          : Instruction*
```

说明：

- `IRBuilder::create_value()` 会先创建一条 `ValueInfo` 占位记录。
- `append_instruction()` 会把产生结果的指令绑定到 `ValueInfo::def`。
- 多结果指令用 `result_index` 记录该 `ValueId` 在结果列表中的位置。
- 多返回值调用中的 `~` 占位输出位用 `InvalidValueId` 保留位次，不进入 `ValueTable`，也没有
  `ValueInfo`。

`TypeFact` 是保守的构建期种子事实，不等价于完整类型推断。当前已知事实包括：

- 常量指令按常量种类写入精确类型。
- `CreateNamedFunctionHandleInst` / `CreateAnonymousFunctionHandleInst` 结果是
  `function_handle scalar`。
- 带 `value_type` 的 slot 被 `LoadSlotInst` 读取时可产生固定类型事实。
- 部分 internal helper 有手写摘要，例如 `internal.foreach_init` 和
  `internal.switch_match`。`internal.end_index` 可作为后续 magic-end 降级策略，但基础
  lowering 当前不直接生成它。

## 6. BasicBlock

```text
BasicBlock
  parent        : CodeUnit*
  label         : InternedString
  source_span   : SourceSpan
  instructions  : unique_ptr<Instruction>[]
  predecessors  : BasicBlock*[]
  successors    : BasicBlock*[]
```

约束：

- `parent` 必须指向所属 `CodeUnit`。
- `entry_block` 必须属于 `basic_blocks`。
- 终结类指令和普通指令都存放在 `instructions` 中。
- 若 block 中存在 terminator，它必须是最后一条指令。
- CFG 目标同时保存在 terminator 指令和 `predecessors/successors` 中，verifier 会检查两者一致。

## 7. Operand / Constant / Op

`Operand`：

```text
Operand = variant<ValueId, Slot, InternedString>
```

字面量不直接进入 `Operand`，必须先经 `ConstInst` 物化为 `ValueId`。

`Constant` 当前包括：

- `LogicalConstant`
- `Int64Constant`
- `UInt64Constant`
- `Float64Constant`
- `Complex128Constant`
- `CharLiteralConstant`
- `StringLiteralConstant`
- `EmptyDoubleMatrixConstant`

`UnaryOp` 当前包括：

- `Uplus`
- `Uminus`
- `LogicalNot`
- `Transpose`
- `Ctranspose`

`BinaryOp` 当前包括算术、点算术、逻辑和比较操作：

- `Add/Sub/Mul/Rdiv/Ldiv/Pow`
- `ElemMul/ElemRdiv/ElemLdiv/ElemPow`
- `And/Or`
- `Lt/Le/Gt/Ge/Eq/Ne`

## 8. Instruction

所有指令继承自 `Instruction`：

```text
Instruction
  parent      : BasicBlock*
  type        : Instruction::Type
  effect      : EffectClass
  source_span : SourceSpan
  attrs       : InstAttrs
```

`EffectClass` 当前包括：

- `Pure`
- `Frame`
- `Heap`
- `Env`
- `Opaque`

`InstAttrs` 当前包括：

- `is_synthetic`

### 普通数据与环境指令

- `ConstInst`
  - `result : ValueId`
  - `value : Constant`
- `LoadSlotInst`
  - `result : ValueId`
  - `slot : Slot`
- `StoreSlotInst`
  - `slot : Slot`
  - `value : Operand`
- `CopyInst`
  - `result : ValueId`
  - `value : Operand`

`LoadSlotInst` / `StoreSlotInst` 表示固定 frame offset 的 slot 访问。对用户可见 slot，
`LoadSlotInst` 的语义前提是当前程序点已经证明该名字仍是 live frame-slot binding；
`StoreSlotInst` 会写入 slot，并可重新建立对应名字的 live binding。这个约束不适用于用户
不可见的 `InternalLocal` / `Hidden` slot。

### 函数句柄与应用

- `CreateNamedFunctionHandleInst`
  - `result : ValueId`
  - `name : InternedString`
  - `resolution_mode : Runtime | Static`
  - `bound_dispatch_type : Dynamic | Builtin | Internal | MFunction`
  - `m_function_target : FunctionUnit*`
- `CreateAnonymousFunctionHandleInst`
  - `result : ValueId`
  - `function_id : AnonymousFunctionId`
  - `captures : CaptureValue[]`

`CaptureValue`：

```text
CaptureValue
  name           : InternedString
  source_slot    : Slot
  captured_value : ValueId
```

`source_slot` 的含义取决于外层 unit：

- 外层是函数 / 匿名函数体：被捕获变量自己的静态 slot。
- 外层是脚本：被捕获变量自己的 `ScriptVar` slot。

真正表达构造点捕获值的是 `captured_value`。

- `ApplyInst`
  - `results : ValueId[]`
  - `callee_or_base : Operand`
  - `arguments : Operand[]`
- `ValueApplyInst`
  - `results : ValueId[]`
  - `base : ValueId`
  - `arguments : Operand[]`
- `MagicEndInst`
  - `result : ValueId`
  - `candidate_contexts : MagicEndInst::MagicEndContext[]`
- `CallInst`
  - `results : ValueId[]`
  - `callee_kind : Direct | Indirect`
  - `dispatch_type : Dynamic | Builtin | Internal | MFunction`
  - `callee : Operand`
  - `m_function_target : FunctionUnit*`
  - `arguments : Operand[]`

`ApplyInst` 保留尚未消歧的源码层 `A(...)`。`ValueApplyInst` 表示 base 已经是运行时值，
但尚未分派为函数句柄调用或圆括号索引。`CallInst` 只表达已经确认是调用的语义。

`MagicEndInst` 是 magic `end` 的 canonical high-level IR。`candidate_contexts` 按内层到
外层排列，每项记录候选 `callee_or_base`、当前维度 `dim` 和总索引数 `nindices`。单层
`A(end)` 生成一个候选上下文；`A(fun(end))` 会保留 `fun -> A` 两层候选：若
`fun(end)` 运行时解析为索引，则 `end` 属于 `fun`；若 `fun(end)` 解析为普通函数调用，
则继续向外尝试 `A(...)`。后续名字解析、专用 pass 或 runtime 可把它收敛为专用 bytecode
或等价的 `internal.end_index(base, dim, nindices)`。`end` 不参与普通函数名查找，用户
自定义 `end.m` 不是合法候选；类对象索引中的自定义 `end` 方法由处理 `MagicEndInst` 的
阶段负责。
`callee_or_base` 的 Operand 种类保留候选是否已经绑定的信息：`InternedString` 是尚未解析的
名字候选；`Slot` / `ValueId` 是已绑定变量或数据流候选。已绑定候选遇到 cleared / unbound
slot 时应报变量引用错误，不能回退到同名函数解析或外层候选。
文本 IR 将每个候选显示为 `(base, dim, nindices)`，方便和具体索引层对照。
`MagicEndInst` 的构建期结果类型事实保持 `unknown`，后续只有在候选 base 和对象索引协议
明确后才能收敛；基础 IR 不假设类自定义 `end` 方法的返回类型。
更完整的语义说明见 [Magic End Lowering 设计](./magic_end_design.md)。

`results` 允许保留空输出位：当源码写 `[~, b] = f()` 时，第一个输出位为
`InvalidValueId`，printer 显示为 `[]`，verifier 和 value table 绑定会跳过该位。

### 运算指令

- `UnaryInst`
  - `result : ValueId`
  - `op : UnaryOp`
  - `dispatch_type : Dynamic | Builtin | Internal`
  - `operand : Operand`
- `BinaryInst`
  - `result : ValueId`
  - `op : BinaryOp`
  - `dispatch_type : Dynamic | Builtin | Internal`
  - `lhs : Operand`
  - `rhs : Operand`

基础 lowering 不会仅因为存在同名 local 函数就把运算符静态分派到 M 函数。`MFunction`
分派应由后续名字解析 / 优化 pass 在完整考虑 `import`、`private`、路径和遮蔽规则后生成。

### 终结指令

- `GotoInst`
  - `target : BasicBlock*`
- `BranchInst`
  - `condition : Operand`
  - `true_target : BasicBlock*`
  - `false_target : BasicBlock*`
- `ReturnInst`
  - `values : ValueId[]`

## 9. 打印约定

当前文本 IR 的几个重要约定：

- block label 顶格打印，指令缩进两个空格。
- block label 默认携带前驱注释，格式为 `label: ; preds = [%pred0, %pred1]`。
  没有前驱的入口块打印为 `entry: ; preds = []`。`ir_print --no-cfg` 可关闭这类注释。
- slot 表打印在 unit 头部，slot 条目比 `; slots:` 再缩进一级。
- `LoadSlotInst` 打印为 `load %slotN`。
- `StoreSlotInst` 打印为 `store %slotN, value`。
- `CreateNamedFunctionHandleInst` 打印为 `create_named_func_handle @name runtime` 或
  `create_named_func_handle @name static builtin/mfunction ...`。
- `dispatch_type = MFunction` 的 call 打印为 `call mfunc @name(...)`。
- `dispatch_type = Internal` 的 direct call 打印为 `call @internal.name(...)`。
- `ValueApplyInst` 打印为 `value_apply(%base, ...)`。
- `MagicEndInst` 打印为
  `[%0, unknown] = magic_end([(inner, 1, 1) -> (outer, 1, 1)])`。
- 多返回值输出打印为 `([%0, type], [%1, type]) = ...`。
- 空输出位打印为 `[]`，例如 `([], [%4, unknown]) = call mfunc @pair_ops(...)`。
- 行尾源码注释默认只显示源码行号，例如 `; line 11-13`。需要折叠后的源码片段时，
  使用 `ir_print --source-full`。

## 相关文档

- [IR Lowering 设计](./ir_lowering_design.md)
- [IR Builder 设计](./ir_builder_design.md)
- [IR Verifier 设计](./ir_verifier_design.md)
- [匿名函数句柄设计](./anonymous_function_handle_design.md)
- [M 变量模型设计](./variable_model_design.md)
- [M 工作区设计](./workspace_design.md)
- [M 函数栈帧设计](./function_frame_design.md)
