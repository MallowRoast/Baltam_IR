# Baltam_IR DominanceFrontier 说明

本文说明当前 `DominanceFrontier` analysis 的定义、算法前提和使用位置。

当前实现位于：

- [src/analysis/dominance_frontier.h](/home/zj/Desktop/Baltam_IR/src/analysis/dominance_frontier.h)
- [src/analysis/dominance_frontier.cpp](/home/zj/Desktop/Baltam_IR/src/analysis/dominance_frontier.cpp)

它运行在已经通过 verifier 的 `NonSSA` CFG 上，依赖 `CFGAnalysis` 与 `DominatorTree` 的结果，直接服务于当前 SSA 构建器。

## DominanceFrontier 的基础概念

### 1. 直觉含义

`DominanceFrontier` 描述的是：

- 某个块的支配影响在哪些块开始“失效”

更具体地说，若一个块 `X` 支配某个块 `Y` 的某个前驱，但 `X` 又不能严格支配 `Y` 本身，
那么 `Y` 就位于 `X` 的支配边界上。

### 2. 常见定义

记作：

- `DF(X)`

定义为：

- `DF(X) = { Y | X dominates a predecessor of Y, and X does not strictly dominate Y }`

直观解释：

- `Y` 是一个“控制流合流点”
- `X` 的支配影响能到达 `Y` 的某条入口
- 但 `X` 又不足以完全包住 `Y`

因此 `Y` 就是 `X` 的 dominance frontier 成员。

### 3. 菱形 CFG 示例

```text
       -> then ->
entry            merge
       -> else ->
```

这里：

- `merge ∈ DF(then)`
- `merge ∈ DF(else)`

因为：

- `then` 支配 `merge` 的一个前驱
- 但 `then` 不支配 `merge`

同理：

- `else` 也不支配 `merge`

但：

- `merge ∉ DF(entry)`

因为 `entry` 严格支配 `merge`。

### 4. 循环 CFG 示例

```text
entry -> header -> body -> header
             \
              -> exit
```

这里一个很关键的现象是：

- `header ∈ DF(header)`

这不是异常，而是循环 CFG 的正常结果。

原因是：

- `header` 支配 `body`
- `body` 是 `header` 的一个前驱
- 但 `header` 不严格支配自己

因此 loop header 常常会出现在自己的 dominance frontier 中。

## 它和 SSA 的关系

DominanceFrontier 的最直接用途是：

- 为 `phi` 插入提供候选块

若某个名字在块 `X` 定义过，那么：

- `DF(X)` 是第一批可能需要插入 `phi` 的块

如果一个名字有多个定义点，则还需要进一步做：

- iterated dominance frontier

但那已经属于当前 SSA 构建器的工作，不属于这个 analysis 本身。

## 当前 DominanceFrontier 的定位

当前 DominanceFrontier 是：

- 建立在 `CFGAnalysis + DominatorTree` 之上的函数级 analysis

它的职责是：

- 为每个可达块计算它的 frontier 块列表

它不负责：

- 直接做 `phi` 插入
- 直接处理名字级 def/use
- 计算 iterated frontier
- 直接构建 SSA

也就是说，当前 DominanceFrontier 的角色是：

- “提供块级 frontier 信息”

而不是：

- “直接做 SSA 变换”

## 输入与前置条件

DominanceFrontier 的输入是：

- `Function`
- `CFGAnalysis::Result`
- `DominatorTree::Result`

也就是说，它依赖：

- 结构合法的 CFG
- 入口可达块集合
- `reverse_postorder / rpo_index`
- `idom`

这些都已经由：

- verifier
- CFGAnalysis
- DominatorTree

提供好了。

## 当前文件布局

当前实现位于：

- [src/analysis/dominance_frontier.h](/home/zj/Desktop/Baltam_IR/src/analysis/dominance_frontier.h)
- [src/analysis/dominance_frontier.cpp](/home/zj/Desktop/Baltam_IR/src/analysis/dominance_frontier.cpp)

对应测试位于：

- [test/dominance_frontier_test.cpp](/home/zj/Desktop/Baltam_IR/test/dominance_frontier_test.cpp)

## 当前接口

当前接口如下：

