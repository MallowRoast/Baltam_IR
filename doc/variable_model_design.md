# M 变量模型设计

本文整理当前 high-level IR 和 runtime 设计中涉及的 M 变量模型。重点不是 C++ 字段现状，而是
不同语义变量的可见性、作用域、生命周期，以及它们在 IR 中应如何表示。

核心原则是：

> 能由源码静态确定的变量绑定，应尽量分配稳定编号或 slot，向 interpreter 和 JIT 传递下去。
> 仍依赖运行时名字创建或目标 workspace 的绑定，应保留为 workspace/env 语义，并可通过
> binding cache 做加速。

## 1. 变量类别总览

当前设计至少需要区分这些变量 / binding：

```text
Arg
Ret
Local
Persistent
Global
WorkspaceDynamic
ScriptStaticWorkspaceName
Capture
InternalLocal
HiddenRuntime
```

这些类别的差异主要来自四个维度：

- 语义：它表示用户变量、共享变量、持久变量、动态名字，还是 runtime 辅助状态。
- 可见性：用户源码、脚本、`eval`、`who/whos`、debugger 是否能看到。
- 作用域：名字属于函数、脚本目标 workspace、session/global table，还是 closure。
- 生命周期：每次调用新建、随 workspace 存在、跨函数调用共享，还是随 closure 存活。

需要额外区分两层事实：

```text
静态声明 / 静态绑定事实:
  由源码文本和 lowering 决定，例如参数、局部变量、persistent 声明、global 声明。

当前运行时绑定状态:
  某个名字在当前 activation/workspace 中此刻是否仍绑定到原来的 storage。
```

`clear`、`eval('clear x')`、`clear variables` 等机制可能破坏第二层事实。也就是说：

- `Arg` / `Ret` / `Local` 的 `SlotId` 和 frame offset 是静态 layout 信息。
- `global g` / `persistent p` 的声明本身也是静态信息。
- `a` 此刻是否仍是 live 的 frame-slot binding、`g` 此刻是否仍绑定到 `GlobalCell`、`p`
  此刻是否仍绑定到 persistent cell，都是可能被 `clear` 影响的动态状态。
- 优化和专用 load/store 只能在没有跨过相关 clear/binding barrier，且能证明当前 binding
  仍然 live 的区域内安全使用。

因此，`SlotId` 不等价于“这个名字在所有后续程序点都存在”。它只表示当前函数 layout 中为
该名字预留了可复用的位置。`clear a` 不会删除 layout 项，但会解除当前 activation 中
`a -> %slot_a` 的 live binding；后续读取 `a` 或执行 `a(...)` 仍可以保留为 `load_slot %slot_a`
这种静态 slot IR，但运行时必须检查 binding 是否 live。若 dead，`load_slot` 不能读旧 slot
value，而要进入基于 slot 名字的 lookup / resolver 慢路径。

## 2. 函数变量

### Arg

函数输入参数。

```text
visibility: 当前函数正文可见，动态 observer 可按函数 workspace 规则看到
scope: 当前 FunctionUnit
lifetime: 每次函数调用一份
IR: Slot::Arg + load_slot/store_slot
runtime: FunctionFrame::slot_values[offset]
```

参数 slot 位置由函数签名静态确定。`nargin` 这类调用 ABI 状态不应混入参数名字表。

### Ret

函数命名返回值。

```text
visibility: 当前函数正文可见，动态 observer 可按函数 workspace 规则看到
scope: 当前 FunctionUnit
lifetime: 每次函数调用一份
IR: Slot::Ret + load_slot/store_slot
runtime: FunctionFrame::slot_values[offset]
```

返回值也是 frame slot。函数返回时 runtime 按 `return_slots` 和调用点 `nargout` 收集结果。

### Local

函数普通局部变量，包括普通赋值左值、循环变量等静态语法绑定。

```text
visibility: 当前函数正文可见，动态 observer 可按函数 workspace 规则看到
scope: 当前 FunctionUnit
lifetime: 每次函数调用一份
IR: Slot::Local + load_slot/store_slot
runtime: FunctionFrame::slot_values[offset]
```

