# Matlab IR Schema 草案

## 目标

本文承接 [ir_draft.md](/home/zj/Desktop/Baltam_IR/doc/ir_draft.md)，只记录当前代码实现对应的第一版 `IR` schema。

主文档负责说明：

- 为什么需要 `IR`
- 第一版 `IR` 的职责边界
- `IR` 与 `bytecode`、`typed SSA` 的关系

本文只负责说明当前实际存在的数据结构：

- `MFileUnit`
- `CodeUnit`
- `ScriptUnit`
- `FunctionUnit`
- `SlotTable`
- `BasicBlock`
- `Instruction`

## 当前范围

本文默认沿用第一版 `IR` 的范围约束：

- 只考虑 `.m` 文件输入
- 只考虑 `script` 和 `function`
- 暂不考虑 `Command` / REPL 输入
- 暂不考虑嵌套函数
- 暂不考虑匿名函数与闭包
- 暂不考虑 `try/catch`
- `global` 需要显式支持
- `persistent` 暂不进入第一版 schema

## 1. 基础类型

### 1.1 EntityId

当前代码通过 `EntityId<Tag>` 模板定义强类型 ID。第一版实际仍在使用的只有：

- `SlotId`
- `ValueId`

它们的作用分别是：

- `SlotId`
  作为 frame 槽位的稳定句柄
- `ValueId`
  作为指令结果值的稳定句柄

### 1.2 其他基础类型

- `InternedString = std::string`
- `NormalizedPath = std::filesystem::path`
- `SourceSpan`
  使用半开区间 `[begin_offset, end_offset)` 表示源码范围

## 2. MFileUnit

`MFileUnit` 对应一个 `.m` 文件。

当前推荐结构如下：

```text
MFileUnit
  path         : NormalizedPath
  code_units   : CodeUnit*[]
  entry_unit   : CodeUnit*
```

### 字段说明

- `path`
  当前文件路径。
- `code_units`
  文件直接拥有的全部 `CodeUnit`。
- `entry_unit`
  当前文件的入口代码单元。

### 派生方法

- `file_stem()`
- `is_script_file()`
- `is_function_file()`

脚本文件和函数文件不再单独缓存 `file_type`，而是通过 `entry_unit` 的真实类型推导。

## 3. CodeUnit / ScriptUnit / FunctionUnit

### 3.1 CodeUnit

`CodeUnit` 是真正的 lowering 单元。

当前结构如下：

```text
CodeUnit (abstract)
  parent        : MFileUnit*
  name          : InternedString
  slot_table    : SlotTable
  entry_block   : BasicBlock*
  basic_blocks  : BasicBlock*[]
  source_span   : SourceSpan
```

### 字段说明

- `parent`
  所属 `MFileUnit`。
- `name`
  单元名。对函数来说是函数名；对脚本来说可直接使用文件名。
- `slot_table`
  当前 unit 的全部 slot。
- `entry_block`
  CFG 入口块。
- `basic_blocks`
  当前 unit 的 block 列表。
- `source_span`
  当前 unit 覆盖的源码范围。

### 虚接口

`CodeUnit` 通过虚接口区分实际类型：

```text
type() -> script | function
```

并提供：

- `is_script()`
- `is_function()`

### 3.2 ScriptUnit

`ScriptUnit` 是 `CodeUnit` 的脚本特化。

当前它没有新增独立字段，只固定脚本单元语义；工作区句柄 slot 仍通过
基类上的 `find_hidden_slot(WorkspaceHandle)` 访问。

### 3.3 FunctionUnit

`FunctionUnit` 是 `CodeUnit` 的函数特化。

当前结构如下：

```text
FunctionUnit
  param_slots   : SlotId[]
  return_slots  : SlotId[]
```

### 字段说明

- `param_slots`
  按源码声明顺序保存参数 slot。
- `return_slots`
  按源码声明顺序保存返回值 slot。

参数名字、返回值名字以及源码位置，都统一由对应 `Slot` 提供，不再重复保存单独描述结构。

## 4. SlotAttrs / Slot / SlotTable

### 4.1 SlotAttrs

```text
SlotAttrs
  is_user_visible : bool
  is_mutable      : bool
  hidden_role     : HiddenRole
```

其中 `HiddenRole` 当前包括：

- `None`
- `Nargin`
- `Nargout`
- `Varargin`
- `Varargout`
- `WorkspaceHandle`

