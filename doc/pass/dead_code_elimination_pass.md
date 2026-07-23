# Dead Code Elimination Pass

源码位置：`src/pass/dead_code_elimination_pass.cpp`

Pass 名称：`dead-code-elimination`

作用域：`IRPassScope::CodeUnit`

## 优化内容

该 pass 删除当前已经能安全证明无用的 IR：

1. 没有任何 use 的 `ConstInst`
2. 同一个 basic block 内被后续同 slot store 覆盖、覆盖前没有 load 的 `StoreSlotInst`

示例：

```text
[%0, double] = const 1
[%1, double] = const 2
store %slot0, %0
store %slot0, %1
```

可改写为：

```text
[%1, double] = const 2
store %slot0, %1
```

被删除 store 的操作数如果因此没有其它 use，会在同一轮中作为 unused const 删除。

## 当前范围

unused const 删除是 code-unit 级 use-count：

- 统计所有指令对 `ValueId` 的使用
- 删除 result 不在 use 集合中的 `ConstInst`
- 清空对应 `ValueInfo::def`

重复 store 删除是 single-basic-block 规则：

- 只处理 `Local / Ret / InternalLocal` slot
- 只删除被后续同 slot store 覆盖的前一条 store
- 覆盖前如果出现 `load` 同一个 slot，则前一条 store 保留
- 不跨 basic block 做 slot liveness
- 不删除 block 末尾最后一次 store，即使当前 pass 看不到后续 load

## Barrier 规则

遇到可能观察或改变运行期环境的指令时，当前 block 内尚未覆盖的 pending store 状态会清空：

- `GlobalDeclInst`
- `PersistentDeclInst`
- `CreateNamedFunctionHandleInst`
- `CreateAnonymousFunctionHandleInst`
- `ApplyInst`
- `ValueApplyInst`
- `MagicEndInst`
- `CallInst`
- `UnaryInst / BinaryInst` 且 `dispatch_type != Internal`

这是保守规则。后续如果有更精确的 effect model，可以放宽部分 barrier。

## 安全边界

- 不删除 `ApplyInst / CallInst`。
- 不删除 unused `LoadSlotInst`。
- 不删除 dynamic `UnaryInst / BinaryInst`，即使结果无 use。
- 不处理 `ScriptVar / BaseVar / Global / Persistent / Capture` store。
- 不跨 basic block 删除 store。

## 推荐使用顺序

默认 cleanup pipeline 中作为最后一轮 cleanup：

```text
load-forwarding
constant-folding
dead-branch-elimination
cfg-simplification
load-forwarding
constant-folding
constant-deduplication
dead-code-elimination
```

前置 pass 会暴露 unused const 和被覆盖 store；后置 `load-forwarding` 先吃掉 CFG 合并后出现的
同 block `store; load`，后置 `constant-folding` 再折叠因此暴露出的常量运算，
`constant-deduplication` 合并重复常量，DCE 最后删除这些残留。
