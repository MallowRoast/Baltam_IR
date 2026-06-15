# Global / Persistent IR 节点设计

本文定义后续支持 `global` 和 `persistent` 时可能加入的 high-level IR 专用节点。当前源码尚未
实现这些节点，因此本文是待实现 schema 设计，不替代 `ir_schema.md` 的现状描述。

这些专用节点不要求基础 lowering 立即生成。当前更稳的策略是：基础 lowering 先保留
workspace/binding 语义，记录 `global` / `persistent` 声明；后续 binding-state analysis 证明某
个 region 内没有 `clear`、`clearvars`、`eval` 等 binding barrier 后，再把 workspace/binding
访问降级为本文定义的专用节点。

核心结论是：

> `persistent` 可以设计成静态 slot 绑定，因为它属于当前函数，slot 位置可以在 lowering
> 阶段确定；但它的存储不是 per-call frame value，仍需要专门的读写语义。
>
> `global` 不应设计成 frame slot。它的真实存储属于 session/global table，跨函数共享，IR
> 中应按 symbol 或 global binding 访问。

## 1. Binding 分类

建议先在语义上区分这些变量绑定：

```text
LiveFrameSlot
  Arg
  Ret
  Local
  SlotId / frame offset 由 layout 静态确定，但当前 activation 中名字仍必须 live

PersistentSlot
  当前 FunctionUnit 内静态确定的 persistent slot
  SlotId 稳定，但 storage 指向函数私有 persistent cell

DynamicEnv
  eval / script / load / assignin 动态创建的 workspace 名字

GlobalCell
  global 声明接入的 session 共享变量
```

`persistent` 是 local-like binding：名字只属于当前函数，其他函数不能通过同名 persistent 访问
同一个存储。它适合作为 `SlotTable` 里的静态 slot，因为 slot 位置由当前函数文本决定。

但 persistent 的值跨调用保留，所以这个 slot 不应映射到每次调用新建的
`FunctionFrame::slot_values[offset]`，而应映射到当前函数 code object 的 persistent cell。

`global` 是 shared binding：多个声明了同名 `global x` 的 workspace 会读写同一个全局 cell，
其他函数调用可能改变它。它不属于某个函数 frame layout，因此不应使用 `SlotId` 表达。

## 2. Slot 表示

建议为 persistent 引入显式 slot 类别：

```text
Slot::Type
  Arg
  Local
  InternalLocal
  Capture
  Ret
  Hidden
  Persistent
```

如果后续不想扩展 `Slot::Type`，也可以用：

```text
Slot::Local + SlotAttrs::storage_class = Persistent
```

但第一版更推荐 `Slot::Persistent`，因为 verifier、printer、frame layout 和优化 pass 都能直接
看到它不是普通 local。

普通 local slot 和 persistent slot 的差别是：

```text
Local slot:
  SlotId -> frame offset -> FunctionFrame::slot_values[offset]
  clear name 之后 layout 保留，但 name -> SlotId live binding 会失效

Persistent slot:
  SlotId -> persistent slot offset -> FunctionCodeObject::persistent_cells[offset]
```

二者都可以在函数内静态确定 slot 位置，但生命周期和存储位置不同。

## 3. 专用指令

建议 specialization 后使用独立节点，而不是通用 `load_binding/store_binding`。独立节点更利于
printer、verifier、smoke test 和后续 effect 建模。

```text
LoadGlobalInst
  result : ValueId
  symbol : InternedString

StoreGlobalInst
  symbol : InternedString
  value  : Operand

LoadPersistentInst
  result  : ValueId
  slot_id : SlotId

StorePersistentInst
  slot_id : SlotId
  value   : Operand
```

语义：

```text
load_global @x
  读取当前 session/global table 中名为 x 的 GlobalCell.value

store_global @x, %v
  写入当前 session/global table 中名为 x 的 GlobalCell.value

load_persistent %slot_p
  读取当前 FunctionUnit 私有 persistent cell 中 %slot_p 对应的值

store_persistent %slot_p, %v
  写入当前 FunctionUnit 私有 persistent cell 中 %slot_p 对应的值
```

这里的关键区别是：global 以 `symbol` 定位共享全局绑定，persistent 以 `SlotId` 定位当前函数
内部的静态 persistent 绑定。

