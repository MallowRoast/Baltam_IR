# Baltam_IR SSA 方案整理

## 当前架构

当前仓库的正式 IR 主线是：

`M 源码 -> AST -> non-SSA IR`

这里的 non-SSA IR 已经落地在：

- [src/ir/ir.h](/home/zj/Desktop/Baltam_IR/src/ir/ir.h)
- [src/lowering/lowering.cpp](/home/zj/Desktop/Baltam_IR/src/lowering/lowering.cpp)

当前没有正式 SSA IR，也没有任何 `phi`、`ValueRef`、旧 hybrid/value-based 节点残留在主链里。

## 长期目标

当前建议的总路线是：

`M 源码 -> AST -> non-SSA IR -> untyped SSA IR -> typed SSA IR -> LLVM IR`

含义如下：

### 1. non-SSA IR

职责：

- 承接 AST lowering
- 构建规范 CFG
- 保留源码变量名和 lowering 临时量
- 不负责 SSA

### 2. untyped SSA IR

职责：

- 作为中端优化 IR
- 显式值流
- 显式 `phi` 或等价合流机制
- 不再以源码名字作为执行语义核心

### 3. typed SSA IR

职责：

- 在热点路径上加入类型信息、guard 和 specialization
- 作为 LLVM lowering 的直接输入

### 4. LLVM IR

职责：

- 作为热点后端 IR
- 面向更低层优化和机器码生成

## 当前为什么不直接做 SSA

当前 non-SSA IR 刚刚稳定下来，接下来最需要的是：

- verifier
- analysis
- 规范 CFG

而不是立刻把 lowering 改成 SSA。

更准确地说：

- lowering 负责生成 non-SSA CFG IR
- analysis + 中端 pass 负责把 non-SSA IR 变成 SSA IR

## 5 个基础 analysis

后续从 non-SSA IR 走向 untyped SSA IR，最关键的是下面 5 个 analysis。

### 1. CFGAnalysis

输入：

- 一个已经 basic well-formed 的 `Function`
- 依赖：
  - `entry_block()`
  - `blocks()`
  - `predecessors()`
  - `successors()`

输出：

- `reachable_blocks`
- `reverse_postorder`
- `postorder`
- `rpo_index`
- `is_reachable(BasicBlock*)`

作用：

- 给所有前向/反向数据流分析提供统一遍历顺序
- 给 dead block 清理、dominator、liveness 提供基础

### 2. DominatorTree

输入：

- `Function`
- `CFGAnalysis::Result`

输出：

- `idom`
- `children`
- `dominates(A, B)`
- `strictly_dominates(A, B)`

作用：

- 为 SSA rename 提供支配关系
- 为 `DominanceFrontier` 提供基础

### 3. DominanceFrontier

输入：

- `Function`
- `CFGAnalysis::Result`
- `DominatorTree::Result`

输出：

- `frontier_of(BasicBlock*)`
- 或 `std::unordered_map<BasicBlock*, std::vector<BasicBlock*>>`

作用：

- 决定某个名字在哪些合流块需要插入 `phi`

### 4. Liveness

输入：

- `Function`
- `CFGAnalysis::Result`

第一版建议分析域：

- 源码层变量名
- lowering 临时量名

输出：

- `live_in[block]`
- `live_out[block]`

作用：

- 为 `Pruned SSA` 避免插入无意义 `phi`

### 5. DefUse

输入：

- `Function`

第一版建议分析域：

- `NamedValue.name`

输出：

- `defs_of_name`
- `uses_of_name`
- `def_blocks_of_name`

作用：

- 支持 `BuildPrunedSSA`
- 支持后续 DCE 和简单传播

## BuildPrunedSSA 的输入

从 SSA 构建角度，真正关键的输入是：

- 规范 CFG
- `CFGAnalysis`
- `DominatorTree`
- `DominanceFrontier`
- `Liveness`
- 名字级 def/use 信息

也就是说：

- 这 5 个 analysis 本身不会“产出 SSA IR”
- 它们是 `BuildPrunedSSA` 的前置条件

正确表述应是：

`non-SSA IR -> analyses -> BuildPrunedSSA -> untyped SSA IR`

## PassManager / AnalysisManager

当前仓库已经有骨架：

- [src/analysis/analysis_manager.h](/home/zj/Desktop/Baltam_IR/src/analysis/analysis_manager.h)
- [src/optimizer/pass.h](/home/zj/Desktop/Baltam_IR/src/optimizer/pass.h)
- [src/optimizer/pass_manager.h](/home/zj/Desktop/Baltam_IR/src/optimizer/pass_manager.h)
- [src/optimizer/optimize.h](/home/zj/Desktop/Baltam_IR/src/optimizer/optimize.h)

当前状态：

- 有 `FunctionAnalysisManager`
- 有 `PreservedAnalyses`
- 有 `FunctionPassManager`
- 还没有任何正式 pass

这套骨架已经够支撑后续 analysis 和 SSA pass。

## 推荐实施顺序

### Step 1

先把当前 non-SSA IR、打印器和 verifier 稳定住。

### Step 2

补齐 5 个基础 analysis 的 `Result` 接口和最小实现。

### Step 3

实现独立的 `BuildPrunedSSA`，输入 `Function`，输出新的 SSA `Function`。

### Step 4

在 untyped SSA IR 上落第一批优化：

- SimplifyCFG
- ConstantFold
- DCE

### Step 5

在解释器或 profile 层面收集热点与类型信息。

### Step 6

把热点 untyped SSA IR specialized 成 typed SSA IR，再 lower 到 LLVM IR。

## 当前不建议做的事

- 不要让 lowering 直接生成 SSA
- 不要在 non-SSA IR 上继续叠加旧 hybrid 语义
- 不要把 typed SSA 和 LLVM lowering 提前到 analysis 之前
- 不要把 SSA 改造和解释器重写绑成一次性大改
