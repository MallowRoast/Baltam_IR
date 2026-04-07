# Baltam_IR SSA 方案整理

## 当前架构

当前仓库的实际 IR 主线已经是：

`M 源码 -> AST -> non-SSA IR -> analyses -> untyped SSA IR`

对应实现包括：

- non-SSA lowering：
  - [src/lowering/lowering.cpp](/home/zj/Desktop/Baltam_IR/src/lowering/lowering.cpp)
- 统一 IR 定义：
  - [src/ir/ir.h](/home/zj/Desktop/Baltam_IR/src/ir/ir.h)
- 5 个基础 analysis：
  - [src/analysis/cfg_analysis.cpp](/home/zj/Desktop/Baltam_IR/src/analysis/cfg_analysis.cpp)
  - [src/analysis/dominator_tree.cpp](/home/zj/Desktop/Baltam_IR/src/analysis/dominator_tree.cpp)
  - [src/analysis/dominance_frontier.cpp](/home/zj/Desktop/Baltam_IR/src/analysis/dominance_frontier.cpp)
  - [src/analysis/liveness.cpp](/home/zj/Desktop/Baltam_IR/src/analysis/liveness.cpp)
  - [src/analysis/def_use.cpp](/home/zj/Desktop/Baltam_IR/src/analysis/def_use.cpp)
- SSA 构建器：
  - [src/optimizer/construct_untyped_ssa.cpp](/home/zj/Desktop/Baltam_IR/src/optimizer/construct_untyped_ssa.cpp)

也就是说，当前仓库已经不再是“只有 non-SSA，没有正式 SSA”的状态。

## 当前已落地的 SSA 能力

当前已经落地的部分包括：

- `UntypedSSA` 节点集合
- `ValueId / ValueRef`
- `phi` 区域
- 分阶段 verifier
- non-SSA 到 untyped SSA 的整体转换
- untyped SSA 打印
- untyped SSA 解释执行

其中当前的 `construct_untyped_ssa_module(...)` 本质上就是一版基于：

- `CFGAnalysis`
- `DominatorTree`
- `DominanceFrontier`
- `Liveness`
- `DefUse`

的 SSA 构建器。

它当前具备这些行为特征：

- 输入必须是 `NonSSA`
- 输出是新构造的 `UntypedSSA` 模块
- `phi` 插入依赖 frontier + liveness
- 缺失定义会物化成 `SSAUndefNode`
- 当前不支持含不可达块的函数

## 长期目标

长期分层仍然建议保持：

`M 源码 -> AST -> non-SSA IR -> untyped SSA IR -> typed SSA IR -> LLVM IR`

含义如下：

### 1. non-SSA IR

职责：

- 承接 AST lowering
- 保留源码变量名和 lowering 临时量名
- 构建规范 CFG

### 2. untyped SSA IR

职责：

- 作为当前统一的中端 IR
- 显式值流和 `phi`
- 承接解释执行、优化和 profile 前的语义归一化

### 3. typed SSA IR

职责：

- 在热点路径上加入类型信息、guard 和 specialization
- 作为 LLVM lowering 的直接输入

### 4. LLVM IR

职责：

- 作为热点后端 IR
- 面向更低层优化和机器码生成

## 当前为什么仍然不让 lowering 直接生成 SSA

虽然当前已经有 SSA，但阶段边界仍应保持清晰：

- lowering 负责生成 `NonSSA`
- analysis + `construct_untyped_ssa_module(...)` 负责构造 `UntypedSSA`

这样做的原因没有变：

- 降低 lowering 的复杂度
- 让 CFG、名字级 analysis、SSA 构建和优化各自独立
- 让 verifier 和测试能分别覆盖 non-SSA 与 SSA 阶段

## 当前已经有的基础设施

当前仓库已经具备：

- `FunctionAnalysisManager`
- `PreservedAnalyses`
- `FunctionPassManager`
- 一组已落地的 analysis
- 一个独立的 SSA 构建器

也就是说，当前还缺的不是“SSA 骨架”，而是：

- 正式优化 pass
- `TypedSSA`
- LLVM IR lowering
- 更完整的 profile / JIT 路线

## 当前下一步

更合理的下一步是：

### Step 1

把 `UntypedSSA` 解释器和端到端回归测试继续扩充。

### Step 2

在 `UntypedSSA` 上落第一批正式优化 pass，例如：

- SimplifyCFG
- ConstantFold
- DCE

### Step 3

明确 profile 信息如何附着在 `UntypedSSA` / `TypedSSA` 边界上。

### Step 4

再把热点 `UntypedSSA` specialized 成 `TypedSSA`，并继续 lower 到 LLVM IR。

## 当前不建议做的事

- 不要让 lowering 直接生成 SSA
- 不要重新引入旧 hybrid/value-based 语义
- 不要在还没有正式优化管线前把问题提前推到 LLVM / JIT
- 不要把 `TypedSSA` 设计和当前 `UntypedSSA` 解释器实现绑成一次性大改
