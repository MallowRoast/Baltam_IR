# Baltam_IR DominatorTree 说明

本文说明当前 `DominatorTree` analysis 的定义、输入约束和结果结构。

当前实现位于：

- [src/analysis/dominator_tree.h](/home/zj/Desktop/Baltam_IR/src/analysis/dominator_tree.h)
- [src/analysis/dominator_tree.cpp](/home/zj/Desktop/Baltam_IR/src/analysis/dominator_tree.cpp)

它运行在已经通过 verifier 的显式 `NonSSA` CFG 上，直接消费 `Function::entry_block()`、`blocks()` 以及块间前驱/后继关系。

## 支配的基础概念

支配相关概念都建立在“从函数入口块出发的可达 CFG”上。

下面默认：

- `entry` 表示函数入口块
- `A`、`B` 表示两个基本块

### 1. `A` 支配 `B`

记作：

- `dominates(A, B)`

含义是：

- 从 `entry` 到 `B` 的每一条路径，都必须经过 `A`

直觉上可以理解为：

- `A` 是到达 `B` 之前无法绕过的关口

直接结论：

- 每个可达块都支配自己
- `entry` 支配所有可达块
- 不可达块不参与当前支配关系定义

### 2. `A` 严格支配 `B`

记作：

- `strictly_dominates(A, B)`

含义是：

- `A` 支配 `B`
- 且 `A != B`

也就是说，它是把“块支配自己”这种平凡情况排除掉之后的版本。

### 3. `B` 的立即支配者

记作：

- `idom(B)`
- `immediate_dominator(B)`

含义是：

- 在所有严格支配 `B` 的块中，离 `B` 最近的那个块

它是“支配链上最靠近 `B` 的父节点”。

性质：

- 入口块没有 immediate dominator
- 每个可达非入口块都恰好有一个唯一的 immediate dominator

### 4. 支配树

把每个可达非入口块都连到自己的 `idom` 上，就得到一棵树：

- 根是 `entry`
- 每个非入口可达块都有唯一父节点
- 父子边就是 `idom(parent) -> child`

这棵树就叫 `DominatorTree`。

### 5. 支配树的孩子

若：

- `idom(B) = A`

则：

- `B` 是 `A` 在支配树上的一个直接孩子

也就是说，`children[A]` 表示“所有以 `A` 为 immediate dominator 的块”。

## 示例

### 1. 线性 CFG

```text
entry -> body -> exit
```

支配关系：

- `entry` 支配 `body`
- `entry` 支配 `exit`
- `body` 支配 `exit`

immediate dominator：

- `idom(entry) = nullptr`
- `idom(body) = entry`
- `idom(exit) = body`

### 2. 菱形 CFG

```text
       -> then ->
entry            merge
       -> else ->
```

支配关系：

- `entry` 支配 `then`
- `entry` 支配 `else`
- `entry` 支配 `merge`
- `then` 不支配 `merge`
- `else` 不支配 `merge`

原因是：

- 到 `merge` 有两条路径
- 其中一条不会经过 `then`
- 另一条不会经过 `else`

所以：

- `idom(merge) = entry`

### 3. 循环 CFG

```text
entry -> header -> body -> header
             \
              -> exit
```

支配关系：

- `entry` 支配 `header`
- `header` 支配 `body`
- `header` 支配 `exit`
- `body` 不支配 `header`

所以：

- `idom(header) = entry`
- `idom(body) = header`
- `idom(exit) = header`

## 当前 DominatorTree 的定位

当前 DominatorTree 是紧跟在 CFGAnalysis 之后的第二个正式函数级 analysis。

它的职责是：

- 在入口可达子图上建立 immediate dominator 关系
- 建立支配树的孩子列表
- 提供 `dominates(...)` 和 `strictly_dominates(...)` 查询

它不负责：

- 重建 CFG
- 处理不可达块的支配语义
- 直接计算 dominance frontier
- 直接构建 SSA

也就是说，DominatorTree 的角色是：

- “提供支配关系”

而不是：

- “直接做 SSA 变换”

