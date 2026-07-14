# M 工作区设计

本文讨论运行时工作区的模型。这里的“工作区”不是一个固定的数据结构名，而是 Matlab 动态
语义看到的按名访问视图。

核心结论是：

> Workspace 是 `name -> binding` 的语义接口，不等价于一张裸 `name -> value` 哈希表。
> 不同场景可以用不同底层存储实现同一套 lookup/store/list/clear 语义。

## 1. 设计目标

工作区设计需要同时满足：

- 脚本和命令行按名字读写变量
- 函数中的 `eval`、脚本调用、`assignin`、`evalin` 可以访问目标工作区
- `who`、`whos`、`exist`、`save`、debugger 能观察变量绑定
- 函数正文普通变量访问在 binding live 时走静态 slot，不被动态名字污染
- 后续解释器、JIT 和环境 cache 可以共享稳定的失效边界

因此，第一版 workspace 应先定义接口和 binding 语义，再决定每类 workspace 的底层存储。

## 2. 基本接口

运行时可以把 workspace 抽象为：

```text
Workspace
  kind
  epoch
  lookup(name) -> LookupResult
  store(name, value) -> StoreResult
  clear(name) -> ClearResult
  list(options) -> BindingInfo[]
  exists(name, query_kind) -> ExistResult
```

其中 `lookup/store/clear/list/exists` 是语义操作。具体实现可以是 base table、function frame
view、global table 或 persistent cell。

`epoch` 用于 inline cache、JIT guard 和 workspace-specialized script 的失效。只要按名绑定的
可观察结果可能变化，就应推进相应 epoch。

## 3. Binding 而不是裸 Value

工作区里保存的概念应是 binding：

```text
WorkspaceBinding
  name
  kind
  storage
  attrs
```

`kind` 至少需要区分：

- `DynamicLocal`：base workspace 或函数动态 env 中的普通动态变量
- `FrameSlot`：函数 frame 中静态确定且当前 live 的 slot binding
- `Global`：由 `global` 声明接入的共享变量
- `Persistent`：函数私有持久变量
- `ImportedName`：后续支持 `import` 时的名字解析入口

`storage` 可以指向不同位置：

```text
DynamicLocal  -> DynamicEnv table entry
FrameSlot     -> FunctionFrame.slot_values[offset] + slot_binding_state[offset]
Global        -> GlobalVariableCell
Persistent    -> PersistentVariableCell
ImportedName  -> Import table entry
```

这样做的原因是：workspace 是名字语义；变量值可能住在 frame slot、动态表、全局 cell 或
persistent cell 里。

## 4. Workspace 类型

第一版建议明确这些 workspace/view：

```text
BaseWorkspace
  dynamic_env
  import_state

FunctionWorkspaceView
  function_frame*
  static_name_map
  slot_binding_state
  dynamic_env

ScriptWorkspaceHandle
  target Workspace*

GlobalWorkspace
  global_table

PersistentWorkspace
  function_private_table
```

`ScriptWorkspaceHandle` 不拥有变量存储，只是脚本执行时拿到的目标 workspace 句柄。脚本在
base 中运行时，它指向 `BaseWorkspace`；脚本在函数中被调用时，它指向 caller 的
`FunctionWorkspaceView`。

`GlobalWorkspace` 和 `PersistentWorkspace` 可以先作为独立存储存在，再通过函数或 base
workspace 中的 binding 暴露出来。

## 5. Base workspace

base workspace 是命令行和顶层脚本常用的动态变量表：

```text
BaseWorkspace
  dynamic_env: name -> Value
  import_state
  epoch
```

规则：

- `store(name, value)` 写入 `dynamic_env`
- `lookup(name)` 先查变量绑定，再交给函数/路径解析层处理未命中
- `clear(name)` 删除动态变量绑定
- `list()` 返回用户可见动态变量，不包含内部执行状态

顶层脚本没有自己的 frame。它只是通过 `WorkspaceHandle` 写入 base workspace。

## 6. Function workspace view

函数工作区是函数 frame 的按名访问视图：

```text
FunctionWorkspaceView
  frame*

lookup(name):
  if name in frame.layout.static_name_map:
    if frame.slot_binding_state[offset] is live:
      return FrameSlot(frame, offset)
    return unresolved
  if frame.dynamic_env contains name:
    return DynamicLocal(frame.dynamic_env[name])
  return unresolved

store(name, value):
  if name in frame.layout.static_name_map:
    frame.slot_values[offset] = value
    frame.slot_binding_state[offset] = live
  else:
    ensure frame.dynamic_env
    frame.dynamic_env[name] = value

clear(name):
  if name in frame.layout.static_name_map:
    frame.slot_binding_state[offset] = unbound
  else:
    remove frame.dynamic_env[name]
```

