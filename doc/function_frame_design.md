# M 函数栈帧设计

本文只讨论普通 M 函数调用时的运行时栈帧。脚本、命令行、匿名函数、`eval` 和
`assignin` 等机制会经过函数栈帧暴露出来，但它们不是本文的主体。

核心结论是：

> 函数栈帧应以固定 `slot` 数组作为主干，以动态 `env` 表作为旁路。用户可见 slot 还需要
> 当前 binding 是否 live 的运行时状态；动态 env 只服务脚本、`eval`、debugger 和环境观察类机制。

## 1. 设计目标

函数栈帧需要同时满足几类约束：

- 能直接承接当前 IR 中的 `SlotTable`、`LoadSlotInst` 和 `StoreSlotInst`
- 支持多输入、多输出、`nargin`、`nargout`、`varargin` 和 `varargout`
- 允许脚本或 `eval` 在函数工作区中读写名字
- 不让运行时动态名字反向改变函数正文已经确定的变量/函数分派
- 为 bytecode interpreter、baseline JIT、debugger、profile 和后续 deopt state map 提供稳定布局

因此，第一版不应把函数 frame 设计成纯粹的 `name -> value` 哈希表。哈希表适合作为动态
env 旁路，但不适合作为函数局部变量和 ABI 状态的主表示。

## 2. 概念分层

建议先区分三个对象：

```text
FunctionFrameLayout
  code_object
  slots[]
  slot_offsets[]
  static_name_map
  hidden_slot_map
  frame_flags

FunctionFrame
  layout*
  caller*
  slot_values[]
  slot_binding_state[]
  dynamic_env
  pc / bytecode state
  call_state
  debug_state

FunctionWorkspaceView
  frame*
  lookup(name)
  store(name, value)
```

`FunctionFrameLayout` 是编译期或 bytecode lowering 后的稳定布局。它来自当前 IR 的
`FunctionUnit::slot_table`，并在 bytecode/JIT 层变成可直接按 offset 访问的数据。

`FunctionFrame` 是一次函数调用的运行时实例。递归调用同一个函数会共享同一个
`FunctionFrameLayout`，但拥有不同的 `FunctionFrame`、`slot_values` 和 slot binding state。

`FunctionWorkspaceView` 是动态名字机制看到的工作区视图，具体 lookup/store/clear 语义由
[M 工作区设计](./workspace_design.md) 维护。本文只要求 frame 暴露足够状态，让这个 view 可以
映射 live slot、动态 env 和 observer 需要的变量绑定。

## 3. Frame layout

`FunctionFrameLayout` 应从 `SlotTable` 派生，至少包含：

```text
FunctionFrameLayout
  slot_count
  slots:
    slot_id
    offset
    kind              Arg | Local | InternalLocal | Ret | Hidden
    name
    attrs
  static_name_map     name -> slot offset
  hidden_slot_map     HiddenRole -> slot offset
  flags
```

其中 `static_name_map` 只记录函数文本中静态确定的变量名：

- 参数
- 返回值
- 普通局部变量
- `for` / `catch` 等语法绑定变量
- 后续接入的 `persistent` 声明入口；它可以有静态 slot 位置，但底层存储是函数私有持久
  cell，不是普通 per-call frame value

`InternalLocal` 和 `Hidden` 默认不进入用户名字查找表。它们可以被 bytecode、解释器和 JIT
通过固定 offset 访问，但不应被 `who` 当作普通用户变量暴露。

`Capture` slot 不属于普通 `FunctionUnit` 的 frame layout。匿名函数可以复用同一套 frame
访问机制，但它的 layout 来自 `AnonymousFunctionUnit`，并额外允许 `Capture` slot。

`global` 不属于普通函数 frame layout。函数中的 `global` 名字可以进入全局声明表和静态名字
解析结果，但实际读写应通过 global table 的 symbol / binding，而不是通过 `SlotId`。

`HiddenRole` 通过 `hidden_slot_map` 固定到 slot offset。当前 IR 已有：

- `Nargin`
- `Nargout`
- `Varargin`
- `Varargout`

`WorkspaceHandle` 当前只允许出现在 `ScriptUnit`，不应作为普通 `FunctionUnit` 的 hidden slot。
函数 frame 如果需要暴露工作区，应通过 `FunctionWorkspaceView` 持有 frame 指针，而不是在
函数 slot 表中额外塞一个 workspace handle。

## 4. Runtime frame

`FunctionFrame` 建议保持简单直接：