## 输入与前置条件

DominatorTree 的输入是：

- `Function`
- `CFGAnalysis::Result`

也就是说，它依赖：

- 结构合法的 CFG
- 入口可达块的 `reverse_postorder`
- `rpo_index`
- `is_reachable(...)`

这些都已经由：

- verifier
- CFGAnalysis

提供好了。

当前实现里，DominatorTree 通过：

- `analysis_manager.get<CFGAnalysis>(function)`

显式获取这个前置 analysis。

## 当前文件布局

当前实现位于：

- [src/analysis/dominator_tree.h](/home/zj/Desktop/Baltam_IR/src/analysis/dominator_tree.h)
- [src/analysis/dominator_tree.cpp](/home/zj/Desktop/Baltam_IR/src/analysis/dominator_tree.cpp)

对应测试位于：

- [test/dominator_tree_test.cpp](/home/zj/Desktop/Baltam_IR/test/dominator_tree_test.cpp)

## 当前接口

当前接口如下：

```cpp
class DominatorTree {
public:
    struct Result {
        std::unordered_map<const BasicBlock*, const BasicBlock*> idom;
        std::unordered_map<const BasicBlock*, std::vector<const BasicBlock*>> children;

        const BasicBlock* immediate_dominator(const BasicBlock* block) const;
        const std::vector<const BasicBlock*>& children_of(const BasicBlock* block) const;
        bool dominates(const BasicBlock* dominator, const BasicBlock* block) const;
        bool strictly_dominates(const BasicBlock* dominator, const BasicBlock* block) const;
    };

    Result run(Function& function, FunctionAnalysisManager& analysis_manager) const;
};
```

这里的设计原则是：

- `idom` 是核心支配信息
- `children` 是支配树视图，避免后续反复从 `idom` 反建
- 查询接口都只读，返回 `const BasicBlock*`

## 每个成员的语义

### 1. `idom`

类型：

- `std::unordered_map<const BasicBlock*, const BasicBlock*>`

含义：

- 某个可达块到其 immediate dominator 的映射

约定：

- 入口块会出现在 `idom` 中，但其值为 `nullptr`
- 不可达块不会出现在 `idom` 中

这是整个 DominatorTree 结果里最核心的数据。

### 2. `children`

类型：

- `std::unordered_map<const BasicBlock*, std::vector<const BasicBlock*>>`

含义：

- 支配树上的直接孩子列表

约定：

- 所有可达块都会作为 key 出现
- 没有孩子的块，其 value 是空向量
- 不可达块不会出现在 `children` 中

它本质上可以由 `idom` 间接重建，但保留它可以让后续：

- `DominanceFrontier`
- SSA rename

直接按树向下遍历，而不必每次扫描全部块。

### 3. `immediate_dominator(...)`

语义：

- 返回块的 immediate dominator

约定：

- `nullptr` 输入返回 `nullptr`
- 不可达块返回 `nullptr`
- 入口块返回 `nullptr`

### 4. `children_of(...)`

语义：

- 返回支配树上的直接孩子列表

约定：

- `nullptr` 输入返回空列表
- 不可达块返回空列表
- 无孩子块返回空列表

### 5. `dominates(...)`

语义：

- 判断 `dominator` 是否支配 `block`

当前约定：

- `nullptr` 相关输入返回 `false`
- 不可达块不参与支配关系，返回 `false`
- 可达块总是支配自己

当前实现方式是：

- 从 `block` 沿 `idom` 一直向上走
- 如果能走到 `dominator`，则返回 `true`

### 6. `strictly_dominates(...)`

语义：

- 判断 `dominator` 是否严格支配 `block`

当前实现方式是：

- `dominator != block && dominates(dominator, block)`

## 当前算法

当前实现采用经典的 iterative immediate dominator 算法。

它依赖：

- `reverse_postorder`
- `rpo_index`

并通过 fixpoint 迭代不断收敛 `idom`。

### Step 1

先取：

- `cfg = analysis_manager.get<CFGAnalysis>(function)`

