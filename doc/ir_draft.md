# Matlab IR 草案

## 目标

本文给出当前代码实现对应的一版 `IR`（High-level IR）草案，用于承接 `AST` 与 `bytecode` 之间的语义层表示。

当前默认的总体流水线是：

`AST -> IR -> bytecode -> interpreter/profile -> typed SSA -> LLVM IR`

其中 `IR` 的角色是：

- 保留 Matlab 的高层语义边界
- 为 `bytecode` lowering 提供直接输入
- 作为后续热点编译前的语义规范化层

## 总体结构与设计取舍

当前已经收敛下来的主方向是：

- 让 `IR` 承担 Matlab 动态语义的主建模责任
- 让 `bytecode` 承担稳定执行与 profile
- 只在热点 region 或热点函数上按需构造 typed SSA

这样分层的核心原因不是“SSA 不好”，而是 Matlab 的完整动态语义并不适合作为全局常驻 SSA 的主干表示。尤其是：

- workspace 访问
- 动态名字解析
- 动态函数分发
- `eval`
- `clear`
- `cd`
- `addpath`
- `mex`

如果把这些行为强行压进一个长期保留的 SSA 主链路，通常会演化成大量 barrier、失效点和保守副作用建模，最后反过来削弱 SSA 本来最有价值的优化能力。

因此当前更明确的职责分工是：

- `IR`：语义层 IR，显式表达动态环境、名字与调用边界
- `bytecode`：执行层 IR，负责解释执行、profile 和作为 deopt 回退目标
- typed SSA：热点优化 IR，只服务于数值热点和相对稳定的 region

更适合进入 typed SSA 的部分通常是：

- 只涉及 `frame` 和有限 `heap` 的数值代码
- loop body 内稳定的 slot 读写
- 已知稳定 builtin 路径上的算术和比较

更适合作为 SSA region 边界的部分通常是：

- `eval`
- `clear`
- path 修改
- mex 交互
- 强动态调用分派

## 非目标

这版 `IR` 明确不承担以下职责：

- 不要求全局 SSA 形式
- 不要求承载主要优化框架
- 不要求直接作为解释执行 IR
- 不要求直接携带 JIT guard、deopt state map、OSR metadata
- 不要求一次性覆盖全部 Matlab 高级特性细节

## 当前范围

第一版 `IR` 只覆盖最小可落地范围：

- 只考虑 `.m` 文件输入
- 只考虑 `script` 和 `function`
- 暂不考虑 `Command` / REPL 输入
- 暂不考虑嵌套函数
- 暂不考虑匿名函数与闭包
- 暂不考虑 `try/catch`
- `global` 需要显式支持
- `persistent` 暂不进入第一版

## 设计原则

### 1. 语义优先

凡是会影响 Matlab 动态语义的行为，都应在 `IR` 中显式出现，而不是隐藏在普通变量读写里。尤其是：

- workspace 访问
- 动态函数解析
- `eval`
- `clear`
- `cd`
- `addpath`
- `mex`

### 2. non-SSA，但允许局部值 ID

`IR` 不是 SSA IR，但允许用 `ValueId` 表达指令结果，方便表达式级数据流。真正的可变程序状态仍然通过 slot 表达。

因此：

- block 内可以有短生命周期值
- block 间的可变状态必须落入 slot
- `IR` 本质上仍然是 non-SSA

### 3. 语义对象与执行对象分层

当前实现把层次关系明确成：

- `MFileUnit` 负责文件级拥有关系
- `CodeUnit` 负责 lowering 单元边界
- `SlotTable` 负责 frame 状态
- `BasicBlock` 负责 CFG
- `Instruction` 负责表达式与控制流语义

### 4. 便于 lowering

`IR` 应容易 lowering 到 bytecode，因此不应过度引入只对优化器友好的复杂结构。

### 5. 便于后续 region 提取

虽然 `IR` 不是主优化 IR，但它应清楚表达哪些区域适合进入后续 typed SSA，哪些区域必须留在解释执行语义里。

## 当前对象模型

当前代码中的核心结构大致如下：

```text
MFileUnit
  path
  code_units : CodeUnit*
  entry_unit : CodeUnit*

CodeUnit (abstract)
  parent : MFileUnit*
  name
  slot_table
  entry_block : BasicBlock*
  basic_blocks : BasicBlock*
  source_span

ScriptUnit : CodeUnit
FunctionUnit : CodeUnit
  param_slots  : SlotId[]
  return_slots : SlotId[]

SlotTable
  slots : Slot[]

BasicBlock
  parent : CodeUnit*
  label
  instructions : Instruction*
  predecessors : BasicBlock*
  successors   : BasicBlock*

Instruction (base)
  parent
  effect
  source_span
  attrs
```

