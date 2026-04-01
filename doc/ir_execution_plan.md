# Baltam_IR 执行链规划

## 当前状态

当前仓库的 CLI 主线是：

`parse -> lower(non-SSA) -> print`

当前没有启用的解释执行主链。

也就是说，这份文档不再描述旧的：

- `lower -> execute`
- hybrid IR interpreter

这些路径已经不再是当前仓库的正式主线。

## 当前主线

当前最重要的事情是先稳定：

`AST -> non-SSA IR`

具体包括：

- lowering
- IR 结构
- verifier
- 打印器

## 后续执行链应如何恢复

后续如果要重新建立执行链，更合理的目标不是回到旧 hybrid 解释器，而是：

`AST -> non-SSA IR -> analysis -> untyped SSA IR -> optimize(optional) -> SSA interpreter`

也就是说，执行链的恢复应该建立在 SSA IR 之上，而不是旧 value-based/hybrid IR 之上。

## 为什么不直接恢复旧解释器

因为当前仓库已经完成了这些重构：

- 旧 IR 已移除
- 当前 IR 已统一成 non-SSA IR
- CFG 容器和节点语义已经和旧解释器不匹配

如果现在重新接旧解释器，只会重新引入过时语义。

## 推荐阶段

### Phase 1

稳定 non-SSA IR：

- 增加测试样例
- 扩大 lowering 覆盖
- 增强 verifier

### Phase 2

完成 analysis 和 SSA 构建：

- CFGAnalysis
- DominatorTree
- DominanceFrontier
- Liveness
- DefUse
- BuildPrunedSSA

### Phase 3

建立 untyped SSA IR 解释器。

### Phase 4

在 SSA IR 上增加 profile 和基础优化。

### Phase 5

再进入 typed SSA 和 LLVM IR。

## 当前结论

当前仓库已经不是“先做解释器”的阶段，而是“先做 IR 和中端地基”的阶段。

因此执行链的近期目标应是：

- `lower -> verify -> print`

而不是：

- `lower -> execute`