### 4.2 Slot

```text
Slot
  slot_id       : SlotId
  type          : arg | local | ret | hidden
  name          : InternedString
  source_span   : SourceSpan
  attrs         : SlotAttrs
```

### 字段说明

- `slot_id`
  slot 级稳定句柄。
- `type`
  slot 的类别。
- `name`
  源码名字或编译器生成名字。
- `source_span`
  对应源码范围。
- `attrs`
  slot 级属性。

当前版本不再保存 `frame_index`。

### 4.3 SlotTable

```text
SlotTable
  slots : Slot[]
```

当前版本直接使用单一 `slots` 容器保存全部 slot 定义，不再维护：

- `arg_slots`
- `local_slots`
- `ret_slots`
- `hidden_slots`

### SlotTable helper

当前代码提供：

- `empty()`
- `find_slot(SlotId)`
- `find_hidden_slot(HiddenRole)`

## 5. BasicBlock

当前结构如下：

```text
BasicBlock
  parent        : CodeUnit*
  label         : InternedString
  source_span   : SourceSpan
  instructions  : Instruction*[]
  predecessors  : BasicBlock*[]
  successors    : BasicBlock*[]
```

### 字段说明

- `parent`
  所属 `CodeUnit`。
- `label`
  文本标签，主要用于打印、调试和诊断。
- `source_span`
  当前 block 覆盖的源码范围。
- `instructions`
  顺序指令列表。
- `predecessors`
  前驱块集合。
- `successors`
  后继块集合。

### BasicBlock 约束

- 终结类指令与普通指令统一建模
- 如果 block 中存在终结指令，则它必须是最后一条
- 当前代码同时保存：
  - `predecessors/successors`
  - `GotoInst/BranchInst` 的目标块

这是一处已知双重状态，后续应收敛为单一真源

### BasicBlock helper

当前代码提供：

- `has_instructions()`
- `has_predecessors()`
- `has_successors()`
- `terminator()`
- `has_terminator()`

## 6. Constant / UnaryOp / BinaryOp / Operand

### 6.1 Constant

当前常量集合包括：

- `LogicalConstant`
- `Int64Constant`
- `UInt64Constant`
- `Float64Constant`
- `Complex128Constant`
- `CharLiteralConstant`
- `StringLiteralConstant`
- `EmptyDoubleMatrixConstant`

统一表示为：

```text
Constant = variant<...>
```

### 6.2 UnaryOp

当前一元操作包括：

- `Uplus`
- `Uminus`
- `LogicalNot`
- `Transpose`
- `Ctranspose`

### 6.3 BinaryOp

当前二元操作包括：

- 算术：`Add/Sub/Mul/Rdiv/Ldiv/Pow`
- 点算术：`ElemMul/ElemRdiv/ElemLdiv/ElemPow`
- 逻辑：`And/Or`
- 比较：`Lt/Le/Gt/Ge/Eq/Ne`

### 6.4 Operand

当前操作数集合是：

```text
Operand = variant<
  ValueId,
  SlotId,
  InternedString
>
```

也就是说：

- 值引用直接用 `ValueId`
- slot 引用直接用 `SlotId`
- 名字引用直接用 `InternedString`

当前版本不允许操作数直接承载立即数，所有进入数据流的字面量都应先经 `ConstInst` 物化成 `ValueId`。

## 7. InstAttrs / EffectClass / Instruction

### 7.1 InstAttrs

```text
InstAttrs
  may_throw    : bool
  is_synthetic : bool
```

### 7.2 EffectClass

```text
EffectClass
  pure
  frame
  heap
  env
  opaque
```

### 7.3 Instruction 基类

当前结构如下：

```text
Instruction
  parent       : BasicBlock*
  type         : Instruction::Type
  effect       : EffectClass
  source_span  : SourceSpan
  attrs        : InstAttrs
```

当前 `Instruction` 采用：

- 继承层次表达具体指令数据结构
- `parent` 反向指回所属 `BasicBlock`
- `Instruction::Type` 作为显式判别标签

这里保留 `type` 标签的目的，是避免在核心 IR 上依赖 RTTI 做分派。

## 8. 当前已实现的指令类

### 8.1 普通指令

- `ConstInst`
  - `result : ValueId`
  - `value  : Constant`
- `LoadSlotInst`
  - `result  : ValueId`
  - `slot_id : SlotId`
  - 读取的是已经静态绑定好的 frame slot