```text
FunctionFrame
  FunctionFrameLayout* layout
  FunctionFrame* caller
  Value slot_values[layout->slot_count]
  SlotBindingState slot_binding_state[layout->user_visible_slot_count]
  DynamicEnv* dynamic_env
  BytecodePC pc
  CallState call_state
  DebugState debug_state
```

`slot_values` 是 frame 主存储。`slot_binding_state` 记录用户可见 `Arg` / `Ret` / `Local`
slot 当前是否仍是 live variable binding。`InternalLocal` 和 `Hidden` 不暴露给用户 `clear`，
通常不需要这层 binding 状态。

`load_slot` 和 `store_slot` 以固定 offset 作为 fast path，但用户可见 slot 的 `load_slot`
不是无条件裸读：

```text
load_slot %3:
  if live(%3):
    return slot_values[offset(%3)]
  return runtime_name_lookup_or_deopt(slot_name(%3), current_continuation)

store_slot %3:
  slot_values[offset(%3)] = value
  mark live(%3)
```

因此，函数正文里的源码 use 仍应保留为 `load_slot a`。`clear a` 或 `eval('clear a')` 会把
`a` 的 binding state 标为 unbound；后续执行到 `load_slot a` 时，fast path 失败，并按这个
use 的 continuation 进入名字 lookup / resolver 慢路径。只有优化层证明 binding live 时，才可
把 checked `load_slot` 降成裸 frame load。

`dynamic_env` 可以延迟创建。没有脚本、`eval`、`assignin`、`load`、debugger 或环境观察需求
时，普通函数调用可以完全不分配动态 env。

`caller` 用于调用栈、错误栈、`evalin('caller', ...)`、debugger 和 deopt 恢复。普通函数调用
不应通过 `caller` 查找局部变量；Matlab 函数默认没有动态父作用域变量查找语义。

## 5. 参数和返回值

调用进入函数时，runtime 按 callee layout 初始化参数与 ABI hidden slot：

```text
for each declared input slot:
  slot = provided argument or default missing marker

nargin slot:
  actual input count

varargin slot:
  cell-like container of extra inputs, when function accepts varargin

nargout slot:
  caller requested output count

varargout slot:
  aggregate output container, when function accepts varargout
```

返回时，runtime 按 `FunctionUnit::return_slots` 和 `nargout` 收集输出。普通命名返回值仍是
frame slot，因此函数体中对返回变量的赋值就是普通 `store_slot`。

需要保留一个区别：

- `nargin / nargout` 是 ABI 状态，适合 hidden slot
- 用户源码里的同名变量语义需要按 Matlab 规则处理，不能和 hidden slot 名字混在同一个用户
  `static_name_map` 中

## 6. 动态 env 旁路

`dynamic_env` 是 frame 的旁路状态，只服务动态名字机制和环境观察点：

- `eval` / `evalin`
- 脚本调用
- `assignin`
- `who` / `whos` / `exist` / `save`
- debugger

