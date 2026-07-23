# Dead Branch Elimination Pass

源码位置：`src/pass/dead_branch_elimination_pass.cpp`

Pass 名称：`dead-branch-elimination`

作用域：`IRPassScope::CodeUnit`

## 优化内容

该 pass 消费常量折叠后的分支条件。当 `BranchInst` 的 condition 已经由 `ConstInst`
定义，并且该常量可以按运行时 truthiness 规则转换成 logical scalar 时，将条件分支改写为
无条件跳转。

示例：

```text
[%0, logical] = const true
br %0, label L1, label L2
```

可改写为：

```text
[%0, logical] = const true
br label L1
```

该 pass 只改写 terminator 和 CFG 边，不删除基本块。改写后变成不可达的 block 由后续
`cfg-simplification` 内部的 unreachable block elimination 删除。

## 当前范围

第一版只处理：

- `BranchInst`
- condition 是 `ValueId`
- condition 的 `ValueInfo::def` 是 `ConstInst`
- 常量可以转换为 `ba_obj`
- `ba_obj::as_bool()` 可以给出确定结果

当前支持的 IR constant 来源：

- `LogicalConstant`
- `Int64Constant`
- `UInt64Constant`
- `Float64Constant`
- `RuntimeObjectConstant`

其中 `RuntimeObjectConstant` 是常量折叠 pass 调 builtin 后产生的主要输入形状。

## CFG 维护

改写分支时必须同时维护 CFG 边：

- 原 `BranchInst` 替换成 `GotoInst`
- 当前 block 的 `successors` 改成唯一选中目标
- 未选中目标的 `predecessors` 移除当前 block
- 选中目标的 `predecessors` 保证包含当前 block

这样即使 PassManager 开启 `verify_after_each_pass`，该 pass 之后的 IR 也能保持 verifier-clean。

## 安全边界

- 不对动态条件求值。
- 不分析 `ApplyInst` / `CallInst` 结果，除非它们已经被前置 pass 折叠成 `ConstInst`。
- 不把 char、string、cell、struct 等不可安全转换为 scalar logical 的常量作为分支条件。
- 不删除 `ConstInst` 或其他现在变成 dead 的普通指令；这属于后续 dead code elimination。

## 推荐使用顺序

默认 cleanup pipeline 中放在 constant folding 之后、CFG simplification 之前：

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

`constant-folding` 负责把比较结果、`sin` 等白名单 direct call 结果折叠为常量；
`dead-branch-elimination` 把常量条件分支改成无条件跳转；
`cfg-simplification` 随后删除不可达块并合并线性 CFG；
后置 `load-forwarding` 清理合并后暴露的 `store; load`；
后置 `constant-folding` 折叠 load forwarding 进一步暴露出的常量运算；
`dead-code-elimination` 清理不再被使用的常量。