这个 view 只给动态机制使用：

- `eval`
- 脚本调用
- `assignin`
- `evalin`
- `who` / `whos` / `exist` / `save`
- debugger

函数正文普通语句不应通过 workspace view 访问变量。它们在 IR 中已经 lower 成
`load_slot / store_slot` 或调用分派。对用户可见 slot，`load_slot` 自身负责检查 binding
是否 live；若 binding 已 dead，则通过 slow path / deopt 进入名字解析，而不是读取旧 slot value。

## 7. Script workspace

脚本没有独立工作区。运行时脚本 activation 需要持有目标 workspace：

```text
ScriptActivation
  IR continuation
  temporaries
  WorkspaceHandle target
```

当前源码中的脚本静态名字会进入 `ScriptVar` slot，基础 IR 仍是 `LoadSlotInst` /
`StoreSlotInst`，文本打印为 `load` / `store`。运行时执行这些脚本 slot 访问时，应按
`ScriptVar` 的目标 workspace 绑定状态做 lookup/store。语义上等价于：

```text
load_workspace  %env, @a
store_workspace %env, @a, value
```

运行时就是：

```text
env.target.lookup("a")
env.target.store("a", value)
```

因此同一份脚本可以在不同 workspace 中运行：

- 顶层运行：target 是 base workspace
- 函数内调用：target 是 caller function workspace view
- `evalin` 场景：target 是显式选择的 workspace

脚本写入 caller 函数时，是否命中 caller slot 只取决于 caller 函数文本中是否已经有静态名字。
如果 caller 没有静态 slot：

```matlab
function y = f()
  myscript;   % myscript.m 内部有：b = 1
  y = b;
end
```

执行 `myscript` 时，脚本中的 `ScriptVar b` 可以绑定到 `f` 的 `dynamic_env` 中的 `b`。但
`f` 正文里的 `y = b` 不应因此反向变成 slot 读取；它仍按 lowering 阶段已经确定的名字语义
继续走函数名解析、零参数调用或未定义函数错误等路径。

如果 caller 文本已经让该名字成为静态变量：

```matlab
function y = f()
  b = [];
  myscript;   % myscript.m 内部有：b = 1
  y = b;
end
```

则脚本中的 `ScriptVar b` 可以在运行时绑定到 caller 的 `b` slot。脚本写入 `b` 时更新该
slot，并建立 live binding；后续 `y = b` 只有在 binding 仍 live 时才可以走 slot fast path。
如果脚本或 `eval` 在中间执行了 `clear b`，则后续 use 不能继续依赖这个 slot，除非又有
支配性的重新赋值恢复 binding。

## 8. Global 和 persistent

从 workspace 视角看，`global` 和 `persistent` 都是可见 binding kind，但底层 storage 不同：

- `global` 是 shared binding。多个声明了同名 `global x` 的 workspace 读写同一个全局 cell。
- `persistent` 是 local-like binding。名字只属于当前函数，其他函数不能通过同名
  `persistent p` 访问同一份存储；它可以有静态 slot 位置，但值跨调用保留。

workspace 层只需要能暴露这两类 binding，并在 `clear`、`clear global`、函数清理和文件失效时
推进对应 epoch。它不负责定义专用 IR 节点、effect 或 runtime binding index；这些细节见
[Global / Persistent IR 节点设计](./global_persistent_ir_design.md)。

## 9. clear / who / exist

这些操作是 workspace 设计的关键压力点。

`clear(name)` 的第一版语义建议：

- base workspace：删除 `dynamic_env[name]`
- function workspace 动态 env：删除动态 binding
- function workspace 静态 slot：不改变 frame layout，只把该名字的 frame-slot binding 标为
  unbound；后续 `load_slot` 仍可作为 IR 节点存在，但 fast path 必须失败并进入 name lookup /
  deopt 慢路径
- global/persistent：按后续 Matlab 规则单独精确化，并推进对应 epoch

`who/whos/list()` 应只返回用户可见变量：

- base dynamic variables
- function static user variables
- function dynamic env variables
- global/persistent user bindings

不应返回：

