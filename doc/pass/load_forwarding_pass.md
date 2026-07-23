# Load Forwarding Pass

源码位置：`src/pass/load_forwarding_pass.cpp`

Pass 名称：`load-forwarding`

作用域：`IRPassScope::CodeUnit`

## 优化内容

该 pass 在单个 basic block 内把 store 后的同 slot load 转发为已知 `ValueId`。

示例：

```text
store %slot0, %0
%1 = load %slot0
%2 = add %1, %3
```

可改写为：

```text
store %slot0, %0
%2 = add %0, %3
```

`%1` 的所有 use 会被替换为 `%0`，对应 `LoadSlotInst` 会删除，`ValueInfo::def` 会清空。

## 当前范围

- 只做 single basic block 分析。
- 不跨 block 使用 predecessor / dominator / liveness 信息。
- 对每个 block 独立维护 `SlotId -> ValueId` 状态。
- 支持多级 value rewrite 压缩。

## Barrier 规则

详细表见 [Store / Load 优化规则设计](../store_load_optimization_design.md)。

当前实现的关键规则：

- `StoreSlotInst(s)` 更新 slot `s` 的当前值。
- `LoadSlotInst(s)` 如果命中当前值，则可转发。
- `GlobalDeclInst(slots)` 只失效声明列表中的 slot。
- `PersistentDeclInst(slots)` 只失效声明列表中的 slot。
- `ApplyInst / ValueApplyInst / MagicEndInst / CallInst` 失效 environment slot 状态。
- `UnaryInst / BinaryInst` 在 `dispatch_type != Internal` 时失效 environment slot 状态。
- `CreateNamedFunctionHandleInst` 不读写当前 frame slot，不作为 frame-local barrier。
- `CreateAnonymousFunctionHandleInst` 使用 `captured_value`，不按 slot barrier 处理。

environment slot 当前指：

```text
ScriptVar / BaseVar / Global / Persistent
```

## 安全边界

- 不删除 `StoreSlotInst`。
- 不跨 basic block 转发。
- 不把 dynamic operator/call 当纯 primitive。
- 不尝试证明 workspace 绑定长期稳定。

## 推荐使用场景

- lowering 后的基础 cleanup
- 常量去重后消除 `store; load` 形状
- CFG simplification 后再次清理合并 block 暴露出的 `store; load`
- CodeObject 构建期常量折叠或 DSE 前后重复运行
