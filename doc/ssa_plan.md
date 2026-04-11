# Baltam_IR SSA 方案整理

本文记录当前仓库的 SSA 分层现状，以及后续 `TypedSSA` 的位置。

当前主线已经稳定到：

`M 源码 -> AST -> NonSSA -> analyses -> UntypedSSA`

对应实现主要在：

- [src/lowering/lowering.cpp](/home/zj/Desktop/Baltam_IR/src/lowering/lowering.cpp)
- [src/analysis](/home/zj/Desktop/Baltam_IR/src/analysis)
- [src/optimizer/construct_untyped_ssa.cpp](/home/zj/Desktop/Baltam_IR/src/optimizer/construct_untyped_ssa.cpp)
- [src/ir/ir.h](/home/zj/Desktop/Baltam_IR/src/ir/ir.h)

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

这里还有一条和圆括号应用相关的阶段边界需要固定下来：

- `A(...) = B` 可以在 lowering 阶段直接识别成下标写入语义
- 表达式位置的 `A(...)` 不能在 lowering 阶段直接识别成下标读取语义

原因是：

- 赋值左值里的 `A(...)` 已经被 AST 的赋值语境唯一化，不再和普通函数调用混淆
- 但表达式位置的 `A(...)` 仍然可能是函数调用、函数句柄调用，或者矩阵/元胞等对象的圆括号取值

因此当前 IR 设计选择是：

- lowering 把 `A(...) = B` 唯一化成 `__ir_paren_set__`
- lowering 对表达式位置或语句位置的 `A(...)`，会先按静态名字绑定分类成 direct / indirect call：
  - parser 已知是变量，或 lowering 已知该名字属于当前函数的用户变量，则走 indirect
  - 否则保留 direct
- indirect call 再由解释器在运行时根据 callee 的动态类型判断这次到底是函数调用还是 `block get`

也就是说，`block_set` 可以静态区分；`block_get` 仍然必须动态区分，但 `A(...)` 是否先走 indirect call 已经会利用静态名字信息提前收窄。

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

其中 `ConstantFold` 的第一批高收益目标应优先覆盖：

- 纯常量的一元 / 二元运算
- 纯常量的 `horzcat/vertcat`，例如 `[1 2 3]`、`[1; 2; 3]`
- 纯常量的 `colon` 表达式，例如 `1:3`、`1:2:5`

### Step 3

明确 profile 信息如何附着在 `UntypedSSA` / `TypedSSA` 边界上。

### Step 4

再把热点 `UntypedSSA` specialized 成 `TypedSSA`，并继续 lower 到 LLVM IR。

## 当前不建议做的事

- 不要让 lowering 直接生成 SSA
- 不要重新引入旧 hybrid/value-based 语义
- 不要在还没有正式优化管线前把问题提前推到 LLVM / JIT
- 不要把 `TypedSSA` 设计和当前 `UntypedSSA` 解释器实现绑成一次性大改
