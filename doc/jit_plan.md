# Baltam_IR JIT 方案整理

## 当前结论

JIT 不是当前阶段的下一步。

当前最合理的路线是先完成：

`non-SSA IR -> analysis -> untyped SSA IR -> profile -> typed SSA IR -> LLVM IR`

JIT 应该发生在这条链的后半段，而不是在 non-SSA IR 还没稳定时提前进入主线。

## 当前仓库离 JIT 还差什么

当前已经有：

- AST parsing
- non-SSA lowering
- IR printer
- 最小 verifier
- AnalysisManager / PassManager 骨架

当前还缺：

- 5 个基础 analysis
- untyped SSA IR
- SSA 构建 pass
- 任何正式优化 pass
- profile 基础设施
- typed SSA IR
- LLVM IR lowering

## JIT 的推荐定位

JIT 更适合作为 typed SSA IR 之后的热点后端，而不是当前项目的主 IR。

建议定位为：

`typed SSA IR -> LLVM IR -> native code`

而不是：

`AST -> 直接 LLVM/MLIR`

## 为什么不建议现在做 JIT

原因很直接：

- 当前还没有 SSA
- 还没有 profile
- 还没有类型专门化
- 还没有 optimizer pass

在这些基础设施没稳定前做 JIT，会把问题都推到更难调试的层次。

## 真正进入 JIT 前的前置条件

至少应具备：

1. 稳定 non-SSA IR
2. 稳定 verifier
3. CFG / dominator / liveness / def-use
4. untyped SSA IR
5. BuildPrunedSSA
6. 基础优化 pass
7. profile
8. typed SSA specialization

## 推荐路线

### Phase 1

完成 non-SSA IR 和 analysis 基建。

### Phase 2

完成 untyped SSA IR 和基础优化。

### Phase 3

在 untyped SSA IR 上加入 profile。

### Phase 4

将热点片段 specialized 为 typed SSA IR。

### Phase 5

把 typed SSA IR lowering 到 LLVM IR，再进入 JIT。

## 当前阶段更值得做的事

当前真正值得投入的是：

- CFGAnalysis
- DominatorTree
- DominanceFrontier
- Liveness
- DefUse
- BuildPrunedSSA
- verifier 扩充

这些才是后续 JIT 的必要地基。
