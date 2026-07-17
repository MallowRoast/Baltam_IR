# Store / Load 优化规则设计

本文记录 high-level IR 中 slot `store/load` 相关优化的安全边界。当前已实现的是
`LoadForwardingPass`；后续可以在同一套规则上增加 frame-local dead store elimination。

核心原则：

- `LoadSlotInst` 是唯一显式 slot 读取。
- `StoreSlotInst` 是唯一显式 slot 写入。
- 其它指令是否阻断优化，取决于它是否可能观察或改变相关 slot / workspace / resolver 环境。
- 当前 IR 已移除 `CopyInst`；值别名应直接通过 `ValueId` 重写表达，不再用独立 copy 指令承载。

## 1. Slot 分类

第一版把 slot 分成两组：

| SlotTag | 优化视角 |
|---|---|
| `Local` | frame-local，可做 load forwarding，后续可做 DSE |
| `Ret` | frame-local，但 DSE 需要确认 `ReturnInst` 已直接返回 ValueId |
| `InternalLocal` | frame-local，可做 load forwarding / DSE |
| `Arg` | frame-local，load forwarding 可用；DSE 通常不处理参数初始绑定 |
| `Nargin` / `Nargout` / `Varargin` / `Varargout` | frame-local 特殊 slot，先只允许 load forwarding |
| `Capture` | closure 相关，先不做 DSE |
| `ScriptVar` | environment slot，可能被 workspace / eval / clear 观察 |
| `BaseVar` | environment slot，映射 base workspace |
| `Global` | environment slot，映射 `InterpreterContext::globals` |
| `Persistent` | environment slot，映射 `CodeObject::persistent` |

`LoadForwardingPass` 当前可以跟踪所有 slot，但遇到 opaque 环境行为时只清理 environment slot 状态。
这允许函数内普通 local slot 跨过普通 call 保持转发状态，同时保护脚本 / workspace 语义。

## 2. 指令副作用表

下表是 store/load 优化使用的 pass-local 分类，不完全等同于 `Instruction::effect` 的粗粒度值。

| 指令 | 当前 effect | Slot 读写 | Store/load 优化规则 |
|---|---:|---|---|
| `ConstInst` | `Pure` | 不读写 slot | 可穿过 |
| `LoadSlotInst` | `Frame` | 读 `slot` | 读取同一 slot 时消费前序 store；可被前序 store 转发 |
| `StoreSlotInst` | `Frame` | 写 `slot` | 写同一 slot 时覆盖前序 store，并更新转发状态 |
| `GlobalDeclInst` | `Env` | 重新绑定声明中的 `Global` slot | 只 invalidate `inst.slots`，不影响其它 slot |
| `PersistentDeclInst` | `Env` | 重新绑定声明中的 `Persistent` slot | 只 invalidate `inst.slots`，不影响其它 slot |
| `CreateNamedFunctionHandleInst` | `Env` | 不读写 frame slot | 不阻断 store/load 优化；但 CodeObject 需要记录 resolver 依赖 |
| `CreateAnonymousFunctionHandleInst` | `Heap` | 不直接读写 slot，只使用 `captured_value` | 不按 slot barrier 处理；捕获值本身通过 ValueId use 表达 |
| `ApplyInst` | `Opaque` | 未解析，可能索引/调用/环境观察 | invalidate environment slot 状态 |
| `ValueApplyInst` | `Opaque` | 未解析，可能索引/函数句柄调用/对象分派 | invalidate environment slot 状态 |
| `MagicEndInst` | `Opaque` | 依赖索引上下文和运行时分派 | invalidate environment slot 状态 |
| `CallInst` | `Opaque` | 默认可能触发用户代码或环境观察 | invalidate environment slot 状态；后续可对白名单 pure internal call 放宽 |
| `UnaryInst` | `Pure` | 操作数一般为 ValueId，不隐式读 slot | `dispatch_type != Internal` 时 invalidate environment slot 状态 |
| `BinaryInst` | `Pure` | 操作数一般为 ValueId，不隐式读 slot | `dispatch_type != Internal` 时 invalidate environment slot 状态 |
| `GotoInst` | `Pure` | 不读写 slot | 结束当前 basic block；第一版不跨 block 分析 |
| `BranchInst` | `Pure` | 读取 condition ValueId / Operand | 结束当前 basic block；若未来允许 Slot operand，应按读 slot 处理 |
| `ReturnInst` | `Pure` | 读取返回 ValueId | 结束当前 basic block；DSE 需要检查返回值是否依赖 ret slot |

说明：

- `CreateNamedFunctionHandleInst` 读取 resolver/path 环境，但不读写当前 frame slot。它不应阻断
  `Local/Ret/InternalLocal` 的 store/load 优化。
- `GlobalDeclInst` / `PersistentDeclInst` 不是全局 barrier。它们只影响声明列表中的 slot。
- dynamic `add` 这类 `BinaryInst` 数据流上不隐式 load slot，但语义上可能进入 Matlab 分派，因此
  对 environment slot 是 barrier。