## 4. 指令 effect

当前 `EffectClass` 只有：

```text
Pure | Frame | Heap | Env | Opaque
```

第一版可以先把四个新节点标为 `Env`：

- `load_global` / `store_global`：读写全局变量表，显然不是当前 frame state。
- `load_persistent` / `store_persistent`：读写函数私有持久表，虽然由 `SlotId` 定位，但跨调用
  可观察。

后续如果优化 pass 需要更细粒度，可以拆出：

```text
GlobalState
PersistentState
```

不要把 `load_persistent/store_persistent` 简单标成 `Frame`。它的地址是 slot 化的，但它不是
当前调用 frame 的 per-call value；递归、重入、`clear function`、debugger 和 deopt 都需要
看到它是独立状态。

## 5. 为什么 persistent 用 SlotId 但不用 load_slot

`load_slot / store_slot` 表达的是当前调用 frame 内的固定 slot：

```text
frame.slot_values[offset]
```

每次函数调用都会创建新的 frame 和新的普通 local slot value。

`persistent` 的 slot 位置也能静态确定，但它指向的是函数私有 persistent cell：

```text
function_code_object.persistent_cells[persistent_offset(slot_id)]
```

递归或再次调用同一函数时，读写的是同一份 cell。

因此，persistent 可以是 slot，但不应复用 `load_slot/store_slot`。更准确的表达是：

```text
slot-addressed persistent storage
```

而不是：

```text
per-call frame slot storage
```

## 6. 为什么 global 不用 SlotId

`global` 的存储是 session 共享 cell：

```text
global_table[name].value
```

同名 global 可能被多个函数、脚本或命令行上下文共享修改。它不属于某个 `FunctionUnit` 的
frame layout，也没有当前函数内可独立拥有的 frame slot 位置。

bytecode lowering 可以把 `symbol` 降成某种 global binding index，用于加速查找：

```text
IR @g -> global_binding_index
```

但这个 index 是全局绑定表或 cache 的索引，不是 `SlotId`，也不是 frame slot offset。

## 7. Declaration 节点是否需要

源码里的：

```matlab
global x
persistent p
```

主要影响当前代码单元的名字绑定，不一定需要在可执行 IR 中保留成普通指令。建议分两层：

```text
CodeUnit metadata:
  global_symbols : InternedString[]

FunctionUnit slot table:
  persistent slot entries

Executable IR:
  load_global / store_global
  load_persistent / store_persistent
```

声明 metadata / slot entries 用于：

- verifier 检查 load/store 是否引用了合法绑定
- printer 打印 unit 级绑定信息
- bytecode lowering 构造 global binding table 和 persistent cell layout
- debugger / `who` / `whos` 识别用户可见变量

如果后续需要表达声明位置的诊断或动态错误行为，可以再引入非值指令：

```text
DeclareGlobalInst
DeclarePersistentInst
```

但第一版不建议把声明作为普通 effectful 指令放进主控制流。

## 8. CodeUnit 元数据

建议在 `FunctionUnit` 或 `CodeUnit` 上增加 global 声明表，并让 persistent 进入 slot 表：

```text
CodeUnit
  global_symbols : GlobalSymbolDecl[]

FunctionUnit
  persistent_slots : SlotId[]

GlobalSymbolDecl
  name        : InternedString
  source_span : SourceSpan
```

约束：

- `Slot::Persistent` 只能出现在 `FunctionUnit` 或后续允许 persistent 的函数类 code unit。
- `global_symbols` 可以出现在 `FunctionUnit`，也可以在脚本语义允许时出现在 `ScriptUnit`。
- 同一程序点上，同名不能同时绑定为普通 local、global 和 persistent。
- 同一 code unit 中，`clear` 之后的后续语句可能让同一个源码名字进入新的 binding state；
  例如 `persistent p; clear p; p = 1` 中，最后的 `p` 不再写 persistent cell，而是普通 local
  绑定。verifier 不应简单按全 code unit 禁止这种“跨 clear 的绑定切换”。
- 同一 code unit 中，同名 global 声明应去重，重复声明可以 warning 或按 Matlab 行为兼容。
- `persistent_slots` 按函数内 persistent 声明顺序或 slot 创建顺序保存，作为 persistent cell
  layout 的稳定输入。