函数正文普通 IR 不应默认通过 workspace lookup 访问局部变量；它仍应使用 `load_slot`、
`store_slot` 或已经 lower 好的调用分派。`FunctionWorkspaceView` 的完整规则见
[M 工作区设计](./workspace_design.md#6-function-workspace-view)。

frame 侧只需要保证三点：

- `static_name_map` 能把函数文本中的用户可见名字定位到 slot offset。
- `slot_binding_state` 能表达该名字当前是否仍 live。
- `dynamic_env` 能保存不属于静态 frame layout 的运行时名字。

这个分层保持一个关键边界：动态机制可以读写函数工作区，但不能因为运行时创建了新名字就临时
扩展 `FunctionFrameLayout` 或反向改变函数正文已经 lower 好的名字语义。

## 7. 脚本在函数 frame 中执行

脚本没有自己的函数 frame。函数调用脚本时，脚本拿到 caller 的 `FunctionWorkspaceView`，并通过
它把 `ScriptVar` slot 访问映射到 caller 的 live slot 或 `dynamic_env`。详细脚本 workspace
规则和例子见 [M 工作区设计](./workspace_design.md#7-script-workspace)。

frame 侧需要提供的能力是：

- 暴露 caller 的 `FunctionWorkspaceView` 给脚本 activation。
- 允许脚本命中 caller 已有静态变量时更新对应 slot 和 binding state。
- 允许脚本创建新名字时写入 caller 的 `dynamic_env`。
- 保证脚本创建的新动态名字不扩展 caller 的 `static_name_map`。

## 8. 匿名函数和 capture

普通函数 frame 不应被匿名函数句柄长期持有。匿名函数构造时，应把捕获值复制到 closure 的
capture environment：

```text
outer function frame slot -> captured Value -> closure capture storage
```

匿名函数调用时可以创建自己的 frame，参数放入 `Arg` slot，捕获值放入 `Capture` slot。
这与普通函数 frame 相同的是固定 slot 数组；不同的是 capture slot 来自 closure storage，
不是 caller frame 的可变局部变量。

第一版匿名函数捕获按值处理即可。后续如果支持嵌套函数的共享可变变量，再单独引入
boxed variable 或 shared closure cell，不要把普通函数 frame 本身延长生命周期。

## 9. 生命周期与 GC root

函数 frame 至少需要承担 GC root 枚举职责：

- 所有活跃 `slot_values`
- `dynamic_env` 中的值
- 当前调用状态中的临时值
- helper 调用和 safepoint 需要暴露的额外 root

第一版 interpreter 可以保守枚举整个 `slot_values` 数组。后续 baseline JIT 和优化 JIT 可以用
bytecode PC map / stack map 精确标记哪些 slot 和临时值在 safepoint 处存活。

## 10. 与 bytecode/JIT 的关系

bytecode lowering 应把 `SlotId` 固化成 frame offset：

```text
IR SlotId -> FrameSlotOffset -> bytecode operand
```

解释器执行 slot bytecode 时按 offset 访问 `slot_values`，并依赖 lowering 或 binding analysis
保证该 slot 当前 live。调试构建可以保留 live check；优化构建可以把它作为前置 guard/事实。
这条路径必须快且稳定，因为它是函数变量访问的主干。

JIT 可以在此基础上继续优化：

- 未逃逸、类型稳定且 binding live 的 slot 可以提升为寄存器或 SSA value
- 在 deopt、异常、debugger safepoint 或 env observer 前，把需要观察的 slot 物化回 frame
- 触碰动态 env、`eval`、脚本、`assignin`、`clear` 等指令时设置 binding barrier 或退出优化 region

优化层不能假设 `dynamic_env` 不存在，除非 guard 已证明当前 region 没有动态环境观察点和相关
Env effect。

## 11. 第一版落地顺序

建议按这个顺序实现：

1. 从 `FunctionUnit::slot_table` 构建 `FunctionFrameLayout`，建立 `SlotId -> offset` 映射。
2. 实现 `FunctionFrame`，让 `load_slot / store_slot` 通过 offset 读写 `slot_values`，并维护
   用户可见 slot 的 binding state。
3. 初始化 `Arg / Ret / Hidden` slot，补齐 `nargin / nargout / varargin / varargout` 的调用约定。
4. 增加延迟分配的 `dynamic_env` 和 `FunctionWorkspaceView`。
5. 让脚本执行可以接收 caller workspace view。
6. 再处理 `eval`、`assignin`、`evalin`、`who/whos/exist/save` 等动态环境入口。
7. 最后接入 bytecode PC map、debugger 观察和 deopt state map。

## 12. 与现有 IR 的对应关系

当前 IR 中的对象可以这样映射：

- `SlotTable` -> `FunctionFrameLayout::slots`
- `SlotId` -> frame offset
- `LoadSlotInst / StoreSlotInst` -> `slot_values[offset]` 读写；用户可见 slot 还受 binding state 约束
- `FunctionUnit::param_slots` -> 参数初始化顺序
- `FunctionUnit::return_slots` -> 返回收集顺序
- `Nargin / Nargout / Varargin / Varargout` slot tag -> ABI hidden slot
- 脚本 `ScriptVar` slot 访问 -> 通过 target `WorkspaceView::lookup/store` 或绑定缓存执行
- 后续若引入显式 workspace access，`load_workspace/store_workspace` -> `WorkspaceView::lookup/store`

这个映射保持一个核心约束：函数 frame layout 是稳定的，动态 env 是旁路状态。运行时可以新增
动态名字，但不能临时扩展普通函数 frame slot。

## 相关文档

- [M 变量模型设计](./variable_model_design.md)
- [Global / Persistent IR 节点设计](./global_persistent_ir_design.md)
- [M 工作区设计](./workspace_design.md)
- [IR Schema](./ir_schema.md)
- [M 函数工作区中的静态 slot 与动态 env](./function_workspace_env_design.md)
- [Matlab 执行策略与 Runtime 机制设计](./execution_strategy.md)