局部变量是优化最稳定的主体，但这里的稳定指的是 slot layout 稳定，不是每个程序点上的名字
binding 永远 live。`clear a`、`eval('clear a')` 或无法静态解析的 `eval` 都可能让当前
activation 中的 `a -> %slot_a` binding 变成 cleared/unbound。

因此：

```text
SlotId / frame offset:
  静态存在，后续赋值可以复用同一个 slot。

Binding liveness:
  运行时状态；clear 后变为 unbound，重新赋值后变回 live。
```

后续可以对 live 且未逃逸的局部变量做寄存器化、SSA 提升和冗余 store 消除；但这些优化不能
跨过可能清除该名字 binding 的 `clear` / `eval` / `assignin` / 脚本调用等边界。

## 3. Persistent

`persistent` 是函数局部名字，但值跨调用保留。

```text
visibility: 当前函数正文可见；其他函数不能通过同名 persistent 访问同一份存储
scope: 当前 FunctionUnit
lifetime: 随函数 code object / 函数清理规则存在，跨调用保留
IR: 保守阶段使用 workspace/binding 语义；特化后使用 Slot::Persistent + load_persistent/store_persistent
runtime: FunctionCodeObject::persistent_cells[persistent_offset]
```

`persistent` 可以有静态 slot 身份，但它不是普通 per-call frame local。它的 slot 定位到当前
函数 code object 的 persistent cell，递归和重入同一函数时共享同一份存储。专用节点、effect
和 `clear` 细节见 [Global / Persistent IR 节点设计](./global_persistent_ir_design.md)。

## 4. Global

`global` 是 session 级共享变量。声明了同名 `global x` 的函数、脚本或命令行上下文应读写同一
个 global cell。

```text
visibility: 所有声明同名 global 的 workspace 可见
scope: session/global table
lifetime: 随 session/global table 和 clear global 规则存在
IR: 保守阶段使用 workspace/binding 语义；特化后使用 load_global/store_global
runtime: GlobalRegistry / GlobalCell
```

`global` 不属于任何函数 frame layout，不应用 `SlotId` 表达。后续可以用 symbol 或 code object
中的 global binding index 加速访问，但这个 index 不是 frame slot offset。专用节点、effect 和
`clear global` 细节见 [Global / Persistent IR 节点设计](./global_persistent_ir_design.md)。

## 5. WorkspaceDynamic

动态 workspace/env 变量来自运行时名字创建或目标 workspace：

- `eval`
- `assignin`
- `evalin`
- `load`
- 脚本向 caller workspace 创建新名字
- base workspace / 命令行中用户直接创建的名字

```text
visibility: 取决于目标 workspace，可被 `who/whos/exist/save/debugger` 观察
scope: 目标 workspace
lifetime: 随目标 workspace 存在
IR: runtime Workspace API；后续可引入 load_workspace/store_workspace 这类 generic binding access
runtime: DynamicEnv table / WorkspaceBinding
```

这类名字不能在基础 lowering 中假设为函数 frame slot，因为名字可能由字符串在运行时产生：

```matlab
eval(['a' num2str(i) ' = 1'])
```

因此需要保留 workspace/env 边界。

## 6. ScriptStaticWorkspaceName

脚本文本中静态出现的变量名和 `eval` 动态拼接出来的名字不同。当前源码会为这些静态名字创建
`ScriptVar` slot，用来表达脚本内稳定的名字身份；但它不是脚本私有存储，真实读写目标仍来自
脚本运行时的 target workspace。

```text
visibility: 目标 workspace 中可见
scope: 脚本运行时的 target workspace
lifetime: 随 target workspace 中的 binding 存在
IR: Slot::ScriptVar + LoadSlotInst/StoreSlotInst；运行时可降成 workspace binding id
runtime: ScriptCodeObject::workspace_symbols + binding cache
```