```cpp
class DominanceFrontier {
public:
    struct Result {
        std::unordered_map<const BasicBlock*, std::vector<const BasicBlock*>> frontier;

        const std::vector<const BasicBlock*>& frontier_of(const BasicBlock* block) const;
    };

    Result run(Function& function, FunctionAnalysisManager& analysis_manager) const;
};
```

这里的设计原则是：

- 只保留“块 -> frontier 块列表”这一核心结果
- 不提前混入 iterated frontier、名字级结果或 `phi` 信息
- `frontier_of(...)` 负责对空块和不可达块返回空列表

## 每个成员的语义

### 1. `frontier`

类型：

- `std::unordered_map<const BasicBlock*, std::vector<const BasicBlock*>>`

含义：

- 每个可达块对应的 dominance frontier 列表

约定：

- 所有可达块都会作为 key 出现
- frontier 为空的块，其 value 是空向量
- 不可达块不会出现在 `frontier` 中

### 2. `frontier_of(...)`

语义：

- 返回某个块的 dominance frontier

约定：

- `nullptr` 输入返回空列表
- 不可达块返回空列表
- frontier 为空的块返回空列表

## 当前算法

当前实现采用第一版最直接的“前驱向上爬”算法。

核心思路是：

- 只对“有多个可达前驱的块”做处理
- 这些块是 CFG 里的合流点
- 从每个可达前驱沿 `idom` 链往上爬
- 一直爬到当前块的 `idom` 之前为止
- 途中经过的每个块，都把当前合流块加入自己的 frontier

### Step 1

先获取：

- `cfg = analysis_manager.get<CFGAnalysis>(function)`
- `dom = analysis_manager.get<DominatorTree>(function)`

### Step 2

为每个可达块初始化空 frontier。

### Step 3

对每个可达块 `Y`：

- 收集它的所有可达前驱
- 如果可达前驱数少于 2，跳过

因为：

- 前驱数少于 2 的块不是合流点
- 不会形成 dominance frontier

### Step 4

取：

- `idom_y = immediate_dominator(Y)`

然后对每个可达前驱 `P`：

- `runner = P`
- 当 `runner != idom_y` 时：
  - 把 `Y` 加入 `DF(runner)`
  - `runner = idom(runner)`

### Step 5

由于实现过程中会先用 set 去重，最后再把每个块的 frontier：

- 转回 vector
- 按 `cfg.rpo_index` 排序

这样做的好处是：

- 结果稳定
- 测试更容易写
- 调试输出更容易比较

## 为什么使用“前驱向上爬”算法

对于当前仓库阶段，这个算法最合适，原因是：

- 直接依赖现有 `idom`
- 实现短
- 易于验证
- 足够支撑后续 SSA 构建

当前并不需要一开始就引入更复杂的树形 DP 版本。

## 对不可达块的处理

当前实现和 CFGAnalysis / DominatorTree 保持一致：

- 只在入口可达子图上建立 frontier
- 不可达块不出现在 `frontier`
- `frontier_of(unreachable)` 返回空列表

## 当前测试覆盖

当前测试位于：

- [test/dominance_frontier_test.cpp](/home/zj/Desktop/Baltam_IR/test/dominance_frontier_test.cpp)

覆盖的 CFG 形状包括：

### 1. 线性 CFG

验证：

- 所有块的 frontier 都为空

### 2. 菱形 CFG

验证：

- `DF(then) = { merge }`
- `DF(else) = { merge }`
- `DF(entry)` 为空

### 3. 循环 CFG

验证：

- `header ∈ DF(header)`
- `header ∈ DF(body)`

### 4. 含不可达块的 CFG

验证：

- 不可达块不出现在 frontier 结果里

## 和后续 analysis / SSA 的关系

当前 DominanceFrontier 是后续这些步骤的直接前置：

- iterated dominance frontier
- `construct_untyped_ssa_module(...)`
- `phi` 插入

但它自己仍然保持块级、结构级，不掺入名字级语义。

## 当前结论

当前仓库里的 DominanceFrontier 可以概括为：

- 输入：
  - `Function`
  - `CFGAnalysis::Result`
  - `DominatorTree::Result`
- 输出：
  - `block -> frontier blocks`
- 范围：
  - 只覆盖入口可达子图
- 作用：
  - 为后续 `phi` 插入和 SSA 构建提供 frontier 信息

它是从 `DominatorTree` 走向当前 SSA 构建器的下一个关键 analysis。
