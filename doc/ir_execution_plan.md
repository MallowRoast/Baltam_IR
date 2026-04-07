# Baltam_IR 执行链说明

## 当前状态

当前仓库已经有两条实际存在的链路。

CLI 主线：

`parse -> lower(non-SSA) -> verify -> analyses -> construct_untyped_ssa -> verify -> print`

测试执行链：

`parse -> lower(non-SSA) -> verify -> construct_untyped_ssa -> verify -> execute(UntypedSSA)`

也就是说，仓库已经不再是“只有 IR、不具备执行能力”的状态；只是执行入口目前主要在测试侧，而不是 `main.cpp`。

## 当前主线包含什么

当前正式主线已经包含：

- non-SSA lowering
- CFG / Dominator / DominanceFrontier / Liveness / DefUse
- untyped SSA 构建
- 分阶段 verifier
- non-SSA / untyped SSA 打印

对应实现分别位于：

- [src/lowering/lowering.cpp](/home/zj/Desktop/Baltam_IR/src/lowering/lowering.cpp)
- [src/analysis](/home/zj/Desktop/Baltam_IR/src/analysis): 目录中的 5 个基础 analysis 与 verifier
- [src/optimizer/construct_untyped_ssa.cpp](/home/zj/Desktop/Baltam_IR/src/optimizer/construct_untyped_ssa.cpp)
- [src/ir/ir_printer.cpp](/home/zj/Desktop/Baltam_IR/src/ir/ir_printer.cpp)

## 当前执行链的定位

当前执行链已经恢复，但恢复的位置是在：

- `UntypedSSA`

而不是回退到旧 hybrid/value-based IR。

当前解释器位于：

- [src/interpreter/interpreter.cpp](/home/zj/Desktop/Baltam_IR/src/interpreter/interpreter.cpp)

它的价值主要有三类：

- 验证 SSA 构建后的语义
- 给优化 pass 提供回归基线
- 给端到端样例提供最小可执行通路

## 为什么不回到旧解释器

原因仍然很直接：

- 旧 IR 已不再是当前主线
- 当前容器和节点语义已经统一到 `NonSSA / UntypedSSA`
- verifier、analysis、printer、SSA 构建器都围绕这条链路组织

如果重新接旧 hybrid 解释器，只会把已经收敛下来的阶段边界再次打散。

## 当前更值得推进的阶段

当前执行链已经具备最小闭环，因此下一阶段更值得做的是：

### Phase 1

扩大 `UntypedSSA` 解释器的覆盖范围和回归测试。

### Phase 2

在 `UntypedSSA` 上增加正式优化 pass。

### Phase 3

决定是否在 `main.cpp` 上增加可选的执行模式，而不是只打印 IR。

### Phase 4

再进入 `TypedSSA`、profile 和 LLVM IR lowering。

## 当前结论

当前仓库的近期目标已经不是“恢复执行链”，因为这件事在 SSA 层已经完成了第一版。

现在真正需要推进的是：

- 把 SSA 执行链从“可用”推进到“可回归、可扩展、可优化”

而不是重新讨论旧 hybrid 执行模型。