脚本静态名字可以在 script code object 内编号，并由 runtime/interpreter 缓存 target workspace
中的 binding。具体 `WorkspaceHandle`、binding cache 和 caller slot 特化规则见
[M 工作区设计](./workspace_design.md#7-script-workspace)。

## 7. Capture

匿名函数捕获变量。

```text
visibility: 匿名函数体内可见
scope: closure body
lifetime: 随 closure object 存活
IR: Slot::Capture + load_slot
runtime: closure capture storage
```

当前匿名函数捕获按值处理：构造 closure 时从外层 slot 或 workspace 读出值，放入 closure
capture storage。调用匿名函数时，capture slot 映射到 closure storage，而不是 caller frame。

后续如果支持嵌套函数共享可变变量，应引入 boxed variable / shared closure cell，不应让普通
函数 frame 本身被 closure 延长生命周期。

## 8. InternalLocal 和 HiddenRuntime

这两类不是用户语义变量。

`InternalLocal` 用于 lowering/runtime 内部临时状态，例如循环迭代状态：

```text
visibility: 用户不可见
scope: 当前 CodeUnit
lifetime: 按当前调用或当前执行单元
IR: Slot::InternalLocal + load_slot/store_slot
runtime: frame slot 或 activation slot
```

`HiddenRuntime` 用于 ABI/runtime 特殊状态。当前 IR 已有独立 slot tag：

```text
Nargin
Nargout
Varargin
Varargout
```

这些 tag 用于函数 ABI 状态，不进入用户名字表。`WorkspaceHandle` 是 runtime activation 字段，
不是当前 `SlotTag`；脚本 activation 需要持有目标 workspace，使 `ScriptVar` slot 访问能落到
base workspace、caller function workspace 或 `evalin` 选择的 workspace。

frame layout 中 ABI slot 的细节见 [M 函数栈帧设计](./function_frame_design.md)，workspace
handle 语义见 [M 工作区设计](./workspace_design.md)。

## 9. M 函数可能持有的变量类型

普通 M 函数可以持有：

```text
Arg
Ret
Local
Persistent
Global binding
WorkspaceDynamic
InternalLocal
HiddenRuntime
Capture source values
```

说明：

- `Arg / Ret / Local` 是普通 per-call frame slot。
- `Persistent` 是当前函数的 persistent slot，映射到 function-private persistent cell。
- `Global binding` 不进入 frame slot 表；它在 global 声明表或 code object global binding table
  中编号。
- `WorkspaceDynamic` 是 `eval`、脚本调用、`assignin/load` 等动态机制在当前函数 workspace
  中创建或访问的名字。
- `InternalLocal / HiddenRuntime` 是 runtime 辅助状态，用户不可见。
- 函数可以作为匿名函数构造点的捕获来源，但普通函数 frame 不应被 closure 长期持有。

## 10. M 脚本可能持有的变量类型

脚本本体没有自己的函数 frame，也没有自己的 local / arg / ret / persistent。

脚本可以涉及：

```text
ScriptStaticWorkspaceName
WorkspaceDynamic
Global binding
InternalLocal
HiddenRuntime(WorkspaceHandle)
Capture source values
```

说明：

- 脚本静态出现的普通名字应保留 workspace 语义，但可以在 script code object 内编号。
- 脚本运行时写入的是 target workspace：base workspace、caller function workspace，或
  `evalin` 指定 workspace。
- 脚本中的 `global` 可以把 target workspace 中的名字绑定到 global cell。
- 脚本不创建 persistent slot。
- 脚本可以创建匿名函数并从 target workspace 捕获值。

## 11. 表示矩阵

| 类别 | M 函数 | M 脚本 | IR 表示 | 是否 slot | 是否 env/workspace |
| --- | --- | --- | --- | --- | --- |
| Arg | 是 | 否 | `Slot::Arg` + `load_slot/store_slot` | 是，frame slot | 否 |
| Ret | 是 | 否 | `Slot::Ret` + `load_slot/store_slot` | 是，frame slot | 否 |
| Local | 是 | 否 | `Slot::Local` + `load_slot/store_slot` | 是，frame slot | 否 |
| Persistent | 是 | 否 | 保守阶段用 workspace/binding 语义；特化后用 `Slot::Persistent` + `load_persistent/store_persistent` | 是，persistent slot | 保守阶段是 |
| Global | 是 | 是 | 保守阶段用 workspace/binding 语义；特化后用 `load_global/store_global` | 否，使用 global binding index | 保守阶段是 |
| WorkspaceDynamic | 是 | 是 | Workspace API；后续可有 `load_workspace/store_workspace` | 否 | 是 |
| ScriptStaticWorkspaceName | 否 | 是 | `Slot::ScriptVar` + `load/store`，运行时可降成 workspace binding id | 是，脚本名字身份 slot；不是私有存储 | 是 |
| Capture | 匿名函数体中是 | 可作为捕获来源 | `Slot::Capture` + `load_slot` | 是，capture slot | 来源可能是 workspace |
| InternalLocal | 是 | 可有 activation 内部状态 | `Slot::InternalLocal` + `load_slot/store_slot` | 是，内部 slot | 否 |
| HiddenRuntime | 是，ABI slot | 是，WorkspaceHandle activation field | `Nargin/Nargout/Varargin/Varargout` slot tag；`WorkspaceHandle` 是 runtime 字段 | 是，ABI slot/activation field | WorkspaceHandle 指向 workspace |

## 12. Lowering 阶段

当前阶段建议把 lowering 分成保守基础 lowering 和后续 binding specialization。

### 保守基础 lowering

基础 lowering 只把“不会被 `clear` 改变绑定类别”的函数静态变量直接降成 slot。对
`global` / `persistent`，先记录声明事实和候选 binding，但访问仍保留 workspace/binding 语义：

```text
function:
  arg/ret/local/static syntax binding
    -> SlotId + load_slot/store_slot

  persistent declaration
    -> record persistent declaration / candidate persistent slot
    -> access remains generic workspace/binding access

  global declaration
    -> record global declaration / candidate global binding
    -> access remains generic workspace/binding access

  eval/script/assignin/load-created names
    -> FunctionWorkspaceView dynamic env

script:
  static workspace names
    -> ScriptVar slot + load/store
    -> runtime may assign workspace binding id

  global declaration
    -> target workspace binding to GlobalCell
    -> base IR may keep ScriptVar slot access or generic binding access

  eval-created names
    -> generic workspace lookup/store
```

这个阶段的目的不是拿到最优 IR，而是先保证 `clear`、`clearvars`、`eval`、`assignin`、`load`
等 binding barrier 不会被提前静态化破坏。

### Binding specialization

后续 pass 做前向 binding-state analysis。对于函数里已经静态分配 slot 的名字，基础 IR 可以
继续使用 `load_slot/store_slot`；区别在于 `load_slot` 默认是带 binding check 的 slot-name
access。只有在能证明某段 region 内没有相关 binding barrier，且该名字在当前程序点仍然绑定
到目标 storage 时，优化层才可以把它降成无检查的更低成本访问：

```text
binding_state(@x) == LiveFrameSlot
  -> load_slot fast path, or raw frame load after eliminating binding check

binding_state(@x) == PersistentSlot
  -> load_persistent / store_persistent

binding_state(@x) == GlobalCell
  -> load_global / store_global

binding_state(@x) == WorkspaceBinding with stable symbol
  -> workspace binding id / inline cache

binding_state(@x) unknown for a static function slot
  -> keep guarded load_slot / store_slot

binding_state(@x) unknown for a workspace-only name
  -> keep generic workspace lookup/store or checked ScriptVar access
```

因此：

- `global` / `persistent` 的声明是静态事实。
- 当前程序点是否仍绑定到 global/persistent storage 是动态状态。
- 普通 local 的 `SlotId` 也是静态事实；当前程序点是否仍是 live frame-slot binding 同样是
  动态状态。
- `load_slot` 是静态 slot 名字的 IR 节点，但它不等于无检查裸 frame load；只有证明 binding
  live 后才能把它当成裸 frame load。
- `load_global/load_persistent` 这类专用节点只表示当前程序点已经证明的 binding，不是基础
  lowering 对整个 code unit 的无条件默认选择。

## 13. clear 对变量绑定和 IR 节点的影响

`clear` 不是普通赋值，也不能建模成简单 `store []`。它会改变当前 activation 或 workspace 中
某个名字的绑定状态。不同变量类型对 `clear` 的反应不同。

### Arg / Ret / Local

对普通函数 slot：

```matlab
clear a
a = 11
```

`clear a` 会让当前 activation 中的 `a -> %slot_a` live binding 失效。`a` 的静态 slot layout
不变，后续赋值可以复用同一个 slot；但在重新赋值之前，后续读取或 `a(...)` 的 `load_slot`
不能再直接读取旧 slot value：

```text
store_slot %slot_a, %v
  marks binding_state(@a) = LiveFrameSlot(%slot_a)

clear_slot %slot_a
  marks binding_state(@a) = Unbound
```

因此下面这种情况仍可以生成 `load_slot %slot_a`，但它必须是带 binding check 的 `load_slot`：

```matlab
function y = f()
  a = 1;
  eval('clear a');
  y = a;
end
```

运行到 `load_slot %slot_a` 时：

```text
if binding_state(@a) == LiveFrameSlot(%slot_a):
  result = frame.slot_values[offset(%slot_a)]
else:
  result = runtime_name_lookup_or_deopt(@a, current_use_continuation)
```

也就是说，IR 节点仍是 `load_slot`；dead 时由 `load_slot` 的慢路径进入按名/binding 解析，处理
“变量不存在、继续查函数/路径/报错”等语义。如果后面有支配性的重新赋值：

```matlab
function y = f()
  a = 1;
  eval('clear a');
  a = 11;
  y = a;
end
```

那么 `a = 11` 可以重新建立 `a -> %slot_a`，后续 `y = a` 才能再次安全使用 `load_slot`。

对 `a(...)` 更要保守：`load_slot a` 的 fast path 命中时，后续可以 `value_apply` 这个变量值；
如果 `load_slot a` 发现 binding dead，则慢路径必须按 `a(...)` 的 continuation 做 generic
名字应用解析，而不是把旧 slot value 交给 `value_apply`。

总结：

```text
SlotId 是否失效:
  不失效。layout 仍有效。

load_slot 是否仍可用于后续 use:
  可用，但默认必须检查 binding。只有证明 binding_state(@a) == LiveFrameSlot 时，才能消掉检查
  并降成裸 frame load。

store_slot 是否仍可用于后续赋值:
  可用，并且会重新建立 frame-slot binding。
```

### Persistent

`clear p` 会让当前 activation 中的 persistent binding 失效；后续同一次调用中的 `p = ...`
不能继续无条件写 persistent cell，需要按当前 binding state 重新分类。下一次进入函数时，
源码中的 `persistent p` 声明会重新建立 persistent binding。

同一个源码名字可能跨 `clear` 切换 binding kind。约束应是“同一程序点不能同时绑定成
persistent 和 ordinary local”，而不是“整个 code unit 中同名永远不能出现两种 binding”。
专用节点和 `clear_binding %slot_p` 细节见
[Global / Persistent IR 节点设计](./global_persistent_ir_design.md#14-clear-对专用节点的影响)。

### Global

`clear g` 会移除当前 workspace 中 `g -> GlobalCell("g")` 的绑定，但不会清除 global cell 本体。
后续 `g = ...` 应重新分类为当前 workspace 的普通 binding；再次出现 `global g` 才重新建立
global binding。`clear global g` 更强，会影响 global cell / registry 本体，并使相关 cache
失效。

因此，`load_global/store_global` 只适用于仍证明绑定到 GlobalCell 的程序点。专用节点和
`clear_binding @g` 细节见
[Global / Persistent IR 节点设计](./global_persistent_ir_design.md#14-clear-对专用节点的影响)。

### WorkspaceDynamic 和 ScriptStaticWorkspaceName

`clear x` 会从当前 target workspace 中删除或解除 `x` 的当前 binding。之后同名赋值按目标
workspace 的当前规则重新创建 binding；已有 workspace binding id 或脚本 binding cache entry
必须失效并重新解析。完整 workspace 规则见 [M 工作区设计](./workspace_design.md)。

### Capture

匿名函数 capture 按值捕获时，`clear` 外层变量不会改变已经构造好的 closure capture storage。
但如果 `clear` 发生在匿名函数构造之前，会影响构造点读取捕获值的行为。

```text
capture slot 是否失效:
  已构造 closure 内的 capture slot 不因外层 clear 失效。
  构造点之前的 clear 会影响捕获来源是否可读。
```

### InternalLocal / HiddenRuntime

用户源码中的 `clear` 不应直接作用于 `InternalLocal` 或 `HiddenRuntime`。这些 slot 用户不可见，
也不应被 `who/whos` 暴露。

```text
internal/hidden slot 是否失效:
  不受用户 clear name 直接影响。
```

## 14. clear 影响矩阵

| 类别 | clear 后绑定变化 | 后续原 IR 节点是否仍可用 | 处理建议 |
| --- | --- | --- | --- |
| Arg | 当前 activation 的 frame-slot binding 失效，layout 保留 | 后续 use 仍可用 `load_slot`，但必须走 binding check；dead 时慢路径 name lookup/deopt | `clear_slot %slot`，kill fast-path binding fact |
| Ret | 当前 activation 的 frame-slot binding 失效，layout 保留 | 后续 use 仍可用 `load_slot`，但必须走 binding check；dead 时慢路径 name lookup/deopt | `clear_slot %slot`，kill fast-path binding fact |
| Local | 当前 activation 的 frame-slot binding 失效，layout 保留 | 后续 use 仍可用 `load_slot`，但必须走 binding check；dead 时慢路径 name lookup/deopt | `clear_slot %slot`，kill fast-path binding fact |
| Persistent | 当前 activation 的 persistent binding 失效 | `load_persistent/store_persistent` 在后续同一 activation 不再安全 | `clear_binding %slot_p`，后续重新分类 |
| Global | 当前 workspace 的 global binding 失效 | `load_global/store_global` 对后续当前 binding 不再安全 | `clear_binding @g`，后续重新分类 |
| WorkspaceDynamic | 当前 workspace binding 被删除/解除 | 已缓存 binding id 失效 | invalidate binding cache |
| ScriptStaticWorkspaceName | target workspace 中该 symbol 的 binding 失效 | workspace binding id 失效 | invalidate script binding cache entry |
| Capture | 已构造 closure capture 不变 | closure 内 capture slot 仍可用 | 构造点前 clear 影响捕获读取 |
| InternalLocal | 不应被用户 clear 直接影响 | 原 IR 节点仍可用 | 不暴露给 clear |
| HiddenRuntime | 不应被用户 clear 直接影响 | 原 hidden state 仍可用 | 不暴露给 clear |

## 15. IR 设计要求

为了表达以上语义，后续 clear 相关 IR 至少需要区分：

```text
clear_slot %slot
  用于 Arg / Ret / Local 这类 per-call frame slot。
  它不删除 slot layout，而是把当前 activation 中的 frame-slot binding 标为 unbound，并使后续
  相关 `load_slot` 的裸 frame-load fast path 失效。后续 `load_slot` 仍可存在，但必须检查
  binding；dead 时进入 name lookup / deopt 慢路径。

clear_binding @name 或 clear_binding %slot_p
  用于解除当前 workspace/activation 中的 global 或 persistent binding。

clear_workspace_binding %env, @name
  用于脚本/base/dynamic workspace 名字。

clear_global @name
  用于清 global cell / registry 本体，并使全局 binding cache 失效。

clear_function @function
  用于清函数 code object 相关状态，包括 persistent cells。
```

如果遇到无法精确分析的动态 clear，例如：

```matlab
eval('clear x')
clear(name_from_runtime)
clear variables
```

则应作为 binding barrier：

```text
invalidate affected binding facts
后续相关名字退回动态 lookup、重新分类，或在第一版中标记 unsupported
```

## 相关文档

- [M 工作区设计](./workspace_design.md)
- [M 函数栈帧设计](./function_frame_design.md)
- [Global / Persistent IR 节点设计](./global_persistent_ir_design.md)
- [匿名函数句柄设计](./anonymous_function_handle_design.md)
- [IR 草案](./ir_draft.md)