- `StoreSlotInst`
  - `slot_id : SlotId`
  - `value   : Operand`
- `LoadWorkspaceInst`
  - `result   : ValueId`
  - `workspace_handle_slot : SlotId`
  - `symbol   : InternedString`
  - 读取的是 workspace 中名为 `symbol` 的名字，而不是静态 frame slot
  - 在当前 printer 中显示为 `load_env`
- `StoreWorkspaceInst`
  - `workspace_handle_slot : SlotId`
  - `symbol   : InternedString`
  - `value    : Operand`
  - 在当前 printer 中显示为 `store_env`
- `ApplyInst`
  - `results        : ValueId[]`
  - `callee_or_base : Operand`
  - `arguments      : Operand[]`
  - 表示尚未消歧的源码层 `A(...)`
- `CallInst`
  - `results      : ValueId[]`
  - `callee_kind  : direct | indirect`
  - `callee       : Operand`
  - `arguments    : Operand[]`
  - 只表示已经确认是调用的语义
- `CopyInst`
  - `result : ValueId`
  - `value  : Operand`
- `UnaryInst`
  - `result  : ValueId`
  - `op      : UnaryOp`
  - `operand : Operand`
- `BinaryInst`
  - `result : ValueId`
  - `op     : BinaryOp`
  - `lhs    : Operand`
  - `rhs    : Operand`

### 8.2 终结类指令

- `GotoInst`
  - `target : BasicBlock*`
- `BranchInst`
  - `condition    : Operand`
  - `true_target  : BasicBlock*`
  - `false_target : BasicBlock*`
- `ReturnInst`
  - `values : Operand[]`

## 9. `LoadSlot` 与 `LoadWorkspace` 的区别

这两个节点都表现为“读一个值”，但它们读取的语义空间不同。

- `LoadSlotInst`
  - 输入操作数是 `slot_id : SlotId`
  - `SlotId` 是 IR 构建阶段就已经确定的静态句柄
  - 读取的是当前 `CodeUnit` frame 内的 slot
- `LoadWorkspaceInst`
  - 输入由 `workspace_handle_slot : SlotId` 和 `symbol : InternedString` 组成
  - `workspace_handle_slot` 指向工作区句柄 slot，`symbol` 是要在 workspace 中查找的名字
  - 读取的是当前 workspace 的动态名字绑定
  - 当前打印文本里用 `load_env` 表示这一语义

换句话说：

- `LoadSlotInst` = 读取静态 slot
- `LoadWorkspaceInst` = 通过环境句柄按名字读取 workspace

## 10. 当前结构约束

### 10.1 MFileUnit / CodeUnit

- `entry_unit` 必须属于当前 `MFileUnit`
- `basic_blocks` 中的 block 必须都属于当前 `CodeUnit`
- `entry_block` 必须属于当前 `CodeUnit`

### 10.2 FunctionUnit

- `param_slots` 只能引用 `arg` slot
- `return_slots` 只能引用 `ret` slot
- `param_slots` 和 `return_slots` 的顺序应与源码声明顺序一致

### 10.3 Hidden slot

- 同一个 `CodeUnit` 中，除 `None` 外的同一 `HiddenRole` 至多出现一个对应 slot
- `WorkspaceHandle` 只应出现在 `ScriptUnit`
- `Nargin/Nargout/Varargin/Varargout` 只应出现在 `FunctionUnit`

### 10.4 BasicBlock

- 若 block 中存在终结指令，则必须是最后一条
- block 中除最后一条外不应出现其他终结指令

## 11. 已经删除的旧设计

为了避免与旧文档混淆，以下结构已经不再属于当前 schema：

- `MFileId`
- `CodeUnitId`
- `BlockId`
- `InstId`
- `Value` 元数据表
- `BasicBlockAttrs`
- `Instruction` / `Terminator` 分离建模
- `Opcode`
- `OpData`
- `TerminatorOpcode`
- `TermData`
- `ParamDesc`
- `ReturnDesc`
- `MFileAttrs`
- `CodeUnitAttrs`
- `arg_slots/local_slots/ret_slots/hidden_slots`
- `frame_index`

## 相关文档

- [ir_draft.md](/home/zj/Desktop/Baltam_IR/doc/ir_draft.md)
- [execution_strategy.md](/home/zj/Desktop/Baltam_IR/doc/execution_strategy.md)
