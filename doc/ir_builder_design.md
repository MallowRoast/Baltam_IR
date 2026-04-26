# Matlab IR Builder 设计

## 目标

本文记录当前 `IRBuilder` 的实际职责和数据结构，只描述已经落地的实现。

`IRBuilder` 是一层低层原始构造器，职责很收敛：

- 创建 `MFileUnit / ScriptUnit / FunctionUnit`
- 维护当前 unit 的插入点
- 分配 `SlotId / ValueId`
- 创建 slot / hidden slot
- 维护静态名字到 `SlotId` 的绑定
- 追加指令并同步维护 CFG 边
- 收集构建期诊断

它不负责 AST 遍历和高层语义判定；这些由 `IRLowerer` 完成。

## 当前对象边界

### `IRBuilder`

`IRBuilder` 负责：

- `begin_file(...)`
- `begin_script_unit(...)`
- `begin_function_unit(...)`
- `set_insert_point(...)`
- `create_slot(...)`
- `create_hidden_slot(...)`
- `create_value()`
- `bind_name(...)`
- `find_name(...)`
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
- 名字按 `slot / workspace` 分类
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
- 重复 hidden slot
- terminator 后继续插指令
- 当前名字绑定缺少有效 `SlotId`

### 2. `IRBuildResult`

```text
IRBuildResult
  mfile        : unique_ptr<MFileUnit>
  diagnostics  : IRBuildDiagnostic[]
```

builder 和 lowering 当前统一复用这一个结果结构。

### 3. `IRUnitBuildState`

每个活动 `CodeUnit` 对应一份 builder 状态：

```text
IRUnitBuildState
  unit            : CodeUnit*
  current_block   : BasicBlock*
  ids             : IRIdAllocator
  name_bindings   : unordered_map<InternedString, SlotId>
  loop_stack      : LoopFrame[]
```

字段职责如下：

- `unit`
  当前活动 `CodeUnit`
- `current_block`
  当前插入点
- `ids`
  当前 unit 内的 `SlotId / ValueId` 分配器
- `name_bindings`
  当前 unit 的静态名字表，只保存 `name -> SlotId`
- `loop_stack`
  预留给 lowering 使用的 loop 栈

### 4. `IRIdAllocator`

```text
IRIdAllocator
  next_slot  : uint32
  next_value : uint32
```

当前策略是：

- `SlotId` 按 unit 局部递增
- `ValueId` 按 unit 局部递增

builder 不跨 unit 共享 ID 分配器。

## 当前名字绑定模型

当前 builder 不再维护额外的名字绑定结构体，也不再区分“调用绑定”。

`name_bindings` 只服务静态 slot 名字：

- 函数参数名
- 函数返回值名
- 已创建的 local 名

script 名字访问不进入这张表，而是直接 lower 成：

- `LoadWorkspaceInst`
- `StoreWorkspaceInst`

因此 builder 里的名字表现在本质上就是一个 `name -> SlotId` 的 side table。

## 构建期不变量

builder 当前主动维护这些约束：

1. 只有存在活动 unit 时才能创建 slot / value。
2. 只有存在活动 block 时才能追加指令。
3. `BasicBlock` 一旦已有 terminator，就不能继续追加指令。
4. 同一个 `CodeUnit` 中，同一 `HiddenRole` 只能有一个 hidden slot。
5. `WorkspaceHandle` 只能出现在 `ScriptUnit`。
6. `Nargin / Nargout / Varargin / Varargout` 只能出现在 `FunctionUnit`。
7. 追加 `GotoInst / BranchInst` 时立即同步维护 `predecessors / successors`。

## Builder 不负责的事

当前 builder 明确不承担：

- AST 遍历
- parser 接口调用
- workspace / global / dynamic call 解析
- verifier 全量校验
- bytecode lowering

也就是说，builder 只负责把已经做完语义决策的 lowering 动作安全地落到最终 IR 对象上。

## 当前实现中的使用方式

当前主路径是：

```text
parse_mfile()
  -> IRLowerer
  -> IRBuilder
  -> MFileUnit / CodeUnit / BasicBlock / Instruction
```

其中：

- `IRLowerer` 持有 `IRBuilder`
- `IRLowerer` 负责高层决策
- `IRBuilder` 负责底层对象落地和 CFG 维护

这就是当前 `src/ir` 中实际存在的分层。