## 9. Lowering 规则

基础 lowering 和 specialization 应分开：

```text
基础 lowering:
  global / persistent 声明 -> 记录声明事实
  global / persistent 名字访问 -> 保留 workspace/binding 语义

binding specialization:
  如果证明当前程序点 binding 仍为 GlobalCell
    -> load_global/store_global

  如果证明当前程序点 binding 仍为 PersistentSlot
    -> load_persistent/store_persistent
```

下面示例描述的是 specialization 之后的专用 IR 形状，不要求基础 lowering 直接生成。基础
lowering 应优先保留脚本 `ScriptVar` slot 访问、函数 checked slot 访问，或等价 generic
binding access。

### 函数中的 global

```matlab
function y = f()
  global g
  g = g + 1;
  y = g;
end
```

lowering：

```text
%g0 = load_global @g
%one = const 1
%g1 = call plus(%g0, %one)
store_global @g, %g1
%g2 = load_global @g
store_slot %slot_y, %g2
```

基础 lowering 阶段，`g` 进入当前 code unit 的 global 声明表，但不创建 `SlotId`。若后续分析
证明从 `global g` 到这些访问之间没有相关 binding barrier，则可把 workspace/binding 访问特化成
上面的 `load_global/store_global`。

### 函数中的 persistent

```matlab
function y = counter()
  persistent n
  if isempty(n)
    n = 0;
  end
  n = n + 1;
  y = n;
end
```

lowering 前先创建 persistent slot：

```text
%slot_n = persistent slot @n
```

正文 lowering：

```text
%n0 = load_persistent %slot_n
%is_empty = call isempty(%n0)
branch %is_empty, init, cont

init:
  %zero = const 0
  store_persistent %slot_n, %zero
  goto cont

cont:
  %n1 = load_persistent %slot_n
  %one = const 1
  %n2 = call plus(%n1, %one)
  store_persistent %slot_n, %n2
  store_slot %slot_y, %n2
```

基础 lowering 阶段，`n` 可以记录为 persistent 声明和候选 persistent slot，但访问可以先保留为
workspace/binding 语义。若后续分析证明当前程序点的 `n` 仍绑定到 persistent cell，则可特化成
上面的 `load_persistent/store_persistent`。

### 脚本中的 global

脚本没有自己的 frame。脚本中的：

```matlab
global g
g = 1;
```

应把当前 target workspace 中的 `g` 绑定到 global cell，之后脚本对 `g` 的读写可以有两种实现：

1. 保守方案：仍用 `ScriptVar` slot 访问或 generic workspace binding access，workspace lookup
   命中 `GlobalCell`。
2. 特化方案：脚本上下文特化后改成 `load_global/store_global`。

第一版建议先采用保守方案，避免基础 script IR 和运行目标 workspace 过早绑定。

`persistent` 不属于脚本本体；脚本不创建 persistent slot。

## 10. Verifier 约束

新增节点后，verifier 至少检查：

- `LoadGlobalInst::result` 是当前 unit 中合法 `ValueId`
- `LoadPersistentInst::result` 是当前 unit 中合法 `ValueId`
- `StoreGlobalInst::value` 是合法 `Operand`
- `StorePersistentInst::value` 是合法 `Operand`
- `LoadGlobalInst::symbol` / `StoreGlobalInst::symbol` 不能为空
- `LoadPersistentInst::slot_id` / `StorePersistentInst::slot_id` 必须引用存在的 slot
- persistent load/store 引用的 slot 必须是 `Slot::Persistent`
- `Slot::Persistent` 只能出现在允许 persistent 的 code unit 中
- 若启用声明表检查，global load/store 的 `symbol` 必须在 global 声明表中
- 同一程序点上，同名不能同时解析为普通 local slot、persistent slot、global decl
- 跨过 `clear_binding` 后，同一个源码名字可以重新分类为另一个 binding kind；此时后续专用
  load/store 必须匹配新的 binding state

如果当前采用保守基础 lowering，则 verifier 不能要求所有 global/persistent 访问都使用专用
节点。专用节点只应在 binding specialization 后出现，并且必须引用当前程序点仍有效的绑定。