- `InternalLocal`
- `Hidden` ABI slot
- script activation 临时状态
- interpreter/JIT 临时值

`exist(name, "var")` 应通过 workspace binding 查询；不命中变量时，再由更高层名字解析系统判断
函数、文件、class、builtin 等其他实体。

动态 env 写入不能简单地一律删除。即使它不会反向改变函数正文的静态分派，后续 `eval`、脚本、
`evalin`、`assignin`、`who/whos/exist/save`、debugger 或交互式观察仍可能看到这份 binding。
只有能证明后续没有 env observer 或动态代码执行点时，优化层才可以把动态 env 写入视为死写。

## 10. 与名字解析的边界

Workspace 只负责变量 binding，不负责完整函数路径解析。

推荐边界：

```text
variable lookup:
  workspace.lookup(name)

function / file / builtin lookup:
  resolver.lookup(name, current_context)

ambiguous A(...):
  先按当前语义判断 A 是否可能是 workspace 变量
  再决定 value_apply / call / apply
```

脚本中 `A(...)` 可能受 workspace 变量遮蔽，所以基础 IR 保留 `apply` 或 workspace 相关语义。
函数中静态变量 `A(...)` 可以 lower 成 `load_slot A` + `value_apply`。这里的 `load_slot A`
是 checked slot access：live 时返回变量值并继续 `value_apply`；dead 时按当前调用 continuation
deopt 到 generic name application，使同名函数重新参与分派。

## 11. Epoch 和 cache

workspace 需要版本号，服务于：

- workspace lookup inline cache
- 脚本上下文特化缓存
- JIT guard
- `who/whos/exist` 的可观察行为

可以先粗粒度设计：

```text
WorkspaceEpoch
  variable_epoch
  import_epoch
  global_epoch
  persistent_epoch
```

第一版实现可以只用一个 `variable_epoch`。当 `store/clear/assignin/eval/load` 改变可观察变量
绑定时推进它。后续如果 cache 失效太粗，再拆分不同 epoch。

## 12. 与现有 IR 的对应关系

当前源码和后续 runtime 可以这样映射：

- 脚本 `ScriptVar` 的 `LoadSlotInst / StoreSlotInst` -> 运行时通过 target workspace 的
  `lookup/store` 或已缓存 binding 访问
- 后续若引入显式 workspace access，`load_workspace/store_workspace` -> `Workspace::lookup/store`
- `WorkspaceHandle` -> script activation 持有的 target workspace，不是当前 `SlotTag`
- `LoadSlotInst / StoreSlotInst` -> 函数 frame slot fast path，不走 workspace API；用户可见
  `LoadSlotInst` dead 时通过 slow path / deopt 进入名字解析
- `Instruction::Env` effect -> 可能读写 workspace、resolver、path 或其他动态环境状态

需要保持的约束：

- 脚本名字访问基础 lowering 使用 `ScriptVar` slot 表达静态身份；执行层根据 target workspace
  和 binding 状态决定实际存储位置
- 函数静态变量访问保持 `load_slot / store_slot` IR；binding live 时走 slot fast path，dead 时
  走 name lookup / deopt
- 动态 workspace 写入不临时扩展函数 frame layout
- workspace observer 前，优化层必须确保可见 slot/env 状态已经物化

## 13. 第一版落地顺序

建议按这个顺序实现：

1. 定义 `Workspace` 接口和 `WorkspaceHandle` runtime 对象。
2. 实现 `BaseWorkspace`，支持 `lookup/store/clear/list/exists(var)`。
3. 实现 `FunctionWorkspaceView`，把 live 的静态名字映射到 frame slot，把新名字写入
   `dynamic_env`，并维护 `clear` 后的 unbound 状态。
4. 让脚本 activation 持有 target workspace，并让 `ScriptVar` slot 访问落到该 workspace 的
   lookup/store 或绑定缓存。
5. 接入 `eval` 和脚本调用，让它们使用当前 workspace view。
6. 再实现 `assignin/evalin/who/whos/exist/save`。
7. 最后补 `global/persistent/import` 的 binding kind、epoch 和精确规则。

## 相关文档

- [M 变量模型设计](./variable_model_design.md)
- [Global / Persistent IR 节点设计](./global_persistent_ir_design.md)
- [M 函数栈帧设计](./function_frame_design.md)
- [M 函数工作区中的静态 slot 与动态 env](./function_workspace_env_design.md)
- [IR Schema](./ir_schema.md)