## 3. Load Forwarding

目标是把：

```text
store %slot_s, %v
...
%x = load %slot_s
```

改写为：

```text
; %x 的所有 use 改成 %v
```

并删除对应 `LoadSlotInst`。

### 3.1 当前实现范围

当前 `LoadForwardingPass` 是 single basic block pass：

- 每个 basic block 独立维护 `SlotId -> ValueId` 状态。
- 遇到 `StoreSlotInst` 时更新该 slot 的当前值。
- 遇到 `LoadSlotInst` 时，如果同 slot 有可用状态，则记录 `load.result -> stored_value` 重写。
- pass 结束时统一重写 uses，并删除被转发的 load。

### 3.2 状态失效规则

状态失效按 slot 精确度处理：

- `StoreSlotInst(s)`：只更新 `s`。
- `GlobalDeclInst(slots)`：只删除 `slots` 的状态。
- `PersistentDeclInst(slots)`：只删除 `slots` 的状态。
- `ApplyInst / ValueApplyInst / MagicEndInst / CallInst`：删除 environment slot 状态。
- `UnaryInst / BinaryInst` 且 `dispatch_type != Internal`：删除 environment slot 状态。
- `CreateNamedFunctionHandleInst`：不删除状态。
- `CreateAnonymousFunctionHandleInst`：不删除状态。

这里的 environment slot 指：

```text
ScriptVar / BaseVar / Global / Persistent
```

Local / Ret / InternalLocal 不会因为普通 opaque call 自动失效，因为当前 IR 没有表示“被调用者能直接写
当前 frame local slot”的机制。后续 nested function、eval、assignin 或 debugger 语义接入后，需要
增加更强的 barrier。

## 4. Dead Store Elimination

DSE 目标是删除不会被观察到的 store：

```text
store %slot_s, %v0
...
store %slot_s, %v1
```

如果 `%v0` 这次写入在被 `%v1` 覆盖前没有任何读取或逃逸，则第一条 store 可以删除。

### 4.1 第一版建议范围

先只处理：

```text
Local / Ret / InternalLocal
```

暂不处理：

```text
ScriptVar / BaseVar / Global / Persistent / Capture
```

原因是这些 slot 可能被 workspace、global table、persistent table、closure 或未来动态机制观察。

### 4.2 单 basic block 保守规则

一条 `store %slot_s, %v` 可以删除，当且仅当：

- `%slot_s` 是允许 DSE 的 frame-local slot。
- 从该 store 到下一次同 slot store 或 block 结束之间，没有 `load %slot_s`。
- 中间没有可能观察当前 frame local 的 barrier。
- 如果 `%slot_s` 是 `Ret`，函数返回已经通过 `ReturnInst::values` 直接返回 ValueId，不依赖退出时扫描
  ret slot。
- 删除后 verifier 仍然通过，且所有 ValueId use 保持合法。

第一版不跨 basic block 做 DSE。跨 block 版本需要 CFG、dominator/post-dominator 或 slot liveness。

### 4.3 `Ret` slot 的特殊情况

对于：

```text
store %slot_ret, %v
ret %v
```

如果后面没有 `load %slot_ret`，且 `ReturnInst` 已直接返回 `%v`，这条 ret slot store 对返回值已经不是
必要条件，可以删除。

但如果 IR 形态是：

```text
store %slot_ret, %v
%r = load %slot_ret
ret %r
```

应先由 load forwarding 改成：

```text
store %slot_ret, %v
ret %v
```

再由 DSE 删除 store。

## 5. 与 CodeObject 阶段优化的关系

AST -> IR 后的环境无关优化只能使用保守规则：

- 不折叠 `dispatch_type = Dynamic` 的 unary/binary/call。
- dynamic unary/binary 对 environment slot 是 barrier。
- 不删除 environment slot store。

CodeObject 构建期可以在名字解析和依赖 guard 已记录后放宽：

- `Builtin/Internal` 且已知 pure 的 numeric op 可不作为 barrier。
- pure internal helper 可加入 call 白名单。
- 常量折叠后再次运行 load forwarding 和 DSE。

例如：

```matlab
function s = test10()
s = 1;
s = s + 1;
s = s + 2;
end
```

当前 cleanup pass 可以把冗余 load 转发掉；CodeObject 阶段若确认 `+` 是纯 builtin double plus，
可以继续常量折叠并删除 ret slot store，最终接近：

```text
%0 = const 4
ret %0
```

## 6. 后续实现顺序

1. 保持 `LoadForwardingPass` 为 single-block pass，并按本文表格维护 barrier。
2. 新增 single-block `DeadStoreEliminationPass`，只处理 `Local / Ret / InternalLocal`。
3. 增加 pure internal call 白名单，允许部分 `CallInst(dispatch_type = Internal)` 不清 environment
   slot 状态。
4. 在 CodeObject 构建期加入 resolver-aware constant folding，再重复运行 load forwarding / DSE。