如果 `cfg.reverse_postorder` 为空，直接返回空结果。

### Step 2

把入口块初始化为：

- `idom(entry) = nullptr`

### Step 3

按 `reverse_postorder` 遍历所有非入口可达块。

对每个块 `B`：

- 枚举它的所有前驱
- 跳过不可达前驱
- 跳过当前还没有 `idom` 的前驱

然后：

- 先拿第一个可用前驱作为 `new_idom`
- 再对其他可用前驱反复做 `intersect(...)`

### Step 4

若某轮迭代中任意块的 `idom` 发生变化，就继续下一轮。

直到整轮没有变化为止，说明达到不动点。

### Step 5

收敛之后，再从 `idom` 反建：

- `children`

也就是把每个非入口块挂到自己的 immediate dominator 下面。

## `intersect(...)` 的含义

`intersect(lhs, rhs)` 的作用是：

- 在当前 `idom` 结果下，找到 `lhs` 和 `rhs` 在支配树上的最近公共祖先

当前实现借助：

- `rpo_index`

让两个“手指”沿 `idom` 链不断向上跳，直到相遇。

也就是说，`intersect(...)` 本质上是在当前支配树近似结果上做：

- common dominator meet

它是整个迭代算法的核心。

## 为什么依赖 `reverse_postorder`

因为在 forward analysis 里：

- `reverse_postorder` 通常能让前驱块尽量早于后继块出现

这样 `idom` 迭代会更快收敛，也更符合经典 dominator 算法的写法。

如果没有 `rpo_index`，`intersect(...)` 就很难高效地比较两个块在 RPO 中的高低关系。

## 对不可达块的处理

当前实现明确只在入口可达子图上建立支配关系。

因此：

- 不可达块不会出现在 `idom`
- 不可达块不会出现在 `children`
- `dominates(unreachable, X)` 返回 `false`
- `dominates(X, unreachable)` 返回 `false`

这个约定和当前 CFGAnalysis 的行为保持一致。

## 为什么 `children` 值得保留

严格来说，`children` 可以由 `idom` 反向推导出来。

但当前仍然保留它，原因是：

- DominanceFrontier 很容易用到“支配树孩子”
- SSA rename 通常需要沿支配树向下 DFS
- 如果每次都从 `idom` 现算孩子列表，会重复扫描所有块

所以这里和 CFGAnalysis 的“平衡版”思路一致：

- 保留最常用、最直接的派生视图
- 不保留过多可间接推导的信息

## 当前测试覆盖

当前测试位于：

- [test/dominator_tree_test.cpp](/home/zj/Desktop/Baltam_IR/test/dominator_tree_test.cpp)

覆盖的 CFG 形状包括：

### 1. 线性 CFG

验证：

- `idom` 形成链
- `children` 形成链
- `dominates` / `strictly_dominates` 符合直觉

### 2. 菱形 CFG

验证：

- `merge` 的 `idom` 是 `entry`
- 分支块不会单独支配合流块

### 3. 循环 CFG

验证：

- `header` 是循环体和退出块的 immediate dominator
- 回边不会破坏收敛

### 4. 含不可达块的 CFG

验证：

- 不可达块不出现在支配树结果中
- 不可达块不参与 `dominates(...)`

## 和后续 analysis 的关系

当前 DominatorTree 是后续这些组件的直接前置：

- `DominanceFrontier`
- SSA rename
- `construct_untyped_ssa_module(...)`

其中最直接的下一步就是：

- `DominanceFrontier`

因为：

- frontier 的定义本身就建立在支配关系之上

## 当前结论

当前仓库里的 DominatorTree 可以概括为：

- 输入：
  - `Function`
  - `CFGAnalysis::Result`
- 输出：
  - `idom`
  - `children`
  - `dominates(...)`
  - `strictly_dominates(...)`
- 范围：
  - 只覆盖入口可达子图
- 作用：
  - 为 `DominanceFrontier` 和 SSA 构建提供支配关系基础

它是从 `CFGAnalysis` 走向当前 SSA 构建器的第二块核心 analysis。