### 当前实现约束

- `MFileUnit` 直接拥有全部 `CodeUnit`
- `CodeUnit` 直接拥有全部 `BasicBlock`
- `BasicBlock` 直接拥有全部 `Instruction`
- `Instruction` 通过 `parent` 反向引用所属 `BasicBlock`
- `FunctionUnit` 的函数接口只记录为 `SlotId` 列表，不再重复保存一份参数/返回值描述结构

## 文件与单元

### MFileUnit

`MFileUnit` 对应一个 `.m` 文件。

它的职责很收敛：

- 持有文件路径
- 持有本文件内全部 `CodeUnit`
- 标记入口 `CodeUnit`

文件是脚本文件还是函数文件，不再单独缓存 `file_type`，而是由 `entry_unit` 的真实类型推导。

### CodeUnit

`CodeUnit` 是真正的 lowering 单元。

当前实现把它做成抽象基类，并保留两个派生类：

- `ScriptUnit`
- `FunctionUnit`

保留派生类的原因是：

- `FunctionUnit` 需要持有 `param_slots` / `return_slots`
- `ScriptUnit` 需要表达工作区句柄相关约束

## 名字与存储模型

Matlab 的关键难点之一是“名字”不只是局部变量名，还可能对应：

- 局部变量
- global 变量
- 当前 workspace 名字
- builtin 名字
- path 上解析到的函数
- mex 导出的函数

因此第一版 `IR` 必须避免把所有名字访问都压成 `load_slot/store_slot`。

### 静态可绑定名字

以下对象优先绑定为 slot：

- 函数参数
- 已确定的局部变量
- 返回值变量

### 动态名字

以下访问必须显式保留动态解析：

- script 中的普通名字访问
- `eval` 引入的名字
- 依赖 workspace 的符号访问
- 需要经过 builtin/path/mex 解析的调用目标

## Slot 模型

`SlotTable` 属于 `CodeUnit`，而不属于某个 `BasicBlock`。这是因为 slot 生命周期天然跨 block。

第一版 `Slot` 只区分四类：

- `arg`
- `local`
- `ret`
- `hidden`

其中：

- `arg/local/ret` 用于表达常规 frame 状态
- `hidden` 用于表达 `nargin`、`nargout`、`varargin`、`varargout`、`script environment handle`

当前实现中，slot 分类完全由：

- `Slot::type`
- `SlotAttrs::hidden_role`

决定，不再维护额外的并行分类索引。

## 控制流模型

`IR` 使用普通 CFG，而不是结构化语句树。

当前实现中：

- `BasicBlock` 直接保存顺序指令列表
- 终结类指令与普通指令统一建模
- 如果 block 中存在终结指令，它必须是最后一条

当前代码还同时保存了两类 CFG 信息：

- `GotoInst/BranchInst` 里的目标块指针
- `BasicBlock` 里的 `predecessors/successors`

这是一处已知的双重状态，后续需要收敛成单一真源。

## 指令模型

第一版当前已经实现的最小指令集包括：

- `ConstInst`
- `LoadSlotInst`
- `StoreSlotInst`
- `LoadWorkspaceInst`
- `StoreWorkspaceInst`
- `ApplyInst`
- `CallInst`
- `CopyInst`
- `UnaryInst`
- `BinaryInst`
- `GotoInst`
- `BranchInst`
- `ReturnInst`

当前版本还有这些约束：

- 所有需要进入数据流的字面量都先通过 `ConstInst` 物化为 `ValueId`
- `Operand` 只承载：
  - `ValueId`
  - `SlotId`
  - `InternedString`

## 当前已知的收敛点

相较更早的文档版本，当前实现已经去掉了这些旧设计：

- `MFileId`
- `CodeUnitId`
- `BlockId`
- `InstId`
- `Value` 元数据表
- `BasicBlockAttrs`
- `Instruction` / `Terminator` 分离建模
- `Opcode + OpData + TermData`
- `ParamDesc / ReturnDesc` 独立描述结构
- `MFileAttrs / CodeUnitAttrs`

这些内容如果在旧文档或旧讨论中出现，应以当前代码结构为准。

## IR 与 bytecode 的关系

`IR` 到 `bytecode` 的 lowering 应遵循以下原则：

- slot 直接映射到 frame layout
- block 线性化为 bytecode 基本块和跳转
- 动态语义保持显式，不能在 lowering 时偷偷静态化

## 相关文档

- [ir_schema.md](/home/zj/Desktop/Baltam_IR/doc/ir_schema.md)
- [execution_strategy.md](/home/zj/Desktop/Baltam_IR/doc/execution_strategy.md)