## 11. Printer 建议

文本 IR 建议打印为：

```text
globals: @g
slots:
  %slot_y = ret @y
  %slot_n = persistent @n

%1 = load_global @g
store_global @g, %2

%3 = load_persistent %slot_n
store_persistent %slot_n, %4
```

这样读 IR 时能立即看出：

- global 没有 slot
- persistent 有 slot，但读写指令不是 `load_slot/store_slot`

## 12. Bytecode lowering

bytecode 层可以分别 lowering：

```text
IR @g        -> global_binding_index
IR %slot_n   -> persistent_cell_offset
```

建议 bytecode 指令：

```text
BC_LOAD_GLOBAL_BINDING      global_binding_index
BC_STORE_GLOBAL_BINDING     global_binding_index
BC_LOAD_PERSISTENT_SLOT     persistent_cell_offset
BC_STORE_PERSISTENT_SLOT    persistent_cell_offset
```

其中：

- global binding index 指向 session global table entry 或可缓存 lookup record
- persistent cell offset 指向当前 function code object 的 persistent cell

## 13. 优化语义

`global` 是强动态共享状态：

- 任意调用可能修改同名 global
- 优化区跨调用时不能随意缓存 global value
- 需要 global epoch 或 call effect summary 保护

`persistent` 比 global 稳定，但仍不是普通 local：

- 只属于当前函数
- 其他函数不能通过同名 persistent 直接修改它
- 同一函数递归/重入会共享同一 cell
- `clear function_name`、文件失效和 debug/observer 需要使相关假设失效
- 优化 pass 可以用 `SlotId` 跟踪它，但不能按普通 per-call frame slot 做寄存器化

因此，persistent 是 slot-addressed function-private state；global 是 symbol-addressed shared state。

## 14. clear 对专用节点的影响

`global` / `persistent` 的声明是静态事实，但当前 binding 是否仍有效是运行时状态。`clear` 可以
破坏这个状态。

```matlab
global g
g = 10
clear g
g = 20
```

`clear g` 会解除当前 workspace 中 `g -> GlobalCell("g")` 的绑定。后续 `g = 20` 不能继续使用
`store_global @g`，除非后面再次出现 `global g` 重新建立绑定。

```matlab
persistent p
p = 10
clear p
p = 20
```

`clear p` 会让当前 activation 中的 persistent binding 失效。后续同一次调用中的 `p = 20`
不能继续使用 `store_persistent %slot_p`；下一次调用重新进入函数时，源码中的 `persistent p`
声明会重新建立 persistent binding。

因此，`clear` 相关 IR 应作为 binding barrier：

```text
clear_binding @g
  invalidates later load_global/store_global for @g in the current binding context

clear_binding %slot_p
  invalidates later load_persistent/store_persistent for %slot_p in the current activation
```

优化 pass 不能把 `load_global/store_global` 或 `load_persistent/store_persistent` 跨过相关
`clear_binding` 使用。

## 15. 与现有 IR 的关系

现有节点边界保持：

- `load_slot/store_slot`：当前 frame slot；用户可见名字还必须处于 live frame-slot binding
- `ScriptVar` slot 访问：脚本静态名字身份；运行时绑定到目标 workspace 中的存储
- generic workspace binding access：目标 workspace 的动态按名访问，当前源码尚无独立
  `LoadWorkspaceInst / StoreWorkspaceInst`
- `load_global/store_global`：显式 global cell 访问，使用 `symbol`
- `load_persistent/store_persistent`：当前函数 persistent cell 访问，使用 `SlotId`

在当前保守策略中，脚本 `ScriptVar` slot、函数 checked slot 或 generic binding access 是
global/persistent 访问的基础表达；`load_global/load_persistent` 是后续证明 binding 稳定后的
降级目标。这样既避免过早静态化 `clear` 可能破坏的 binding，也保留了后续降成 O(1) global
binding index 或 persistent slot offset 的空间。

## 相关文档

- [M 变量模型设计](./variable_model_design.md)
- [M 工作区设计](./workspace_design.md)
- [M 函数栈帧设计](./function_frame_design.md)
- [IR Schema](./ir_schema.md)
