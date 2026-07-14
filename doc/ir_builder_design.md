# Matlab IR Builder 设计

## 目标

本文记录当前 `IRBuilder` 的实际职责和数据结构，只描述已经落地的实现。

`IRBuilder` 是一层低层原始构造器，职责很收敛：

- 创建 `IRModule / MFileUnit / ScriptUnit / FunctionUnit / AnonymousFunctionUnit`
- 维护当前 unit 的插入点
- 分配 `SlotId / ValueId / AnonymousFunctionId`
- 创建 slot
- 追加指令并同步维护 CFG 边
- 收集构建期诊断

它不负责 AST 遍历和高层语义判定；这些由 `IRLowerer` 完成。

## 当前对象边界

### `IRBuilder`

`IRBuilder` 负责：

- `begin_file(...)`
- `begin_script_unit(...)`
- `begin_function_unit(...)`
- `begin_anonymous_function_unit(...)`
- `set_current_unit(...)`
- `set_insert_point(...)`
- `create_slot(...)`
- `create_value()`
- `create_anonymous_function_id()`
- `append_instruction(...)`
- `finish()`

### `CodeUnit`

block 的生命周期已经从 builder 中拆出，当前由 `CodeUnit` 负责：

- `create_block(...)`
- `set_entry_block(...)`

builder 只维护“当前插入点”，不再负责 block 的创建和入口块设置。

### `IRLowerer`

`IRLowerer` 负责更高层的 lowering 状态，包括：

- AST 语义判定
- 名字分类和 slot 绑定
- lowering 期 `name -> Slot` 绑定
- `if` 等结构化语句展开
- 源码 `location -> SourceSpan` 桥接

## 核心数据结构

### 1. `IRBuildDiagnostic`

```text
IRBuildDiagnostic
  severity    : Log | Warning | Error
  message     : InternedString
  source_span : SourceSpan
```

用于承载构建期错误和 warning，例如：

- 没有活动 unit 或 block
- 无效 slot tag 使用
- terminator 后继续插指令

### 2. `IRBuildResult`

```text
IRBuildResult
  module      : unique_ptr<IRModule>
  mfile       : MFileUnit*
  diagnostics : IRBuildDiagnostic[]
```

`module` 拥有所有文件单元和匿名函数体；`mfile` 是便利裸指针，指向本次文件级 lowering
入口对应的 `MFileUnit`。

### 3. `IRUnitBuildState`

每个活动 `CodeUnit` 对应一份 builder 状态：

```text
IRUnitBuildState
  unit            : CodeUnit*
  current_block   : BasicBlock*
  ids             : IRIdAllocator
```

字段职责如下：

- `unit`
  当前活动 `CodeUnit`
- `current_block`
  当前插入点
- `ids`
  当前 unit 内的 `SlotId / ValueId` 分配器

### 4. `IRIdAllocator`

```text
IRIdAllocator
  next_slot  : uint32
  next_value : uint32
```

当前策略是：

- `SlotId` 按 unit 局部递增
- `ValueId` 按 unit 局部递增
- `AnonymousFunctionId` 按 module 递增

builder 不跨 unit 共享 `SlotId / ValueId` 分配器。

## 名字绑定归属

`IRBuilder` 不维护名字绑定。lowering 期 `name -> Slot` 是 AST 语义作用域状态，
由 `IRLowerer` 的每个 `CodeUnit` side table 持有。

这张 lowering 表只服务静态 slot 名字：

- 脚本中静态出现的变量名
- 函数参数名
- 函数返回值名
- 已创建的 local 名
- 匿名函数参数名和捕获名

script 名字访问也进入这张表，slot tag 为 `ScriptVar`。基础 lowering 会生成
`LoadSlotInst` / `StoreSlotInst`，文本 IR 打印为 `load` / `store`。运行时再根据
`ScriptVar` slot 的绑定状态决定是否直接访问已绑定存储，或在绑定失效后退回动态 lookup。

函数调用是否能从 `apply` 收敛成 `call`，依赖 `IRLowerer` 中“名字是否已经被绑定成变量”
这一事实。builder 只负责创建 slot 本身，不判断源码名字语义。

## 构建期不变量

builder 当前主动维护这些约束：

1. 只有存在活动 unit 时才能创建 slot / value。
2. 只有存在活动 block 时才能追加指令。
3. `BasicBlock` 一旦已有 terminator，就不能继续追加指令。
4. `Slot` 由 `SlotId` 和 `SlotTag` 组成，是否有效只由 `SlotId` 判断。
5. `Nargin / Nargout / Varargin / Varargout` 只能出现在 `FunctionUnit`。
6. 追加 `GotoInst / BranchInst` 时立即同步维护 `predecessors / successors`。

## Builder 不负责的事

当前 builder 明确不承担：

- AST 遍历
- parser 接口调用
- dynamic env / global / dynamic call 解析
- lowering 期名字绑定
- verifier 全量校验
- IR interpreter / JIT 执行准备

也就是说，builder 只负责把已经做完语义决策的 lowering 动作安全地落到最终 IR 对象上。

## 当前实现中的使用方式

当前主路径是：

```text
parse_mfile()
  -> IRLowerer
  -> IRBuilder
  -> IRModule / MFileUnit / CodeUnit / BasicBlock / Instruction
```

其中：

- `IRLowerer` 持有 `IRBuilder`
- `IRLowerer` 负责高层决策
- `IRBuilder` 负责底层对象落地和 CFG 维护

这就是当前 `src/ir` 中实际存在的分层。
